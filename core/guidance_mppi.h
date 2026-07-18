/* guidance_mppi.h — HIER MPPI CPU controller (track 4-B, M4). CLAUDE_v1.md §9.2-9.6.
 *
 * Model-Predictive Path-Integral control over the SLOW hierarchical channels
 * (throttle + 2 world-lateral-accel). The §8.3 attitude-PD + AoA-hold allocation
 * (control_step) and the shared EOM (dynamics_deriv) run INSIDE each rollout at an
 * RK4 25 ms control step, so MPPI plans only 3 sluggish channels and K=256 converges
 * on CPU. Verified vs Williams T-RO 2018 + canon §9.2-9.6.
 *
 * DETERMINISM (sacred): every noise draw comes from Philox (rng.h) keyed by
 * (seed, RNG_MPPI) with lane = rollout id and counter = (replan_index, t*NCH+ch).
 * No wall-clock, no atomics, fixed pairwise-tree reductions. Same seed + same state
 * => bit-identical plan. The GM_MPPI branch NEVER perturbs the GM_HOVERSLAM path.
 *
 * The E3 entry_supervisor stays ABOVE this controller (sim.c). MPPI owns the
 * unpowered aero-descent divert + the landing burn only.
 */
#ifndef BL_GUIDANCE_MPPI_H
#define BL_GUIDANCE_MPPI_H

#include "state.h"
#include "guidance.h"
#include "scenario.h"
#include "dynamics.h"   /* EnvCtx, MassProps */

/* ---- planner dimensions (compile-time; tuning knobs) ---- */
#ifndef MPPI_H
#define MPPI_H   200          /* horizon knots */
#endif
#ifndef MPPI_K
#define MPPI_K   256          /* rollouts per replan (bump to 512/1024 if plateau) */
#endif
#define MPPI_NCH 3            /* channels: throttle, a_lat_x, a_lat_y */
#define MPPI_DT  0.025        /* control-step / rollout RK4 dt [s] (§ design pt 2) */
#ifndef MPPI_REPLAN_DECIM
#define MPPI_REPLAN_DECIM 5   /* full solve every Nth 50 Hz guidance tick (=10 Hz); hold plan between */
#endif

/* Persistent solver state: warm-started mean + config + telemetry. Lives for the
 * whole descent (one per Sim). Holds NO wall-clock and NO hidden RNG state. */
typedef struct {
    double ubar[MPPI_H][MPPI_NCH];  /* warm-started nominal control sequence */
    double lambda;                   /* temperature (ESS-servoed) */
    uint32_t seed;                   /* master seed (from Sim) */
    uint32_t replan;                 /* full-solve counter -> Philox counter hi word */
    uint32_t gtick;                  /* 50 Hz guidance-tick counter (replan cadence) */
    double   ignite_h;               /* precomputed landing-burn ignition altitude [m] (per replan) */
    int      scenario;
    int      inited;
    /* last-solve telemetry */
    double   ess, beta, cmin;
    int      n_feasible;
} MppiState;

/* Initialize the planner for a run. Zeroes the warm-start (glide-ish nominal). */
void mppi_init(MppiState* M, uint32_t seed, int scenario);

/* One 50 Hz replan from the current (nav) plant state. Fills g->throttle,
 * g->a_lat[2], g->engine_cmd, g->deploy_cmd, g->mode=GM_MPPI, g->t_go,
 * g->solver_flags. env is the plant EnvCtx (fins_deployed, module_mask); wind is
 * treated as zero-mean inside the rollout (planner is nominal, robustness comes
 * from replanning). Executes the first knot; shifts the warm-start by one. */
void mppi_step(MppiState* M, const State* st, const EnvCtx* env, GuidanceCmd* g);

/* Cheap between-solve tick: emit the current warm-start first knot for the CURRENT state (so
 * ignition/deploy latch on the live altitude) and shift the plan by one. No rollouts. Used on the
 * guidance ticks between full solves (MPPI_REPLAN_DECIM). */
void mppi_execute(MppiState* M, const State* st, GuidanceCmd* g);

/* Landing-burn IGNITION ALTITUDE predictor (aero-aware, thrust-only suicide-burn shoot). PURE
 * function of state (reads only st->y) — matches guidance_hoverslam.c's ignition trigger and is
 * exactly what the MPPI rollout precomputes per replan (MppiState.ignite_h). Public so the
 * telemetry writer (fill_tlm) can populate the v3 BlTlmFixed.ignite_h field for ANY guidance mode
 * (hoverslam or MPPI) without a feedback path — it is a read-only diagnostic, directive-2 legal. */
double bl_predict_ignite_h(const State* st);

#endif /* BL_GUIDANCE_MPPI_H */
