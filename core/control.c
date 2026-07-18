/* control.c — inner attitude loop + allocation. Runs at 500 Hz. */
#include "control.h"
#include "constants.h"
#include "atmosphere.h"
#include <math.h>

/* grid-fin transonic authority dip (mirror of dynamics.c fin_dip). */
static double fin_dip_ctrl(double M){
    if(M>0.8 && M<1.2) return 0.55;
    if(M>2.0) return 0.80;
    if(M>=0.6 && M<=0.8) return 1.0 + (0.55-1.0)*(M-0.6)/0.2;
    if(M>=1.2 && M<=2.0) return 0.55 + (0.80-0.55)*(M-1.2)/0.8;
    return 1.0;
}
/* body aero mirrors of dynamics.c (control ESTIMATE for trim feedforward; plant is truth).
 * Keep in sync with dynamics.c AERO_CN table and xcp_frac. */
static double body_cna_ctrl(double M){
    static const double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0};
    static const double CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
    if(M<=XM[0])return CN[0]; if(M>=XM[8])return CN[8];
    for(int i=0;i<8;i++) if(M<=XM[i+1]){ double t=(M-XM[i])/(XM[i+1]-XM[i]); return CN[i]+t*(CN[i+1]-CN[i]); }
    return CN[8];
}
static double xcp_frac_ctrl(double M, double alpha){
    double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));
    double amod=0.015*fmin(fabs(alpha)/(15.0*DEG2RAD),1.0);
    return base-amod;
}

