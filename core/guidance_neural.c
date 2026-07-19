/* guidance_neural.c — GM_NEURAL inference. Pure C, fixed-order, fp64, no library, no atomics, no
 * threads. BIT-DETERMINISTIC by construction: every matrix-vector product is a FIXED sequence of
 * scalar fp64 multiply-adds in a FIXED loop order (j outer, i inner) — no unordered reduction, no
 * fma-fusion ambiguity (the C target compiles with /fp:precise; the plant .cu uses -fmad=false).
 * Canon: CLAUDE_v2.md §9.8; neural_policy_design.md §F.2 (VERBATIM discipline), §F.3 (why fp64).
 *
 * The observation is built by the SHARED policy_build_obs (policy_obs.h) — the SAME ingredient math
 * the teacher tap logs, so the net at run time consumes EXACTLY the features it was trained on
 * (the anti-drift guarantee). Normalization (mu,sd), weights, and de-norm ranges are the FROZEN
 * constants in neural_policy_weights.h. De-norm + clamp MIRROR trainer/train_s0.py Policy.forward
 * EXACTLY (a_lat = A_LAT_GAMUT*tanh(logit); throttle = ENG_THR_MIN + (1-ENG_THR_MIN)*0.5*(tanh+1)),
 * so trained θ behaves identically in C and in the trainer.
 *
 * This file is specialized to NP_N_LAYERS==3 (the S0 topology input->128->128->128->3); a deeper net
 * is a re-architecture event (new NP_VERSION). A compile-time check enforces the assumption.
 */
#include "guidance_neural.h"
#include "policy_obs.h"                 /* policy_build_obs + NPOBS_N (the shared feature math) */
#include "neural_policy_weights.h"      /* NP_VERSION, NP_N_*, NP_IN_MU/SD, NP_OUT_SCALE, weights+biases */
#include "constants.h"                  /* ENG_THR_MIN (cross-checked against NP_ENG_THR_MIN) */
#include <math.h>

/* The interface the net is architected against must match the tap's feature count and the header. */
#if (NP_N_IN != NPOBS_N)
#  error "NP_N_IN (neural_policy_weights.h) != NPOBS_N (policy_obs.h): observation-socket drift"
#endif
#if (NP_N_LAYERS != 3)
#  error "guidance_neural.c is specialized to NP_N_LAYERS==3; regenerate/generalize for a deeper net"
#endif
#if (NP_N_OUT != 3)
#  error "NP_N_OUT must be 3 (a_lat[0], a_lat[1], throttle) for the Tier-A action space"
#endif

BL_HD static inline double clampd(double x, double lo, double hi){
    return x<lo ? lo : (x>hi ? hi : x);
}

/* A_LAT_GAMUT lives in guidance_mppi.c (a static there); the frozen de-norm range is NP_OUT_SCALE[0/1]
 * (== A_LAT_GAMUT, baked into the header by the exporter). We clamp to the SAME magnitude the tanh
 * de-norm produces so the C clamp is a pure numerical backstop (parity with MPPI's ±A_LAT_GAMUT clamp,
 * the D-009 lesson). Read it from the header so C and the frozen artifact never disagree. */

int neural_policy_version(void){ return NP_VERSION; }

/* The bit-deterministic core: normalize -> 3 tanh layers (FIXED j-outer/i-inner) -> output head ->
 * de-norm + clamp. Reads o_raw[NP_N_IN] (the un-normalized policy_obs.h ingredients), writes
 * a_out = {a_lat0, a_lat1, throttle}. isfinite-guarded end to end (never emits NaN). */
