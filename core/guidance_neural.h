/* guidance_neural.h — tier 3 GM_NEURAL: the learned guidance policy (N1, S0). CLAUDE_v2.md §9.8;
 * neural_policy_design.md §F.2 (the fixed-order forward pass), §F.4 (mode wiring), App-G (the socket).
 *
 * A direct feedback policy: pi_theta(legal observation) -> GuidanceCmd (a_lat[2] + throttle), evaluated
 * fresh every 50 Hz guidance tick. The observation is built by the SHARED policy_build_obs (policy_obs.h)
 * — the SAME legal features the teacher tap logs — normalized by the FROZEN (mu,sd) in
 * neural_policy_weights.h, run through a hand-rolled fixed-order fp64 MLP, de-normalized, and C-clamped
 * to the physical gamut. No malloc, no threads, no BLAS/cuDNN, no data-dependent branches beyond the
 * standard clamps -> BIT-DETERMINISTIC, memcmp-golden'd (the determinism export, §F).
 *
 * Ships DEAD behind --neural / GM_NEURAL: default OFF => the compiler folds it away => every existing
 * golden reproduces byte-exact (the §13.6 leak gate). With the NP_VERSION-0 placeholder weights it
 * flies badly (an honest crash) — the gate is that two --neural runs are BIT-IDENTICAL and finite.
 */
#ifndef BL_GUIDANCE_NEURAL_H
#define BL_GUIDANCE_NEURAL_H

#include "state.h"
#include "guidance.h"

/* One 50 Hz forward pass from the (nav) plant state. `nav` is nav_measure's output (the legal view);
 * `g` supplies the §8.1-legal target pose (g->target_xy, filled by sim.c) and receives the command:
 * g->a_lat[2], g->throttle, g->mode=GM_NEURAL. Ignition (engine_cmd) and legs stay on the analytic
 * triggers (Tier A) — set by the sim.c block that calls this, exactly as GM_MPPI. Pure/read-only:
 * no RNG, no state writes. isfinite-guarded: a non-finite forward pass clamps to a safe neutral
 * command (never propagates NaN), so a broken/absurd net produces an honest crash, not a hang. */
BL_HD void neural_policy_step(const State* nav, GuidanceCmd* g);

/* The PURE forward pass on an ALREADY-BUILT raw observation vector o_raw[NP_N_IN] (the policy_obs.h
 * ingredients, un-normalized). Writes the de-normalized, clamped command a_out = {a_lat0, a_lat1,
 * throttle}. This is the bit-deterministic core neural_policy_step wraps (it does obs-build then
 * calls this). EXPOSED so the selftest NP KAT can feed a hardcoded observation and assert a
 * bit-exact expected output. NP_N_IN is neural_policy_weights.h's input dimension. */
BL_HD void neural_policy_forward(const double o_raw[/*NP_N_IN*/], double a_out[3]);

/* The compiled-in policy version (NP_VERSION from neural_policy_weights.h), for HELLO/telemetry
 * provenance (a replay is attributable to the exact frozen policy that flew it). 0 = placeholder. */
int neural_policy_version(void);

#endif /* BL_GUIDANCE_NEURAL_H */
