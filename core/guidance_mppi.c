/* guidance_mppi.c — HIER MPPI CPU controller (M4). See guidance_mppi.h + CLAUDE_v1.md §9.2-9.6.
 *
 * Update law (per 50 Hz replan, horizon H, K rollouts):
 *   1. Sample OU-colored noise eps_t ~ around the warm-started mean ubar (shifted 1 step).
 *   2. Each rollout: build GuidanceCmd from (ubar+eps), run control_step (§8.3 allocation) +
 *      dynamics_deriv (shared EOM) at RK4 MPPI_DT; event-terminate on ground crossing.
 *      Cost C_k = phi(x_T) + sum_t [ q(x_t) + gamma*u_t^T Sigma^-1 eps_t ], gamma=lambda(1-alpha).
 *   3. Baseline beta=min_k C_k; weights w_k=exp(-(C_k-beta)/lambda)/eta.
 *   4. ubar += sum_k w_k eps_k  (fixed pairwise-tree reduction); Savitzky-Golay smooth; clamp.
 *   5. ESS=1/sum w_k^2; servo lambda (bisection) to hold ESS in [2%,10%]*K. Execute first knot; shift.
 *
 * Determinism: all noise from Philox (rng.h), stream RNG_MPPI, lane=rollout id,
 * counter=(replan, t*NCH+ch). No wall-clock, no atomics, fixed reduction order.
 */
#include "guidance_mppi.h"
#include "guidance_hoverslam.h"   /* hoverslam_step: the warm-start baseline (COORDINATOR #1037) */
#include "dynamics.h"
#include "control.h"
#include "integrator.h"
#include "contact.h"
#include "atmosphere.h"
#include "constants.h"
#include "rng.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================= TUNING (the differentiator) =============================
 * These weights are what make the divert LAND. The terminal cost must kill rollouts that
 * overshoot the pad in BOTH position and velocity, so MPPI plans the bang-bang cross-range
 * reversal AHEAD of the aero/thrust crossover dead zone (the tier-0 failure). */

/* Per-channel sampling sigma (OU stationary std). Lateral-accel sigma is large enough to
 * explore the full bang-bang reversal; throttle sigma explores the ignition timing. */
/* LATERAL-ONLY MPPI (decision #1045): throttle channel unused (hoverslam owns the vertical burn).
 * Sigma is small because we perturb around the hoverslam-aligned WARM-START, not zero. */
static const double SIG_THR   = 0.0;      /* throttle channel UNUSED (hoverslam owns vertical) */
static const double SIG_ALAT  = 1.5;      /* lateral-accel correction std [m/s^2] */
#define A_LAT_GAMUT 3.2                    /* physical lateral-accel gamut [m/s^2] = a_vert_ref*tan(15°).
                                            * D-009: ubar MUST be clamped HERE, not at a "margin" above
                                            * it — a warm-start railed beyond the gamut saturates every
                                            * rollout identically at the plant (tilt cap), killing the
                                            * softmax gradient through that channel. Measured: the old
                                            * ±8 clamp left alat railed at -8.00 for the first ~11 s of
                                            * AERO descent, making all cost redesigns inert. */
#define OU_THETA   0.15                    /* Ornstein-Uhlenbeck mean-reversion per step */

/* ---- CROSS-RANGE STRATEGY (the crux) ----
 * Horizon (5 s) << time-to-go (~50 s), so penalizing |r_xy| at the horizon end forces MPPI to
 * null cross-range at altitude, then it OVERSHOOTS the pad and swings out (confirmed independently
 * by both MPPI subagents). Instead track a CONVERGING-VELOCITY reference: vdes_xy = -Kconv*r_xy
 * (clamped), a first-order position loop -> exponential decay with NO overshoot. The running cost
 * penalizes |v_xy - vdes_xy|^2, so rollouts converge smoothly toward the pad and arrive low+slow.
 * Position=0 is only strongly rewarded at the true ground-crossing terminal. */
/* Bang-bang converging profile: vdes_in = sqrt(2*A_DECEL*r) (the inward speed that can still be
 * nulled to zero at r=0 given deceleration A_DECEL). A_DECEL is CONSERVATIVE (< the ~2.5 m/s^2
 * tilt-capped max) so the vehicle reverses cross-range EARLY and arrives at the pad with v_xy~0,
 * BEFORE the aero/thrust crossover dead zone where lateral authority inverts. This is what beats
 * the tier-0 heuristic: MPPI plans the reversal ahead instead of overshooting into the dead zone. */
/* A_DECEL is VERY conservative (well below the ~3.2 m/s^2 tilt-capped max) and the inward speed is
 * hard-capped LOW, so the vehicle never builds more cross-range velocity than it can null before the
 * pad. Arriving gently (below the deceleration limit) is what prevents the overshoot-into-dead-zone
 * that kills the reactive law. MPPI's edge: it holds this slow profile AND pre-reverses. */
/* D-009 resolution of the D-008 NOTE (divert-reach vs soft-arrest): three coupled fixes.
 * (1) The execution/rollout FADE now BLENDS into the tier-0 velocity-null law instead of fading to
 *     zero (the D-003 lesson repeated at the MPPI layer): residual v_xy is damped to contact, so
 *     divert reach no longer buys a hard touchdown. (2) A GATE cost at H_GATE=400 m + an altitude-
 *     ramped running |v_xy| weight implement "arrive CENTERED (r≈0, v_xy≈0) by 400 m" IN THE COST,
 *     front-loading the cross-range null where authority is plentiful. (3) Warm-start parity with
 *     the PROVEN tier-0 landing recipe (A_DECEL=1.5, VLAT_MAX=35 — the values that actually land).
 * Scenario note: if the measured divert ceiling (runs/sandbox/ceiling.c oracle study) sits below the
 * AERO_OFFSET dispersion, the dispersion is retuned by ADR (like D-006's 800→500). */
static const double A_DECEL    = 1.5;     /* cross-range decel — PARITY with the tier-0 landing recipe
                                           * (D-009: on the fin-shield-corrected plant, tier-0 lands 60%
                                           * with 1.5/35; MPPI's warm start should match the proven
                                           * profile and spend its corrections on wind/dispersions) */
static const double VLAT_MAX   = 35.0;    /* cruise cap [m/s] (tier-0 parity) */
static const double T_LEAD     = 2.0;     /* velocity-lead time [s] (COORDINATOR #1044: the landing trick) */

/* ---- COST CALIBRATION (critical) ----
 * All terms are NORMALIZED by reference scales so a typical rollout cost is O(100-1000) and the
 * softmax temperature lambda~30 gives a healthy ESS (~5-10% of K). Raw canon weights (40|r|^2 with
 * r~500) explode to 1e7 and rail lambda at its cap -> ESS=1 (single-sample collapse). Reference
 * scales: R_REF position, V_REF velocity, so (x/ref)^2 is unit-scaled. */
#define R_REF   40.0     /* position reference [m] (~1.5x pad radius) */
#define V_REF   8.0      /* velocity reference [m/s] */
#define W_REF   0.30     /* angular-rate reference [rad/s] */
#define TILT_REF (10.0*DEG2RAD)  /* tilt reference */

