/* nav.c — measurement layer implementation. CLAUDE_v1.md §8.1.
 *
 * See nav.h for the contract. In one line: build a State copy per guidance tick whose
 * pose/motion (r, v, q, ω) is the measured estimate and whose every other field is truth,
 * driven entirely by the Philox RNG_NAV stream so it is deterministic and replayable.
 */
#include "nav.h"
#include "rng.h"
#include "constants.h"   /* DEG2RAD, GUIDANCE_DT */
#include <string.h>
#include <math.h>

/* ---- canon §8.1 noise magnitudes ---- */
static const double NAV_SIG_POS[3] = { 0.5, 0.5, 0.3 };  /* position σ world x,y,z [m] */
#define NAV_SIG_VEL      0.1                              /* velocity σ per axis [m/s] */
#define NAV_SIG_ATT_DEG  0.1                              /* attitude σ per axis [deg] */
/* gyro-bias random walk: σ of the per-step bias INCREMENT [rad/s per √tick]. Sized so the
 * bias wanders on the order of the attitude-noise scale over a descent (a few hundred ticks
 * -> ~a few ×0.001 rad/s ≈ sub-deg/s), a realistic MEMS-grade slow drift. A trigger-grade
 * value; the honest point is that ω carries a slowly-varying bias, not white noise. */
#define NAV_GYRO_RW      0.0005
/* INJECT_DISTURBANCE(sensor_bias): a small CONSTANT-per-run measurement bias added on top of
 * the noise when MOD_INJECT is active (§8.1: the injection acts on the measurement, not the
 * truth). Fixed per (seed,run) so injected runs replay bit-exact. Position bias [m]. */
#define NAV_INJ_POS_BIAS 0.3
#define NAV_INJ_VEL_BIAS 0.05

/* Philox counter/lane addressing. RNG_NAV is the dedicated stream (rng.h). We key by
 * (seed, RNG_NAV); the 128-bit counter carries (ctrlo=step_lo, ctrhi=step_hi, lane, sub).
 * `lane` separates channels (position/velocity/attitude/gyro) so no two draws alias; `sub`
 * inside rng_block adds a second independent index. Per-run separation is folded into ctrhi
 * so different runs at the same step never share a block. */
enum {
    NAV_LANE_POS  = 10,   /* + axis (0..2) */
    NAV_LANE_VEL  = 20,   /* + axis (0..2) */
    NAV_LANE_ATT  = 30,   /* + axis (0..2) */
    NAV_LANE_GYRO = 40    /* + axis (0..2) — random-walk increment */
};

/* One standard normal from the RNG_NAV stream for (step, run, lane). Box-Muller via rng.h.
 * The counter low word is the step; the high word folds in the run so per-run streams are
 * disjoint. lane picks the channel/axis. */
static inline double nav_normal(uint32_t seed, uint64_t step, uint32_t run, uint32_t lane){
    uint32_t ctrlo = (uint32_t)step;
    uint32_t ctrhi = (uint32_t)(step >> 32) ^ (run * 2654435761u + 0x9E3779B9u);
    return rng_normal(seed, RNG_NAV, ctrlo, ctrhi, lane);
}

void nav_init(NavState* nav, int module_mask, uint32_t seed, uint32_t run){
    memset(nav, 0, sizeof(*nav));
    nav->mode   = (module_mask & MOD_NAV_NOISY) ? NAV_NOISY : NAV_TRUTH;
    nav->inject = (module_mask & MOD_INJECT) ? 1 : 0;
    nav->seed   = seed;
    nav->run    = run;
    nav->gyro_bias[0] = nav->gyro_bias[1] = nav->gyro_bias[2] = 0.0;
    nav->inited = 1;
}

void nav_resync(const State* st, State* out){
    /* Preserve the perturbed kinematic estimate (world pos S_RX..S_RZ, vel S_VX..S_VZ,
     * quat S_QX..S_QW, body rate S_WX..S_WZ = flat indices 0..12), refresh EVERYTHING else
     * from live truth. Index range chosen from state.h's S_* enum ordering; S_WZ is the
     * last perturbed field before the mass/actuator block. In NAV_TRUTH the saved values
     * already equal truth, so this is a no-op-equivalent full copy. */
    double kin[S_WZ+1];
    for(int i=0;i<=S_WZ;i++) kin[i]=out->y[i];
    memcpy(out, st, sizeof(State));
    for(int i=0;i<=S_WZ;i++) out->y[i]=kin[i];
}

