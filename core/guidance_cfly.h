/* guidance_cfly.h — the OPTIMIZER-IN-THE-LOOP guidance mode (GM_CFLY, N2-S2, 2026-07-21).
 *
 * Provenance: the LODESTAR sandbox arc (opus/robust-oracle @ 2e16674). The finding: the compound
 * (engine-out x gust x moving deck x heave) landing is a KNIFE-EDGE — the guidance law's theta-basin
 * is +/-5% on 4 of 9 coordinates, so NO open-loop selection (fixed theta, lookup, or NN regression)
 * can fly it; but a solver finds the basin ~100%. So the solver runs IN the loop: an oracle-strength
 * CEM solve at t=0 (the mission plan), then small warm-started CEM replans every CFLY_REPLAN_DT
 * (the onboard replan). Sandbox proof: held-out compound 16/16 GOOD+ (12 PERFECT) vs nominal-theta
 * 4/16, theta-net 2/16.
 *
 * The law itself (cfly_law): max-Q/entry burn + unified ZEM/ZEV lateral + single-engine
 * distance-triggered hoverslam + COAST-RELIGHT (the hover-starve fix: min-throttle TWR>1 forbids a
 * slow hold, so a mistimed burn cuts the engine, coasts ballistic, relights when braking is needed).
 *
 * Determinism (house law): the plant RNG is a pure function of (seed, stream, step), so a copied
 * Sim replays bit-identically; the CEM sampler is a local xorshift seeded from (seed, step); the
 * replan is thus deterministic and replayable. GM_CFLY is default-OFF => every existing byte-gate
 * is untouched (goldens, selftest, TERMINAL x200, MPPI run-1). */
#ifndef BL_GUIDANCE_CFLY_H
#define BL_GUIDANCE_CFLY_H

#include "state.h"      /* State */
#include "guidance.h"   /* GuidanceCmd */

typedef struct Sim Sim;   /* forward (sim.h includes this header for CflyState) */

/* theta parameterization (identical to the sandbox so solutions transfer) */
enum { TH_HENTRY, TH_ENTHR, TH_VCUT, TH_EALAT, TH_FRES, TH_HLAND, TH_KR, TH_KV, TH_TGO, CFLY_N_THETA };

#define CFLY_REPLAN_DT 10.0   /* s between onboard replans (the t=0 plan is separate) */

typedef struct {
    double theta[CFLY_N_THETA];   /* the current tuning (the replan rewrites it) */
    double next_replan_t;         /* sim-time of the next replan [s] (0 => the big t=0 solve) */
    int    entry_done, land_on, coast_done;   /* law latches (per-run) */
    int    noreplan;              /* candidate-evaluation copy: fly fixed theta, never replan */
} CflyState;

/* one guidance tick of the theta-law (writes g: throttle/a_lat/engine_cmd/n_eng/deploy_cmd/
 * solver_flags). Reads the nav view + the Sim's target/deck sockets (gcmd.target_xy/target_vxy/
 * deck_z, deck_vz_live, eng_health) — the same legal fields the other modes consume. */
void cfly_law(Sim* s, const State* nav, GuidanceCmd* g);

/* the replan: warm-started local CEM from the CURRENT Sim state. Copies the Sim per candidate,
 * sim_runs each with the candidate theta (noreplan=1), cost-arbitrates, writes s->cfly.theta.
 * Deterministic (seeded xorshift + pure replay). big=1 => oracle-strength broad solve (t=0 plan);
 * big=0 => cheap warm tracking solve. */
void cfly_replan(Sim* s, int big);

#endif
