/* guidance_theta.c — GM_RFLY theta-prior forward pass (R2, D-042). See guidance_theta.h.
 * Bit-deterministic by construction: every matrix-vector product is a FIXED sequence of scalar fp64
 * multiply-adds in a FIXED loop order (j outer, i inner) — no unordered reduction. The obs is built
 * by the SHARED policy_build_obs (the same features the RFLY tap logged), so the net at run time
 * consumes exactly what it was trained on. De-norm MIRRORS train_theta.py ThetaPrior.forward:
 *     theta = RT_LO + (RT_HI - RT_LO) * 0.5 * (tanh(logit) + 1)  in [RT_LO, RT_HI]. */
#include "guidance_theta.h"
#include "theta_policy_weights.h"
#include <math.h>

#if (TP_N_IN != NPOBS_N)
#  error "TP_N_IN (theta_policy_weights.h) != NPOBS_N (policy_obs.h): observation-socket drift"
#endif
#if (TP_N_LAYERS != 3)
#  error "guidance_theta.c is specialized to TP_N_LAYERS==3"
#endif
#if (TP_N_OUT != 10)
#  error "TP_N_OUT must be 10 (the RFLY theta dimensionality)"
#endif

int theta_policy_version(void){ return TP_VERSION; }

void theta_policy_forward(const double o_raw[], double th_out[10]){
    double o[TP_N_IN];
    for(int i=0;i<TP_N_IN;i++){ double v=o_raw[i]; if(!isfinite(v)) v=0.0; o[i]=(v-TP_IN_MU[i])/TP_IN_SD[i]; }
    double h0[TP_N_HID];
    for(int j=0;j<TP_N_HID;j++){ double a=TP_B0[j]; for(int i=0;i<TP_N_IN;i++) a+=TP_W0[j][i]*o[i];  h0[j]=tanh(a); }
    double h1[TP_N_HID];
    for(int j=0;j<TP_N_HID;j++){ double a=TP_B1[j]; for(int i=0;i<TP_N_HID;i++) a+=TP_W1[j][i]*h0[i]; h1[j]=tanh(a); }
    double h2[TP_N_HID];
    for(int j=0;j<TP_N_HID;j++){ double a=TP_B2[j]; for(int i=0;i<TP_N_HID;i++) a+=TP_W2[j][i]*h1[i]; h2[j]=tanh(a); }
    for(int k=0;k<TP_N_OUT;k++){
        double a=TP_B_OUT[k]; for(int j=0;j<TP_N_HID;j++) a+=TP_W_OUT[k][j]*h2[j];
        double u=tanh(a);
        double th=TP_RT_LO[k] + (TP_RT_HI[k]-TP_RT_LO[k])*0.5*(u+1.0);
        th_out[k] = isfinite(th) ? th : ((k==8)?0.0:1.0);   /* identity fallback (RT_TGTLEAD id=0) */
    }
}

void theta_policy_step(const State* nav, const GuidanceCmd* g, const PolicyHist* hist, double th_out[10]){
    double o[TP_N_IN];
    policy_build_obs(nav, g, hist, o);
    theta_policy_forward(o, th_out);
}
