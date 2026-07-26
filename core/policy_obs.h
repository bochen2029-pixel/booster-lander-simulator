/* policy_obs.h — THE SINGLE SOURCE of the learned-policy observation-ingredient math (N1, S0).
 *
 * Canon: CLAUDE_v2.md §8.1 (the wide socket + the PROVENANCE RULE), App-G (the frozen socket),
 * runs/neural_policy_design.md §C.1 (the field-by-field observation) + §F.2 (build_observation).
 *
 * WHY THIS FILE EXISTS (the anti-drift move). Two consumers must compute the SAME features from the
 * SAME legal inputs, bit-for-bit:
 *   (1) the TEACHER TAP  (--policy-log, main.c / sim.c) — logs (o, a*) rows for the offline trainer;
 *   (2) GM_NEURAL inference (guidance_neural.c, neural_policy_step) — feeds o to the frozen net.
 * If the tap logged one feature set and the net consumed another, the distilled policy would be
 * trained on inputs it never sees at run time — a silent, fatal skew. So the ingredient math lives
 * HERE, ONCE, as a header-only BL_HD function both include. (The design's "share the math via a small
 * common helper" option, chosen over "duplicated-with-comment".)
 *
 * THE PROVENANCE RULE IS ABSOLUTE (§8.1). Every ingredient below is computable from:
 *     the nav view (nav_measure output: perturbed r/v/q/w + truth pass-through of the rest)
 *   + atmo_eval(h)
 *   + the §8.1-legal TargetEstimate (nav->tgt) and EngineHealth (nav->eng_health)
 *   + the §8.1-legal target pose the guidance layer nulls to (gcmd->target_xy / target_vxy).
 * NOTHING from wind_world / wind_filt / the truth target / a hidden disturbance schedule may enter.
 * `wind_filt` sits in the nav pass-through (easy to reach, FORBIDDEN to touch) — it is NOT read here.
 *
 * The ORDER of features is a FROZEN INTERFACE (App-G). It is **APPEND-ONLY**: existing indices never
 * move or change meaning, and new ingredients are added at the END. A frozen policy consumes the
 * FIRST NP_N_IN features it was trained against, so widening the socket leaves every older
 * NP_VERSION bit-identical (NP_VERSION 6 reads o[0..29] and cannot tell the socket grew). Growing it
 * is still a re-architecture EVENT for the NEXT policy (new NP_VERSION + retrain + re-golden) — but
 * it is no longer a break for the current one. NP_N_IN must be <= NPOBS_N. The tap's row mirrors
 * these indices verbatim (documented in policy_tap.h and trainer/rowformat.py).
 *
 * Pure, read-only, no RNG, no state writes, no malloc, no branches on data beyond finite bookkeeping.
 */
#ifndef BL_POLICY_OBS_H
#define BL_POLICY_OBS_H

#include "state.h"
#include "guidance.h"
#include "vmath.h"
#include "dynamics.h"
#include "atmosphere.h"
#include "constants.h"       /* G0, R_EARTH — the §4.1 g(h) law, a legal pure function of altitude */
#include "guidance_mppi.h"   /* bl_predict_ignite_h — the aero-aware ignition-altitude foresight (legal) */

/* Number of RAW observation ingredients.
 *   v1 (NP_VERSION<=6): 30 — App-G ≈28 features; eng_health[3]/target_cov[3] expand.
 *   v2 (2026-07-25, the ORACLE-DISTILL re-architecture): +9 = 39 — the SELF-SENSED contingency
 *     channel. See the block comment at OBS_SFX below for why these nine and not others. */
#define NPOBS_N 39
#define NPOBS_N_V1 30        /* the frozen v1 prefix — NP_VERSION 6 reads exactly this much */

/* Fixed index map into the ingredient vector o[NPOBS_N]. These indices are the FROZEN socket order;
 * the tap row and neural_policy_weights.h (NP_IN_MU/SD) are laid out against them. */
