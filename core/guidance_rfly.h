/* guidance_rfly.h — GM_RFLY: the OPTIMIZER-IN-THE-LOOP over the NATIVE reactive stack (D-040 pivot).
 *
 * PROVENANCE. The direct port of the LODESTAR sandbox 3-phase law (GM_CFLY) is an honest NULL on
 * the main-tree plant: it fuel-starves even the CLEAN entry while the native hoverslam stack lands
 * the same cases with 2-3.3 t of margin (Phase-A data: runs/D040_draft.md, runs/cfly_diag2.txt).
 * The sandbox 16/16 certified the SEARCH ARCHITECTURE — oracle-strength solve at t=0, warm CEM
 * replans in flight — not that particular law. GM_RFLY keeps the architecture and aims it at the
 * law that IS fuel-feasible here: the entry-supervisor + hoverslam reactive stack, whose compound
 * failure mode is a LATERAL 35 m soft/upright miss — precisely a gain-tuning problem.
 *
 * THETA = 10 interpretable per-scenario overrides of the stack's frozen gains, MULTIPLIERS around
 * identity (RT_TGTLEAD is additive around 0). Identity theta == the shipped D-030 stack EXACTLY,
 * and elitism seeds candidate 0 with the warm start, so the t=0 population CONTAINS plain
 * hoverslam: the search can only match or beat the baseline, never regress it.
 *
 * CONTRACTS (mirrors guidance_cfly.h):
 *  - Default OFF (GM_RFLY unarmed) => rt_on stays 0 everywhere => every legacy mode byte-identical.
 *  - Deterministic: plant RNG pure-per-(seed,step); the CEM sampler is a seeded xorshift => replays.
 *  - Candidates: full-Sim copy, noreplan=1, fly the REAL plant to the horizon; cost arbitrates
 *    (sandbox-faithful cost_of — no fuel term).
 *  - The theta flows to the law through GuidanceCmd.rt (OpenMP-safe per-candidate). */
#ifndef BL_GUIDANCE_RFLY_H
#define BL_GUIDANCE_RFLY_H

struct Sim;
#include "state.h"   /* E5: the MLP gain policy reads the nav State (an anonymous typedef, so no forward declaration) */

#define RFLY_N_THETA 10
enum {
    RT_EKR    = 0,  /* × entry-divert KR (after the D-030 n_eng<3 re-auth)          */
    RT_EKV    = 1,  /* × entry-divert KV                                            */
    RT_EBANK  = 2,  /* × entry-divert bank cap (absolute ceiling 45 deg)            */
    RT_ADECEL = 3,  /* × A_DECEL       — divert deceleration profile aggressiveness */
    RT_TLEAD  = 4,  /* × T_LEAD        — cross-range reversal pre-emption           */
    RT_KDIV   = 5,  /* × Kvel schedule — divert seek/brake gain (fins-deployed)     */
    RT_KVNEAR = 6,  /* × KVEL_NEAR     — near-ground velocity-null boost            */
    RT_IGNM   = 7,  /* × LANDING_IGNITE_MARGIN — landing-burn ignition timing       */
    RT_TGTLEAD= 8,  /* + target_vxy lead ON THE SEEK term (identity 0; the D-038
                     *   redemption: lead the vdes seek — fades with lat_scale —
                     *   never the damping, which is what nulled D-038)             */
    RT_KV     = 9,  /* × Kv            — vertical profile tracking gain             */
};

typedef struct {
    double th[RFLY_N_THETA];
    double next_replan_t;   /* 0 => the big t=0 solve fires on the first gtick */
    int    noreplan;        /* candidates never replan (no recursion) */
    int    last_n_eng;      /* D-052: engine count at the previous gtick; 0 => not yet seeded.
                             * The replan cadence is PURELY PERIODIC at RFLY_REPLAN_DT=10 s while
                             * the fault fires at t in [4,18] s — so a fault at t=11 leaves the
                             * vehicle flying a THREE-ENGINE plan on two engines for nine seconds,
                             * mid-entry-burn. That is the suspected mechanism behind the blind
                             * arm's LOC 13, and n_eng is §4.3-legal sensed state, so reacting to
                             * it is not privilege. */
    /* E5 (2026-09-15): the runtime-loaded MLP gain policy's own memory — when the sensed engine
     * count last changed (feature 8, time since the fault). Separate from last_n_eng, which
     * rfly_event_due owns. t_n_eng_change < -1e8 means "never". */
    double t_n_eng_change;
    int    mlp_last_n_eng;
    double phi[12];          /* E7: the twelve legal features at the current replan (rfly_features) */
    double obs39[39];        /* E8: the FULL legal observation at the current replan (policy_build_obs,
                              *     exactly what the tap and every net consume). Filled in sim.c only
                              *     when the candidate log or the critic is armed. */
} RflyState;

#define RFLY_REPLAN_DT 10.0

