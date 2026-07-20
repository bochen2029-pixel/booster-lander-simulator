/* ceiling_eo.c — 2-ENGINE FRONTIER ORACLE for the ENTRY-regime engine-out (D-025 BLOCKING item).
 *
 * PURPOSE (canon 9.9 + engineout_design §8): decide whether MPPI's collapse under
 * `--engine-out random` (ENTRY 1/60, down from 57/60 clean, D-025) is PHYSICS (most random
 * failure draws leave the state outside the shrunken 2-engine backward-reachable set — honest
 * directive-6 crashes) or a CONTROLLER SHORTFALL (room for the expert-iteration EO teachers).
 *
 * WHAT IT COMPUTES, per engine-failure time t_fail inside the 3-engine entry-burn window:
 *   1. D_phys_2eng(t_fail): the max landable effective lateral offset when ONE (side) engine dies
 *      at t_fail. The vertical profile is RE-INTEGRATED with n_eng 3->2 from t_fail (less
 *      deceleration -> hotter/different downstream a_max(t)), the entry-burn LATERAL authority is
 *      scaled by (n_eng/3) from t_fail, AND a GIMBAL-TRIM DEBIT reduces usable steering to reflect
 *      the survivor-centroid trim eating the +-5deg cone (parameterized 40/60/80 %). Then the SAME
 *      phase-decomposed rest-to-rest bang-bang / vcap divert entrydiv.c uses gives the frontier.
 *   2. The LOC/attitude-recoverability oracle: the induced torque tau = F_surv * R/2, the angular
 *      accel alpha = tau/I_tr, the max counter-gimbal moment com*(2T)*sin(5deg), the trim cone
 *      fraction, and the time-to-wmag=2 if UNCORRECTED (the F_LOC gate is wmag>2 rad/s sustained
 *      >3 s, sim.c:565-566). This is the axis the LATERAL frontier does NOT see.
 *   3. The random-draw verdict: t_fail ~ U[4,18] s (main.c:350) x the ENTRY IC offset
 *      (mean 3000, per-axis sigma 250, scenario.c:18,53) -> what fraction of draws land INSIDE the
 *      shrunken frontier. That fraction is the CEILING on any controller's landed rate.
 *
 * PARITY (mirrored verbatim from core/ and cross-checked vs runs/sandbox/entrydiv.c, itself
 * parity-checked): US76 atmosphere (atmosphere.c); frozen aero CA/CN + VEH_AREF (dynamics.c);
 * engine_thrust=throttle*(T_vac-AE*p) (constants.h); E3 window IGNITE 72k / CUT 68k / FUEL_FLOOR
 * 7 t + entry_predict_peak_qbar CA=1.0 (sim.c:272-274, guidance_hoverslam.c); SRP drag shielding
 * during the burn (dynamics.c); suicide_burn_margin + 0.85 v_ref/Kv=3 landing + fade s=(h/400)^2
 * (guidance_hoverslam.c / guidance_mppi.c); mass_props transverse inertia I_tr (dynamics.c:56-84);
 * ENG_RING_R=0.6*VEH_RADIUS, survivor centroid R/2 (constants.h:24, sim.c:38-47); F_LOC wmag>2
 * >3 s (sim.c:565-566); gimbal +-5 deg (ENG_GIMBAL_MAX, constants.h:39), moment com*thr*sin(g)
 * (control.c:187-188).
 *
 * C ONLY. Build (MSVC): cl /nologo /O2 /fp:precise /W3 ceiling_eo.c   (see ceiling_eo.cmd)
 * Output: runs/sandbox/ceiling_eo_out.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ================= constants (core/constants.h) ================= */
#define G0        9.80665
#define R_EARTH   6356766.0
#define R_AIR     287.053
#define GAMMA_AIR 1.4
#define P0_ATM    101325.0
#define ENG_T_SL   845000.0
#define ENG_ISP_SL 282.0
#define ENG_ISP_VAC 311.0
#define VEH_AREF  10.52
#define VEH_DRY   25600.0
#define VEH_LEN   47.7
#define VEH_DIA   3.66
#define VEH_RADIUS (VEH_DIA*0.5)
#define VEH_DRY_COMZ 12.4
#define VEH_STAGE_LEN 41.2
#define TANK_AREA 9.9
#define LOX_RHO   1220.0
#define RP1_RHO   833.0
#define LOX_BASE_Z 16.0
#define RP1_BASE_Z 1.6
#define MIX_RATIO 2.33
#define TD_V_TARGET 1.5
#define LANDING_IGNITE_MARGIN 150.0
#define PAD_RADIUS 26.0
#define DEG2RAD   0.017453292519943295
#define RAD2DEG   57.29577951308232
#define PI        3.141592653589793

/* engine-out geometry + failure limits (core/constants.h, sim.c) */
#define ENG_RING_R      (0.6*VEH_RADIUS)              /* side-engine ring radius (constants.h:24) */
#define ENG_GIMBAL_MAX  (5.0*DEG2RAD)                 /* +-5 deg gimbal (constants.h:39) */
#define WMAG_LOC        2.0                           /* rad/s LOC gate (sim.c:566) */
#define LOC_DWELL       3.0                           /* s sustained before F_LOC (sim.c:566) */

/* E3 entry-burn supervisor window (sim.c:272-274) */
#define ENTRY_QBAR_IGNITE 72000.0
#define ENTRY_QBAR_CUT    68000.0
#define ENTRY_FUEL_FLOOR  7000.0
#define ENTRY_PRED_CA     1.0

/* the random engine-out draw window (main.c:350: eo_time = 4.0 + u2*14.0) */
#define EO_T0   4.0
#define EO_T1   18.0

/* divert bank / AoA cap (prompt <=15 deg; entrydiv parity) */
#define BANK_CAP_DEG 15.0

/* derived engine (constants.h) */
static double ENG_MDOT_100, ENG_T_VAC, ENG_AE;
static void init_engine(void){
    ENG_MDOT_100 = ENG_T_SL/(G0*ENG_ISP_SL);
    ENG_T_VAC    = ENG_MDOT_100*G0*ENG_ISP_VAC;
    ENG_AE       = (ENG_T_VAC-ENG_T_SL)/P0_ATM;
}
static double engine_thrust(double thr, double p){
    if(thr<=0.0) return 0.0;
    double T = ENG_T_VAC - ENG_AE*p;
    return thr*T;
}
static double isp_of(double p){ return ENG_ISP_VAC - (ENG_ISP_VAC-ENG_ISP_SL)*(p/P0_ATM); }

