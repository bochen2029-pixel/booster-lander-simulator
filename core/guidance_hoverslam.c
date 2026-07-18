/* guidance_hoverslam.c — forward-shooting ignition solver + constant-decel tracking. */
#include "guidance_hoverslam.h"
#include "dynamics.h"
#include "atmosphere.h"
#include "constants.h"
#include "contact.h"
#include <math.h>

/* ---- Entry-burn supervisor predictor (E3, Agent A §9) ----
 * Forward-shoot a cheap ballistic (no-thrust) vertical channel from the current state to the
 * ground, tracking peak dynamic pressure. This is the PREDICTIVE trigger for the 3-engine entry
 * burn: ignite while the predicted-to-touchdown peak qbar would breach the structural line, cut
 * once the remaining descent is predicted to stay under it (which self-consistently caps the
 * downstream aero-descent qbar too — Agent A). Coarse (dt=1 s), a trigger not the plant: the real
 * qbar is what dynamics.c integrates and sim.c enforces. Effective axial drag CA includes the
 * DEPLOYED GRID FINS (ENTRY flies fins-out): body CA~1.0 + fin drag => CA_eff~1.5 (ref VEH_AREF).
 * Ignoring the fins (CA=1.0) over-predicts the peak (carries velocity deeper than the real finned
 * descent) so the burn over-decelerates to the fuel floor; a calibrated CA cuts with fuel to spare. */
#define ENTRY_PRED_CA   1.0
BL_HD double entry_predict_peak_qbar(double h0, double speed0, double mass){
    double h=h0, v=speed0;               /* v: downward speed magnitude [m/s] */
    double qmax=0.0;
    const double dt=1.0;
    for(int i=0;i<600 && h>0.0 && v>1.0; i++){
        AtmoOut atm; atmo_eval(h,&atm);
        double q=0.5*atm.rho*v*v;
        if(q>qmax) qmax=q;
        double gh=G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));
        double drag=0.5*atm.rho*v*v*VEH_AREF*ENTRY_PRED_CA;   /* decelerates the fall */
        double a=gh - drag/mass;                              /* net downward accel */
        v += a*dt; if(v<0.0) v=0.0;
        h -= v*dt;
    }
    return qmax;
}

#define LANDING_IGNITE_MARGIN 150.0   /* [m] ignite the fins-deployed landing burn when a full-thrust+aero
                                       * shoot arrests within this of the ground. (D-009: 220 was tried
                                       * for the 6-9 m/s marginal-arrest band and made td_v WORSE overall
                                       * — the longer burn adds trim/tilt exposure; reverted.) */
/* ---- Aero-aware suicide-burn feasibility (fins-deployed handoffs) ----
 * Forward-shoot FULL 1-engine thrust + gravity + aero drag (body + deployed fins, CA_eff≈1.5) from
 * the current vertical state; return the altitude MARGIN [m] by which the vehicle arrests (vz→0)
 * before the ground. The plain v_ref ignition ignores aero drag, so it lights the landing burn far
 * too high (ENTRY: 17 km → a 57 s min-throttle fuel-waster; AERO: 5.3 km → cuts off the aero-descent
 * divert). Igniting only when this margin nears zero lets aero do the free deceleration through a
 * high/fast descent, then a short HARD suicide burn low down — more fuel AND more coast-divert time.
 * Coarse (0.1 s), a trigger not the plant. Gated to fins-deployed; TERMINAL keeps the v_ref trigger. */
