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
    double a_design = st->engine_on ? st->ada : 0.58*a_max_now;
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
    double Kpos = 0.20, vlat_max = 40.0, Kvel = 0.6;   /* vlat cap raised so the aero-divert can use the full AoA ceiling */
    double vdes_mag = Kpos*r_mag; if(vdes_mag>vlat_max) vdes_mag=vlat_max;
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
        g->a_lat[0]*=sfac; g->a_lat[1]*=sfac;
        if(vz < -1.0 && vz <= v_ref){ g->engine_cmd=1; g->throttle=1.0; }
        else { g->engine_cmd=0; g->throttle=0.0; }
        return;
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