/* Running (stage) cost weights — per control step, scaled by MPPI_DT. Units: normalized^2. */
static const double Q_VXYERR = 10.0;      /* (v_xy - vdes_xy)/V_REF  track the converging profile HARD */
static const double Q_VOUT   = 25.0;      /* ASYMMETRIC extra on OUTWARD radial vel (overshoot=death) */
static const double Q_RXY    = 0.30;      /* (r_xy/R_REF)^2 gentle padward bias */
static const double Q_TILT   = 2.0;       /* (tilt/TILT_REF)^2 keep controllable */
static const double Q_OMEGA  = 2.0;       /* (|omega|/W_REF)^2 */
static const double Q_FUEL   = 0.5;       /* fuel-use (m0-m)/1000 kg */
static const double Q_ACC    = 2.0;       /* control-effort regularizer on |a_lat| (anti-divergence) */

/* In-air terminal (horizon end, still descending): reward being ON the converging profile + a
 * moderate padward pull + feasibility. NOT a hard r_xy=0 (that caused the overshoot). */
static const double T_VXYERR = 12.0;      /* terminal velocity-tracking error (normalized) */
static const double T_RXY_AIR= 4.0;       /* terminal position (normalized; altitude-faded) */
static const double T_TILT   = 15.0;
static const double T_OMEGA  = 8.0;

/* ZEM (zero-effort-miss) terminal — the FORESIGHT term (COORDINATOR #1042/#1044). Penalize the
 * predicted landing offset |r_xy + v_xy*t_go|^2 (t_go from the vertical shoot), so MPPI sees the
 * cross-range touchdown point PAST its 5 s horizon and pre-emptively nulls v_xy during the aero
 * descent. This is what corrects the (unseen) burn-phase drift and pulls MPPI OFF the warm-start. */
static const double T_ZEM    = 60.0;      /* (|ZEM|/R_REF)^2 — anchored at the IGNITION GATE above it,
                                           * at touchdown below it (D-009 config D; see the terminal) */
static const double T_VIGN   = 25.0;      /* |v_xy|^2 penalty ramping in over the last ~10 s before
                                           * the ignition gate: arrive centered AND slow (D-009) */
static const double T_RXYD   = 3.0;       /* small LINEAR |r|/R_REF anchor alongside ZEM (D-009, from
                                           * MPPI-1: pure ZEM with a noisy t_go can misbehave) */

/* Ground-crossing terminal (the TRUE objective): heavily penalize touchdown miss (pos AND vel). */
static const double TD_RXY   = 120.0;     /* touchdown (r_xy/R_REF)^2 (STRONG: off-pad dies) */
static const double TD_VXY   = 90.0;      /* touchdown (v_xy/V_REF)^2 (STRONG: carry-through dies) */
static const double TD_VZ    = 30.0;      /* touchdown ((vz-vtd)/V_REF)^2 */
static const double TD_TILT  = 120.0;
static const double TD_OMEGA = 60.0;
static const double CRASH_COST = 800.0;   /* BOUNDED crash indicator (ranks worst, stays in softmax range) */
static const double COST_CLIP  = 20000.0;  /* per-rollout cost cap (divergent rollouts can't dominate spread) */

/* Suicide-burn feasibility terminal barrier (§ design pt 4): at the in-air horizon end,
 * ask "can this still be landed?" via a cheap 1-D full-thrust vertical shoot (normalized). */
static const double W_VMARGIN = 6.0;      /* behind the min-fuel manifold ((margin/R_REF)^2) */
static const double W_FUEL_INFEAS = 30.0; /* short of fuel to arrest (deficit/1000 kg) */

/* D-009 "centered by 400 m": a one-shot GATE cost when a rollout descends through H_GATE (the
 * fade-to-vertical must inherit r≈0 AND v_xy≈0 there), plus an altitude-ramped running |v_xy|
 * weight that front-loads the cross-range null (kill v_xy HIGH, where aero authority is cheap). */
/* D-009 config C: the centered-by gate moved from a fixed 400 m to the PER-REPLAN IGNITION ALTITUDE
 * (M->ignite_h, ~3 km). Rationale: the ~100-150 m residual that survived every profile tuning is the
 * aero/thrust CROSSOVER DEAD ZONE (~22 kPa, right at ignition) where lateral authority genuinely
 * collapses — closure must complete BEFORE the trough, in clean aero. "Centered by IGNITION" was the
 * original coaching insight (#1048); the 400 m variant gated too late to matter. */
static const double G_RXY    = 60.0;      /* gate (r_xy/R_REF)^2 */
static const double G_VXY    = 60.0;      /* gate (v_xy/V_REF)^2 */
static const double Q_VLOW   = 0.0;       /* ZEROED (D-009): an ABSOLUTE |v_xy| penalty fights the
                                           * cruise phase of the optimal trapezoid (you MUST carry
                                           * 30-40 m/s inward to reach far seeds; the converging-
                                           * profile tracking term already encodes the correct null).
                                           * Ramped-in at 6→12 it under-diverted: 0/60 vs pkg1 2/60. */
#define H_VLOW_HI  6000.0
#define H_VLOW_LO  1500.0

static const double GAMMA_IS   = 1.0;     /* IS control-correction weight (small tie-breaker) */
static const double VTD_TARGET = 1.5;     /* desired touchdown descent speed [m/s] */
static const double LAMBDA0    = 30.0;    /* initial temperature (matched to normalized cost scale) */
static const double LAMBDA_MIN = 2.0;
static const double LAMBDA_MAX = 8000.0;  /* headroom (costs clipped at COST_CLIP -> spread bounded) */
static const double ESS_LO_FRAC = 0.03;   /* ESS servo band (fraction of K) */
static const double ESS_HI_FRAC = 0.20;

/* ---------------------------------------------------------------------------------------- */

/* Aero-aware-ish feasibility shoot: from horizon vertical state, forward-shoot full 1-engine
 * thrust vs gravity (no drag credit — retropropulsive plume shields the airframe; matches the
 * guidance_hoverslam.c suicide_burn_margin convention). Returns altitude margin [m] at vz->0;
 * <=0 means it would hit the ground still descending. Coarse (0.1 s), a barrier not the plant. */
static double feas_margin(double h_feet, double vz, double m){
    double h=h_feet, v=vz;
    const double dt=0.1;
    for(int i=0;i<3000 && v<0.0 && h>-60.0; i++){
        AtmoOut atm; atmo_eval(h,&atm);
        double T=engine_thrust(1.0, atm.p);
        double gh=G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));
        double a=T/m - gh;
        v += a*dt; h += v*dt;
    }
    return h;
}

/* Precompute the landing-burn IGNITION ALTITUDE once per replan (perf: replaces the per-rollout-step
 * suicide_burn_margin shoot). Bisection on ignition altitude h_ig: the largest h at which a full-
 * thrust arrest from the current descent speed reaches vz->0 within LANDING_IGNITE_MARGIN(150m) of
 * the ground — i.e. where the vehicle should light HARD. Matches guidance_hoverslam.c's trigger.
 * The current descent speed is scaled to each candidate altitude by energy (v grows falling). */
