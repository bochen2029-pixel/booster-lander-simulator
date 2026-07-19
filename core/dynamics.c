/* dynamics.c — forces, torques, mass properties, state derivative. CLAUDE_v1.md §6. */
#include "dynamics.h"
#include "constants.h"
#include "atmosphere.h"
#include <math.h>

/* ---------- Aero coefficient tables (App-A.6, representative, frozen) ----------
 * M5 CUDA: the tables moved to FUNCTION-LOCAL static const inside dynamics_deriv (their only user)
 * so the BL_HD path is device-compilable; helpers below are BL_HD static. Values byte-identical. */
BL_HD static double table_lookup(const double* xs, const double* ys, int n, double x){
    if(x<=xs[0]) return ys[0];
    if(x>=xs[n-1]) return ys[n-1];
    for(int i=0;i<n-1;i++){
        if(x<=xs[i+1]){ double t=(x-xs[i])/(xs[i+1]-xs[i]); return ys[i]+t*(ys[i+1]-ys[i]); }
    }
    return ys[n-1];
}
/* Grid-fin normal-force slope vs Mach: transonic authority dip (lattice cells choke). */
BL_HD static double fin_dip(double M){
    if(M>0.8 && M<1.2) return 0.55;
    if(M>2.0) return 0.80;
    /* smooth blends at the edges */
    if(M>=0.6 && M<=0.8) return 1.0 + (0.55-1.0)*(M-0.6)/0.2;
    if(M>=1.2 && M<=2.0) return 0.55 + (0.80-0.55)*(M-1.2)/0.8;
    return 1.0;
}

/* CoP as fraction of stage length from base (bare body). A flat-base-leading cylinder
 * generates its crossflow force near the separated leading (base) end -> CoP ~0.29-0.32 L,
 * i.e. at/below the CoM (~0.30 L) => MARGINALLY UNSTABLE bare body, per CLAUDE_v1 §5.4/§6.3.
 * (The old 0.62 L placed CoP ~13 m aft of CoM => wrongly self-stable, stealing the fins' job
 * and contradicting the spec. See DECISIONS D-005.) */
BL_HD static double xcp_frac(double M, double alpha){
    /* Fraction of STAGE length from base. CoM(empty) ~12.27 m = 0.298 of stage length, so to
     * keep the bare body marginally UNSTABLE at ALL Mach (canon §5.4/6.3), the CoP must stay
     * below ~0.298. Audit: the old +0.03 transonic bump overshot CoM -> wrongly STABLE in
     * M0.9-1.2; reduced to +0.01 and base 0.28 so the peak (0.29) stays below CoM. */
    double base = 0.28 + 0.01*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));  /* 0.28 sub/hyper, ~0.29 transonic */
    double amod = 0.015*fmin(fabs(alpha)/(15.0*DEG2RAD),1.0);
    return base - amod;
}

BL_HD double engine_thrust(double throttle_act, double p_amb){
    if(throttle_act<=0.0) return 0.0;
    double T = ENG_T_VAC - ENG_AE*p_amb;      /* pressure-corrected (§5.3) */
    return throttle_act * T;
}
BL_HD double ignition_ramp(double it){
    if(it < ENG_IGN_T0) return 0.0;
    if(it >= ENG_IGN_T1) return 1.0;
    double x = (it-ENG_IGN_T0)/(ENG_IGN_T1-ENG_IGN_T0);
    return x*x*(3.0-2.0*x);                    /* smoothstep ramp */
}