void nav_measure(NavState* nav, const State* st, uint64_t step, State* out){
    /* PASS-THROUGH is the whole State, always. In NAV_TRUTH we stop here: the view is a
     * byte copy of truth with zero RNG draws and no bias update — provably transparent
     * (the bit-identical acceptance gate). */
    memcpy(out, st, sizeof(State));
    if(nav->mode == NAV_TRUTH) return;

    double* y = out->y;
    const double* yt = st->y;

    /* ---- position: additive Gaussian, per-axis σ = [0.5,0.5,0.3] m (+ inject bias) ---- */
    for(int i=0;i<3;i++){
        double n = nav_normal(nav->seed, step, nav->run, NAV_LANE_POS + (uint32_t)i);
        y[S_RX+i] = yt[S_RX+i] + NAV_SIG_POS[i]*n;
    }
    /* ---- velocity: additive Gaussian, σ = 0.1 m/s per axis (+ inject bias) ---- */
    for(int i=0;i<3;i++){
        double n = nav_normal(nav->seed, step, nav->run, NAV_LANE_VEL + (uint32_t)i);
        y[S_VX+i] = yt[S_VX+i] + NAV_SIG_VEL*n;
    }

    /* ---- attitude: small-angle body-frame perturbation, σ = 0.1° per axis ----
     * Build δθ (body axes) from three independent draws, form the small-angle quaternion
     * dq = normalize([δθ/2, 1]) and right-multiply the true attitude (q_meas = q_true ⊗ dq)
     * so the perturbation is expressed in the BODY frame — the natural frame of an attitude
     * sensor. Small-angle => the exact-vs-linearized difference is negligible at 0.1°. */
    {
        double sig = NAV_SIG_ATT_DEG * DEG2RAD;
        double dtheta[3];
        for(int i=0;i<3;i++)
            dtheta[i] = sig * nav_normal(nav->seed, step, nav->run, NAV_LANE_ATT + (uint32_t)i);
        double dq[4] = { 0.5*dtheta[0], 0.5*dtheta[1], 0.5*dtheta[2], 1.0 };
        q_normalize(dq);
        double qt[4] = { yt[S_QX], yt[S_QY], yt[S_QZ], yt[S_QW] };
        double qm[4]; q_mul(qm, qt, dq);
        q_normalize(qm);
        y[S_QX]=qm[0]; y[S_QY]=qm[1]; y[S_QZ]=qm[2]; y[S_QW]=qm[3];
    }

    /* ---- angular rate: gyro output = truth + integrated random-walk bias ----
     * The bias WALKS (canon §8.1 "gyro bias random-walk"): each guidance tick it takes a
     * Gaussian step σ = NAV_GYRO_RW·√(dt-tick). This is the one term with memory — it lives
     * in NavState and is regenerated deterministically from (seed, step, run) so replay is
     * still exact. The measured ω is truth + this slowly-varying bias (no white term on top:
     * a rate gyro's dominant flight-relevant error is the bias, not sample noise). */
    {
        double sdt = sqrt(GUIDANCE_DT);
        for(int i=0;i<3;i++){
            double dn = nav_normal(nav->seed, step, nav->run, NAV_LANE_GYRO + (uint32_t)i);
            nav->gyro_bias[i] += NAV_GYRO_RW * sdt * dn;
            y[S_WX+i] = yt[S_WX+i] + nav->gyro_bias[i];
        }
    }

    /* ---- INJECT_DISTURBANCE(sensor_bias) — on the MEASUREMENT, never truth (§8.1) ----
     * A fixed-per-run constant bias, seeded from (seed,run) exactly like the plant-side
     * inject in sim.c (directive 4: injected runs replay bit-exact). Applied ONLY under
     * MOD_INJECT so the plain NAV_NOISY path is untouched. Direction from a per-run angle;
     * magnitude fixed. This is the "it matters" case canon called out: v0 never defined
     * where sensor_bias lands — it lands here. */
    if(nav->inject){
        double a = 6.2831853071795864 * rng_u01((uint32_t)(nav->seed + nav->run*2654435761u + 909u));
        double cx = cos(a), cy = sin(a);
        y[S_RX] += NAV_INJ_POS_BIAS * cx;
        y[S_RY] += NAV_INJ_POS_BIAS * cy;
        y[S_VX] += NAV_INJ_VEL_BIAS * cx;
        y[S_VY] += NAV_INJ_VEL_BIAS * cy;
    }
}