static double compute_ignite_h(const State* st){
    const double* y=st->y;
    MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
    double m=mp.m;
    double h0 = y[S_RZ]-mp.com;
    double vz0 = y[S_VZ]; if(vz0>-1.0) vz0=-1.0;
    /* find h_ig in [50, h0] s.t. feas_margin(h_ig, v_at(h_ig), m) ~ LANDING_IGNITE_MARGIN.
     * v_at(h): approximate the terminal (near-constant) descent speed as vz0 (drag-limited); the
     * vehicle is near terminal velocity in the lower atmosphere, so vz0 is a good proxy. */
    double lo=50.0, hi=(h0>60.0?h0:60.0);
    for(int it=0; it<28; it++){
        double mid=0.5*(lo+hi);
        double margin=feas_margin(mid, vz0, m);   /* altitude remaining at vz->0 from mid */
        if(margin > 150.0) hi=mid;   /* would arrest with room to spare -> can ignite LOWER */
        else lo=mid;                 /* barely/insufficient -> must ignite HIGHER */
    }
    double h_ig=0.5*(lo+hi);
    if(h_ig<40.0) h_ig=40.0;
    return h_ig;
}

/* Public wrapper (declared in guidance_mppi.h): expose the aero-aware ignition-altitude predictor
 * to the telemetry writer. Read-only over state; no side effects; not a guidance input path. */
double bl_predict_ignite_h(const State* st){ return compute_ignite_h(st); }

/* NaN/inf-safe clamp (D-009, ported from MPPI-1's hardening: a non-finite value must not escape
 * into the executed plan — map it to 0, the neutral correction). Pure function, determinism-safe. */
static double clampd(double x, double lo, double hi){
    if(!isfinite(x)) return 0.0;
    return x<lo?lo:(x>hi?hi:x);
}

/* Body tilt (angle of body +Z from world +Z) from a state's quaternion. */
static double state_tilt(const double y[NSTATE]){
    double zb[3]={0,0,1}, zw[3]; q_rot(zw,&y[S_QX],zb);
    double c=zw[2]; if(c>1)c=1; if(c<-1)c=-1; return acos(c);
}

/* Converging inward-velocity reference with a VELOCITY LEAD (COORDINATOR #1044 — the trick that
 * makes the reactive divert LAND). Predict the offset T_LEAD seconds ahead along the current radial
 * velocity: r_pred = r + v_rad*T_LEAD (v_rad<0 moving inward -> r_pred<r -> the sqrt profile
 * decelerates ~T_LEAD EARLY, beating the fin-rate attitude-reversal lag). Returns the desired world
 * lateral velocity (vdx,vdy) and the radial unit vector. */
static void converging_vdes(double rx, double ry, double vx, double vy,
                            double* vdx, double* vdy, double* rhx, double* rhy){
    double r_mag = sqrt(rx*rx+ry*ry);
    double rhxx=0.0, rhyy=0.0, vrad=0.0;
    if(r_mag>1e-3){ rhxx=rx/r_mag; rhyy=ry/r_mag; vrad=vx*rhxx+vy*rhyy; }
    double r_pred = r_mag + vrad*T_LEAD; if(r_pred<0.0) r_pred=0.0;
    double vdm = sqrt(2.0*A_DECEL*r_pred); if(vdm>VLAT_MAX) vdm=VLAT_MAX;
    *vdx = -vdm*rhxx; *vdy = -vdm*rhyy;
    *rhx = rhxx; *rhy = rhyy;
}

/* Predict time-to-touchdown from the current vertical state under the nominal (hoverslam-like)
 * descent — used for the ZEM (zero-effort-miss) terminal so MPPI can see the cross-range landing
 * point PAST its 5 s horizon (COORDINATOR #1042/#1044). Coarse full-thrust arrest shoot; returns t_go [s]. */
static double predict_tgo(double h_feet, double vz, double m){
    double h=h_feet, v=vz, t=0.0; const double dt=0.1;
    /* if still coasting fast, first the unpowered fall to the ignition-ish altitude is dominated by
     * v; approximate t_go ~ time to reach ground under current v decelerating at a modest net rate. */
    for(int i=0;i<3000 && h>0.0; i++){
        AtmoOut atm; atmo_eval(h,&atm);
        double T=engine_thrust(1.0, atm.p);
        double gh=G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));
        /* net accel: if descending fast, gravity+drag until the burn arrests; approximate with the
         * available decel once |v| is high (burn active), else free-fall. Use half-thrust average. */
        double a = (v<-2.0)? (0.5*T/m - gh) : -gh;   /* up positive while burning */
        v += a*dt; h += v*dt; t += dt;
        if(v>0.0 && h<=0.0) break;
        if(h<=0.0) break;
    }
    if(t<0.1) t=0.1; if(t>40.0) t=40.0;
    return t;
}

/* LEAN vertical model for the ROLLOUT (perf: calling full hoverslam_step — with its 3000-iter
 * suicide_burn_margin shoot — 256x200 per replan cost 60 s/run). Replicates the CHEAP part of the
 * hoverslam suicide burn: ignite at a PRECOMPUTED altitude (ignite_h, from one shoot per replan),
 * then track the frozen-a_design v_ref profile with lag-correcting feedback; terminal commit-to-
 * touchdown cut. No inner shoot. MPPI contributes ONLY the world-lateral-accel steering u[1],u[2].
 * (At EXECUTION the exact hoverslam_step runs; this proxy only needs to rank rollouts consistently.) */