enum {
    OBS_RXREL=0, OBS_RYREL,      /* 0,1  target-relative horizontal offset (r_xy - target_xy) [m]   */
    OBS_H,                       /* 2    height above deck  h = r_z - com - deck_z [m]              */
    OBS_VX, OBS_VY,              /* 3,4  world horizontal velocity [m/s]                            */
    OBS_VZ,                      /* 5    world vertical velocity [m/s]                              */
    OBS_ZB2W_X, OBS_ZB2W_Y, OBS_ZB2W_Z,  /* 6,7,8  body +Z axis expressed in world (tilt vector)   */
    OBS_WX, OBS_WY, OBS_WZ,      /* 9,10,11  body angular rate [rad/s]                              */
    OBS_PROP,                    /* 12   total propellant mass (lox+rp1) [kg]                       */
    OBS_MACH,                    /* 13   Mach = |v| / a(h)                                          */
    OBS_QBAR,                    /* 14   dynamic pressure 0.5*rho*|v|^2 [Pa]                        */
    OBS_FINS,                    /* 15   fins_deployed flag {0,1}                                   */
    OBS_ENGON,                   /* 16   engine_on flag {0,1}                                       */
    OBS_IGNT,                    /* 17   ign_timer [s] (<0 when off)                                */
    OBS_EH0, OBS_EH1, OBS_EH2,   /* 18,19,20  per-engine chamber-P health flags {0,1} (§4.6 LEGAL)  */
    OBS_RELIGHTS,                /* 21   relights_left                                              */
    OBS_IGNMARGIN,               /* 22   ignition-altitude margin  h - ignite_h [m]                 */
    OBS_TVX, OBS_TVY,            /* 23,24  target velocity estimate [m/s]                           */
    OBS_COVXX, OBS_COVYY, OBS_COVXY, /* 25,26,27  target covariance (xx,yy,xy) [m^2]                */
    OBS_TAGE,                    /* 28   target staleness [s]                                       */
    OBS_TVALID,                  /* 29   target_valid flag {0,1}   <-- END OF THE FROZEN v1 PREFIX  */

    /* ---- App-G v2 (2026-07-25): THE SELF-SENSED CONTINGENCY CHANNEL ------------------------------
     * WHY. The oracle-distill design's load-bearing claim (§1.5): the policy must NOT be told "engine
     * out" — it must INFER the contingency from how the vehicle mis-responds, the way a pilot feels it.
     * A failed engine announces itself twice in the measurable dynamics, and NEITHER signal is in the
     * v1 socket: (a) the achieved specific force drops below what the commanded throttle should have
     * produced (thrust deficit), and (b) the surviving-thrust centroid offset produces an INDUCED
     * ANGULAR ACCELERATION. `eng_health[3]` (OBS_EH0..2) is the cheat-sheet version of the same fact;
     * these are the honest sensor version. The operator's acceptance test is to HIDE the flags and
     * check the policy still lands — which is only possible if these channels exist.
     *
     * WHAT (and why not a pre-computed "residual"). We expose the ACHIEVED specific force and the LAST
     * EXECUTED COMMAND as separate features rather than differencing them here. The difference is a
     * linear combination of inputs — trivially learnable by the first layer — while doing it in C
     * would require an engine/atmosphere thrust model inside the observation path, coupling the socket
     * to the plant model and re-opening the exact drift risk this file exists to close. Model-free is
     * both safer and strictly more informative.
     *
     * PROVENANCE (§8.1, checked field by field). Specific force = dv/dt + g(h)ẑ, from two consecutive
     * NAV velocities and the §4.1 g(h) law (a pure function of altitude) — this is precisely what a
     * real IMU accelerometer reads, and it is derived ONLY from the nav view. Angular acceleration =
     * dω/dt from two consecutive NAV body rates. The last command is the vehicle's own output. Nothing
     * from wind_world / wind_filt / the truth target enters. All of it comes from PolicyHist below,
     * which is written AFTER each guidance tick resolves — never from the plant's hidden state. */
    OBS_SFX, OBS_SFY, OBS_SFZ,   /* 30,31,32  ACHIEVED specific force, world [m/s^2] (the accelerometer) */
    OBS_WDOTX, OBS_WDOTY, OBS_WDOTZ, /* 33,34,35  body angular acceleration dω/dt [rad/s^2]           */
    OBS_ALATPX, OBS_ALATPY,      /* 36,37  the LAST EXECUTED lateral command [m/s^2]                  */
    OBS_THRP                     /* 38     the LAST EXECUTED throttle [0 or ENG_THR_MIN..1]           */
};

