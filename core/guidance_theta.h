/* guidance_theta.h — GM_RFLY theta-prior inference (R2, D-042). The SECOND net: obs(App-G v2) ->
 * theta_hat[10] driving the reactive stack's gains (gcmd.rt) each tick IN PLACE of the CEM.
 * Pure C, fixed-order fp64, no library — the same determinism discipline as guidance_neural.c.
 * Default OFF (only reached under --rfly-theta-net); the compiled artifact is byte-inert otherwise. */
#ifndef BL_GUIDANCE_THETA_H
#define BL_GUIDANCE_THETA_H

#include "state.h"
#include "guidance.h"
#include "policy_obs.h"   /* PolicyHist + policy_build_obs — the shared 39-D obs (train/fly parity) */

/* Fill th_out[10] (the RT-box gain multipliers, guidance_rfly.h RT_* order) from the legal obs.
 * `g` supplies the §8.1-legal target pose the obs offset is measured against (gcmd.target_xy),
 * exactly as neural_policy_step reads it. `hist` is the previous resolved tick (App-G v2 self-sensed
 * channel; NULL => those 9 features 0). Pure/read-only. isfinite-guarded: a non-finite forward pass
 * falls back to identity gains, so a broken net degrades to plain hoverslam, never a NaN cascade. */
void theta_policy_step(const State* nav, const GuidanceCmd* g, const PolicyHist* hist, double th_out[10]);

/* the PURE forward pass on an already-built raw obs vector (exposed for a self-check). */
void theta_policy_forward(const double o_raw[/*TP_N_IN*/], double th_out[10]);

int theta_policy_version(void);   /* TP_VERSION, 0 if the placeholder header is compiled */

#endif
