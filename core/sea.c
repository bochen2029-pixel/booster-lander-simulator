/* sea.c — see sea.h. Pierson-Moskowitz deck-motion spectrum, equal-energy sum-of-sines. */
#include "sea.h"
#include "rng.h"
#include "constants.h"
#include <math.h>

/* Pierson-Moskowitz (canon App-A.3): S(ω) = α g²/ω⁵ · exp(−β (ω0/ω)⁴),  ω0 = g/U19.5.
 * The cumulative energy F(ω) = exp(−β (ω0/ω)⁴) is a closed-form CDF — verified: dF/dω = S(ω)/m0 with
 * the total variance m0 = ∫₀^∞ S dω = α g²/(4 β ω0⁴). So the equal-energy quantiles invert in closed
 * form:  q = F(ω) ⇒ ω = ω0 · (β / −ln q)^(1/4).  And Hs = 4√m0 = 0.2092 · U19.5²/g (the standard P-M
 * relation), which we INVERT to parametrize by the operator-facing Hs: U19.5 = √(Hs g / 0.2092). */
#define SEA_ALPHA   8.1e-3
#define SEA_BETA    0.74
#define SEA_HS_MIN  0.05        /* guard the math (MOD_SEA gates the feature; this floors the spectrum) */
#define SEA_TWO_PI  6.283185307179586
/* pitch/roll: small tilt from the wave slope (a_k·k_wave, k_wave=ω²/g deep-water). Renderer/HELLO only in
 * Stage-1b — NOT fed to the contact normal (tilted-normal contact is the SEA-polish follow-up, §F). */
#define SEA_TILT_MAX  0.0436    /* clamp each of pitch/roll to ±2.5° (canon §4.4) */
#define SEA_PHI_PITCH 1.0471975512   /* π/3  — decorrelate pitch from heave */
#define SEA_PHI_ROLL  2.0943951024   /* 2π/3 — decorrelate roll from pitch */

void sea_init(SeaState* S, uint32_t seed, double Hs){
    if(Hs < SEA_HS_MIN) Hs = SEA_HS_MIN;
    S->Hs = Hs;
    double U  = sqrt(Hs * G0 / 0.2092);                 /* U19.5 from Hs */
    double w0 = G0 / U;                                  /* P-M scale ω0 = g/U19.5 */
    double amp = Hs / sqrt(8.0 * (double)SEA_N);         /* equal-energy ⇒ constant amp = √(2·m0/N) */
    for(int k=0;k<SEA_N;k++){
        double q = ((double)k + 0.5) / (double)SEA_N;    /* energy quantile in (0,1) */
        S->omega[k] = w0 * pow(SEA_BETA / (-log(q)), 0.25);
        S->amp[k]   = amp;
        uint32_t o[4]; rng_block(seed, RNG_SEA, (uint32_t)k, 0u, 0u, 0u, o);
        S->phase[k] = SEA_TWO_PI * rng_u01(o[0]);
    }
    /* Stage-1b: the deck is horizontally FIXED at the origin (pure heave). The ±3 m station-keeping
     * wander + feeding target_xy to guidance is Stage-1c — kept zero here so the horizontal behavior is
     * identical to a fixed pad and the heave physics is measured in isolation. Fields kept for 1c. */
    S->x0 = 0.0; S->y0 = 0.0;
    for(int j=0;j<2;j++){ S->wander_omega[j]=0.0; S->wander_amp[j]=0.0; S->wander_phase[j]=0.0; }
}

void sea_deck_pose(const SeaState* S, double t,
                   double* deck_z, double* deck_vz,
                   double deck_quat[4],
                   double* target_x, double* target_y){
    double z=0.0, vz=0.0, sx=0.0, sy=0.0;
    for(int k=0;k<SEA_N;k++){
        double ph = S->omega[k]*t + S->phase[k];
        double c  = cos(ph), s = sin(ph);
        z  += S->amp[k]*c;
        vz += -S->amp[k]*S->omega[k]*s;
        if(deck_quat){
            double kw = S->omega[k]*S->omega[k]/G0;      /* deep-water wavenumber (dispersion ω²=g·k) */
            double sl = S->amp[k]*kw;                     /* wave-slope contribution [rad] */
            sx += sl*cos(ph + SEA_PHI_PITCH);
            sy += sl*cos(ph + SEA_PHI_ROLL);
        }
    }
    if(deck_z)  *deck_z  = z;
    if(deck_vz) *deck_vz = vz;
    if(deck_quat){
        double pitch = sx, roll = sy;                     /* small angles */
        if(pitch> SEA_TILT_MAX) pitch= SEA_TILT_MAX; if(pitch<-SEA_TILT_MAX) pitch=-SEA_TILT_MAX;
        if(roll > SEA_TILT_MAX) roll = SEA_TILT_MAX; if(roll <-SEA_TILT_MAX) roll =-SEA_TILT_MAX;
        /* small-angle quat (xyzw): roll about world-x, pitch about world-y; normalized. */
        double qx=0.5*roll, qy=0.5*pitch, qz=0.0, qw=1.0;
        double n=sqrt(qx*qx+qy*qy+qz*qz+qw*qw);
        deck_quat[0]=qx/n; deck_quat[1]=qy/n; deck_quat[2]=qz/n; deck_quat[3]=qw/n;
    }
    /* horizontal station (Stage-1b: x0=y0=0, wander disabled ⇒ (0,0)) */
    double wx = S->wander_amp[0]*cos(S->wander_omega[0]*t + S->wander_phase[0]);
    double wy = S->wander_amp[1]*cos(S->wander_omega[1]*t + S->wander_phase[1]);
    if(target_x) *target_x = S->x0 + wx;
    if(target_y) *target_y = S->y0 + wy;
}
