/* policy_tap.h — THE TEACHER TAP (N1, S0, Deliverable 1). Canon CLAUDE_v2.md §9.8/§19.4;
 * runs/neural_policy_design.md §B.1 (DAgger data-gen), §E.2 (env/tap philosophy), App-G (the socket).
 *
 * WHAT IT DOES. While GM_MPPI (the distillation TEACHER) is active, at each 50 Hz guidance tick it
 * appends ONE fixed-layout binary row = [ the LEGAL raw observation ingredients (policy_obs.h) that
 * the eventual GM_NEURAL will consume ] + [ the EXECUTED command mppi_execute actually emitted
 * (a_lat[0], a_lat[1], throttle — the post-blend values) ]. The offline PyTorch trainer regresses
 * the command on the observation (imitation): trainer/train_s0.py. The row layout is mirrored, field
 * for field, in trainer/rowformat.py.
 *
 * THE D-014 LESSON — INSTRUMENT WITHOUT TOUCHING. The tap is a PURE, READ-ONLY observation:
 *   - no RNG draw, no state write, no effect on the command, no effect on sim.step ordering;
 *   - fwrite of a snapshot taken AFTER the command is fully resolved for the tick.
 * Therefore the sim path is byte-identical whether the flag is present or absent — verified by the
 * byte-equality gates (TERMINAL 194, AERO --mppi 44/60, run-1), and the same run twice produces an
 * identical log (same SHA256).
 *
 * LIFECYCLE (main.c): fopen the file ONCE per process (binary "wb"); attach the FILE* + (seed,run)
 * to each Sim's PolicyTap AFTER sim_init (which memsets it to disarmed); fclose once at process end.
 * Absent flag => tap.f == NULL => the fwrite site is skipped => byte-identical.
 *
 * PROVENANCE (§8.1, ABSOLUTE). Every logged observation ingredient comes from policy_build_obs,
 * which reads only the nav view + atmo_eval + the §8.1-legal target/health. Nothing from
 * wind_world / wind_filt / truth-target enters a row. The executed command is read from gcmd
 * (what control_step consumes) — the imitation target.
 */
#ifndef BL_POLICY_TAP_H
#define BL_POLICY_TAP_H

#include <stdio.h>
#include <stdint.h>
#include "state.h"
#include "guidance.h"
#include "policy_obs.h"

/* ---- THE ROW FORMAT (FROZEN; mirror in trainer/rowformat.py) --------------------------------------
 * All fields are IEEE-754 float64, little-endian, tightly packed, NO padding. One row per logged tick.
 * POLICY_TAP_ROW_N doubles/row => POLICY_TAP_ROW_BYTES bytes/row. No file header — the reader knows
 * the layout from this constant (row count == file_size / POLICY_TAP_ROW_BYTES).
 *
 *   col  0        : t          sim time [s]
 *   col  1        : seed       master seed (as f64)         } provenance / grouping (train/val split
 *   col  2        : run        run index  (as f64)          }   is BY RUN, and the held-out law keys on seed)
 *   col  3..2+N   : o[NPOBS_N] the raw observation ingredients, policy_obs.h index order (OBS_*)
 *   col  3+N      : a_lat[0]   EXECUTED world lateral accel x [m/s^2]  (gcmd, post-blend)  } the
 *   col  4+N      : a_lat[1]   EXECUTED world lateral accel y [m/s^2]  (gcmd, post-blend)  } imitation
 *   col  5+N      : throttle   EXECUTED throttle [0 or ENG_THR_MIN..1] (gcmd)              } LABEL a*
 *
 *   col 6+N..15+N : rt[10]     the TEACHER-CONTEXT theta (GM_RFLY only; 0 otherwise)  } NOT an obs
 *
 * THE TEACHER-CONTEXT BLOCK (2026-07-25, oracle-distill). Under GM_RFLY the flown law is the native
 * reactive stack scaled by a per-scenario theta the CEM searched (guidance_rfly.h RT_*). That theta
 * is the SECOND half of the AlphaZero structure: a net that predicts it from the observation becomes
 * the CEM's warm-start PRIOR (compute reduction), which is what makes live replans affordable. It is
 * logged here because it is free to capture now and would otherwise cost a whole second farm.
 *
 * IT IS NOT AN OBSERVATION AND MUST NEVER BE FED TO THE STUDENT. Theta is PRIVILEGED teacher context —
 * the CEM chose it by flying candidates through the real plant with the actual disturbance realization,
 * i.e. by seeing the future. Putting it in the student's input would be a §8.1 provenance violation and
 * exactly the "assist term" directive 6 forbids. It lives in its own block, past the action columns,
 * so the trainer's obs slice cannot reach it by accident.
 *
 * With NPOBS_N=30 (v1): 3 meta + 30 obs + 3 action = 36 doubles = 288 bytes/row.
 * With NPOBS_N=39 (v2, 2026-07-25): 3 + 39 + 3 + 10 theta = 55 doubles = 440 bytes/row.
 * The row width is derived from NPOBS_N, so a socket widening re-widens the row automatically —
 * but a dataset written against an OLDER width is NOT readable by the new reader and vice versa.
 * trainer/rowformat.py detects the width from the file size and refuses a mismatched dataset. */