/* ================= US76 atmosphere (atmosphere.c verbatim) ================= */
static const double H_b[8]={0.0,11.0,20.0,32.0,47.0,51.0,71.0,84.852};
static const double T_b[8]={288.15,216.65,216.65,228.65,270.65,270.65,214.65,186.946};
static const double L_b[7]={-6.5,0.0,1.0,2.8,0.0,-2.8,-2.0};
static const double P_b[8]={101325.0,22632.1,5474.89,868.019,110.906,66.9389,3.95642,0.373384};
typedef struct { double T,p,rho,a; } Atmo;
static void atmo_eval(double h, Atmo* o){
    if(h<0.0) h=0.0;
    double Hp=(R_EARTH*h)/(R_EARTH+h)/1000.0;
    if(Hp>84.852) Hp=84.852;
    int b=0; for(int i=0;i<7;i++){ if(Hp>=H_b[i]) b=i; }
    double Tb=T_b[b],Lb=L_b[b],Pb=P_b[b],Hb=H_b[b];
    double dz=Hp-Hb;
    double T=Tb+Lb*dz, p;
    if(fabs(Lb)>1e-9){ double Lm=Lb/1000.0; p=Pb*pow(T/Tb,-G0/(R_AIR*Lm)); }
    else { p=Pb*exp(-G0*(dz*1000.0)/(R_AIR*Tb)); }
    if(T<1.0) T=1.0;
    o->T=T; o->p=p; o->rho=p/(R_AIR*T); o->a=sqrt(GAMMA_AIR*R_AIR*T);
}

/* ================= frozen aero tables (dynamics.c verbatim) ================= */
static const double AERO_M[9] ={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0};
static const double AERO_CA[9]={0.85,0.88,1.10,1.40,1.25,1.10,0.95,0.92,0.90};
static const double AERO_CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
static double tbl(const double* xs,const double* ys,int n,double x){
    if(x<=xs[0]) return ys[0];
    if(x>=xs[n-1]) return ys[n-1];
    for(int i=0;i<n-1;i++){ if(x<=xs[i+1]){ double t=(x-xs[i])/(xs[i+1]-xs[i]); return ys[i]+t*(ys[i+1]-ys[i]); } }
    return ys[n-1];
}
static double CA_of(double M){ return tbl(AERO_M,AERO_CA,9,fabs(M)); }
static double CNa_of(double M){ return tbl(AERO_M,AERO_CN,9,fabs(M)); }

/* ================= mass properties (dynamics.c mass_props: com + transverse inertia) ================= */
typedef struct { double m, com, I_tr; } MP;
static void mass_props(double m_lox, double m_rp1, MP* mp){
    if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
    const double rt2 = TANK_AREA/PI;
    const double R2  = VEH_RADIUS*VEH_RADIUS;
    double m_dry = VEH_DRY, z_dry = VEH_DRY_COMZ;
    double Itl_dry = m_dry*(6.0*R2 + VEH_LEN*VEH_LEN)/12.0;
    double h_l = m_lox/(LOX_RHO*TANK_AREA), z_l = LOX_BASE_Z + 0.5*h_l;
    double h_r = m_rp1/(RP1_RHO*TANK_AREA), z_r = RP1_BASE_Z + 0.5*h_r;
    double Itl_l = m_lox*(3.0*rt2 + h_l*h_l)/12.0;
    double Itl_r = m_rp1*(3.0*rt2 + h_r*h_r)/12.0;
    double M = m_dry + m_lox + m_rp1;
    double com = (m_dry*z_dry + m_lox*z_l + m_rp1*z_r)/M;
    double I_tr = Itl_dry + m_dry*(z_dry-com)*(z_dry-com)
                + Itl_l   + m_lox*(z_l-com)*(z_l-com)
                + Itl_r   + m_rp1*(z_r-com)*(z_r-com);
    mp->m=M; mp->com=com; mp->I_tr=I_tr;
}
static double mass_com(double m_lox, double m_rp1, double* m_out){
    MP mp; mass_props(m_lox,m_rp1,&mp); if(m_out)*m_out=mp.m; return mp.com;
}
static double g_of(double h){ double x=R_EARTH/(R_EARTH+h); return G0*x*x; }

/* SRP aero-shielding blend (dynamics.c): CA scaled by thrust coefficient CT=thrust/(qbar*Aref). */
static double srp_shield(double thrust, double qbar){
    if(thrust<=0.0 || qbar<=1e-4) return 1.0;
    double CT=thrust/(qbar*VEH_AREF);
    if(CT<=0.5) return 1.0;
    double t=(CT-0.5)/(3.0-0.5); if(t>1)t=1;
    return 1.0 + t*(0.05-1.0);
}

/* entry_predict_peak_qbar (guidance_hoverslam.c verbatim, CA=1.0 ballistic shoot dt=1) */
static double entry_predict_peak_qbar(double h0, double speed0, double mass){
    double h=h0, v=speed0, qmax=0.0; const double dt=1.0;
    for(int i=0;i<600 && h>0.0 && v>1.0; i++){
        Atmo atm; atmo_eval(h,&atm);
        double q=0.5*atm.rho*v*v; if(q>qmax)qmax=q;
        double gh=g_of(h);
        double drag=0.5*atm.rho*v*v*VEH_AREF*ENTRY_PRED_CA;
        double a=gh - drag/mass;
        v += a*dt; if(v<0)v=0; h -= v*dt;
    }
    return qmax;
}

/* suicide_burn_margin (guidance_hoverslam.c verbatim): thrust-only, full 1-engine, dt=0.1 */
static double suicide_burn_margin(double h_feet, double vz, double m){
    double h=h_feet, v=vz; const double dt=0.1;
    for(int i=0;i<3000 && v<0.0 && h>-50.0; i++){
        Atmo atm; atmo_eval(h,&atm);
        double T=engine_thrust(1.0, atm.p);
        double gh=g_of(h);
        double a=T/m - gh;
        v += a*dt; h += v*dt;
    }
    return h;
}