BL_HD void neural_policy_forward(const double o_raw[], double a_out[3]){
    /* 1) frozen affine normalize (§C.3). NAN-harden the input (the plant is finite; this is belt+braces). */
    double o[NP_N_IN];
    for(int i=0;i<NP_N_IN;i++){
        double v = o_raw[i];
        if(!isfinite(v)) v = 0.0;                 /* clamp, never continue NaN (the isfinite guard) */
        o[i] = (v - NP_IN_MU[i]) / NP_IN_SD[i];
    }
    /* 2) layer 0: input -> hidden, tanh. FIXED j-outer / i-inner accumulation order. */
    double h0[NP_N_HID];
    for(int j=0;j<NP_N_HID;j++){
        double acc = NP_B0[j];
        for(int i=0;i<NP_N_IN;i++) acc += NP_W0[j][i]*o[i];
        h0[j] = tanh(acc);
    }
    /* 3) layer 1: hidden -> hidden, tanh. */
    double h1[NP_N_HID];
    for(int j=0;j<NP_N_HID;j++){
        double acc = NP_B1[j];
        for(int i=0;i<NP_N_HID;i++) acc += NP_W1[j][i]*h0[i];
        h1[j] = tanh(acc);
    }
    /* 4) layer 2: hidden -> hidden, tanh. */
    double h2[NP_N_HID];
    for(int j=0;j<NP_N_HID;j++){
        double acc = NP_B2[j];
        for(int i=0;i<NP_N_HID;i++) acc += NP_W2[j][i]*h1[i];
        h2[j] = tanh(acc);
    }
    /* 5) output head: hidden -> out (pre-tanh logits). */
    double a[NP_N_OUT];
    for(int k=0;k<NP_N_OUT;k++){
        double acc = NP_B_OUT[k];
        for(int j=0;j<NP_N_HID;j++) acc += NP_W_OUT[k][j]*h2[j];
        a[k] = acc;
    }
    /* 6) de-normalize + clamp to the PHYSICAL gamut. MIRRORS train_s0.py Policy.forward EXACTLY:
     *      a_lat_c  = A_LAT_GAMUT * tanh(logit)                                  (NP_OUT_SCALE[0/1])
     *      throttle = ENG_THR_MIN + (1 - ENG_THR_MIN) * 0.5 * (tanh(logit)+1)   -> [ENG_THR_MIN, 1]
     *    The clamps are numerical backstops. NAN-harden: any non-finite output => a safe neutral command. */
    double u0 = tanh(a[0]), u1 = tanh(a[1]), u2 = tanh(a[2]);
    double alat0 = NP_OUT_SCALE[0]*u0;
    double alat1 = NP_OUT_SCALE[1]*u1;
    double thr   = NP_ENG_THR_MIN + (1.0 - NP_ENG_THR_MIN)*0.5*(u2 + 1.0);
    if(!isfinite(alat0) || !isfinite(alat1) || !isfinite(thr)){
        a_out[0] = 0.0; a_out[1] = 0.0; a_out[2] = NP_ENG_THR_MIN;   /* honest-crash neutral, never NaN */
    } else {
        a_out[0] = clampd(alat0, -NP_OUT_SCALE[0], NP_OUT_SCALE[0]);
        a_out[1] = clampd(alat1, -NP_OUT_SCALE[1], NP_OUT_SCALE[1]);
        a_out[2] = clampd(thr, NP_ENG_THR_MIN, 1.0);
    }
}

BL_HD void neural_policy_step(const State* nav, GuidanceCmd* g){
    /* build the legal observation (the SAME features the tap logs) — the shared policy_obs.h math —
     * then run the bit-deterministic forward pass and write ONLY the continuous channels. */
    double o[NP_N_IN], a[3];
    policy_build_obs(nav, g, o);
    neural_policy_forward(o, a);
    g->a_lat[0] = a[0];
    g->a_lat[1] = a[1];
    g->throttle = a[2];
    g->mode = GM_NEURAL;
    /* engine_cmd / n_eng / deploy_cmd stay on the analytic triggers (Tier A) — set by the sim.c
     * GM_NEURAL block (hoverslam_step's ignition + leg-deploy gate), exactly as GM_MPPI. */
}