/* ---------- Mass properties ---------- */
BL_HD void mass_props(double m_lox, double m_rp1, double mdot_lox, double mdot_rp1, MassProps* mp){
    if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
    const double rt2 = TANK_AREA/PI;            /* tank radius^2 */
    const double R2  = VEH_RADIUS*VEH_RADIUS;
    /* dry */
    double m_dry = VEH_DRY, z_dry = VEH_DRY_COMZ;
    double Iax_dry = m_dry*R2;
    double Itl_dry = m_dry*(6.0*R2 + VEH_LEN*VEH_LEN)/12.0;   /* local, about dry CoM */
    /* fluid columns */
    double h_l = m_lox/(LOX_RHO*TANK_AREA), z_l = LOX_BASE_Z + 0.5*h_l;
    double h_r = m_rp1/(RP1_RHO*TANK_AREA), z_r = RP1_BASE_Z + 0.5*h_r;
    double Iax_l = 0.5*m_lox*rt2, Itl_l = m_lox*(3.0*rt2 + h_l*h_l)/12.0;
    double Iax_r = 0.5*m_rp1*rt2, Itl_r = m_rp1*(3.0*rt2 + h_r*h_r)/12.0;
    double M = m_dry + m_lox + m_rp1;
    double com = (m_dry*z_dry + m_lox*z_l + m_rp1*z_r)/M;
    /* transverse via parallel axis to CoM */
    double I_tr = Itl_dry + m_dry*(z_dry-com)*(z_dry-com)
                + Itl_l   + m_lox*(z_l-com)*(z_l-com)
                + Itl_r   + m_rp1*(z_r-com)*(z_r-com);
    double I_ax = Iax_dry + Iax_l + Iax_r;
    /* analytic Idot: dI/dm_i * mdot_i (coupling through com cancels, App-A.4 derivation) */
    double dz_l = 1.0/(2.0*LOX_RHO*TANK_AREA);          /* dz/dm */
    double dz_r = 1.0/(2.0*RP1_RHO*TANK_AREA);
    double dItl_l = (3.0*rt2 + h_l*h_l)/12.0 + m_lox*h_l/(6.0*LOX_RHO*TANK_AREA);
    double dItl_r = (3.0*rt2 + h_r*h_r)/12.0 + m_rp1*h_r/(6.0*RP1_RHO*TANK_AREA);
    double dItr_dml = dItl_l + (z_l-com)*(z_l-com) + 2.0*m_lox*(z_l-com)*dz_l;
    double dItr_dmr = dItl_r + (z_r-com)*(z_r-com) + 2.0*m_rp1*(z_r-com)*dz_r;
    mp->m=M; mp->com=com; mp->I_ax=I_ax; mp->I_tr=I_tr;
    mp->Idot_tr = dItr_dml*mdot_lox + dItr_dmr*mdot_rp1;
    mp->Idot_ax = 0.5*rt2*(mdot_lox + mdot_rp1);
}