/* ============================================================================================
 *  FULL ENTRY TRAJECTORY (entrydiv.c structure) — vertical channel + per-step lateral authority.
 *  Engine-out extension: at t >= t_fail, during PH_ENTRY_BURN, n_eng 3->2 (thrust, mdot, a_burn
 *  all scale). t_fail<0 disables the failure (=> pure 3-engine clean reference).
 * ============================================================================================ */
#define NMAX 400000

enum { PHC=0, PHE=1, PHA=2, PHL=3 };

typedef struct {
    int    n;
    double dt;
    double t[NMAX];
    double h[NMAX];        /* feet height above ground (RZ - com) */
    double vz[NMAX];
    double mach[NMAX];
    double qbar[NMAX];
    double m[NMAX];
    double thr[NMAX];
    double p[NMAX];
    int    n_eng[NMAX];    /* 1, 2 (post-fail), or 3 */
    int    phase[NMAX];
    double a_burn[NMAX];   /* |thrust accel| available for banking this step (n_eng*T/m) */
    /* summary */
    double t_ig_entry, h_ig_entry, t_cut_entry, h_cut_entry, v_cut_entry, speed_cut_entry;
    double t_ig_land, h_ig_land, T_td, vz_td;
    double qbar_peak; double mach_peak;
    double fuel_after_entry, fuel_left;
    double v_at_ignite_entry, v_min_entry;
    int    ok;
    int    i_cut_entry, i_ig_land, i_fail;   /* sample index at fail (or -1) */
    double t_fail_eff;                        /* the t_fail actually used (or <0) */
} Traj;

/* Integrate the ENTRY vertical channel. t_fail<0 => no engine-out (clean 3-engine reference).
 * t_fail>=0 => at t>=t_fail during PH_ENTRY_BURN drop to n_eng_after engines (2 for a single out). */
static void sim_entry_vertical_eo(double h0_feet, double vz0, double prop0, double dt,
                                  double t_fail, int n_eng_after, Traj* V){
    memset(V,0,sizeof(*V));
    V->dt=dt; V->i_fail=-1; V->t_fail_eff=t_fail;
    double m_rp1=prop0/(1.0+MIX_RATIO), m_lox=prop0-m_rp1;
    double mtot; double com=mass_com(m_lox,m_rp1,&mtot);
    double h=h0_feet, vz=vz0, t=0.0;
    int phase=PHC; int engine_on=0; int n_eng=1; double ada=0.0;
    double qpeak=0.0, mpeak=0.0, vmin=1e18;
    int n=0; int failed=0;
    V->v_at_ignite_entry=0;

    while(h>0.0 && n<NMAX){
        double m; com=mass_com(m_lox,m_rp1,&m);
        Atmo atm; atmo_eval(h+com,&atm);
        double speed=fabs(vz);
        double mach=speed/atm.a;
        double qbar=0.5*atm.rho*vz*vz;
        if(qbar>qpeak)qpeak=qbar;
        if(mach>mpeak)mpeak=mach;
        double fuel=m_lox+m_rp1;

        /* ---- E3 entry supervisor (sim.c entry_supervisor) ---- */
        if(phase==PHC || phase==PHE){
            double qpk=entry_predict_peak_qbar(h+com, speed, m);
            if(phase==PHE){
                if(qpk<=ENTRY_QBAR_CUT || fuel<=ENTRY_FUEL_FLOOR){
                    engine_on=0; n_eng=1; phase=PHA;
                    V->t_cut_entry=t; V->h_cut_entry=h; V->v_cut_entry=vz; V->speed_cut_entry=speed;
                    V->fuel_after_entry=fuel; V->i_cut_entry=n;
                }
            } else { /* PHC */
                if(qpk>=ENTRY_QBAR_IGNITE && fuel>ENTRY_FUEL_FLOOR){
                    phase=PHE; engine_on=1; n_eng=3;
                    V->t_ig_entry=t; V->h_ig_entry=h; V->v_at_ignite_entry=speed;
                }
            }
        }

        /* ---- ENGINE-OUT event (sim.c:335-342): fire ONCE during the multi-engine entry burn ---- */
        if(!failed && t_fail>=0.0 && phase==PHE && engine_on && n_eng>1 && t>=t_fail){
            failed=1; n_eng=n_eng_after; V->i_fail=n; V->t_fail_eff=t;
        }

        /* ---- landing-burn ignition (aero-aware, fins deployed) — only after the entry burn cut ---- */
        if((phase==PHA) && !engine_on){
            double margin = suicide_burn_margin(h, vz, m);
            if(vz < -1.0 && margin <= LANDING_IGNITE_MARGIN){
                engine_on=1; n_eng=1; phase=PHL;
                double Tfull=engine_thrust(1.0,atm.p);
                double amax=Tfull/m - G0; if(amax<1.0)amax=1.0;
                ada=0.85*amax;
                V->t_ig_land=t; V->h_ig_land=h; V->i_ig_land=n;
            }
        }

        /* ---- vertical accel by phase ---- */
        double thr_cmd=0.0, az, a_burn_avail=0.0;
        double Tfull=engine_thrust(1.0,atm.p);
        double gh=g_of(h+com);
        if(phase==PHE){
            thr_cmd=1.0;
            double Tv=n_eng*Tfull;
            a_burn_avail = Tv/m;                    /* total thrust accel (for lateral projection) */
            double CA=CA_of(mach)*srp_shield(Tv,qbar);
            double D=0.5*atm.rho*vz*vz*VEH_AREF*CA;
            az = Tv/m - gh + D/m;                    /* retrograde: thrust up + drag up - gravity */
            double Isp=isp_of(atm.p);
            double mdot=n_eng*Tfull/(Isp*G0);        /* n_eng engines burning (dead engine burns none) */
            double mdot_rp1=mdot/(1.0+MIX_RATIO), mdot_lox=mdot-mdot_rp1;
            m_lox-=mdot_lox*dt; m_rp1-=mdot_rp1*dt;
            if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
        } else if(phase==PHL){
            double a_design=ada; if(a_design<1.0)a_design=1.0;
            double hgo=h; if(hgo<0.02)hgo=0.02;
            double v_ref=-sqrt(TD_V_TARGET*TD_V_TARGET + 2.0*a_design*hgo);
            double Kv=3.0;
            double a_cmd=a_design + Kv*(v_ref - vz);
            double T_need=m*(gh + a_cmd);
            double thr=T_need/Tfull; if(thr<0.40)thr=0.40; if(thr>1.0)thr=1.0;
            if(h<6.0 && vz>-0.6) thr=0.40;
            thr_cmd=thr;
            double Tact=engine_thrust(thr_cmd,atm.p);
            az = Tact/m - gh;
            double Isp=isp_of(atm.p);
            double mdot=Tact/(Isp*G0);
            double mdot_rp1=mdot/(1.0+MIX_RATIO), mdot_lox=mdot-mdot_rp1;
            m_lox-=mdot_lox*dt; m_rp1-=mdot_rp1*dt;
            if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
        } else {
            double CA=CA_of(mach);
            double D=0.5*atm.rho*vz*vz*VEH_AREF*CA;
            az = -gh + D/m;
        }

        if(phase==PHE && speed<vmin) vmin=speed;

        V->t[n]=t; V->h[n]=h; V->vz[n]=vz; V->mach[n]=mach; V->qbar[n]=qbar; V->m[n]=m;
        V->thr[n]=thr_cmd; V->p[n]=atm.p; V->n_eng[n]=n_eng; V->phase[n]=phase; V->a_burn[n]=a_burn_avail;
        n++;

        vz += az*dt; h += vz*dt; t += dt;
        if(phase==PHL && vz>0.5) break;
    }
    V->n=n; V->T_td=t; V->vz_td=vz; V->qbar_peak=qpeak; V->mach_peak=mpeak;
    V->fuel_left=m_lox+m_rp1; V->v_min_entry=vmin;
    V->ok = (fabs(vz)<6.0 && V->fuel_left>0.0 && phase==PHL);
}