static double suicide_burn_margin(double h_feet, double vz, double m){
    double h=h_feet, v=vz;   /* v negative = descending */
    const double dt=0.1;
    /* NO drag credit: the landing burn is RETROPROPULSIVE, so the SRP plume shields the airframe
     * from aero drag (dynamics.c cuts CA up to ~95% when the thrust coefficient is high). Crediting
     * drag here (an earlier CA=1.5 version) ignited far too late and slammed in at 157 m/s. Thrust
     * alone must be able to arrest — the (shielded) drag is only a safety bonus. */
    for(int i=0;i<3000 && v<0.0 && h>-50.0; i++){
        AtmoOut atm; atmo_eval(h,&atm);
        double T=engine_thrust(1.0, atm.p);                  /* full 1-engine */
        double gh=G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));
        double a=T/m - gh;                                   /* thrust vs gravity, up positive */
        v += a*dt; h += v*dt;                                /* v<0 descends; stops as v→0 */
    }
    return h;   /* altitude remaining at vz=0; <=0 means it would hit the ground still moving */
}

BL_HD void hoverslam_step(const State* st, GuidanceCmd* g){
    const double* y=st->y;
    MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
    double m=mp.m;
    double h_base = y[S_RZ]-mp.com;
    double vz = y[S_VZ];
    double r_xy[2]={y[S_RX],y[S_RY]};
    double v_xy[2]={y[S_VX],y[S_VY]};

    g->mode=GM_HOVERSLAM; g->n_eng=1; g->solver_flags=0;
    g->deploy_cmd = (h_base<=LEG_DEPLOY_H)?1:0;
    g->a_lat[0]=0; g->a_lat[1]=0;
    g->t_go = (vz<-0.1)? (h_base/(-vz)) : 5.0;

    AtmoOut atm; atmo_eval(y[S_RZ],&atm);
    double Tfull = g->n_eng*engine_thrust(1.0, atm.p);

    /* Unified suicide-burn reference: v_ref(h) = -sqrt(vtd^2 + 2 a_design hgo). a_design is
     * FIXED (pinned to a landing-mass reference), not recomputed from current mass — a
     * mass-varying a_design steepens the profile as propellant drains, dropping the vehicle
     * behind it into a min-throttle hover. Ignite the instant the vehicle falls onto the
     * profile; then throttle-track with lag-correcting feedback. */
    double vtd=TD_V_TARGET;
    /* body tilt (angle of thrust axis from vertical) */
    double zw[3],zb0[3]={0,0,1}; q_rot(zw,&y[S_QX],zb0);
    double ctilt=zw[2]; if(ctilt>1)ctilt=1; if(ctilt<-1)ctilt=-1; double tilt=acos(ctilt);
    /* Aim the profile at the feet reaching the ground at vtd. Main descent uses the clean
     * axial feet height (tilt-independent, so vertical guidance never couples to steering
     * tilt). Below 30 m, correct for the tilted low foot contacting early (small, stable
     * tilt there) so a slightly canted booster still arrives at vtd on its low foot.
     * Nulling velocity above contact is fatal (cannot hover, TWR>1). */
    double h_feet = h_base - 1.0*st->deploy_frac;
    if(h_feet < 15.0) h_feet -= 0.6*LEG_RADIUS*sin(tilt);   /* small terminal tilt correction */
    double hgo = h_feet; if(hgo<0.02) hgo=0.02;
    /* a_design: pre-ignition, sized from CURRENT mass (so the ignition altitude matches the
     * run's actual thrust-to-weight); frozen at ignition (st->ada) so the profile does not
     * steepen as propellant drains (which would drop the vehicle into a min-throttle hover). */
    double a_max_now = Tfull/m - G0; if(a_max_now<1.0) a_max_now=1.0;
    /* Design decel fraction: TERMINAL 0.58 (D-002 tuned). Fins-deployed = high-energy aero handoff,
     * ignites LOW via the feasibility trigger below, so it needs a HARD near-full-throttle suicide
     * burn (short → less gravity loss → affordable from the fast handoff). */
    double base_frac = st->fins_deployed ? 0.85 : 0.58;
    double a_design = st->engine_on ? st->ada : base_frac*a_max_now;
    double v_ref = -sqrt(vtd*vtd + 2.0*a_design*hgo);

    /* ---- lateral steering (shared by aero-descent AND landing burn) ----
     * Command an INWARD velocity proportional to offset (capped), then track it: a first-order
     * position loop -> exponential decay, NO overshoot. Fade the position-SEEKING part near the
     * ground but keep the velocity-NULL damping to contact (Agent C: fading the whole command
     * opens the loop mid-oscillation and residual lateral velocity flings it off-pad). During
     * the unpowered aero-descent this same a_lat is realized by BODY ANGLE-OF-ATTACK (control
     * tilts the airframe with grid fins; body lift steers) — Agent A: body lift ~4x fin lift. */
    double vspeed=sqrt(v_xy[0]*v_xy[0]+v_xy[1]*v_xy[1]+vz*vz);
    double qbar_g=0.5*atm.rho*vspeed*vspeed;
    double r_mag = sqrt(r_xy[0]*r_xy[0]+r_xy[1]*r_xy[1]);
    /* Inward-velocity target. The binding limit for the aero divert is NULLING the cross-range
     * velocity before the burn: residual inward velocity gets reversed AT the aero/thrust crossover
     * (~22 kPa) where lateral authority vanishes, so it overshoots outbound. */
    double Kvel = st->fins_deployed ? 1.2 : 0.6;
    double vdes_mag;
    if(st->fins_deployed){
        /* Decelerating (bang-bang-ish) profile: vdes = sqrt(2·a_decel·r) commands the inward velocity
         * that can still be nulled to zero by r=0 at a_decel — so the vehicle arrives at the pad
         * (and the crossover) with v_xy≈0 instead of carrying velocity into the dead zone. a_decel is
         * CONSERVATIVE (reverses early) to beat the sluggish attitude/fin reversal lag. (D-007) */
        double A_DECEL = 1.5, vlat_max = 35.0, T_LEAD = 2.0;
        /* velocity LEAD: decelerate based on where the vehicle WILL be (r + v_rad·t_lead), so the
         * cross-range reversal starts ~2 s early — enough to beat the fin-rate-limited attitude
         * reversal lag that otherwise overshoots the pad. (crude pre-emptive reversal; MPPI does it
         * properly.) v_rad + = outward. (D-007) */
        double v_rad_cur = (r_mag>1e-3)?(v_xy[0]*r_xy[0]+v_xy[1]*r_xy[1])/r_mag:0.0;
        double r_pred = r_mag + v_rad_cur*T_LEAD; if(r_pred<0.0) r_pred=0.0;
        vdes_mag = sqrt(2.0*A_DECEL*r_pred); if(vdes_mag>vlat_max) vdes_mag=vlat_max;
    } else {
        /* TERMINAL final-approach: gentle linear law (D-002/003, load-bearing velocity-null). */
        double Kpos = 0.20, vlat_max = 40.0;
        vdes_mag = Kpos*r_mag; if(vdes_mag>vlat_max) vdes_mag=vlat_max;
    }
    double vdes[2]={0,0};
    if(r_mag>1e-3){ vdes[0]=-vdes_mag*r_xy[0]/r_mag; vdes[1]=-vdes_mag*r_xy[1]/r_mag; }
    double lat_scale = (h_feet-30.0)/90.0; if(lat_scale>1.0)lat_scale=1.0; if(lat_scale<0.0)lat_scale=0.0;
    lat_scale*=lat_scale;
    g->a_lat[0] = Kvel*(vdes[0]*lat_scale - v_xy[0]);
    g->a_lat[1] = Kvel*(vdes[1]*lat_scale - v_xy[1]);

    if(!st->engine_on){
        /* Aero-descent steering, with STABILIZE-FIRST logic. The bare body is marginally
         * unstable and grid fins lose authority in the transonic dip (0.8<M<1.2) and at stall
         * (|AoA|>~20 deg); commanding a divert while stalling/transonic just tumbles it. So:
         * steer only with dynamic pressure, fade steering hard in the transonic dip, and cut
         * steering entirely while the body is far from aligned (recover to low AoA first). */
        double machg = vspeed/atm.a;
        double sfac = 1.0;
        if(machg>0.8 && machg<1.3) sfac *= 0.5;               /* transonic fin-authority dip (reduced, not killed) */
        if(tilt > 20.0*DEG2RAD) sfac *= 0.0;                  /* only stop steering near fin stall */
        if(qbar_g < 300.0) sfac = 0.0;                        /* no bite */
        int ignite;
        if(st->fins_deployed){
            /* aero-aware: coast (aero decelerates for free) until a full-thrust+aero suicide burn
             * would only just arrest above the ground — then light it hard and low. */
            double margin = suicide_burn_margin(h_feet, vz, m);
            ignite = (vz < -1.0 && margin <= LANDING_IGNITE_MARGIN);
            /* D-009 IGNITION-ATTITUDE FEATHER (the universal ~140 m floor). Measured (ENTRY run 1):
             * the vehicle arrives at ignition CENTERED (lat 11 m, vrad -1) but the burn lights on the
             * aero-trimmed attitude — the body is deliberately tilted AWAY from the pad (aero-dominant
             * steer negation) holding ~6-12 deg AoA at ~40 kPa — so the 830 kN thrust vector's lateral
             * component shoves the vehicle OUTWARD (+15 m/s) before the attitude rights: 11 m becomes
             * ~150 m. Fix: FEATHER the steering to zero over the last ~450 m of margin (~1.5 s) so the
             * attitude loop straightens the body BEFORE light-up, and the burn ignites vertical. */
            double feather = (margin - LANDING_IGNITE_MARGIN)/450.0;
            if(feather<0.0)feather=0.0; if(feather>1.0)feather=1.0;
            sfac *= feather;
        } else {
            ignite = (vz < -1.0 && vz <= v_ref);   /* TERMINAL: original v_ref trigger, unchanged */
        }
        g->a_lat[0]*=sfac; g->a_lat[1]*=sfac;
        if(ignite){ g->engine_cmd=1; g->throttle=1.0; }
        else { g->engine_cmd=0; g->throttle=0.0; }
        return;
    }

    /* D-009 FIX #3 — DAMP-THROUGH-IGNITION (fins-deployed): during the ignition ramp command pure
     * velocity-null damping (no position seek). The earlier ZERO-hold handed the crosswind a free
     * ~2 s window that seeded +4 m/s of outward drift (the root-cause analysis' Stage 1); with the
     * steer_sign shielding fix the thrust has authority immediately, so damping from t=0 arrests
     * the seed as it forms. TERMINAL (fins stowed) unchanged. */
    if(st->fins_deployed && st->ign_timer>=0.0 && st->ign_timer<2.0){
        g->a_lat[0] = Kvel*(-v_xy[0]);
        g->a_lat[1] = Kvel*(-v_xy[1]);
    }

    /* burning: feedback-track the reference velocity profile. High Kv nulls the first-order
     * actuator-lag steady-state error, which otherwise leaves the vehicle arriving hot. */
    g->engine_cmd=1;
    double Kv = 3.0;
    double a_cmd = a_design + Kv*(v_ref - vz);       /* vz below v_ref (too fast) -> more decel */
    double D = 0.5*atm.rho*vz*vz*VEH_AREF*0.9;       /* drag helps */
    double T_need = m*(G0 + a_cmd) - D;
    double ct = cos(tilt); if(ct<0.3) ct=0.3;        /* tilted -> less vertical thrust */
    double thr = T_need/(Tfull*ct);
    if(thr<ENG_THR_MIN) thr=ENG_THR_MIN;
    if(thr>1.0) thr=1.0;
    /* commit-to-touchdown: once low, do not let the vehicle arrest above the ground and
     * climb. If it is slow and nearly stopped near contact, ride minimum throttle in. */
    if(h_feet < 6.0){
        g->solver_flags|=SF_TERMINAL;
        if(vz > -0.6) thr = ENG_THR_MIN;            /* about to reverse -> stop pushing */
    }
    g->throttle=thr;
}