#define POLICY_TAP_N_META    3
#define POLICY_TAP_N_ACT     3
#define POLICY_TAP_N_THETA   10                  /* == RFLY_N_THETA (guidance_rfly.h) */
#define POLICY_TAP_ROW_N     (POLICY_TAP_N_META + NPOBS_N + POLICY_TAP_N_ACT + POLICY_TAP_N_THETA)
#define POLICY_TAP_ROW_BYTES (POLICY_TAP_ROW_N * (int)sizeof(double))

/* The tap handle carried on the Sim. Disarmed = {f=NULL} (memset in sim_init). Set post-init. */
typedef struct {
    FILE*    f;      /* append target; NULL => tap OFF (the fwrite site is skipped) */
    uint32_t seed;   /* logged into col 1 (provenance / held-out-law key) */
    uint32_t run;    /* logged into col 2 (train/val split is BY RUN) */
} PolicyTap;

/* Emit ONE row for the current guidance tick. Call from the GM_MPPI gtick block AFTER the command is
 * fully resolved (post ignition-latch, so engine_on/n_eng/ign_timer reflect this tick) and AFTER
 * mppi_step/mppi_execute (so gcmd.a_lat/throttle are the EXECUTED post-blend command). `nav` is the
 * nav view for this tick; `g` is gcmd. Pure/read-only: no RNG, no state writes. No-op when tap->f is
 * NULL (flag absent) — the byte-equality guarantee. */
static inline void policy_tap_write(const PolicyTap* tap, const State* nav, const GuidanceCmd* g,
                                    const PolicyHist* hist){
    if(!tap || !tap->f) return;
    double row[POLICY_TAP_ROW_N];
    row[0] = nav->t;                 /* NOTE: nav->t == truth t (pass-through), the tick time */
    row[1] = (double)tap->seed;
    row[2] = (double)tap->run;
    double o[NPOBS_N];
    policy_build_obs(nav, g, hist, o);
    for(int i=0;i<NPOBS_N;i++) row[POLICY_TAP_N_META + i] = o[i];
    row[POLICY_TAP_N_META + NPOBS_N + 0] = g->a_lat[0];   /* EXECUTED */
    row[POLICY_TAP_N_META + NPOBS_N + 1] = g->a_lat[1];   /* EXECUTED */
    row[POLICY_TAP_N_META + NPOBS_N + 2] = g->throttle;   /* EXECUTED */
    /* teacher context: the RAW theta being flown. A non-RFLY row leaves gcmd.rt memset to zero, and
     * ALL-ZERO is an unambiguous "no teacher context" sentinel — a real theta can never be all-zero
     * (nine of the ten slots are multipliers bounded at >= 0.30; only RT_TGTLEAD may sit at 0). Do
     * NOT substitute rt_gain()'s 1.0 default here: that would silently claim slot 8 was 1.0 when its
     * identity is 0.0, i.e. it would write a theta that was never flown. */
    {   int base = POLICY_TAP_N_META + NPOBS_N + POLICY_TAP_N_ACT;
        for(int i=0;i<POLICY_TAP_N_THETA;i++) row[base+i] = g->rt[i];   }
    fwrite(row, sizeof(double), POLICY_TAP_ROW_N, tap->f);
}

#endif /* BL_POLICY_TAP_H */