/* ================= lateral authority a_max(t) along the trajectory (per phase) =================
 * Entry burn: a_lat = a_burn * sin(bank_cap) * steer_frac  (thrust-vector projection; steer_frac
 *   is the fraction of the gimbal cone left for steering AFTER the survivor-centroid trim debit,
 *   applied only from the failure step onward — before the failure the full cone steers).
 * Aero-descent (unpowered): a_lat = qbar*Aref*CNa*alpha_cap / m.
 * Landing burn: a_lat = (thr*T/m)*sin(aoa_cap) * fade s=(h/400)^2. */
static void build_amax_entry_eo(const Traj* V, double bank_cap_deg, double aoa_cap_deg,
                                int fade_landing, double steer_frac_after_fail, double* amax){
    double bcr=bank_cap_deg*DEG2RAD, acr=aoa_cap_deg*DEG2RAD;
    for(int i=0;i<V->n;i++){
        double a=0.0;
        if(V->phase[i]==PHE){
            double sf = 1.0;
            if(V->i_fail>=0 && i>=V->i_fail) sf = steer_frac_after_fail;   /* trim debit post-fail */
            a = V->a_burn[i]*sin(bcr)*sf;      /* a_burn already reflects n_eng (2 post-fail) */
        } else if(V->phase[i]==PHA){
            double CNa=CNa_of(V->mach[i]);
            a = V->qbar[i]*VEH_AREF*CNa*acr / V->m[i];
        } else if(V->phase[i]==PHL){
            double s=1.0;
            if(fade_landing){ s=V->h[i]/400.0; if(s>1)s=1; if(s<0)s=0; s*=s; }
            double T=engine_thrust(V->thr[i], V->p[i]);
            a = s*(T/V->m[i])*sin(acr);
        } else a=0.0;
        amax[i]=a;
    }
}

/* ================= rest-to-rest divert solvers (verbatim structure from entrydiv.c/ceiling.c) ===== */
static double bb_rest_to_rest(const Traj* V, const double* amax, double* vresid, double* vpk_out){
    int n=V->n; double dt=V->dt;
    double tlo=0.0, thi=V->t[n-1]+dt;
    double best_r=0.0, best_v=0.0, best_vpk=0.0;
    for(int it=0; it<70; it++){
        double ts=0.5*(tlo+thi);
        double r=0.0, v=0.0, vpk=0.0;
        for(int i=0;i<n;i++){
            double te=V->t[i], frac;
            if(te+dt<=ts)frac=1.0; else if(te>=ts)frac=0.0; else frac=(ts-te)/dt;
            double dtp=frac*dt, dtm=(1.0-frac)*dt;
            v += (+amax[i])*dtp; r += v*dtp;
            v += (-amax[i])*dtm; r += v*dtm;
            if(fabs(v)>vpk)vpk=fabs(v);
        }
        best_r=fabs(r); best_v=v; best_vpk=vpk;
        if(v>0.0) thi=ts; else tlo=ts;
    }
    if(vresid)*vresid=best_v; if(vpk_out)*vpk_out=best_vpk;
    return best_r;
}
static double vcap_rest_to_rest(const Traj* V, const double* amax, double vcap,
                                double* vresid, double* vpk_out){
    int n=V->n; double dt=V->dt;
    double tlo=0.0, thi=V->t[n-1]+dt;
    double best_r=0.0, best_v=0.0, best_vpk=0.0;
    for(int it=0; it<80; it++){
        double ts=0.5*(tlo+thi);
        double r=0.0, v=0.0, vpk=0.0;
        for(int i=0;i<n;i++){
            double te=V->t[i], frac;
            if(te+dt<=ts)frac=1.0; else if(te>=ts)frac=0.0; else frac=(ts-te)/dt;
            double dtp=frac*dt, dtm=(1.0-frac)*dt;
            v += (+amax[i])*dtp; if(v>vcap)v=vcap; r += v*dtp;
            v += (-amax[i])*dtm; if(v<-vcap)v=-vcap; r += v*dtm;
            if(fabs(v)>vpk)vpk=fabs(v);
        }
        best_r=fabs(r); best_v=v; best_vpk=vpk;
        if(v>0.0) thi=ts; else tlo=ts;
    }
    if(vresid)*vresid=best_v; if(vpk_out)*vpk_out=best_vpk;
    return best_r;
}