void rfly_init(struct Sim* s);
void rfly_replan(struct Sim* s, int big);

/* ---- ASYNC live replans (N3 showcase; SERVE + --interactive ONLY — the §M2
 * determinism-waived context). The sync path (run/headless/serve-observer) is
 * untouched: async is armed only by cmd_serve when interactive is also on, so
 * every gate/golden flies the synchronous, bit-replayable replans.
 *   - rfly_set_async(1): arm (cmd_serve).
 *   - rfly_async_poll(s): called from the GM_RFLY gtick block instead of the sync
 *     replan — swaps in a completed worker theta (staging buffer, no tearing) and
 *     launches the next warm solve from a full-Sim snapshot when the cadence is
 *     due and no worker is in flight. Solve-in-progress => the sim keeps flying
 *     the CURRENT theta (the whole point: the stream never stalls). */
void rfly_set_async(int on);
int  rfly_async_on(void);
void rfly_async_poll(struct Sim* s);
/* D-052: 1 when the LEGAL sensed engine count changed since the last gtick AND
 * --rfly-event-replan is armed, so a stale plan is re-solved the moment the fault fires
 * rather than up to RFLY_REPLAN_DT later. Always updates last_n_eng, even when disarmed. */
int rfly_event_due(struct Sim* s, int n_eng_now);

/* E5 (2026-09-15): --rfly-mlp FILE — a small MLP gain policy over twelve legal features, loaded at
 * run time so the outcome optimiser (runs/e5_es_mlp.py) can evaluate candidates without an export
 * ceremony. theta = clamp(b2 + Wlin.phi + W2.tanh(W1.phi + b1)). Default off => byte-identical.
 * File: "nin nhid" then Wlin[10][nin], b2[10], W1[nhid][nin], b1[nhid], W2[10][nhid]. */
#define RFLY_MLP_NIN   12
#define RFLY_MLP_MAXH  32
extern int g_rfly_mlp_on;
extern int g_rfly_mlp_warm;   /* E6: the MLP as the search's warm start (search still on); default off => byte-identical */
int  rfly_load_mlp(const char* path);
void rfly_clamp_theta(double th[RFLY_N_THETA]);   /* defined in guidance_rfly.c; sim.c declares it locally too */
void rfly_features(struct Sim* s, const State* nav, double phi[RFLY_MLP_NIN]);   /* the twelve legal features (updates the engine-change memory) */
void rfly_mlp_theta(struct Sim* s, const State* nav, double th[RFLY_N_THETA]);
/* E7 (2026-09-15): --rfly-cand-log FILE — every candidate the CEM evaluates is written as one row of
 * 27 f64: t, seed, run, big, phi[12], theta[10], cost. The search's JUDGMENT, not its pick. */
#include <stdio.h>
extern FILE* g_rfly_cand_log;
/* E7: --rfly-critic FILE — the search's sampler unchanged, the plant rollouts replaced by a critic
 * Q(phi[12], theta[10]) -> log cost trained on the candidate log (runs/e7_train_critic.py). Sixty
 * forward passes per replan instead of sixty rollouts. Default off => byte-identical. */
/* E8 (2026-09-23): the critic rebuilt on the FULL observation. Input = obs39[39] + the CEM mean at
 * that iteration[10] + the candidate[10] = 59 channels; the file declares nin (<= MAXIN) so the
 * trainer may drop dead channels. Trained to RANK within a replan group, not to regress cost.
 * The E7 22-input critics are dead data; this loader refuses them. */
#define RFLY_OBS_N        39
#define RFLY_CRITIC_NIN   (RFLY_OBS_N + 2*RFLY_N_THETA)   /* 59 */
#define RFLY_CRITIC_MAXIN 64
#define RFLY_CRITIC_MAXH  256
extern int g_rfly_critic_on;
int  rfly_load_critic(const char* path);
void rfly_replan_critic(struct Sim* s, int big);

/* E8: --rfly-cand-design — beside the CEM's own population, evaluate a DESIGNED set at every
 * replan and log it: the replan's start mean, plus one-coordinate steps of +-0.5 and +-1.5 sd on
 * each of the ten gains (41 rollouts). Logged ONLY: they consume no RNG draws and never enter
 * elite selection, so the flight is byte-identical to the same run without the flag. */
extern int g_rfly_cand_design;

/* E8 candidate-log ROW (f64, little-endian), 71 columns. The E7 27-column format is retired.
 *   0 t   1 seed   2 run   3 big   4 iter   5 designed(0/1)
 *   6..44   obs39            45..54  mean_theta (the CEM mean this candidate was drawn around)
 *   55..64  cand_theta (clamped, as flown)
 *   65 cost   66 landed(0/1)   67 td_v   68 td_lat   69 td_tilt   70 fuel_margin */
#define RFLY_CAND_ROW 71

#endif