static void cmd_from_u_lean(State* rst, const double u[MPPI_NCH], double h_feet, double ignite_h,
                            GuidanceCmd* g){
    const double* y=rst->y;
    MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
    double m=mp.m;
    double vz=y[S_VZ];
    AtmoOut atm; atmo_eval(y[S_RZ],&atm);
    double Tfull=engine_thrust(1.0,atm.p);
    double zw[3],zb0[3]={0,0,1}; q_rot(zw,&y[S_QX],zb0);
    double ctilt=zw[2]; if(ctilt>1)ctilt=1; if(ctilt<-1)ctilt=-1; double tilt=acos(ctilt);

    g->mode=GM_MPPI; g->n_eng=1; g->solver_flags=0;
    g->deploy_cmd=(h_feet<=LEG_DEPLOY_H)?1:0;
    if(h_feet<6.0) g->solver_flags|=SF_TERMINAL;

    /* ignition: precomputed altitude trigger (aero-aware margin computed once per replan) */
    int ignite = (!rst->engine_on) && (vz < -1.0) && (h_feet <= ignite_h);
    if(rst->engine_on || ignite){
        g->engine_cmd=1;
        /* frozen a_design (ada set at ignition in the latch); v_ref profile */
        double a_design = rst->engine_on ? rst->ada : 0.85*(Tfull/m - G0);
        if(a_design<1.0) a_design=1.0;
        double hgo=h_feet; if(hgo<0.02)hgo=0.02;
        double v_ref=-sqrt(TD_V_TARGET*TD_V_TARGET + 2.0*a_design*hgo);
        double Kv=3.0;
        double a_cmd=a_design + Kv*(v_ref - vz);
        double D=0.5*atm.rho*vz*vz*VEH_AREF*0.9;
        double T_need=m*(G0+a_cmd) - D;
        double ct=cos(tilt); if(ct<0.3)ct=0.3;
        double thr=T_need/(Tfull*ct);
        if(thr<ENG_THR_MIN)thr=ENG_THR_MIN; if(thr>1.0)thr=1.0;
        /* commit-to-touchdown: cut so it free-falls the last <2 m instead of climbing (min-thr TWR>1) */
        if(h_feet<2.5 && vz>-1.2){ g->engine_cmd=0; g->throttle=0.0; }
        else g->throttle=thr;
    } else {
        g->engine_cmd=0; g->throttle=0.0;
    }

    /* MPPI steering, FADED during the powered burn so the arrest stays ~vertical — BLENDED into a
     * pure velocity-null damping term below the fade (D-009; the D-003 lesson at the MPPI layer:
     * fading to ZERO let residual v_xy ride to touchdown — dead-center seeds crashed at td_v 6-8).
     * Parity with mppi_execute's blend into hoverslam's law, which near the ground reduces to
     * -Kvel*v_xy with Kvel~0.6. The control.c altitude tilt cap bounds it near touchdown.
     * D-009 ignition-attitude handling (parity with guidance_hoverslam.c): FEATHER steering to zero
     * approaching the ignition altitude (straighten before light-up) and HOLD zero through the
     * ignition ramp — the burn lighting on an aero-trimmed tilt shoves the vehicle ~150 m off. */
    double s=1.0, damp=0.0;
    if(rst->engine_on){
        s=h_feet/400.0; if(s>1.0)s=1.0; if(s<0.0)s=0.0; s*=s;
        damp=1.0-s;
        if(rst->ign_timer>=0.0 && rst->ign_timer<2.0){ s=0.0; damp=1.0; }   /* damp-through-ignition (FIX #3) */
    } else {
        double fe=(h_feet - ignite_h)/450.0; if(fe<0.0)fe=0.0; if(fe>1.0)fe=1.0;
        s=fe;                                                                /* pre-ignition feather */
    }
    /* D-010 rollout<->execution parity: the execution blend inherits hoverslam's HEIGHT-SPLIT null
     * gain (0.9 -> 1.6 at the deck), so the rollout's damping must mirror it — a fixed 0.6 here
     * made rollouts rank trajectories a way the plant no longer flies (directive 7), and the MPPI
     * batch dropped 63->40% when the split landed. Same ramp, same constants.
     * D-012: the execution base gain is now the STATE-ADAPTIVE schedule (KDIV_* shared from
     * guidance_hoverslam.h; converging_vdes IS hoverslam's vdes profile — A_DECEL/VLAT_MAX/T_LEAD
     * parity), so the rollout mirror computes the same overspeed blend from the rollout state. */
    double kbase = KDIV_SEEK;
    if(rst->engine_on){   /* D-012 powered-burn gate — parity with hoverslam_step */
        double vdxm,vdym,rhxm,rhym;
        converging_vdes(y[S_RX],y[S_RY],y[S_VX],y[S_VY],&vdxm,&vdym,&rhxm,&rhym);
        double vdmm = sqrt(vdxm*vdxm+vdym*vdym);
        double vxym = sqrt(y[S_VX]*y[S_VX]+y[S_VY]*y[S_VY]);
        double osm = (vxym - vdmm)/KDIV_VBLEND; if(osm<0.0)osm=0.0; if(osm>1.0)osm=1.0;
        kbase = KDIV_SEEK + osm*(KDIV_BRAKE - KDIV_SEEK);
    }
    double bk=(250.0-h_feet)/250.0; if(bk<0.0)bk=0.0; if(bk>1.0)bk=1.0;
    double kvd=kbase + bk*(1.6-kbase);
    g->a_lat[0]=s*u[1] + damp*(-kvd*y[S_VX]);
    g->a_lat[1]=s*u[2] + damp*(-kvd*y[S_VY]);
    g->t_go=(vz<-0.1)?(h_feet/(-vz)):5.0;
}

/* One rollout: integrate the plant forward H steps under control (ubar+eps), accumulate cost.
 * eps is added to ubar to form u_t. Returns total cost C_k (incl. IS correction + terminal).
 * Determinism: no RNG here (noise pre-sampled by caller). Event-terminate on ground; freeze. */