/* ================= PLANT-FAITHFUL closed-loop ZEM/ZEV divert (sim.c entry_divert_step) ===========
 * The ACTUAL deployed law: a_cmd = KR*(-6 r/tgo^2) + KV*(-4 v/tgo), capped at amax=a_burn*sin(15deg)
 * with a_burn = n_eng*T/m (sim.c:260-268, KR=2.0 KV=3.5). This is what MPPI's E3 supervisor commands
 * during the entry burn; below the aero-descent/landing use the ceiling.c lateral models. tgo is the
 * plant's entry_tgo_estimate (sim.c:220, dt=0.5 ballistic shoot). We forward-integrate the closed-loop
 * lateral channel from initial offset D0 to touchdown -> (td_lat, vxy_td). Confirms whether the
 * 2-engine authority realizes the 3 km closure with a soft terminal (the reachability being ACHIEVABLE,
 * not just theoretically inside the frontier). steer_frac applies the trim debit to the burn cap. */
#define ENTRY_DIVERT_KR 2.0
#define ENTRY_DIVERT_KV 3.5
static double entry_tgo_estimate(double h, double vz, double m){
    double v=vz, t=0.0; const double dt=0.5;
    for(int i=0;i<600 && h>0.0;i++){
        Atmo atm; atmo_eval(h,&atm);
        double gh=g_of(h);
        double a_drag=0.5*atm.rho*v*v*VEH_AREF*1.0/m;
        double a=-gh + ((v<0.0)? a_drag : -a_drag);
        v+=a*dt; h+=v*dt; t+=dt;
    }
    if(t<1.0) t=1.0; return t;
}
static double zemzev_closed_loop_eo(const Traj* V, double D0, double steer_frac,
                                    double* vxy_td, double* peak_vlat, double* td_at_cut){
    double rx=D0, vx=0.0, dt=V->dt; int n=V->n; double vpk=0.0, r_cut=0.0;
    for(int i=0;i<n;i++){
        double a_eff_cap;
        if(V->phase[i]==PHE){
            double sf=1.0; if(V->i_fail>=0 && i>=V->i_fail) sf=steer_frac;
            a_eff_cap = V->a_burn[i]*sin(15.0*DEG2RAD)*sf;     /* a_burn already n_eng-scaled */
            r_cut=rx;
        } else if(V->phase[i]==PHA){
            double CNa=CNa_of(V->mach[i]); a_eff_cap=V->qbar[i]*VEH_AREF*CNa*sin(15.0*DEG2RAD)/V->m[i];
        } else if(V->phase[i]==PHL){
            double s=V->h[i]/400.0; if(s>1)s=1; if(s<0)s=0; s*=s;
            double T=engine_thrust(V->thr[i],V->p[i]); a_eff_cap=s*(T/V->m[i])*sin(15.0*DEG2RAD);
        } else a_eff_cap=0.0;
        /* plant t_go from the vertical channel (feet height, vz, mass) */
        double t_go = entry_tgo_estimate(V->h[i], V->vz[i], V->m[i]);
        double a_cmd = ENTRY_DIVERT_KR*(-6.0*rx/(t_go*t_go)) + ENTRY_DIVERT_KV*(-4.0*vx/t_go);
        if(a_cmd> a_eff_cap) a_cmd= a_eff_cap;
        if(a_cmd<-a_eff_cap) a_cmd=-a_eff_cap;
        vx += a_cmd*dt; rx += vx*dt;
        if(fabs(vx)>vpk)vpk=fabs(vx);
    }
    if(vxy_td)*vxy_td=vx; if(peak_vlat)*peak_vlat=vpk; if(td_at_cut)*td_at_cut=r_cut;
    return fabs(rx);
}

/* ================= the frontier for a given (t_fail, steer_frac): whole-traj vcap divert =========
 * vcap=250 m/s matches entrydiv.c's realistic ENTRY ceiling (the burn+coast builds large v_xy that
 * the ~87 s coast carries; 250 is the physical band, entrydiv line 589). Returns D_phys_2eng. */
static double frontier_2eng(double h0, double vz0, double prop0, double dt,
                            double t_fail, int n_eng_after, double steer_frac,
                            double* D_burn_only, double* v_cut, double* fuel_after, int* ok_vert){
    static Traj V;
    static double amax[NMAX];
    sim_entry_vertical_eo(h0,vz0,prop0,dt,t_fail,n_eng_after,&V);
    if(v_cut)*v_cut=V.speed_cut_entry;
    if(fuel_after)*fuel_after=V.fuel_after_entry;
    if(ok_vert)*ok_vert=V.ok;
    build_amax_entry_eo(&V, BANK_CAP_DEG, 15.0, 1, steer_frac, amax);
    double vr, vpk;
    double D = vcap_rest_to_rest(&V, amax, 250.0, &vr, &vpk);
    if(D_burn_only){
        /* burn-phase-only contribution (authority only in [0, i_cut)) */
        static double amask[NMAX];
        int i_cut=V.i_cut_entry>0?V.i_cut_entry:V.n;
        for(int i=0;i<V.n;i++) amask[i]=(i<i_cut)?amax[i]:0.0;
        double vr2,vpk2; *D_burn_only=vcap_rest_to_rest(&V,amask,250.0,&vr2,&vpk2);
    }
    return D;
}

/* ============================================================================================
 *  THE LOC / ATTITUDE-RECOVERABILITY ORACLE (the axis the lateral frontier does NOT see).
 *  At the failure step the surviving side pair -> centroid R/2 off-axis -> induced torque
 *  tau = F_surv * (R/2). The gimbal nulls it with com*(2T)*sin(g), g<=5 deg. Report:
 *    - tau, alpha=tau/I_tr (uncorrected angular accel)
 *    - M_gimbal_max = com*(2T)*sin(5deg), the trim it must supply
 *    - trim cone fraction = tau / M_gimbal_max  (>=1 => gimbal CANNOT hold => guaranteed LOC)
 *    - t_to_LOC = time for wmag to reach 2 rad/s under alpha if UNCORRECTED (vs the 3 s dwell)
 *  Evaluated at the failure sample of the re-integrated 2-engine trajectory (true m, com, I_tr,
 *  thrust at that instant). This decides the ATTITUDE-recoverability, distinct from lateral reach.
 * ============================================================================================ */
typedef struct {
    double m, com, I_tr, Tpereng, F_surv, tau, alpha, M_gimbal_max, trim_frac, t_to_wmag2, h_fail;
    int    holdable;   /* 1 if trim_frac<1 (gimbal can null the induced torque) */
} LOCInfo;

