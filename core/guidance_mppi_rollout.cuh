/* guidance_mppi_rollout.cuh — SHARED (host+device) MPPI rollout kernel math (M5 CUDA port).
 *
 * DIRECTIVE 7 (one dynamics source): this header holds the rollout-cost machinery EXTRACTED
 * VERBATIM from core/guidance_mppi.c (the CPU M4 controller). The bodies below are byte-for-byte
 * the CPU statics — same constants, same order of operations, same converging_vdes / feas_margin /
 * predict_tgo / cmd_from_u_lean / rollout_cost. The ONLY change is the BL_HD (__host__ __device__)
 * qualifier so nvcc can compile the identical source for the GPU rollout.
 *
 * The CPU production path (guidance_mppi.c) keeps its own copy UNCHANGED (so --mppi without
 * --mppi-cuda stays byte-identical). The parity harness cross-checks THIS header's __host__
 * compilation against the GPU __device__ compilation (design §9.5) AND — because the extraction is
 * verbatim — against the production guidance_mppi.c numbers. Any drift is a golden-superseding edit.
 *
 * Physics (dynamics_deriv / control_step / rk4_step / atmo_eval / mass_props / lowest_point_z) is
 * the SAME translation units, #included as BL_HD by the .cu unity build — no reimplementation.
 *
 * All fp64, matching the CPU rollout (the design's "fp32 planner" note is superseded by the shipped
 * CPU implementation, which integrates the rollout in double; parity is only meaningful vs what
 * actually flies). -fmad=false pins the device libm/mul-add contraction to match /fp:precise host.
 */
#ifndef BL_GUIDANCE_MPPI_ROLLOUT_CUH
#define BL_GUIDANCE_MPPI_ROLLOUT_CUH

#include "state.h"
#include "guidance.h"
#include "guidance_mppi.h"
#include "guidance_hoverslam.h"   /* KDIV_SEEK/BRAKE/VBLEND shared schedule (directive 7) */
#include "dynamics.h"
#include "control.h"
#include "integrator.h"
#include "contact.h"
#include "atmosphere.h"
#include "constants.h"
#include "rng.h"
#include <math.h>
#include <string.h>

/* ============================= TUNING (verbatim from guidance_mppi.c) ============================= */
#define MR_SIG_THR   0.0
#define MR_SIG_ALAT  1.5
/* A_LAT_GAMUT already provided as a macro in the CPU file; redefine identically here (header-local). */
#ifndef MR_A_LAT_GAMUT
#define MR_A_LAT_GAMUT 3.2
#endif
#ifndef MR_OU_THETA
#define MR_OU_THETA   0.15
#endif
#define MR_A_DECEL    1.5
#define MR_VLAT_MAX   35.0
#define MR_T_LEAD     2.0

#define MR_R_REF   40.0
#define MR_V_REF   8.0
#define MR_W_REF   0.30
#define MR_TILT_REF (10.0*DEG2RAD)

#define MR_Q_VXYERR 10.0
#define MR_Q_VOUT   25.0
#define MR_Q_RXY    0.30
#define MR_Q_TILT   2.0
#define MR_Q_OMEGA  2.0
#define MR_Q_FUEL   0.5
#define MR_Q_ACC    2.0

#define MR_T_VXYERR 12.0
#define MR_T_RXY_AIR 4.0
#define MR_T_TILT   15.0
#define MR_T_OMEGA  8.0

#define MR_T_ZEM    60.0
#define MR_T_VIGN   25.0
#define MR_T_RXYD   3.0

#define MR_TD_RXY   120.0
#define MR_TD_VXY   90.0
#define MR_TD_VZ    30.0
#define MR_TD_TILT  120.0
#define MR_TD_OMEGA 60.0
#define MR_CRASH_COST 800.0
#define MR_COST_CLIP  20000.0

#define MR_W_VMARGIN 6.0
#define MR_W_FUEL_INFEAS 30.0