static double rollout_cost(const MppiState* M, const State* st0, const EnvCtx* env0,
                           const double eps[MPPI_H][MPPI_NCH], double gamma,
                           double m0){
    State rst = *st0;                 /* full hybrid state copy (engine/legs/timers) */
    Actuators act; memset(&act,0,sizeof(act)); act.n_eng=1;  /* OWN actuators (fin_eint per rollout) */
    EnvCtx env = *env0;
    env.wind_world[0]=env.wind_world[1]=env.wind_world[2]=0.0;  /* nominal planner */

    double cost = 0.0;
    int grounded = 0;
    int gate_done = 0;                /* D-009: one-shot centered-by-400m gate cost fired? */
    double deck_z = 0.0;              /* RTLS ground; AERO_OFFSET/TERMINAL deck_z=0 */

    /* inverse-Sigma for IS correction (diagonal). Guard the unused (zero-sigma) throttle channel. */
    const double invSig[MPPI_NCH] = { (SIG_THR>0.0)?1.0/(SIG_THR*SIG_THR):0.0,
                                      1.0/(SIG_ALAT*SIG_ALAT),
                                      1.0/(SIG_ALAT*SIG_ALAT) };

    double last_hfeet = 0.0;
    for(int t=0;t<MPPI_H && !grounded;t++){
        MassProps mp; mass_props(rst.y[S_MLOX],rst.y[S_MRP1],0,0,&mp);
        double h_feet = rst.y[S_RZ] - mp.com - 1.0*rst.deploy_frac;
        last_hfeet = h_feet;

        /* D-009 GATE (config C): first descent through the IGNITION altitude — the vehicle must be
         * CENTERED (r≈0, v_xy≈0) when it lights, because the aero/thrust crossover dead zone right
         * below kills lateral authority. One-shot cost so rollouts that arrive centered win.
         * (Rollouts starting below the gate fire it immediately with the shared initial state — a
         * constant offset, harmless.) */
        if(!gate_done && h_feet<=M->ignite_h+50.0){
            gate_done=1;
            double gr2=(rst.y[S_RX]*rst.y[S_RX]+rst.y[S_RY]*rst.y[S_RY])/(R_REF*R_REF);
            double gv2=(rst.y[S_VX]*rst.y[S_VX]+rst.y[S_VY]*rst.y[S_VY])/(V_REF*V_REF);
            cost += G_RXY*gr2 + G_VXY*gv2;
        }

        /* control input for this knot */
        double u[MPPI_NCH];
        for(int c=0;c<MPPI_NCH;c++) u[c] = M->ubar[t][c] + eps[t][c];

        GuidanceCmd g; memset(&g,0,sizeof(g));
        cmd_from_u_lean(&rst, u, h_feet, M->ignite_h, &g);   /* lean vertical proxy (perf) */

        /* engine ignition latch inside the rollout (mirror of sim.c) */
        if(g.engine_cmd && !rst.engine_on && rst.relights_left>0){
            rst.engine_on=1; rst.ign_timer=0.0; rst.n_eng=g.n_eng; rst.relights_left--;
            if(rst.phase==PH_COAST||rst.phase==PH_AERO) rst.phase=PH_LANDING_BURN;
        }
        rst.deploy_cmd = g.deploy_cmd;
        if(rst.engine_on && rst.ign_timer>=0.0) rst.ign_timer += MPPI_DT;
        if(rst.deploy_cmd){ rst.deploy_frac += MPPI_DT/LEG_DEPLOY_T; if(rst.deploy_frac>1)rst.deploy_frac=1; }

        /* allocation (§8.3) then EOM step (RK4 at the control step) */
        control_step(&rst, &g, &env, &act);
        rk4_step(&rst, &act, &env, MPPI_DT);

        /* ---- running cost: track a CONVERGING-VELOCITY reference (no overshoot) ---- */
        double rx=rst.y[S_RX], ry=rst.y[S_RY];
        double vx=rst.y[S_VX], vy=rst.y[S_VY];
        double wmag2 = rst.y[S_WX]*rst.y[S_WX]+rst.y[S_WY]*rst.y[S_WY]+rst.y[S_WZ]*rst.y[S_WZ];
        double tilt = state_tilt(rst.y);
        double rxy2 = rx*rx+ry*ry;
        double r_mag = sqrt(rxy2);
        MassProps mp2; mass_props(rst.y[S_MLOX],rst.y[S_MRP1],0,0,&mp2);
        double fuel_used = m0 - mp2.m;

        /* desired inward velocity from the velocity-LED converging profile (COORDINATOR #1044). */
        double vdx=0.0, vdy=0.0, rhx=0.0, rhy=0.0;
        converging_vdes(rx,ry,vx,vy,&vdx,&vdy,&rhx,&rhy);
        double evx=vx-vdx, evy=vy-vdy;
        double vxyerr2 = (evx*evx+evy*evy)/(V_REF*V_REF);            /* normalized */
        /* radial velocity (outward +). Penalize OUTWARD hard: overshoot past the pad is the killer. */
        double vrad = vx*rhx + vy*rhy;
        double vout = (vrad>0.0)? vrad/V_REF : 0.0;

        double q = Q_VXYERR*vxyerr2 + Q_VOUT*vout*vout
                 + Q_RXY*(rxy2/(R_REF*R_REF))
                 + Q_TILT*(tilt*tilt/(TILT_REF*TILT_REF))
                 + Q_OMEGA*(wmag2/(W_REF*W_REF))
                 + Q_FUEL*(fuel_used/1000.0);
        /* D-009 front-load: altitude-ramped ABSOLUTE |v_xy| penalty — null cross-range velocity
         * EARLY (aero-descent authority is plentiful); by the low gate it must already be small. */
        {
            double wlow=(H_VLOW_HI - h_feet)/(H_VLOW_HI - H_VLOW_LO);
            if(wlow<0.0)wlow=0.0; if(wlow>1.0)wlow=1.0;
            q += Q_VLOW*wlow*((vx*vx+vy*vy)/(V_REF*V_REF));
        }
        /* CONTROL-EFFORT regularizer: penalize large lateral-accel commands so MPPI's correction
         * stays near the (already-decent) warm-start baseline instead of railing the clamp and
         * diverging. Normalized by SIG_ALAT. This replaces the accidental regularization the
         * mis-scaled IS term was providing. */
        double aeff2 = (u[1]*u[1]+u[2]*u[2])/(SIG_ALAT*SIG_ALAT*9.0);
        q += Q_ACC*aeff2;
        cost += q*MPPI_DT;

        /* ---- IS correction: gamma * u^T Sigma^-1 eps ---- */
        double isc = 0.0;
        for(int c=0;c<MPPI_NCH;c++) isc += u[c]*invSig[c]*eps[t][c];
        cost += gamma*isc;

        /* ---- event-terminate on ground crossing (no underground integration) ---- */
        double lo = lowest_point_z(&rst);
        if(lo <= deck_z + 1e-3){
            grounded = 1;
            double vz2 = rst.y[S_VZ];
            double vxy2 = vx*vx+vy*vy;
            double td_v = sqrt(vxy2+vz2*vz2);
            double dvz = vz2-(-VTD_TARGET);
            /* THE true objective (normalized): strong touchdown position + velocity miss + crash. */
            double phi = TD_RXY*(rxy2/(R_REF*R_REF)) + TD_VXY*(vxy2/(V_REF*V_REF))
                       + TD_VZ*(dvz*dvz/(V_REF*V_REF))
                       + TD_TILT*(tilt*tilt/(TILT_REF*TILT_REF)) + TD_OMEGA*(wmag2/(W_REF*W_REF));
            int off_pad = (rxy2 > PAD_RADIUS*PAD_RADIUS);
            int too_hard = (td_v > TD_V_HARD);
            int tipped   = (tilt > 30.0*DEG2RAD);
            if(off_pad || too_hard || tipped) phi += CRASH_COST;
            /* graded linear pad-distance pull so near-misses are ranked (normalized) */
            phi += 40.0*(sqrt(rxy2)/R_REF);
            cost += phi;
            break;
        }
    }

    if(!grounded){
        /* ---- in-air terminal at horizon end: reward being ON the converging profile ----
         * NOT a hard r_xy=0 (that caused the overshoot). Penalize velocity-tracking error to the
         * converging reference + a MODERATE, altitude-faded position pull + feasibility. */
        double rx=rst.y[S_RX], ry=rst.y[S_RY];
        double vx=rst.y[S_VX], vy=rst.y[S_VY], vz=rst.y[S_VZ];
        double wmag2 = rst.y[S_WX]*rst.y[S_WX]+rst.y[S_WY]*rst.y[S_WY]+rst.y[S_WZ]*rst.y[S_WZ];
        double tilt = state_tilt(rst.y);
        double rxy2 = rx*rx+ry*ry;
        double r_mag = sqrt(rxy2);
        MassProps mp; mass_props(rst.y[S_MLOX],rst.y[S_MRP1],0,0,&mp);
        double h_feet = rst.y[S_RZ]-mp.com-1.0*rst.deploy_frac;

        double vdx=0.0, vdy=0.0, rhx=0.0, rhy=0.0;
        converging_vdes(rx,ry,vx,vy,&vdx,&vdy,&rhx,&rhy);
        double evx=vx-vdx, evy=vy-vdy;
        double vxyerr2 = (evx*evx+evy*evy)/(V_REF*V_REF);           /* normalized */
        double vrad = vx*rhx + vy*rhy; double vout=(vrad>0.0)?vrad/V_REF:0.0;
        /* position pull grows as the ground nears (below ~1.5 km the pad really must be closed) */
        double hkm = (rst.y[S_RZ]-mp.com)/1500.0; if(hkm<0)hkm=0;
        double pos_w = T_RXY_AIR * (hkm<1.0 ? (2.0-hkm) : 1.0);

        /* ZEM foresight (D-009 config D): anchored at the IGNITION GATE, not touchdown, while above
         * it. A touchdown-anchored ZEM scores a ballistic collision course as PERFECT — it rewards
         * drifting THROUGH the ignition/crossover dead zone still carrying v_xy, exactly the observed
         * ~100-150 m residual (the dead zone cannot null it). Above the gate: project to the gate
         * (t_ign = (h - ignite_h)/|vz|), demand r->0 there, and ramp in a |v_xy| penalty as the gate
         * nears (arrive centered AND slow — the burn is then a clean vertical arrest). Below the gate
         * (burning): the touchdown anchor is correct (ZEM sees the residual burn drift). This is the
         * only horizon-visible foresight for most of the descent — event gates inside the 5 s window
         * never fire until the final seconds (measured: gate moves had zero effect on outcomes). */
        double zem2, vign_pen = 0.0;
        if(h_feet > M->ignite_h){
            double vzm = (vz < -20.0) ? -vz : 20.0;
            double t_ign = (h_feet - M->ignite_h)/vzm;
            double zx = rx + vx*t_ign, zy = ry + vy*t_ign;
            zem2 = (zx*zx+zy*zy)/(R_REF*R_REF);
            double wv = 1.0 - t_ign/10.0; if(wv<0.0)wv=0.0;
            vign_pen = T_VIGN*wv*((vx*vx+vy*vy)/(V_REF*V_REF));
        } else {
            double t_go = predict_tgo(h_feet, vz, mp.m);
            double zx = rx + vx*t_go, zy = ry + vy*t_go;
            zem2 = (zx*zx+zy*zy)/(R_REF*R_REF);
        }

        double phi = T_VXYERR*vxyerr2 + Q_VOUT*vout*vout + pos_w*(rxy2/(R_REF*R_REF))
                   + T_ZEM*zem2 + vign_pen + T_RXYD*(r_mag/R_REF)
                   + T_TILT*(tilt*tilt/(TILT_REF*TILT_REF)) + T_OMEGA*(wmag2/(W_REF*W_REF));

        /* feasibility barrier: can a full-thrust suicide burn still arrest above the ground?
         * NORMALIZED by R_REF so it stays in the softmax's dynamic range. */
        double margin = feas_margin(h_feet, vz<0?vz:0.0, mp.m);
        if(margin < 0.0){ double mn=margin/R_REF; phi += W_VMARGIN*mn*mn; }  /* behind arrest manifold */

        /* fuel-to-arrest check: rough impulse to null vz at ~0.85 g_eff decel vs remaining prop */
        {
            double fuel = rst.y[S_MLOX]+rst.y[S_MRP1];
            double dv = fabs(vz) + 20.0;
            AtmoOut atm; atmo_eval(rst.y[S_RZ],&atm);
            double T = engine_thrust(1.0, atm.p);
            double aeff = T/mp.m - G0; if(aeff<1.0) aeff=1.0;
            double t_burn = dv/aeff;
            double mdot = T/(ENG_ISP_SL*G0);
            double m_needed = mdot*t_burn;
            double deficit = m_needed - fuel;
            if(deficit > 0.0) phi += W_FUEL_INFEAS*(deficit/1000.0);   /* normalized per t */
        }
        (void)last_hfeet;
        cost += phi;
    }
    /* CLIP: a single divergent rollout (tumble / lateral runaway) must not dominate the softmax
     * spread. Cap keeps costs in a range where lambda can discriminate; still ranks the bad ones
     * worst (they all saturate at the cap and get ~zero weight). */
    if(cost > COST_CLIP) cost = COST_CLIP;
    return cost;
}