/* ---- PolicyHist — the ONE-TICK memory the v2 ingredients need ------------------------------------
 * Carried on the Sim (NOT on State): it must never enter the RK4 vector, never be integrated, and
 * never pass through nav_measure. It is pure write-after-the-fact bookkeeping — no legacy code path
 * reads it — so every existing golden is byte-identical by construction.
 *
 * FIRST-TICK CONVENTION (pinned; both the tap and inference get it from this one function, so there
 * is no trainer mirror to skew): before the first update, or across a non-positive dt, all nine v2
 * ingredients are ZERO. A zero specific force is physically impossible in flight, so the net can
 * learn "this is the first tick" if it matters; more importantly the value is DEFINED, never garbage. */
typedef struct {
    int    valid;        /* 0 until the first policy_hist_update */
    double t;            /* tick time these were captured at [s] */
    double v[3];         /* nav world velocity at that tick [m/s] */
    double w[3];         /* nav body angular rate at that tick [rad/s] */
    double a_lat[2];     /* the EXECUTED lateral command at that tick [m/s^2] */
    double throttle;     /* the EXECUTED throttle at that tick */
} PolicyHist;

/* Capture the resolved tick. Call ONCE per guidance tick, AFTER the executed command is final
 * (i.e. after the ignition latch AND after the D-009 wind-trim integral, which still edits a_lat).
 * Pure bookkeeping: no RNG, no plant write. */
BL_HD static inline void policy_hist_update(PolicyHist* h, const State* nav, const GuidanceCmd* g){
    h->valid = 1;
    h->t = nav->t;
    h->v[0]=nav->y[S_VX]; h->v[1]=nav->y[S_VY]; h->v[2]=nav->y[S_VZ];
    h->w[0]=nav->y[S_WX]; h->w[1]=nav->y[S_WY]; h->w[2]=nav->y[S_WZ];
    h->a_lat[0]=g->a_lat[0]; h->a_lat[1]=g->a_lat[1];
    h->throttle=g->throttle;
}

/* Build the RAW (un-normalized) observation-ingredient vector from the LEGAL nav view + the legal
 * target pose the guidance layer nulls to. `nav` is nav_measure's output (r/v/q/w perturbed, the
 * rest truth pass-through). `g` supplies the §8.1-legal target pose (gcmd.target_xy/target_vxy,
 * filled by sim.c from the seeded/nominal target slot — NOT the hidden truth trajectory). `hist` is
 * the previous resolved tick (App-G v2; may be NULL or invalid — then the v2 block is all zero).
 * Writes NPOBS_N doubles into o. No normalization here (that is the frozen (mu,sd) step, applied by
 * the consumer just before the net / logged raw by the tap so the trainer computes (mu,sd) itself). */