#define MR_G_RXY    60.0
#define MR_G_VXY    60.0
#define MR_Q_VLOW   0.0
#define MR_H_VLOW_HI  6000.0
#define MR_H_VLOW_LO  1500.0

#define MR_GAMMA_IS   1.0
#define MR_VTD_TARGET 1.5

/* solver constants shared with the CUDA host driver (VERBATIM from guidance_mppi.c) */
#define LAMBDA0_C     30.0
#define LAMBDA_MIN_C  2.0
#define LAMBDA_MAX_C  8000.0
#define ESS_LO_FRAC_C 0.03
#define ESS_HI_FRAC_C 0.20

/* ---------------------------------------------------------------------------------------- */

/* feas_margin — verbatim from guidance_mppi.c */
BL_HD static inline double mr_feas_margin(double h_feet, double vz, double m){
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

/* clampd — verbatim */
BL_HD static inline double mr_clampd(double x, double lo, double hi){
    if(!isfinite(x)) return 0.0;
    return x<lo?lo:(x>hi?hi:x);
}

/* state_tilt — verbatim */
BL_HD static inline double mr_state_tilt(const double y[NSTATE]){
    double zb[3]={0,0,1}, zw[3]; q_rot(zw,&y[S_QX],zb);
    double c=zw[2]; if(c>1)c=1; if(c<-1)c=-1; return acos(c);
}

/* converging_vdes — verbatim */
BL_HD static inline void mr_converging_vdes(double rx, double ry, double vx, double vy,
                            double* vdx, double* vdy, double* rhx, double* rhy){
    double r_mag = sqrt(rx*rx+ry*ry);
    double rhxx=0.0, rhyy=0.0, vrad=0.0;
    if(r_mag>1e-3){ rhxx=rx/r_mag; rhyy=ry/r_mag; vrad=vx*rhxx+vy*rhyy; }
    double r_pred = r_mag + vrad*MR_T_LEAD; if(r_pred<0.0) r_pred=0.0;
    double vdm = sqrt(2.0*MR_A_DECEL*r_pred); if(vdm>MR_VLAT_MAX) vdm=MR_VLAT_MAX;
    *vdx = -vdm*rhxx; *vdy = -vdm*rhyy;
    *rhx = rhxx; *rhy = rhyy;
}

/* predict_tgo — verbatim */
BL_HD static inline double mr_predict_tgo(double h_feet, double vz, double m){
    double h=h_feet, v=vz, t=0.0; const double dt=0.1;
    for(int i=0;i<3000 && h>0.0; i++){
        AtmoOut atm; atmo_eval(h,&atm);
        double T=engine_thrust(1.0, atm.p);
        double gh=G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));
        double a = (v<-2.0)? (0.5*T/m - gh) : -gh;
        v += a*dt; h += v*dt; t += dt;
        if(v>0.0 && h<=0.0) break;
        if(h<=0.0) break;
    }
    if(t<0.1) t=0.1; if(t>40.0) t=40.0;
    return t;
}