static void loc_at_fail(double h0, double vz0, double prop0, double dt, double t_fail, LOCInfo* L){
    memset(L,0,sizeof(*L));
    static Traj V;
    sim_entry_vertical_eo(h0,vz0,prop0,dt,t_fail,2,&V);
    int i=V.i_fail; if(i<0){ i=0; }
    double m=V.m[i], p=V.p[i], h=V.h[i];
    /* mass_props for com + I_tr at the failure instant */
    double prop=0; /* reconstruct lox/rp1 from mass: m = VEH_DRY + prop */
    prop = m - VEH_DRY; if(prop<0)prop=0;
    double m_rp1=prop/(1.0+MIX_RATIO), m_lox=prop-m_rp1;
    MP mp; mass_props(m_lox,m_rp1,&mp);
    double Tpe=engine_thrust(1.0,p);
    double F_surv=2.0*Tpe;                          /* 2 survivors, full throttle */
    double R=ENG_RING_R;
    double tau=F_surv*(R/2.0);                       /* induced torque about CoM (side-out centroid R/2) */
    double alpha=tau/mp.I_tr;
    double Mg=mp.com*F_surv*sin(ENG_GIMBAL_MAX);      /* max counter-gimbal moment (control.c:187-188) */
    double trim_frac=tau/Mg;
    /* time for wmag to reach 2 rad/s under constant alpha from rest: t=sqrt? no, w=alpha*t => t=2/alpha */
    double t2 = (alpha>1e-9)? WMAG_LOC/alpha : 1e18;
    L->m=m; L->com=mp.com; L->I_tr=mp.I_tr; L->Tpereng=Tpe; L->F_surv=F_surv;
    L->tau=tau; L->alpha=alpha; L->M_gimbal_max=Mg; L->trim_frac=trim_frac;
    L->t_to_wmag2=t2; L->holdable=(trim_frac<1.0); L->h_fail=h;
}

/* ================= SELF-TESTS (closed-form limits the prompt requires) =================
 * ST1: rest-to-rest bang-bang on a CONSTANT-a synthetic must reproduce D=a*T^2/4; vcap trapezoid
 *      D=vcap*T-vcap^2/a. (Solver correctness, same as ceiling.c.)
 * ST2: t_fail AFTER burn-end reproduces the CLEAN 3-engine frontier (no engine ever drops).
 * ST3: t_fail at burn-start with n_eng_after=3 AND steer_frac=1 reproduces the CLEAN frontier
 *      (a no-op failure: same n_eng, full cone). Both are HARD equalities (<0.5% for ST2 vertical
 *      re-integration identity; exact for ST3 which never changes anything). */
static int self_test(void){
    int pass=1;
    /* ST1 — synthetic constant-a */
    { static Traj V; memset(&V,0,sizeof(V));
      double a=3.0, T=40.0, dt=0.002; int n=(int)(T/dt); if(n>NMAX)n=NMAX; V.n=n; V.dt=dt;
      static double amax[NMAX];
      for(int i=0;i<n;i++){ V.t[i]=i*dt; amax[i]=a; }
      double vr;
      double dbb=bb_rest_to_rest(&V,amax,&vr,NULL);   double dbb_cf=a*T*T/4.0;
      double vcap=25.0; double dvc=vcap_rest_to_rest(&V,amax,vcap,&vr,NULL);
      double dvc_cf=vcap*T - vcap*vcap/a;
      double e1=100*fabs(dbb-dbb_cf)/dbb_cf, e2=100*fabs(dvc-dvc_cf)/dvc_cf;
      printf("  ST1 bang-bang: solver=%.2f cf=%.2f (err %.3f%%) ; vcap: solver=%.2f cf=%.2f (err %.3f%%) -> %s\n",
             dbb,dbb_cf,e1,dvc,dvc_cf,e2,(e1<0.5&&e2<0.5)?"PASS":"FAIL");
      if(!(e1<0.5&&e2<0.5))pass=0;
    }
    /* ST2 — t_fail after burn-end == clean 3-engine frontier */
    { double Dclean = frontier_2eng(62000,-1500,30000,0.001, -1.0, 2, 1.0, NULL,NULL,NULL,NULL);
      double Dlate  = frontier_2eng(62000,-1500,30000,0.001, 60.0, 2, 0.5, NULL,NULL,NULL,NULL); /* t_fail>burn-end */
      double e=100*fabs(Dlate-Dclean)/Dclean;
      printf("  ST2 t_fail=60s (after ~25s cut): D=%.1f  vs clean D=%.1f  (err %.4f%%) -> %s\n",
             Dlate,Dclean,e,(e<0.5)?"PASS":"FAIL");
      if(!(e<0.5))pass=0;
    }
    /* ST3 — t_fail at burn-start, n_eng_after=3, steer_frac=1 == clean (no-op failure) */
    { double Dclean = frontier_2eng(62000,-1500,30000,0.001, -1.0, 2, 1.0, NULL,NULL,NULL,NULL);
      double Dnoop  = frontier_2eng(62000,-1500,30000,0.001,  1.0, 3, 1.0, NULL,NULL,NULL,NULL);
      double e=100*fabs(Dnoop-Dclean)/Dclean;
      printf("  ST3 t_fail=1s but n_eng_after=3 & steer=1.0 (no-op): D=%.1f vs clean D=%.1f (err %.4f%%) -> %s\n",
             Dnoop,Dclean,e,(e<0.5)?"PASS":"FAIL");
      if(!(e<0.5))pass=0;
    }
    printf("  SELF-TEST: %s\n\n", pass?"ALL PASS":"FAIL");
    return pass;
}

/* ================= normal CDF for the in-frontier fraction ================= */
static double norm_cdf(double x){ return 0.5*erfc(-x/sqrt(2.0)); }