/* ---- WARM-START the mean with the HOVERSLAM baseline (COORDINATOR #1037: the big unlock) ----
 * Forward-shoot the plant from the current state using hoverslam_step for guidance (which handles
 * the aero-aware ignition + the sqrt-decel divert), recording the COMMANDED (throttle, a_lat[2]) at
 * each knot into ubar. MPPI then samples small sigma around THIS landing-ish baseline and only has
 * to discover the cross-range velocity-null CORRECTION — not the whole descent from noise. This
 * clusters all rollouts near a good trajectory (healthy softmax) and inherits ignition timing (fixes
 * the horizon<<t_go vertical-channel blindness). Deterministic: pure function of state, no RNG. */
static void warm_start_nominal(MppiState* M, const State* st0, const EnvCtx* env0){
    State rst = *st0;
    Actuators act; memset(&act,0,sizeof(act)); act.n_eng=1;
    EnvCtx env = *env0; env.wind_world[0]=env.wind_world[1]=env.wind_world[2]=0.0;
    int grounded=0; double deck_z=0.0;
    for(int t=0;t<MPPI_H;t++){
        MassProps mp; mass_props(rst.y[S_MLOX],rst.y[S_MRP1],0,0,&mp);
        double h_feet = rst.y[S_RZ]-mp.com-1.0*rst.deploy_frac;
        /* baseline a_lat = velocity-led converging profile (aligned with the MPPI cost) */
        double alx=0.0, aly=0.0;
        if(!grounded){
            double vdxw=0.0,vdyw=0.0,rhxw=0.0,rhyw=0.0;
            converging_vdes(rst.y[S_RX],rst.y[S_RY],rst.y[S_VX],rst.y[S_VY],&vdxw,&vdyw,&rhxw,&rhyw);
            /* clamp the BASELINE to the physical gamut so sampling around it explores the
             * actually-flyable range instead of saturating every rollout at the plant (D-009) */
            alx = clampd(1.0*(vdxw - rst.y[S_VX]), -A_LAT_GAMUT, A_LAT_GAMUT);
            aly = clampd(1.0*(vdyw - rst.y[S_VY]), -A_LAT_GAMUT, A_LAT_GAMUT);
        }
        /* record the baseline lateral control for this knot (what MPPI perturbs around) */
        M->ubar[t][0]=0.0; M->ubar[t][1]=alx; M->ubar[t][2]=aly;
        if(grounded) continue;

        /* advance the nominal plant using the LEAN vertical model (same as the rollout, for a
         * consistent baseline trajectory + inherited ignition timing) */
        double uu[MPPI_NCH]={0.0,alx,aly};
        GuidanceCmd g; memset(&g,0,sizeof(g));
        cmd_from_u_lean(&rst, uu, h_feet, M->ignite_h, &g);
        if(g.engine_cmd && !rst.engine_on && rst.relights_left>0){
            rst.engine_on=1; rst.ign_timer=0.0; rst.n_eng=g.n_eng; rst.relights_left--;
            if(rst.phase==PH_COAST||rst.phase==PH_AERO) rst.phase=PH_LANDING_BURN;
            AtmoOut atm; atmo_eval(rst.y[S_RZ],&atm);
            double Tf=rst.n_eng*engine_thrust(1.0,atm.p); double amax=Tf/mp.m-G0; if(amax<1.0)amax=1.0;
            rst.ada=(rst.fins_deployed?0.85:0.58)*amax;
        }
        rst.deploy_cmd = g.deploy_cmd;
        if(rst.engine_on && rst.ign_timer>=0.0) rst.ign_timer += MPPI_DT;
        if(rst.deploy_cmd){ rst.deploy_frac += MPPI_DT/LEG_DEPLOY_T; if(rst.deploy_frac>1)rst.deploy_frac=1; }
        control_step(&rst, &g, &env, &act);
        rk4_step(&rst, &act, &env, MPPI_DT);
        if(lowest_point_z(&rst) <= deck_z + 1e-3) grounded=1;
    }
}