BL_HD void control_step(const State* st, const GuidanceCmd* g, const EnvCtx* env, Actuators* act){
    (void)env;
    const double* y=st->y;
    double q[4]={y[S_QX],y[S_QY],y[S_QZ],y[S_QW]};
    double w[3]={y[S_WX],y[S_WY],y[S_WZ]};
    MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
    double h_base = y[S_RZ] - mp.com;
    AtmoOut atm; atmo_eval(y[S_RZ],&atm);
    double vv[3]={y[S_VX],y[S_VY],y[S_VZ]};
    double speed=v3_norm(vv); double mach=speed/atm.a; double qbar=0.5*atm.rho*speed*speed;

    act->throttle = g->throttle;
    act->engine_cmd = g->engine_cmd;
    act->n_eng = g->n_eng>0? g->n_eng : 1;
    act->deploy_cmd = g->deploy_cmd;
    act->rcs_dm = 0.0;

    /* Lateral steering SIGN depends on whether aero LIFT or thrust-vectoring dominates the response
     * to a body tilt. A base-first body's aero lift points OPPOSITE the tilt; thrust points TOWARD it.
     * Per rad of tilt the aero lateral authority is ~qbar*Aref*CNa and the thrust authority is ~T, so
     * the NET response flips sign at qbar*Aref*CNa == T. Negate the steering demand while aero
     * dominates: the whole unpowered aero-descent (no thrust) AND the EARLY landing burn at high qbar.
     * Without this the burn tilts toward the pad while aero shoves the vehicle the other way, so it
     * overshoots inbound and drifts back off-pad (the AERO_OFFSET failure; instrumented vrad flips
     * outward at the aero/thrust crossover ~22 kPa — DECISIONS D-006/D-007). */
    double ramp0 = st->engine_on?ignition_ramp(st->ign_timer):0.0;
    double thr_est0 = (g->n_eng>0?g->n_eng:1)*engine_thrust(y[S_THR],atm.p)*ramp0;
    double aero_lat_auth = qbar*VEH_AREF*body_cna_ctrl(mach);
    /* Signed steering-authority factor in [-1,1]: +1 when thrust dominates (tilt TOWARD pad), -1 when
     * aero dominates (tilt AWAY), smoothly through 0 at the crossover where a body tilt makes ~no net
     * lateral force. A HARD sign flip caused a tilt-reversal transient (the body still tilted the old
     * way while the newly-dominant effector shoved it outbound — seen in the instrumented vrad); the
     * smooth blend fades steering through the dead zone instead of snapping it. */
    double steer_sign = 1.0;
    if(env->fins_deployed && qbar>200.0){
        /* D-009 ROOT-CAUSE FIX #1: during the powered LANDING burn the SRP plume shields body lift
         * up to ~95% (dynamics.c C_T blend), so comparing thrust against UN-shielded aero authority
         * wrongly concludes aero still competes (830 vs 790 kN at 30 kPa -> steer_sign ~0) and
         * muzzles the burn's steering for its whole duration (measured: -84 m/s commanded, -8%
         * realized -> the ~140 m wind floor). Weight the aero term by the SAME shielding factor the
         * plant uses so the crossover reflects the REAL aero authority. Unpowered descent (thrust~0,
         * shield=1) is unchanged; TERMINAL (fins stowed) never enters this block. */
        double shield = 1.0;
        if(thr_est0 > 1000.0 && qbar > 1.0){
            double CT = thr_est0/(qbar*VEH_AREF);
            if(CT >= 3.0)      shield = 0.05;
            else if(CT > 0.5)  shield = 1.0 - 0.95*(CT-0.5)/2.5;
        }
        double aero_eff = aero_lat_auth*shield;
        double denom = fmax(thr_est0, aero_eff); if(denom<1.0) denom=1.0;
        steer_sign = (thr_est0 - aero_eff)/denom;
    }
    /* desired thrust-axis direction in world */
    double a_lat[2]={g->a_lat[0]*steer_sign, g->a_lat[1]*steer_sign};
    double a_vert_ref = G0 + 2.0;
    /* D-009: during the high-thrust ENTRY burn the true vertical specific force is ~n*T/m ~ 50
     * m/s^2, not G0+2 — map a_lat->tilt against THAT, or a commanded bank over-produces lateral
     * ~3.4x (the naive-bank 17 km catastrophe's second ingredient). Roughly doubles the burn's
     * realized divert authority (study: 25.6 km phys vs 11.8 km at the old mapping) — needed for
     * the far-offset tail. Gated to PH_ENTRY_BURN -> TERMINAL/AERO steering is byte-identical. */
    if(st->phase==PH_ENTRY_BURN || (st->phase==PH_LANDING_BURN && env->fins_deployed)){
        /* D-009 FIX #2: the same true-specific-force mapping for the fins-deployed LANDING burn —
         * a_vert_ref=G0+2 clamped the burn's lateral authority to 3.16 m/s^2 (amax=11.8*tan(cap))
         * while the real specific force is ~26 m/s^2. With the correct reference the same tilt cap
         * yields ~3x the claw-back authority against the wind seed. FINS-GATED: TERMINAL's landing
         * burn (fins stowed) keeps the original mapping byte-identically. */
        double thr_e  = act->n_eng * engine_thrust(y[S_THR], atm.p) * ramp0;
        double avert_e = (mp.m>1.0)? thr_e/mp.m : a_vert_ref;
        if(avert_e>a_vert_ref) a_vert_ref=avert_e;
    }
    /* tilt cap scheduled by altitude: generous up high for lateral divert, vertical near
     * ground so touchdown is on all four legs at the profile speed. */
    double tmax_hi = env->fins_deployed ? 15.0 : 12.0;   /* fins-deployed aero-divert gets more AoA; TERMINAL keeps 12 */
    double tilt_max = (4.0 + (tmax_hi-4.0)*((h_base-20.0)/180.0))*DEG2RAD;
    if(tilt_max<4.0*DEG2RAD) tilt_max=4.0*DEG2RAD;
    if(tilt_max>tmax_hi*DEG2RAD) tilt_max=tmax_hi*DEG2RAD;
    /* side-load limit (§5.7): shrink the AoA cap where dynamic pressure is high — only in the
     * unpowered aero-descent (fins deployed); the powered landing burn is unconstrained. */
    if(env->fins_deployed){
        /* STRUCT side-load line is 15 deg and aero-descent qbar peaks only ~36 kPa (audit P4), so
         * allow up to 15 deg AoA for divert authority: the 12 deg cap left cross-range decel at only
         * ~2.5 m/s² — too weak to null v_xy at the pad, so the divert overshoots off-pad (D-007). */
        /* FLAT 15 deg for max aero-divert authority (raises the divert ceiling — MPPI-2 found the far
         * seeds were ceiling-limited when qcap dropped to ~12 deg at 38 kPa). The sim's STRUCT line is
         * qbar>80 kPa (NOT an AoA side-load), and the aero-descent qbar peaks only ~36-39 kPa, so 15
         * deg is safe throughout; only soften above 50 kPa (which the descent never reaches). (D-007) */
        double qcap=15.0*DEG2RAD;
        if(qbar>50000.0){ qcap=(15.0 - 4.0*((qbar-50000.0)/30000.0))*DEG2RAD; if(qcap<9.0*DEG2RAD)qcap=9.0*DEG2RAD; }
        if(tilt_max>qcap) tilt_max=qcap;
    }
    double amax = a_vert_ref*tan(tilt_max);
    double alat_mag = sqrt(a_lat[0]*a_lat[0]+a_lat[1]*a_lat[1]);
    if(alat_mag>amax && alat_mag>1e-9){ double s=amax/alat_mag; a_lat[0]*=s; a_lat[1]*=s; }
    if(g->solver_flags&SF_TERMINAL){ a_lat[0]*=0.2; a_lat[1]*=0.2; }
    double zdes[3]={a_lat[0],a_lat[1],a_vert_ref};
    v3_normalize(zdes,zdes);

    double zbody_w[3]; double zb[3]={0,0,1}; q_rot(zbody_w,q,zb);
    double errw[3]; v3_cross(errw,zbody_w,zdes);        /* ~ sin(angle) * axis, world */
    double errb[3]; q_rot_inv(errb,q,errw);             /* to body */

    /* thrust + dynamic-pressure regime (determines effector + gain schedule) */
    double thr = act->n_eng * engine_thrust(y[S_THR], atm.p) * (st->engine_on?ignition_ramp(st->ign_timer):0.0);
    int gimbal_active = (thr > 5000.0);
    int fins_active = (!gimbal_active && env->fins_deployed && qbar>200.0);

    /* PD gains scheduled by transverse inertia AND effector. Fins at high qbar are very
     * powerful and rate-limited (20 deg/s) -> the loop must be slower + heavily damped or it
     * oscillates (tilt swinging 2..24 deg). Gimbal/RCS keep the sluggish-plant tune. */
    double wn = fins_active ? 1.1 : 1.5;
    double zeta = fins_active ? 1.3 : 1.1;
    double Kp = mp.I_tr*wn*wn;
    double Kd_design = mp.I_tr*2.0*zeta*wn;
    /* Damping augmentation: the plant's aero pitch damping (Cmq) already damps rate. Subtract
     * it from the controller's rate gain so the gimbal/fins don't DOUBLE-damp and waste
     * authority fighting rate the air already kills — that is what made TERMINAL sluggish the
     * moment Cmq was added. Total closed-loop damping ≈ the designed value. (DECISIONS D-005.) */
    double zc=mp.com, L=VEH_LEN;
    double Jd = L*L*L/3.0 - zc*L*L + zc*zc*L;
    double Cmq_aero = (speed>5.0)? 0.5*atm.rho*speed*BODY_CMQ_CDC*VEH_DIA*Jd : 0.0;
    double Kd = Kd_design - Cmq_aero; if(Kd < 0.1*Kd_design) Kd = 0.1*Kd_design;
    double tau_cmd[3];
    tau_cmd[0] = Kp*errb[0] - Kd*w[0];
    tau_cmd[1] = Kp*errb[1] - Kd*w[1];
    /* roll: damp only (no heading requirement) */
    double Kd_roll = mp.I_ax*2.0*0.9*1.0;
    tau_cmd[2] = -Kd_roll*w[2];

    /* TRIM FEEDFORWARD (DECISIONS D-005): the bare body is marginally UNSTABLE, so a pure PD
     * produces the moment to hold a commanded AoA only from tracking error -> it trims at a
     * fraction of command and (unstable) can diverge past stall. Cancel the body aero moment
     * directly so the fins/gimbal statically re-trim the airframe to the commanded AoA and the
     * PD's only job is damping residuals. Uses the shared coefficient mirrors (control estimate). */
    if(speed>5.0 && qbar>50.0){
        double vrelb[3]; q_rot_inv(vrelb,q,vv);
        double sp=v3_norm(vrelb);
        if(sp>5.0){
            double vhat[3]; v3_scale(vhat,vrelb,1.0/sp);
            double cosa=-vhat[2]; if(cosa>1)cosa=1; if(cosa<-1)cosa=-1;
            double alpha=acos(cosa);
            double lat2=vhat[0]*vhat[0]+vhat[1]*vhat[1];
            if(lat2>1e-12){
                double latm=sqrt(lat2), lh[3]={vhat[0]/latm,vhat[1]/latm,0.0};
                double Fn=qbar*VEH_AREF*body_cna_ctrl(mach)*alpha;
                double Faero[3]={-Fn*lh[0],-Fn*lh[1],0.0};
                double arm[3]={0,0,xcp_frac_ctrl(mach,alpha)*VEH_STAGE_LEN - mp.com};
                double Ta[3]; v3_cross(Ta,arm,Faero);
                tau_cmd[0]-=Ta[0]; tau_cmd[1]-=Ta[1];   /* cancel body aero moment */
            }
        }
    }

    double g0=0.0, g1=0.0;
    double rcs[3]={0,0,0};
    double fins[4]={0,0,0,0};

    if(gimbal_active){
        /* gimbal handles pitch/yaw: Tb_x=com*thr*sin(g1); Tb_y=-com*thr*sin(g0) */
        double denom = mp.com*thr;
        double sg1 = tau_cmd[0]/denom;   /* -> g1 */
        double sg0 = -tau_cmd[1]/denom;  /* -> g0 */
        double smax=sin(ENG_GIMBAL_MAX);
        if(sg0>smax)sg0=smax; if(sg0<-smax)sg0=-smax;
        if(sg1>smax)sg1=smax; if(sg1<-smax)sg1=-smax;
        g0=asin(sg0); g1=asin(sg1);
        rcs[2]=tau_cmd[2];               /* roll via RCS (gimbal cannot roll) */
    } else if(fins_active){
        /* fins handle pitch/yaw (Agent A orthogonal patterns): pitch [-1,-1,1,1] -> tau_x,
         * yaw [1,-1,-1,1] -> tau_y, roll [1,1,1,1] -> tau_z (weak). RCS trims roll. */
        double CNa_f=FIN_CNA*fin_dip_ctrl(mach);
        double k=qbar*FIN_AREA*CNa_f;
        double A=(FIN_Z-mp.com)*k;               /* pitch/yaw gain scale */
        double C=RCS_ARM*(FIN_CT_DELTA_FRAC*k);  /* roll gain scale (Rf=VEH_RADIUS~RCS_ARM) */
        /* Patterns derived to MATCH the plant fin torque tau_x=A*0.707*(d1+d2-d3-d4),
         * tau_y=-A*0.707*(d1-d2-d3+d4). The earlier [-1,-1,1,1]/[1,-1,-1,1] gave the WRONG
         * SIGN (positive feedback -> divergence to stall). Correct: pitch [1,1,-1,-1],
         * yaw [-1,1,1,-1]; unit-gain divisor 4*0.707*A. (DECISIONS D-005.) */
        double patP[4]={1,1,-1,-1}, patY[4]={-1,1,1,-1};
        /* P5 MOVE-THE-TRIM-POINT (audit): feedforward the fin deflection that TRIMS the airframe
         * AT the commanded AoA (commanded tilt-from-vertical, body axes), so the fins hold a
         * nonzero AoA at zero PD error instead of relaxing to aligned (the drift-to-2deg bug).
         * K_ff~0.73 rad-fin/rad-AoA, ~Mach-invariant. Plus a slow deflection-domain integral with
         * conditional-integration anti-windup for coeff-estimate / disturbance error. */
        double upz[3]={0,0,1}, tiltw[3]; v3_cross(tiltw,upz,zdes);   /* commanded AoA axis (world) */
        double tiltb[3]; q_rot_inv(tiltb,q,tiltw);                   /* -> body pitch/yaw components */
        const double Kff=0.73, Ki=0.73/1.5;                          /* Ki = Kff/tau_i, tau_i=1.5 s */
        double dpitch_pd = (fabs(A)>1.0)? tau_cmd[0]/(4.0*0.7071*A):0.0;
        double dyaw_pd   = (fabs(A)>1.0)? tau_cmd[1]/(4.0*0.7071*A):0.0;
        double droll  = (fabs(C)>1.0)? -tau_cmd[2]/(4.0*C):0.0;  /* plant gives tau_z=-4C*droll -> negate */
        double dpitch = Kff*tiltb[0] + Ki*act->fin_eint[0] + dpitch_pd;
        double dyaw   = Kff*tiltb[1] + Ki*act->fin_eint[1] + dyaw_pd;
        int sat=0;
        for(int i=0;i<4;i++){
            double d=droll + dpitch*patP[i] + dyaw*patY[i];
            if(d>FIN_DEFL_MAX){d=FIN_DEFL_MAX;sat=1;} if(d<-FIN_DEFL_MAX){d=-FIN_DEFL_MAX;sat=1;}
            fins[i]=d;
        }
        /* integral update: AoA error = commanded tilt - measured tilt (body axes); freeze on sat */
        double zbw[3],zbb2[3]={0,0,1}; q_rot(zbw,q,zbb2);
        double measw[3]; v3_cross(measw,upz,zbw); double measb[3]; q_rot_inv(measb,q,measw);
        if(!sat){
            double emax=8.0*DEG2RAD/Ki;
            act->fin_eint[0]+=(tiltb[0]-measb[0])*DT; act->fin_eint[1]+=(tiltb[1]-measb[1])*DT;
            for(int j=0;j<2;j++){ if(act->fin_eint[j]>emax)act->fin_eint[j]=emax; if(act->fin_eint[j]<-emax)act->fin_eint[j]=-emax; }
        }
        rcs[2]=0.5*tau_cmd[2];           /* RCS assists the weak fin roll */
    } else {
        /* low qbar, no thrust: RCS provides everything (low authority) */
        rcs[0]=tau_cmd[0]; rcs[1]=tau_cmd[1]; rcs[2]=tau_cmd[2];
    }
    /* RCS saturation: max torque per axis (2 nozzles * force * arm) with deadband */
    double rcs_max = 2.0*RCS_FORCE*RCS_ARM;
    double db = 30.0;   /* Nm deadband (PWM-averaged model) */
    double n2flow=0.0;
    for(int k=0;k<3;k++){
        if(fabs(rcs[k])<db){ rcs[k]=0.0; continue; }
        if(rcs[k]> rcs_max) rcs[k]= rcs_max;
        if(rcs[k]<-rcs_max) rcs[k]=-rcs_max;
        n2flow += fabs(rcs[k])/(RCS_ARM*RCS_ISP*G0);   /* rough N2 usage */
    }
    if(st->N2<=0.0){ rcs[0]=rcs[1]=rcs[2]=0.0; n2flow=0.0; }

    act->gimbal[0]=g0; act->gimbal[1]=g1;
    act->rcs_torque[0]=rcs[0]; act->rcs_torque[1]=rcs[1]; act->rcs_torque[2]=rcs[2];
    act->rcs_dm = n2flow;
    act->fins[0]=fins[0]; act->fins[1]=fins[1]; act->fins[2]=fins[2]; act->fins[3]=fins[3];
}