BL_HD static inline void policy_build_obs(const State* nav, const GuidanceCmd* g,
                                          const PolicyHist* hist, double o[NPOBS_N]){
    const double* y = nav->y;

    /* mass / CoM for height-above-deck and the ignition-margin (mass is truth pass-through — the
     * flight computer knows its own propellant load; NAV_NOISY does not perturb it, §8.1). */
    MassProps mp; mass_props(y[S_MLOX], y[S_MRP1], 0, 0, &mp);

    /* target-relative horizontal offset — the quantity guidance nulls (§4.5 one-line substitution).
     * gcmd.target_xy is the LEGAL current target pose (origin for FIXED => o == r_xy, v1 behavior). */
    o[OBS_RXREL] = y[S_RX] - g->target_xy[0];
    o[OBS_RYREL] = y[S_RY] - g->target_xy[1];

    /* height above the (possibly moving) deck: the vertical state guidance actually uses. */
    o[OBS_H] = y[S_RZ] - mp.com - nav->tgt.deck_z;

    o[OBS_VX] = y[S_VX];
    o[OBS_VY] = y[S_VY];
    o[OBS_VZ] = y[S_VZ];

    /* attitude as the body +Z axis in world (zb2w) — 3 numbers, avoids the quaternion double-cover
     * discontinuity that hurts NN training (§C.3). */
    { double zb[3]={0.0,0.0,1.0}, zw[3]; q_rot(zw, &y[S_QX], zb);
      o[OBS_ZB2W_X]=zw[0]; o[OBS_ZB2W_Y]=zw[1]; o[OBS_ZB2W_Z]=zw[2]; }

    o[OBS_WX] = y[S_WX];
    o[OBS_WY] = y[S_WY];
    o[OBS_WZ] = y[S_WZ];

    o[OBS_PROP] = y[S_MLOX] + y[S_MRP1];

    /* Mach + dynamic pressure from |v| and the atmosphere at the current altitude (aero/thrust
     * regime). atmo_eval is a legal pure function of altitude (§8.1). */
    { AtmoOut atm; atmo_eval(y[S_RZ], &atm);
      double sp = sqrt(y[S_VX]*y[S_VX] + y[S_VY]*y[S_VY] + y[S_VZ]*y[S_VZ]);
      o[OBS_MACH] = (atm.a > 1e-9) ? (sp/atm.a) : 0.0;
      o[OBS_QBAR] = 0.5*atm.rho*sp*sp; }

    o[OBS_FINS]  = nav->fins_deployed ? 1.0 : 0.0;
    o[OBS_ENGON] = nav->engine_on ? 1.0 : 0.0;
    o[OBS_IGNT]  = nav->ign_timer;

    /* engine-health flags — THE engine-out signal, §4.6-legal (chamber-P analog, nav pass-through). */
    o[OBS_EH0] = nav->eng_health[0] ? 1.0 : 0.0;
    o[OBS_EH1] = nav->eng_health[1] ? 1.0 : 0.0;
    o[OBS_EH2] = nav->eng_health[2] ? 1.0 : 0.0;

    o[OBS_RELIGHTS] = (double)nav->relights_left;

    /* ignition-altitude margin: the aero-aware ignition foresight MPPI precomputes (read-only, legal
     * pure function of nav->y — §9.2 bl_predict_ignite_h). A strong feature (§C.1 #24). */
    o[OBS_IGNMARGIN] = o[OBS_H] - bl_predict_ignite_h(nav);

    /* target estimate: velocity, covariance, staleness, validity (§8.1 socket — the uncertainty the
     * policy can hedge on; nominal-constant at N0, varied by the N2/N3 curriculum). */
    o[OBS_TVX]   = nav->tgt.target_vxy[0];
    o[OBS_TVY]   = nav->tgt.target_vxy[1];
    o[OBS_COVXX] = nav->tgt.target_cov[0];
    o[OBS_COVYY] = nav->tgt.target_cov[1];
    o[OBS_COVXY] = nav->tgt.target_cov[2];
    o[OBS_TAGE]  = nav->tgt.target_age;
    o[OBS_TVALID]= nav->tgt.target_valid ? 1.0 : 0.0;

    /* ---- App-G v2: the self-sensed contingency channel (see the enum block comment). ------------
     * dt is the elapsed time since the previous RESOLVED guidance tick — normally GUIDANCE_DT, but
     * computed from the timestamps rather than assumed, so a decimated or resumed tick is still
     * correct. The <=0 branch also covers the very first tick of a run (hist->valid==0). */
    {
        double dt = (hist && hist->valid) ? (nav->t - hist->t) : 0.0;
        if(dt > 1e-9){
            /* specific force = dv/dt - a_gravity, with a_gravity = -g(h) ẑ  (§4.1; dynamics.c:233) */
            double gh = G0*(R_EARTH/(R_EARTH+y[S_RZ]))*(R_EARTH/(R_EARTH+y[S_RZ]));
            o[OBS_SFX] = (y[S_VX]-hist->v[0])/dt;
            o[OBS_SFY] = (y[S_VY]-hist->v[1])/dt;
            o[OBS_SFZ] = (y[S_VZ]-hist->v[2])/dt + gh;
            o[OBS_WDOTX] = (y[S_WX]-hist->w[0])/dt;
            o[OBS_WDOTY] = (y[S_WY]-hist->w[1])/dt;
            o[OBS_WDOTZ] = (y[S_WZ]-hist->w[2])/dt;
            o[OBS_ALATPX] = hist->a_lat[0];
            o[OBS_ALATPY] = hist->a_lat[1];
            o[OBS_THRP]   = hist->throttle;
        } else {
            o[OBS_SFX]=0.0; o[OBS_SFY]=0.0; o[OBS_SFZ]=0.0;
            o[OBS_WDOTX]=0.0; o[OBS_WDOTY]=0.0; o[OBS_WDOTZ]=0.0;
            o[OBS_ALATPX]=0.0; o[OBS_ALATPY]=0.0; o[OBS_THRP]=0.0;
        }
    }
}

#endif /* BL_POLICY_OBS_H */