/* Savitzky-Golay (window 9, order 3) smoothing of one channel across the horizon. Reduces
 * chatter without lag (§ design pt 4). Applied in-place over a length-H array. */
static void sgf_smooth(double* x, int n){
    /* Normalized SG(9,3) coefficients (symmetric): from -4..+4. */
    static const double c[9] = {
        -21.0/231.0, 14.0/231.0, 39.0/231.0, 54.0/231.0, 59.0/231.0,
         54.0/231.0, 39.0/231.0, 14.0/231.0, -21.0/231.0
    };
    if(n < 9) return;
    double tmp[MPPI_H];
    for(int i=0;i<n;i++){
        int lo=i-4, hi=i+4;
        if(lo<0 || hi>=n){ tmp[i]=x[i]; continue; }   /* leave edges (keeps the executed knot crisp) */
        double s=0.0;
        for(int k=0;k<9;k++) s += c[k]*x[lo+k];
        tmp[i]=s;
    }
    for(int i=0;i<n;i++) x[i]=tmp[i];
}

void mppi_init(MppiState* M, uint32_t seed, int scenario){
    memset(M,0,sizeof(*M));
    M->seed = seed;
    M->scenario = scenario;
    M->lambda = LAMBDA0;
    M->replan = 0;
    /* warm-start nominal: zero lateral accel, coast throttle (below the ignition floor).
     * The first replans discover the burn/divert; zeroing is the neutral prior. */
    for(int t=0;t<MPPI_H;t++){ M->ubar[t][0]=0.0; M->ubar[t][1]=0.0; M->ubar[t][2]=0.0; }
    M->inited = 1;
}

void mppi_step(MppiState* M, const State* st, const EnvCtx* env, GuidanceCmd* g){
    if(!M->inited) mppi_init(M, M->seed, M->scenario);
    if(getenv("MPPI_DBG")){ static int once=0; if(!once){ fprintf(stderr,"[mppi] ENTER mppi_step first call\n"); once=1; } }

    MassProps mp0; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp0);
    double m0 = mp0.m;

    /* Precompute the landing-burn ignition altitude ONCE per replan (perf: the rollout's lean
     * vertical model reads this instead of running the 3000-iter arrest shoot 256x200 times). */
    M->ignite_h = compute_ignite_h(st);

    /* WARM-START: rebuild the (lateral) mean from the velocity-led converging baseline each replan
     * (COORDINATOR #1037). MPPI optimizes the cross-range velocity-null CORRECTION around it.
     * D-009 NOTE: carrying the correction ACROSS replans (persistent ucorr on top of this rebuild)
     * was tried and REGRESSED (singles lat 15→91 m on the landing seed): the baseline is CLOSED-LOOP
     * (recomputed from the corrected state), so a carried correction double-counts — accumulation
     * happens through the PLANT, not the plan. One-shot correction + strong per-replan cost gradients
     * is the correct pairing for a closed-loop baseline. */
    warm_start_nominal(M, st, env);

    const double sig[MPPI_NCH] = { SIG_THR, SIG_ALAT, SIG_ALAT };
    const double drive[MPPI_NCH] = {
        SIG_THR *sqrt(2.0*OU_THETA - OU_THETA*OU_THETA),
        SIG_ALAT*sqrt(2.0*OU_THETA - OU_THETA*OU_THETA),
        SIG_ALAT*sqrt(2.0*OU_THETA - OU_THETA*OU_THETA)
    };
    (void)sig;

    /* ---- sample OU noise for ALL K rollouts (stored once), then run rollouts ----
     * Store eps in a single static buffer [K][H][NCH] (~1.2 MB at 256/200/3). CPU determinism:
     * sampled from Philox with lane=rollout id k, counter=(replan, t*NCH+c); regenerating in the
     * reduction is O(H^2) and needless here — storing keeps it O(H*K) and bit-identical. */
    static double EPS[MPPI_K][MPPI_H][MPPI_NCH];
    static double C[MPPI_K];
    /* IS (control) correction weight. Canonically lambda*(1-alpha), but coupling it to the swelling
     * ESS-servoed lambda lets the u^T Sigma^-1 eps term swamp the (small, post-warm-start) state cost
     * and drive beta wildly negative. Use a SMALL fixed gamma so IS stays a minor tie-breaker that
     * nudges toward lower-effort corrections without dominating the landing objective. */
    double gamma = GAMMA_IS;

    /* Rollouts are INDEPENDENT and deterministic (noise is Philox-addressed by k, not stateful),
     * so the K-loop parallelizes with no effect on results — the reduction below runs in a FIXED
     * order regardless of thread scheduling, so same seed => bit-identical plan. */
    int k;