/* ---------- State derivative ---------- */
BL_HD void dynamics_deriv(const State* st, const Actuators* act, const EnvCtx* env,
                          double dy[NSTATE], Diag* diag){
    /* aero tables (function-local static const for device compilation; byte-identical values) */
    static const double AERO_M[9]  = {0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0};
    static const double AERO_CA[9] = {0.85,0.88,1.10,1.40,1.25,1.10,0.95,0.92,0.90};
    static const double AERO_CN[9] = {2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
    const double* y = st->y;
    double r[3]={y[S_RX],y[S_RY],y[S_RZ]};
    double v[3]={y[S_VX],y[S_VY],y[S_VZ]};
    double q[4]={y[S_QX],y[S_QY],y[S_QZ],y[S_QW]};
    double w[3]={y[S_WX],y[S_WY],y[S_WZ]};
    double m_lox=y[S_MLOX], m_rp1=y[S_MRP1];
    double h = r[2];

    AtmoOut atm; atmo_eval(h,&atm);

    /* INJECT_DISTURBANCE scales (0 read as nominal so zero-init callers stay bit-identical). */
    double tscale = (env->thrust_scale>0.0)? env->thrust_scale : 1.0;
    double iscale = (env->isp_scale  >0.0)? env->isp_scale   : 1.0;

    /* thrust magnitude (all engines) with ignition ramp */
    double ramp = st->engine_on ? ignition_ramp(st->ign_timer) : 0.0;
    double thr_per = engine_thrust(y[S_THR], atm.p) * ramp * tscale;
    double thrust = st->n_eng * thr_per;

    /* mass flow (split by mixture ratio) */
    double Isp = (thrust>0.0)? iscale*(ENG_ISP_VAC - (ENG_ISP_VAC-ENG_ISP_SL)*(atm.p/P0_ATM)) : ENG_ISP_SL;
    double mdot_total = (thrust>0.0)? (thrust/(Isp*G0)) : 0.0;   /* positive rate leaving */
    double mdot_rp1 = mdot_total/(1.0+MIX_RATIO);
    double mdot_lox = mdot_total - mdot_rp1;
    if(m_lox<=0.0||m_rp1<=0.0){ mdot_lox=mdot_rp1=0.0; thrust=0.0; thr_per=0.0; }

    MassProps mp; mass_props(m_lox,m_rp1,-mdot_lox,-mdot_rp1,&mp);
    double m = mp.m, com = mp.com;

    /* ---- forces in body frame ---- */
    double Fb[3]={0,0,0};        /* body forces (non-gravity) */
    double Tb[3]={0,0,0};        /* body torques about CoM */

    /* thrust: direction from gimbal (mostly +Z body), applied at base (0,0,0) */
    double g0=y[S_G0], g1=y[S_G1];
    double tdir[3]={ sin(g0), sin(g1), 0.0 };
    double tz2 = 1.0 - tdir[0]*tdir[0] - tdir[1]*tdir[1];
    tdir[2] = (tz2>0.0)? sqrt(tz2) : 0.0;
    double Fthr[3]; v3_scale(Fthr,tdir,thrust);
    v3_add(Fb,Fb,Fthr);
    /* CoM laterally offset by the disturbance → the base-mounted thrust gains a moment arm (thrust
     * misalignment). com_offset defaults to 0 (nominal, no torque).
     * N0 ENGINE-OUT (§4.6): the surviving-cluster centroid offset thrust_offset sums into the arm —
     * the induced torque rides the EXISTING arm_thr × Fthr lever (no new torque term; §B.2). Both
     * default (0,0) => arm_thr identical to v1 => byte-identical when no engine is out. This is the
     * ONLY EOM edit; it affects the THRUST moment only (com_offset/thrust_offset touch nothing else). */
    double arm_thr[3]={ -(env->com_offset[0]+env->thrust_offset[0]),
                        -(env->com_offset[1]+env->thrust_offset[1]),
                        -com };                                          /* r_gimbal - r_com */
    double Tthr[3]; v3_cross(Tthr,arm_thr,Fthr); v3_add(Tb,Tb,Tthr);

    /* aero */
    double vrel_w[3]; v3_sub(vrel_w,v,env->wind_world);
    double vrel_b[3]; q_rot_inv(vrel_b,q,vrel_w);
    double speed=v3_norm(vrel_b);
    double mach = speed/atm.a;
    double qbar = 0.5*atm.rho*speed*speed;
    double alpha=0.0;
    double Faero_b[3]={0,0,0};
    /* SRP aero shielding while burning (§6.3): blend by thrust coefficient. Computed ONCE here and
     * applied to BOTH the body aero AND the grid fins (D-009 plant correction): the plume envelops
     * the vehicle from the base and the fins — the farthest-DOWNSTREAM surfaces in a base-first
     * descent — sit deep in the disturbed wake. Canon §6.3 says "aero forces blend out with C_T",
     * not "body only". Un-shielded fins passed the full crosswind side-force (~2-3 m/s²) at a 45 m
     * arm during the landing burn — the systematic downwind push behind the ~140 m wind floor
     * (traced: guidance commanded -84 m/s of correction, vehicle realized +6.8 outward). */
    double srp_shield = 1.0;
    if(thrust>0.0 && qbar>1e-4){
        double CT = thrust/(qbar*VEH_AREF);
        if(CT>0.5){ double t=(CT-0.5)/(3.0-0.5); if(t>1)t=1; srp_shield = 1.0 + t*(0.05-1.0); }
    }
    if(speed>0.2 && qbar>1e-4){
        double vhat[3]; v3_scale(vhat,vrel_b,1.0/speed);
        double cosa = -vhat[2]; if(cosa>1)cosa=1; if(cosa<-1)cosa=-1;
        alpha = acos(cosa);
        double CA = table_lookup(AERO_M,AERO_CA,9,mach)*srp_shield;
        double CNa = table_lookup(AERO_M,AERO_CN,9,mach);
        double CN = CNa*alpha*srp_shield;
        double Fax = -copysign(qbar*VEH_AREF*CA, vhat[2]);   /* along body Z */
        Faero_b[2]+=Fax;
        double lat[3]={vhat[0],vhat[1],0.0}; double latm=sqrt(lat[0]*lat[0]+lat[1]*lat[1]);
        if(latm>1e-6){ double lh[3]={lat[0]/latm,lat[1]/latm,0}; double Fn=qbar*VEH_AREF*CN;
            Faero_b[0]-=Fn*lh[0]; Faero_b[1]-=Fn*lh[1]; }
        double xcp = xcp_frac(mach,alpha)*VEH_STAGE_LEN;
        double arm_cp[3]={0,0,xcp-com};
        double Taero[3]; v3_cross(Taero,arm_cp,Faero_b); v3_add(Tb,Tb,Taero);
        v3_add(Fb,Fb,Faero_b);
    }

    /* Body pitch/yaw aerodynamic damping (Cmq, strip theory). A long body rotating at rate w
     * in a fast axial stream sees a distributed crossflow w*(z-z_com) that opposes the rotation:
     * tau = -0.5*rho*V*Cdc*D * J * w_perp, J = integral_0^L (z-z_com)^2 dz. Without this the
     * vehicle is an almost-undamped aero pendulum (zeta~0.03) that rings off every transient.
     * (DECISIONS D-005; roll damping about the slender axis is negligible and omitted.) */
    if(speed>5.0){
        double zc=com, L=VEH_LEN;
        double J = L*L*L/3.0 - zc*L*L + zc*zc*L;     /* integral_0^L (z-zc)^2 dz */
        double Cdamp = 0.5*atm.rho*speed*BODY_CMQ_CDC*VEH_DIA*J;
        Tb[0] -= Cdamp*w[0];
        Tb[1] -= Cdamp*w[1];
    }

    /* grid fins (Agent A model): per-fin force at its mount from local flow (incl. omega x r,
     * which is what damps rotation), radial lift + tangential roll-cant, transonic dip, stall. */
    if(env->fins_deployed && qbar>1.0){
        double CNa_f = FIN_CNA*fin_dip(mach);
        for(int i=0;i<4;i++){
            double phi=(45.0+90.0*i)*DEG2RAD;
            double er[3]={cos(phi),sin(phi),0.0};       /* radial (body) */
            double et[3]={-sin(phi),cos(phi),0.0};      /* tangential (body) */
            double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};
            double wxr[3]; v3_cross(wxr,w,rm);
            double vi[3]; v3_add(vi,vrel_b,wxr);
            double vsp=v3_norm(vi); if(vsp<1.0) continue;
            double qbi=0.5*atm.rho*vsp*vsp;
            double w_ax=-vi[2];                          /* leading is -Z body (base-first) */
            double w_r=v3_dot(vi,er);
            double delta=y[S_F0+i];
            double alpha_i=delta + atan2(w_r,w_ax);
            double aeff=alpha_i; if(aeff>FIN_STALL)aeff=FIN_STALL; if(aeff<-FIN_STALL)aeff=-FIN_STALL;
            double L=qbi*FIN_AREA*CNa_f*aeff*srp_shield;            /* radial lift (SRP-shielded, D-009) */
            double Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*delta*srp_shield;  /* tangential (roll) */
            double Ff[3]={ -L*er[0]-Ft*et[0], -L*er[1]-Ft*et[1], 0.0 };  /* force opposes incidence */
            v3_add(Fb,Fb,Ff);
            double arm[3]={rm[0],rm[1],rm[2]-com};
            double Tf[3]; v3_cross(Tf,arm,Ff); v3_add(Tb,Tb,Tf);
        }
        /* passive fin ROLL damping (audit: roll axis had ZERO aero damping -> unbounded spin
         * if RCS saturates). Fins rolling into the stream see a local AoA ~ w_z*R/V. */
        double Croll = 4.0*0.5*atm.rho*speed*FIN_AREA*CNa_f*VEH_RADIUS*VEH_RADIUS;
        Tb[2] -= Croll*w[2];
    }

    /* RCS torque (from control) */
    v3_add(Tb,Tb,act->rcs_torque);

    /* ---- linear acceleration (world) ---- */
    double Fw[3]; q_rot(Fw,q,Fb);
    double g_h = G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));
    double a[3]={ Fw[0]/m, Fw[1]/m, Fw[2]/m - g_h };

    /* ---- rotational dynamics: I wdot = tau - w x (Iw) - Idot w ---- */
    double Iw[3]={mp.I_tr*w[0], mp.I_tr*w[1], mp.I_ax*w[2]};
    double wxIw[3]; v3_cross(wxIw,w,Iw);
    double wdot[3];
    wdot[0]=(Tb[0]-wxIw[0]-mp.Idot_tr*w[0])/mp.I_tr;
    wdot[1]=(Tb[1]-wxIw[1]-mp.Idot_tr*w[1])/mp.I_tr;
    wdot[2]=(Tb[2]-wxIw[2]-mp.Idot_ax*w[2])/mp.I_ax;

    /* ---- quaternion derivative ---- */
    double qd[4]; q_deriv(qd,q,w);

    /* ---- actuator lags ---- */
    /* throttle: rate-limited first-order toward command (only meaningful when firing) */
    double thr_err = act->throttle - y[S_THR];
    double thr_rate = thr_err/ENG_THR_TAU;
    if(thr_rate> ENG_THR_RATE) thr_rate= ENG_THR_RATE;
    if(thr_rate<-ENG_THR_RATE) thr_rate=-ENG_THR_RATE;
    /* gimbal: 2nd order (rate state) toward command, accel-limited */
    double gcmd0=act->gimbal[0], gcmd1=act->gimbal[1];
    double ga0=2.0*ENG_GIMBAL_RATE*(0.8); /* not used directly; PD below */
    (void)ga0;
    double wn=8.0, zeta=0.9;
    double gacc0 = wn*wn*(gcmd0-y[S_G0]) - 2.0*zeta*wn*y[S_GR0];
    double gacc1 = wn*wn*(gcmd1-y[S_G1]) - 2.0*zeta*wn*y[S_GR1];
    if(gacc0> ENG_GIMBAL_ACC)gacc0=ENG_GIMBAL_ACC; if(gacc0<-ENG_GIMBAL_ACC)gacc0=-ENG_GIMBAL_ACC;
    if(gacc1> ENG_GIMBAL_ACC)gacc1=ENG_GIMBAL_ACC; if(gacc1<-ENG_GIMBAL_ACC)gacc1=-ENG_GIMBAL_ACC;
    /* fins: rate-limited toward command */
    double fdot[4];
    for(int i=0;i<4;i++){ double e=act->fins[i]-y[S_F0+i]; double rr=e/0.05;
        if(rr>FIN_RATE)rr=FIN_RATE; if(rr<-FIN_RATE)rr=-FIN_RATE; fdot[i]=rr; }

    /* ---- pack derivative ---- */
    for(int i=0;i<NSTATE;i++) dy[i]=0.0;
    dy[S_RX]=v[0]; dy[S_RY]=v[1]; dy[S_RZ]=v[2];
    dy[S_VX]=a[0]; dy[S_VY]=a[1]; dy[S_VZ]=a[2];
    dy[S_QX]=qd[0]; dy[S_QY]=qd[1]; dy[S_QZ]=qd[2]; dy[S_QW]=qd[3];
    dy[S_WX]=wdot[0]; dy[S_WY]=wdot[1]; dy[S_WZ]=wdot[2];
    dy[S_MLOX]=-mdot_lox; dy[S_MRP1]=-mdot_rp1;
    dy[S_THR]=thr_rate;
    dy[S_G0]=y[S_GR0]; dy[S_G1]=y[S_GR1];
    dy[S_GR0]=gacc0; dy[S_GR1]=gacc1;
    dy[S_F0]=fdot[0]; dy[S_F1]=fdot[1]; dy[S_F2]=fdot[2]; dy[S_F3]=fdot[3];
    /* slosh: damped pendulum (module gated by caller zeroing excitation) — placeholder rates */
    dy[S_SL0]=y[S_SV0]; dy[S_SL1]=y[S_SV1]; dy[S_SL2]=y[S_SV2]; dy[S_SL3]=y[S_SV3];
    /* heat */
    double qdot_heat = HEAT_K*sqrt(atm.rho/HEAT_RN)*speed*speed*speed;
    dy[S_QHEAT]=qdot_heat;

    if(diag){
        diag->mach=mach; diag->qbar=qbar; diag->alpha=alpha; diag->p_amb=atm.p; diag->rho=atm.rho;
        diag->qdot_heat=qdot_heat; diag->thrust=thrust;
        q_rot(diag->f_aero_world,q,Faero_b);
        /* specific force in body (thrust+aero)/m, for accelerometer HUD */
        diag->a_body[0]=Fb[0]/m; diag->a_body[1]=Fb[1]/m; diag->a_body[2]=Fb[2]/m;
        diag->twr = thrust/(m*g_h);
    }
}