/* cmd_from_u_lean — verbatim (uses KDIV_* from guidance_hoverslam.h) */
BL_HD static inline void mr_cmd_from_u_lean(State* rst, const double u[MPPI_NCH], double h_feet,
                            double ignite_h, GuidanceCmd* g){
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

    int ignite = (!rst->engine_on) && (vz < -1.0) && (h_feet <= ignite_h);
    if(rst->engine_on || ignite){
        g->engine_cmd=1;
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
        if(h_feet<2.5 && vz>-1.2){ g->engine_cmd=0; g->throttle=0.0; }
        else g->throttle=thr;
    } else {
        g->engine_cmd=0; g->throttle=0.0;
    }

    double s=1.0, damp=0.0;
    if(rst->engine_on){
        s=h_feet/400.0; if(s>1.0)s=1.0; if(s<0.0)s=0.0; s*=s;
        damp=1.0-s;
        if(rst->ign_timer>=0.0 && rst->ign_timer<2.0){ s=0.0; damp=1.0; }
    } else {
        double fe=(h_feet - ignite_h)/450.0; if(fe<0.0)fe=0.0; if(fe>1.0)fe=1.0;
        s=fe;
    }
    double kbase = KDIV_SEEK;
    if(rst->engine_on){
        double vdxm,vdym,rhxm,rhym;
        mr_converging_vdes(y[S_RX],y[S_RY],y[S_VX],y[S_VY],&vdxm,&vdym,&rhxm,&rhym);
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

/* mr_rollout_cost — VERBATIM from guidance_mppi.c rollout_cost(). The CPU takes the whole
 * MppiState* M and reads M->ubar[t][c] + eps[t][c] and M->ignite_h; here ubar and ignite_h are
 * passed explicitly (bit-identical values) so the device kernel needs no MppiState in registers.
 * eps is the pre-sampled OU noise sequence for THIS rollout. Returns C_k. */
BL_HD static inline double mr_rollout_cost(double ignite_h, const State* st0, const EnvCtx* env0,
                           const double ubar[MPPI_H][MPPI_NCH], const double eps[MPPI_H][MPPI_NCH],
                           double gamma, double m0){
    State rst = *st0;                 /* full hybrid state copy (engine/legs/timers) */
    Actuators act; memset(&act,0,sizeof(act)); act.n_eng=1;  /* OWN actuators (fin_eint per rollout) */
    EnvCtx env = *env0;
    env.wind_world[0]=env.wind_world[1]=env.wind_world[2]=0.0;  /* nominal planner */

    double cost = 0.0;
    int grounded = 0;
    int gate_done = 0;                /* one-shot centered-by gate cost fired? */
    double deck_z = 0.0;

    const double invSig[MPPI_NCH] = { (MR_SIG_THR>0.0)?1.0/(MR_SIG_THR*MR_SIG_THR):0.0,
                                      1.0/(MR_SIG_ALAT*MR_SIG_ALAT),
                                      1.0/(MR_SIG_ALAT*MR_SIG_ALAT) };

    double last_hfeet = 0.0;
    for(int t=0;t<MPPI_H && !grounded;t++){
        MassProps mp; mass_props(rst.y[S_MLOX],rst.y[S_MRP1],0,0,&mp);
        double h_feet = rst.y[S_RZ] - mp.com - 1.0*rst.deploy_frac;
        last_hfeet = h_feet;

        if(!gate_done && h_feet<=ignite_h+50.0){
            gate_done=1;
            double gr2=(rst.y[S_RX]*rst.y[S_RX]+rst.y[S_RY]*rst.y[S_RY])/(MR_R_REF*MR_R_REF);
            double gv2=(rst.y[S_VX]*rst.y[S_VX]+rst.y[S_VY]*rst.y[S_VY])/(MR_V_REF*MR_V_REF);
            cost += MR_G_RXY*gr2 + MR_G_VXY*gv2;
        }

        double u[MPPI_NCH];
        for(int c=0;c<MPPI_NCH;c++) u[c] = ubar[t][c] + eps[t][c];

        GuidanceCmd g; memset(&g,0,sizeof(g));
        mr_cmd_from_u_lean(&rst, u, h_feet, ignite_h, &g);

        if(g.engine_cmd && !rst.engine_on && rst.relights_left>0){
            rst.engine_on=1; rst.ign_timer=0.0; rst.n_eng=g.n_eng; rst.relights_left--;
            if(rst.phase==PH_COAST||rst.phase==PH_AERO) rst.phase=PH_LANDING_BURN;
        }
        rst.deploy_cmd = g.deploy_cmd;
        if(rst.engine_on && rst.ign_timer>=0.0) rst.ign_timer += MPPI_DT;
        if(rst.deploy_cmd){ rst.deploy_frac += MPPI_DT/LEG_DEPLOY_T; if(rst.deploy_frac>1)rst.deploy_frac=1; }

        control_step(&rst, &g, &env, &act);
        rk4_step(&rst, &act, &env, MPPI_DT);

        double rx=rst.y[S_RX], ry=rst.y[S_RY];
        double vx=rst.y[S_VX], vy=rst.y[S_VY];
        double wmag2 = rst.y[S_WX]*rst.y[S_WX]+rst.y[S_WY]*rst.y[S_WY]+rst.y[S_WZ]*rst.y[S_WZ];
        double tilt = mr_state_tilt(rst.y);
        double rxy2 = rx*rx+ry*ry;
        double r_mag = sqrt(rxy2); (void)r_mag;
        MassProps mp2; mass_props(rst.y[S_MLOX],rst.y[S_MRP1],0,0,&mp2);
        double fuel_used = m0 - mp2.m;

        double vdx=0.0, vdy=0.0, rhx=0.0, rhy=0.0;
        mr_converging_vdes(rx,ry,vx,vy,&vdx,&vdy,&rhx,&rhy);
        double evx=vx-vdx, evy=vy-vdy;
        double vxyerr2 = (evx*evx+evy*evy)/(MR_V_REF*MR_V_REF);
        double vrad = vx*rhx + vy*rhy;
        double vout = (vrad>0.0)? vrad/MR_V_REF : 0.0;

        double q = MR_Q_VXYERR*vxyerr2 + MR_Q_VOUT*vout*vout
                 + MR_Q_RXY*(rxy2/(MR_R_REF*MR_R_REF))
                 + MR_Q_TILT*(tilt*tilt/(MR_TILT_REF*MR_TILT_REF))
                 + MR_Q_OMEGA*(wmag2/(MR_W_REF*MR_W_REF))
                 + MR_Q_FUEL*(fuel_used/1000.0);
        {
            double wlow=(MR_H_VLOW_HI - h_feet)/(MR_H_VLOW_HI - MR_H_VLOW_LO);
            if(wlow<0.0)wlow=0.0; if(wlow>1.0)wlow=1.0;
            q += MR_Q_VLOW*wlow*((vx*vx+vy*vy)/(MR_V_REF*MR_V_REF));
        }
        double aeff2 = (u[1]*u[1]+u[2]*u[2])/(MR_SIG_ALAT*MR_SIG_ALAT*9.0);
        q += MR_Q_ACC*aeff2;
        cost += q*MPPI_DT;

        double isc = 0.0;
        for(int c=0;c<MPPI_NCH;c++) isc += u[c]*invSig[c]*eps[t][c];
        cost += gamma*isc;

        double lo = lowest_point_z(&rst);
        if(lo <= deck_z + 1e-3){
            grounded = 1;
            double vz2 = rst.y[S_VZ];
            double vxy2 = vx*vx+vy*vy;
            double td_v = sqrt(vxy2+vz2*vz2);
            double dvz = vz2-(-MR_VTD_TARGET);
            double phi = MR_TD_RXY*(rxy2/(MR_R_REF*MR_R_REF)) + MR_TD_VXY*(vxy2/(MR_V_REF*MR_V_REF))
                       + MR_TD_VZ*(dvz*dvz/(MR_V_REF*MR_V_REF))
                       + MR_TD_TILT*(tilt*tilt/(MR_TILT_REF*MR_TILT_REF)) + MR_TD_OMEGA*(wmag2/(MR_W_REF*MR_W_REF));
            int off_pad = (rxy2 > PAD_RADIUS*PAD_RADIUS);
            int too_hard = (td_v > TD_V_HARD);
            int tipped   = (tilt > 30.0*DEG2RAD);
            if(off_pad || too_hard || tipped) phi += MR_CRASH_COST;
            phi += 40.0*(sqrt(rxy2)/MR_R_REF);
            cost += phi;
            break;
        }
    }

    if(!grounded){
        double rx=rst.y[S_RX], ry=rst.y[S_RY];
        double vx=rst.y[S_VX], vy=rst.y[S_VY], vz=rst.y[S_VZ];
        double wmag2 = rst.y[S_WX]*rst.y[S_WX]+rst.y[S_WY]*rst.y[S_WY]+rst.y[S_WZ]*rst.y[S_WZ];
        double tilt = mr_state_tilt(rst.y);
        double rxy2 = rx*rx+ry*ry;
        double r_mag = sqrt(rxy2);
        MassProps mp; mass_props(rst.y[S_MLOX],rst.y[S_MRP1],0,0,&mp);
        double h_feet = rst.y[S_RZ]-mp.com-1.0*rst.deploy_frac;

        double vdx=0.0, vdy=0.0, rhx=0.0, rhy=0.0;
        mr_converging_vdes(rx,ry,vx,vy,&vdx,&vdy,&rhx,&rhy);
        double evx=vx-vdx, evy=vy-vdy;
        double vxyerr2 = (evx*evx+evy*evy)/(MR_V_REF*MR_V_REF);
        double vrad = vx*rhx + vy*rhy; double vout=(vrad>0.0)?vrad/MR_V_REF:0.0;
        double hkm = (rst.y[S_RZ]-mp.com)/1500.0; if(hkm<0)hkm=0;
        double pos_w = MR_T_RXY_AIR * (hkm<1.0 ? (2.0-hkm) : 1.0);

        double zem2, vign_pen = 0.0;
        if(h_feet > ignite_h){
            double vzm = (vz < -20.0) ? -vz : 20.0;
            double t_ign = (h_feet - ignite_h)/vzm;
            double zx = rx + vx*t_ign, zy = ry + vy*t_ign;
            zem2 = (zx*zx+zy*zy)/(MR_R_REF*MR_R_REF);
            double wv = 1.0 - t_ign/10.0; if(wv<0.0)wv=0.0;
            vign_pen = MR_T_VIGN*wv*((vx*vx+vy*vy)/(MR_V_REF*MR_V_REF));
        } else {
            double t_go = mr_predict_tgo(h_feet, vz, mp.m);
            double zx = rx + vx*t_go, zy = ry + vy*t_go;
            zem2 = (zx*zx+zy*zy)/(MR_R_REF*MR_R_REF);
        }

        double phi = MR_T_VXYERR*vxyerr2 + MR_Q_VOUT*vout*vout + pos_w*(rxy2/(MR_R_REF*MR_R_REF))
                   + MR_T_ZEM*zem2 + vign_pen + MR_T_RXYD*(r_mag/MR_R_REF)
                   + MR_T_TILT*(tilt*tilt/(MR_TILT_REF*MR_TILT_REF)) + MR_T_OMEGA*(wmag2/(MR_W_REF*MR_W_REF));

        double margin = mr_feas_margin(h_feet, vz<0?vz:0.0, mp.m);
        if(margin < 0.0){ double mn=margin/MR_R_REF; phi += MR_W_VMARGIN*mn*mn; }

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
            if(deficit > 0.0) phi += MR_W_FUEL_INFEAS*(deficit/1000.0);
        }
        (void)last_hfeet;
        cost += phi;
    }
    if(cost > MR_COST_CLIP) cost = MR_COST_CLIP;
    return cost;
}

/* mr_compute_ignite_h — VERBATIM from guidance_mppi.c compute_ignite_h. Host-only in practice
 * (called once per replan on the CPU side of mppi_step_cuda), but BL_HD-clean. */
BL_HD static inline double mr_compute_ignite_h(const State* st){
    const double* y=st->y;
    MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
    double m=mp.m;
    double h0 = y[S_RZ]-mp.com;
    double vz0 = y[S_VZ]; if(vz0>-1.0) vz0=-1.0;
    double lo=50.0, hi=(h0>60.0?h0:60.0);
    for(int it=0; it<28; it++){
        double mid=0.5*(lo+hi);
        double margin=mr_feas_margin(mid, vz0, m);
        if(margin > 150.0) hi=mid;
        else lo=mid;
    }
    double h_ig=0.5*(lo+hi);
    if(h_ig<40.0) h_ig=40.0;
    return h_ig;
}

/* mr_warm_start_nominal — VERBATIM from guidance_mppi.c warm_start_nominal. Writes ubar[H][NCH] and
 * needs ignite_h precomputed (passed in). Deterministic, no RNG. Host-side prologue for the GPU solve. */
BL_HD static inline void mr_warm_start_nominal(double ubar[MPPI_H][MPPI_NCH], double ignite_h,
                                               const State* st0, const EnvCtx* env0){
    State rst = *st0;
    Actuators act; memset(&act,0,sizeof(act)); act.n_eng=1;
    EnvCtx env = *env0; env.wind_world[0]=env.wind_world[1]=env.wind_world[2]=0.0;
    int grounded=0; double deck_z=0.0;
    for(int t=0;t<MPPI_H;t++){
        MassProps mp; mass_props(rst.y[S_MLOX],rst.y[S_MRP1],0,0,&mp);
        double h_feet = rst.y[S_RZ]-mp.com-1.0*rst.deploy_frac;
        double alx=0.0, aly=0.0;
        if(!grounded){
            double vdxw=0.0,vdyw=0.0,rhxw=0.0,rhyw=0.0;
            mr_converging_vdes(rst.y[S_RX],rst.y[S_RY],rst.y[S_VX],rst.y[S_VY],&vdxw,&vdyw,&rhxw,&rhyw);
            alx = mr_clampd(1.0*(vdxw - rst.y[S_VX]), -MR_A_LAT_GAMUT, MR_A_LAT_GAMUT);
            aly = mr_clampd(1.0*(vdyw - rst.y[S_VY]), -MR_A_LAT_GAMUT, MR_A_LAT_GAMUT);
        }
        ubar[t][0]=0.0; ubar[t][1]=alx; ubar[t][2]=aly;
        if(grounded) continue;

        double uu[MPPI_NCH]={0.0,alx,aly};
        GuidanceCmd g; memset(&g,0,sizeof(g));
        mr_cmd_from_u_lean(&rst, uu, h_feet, ignite_h, &g);
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

/* mr_sgf_smooth — VERBATIM from guidance_mppi.c sgf_smooth (Savitzky-Golay window 9, order 3). */
BL_HD static inline void mr_sgf_smooth(double* x, int n){
    static const double c[9] = {
        -21.0/231.0, 14.0/231.0, 39.0/231.0, 54.0/231.0, 59.0/231.0,
         54.0/231.0, 39.0/231.0, 14.0/231.0, -21.0/231.0
    };
    if(n < 9) return;
    double tmp[MPPI_H];
    for(int i=0;i<n;i++){
        int lo=i-4, hi=i+4;
        if(lo<0 || hi>=n){ tmp[i]=x[i]; continue; }
        double s=0.0;
        for(int k=0;k<9;k++) s += c[k]*x[lo+k];
        tmp[i]=s;
    }
    for(int i=0;i<n;i++) x[i]=tmp[i];
}

/* mr_ou_eps — regenerate the OU-colored noise for one (rollout k, channel c) up to step t_max.
 * VERBATIM recurrence from mppi_step: Philox lane=k, counter=(replan, t*NCH+c), stream RNG_MPPI;
 * OU: e_t = (1-theta) e_{t-1} + drive[c]*n_t.  Fills eps_out[0..MPPI_H-1]. Zero storage on device:
 * K1 stores each rollout's full eps in registers/local; K2 regenerates identically. */
BL_HD static inline void mr_ou_channel(uint32_t seed, uint32_t replan, uint32_t k, int c,
                                       double drive_c, double eps_out[MPPI_H]){
    double prev = 0.0;
    for(int t=0;t<MPPI_H;t++){
        double n = rng_normal(seed, RNG_MPPI, replan, (uint32_t)(t*MPPI_NCH+c), k);
        double e = (1.0-MR_OU_THETA)*prev + drive_c*n;
        eps_out[t] = e;
        prev = e;
    }
}

#endif /* BL_GUIDANCE_MPPI_ROLLOUT_CUH */