#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for(k=0;k<MPPI_K;k++){
        for(int c=0;c<MPPI_NCH;c++){
            double prev = 0.0;
            for(int t=0;t<MPPI_H;t++){
                /* Philox: lane=rollout id k, counter=(replan, t*NCH+c). Stream RNG_MPPI. */
                double n = rng_normal(M->seed, RNG_MPPI, M->replan, (uint32_t)(t*MPPI_NCH+c), (uint32_t)k);
                /* Ornstein-Uhlenbeck: eps_t = (1-theta) eps_{t-1} + sigma*sqrt(2 theta - theta^2)*n
                 * (stationary std = sigma). White (theta=1) chatters; OU (theta=0.15) flies. */
                double e = (1.0-OU_THETA)*prev + drive[c]*n;
                EPS[k][t][c] = e;
                prev = e;
            }
        }
        C[k] = rollout_cost(M, st, env, EPS[k], gamma, m0);
        /* D-009 (MPPI-1 hardening): a NaN slips past a '>' compare but not !isfinite — sanitize to
         * the cost cap so a poisoned rollout ranks worst yet stays inside the softmax range. */
        if(!isfinite(C[k])) C[k]=COST_CLIP;
    }

    /* ---- baseline beta = min_k C_k (CPU min: trivially deterministic) ---- */
    double beta = C[0];
    for(int k=1;k<MPPI_K;k++) if(C[k]<beta) beta=C[k];

    /* ---- ESS-adaptive lambda: bisection so ESS in [2%,10%]*K ----
     * ESS(lambda) = (sum exp(-(C-beta)/lambda))^2 / sum exp(-2(C-beta)/lambda). Monotone in lambda. */
    double lam_lo=LAMBDA_MIN, lam_hi=LAMBDA_MAX, lam=M->lambda;
    if(lam<lam_lo)lam=lam_lo; if(lam>lam_hi)lam=lam_hi;
    double ess_target_lo = ESS_LO_FRAC*MPPI_K, ess_target_hi = ESS_HI_FRAC*MPPI_K;
    for(int it=0; it<40; it++){
        double s1=0.0, s2=0.0;
        for(int k=0;k<MPPI_K;k++){ double w=exp(-(C[k]-beta)/lam); s1+=w; s2+=w*w; }
        double ess = (s2>1e-300)? (s1*s1/s2) : 1.0;
        if(ess < ess_target_lo)      lam_lo = lam;   /* too peaked -> raise lambda */
        else if(ess > ess_target_hi) lam_hi = lam;   /* too flat  -> lower lambda */
        else break;
        lam = 0.5*(lam_lo+lam_hi);
    }
    M->lambda = lam;

    /* ---- weights + update: ubar += sum_k w_k eps_k  (fixed pairwise-tree reduction) ----
     * Recompute eps from Philox (zero noise storage). Pairwise tree over k for bit-stable sum. */
    double eta=0.0;
    static double W[MPPI_K];
    for(int k=0;k<MPPI_K;k++){ W[k]=exp(-(C[k]-beta)/lam); eta+=W[k]; }
    double inv_eta = (eta>1e-300)? 1.0/eta : 0.0;

    /* numerator per (t,ch): sum_k W[k]*eps_k[t][ch], via a FIXED-TOPOLOGY pairwise tree over k.
     * The balanced power-of-two fold gives a bit-identical FP sum regardless of later
     * parallelization (matches the CUDA K2 reduction). K=256 is a power of two. */
    static double num[MPPI_H][MPPI_NCH];
    {
        static double buf[MPPI_K];
        for(int t=0;t<MPPI_H;t++){
            for(int c=0;c<MPPI_NCH;c++){
                for(int k=0;k<MPPI_K;k++) buf[k] = W[k]*EPS[k][t][c];
                int len=MPPI_K;
                while(len>1){
                    int half=len/2;
                    for(int i=0;i<half;i++) buf[i]=buf[i]+buf[i+half];
                    if(len&1) buf[half-1]+=buf[len-1];   /* odd carry (K pow2 -> never taken) */
                    len=half;
                }
                num[t][c]=buf[0];
            }
        }
    }

    for(int t=0;t<MPPI_H;t++)
        for(int c=0;c<MPPI_NCH;c++)
            M->ubar[t][c] += inv_eta*num[t][c];

    /* ---- Savitzky-Golay smooth each channel, then clamp to physical ranges ---- */
    {
        double col[MPPI_H];
        for(int c=0;c<MPPI_NCH;c++){
            for(int t=0;t<MPPI_H;t++) col[t]=M->ubar[t][c];
            sgf_smooth(col, MPPI_H);
            for(int t=0;t<MPPI_H;t++) M->ubar[t][c]=col[t];
        }
        for(int t=0;t<MPPI_H;t++){
            M->ubar[t][0] = clampd(M->ubar[t][0], 0.0, 1.0);          /* NaN-safe (D-009) */
            for(int c=1;c<MPPI_NCH;c++){
                /* clamp to the PHYSICAL gamut — see A_LAT_GAMUT note (the old 8.0 "margin" railed
                 * the plan beyond the plant's authority and killed the exploration gradient) */
                M->ubar[t][c] = clampd(M->ubar[t][c], -A_LAT_GAMUT, A_LAT_GAMUT);   /* NaN-safe */
            }
        }
    }

    /* telemetry */
    {
        double s1=0.0,s2=0.0; for(int k=0;k<MPPI_K;k++){ s1+=W[k]; s2+=W[k]*W[k]; }
        M->ess = (s2>1e-300)? s1*s1/s2 : 0.0;
        M->beta = beta; M->cmin = beta;
    }
    {
        static int dbgc=0;
        const char* e=getenv("MPPI_DBG");
        if(e && (dbgc%10==0)){
            double cmax=C[0]; for(int k=1;k<MPPI_K;k++) if(C[k]>cmax)cmax=C[k];
            fprintf(stderr,"[mppi] rep=%u thr0=%.3f alat=(%.2f,%.2f) ess=%.1f lam=%.1f beta=%.1f cspread=%.1f\n",
                    M->replan, M->ubar[0][0], M->ubar[0][1], M->ubar[0][2],
                    M->ess, M->lambda, beta, cmax-beta);
        }
        dbgc++;
    }

    M->replan++;

    /* emit the first knot for THIS state + shift the warm-start (receding horizon) */
    mppi_execute(M, st, g);
}

/* Cheap between-solve tick: emit the current warm-start knot 0 (latched on the LIVE altitude so
 * ignition/deploy fire correctly) and shift the plan by one knot. No rollouts. */
void mppi_execute(MppiState* M, const State* st, GuidanceCmd* g){
    MassProps mpc; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mpc);
    double h_feet0 = st->y[S_RZ]-mpc.com-1.0*st->deploy_frac;

    /* EXECUTION uses the REAL hoverslam for the vertical channel (exact aero-aware ignition +
     * throttle + terminal commit — the proven 97% burn), then OVERRIDES a_lat with MPPI's planned
     * steering, faded during the powered burn so the arrest stays ~vertical (soft touchdown). */
    hoverslam_step(st, g);
    g->mode = GM_MPPI;
    /* D-009 (the D-003 lesson at the MPPI layer): fading to ZERO let residual cross-range velocity
     * ride to touchdown (near-seeds hit dead-center yet crashed at td_v 6-8). Below the fade, BLEND
     * back into HOVERSLAM'S OWN a_lat (already in g from hoverslam_step above) — the proven tier-0
     * touchdown law, whose position-seek fades but whose velocity-null damping persists to contact.
     * s=1: pure MPPI plan. s→0: pure tier-0 arrest (which itself feathers/holds through ignition —
     * the D-009 ignition-attitude fix — and the (1-s) blend inherits it). */
    double s=1.0;
    if(st->engine_on){
        s=h_feet0/400.0; if(s>1.0)s=1.0; if(s<0.0)s=0.0; s*=s;
        if(st->ign_timer>=0.0 && st->ign_timer<2.0) s=0.0;     /* post-ignition hold: pure hoverslam(=0) */
    } else {
        double fe=(h_feet0 - M->ignite_h)/450.0; if(fe<0.0)fe=0.0; if(fe>1.0)fe=1.0;
        s=fe;                                                   /* pre-ignition feather toward hoverslam */
    }
    g->a_lat[0] = s*M->ubar[0][1] + (1.0-s)*g->a_lat[0];
    g->a_lat[1] = s*M->ubar[0][2] + (1.0-s)*g->a_lat[1];

    /* shift by one knot; hold the tail */
    for(int t=0;t<MPPI_H-1;t++)
        for(int c=0;c<MPPI_NCH;c++)
            M->ubar[t][c]=M->ubar[t+1][c];
    for(int c=0;c<MPPI_NCH;c++) M->ubar[MPPI_H-1][c]=M->ubar[MPPI_H-2][c];
}