int main(void){
    init_engine();
    FILE* out=stdout;
    fprintf(out,"================================================================================\n");
    fprintf(out," 2-ENGINE FRONTIER ORACLE (ceiling_eo.c) — ENTRY engine-out (D-025 BLOCKING)\n");
    fprintf(out,"================================================================================\n");
    fprintf(out,"Engine: T_vac=%.1f N  thrust3(1,0)=%.1f  thrust2(1,0)=%.1f N\n",
            ENG_T_VAC,3.0*engine_thrust(1.0,0.0),2.0*engine_thrust(1.0,0.0));
    fprintf(out,"ENG_RING_R=%.3f m  side-out centroid=R/2=%.3f m  gimbal_max=%.1f deg\n",
            ENG_RING_R,ENG_RING_R/2.0,ENG_GIMBAL_MAX*RAD2DEG);
    double prop0=30000.0, com0=mass_com(prop0-prop0/(1.0+MIX_RATIO),prop0/(1.0+MIX_RATIO),NULL);
    fprintf(out,"ENTRY IC (scenario.c:18): h0=62000 vz0=-1500 offset=3000 prop0=%.0f (com0=%.2f)\n",prop0,com0);
    fprintf(out,"Random draw (main.c:346,350): SIDE engine, t_fail ~ U[%.0f,%.0f] s ; offset mean 3000 sigma 250/axis\n",EO_T0,EO_T1);
    fprintf(out,"LOC gate (sim.c:566): wmag>%.0f rad/s sustained >%.0f s -> F_LOC\n\n",WMAG_LOC,LOC_DWELL);

    /* ---------- SELF-TESTS ---------- */
    fprintf(out,"--- SELF-TESTS (closed-form limits) ---\n");
    self_test();

    /* ---------- dt-convergence of D_phys_2eng at a mid-window failure ---------- */
    fprintf(out,"--- dt-convergence: D_phys_2eng at t_fail=11s, steer_frac=0.5 (60%% debit) ---\n");
    fprintf(out,"   dt(s)     D_phys_2eng(m)   D_burn_only(m)   v_cut(m/s)   fuel_after(kg)\n");
    double dts[4]={0.004,0.002,0.001,0.0005};
    for(int i=0;i<4;i++){ double Db,vc,fa; int okv;
        double D=frontier_2eng(62000,-1500,prop0,dts[i],11.0,2,0.5,&Db,&vc,&fa,&okv);
        fprintf(out,"   %6.4f   %13.1f   %14.1f   %10.1f   %11.0f\n",dts[i],D,Db,vc,fa);
    }
    fprintf(out,"\n");

    /* ---------- CLEAN 3-engine reference (must match entrydiv.c ~25590 m) ---------- */
    { double Db,vc,fa; int okv;
      double Dclean=frontier_2eng(62000,-1500,prop0,0.001,-1.0,2,1.0,&Db,&vc,&fa,&okv);
      fprintf(out,"--- CLEAN 3-engine reference (parity vs entrydiv.c) ---\n");
      fprintf(out," D_phys_clean (vcap250, whole-traj) = %.1f m  (entrydiv.c reported 25590) ; burn-only=%.1f m\n",Dclean,Db);
      fprintf(out," v_cut=%.1f m/s  fuel_after_entry=%.0f kg  vert_ok=%d\n\n",vc,fa,okv);
    }

    /* ---------- D_phys_2eng(t_fail) across the [4,18]s window, x the trim-debit sensitivity ---------- */
    fprintf(out,"================= D_phys_2eng(t_fail) — the SHRUNKEN FRONTIER =================\n");
    fprintf(out," One SIDE engine dies at t_fail (n_eng 3->2). Whole-trajectory vcap250 divert.\n");
    fprintf(out," steer_frac = fraction of the +-5deg gimbal cone left for STEERING after the survivor-\n");
    fprintf(out," centroid trim debit (0.60=60%% debit, 0.40=40%%, 0.20=80%%). Offset to close = 3000 m.\n\n");
    fprintf(out,"   t_fail(s)   D_2eng[debit40%%,sf=.60]  D_2eng[debit60%%,sf=.40]  D_2eng[debit80%%,sf=.20]   v_cut(m/s)  fuel(kg)\n");
    double tfs[13]={4,5,6,7,8,9,10,11,12,13,14,16,18};
    double sfracs[3]={0.60,0.40,0.20};   /* debit 40/60/80% */
    for(int i=0;i<13;i++){
        double Ds[3]; double vc=0,fa=0; int okv=0;
        for(int j=0;j<3;j++) Ds[j]=frontier_2eng(62000,-1500,prop0,0.001,tfs[i],2,sfracs[j],NULL,&vc,&fa,&okv);
        fprintf(out,"   %7.1f   %20.1f  %20.1f  %20.1f   %9.1f  %8.0f\n",
                tfs[i],Ds[0],Ds[1],Ds[2],vc,fa);
    }
    fprintf(out,"\n Interpretation: if EVERY cell >> 3000 m, the LATERAL 2-engine reachable set is NOT the\n");
    fprintf(out," binding constraint -> MPPI's 1/60 is NOT a lateral-frontier (directive-6) failure.\n\n");

    /* ---------- THE LOC / ATTITUDE-RECOVERABILITY ORACLE (the real binding axis) ---------- */
    fprintf(out,"================= LOC / ATTITUDE RECOVERABILITY (the axis lateral reach misses) =================\n");
    fprintf(out," Induced torque tau=F_surv*R/2 ; alpha=tau/I_tr ; max counter-gimbal M=com*F_surv*sin(5deg).\n");
    fprintf(out," trim_frac=tau/M (>=1 => gimbal CANNOT null => guaranteed LOC). t_to_wmag2 = 2/alpha (rad/s\n");
    fprintf(out," reached if UNcorrected; the F_LOC dwell is %.0f s). Evaluated at each failure instant.\n\n",LOC_DWELL);
    fprintf(out,"   t_fail(s)  h_fail(m)  m(kg)   com(m)   I_tr(kg m^2)  tau(N m)   alpha(rad/s^2)  M_gim(N m)  trim_frac  t_wmag2(s)  HOLDABLE\n");
    for(int i=0;i<13;i++){ LOCInfo L; loc_at_fail(62000,-1500,prop0,0.001,tfs[i],&L);
        fprintf(out,"   %7.1f  %8.0f  %6.0f  %6.2f  %11.3e  %8.3e  %11.3f    %9.3e  %8.3f   %8.3f   %s\n",
                tfs[i],L.h_fail,L.m,L.com,L.I_tr,L.tau,L.alpha,L.M_gimbal_max,L.trim_frac,L.t_to_wmag2,
                L.holdable?"yes":"NO(LOC)");
    }
    fprintf(out,"\n Interpretation: trim_frac<1 everywhere => the 2-engine gimbal CAN statically hold the\n");
    fprintf(out," survivor-centroid trim (attitude is recoverable), leaving (1-trim_frac) of the cone to\n");
    fprintf(out," steer. t_wmag2 >> the 3 s dwell => even the transient before the loop catches does not\n");
    fprintf(out," trip F_LOC. If trim_frac>=1 at any t_fail => THAT failure is an honest guaranteed LOC.\n\n");

    /* ---------- PLANT-FAITHFUL CLOSED-LOOP: does the deployed ZEM/ZEV law realize the closure? ---------- */
    fprintf(out,"================= PLANT-FAITHFUL CLOSED-LOOP DIVERT (sim.c KR=2.0/KV=3.5, 2-engine) =================\n");
    fprintf(out," The ACTUAL entry_divert_step law under 2-engine authority from the 3000 m offset. This is what\n");
    fprintf(out," MPPI's E3 supervisor commands. Shows whether the reachability is ACHIEVED (td_lat small, soft vxy),\n");
    fprintf(out," not merely inside the theoretical frontier. (Pure lateral guidance; no attitude-transient coupling\n");
    fprintf(out," -- that is the one axis this static oracle cannot close; see the report.)\n\n");
    fprintf(out,"   t_fail(s)  steer_frac  td_lat(m)  vxy_td(m/s)  peak|v_lat|  lat@cut(m)   ON-PAD(<=26m)?\n");
    { static Traj Vt;
      for(int i=0;i<13;i++){ double vtd,vpk,rc;
        sim_entry_vertical_eo(62000,-1500,prop0,0.001,tfs[i],2,&Vt);
        double lat=zemzev_closed_loop_eo(&Vt,3000.0,0.40,&vtd,&vpk,&rc);
        fprintf(out,"   %7.1f   %8.2f   %8.2f   %9.3f    %9.1f   %9.1f    %s\n",
                tfs[i],0.40,lat,vtd,vpk,rc,(lat<=PAD_RADIUS)?"YES":"no");
      }
    }
    fprintf(out,"\n Interpretation: if td_lat is small (<= a few hundred m, closable by the aero+landing tail) the\n");
    fprintf(out," deployed 2-engine divert REACHES the pad laterally -> the 1/60 is not a lateral-guidance limit.\n");
    fprintf(out," (D-020 smoke: in-burn 1@20 under --mppi CRASHED 1787 m off -> a controller that flew nowhere near\n");
    fprintf(out," the 12+km frontier, corroborating shortfall, not physics.)\n\n");

    /* ---------- THE RANDOM-DRAW VERDICT: what fraction of draws land INSIDE the frontier ---------- */
    fprintf(out,"================= RANDOM-DRAW IN-FRONTIER FRACTION (the CEILING on landed rate) =================\n");
    fprintf(out," Draw model (sim.c/main.c): t_fail ~ U[%.0f,%.0f] s (all in-burn), SIDE engine; ENTRY offset\n",EO_T0,EO_T1);
    fprintf(out," effective radius R_off = sqrt((3000+eps_x)^2 + eps_y^2), eps ~ N(0,250) (scenario.c:18,53).\n");
    fprintf(out," A draw is IN-frontier iff R_off <= D_phys_2eng(t_fail). Report per debit level.\n\n");

    /* worst-case (min over the window) D_phys_2eng per debit; and the fraction of offset draws inside it */
    for(int j=0;j<3;j++){
        double Dmin=1e18, Dmax=-1e18, Dmean=0; int cnt=0;
        for(int i=0;i<13;i++){ double D=frontier_2eng(62000,-1500,prop0,0.001,tfs[i],2,sfracs[j],NULL,NULL,NULL,NULL);
            if(D<Dmin)Dmin=D; if(D>Dmax)Dmax=D; Dmean+=D; cnt++; }
        Dmean/=cnt;
        /* Offset-only in-frontier fraction: P(R_off <= D). R_off ~ (3000+eps_x, eps_y). Since D>>3000
         * or D<3000 dominates, use the exact axial marginal on the dominant x-axis plus a Monte-integral
         * over the small y. Closed enough: P(R_off<=D) with x~N(3000,250), y~N(0,250):
         *   for D>=~ we integrate radially. Use a fine deterministic grid (no RNG). */
        double frac_min=0.0, frac_mean=0.0, frac_max=0.0;
        double Ds3[3]={Dmin,Dmean,Dmax};
        double fr3[3];
        for(int k=0;k<3;k++){
            double D=Ds3[k]; double inside=0, total=0;
            /* deterministic grid over eps_x,eps_y in +-4 sigma */
            int NG=241; double s=250.0, lo=-4.0*s, hi=4.0*s, dstep=(hi-lo)/(NG-1);
            for(int a=0;a<NG;a++){ double ex=lo+a*dstep; double wx=exp(-0.5*ex*ex/(s*s));
                for(int b=0;b<NG;b++){ double ey=lo+b*dstep; double wy=exp(-0.5*ey*ey/(s*s));
                    double w=wx*wy; total+=w;
                    double rx=3000.0+ex, roff=sqrt(rx*rx+ey*ey);
                    if(roff<=D) inside+=w;
                } }
            fr3[k]=(total>0)?inside/total:0.0;
        }
        frac_min=fr3[0]; frac_mean=fr3[1]; frac_max=fr3[2];
        fprintf(out," debit %s (steer_frac=%.2f): D_2eng over window = [min %.0f, mean %.0f, max %.0f] m\n",
                (j==0?"40%":j==1?"60%":"80%"),sfracs[j],Dmin,Dmean,Dmax);
        fprintf(out,"   in-frontier fraction of the OFFSET distribution: at D_min=%.3f  at D_mean=%.3f  at D_max=%.3f\n",
                frac_min,frac_mean,frac_max);
    }
    fprintf(out,"\n NOTE: this fraction is the LATERAL-reach ceiling ONLY. The TRUE ceiling on landed rate is\n");
    fprintf(out," min(lateral-in-frontier, attitude-recoverable, terminal-null-achievable). See the LOC table\n");
    fprintf(out," (attitude) and the report for the terminal-null axis. If lateral-in-frontier ~ 1.0 AND\n");
    fprintf(out," attitude is holdable everywhere, then MPPI's 1/60 is a CONTROLLER shortfall, not physics.\n\n");

    fprintf(out,"================================================================================\n");
    fprintf(out," VERDICT: see D_phys_2eng table (lateral), LOC table (attitude), in-frontier fraction.\n");
    fprintf(out,"          Report: runs/eo_frontier_report.md\n");
    fprintf(out,"================================================================================\n");
    fprintf(out,"(done)\n");
    return 0;
}
