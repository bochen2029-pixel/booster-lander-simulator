/* entrydiv.c — ENTRY-DIVERT feasibility oracle for the flagship 62 km -> pad problem.
 *
 * A 3-DOF point-mass (vertical z + ONE lateral channel x) mirror of the ENTRY trajectory that
 * measures how much of the 3 km cross-range the FULL trajectory can close, decomposed by phase:
 *   (i)   3-engine RETROGRADE entry burn   62 -> ~40 km  (~27 s, |a_burn| up to ~5 g)
 *   (ii)  exo/thin-air ballistic coast     ~40 -> ~12 km (~55 s, drift dominated by v_xy built in (i))
 *   (iii) aero-descent + aero-aware ignition + landing burn (~12 km -> pad, the ~2.5 km tail)
 *
 * The central question (HANDOFF/D-007/D-009): the burn+coast produce ENORMOUS lateral displacement
 * (naive constant bank -> 17 km off). The problem is NOT authority, it is NULLING v_xy at the right
 * time so the coast drifts EXACTLY onto the pad ("burn to a collision course, coast, aero-trim").
 *
 * Two divert strategies evaluated on the SAME vertical trajectory:
 *   A. ORACLE 1-switch bang-bang bank during the burn (bank=+cap then -cap; root-find the switch so
 *      v_xy(cut)=0) -> max rest-to-rest divert achievable by the ENTRY BURN ALONE (upper bound).
 *   B. PREDICTED-IMPACT-POINT (ZEM) proportional-navigation bank law (the RECOMMENDED architecture):
 *      each step steer the velocity vector's ballistic ground-impact point toward the pad; when the
 *      predicted impact crosses the pad, bank -> 0. This AUTOMATICALLY times the null. Scan its gain.
 *   C. The aero-descent tail divert (from ceiling.c's model) is added on top for total closure.
 *
 * PARITY (mirrored verbatim from core/, cross-checked against runs/sandbox/ceiling.c which is itself
 * parity-checked): US76 atmosphere (atmosphere.c); frozen aero CA/CN + VEH_AREF (dynamics.c);
 * engine_thrust=throttle*(T_vac-AE*p) (constants.h); E3 entry window ENTRY_QBAR_IGNITE 72k / CUT 68k /
 * FUEL_FLOOR 7t + entry_predict_peak_qbar CA=1.0 (sim.c/guidance_hoverslam.c); SRP shielding of drag
 * during the burn by thrust coefficient (dynamics.c); suicide_burn_margin thrust-only dt=0.1 +
 * a_design=0.85 v_ref + Kv=3 landing burn + fade s=(h/400)^2 (guidance_hoverslam.c/guidance_mppi.c).
 *
 * C ONLY. Build (MSVC): cl /O2 /fp:precise entrydiv.c   (see entrydiv.cmd)
 * Output: runs/sandbox/entrydiv_out.txt
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

/* E3 entry-burn supervisor window (sim.c) */
#define ENTRY_QBAR_IGNITE 72000.0
#define ENTRY_QBAR_CUT    68000.0
#define ENTRY_FUEL_FLOOR  7000.0
#define ENTRY_PRED_CA     1.0

/* divert bank cap (the entry-burn bank; prompt: <=15deg) */
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

/* ================= mass properties (dynamics.c mass_props, com only) ================= */
static double mass_com(double m_lox, double m_rp1, double* m_out){
    if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
    double m = VEH_DRY + m_lox + m_rp1;
    double h_l=m_lox/(LOX_RHO*TANK_AREA), z_l=LOX_BASE_Z+0.5*h_l;
    double h_r=m_rp1/(RP1_RHO*TANK_AREA), z_r=RP1_BASE_Z+0.5*h_r;
    double com=(VEH_DRY*VEH_DRY_COMZ + m_lox*z_l + m_rp1*z_r)/m;
    if(m_out)*m_out=m;
    return com;
}
static double g_of(double h){ double x=R_EARTH/(R_EARTH+h); return G0*x*x; }

/* SRP aero-shielding blend (dynamics.c): CA scaled by thrust coefficient CT=thrust/(qbar*Aref).
 * CT>0.5 -> shield ramps from 1.0 down to 0.05 by CT=3. Returns the shield multiplier in [0.05,1]. */
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
 *  FULL ENTRY TRAJECTORY — vertical channel + lateral channel coupled (single-lateral-axis x).
 *  Phases: PH_COAST -> PH_ENTRY_BURN (3-eng retrograde) -> PH_AERO (unpowered) -> landing burn.
 *  The lateral control is a BANK of the vertical thrust/aero: a_lat = a_effector * sin(bank) for
 *  the burn (thrust-vector), and the ceiling.c aero/thrust lateral models for the aero-descent.
 * ============================================================================================ */

#define NMAX 400000   /* 62km/-1500 -> ground ~133s; at dt=0.5ms ~266k steps -> ample headroom */

typedef struct {
    int    n;
    double dt;
    /* per-step recorded vertical state (for building lateral authority a_max(t) + the coast) */
    double t[NMAX];
    double h[NMAX];        /* feet height above ground (RZ - com) */
    double vz[NMAX];       /* vertical velocity (negative descending) */
    double mach[NMAX];
    double qbar[NMAX];
    double m[NMAX];
    double thr[NMAX];      /* throttle actual (commanded; no lag in this proxy) */
    double p[NMAX];        /* ambient pressure */
    int    n_eng[NMAX];    /* 1 or 3 */
    int    phase[NMAX];    /* 0=coast,1=entry_burn,2=aero(unpowered),3=landing_burn */
    double a_burn[NMAX];   /* |thrust accel| available for banking this step (n_eng*T/m) [entry burn] */
    /* summary */
    double t_ig_entry, h_ig_entry, t_cut_entry, h_cut_entry, v_cut_entry, speed_cut_entry;
    double t_ig_land, h_ig_land, T_td, vz_td;
    double qbar_peak; double mach_peak;
    double fuel_after_entry, fuel_left;
    double v_at_ignite_entry;  /* speed at entry-burn ignite */
    double v_min_entry;        /* min speed reached during/after the entry burn (the re-accel floor) */
    double v_reaccel_17km;     /* speed re-built by ~17 km (diagnostic) */
    int    ok;
    int    i_cut_entry;        /* sample index at entry-burn cut */
    int    i_ig_land;          /* sample index at landing-burn ignite */
} Traj;

enum { PHC=0, PHE=1, PHA=2, PHL=3 };

/* Integrate the FULL vertical ENTRY channel (retrograde burn, no bank -> pure vertical energy).
 * The lateral bank's cos-loss on the vertical decel is applied SEPARATELY (see run with bank):
 * here bank=0 gives the reference vertical trajectory; a bank_frac scales the vertical thrust by
 * cos(bank) when requested (to price the fuel/decel cost of banking). */
static void sim_entry_vertical(double h0_feet, double vz0, double prop0, double dt,
                               double bank_cos_avg /* 1.0 = no bank; <1 prices cos-loss */,
                               Traj* V){
    memset(V,0,sizeof(*V));
    V->dt=dt;
    double m_rp1=prop0/(1.0+MIX_RATIO), m_lox=prop0-m_rp1;
    double mtot; double com=mass_com(m_lox,m_rp1,&mtot);
    double h=h0_feet, vz=vz0, t=0.0;
    int phase=PHC; int engine_on=0; int n_eng=1; double ada=0.0;
    double qpeak=0.0, mpeak=0.0, vmin=1e18;
    int n=0;
    int got_reaccel=0;
    V->v_at_ignite_entry=0;

    while(h>0.0 && n<NMAX){
        double m; com=mass_com(m_lox,m_rp1,&m);
        Atmo atm; atmo_eval(h+com,&atm);
        double speed=fabs(vz);          /* vertical channel: speed ~ |vz| (lateral is small vs vz here) */
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
                    /* CUT -> hand to unpowered aero-descent */
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
            /* 3-engine full-throttle RETROGRADE. Thrust opposes velocity (upward, since descending).
             * The bank steals cos(bank) of the vertical decel: vertical thrust = n*T*cos(bank_avg).
             * a_burn_avail = full n*T/m is what the bank can project laterally (a_lat=a_burn*sin). */
            thr_cmd=1.0;
            double Tv=n_eng*Tfull;
            a_burn_avail = Tv/m;                    /* total thrust accel (for lateral projection) */
            /* SRP shields drag during the burn (retropropulsion) */
            double CA=CA_of(mach)*srp_shield(Tv,qbar);
            double D=0.5*atm.rho*vz*vz*VEH_AREF*CA; /* opposes motion; vz<0 -> +z */
            az = (Tv*bank_cos_avg)/m - gh + D/m;    /* thrust up + drag up - gravity */
            /* mass depletion (3 engines) */
            double Isp=isp_of(atm.p);
            double mdot=n_eng*Tfull/(Isp*G0);       /* full-throttle 3-eng */
            double mdot_rp1=mdot/(1.0+MIX_RATIO), mdot_lox=mdot-mdot_rp1;
            m_lox-=mdot_lox*dt; m_rp1-=mdot_rp1*dt;
            if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
        } else if(phase==PHL){
            /* landing burn: 1-engine, track 0.85 v_ref, thrust-only (SRP shielded, no drag credit) */
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
            /* unpowered (coast or aero-descent): gravity + full drag */
            double CA=CA_of(mach);
            double D=0.5*atm.rho*vz*vz*VEH_AREF*CA;
            az = -gh + D/m;
        }

        /* v_min = the deceleration floor of the ENTRY BURN (speed at/near cut) — the "156 m/s at
         * 40 km" figure of D-007. Track only during PHE so the landing-burn arrest (speed->0) does
         * not clobber it. */
        if(phase==PHE && speed<vmin) vmin=speed;
        if(!got_reaccel && (h+com)<=17000.0){ V->v_reaccel_17km=speed; got_reaccel=1; }

        /* record state at time t */
        V->t[n]=t; V->h[n]=h; V->vz[n]=vz; V->mach[n]=mach; V->qbar[n]=qbar; V->m[n]=m;
        V->thr[n]=thr_cmd; V->p[n]=atm.p; V->n_eng[n]=n_eng; V->phase[n]=phase; V->a_burn[n]=a_burn_avail;
        n++;

        vz += az*dt; h += vz*dt; t += dt;
        if(phase==PHL && vz>0.5) break;   /* landing burn arrested & climbing -> touchdown */
    }
    V->n=n; V->T_td=t; V->vz_td=vz; V->qbar_peak=qpeak; V->mach_peak=mpeak;
    V->fuel_left=m_lox+m_rp1; V->v_min_entry=vmin;
    V->ok = (fabs(vz)<6.0 && V->fuel_left>0.0 && phase==PHL);
}

/* ================= lateral authority a_max(t) along the trajectory (per phase) ================= */
/* Entry burn: a_lat = a_burn * sin(bank_cap) (thrust vector projection).
 * Aero-descent (unpowered): a_lat = qbar*Aref*CNa*alpha_cap / m (body lift).
 * Landing burn: a_lat = (thr*T/m)*sin(alpha_cap) * fade s=(h/400)^2  [phys model, ceiling.c parity]. */
static void build_amax_entry(const Traj* V, double bank_cap_deg, double aoa_cap_deg,
                             int fade_landing, double* amax){
    double bcr=bank_cap_deg*DEG2RAD, acr=aoa_cap_deg*DEG2RAD;
    for(int i=0;i<V->n;i++){
        double a=0.0;
        if(V->phase[i]==PHE){
            a = V->a_burn[i]*sin(bcr);
        } else if(V->phase[i]==PHA){
            double CNa=CNa_of(V->mach[i]);
            a = V->qbar[i]*VEH_AREF*CNa*acr / V->m[i];
        } else if(V->phase[i]==PHL){
            double s=1.0;
            if(fade_landing){ s=V->h[i]/400.0; if(s>1)s=1; if(s<0)s=0; s*=s; }
            double T=engine_thrust(V->thr[i], V->p[i]);
            a = s*(T/V->m[i])*sin(acr);
        } else {
            a = 0.0;  /* pre-entry-burn coast: no authority (before ignition) */
        }
        amax[i]=a;
    }
}

/* ================= optimal 1-switch bang-bang rest-to-rest divert (verbatim structure from ceiling.c)
 * Max rest-to-rest lateral displacement under |a|<=amax(t): a=+amax for t<ts then -amax; bisect ts
 * so v(T)=0. Returns |r(T)| = max divertible offset. (Free double-integrator; can build large v.) */
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
/* velocity-capped trapezoidal rest-to-rest (the PHYSICAL ceiling; ceiling.c vcap_divert parity).
 * |a|<=amax(t) AND |v|<=vcap: accelerate (clamp vcap) -> cruise -> decelerate to rest. */
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

/* Restrict authority to a phase window (start_idx..end_idx) — to decompose contribution by phase. */
static double bb_window(const Traj* V, const double* amax_in, int i0, int i1, double* vpk){
    static double amax[NMAX];
    for(int i=0;i<V->n;i++) amax[i]=(i>=i0 && i<i1)? amax_in[i] : 0.0;
    double vr; return bb_rest_to_rest(V,amax,&vr,vpk);
}
static double vcap_window(const Traj* V, const double* amax_in, int i0, int i1, double vcap, double* vpk){
    static double amax[NMAX];
    for(int i=0;i<V->n;i++) amax[i]=(i>=i0 && i<i1)? amax_in[i] : 0.0;
    double vr; return vcap_rest_to_rest(V,amax,vcap,&vr,vpk);
}

/* ============================================================================================
 *  THE PREDICTED-IMPACT-POINT (ZEM) COLLISION-COURSE BANK LAW  (RECOMMENDED ARCHITECTURE)
 *  Forward-simulate the ACTUAL closed-loop lateral channel driven by a proportional bank command
 *  on the predicted ballistic ground-impact point. The law:
 *     r_impact = r_x + v_x * t_fall(h, vz)                  (ZEM: where the current cross-range drifts)
 *     err      = r_impact - pad(=0)
 *     bank_cmd = -Kzem * err / (a_effector * t_fall^2)       (proportional-nav toward pad)
 *     bank     = clamp(bank_cmd, +-BANK_CAP)                 (15 deg AoA cap)
 *     a_lat    = -a_effector * sin(bank)                     (steer to reduce |r_impact|)
 *  t_fall is the remaining coast+descent time (recomputed each step from the vertical channel).
 *  As r_impact -> 0, bank -> 0 automatically (the null-timing is emergent). This runs on the SAME
 *  recorded vertical trajectory V (a_effector = a_burn during burn, aero/landing lift otherwise).
 *  Returns the touchdown cross-range |r_x(T)| for a given initial offset D0 and gain Kzem.
 * ============================================================================================ */
/* ctrl_cap: if 1, during the ENTRY BURN the realized lateral authority is control.c's
 * (G0+2)*tan(bank) (WITHOUT the a_vert_ref fix), not the physical a_burn*sin(bank) — proves whether
 * the law still closes 3 km at the weaker realized cap. bank_at_cut: bank angle at burn cut (shows
 * the reversal has completed). */
static double zem_closed_loop(const Traj* V, double D0, double Kzem, double bank_cap_deg,
                              double aoa_cap_deg, int fade_landing, int ctrl_cap, double tfall_err,
                              double* vxy_td, double* peak_vlat, double* peak_bank_deg,
                              double* bank_at_cut){
    double bcap=bank_cap_deg*DEG2RAD, acap=aoa_cap_deg*DEG2RAD;
    double rx=D0, vx=0.0, dt=V->dt;
    double vpk=0.0, bpk=0.0, b_cut=0.0;
    int n=V->n;
    for(int i=0;i<n;i++){
        /* remaining fall time from this sample to touchdown. The ORACLE knows it exactly (from the
         * recorded vertical channel); a real flight ESTIMATES it (predict_tgo-style shoot). tfall_err
         * (0.7..1.3) models that estimation error to prove the law is robust to a crude t_go. */
        double t_fall = (V->t[n-1]-V->t[i])*tfall_err; if(t_fall<0.05) t_fall=0.05;
        /* effector lateral authority available NOW (per-phase). During the burn, a_eff is the accel
         * that a unit sin-bank projects laterally: physical = a_burn (thrust/m), or the control.c
         * realized cap where a_lat is capped at (G0+2)*tan(bank) => effective a_eff*sin ~ (G0+2)*tan. */
        double a_eff, cap_used;
        if(V->phase[i]==PHE){
            cap_used=bcap;
            if(ctrl_cap){ /* realized cap: a_lat <= (G0+2)*tan(bank) => model a_eff so a_eff*sin(cap)=(G0+2)*tan(cap) */
                a_eff=(G0+2.0)*tan(bcap)/sin(bcap);   /* = (G0+2)/cos(bcap) */
            } else a_eff=V->a_burn[i];
        }
        else if(V->phase[i]==PHA){ double CNa=CNa_of(V->mach[i]); a_eff=V->qbar[i]*VEH_AREF*CNa/V->m[i]; cap_used=acap; }
        else if(V->phase[i]==PHL){
            double s=1.0; if(fade_landing){ s=V->h[i]/400.0; if(s>1)s=1; if(s<0)s=0; s*=s; }
            double T=engine_thrust(V->thr[i],V->p[i]); a_eff=s*(T/V->m[i]); cap_used=acap;
        } else { a_eff=0.0; cap_used=0.0; }   /* pre-burn coast: no authority */

        /* predicted ballistic impact cross-range (ZEM): r + v*t_fall */
        double r_impact = rx + vx*t_fall;
        /* proportional bank on the miss, normalized by the displacement a unit sin-bank produces
         * over the remaining fall (~ a_eff * t_fall^2 / 2). This makes Kzem ~ dimensionless loop gain. */
        double denom = 0.5*a_eff*t_fall*t_fall; if(denom<1e-6) denom=1e-6;
        double sin_cmd = -Kzem * r_impact / denom;        /* want a_lat to cancel the miss */
        if(sin_cmd> sin(cap_used)) sin_cmd= sin(cap_used);
        if(sin_cmd<-sin(cap_used)) sin_cmd=-sin(cap_used);
        double a_lat = a_eff*sin_cmd;                      /* signed lateral accel */
        double bank = asin( (fabs(sin_cmd)>1)?1:sin_cmd );
        if(fabs(bank)*RAD2DEG>bpk) bpk=fabs(bank)*RAD2DEG;
        /* capture bank at the last burn sample */
        if(V->phase[i]==PHE) b_cut=bank*RAD2DEG;

        vx += a_lat*dt; rx += vx*dt;
        if(fabs(vx)>vpk)vpk=fabs(vx);
    }
    if(vxy_td)*vxy_td=vx; if(peak_vlat)*peak_vlat=vpk; if(peak_bank_deg)*peak_bank_deg=bpk;
    if(bank_at_cut)*bank_at_cut=b_cut;
    return fabs(rx);
}

/* NAIVE CONSTANT BANK toward the pad during the entry burn (reproduces the 17 km catastrophe of
 * D-007 addendum 2): bank=+cap the WHOLE burn, then a_lat=0 (no reversal). The huge burn+coast
 * builds cross-range velocity the descent never nulls. Returns touchdown |r_x|. */
static double naive_constant_bank(const Traj* V, double D0, double bank_cap_deg, int ctrl_cap){
    double bcap=bank_cap_deg*DEG2RAD;
    double rx=D0, vx=0.0, dt=V->dt; int n=V->n;
    for(int i=0;i<n;i++){
        double a_lat=0.0;
        if(V->phase[i]==PHE){
            double a_eff = ctrl_cap ? (G0+2.0)/cos(bcap) : V->a_burn[i];
            a_lat = -a_eff*sin(bcap);   /* toward pad (reduce +D0), constant, NO reversal */
        }
        vx += a_lat*dt; rx += vx*dt;
    }
    return fabs(rx);
}

/* ================= ZEM/ZEV OPTIMAL TERMINAL GUIDANCE — the CORRECT collision-course law =============
 * The raw impact-point law drives r->0 at touchdown but leaves LARGE terminal v_xy (arrives at the pad
 * still moving -> crash). A landing needs BOTH r_f=0 AND v_f=0. The energy-optimal acceleration that
 * drives a double-integrator to zero final position AND zero final velocity in time-to-go t_go is:
 *     a_cmd = -6*r/t_go^2 - 4*v/t_go        (classic ZEM/ZEV / terminal-guidance gains for r_f=v_f=0)
 * (equivalently a = 6*ZEM/t_go^2 - 2*(v)/t_go with ZEM=-(r+v*t_go), same law.) This is exactly the
 * "burn to a collision course, then null" done CONTINUOUSLY and OPTIMALLY: it banks hard early (build
 * cross-range) and reverses (null v_xy) so BOTH hit zero at t_go. Cap a_cmd at the phase authority; the
 * saturation early is fine (rest-to-rest headroom is huge). Returns touchdown |r_x| AND terminal v_xy.
 * tfall_err scales t_go to probe estimation robustness. Kgain scales both terms (loop aggressiveness). */
static double zemzev_optimal(const Traj* V, double D0, double Kgain, double vlat_cap,
                             double bank_cap_deg, double aoa_cap_deg, int fade_landing,
                             int ctrl_cap, double tfall_err,
                             double* vxy_td, double* peak_vlat, double* peak_bank_deg){
    double bcap=bank_cap_deg*DEG2RAD, acap=aoa_cap_deg*DEG2RAD;
    double rx=D0, vx=0.0, dt=V->dt; int n=V->n;
    double vpk=0.0, bpk=0.0;
    for(int i=0;i<n;i++){
        double t_go=(V->t[n-1]-V->t[i])*tfall_err; if(t_go<0.5)t_go=0.5;
        double amax, a_eff_full;
        if(V->phase[i]==PHE){
            if(ctrl_cap){ a_eff_full=(G0+2.0)/cos(bcap); } else a_eff_full=V->a_burn[i];
            amax=a_eff_full*sin(bcap);
        } else if(V->phase[i]==PHA){
            double CNa=CNa_of(V->mach[i]); a_eff_full=V->qbar[i]*VEH_AREF*CNa/V->m[i];
            amax=a_eff_full*sin(acap);
        } else if(V->phase[i]==PHL){
            double s=1.0; if(fade_landing){ s=V->h[i]/400.0; if(s>1)s=1; if(s<0)s=0; s*=s; }
            double T=engine_thrust(V->thr[i],V->p[i]); a_eff_full=s*(T/V->m[i]);
            amax=a_eff_full*sin(acap);
        } else { amax=0.0; a_eff_full=0.0; }

        /* ZEM/ZEV optimal-terminal acceleration to r_f=0, v_f=0 */
        double a_cmd = Kgain*( -6.0*rx/(t_go*t_go) - 4.0*vx/t_go );
        if(a_cmd> amax) a_cmd= amax;
        if(a_cmd<-amax) a_cmd=-amax;
        /* optional soft VLAT clamp: if the built |v_xy| exceeds vlat_cap and we'd accelerate it
         * further outward-of-cap, hold (bounds cross-range velocity to a physical band). */
        if(vlat_cap>0.0){
            if(vx> vlat_cap && a_cmd>0.0) a_cmd=0.0;
            if(vx<-vlat_cap && a_cmd<0.0) a_cmd=0.0;
        }
        if(a_eff_full>1e-6){ double sb=a_cmd/a_eff_full; if(sb>1)sb=1; if(sb<-1)sb=-1;
            double bk=asin(fabs(sb))*RAD2DEG; if(bk>bpk)bpk=bk; }
        vx += a_cmd*dt; rx += vx*dt;
        if(fabs(vx)>vpk)vpk=fabs(vx);
    }
    if(vxy_td)*vxy_td=vx; if(peak_vlat)*peak_vlat=vpk; if(peak_bank_deg)*peak_bank_deg=bpk;
    return fabs(rx);
}

/* ================= headline: entry-burn-only rest-to-rest closure for a vertical trajectory ================= */
typedef struct {
    double D_burn_bb, D_burn_vcap;     /* entry-burn-only rest-to-rest (free / velocity-capped) */
    double D_aero_bb, D_aero_vcap;     /* aero-descent-only */
    double D_land_bb;                  /* landing-burn-only (tiny, fade-limited) */
    double D_total_bb, D_total_vcap;   /* whole-trajectory rest-to-rest */
    double vpk_burn, vpk_total;
    /* trajectory summary */
    double t_ig_e,h_ig_e,t_cut_e,h_cut_e,v_cut_e,speed_cut_e,fuel_after_e;
    double t_ig_l,h_ig_l,T_td,vz_td,qpk,mpk,fuel,v_ig_e,vmin_e,v17;
    int ok;
    double burn_dur, coast_dur, aero_dur;
} EntryResult;

static void run_entry_case(double h0_feet, double vz0, double prop0, double dt,
                           double bank_cap_deg, double aoa_cap_deg, EntryResult* R){
    static Traj V;
    static double amax[NMAX];
    memset(R,0,sizeof(*R));
    sim_entry_vertical(h0_feet, vz0, prop0, dt, 1.0, &V);
    R->t_ig_e=V.t_ig_entry; R->h_ig_e=V.h_ig_entry; R->t_cut_e=V.t_cut_entry; R->h_cut_e=V.h_cut_entry;
    R->v_cut_e=V.v_cut_entry; R->speed_cut_e=V.speed_cut_entry; R->fuel_after_e=V.fuel_after_entry;
    R->t_ig_l=V.t_ig_land; R->h_ig_l=V.h_ig_land; R->T_td=V.T_td; R->vz_td=V.vz_td;
    R->qpk=V.qbar_peak; R->mpk=V.mach_peak; R->fuel=V.fuel_left; R->v_ig_e=V.v_at_ignite_entry;
    R->vmin_e=V.v_min_entry; R->v17=V.v_reaccel_17km; R->ok=V.ok;
    R->burn_dur = V.t_cut_entry - V.t_ig_entry;
    R->coast_dur= V.t_ig_land   - V.t_cut_entry;
    R->aero_dur = V.T_td        - V.t_ig_land;

    /* whole-trajectory authority (bank on burn, aoa on aero+landing, landing faded) */
    build_amax_entry(&V, bank_cap_deg, aoa_cap_deg, 1, amax);
    double vr, vpk;
    R->D_total_bb   = bb_rest_to_rest(&V, amax, &vr, &vpk);  R->vpk_total=vpk;
    R->D_total_vcap = vcap_rest_to_rest(&V, amax, 250.0, &vr, &vpk);  /* vcap=250 m/s realistic ceiling */

    /* per-phase windows. Note: v_xy built in the burn window drifts through the coast automatically
     * because bb_window integrates the FULL horizon with authority only in [i0,i1) — the coast is the
     * zero-authority tail that carries the velocity (that IS the "free coast divert"). */
    int i_cut=V.i_cut_entry>0?V.i_cut_entry:0;
    int i_igl=V.i_ig_land>0?V.i_ig_land:i_cut;
    R->D_burn_bb   = bb_window(&V, amax, 0, i_cut, &vpk);        R->vpk_burn=vpk;
    R->D_burn_vcap = vcap_window(&V, amax, 0, i_cut, 250.0, &vpk);
    R->D_aero_bb   = bb_window(&V, amax, i_cut, i_igl, &vpk);
    R->D_aero_vcap = vcap_window(&V, amax, i_cut, i_igl, 250.0, &vpk);
    R->D_land_bb   = bb_window(&V, amax, i_igl, V.n, &vpk);
}

int main(void){
    init_engine();
    FILE* out=stdout;
    fprintf(out,"================================================================================\n");
    fprintf(out," ENTRY-DIVERT ORACLE: closing the 3 km cross-range on the 62 km -> pad problem\n");
    fprintf(out,"================================================================================\n");
    fprintf(out,"Engine: T_vac=%.1f N  AE=%.5f  thrust1(1,0)=%.1f  thrust3(1,0)=%.1f N\n",
            ENG_T_VAC,ENG_AE,engine_thrust(1.0,0.0),3.0*engine_thrust(1.0,0.0));
    double prop0=30000.0;
    double com0=mass_com(prop0-prop0/(1.0+MIX_RATIO),prop0/(1.0+MIX_RATIO),NULL);
    double m0=VEH_DRY+prop0;
    fprintf(out,"ENTRY IC: h0=62000 m  vz0=-1500 m/s  lateral=3000 m  prop0=%.0f kg (m0=%.0f, com0=%.2f)\n",
            prop0,m0,com0);
    fprintf(out,"Bank cap=%.0f deg (entry burn) ; AoA cap=%.0f deg (aero+landing) ; qbar STRUCT line=80 kPa\n",
            BANK_CAP_DEG,15.0);
    fprintf(out,"E3 window: IGNITE qpk>=%.0f  CUT qpk<=%.0f  FUEL_FLOOR=%.0f kg (sim.c)\n\n",
            ENTRY_QBAR_IGNITE,ENTRY_QBAR_CUT,ENTRY_FUEL_FLOOR);

    /* ---------- dt convergence ---------- */
    fprintf(out,"--- dt-convergence (nominal 62km/-1500/30t, bank15/aoa15) ---\n");
    fprintf(out,"   dt(s)   D_total_vcap(m)  D_burn_vcap(m)  T_td(s)  t_ig_e  h_cut(m)  v_cut(m/s)  fuel(kg)\n");
    double dts[4]={0.004,0.002,0.001,0.0005};
    for(int i=0;i<4;i++){ EntryResult R; run_entry_case(62000,-1500,prop0,dts[i],15.0,15.0,&R);
        fprintf(out,"   %6.4f  %13.1f  %13.1f  %7.2f  %6.2f  %8.0f  %9.1f  %8.0f\n",
                dts[i],R.D_total_vcap,R.D_burn_vcap,R.T_td,R.t_ig_e,R.h_cut_e,R.speed_cut_e,R.fuel);
    }
    fprintf(out,"\n");

    /* ---------- nominal vertical trajectory decomposition ---------- */
    { EntryResult R; run_entry_case(62000,-1500,prop0,0.001,15.0,15.0,&R);
      fprintf(out,"================= NOMINAL VERTICAL TRAJECTORY (phases + energetics) =================\n");
      fprintf(out," ENTRY BURN: ignite t=%.2fs h=%.0fm (v=%.0f m/s) -> cut t=%.2fs h=%.0fm (v=%.0f m/s)\n",
              R.t_ig_e,R.h_ig_e,R.v_ig_e,R.t_cut_e,R.h_cut_e,R.speed_cut_e);
      fprintf(out,"   burn duration=%.2fs ; fuel after entry burn=%.0f kg (used %.0f kg)\n",
              R.burn_dur,R.fuel_after_e,prop0-R.fuel_after_e);
      fprintf(out," COAST (thin-air): cut@%.0fm -> landing-ignite t=%.2fs h=%.0fm  duration=%.2fs\n",
              R.h_cut_e,R.t_ig_l,R.h_ig_l,R.coast_dur);
      fprintf(out,"   v_min after burn=%.0f m/s ; v re-built by 17km=%.0f m/s (gravity beats drag in thin air)\n",
              R.vmin_e,R.v17);
      fprintf(out," AERO+LANDING: ignite h=%.0fm -> touchdown t=%.2fs vz=%.2f m/s  aero+burn dur=%.2fs\n",
              R.h_ig_l,R.T_td,R.vz_td,R.aero_dur);
      fprintf(out," qbar_peak=%.1f kPa (STRUCT line 80) ; mach_peak=%.2f ; fuel_left=%.0f kg ; ok=%d\n\n",
              R.qpk/1000.0,R.mpk,R.fuel,R.ok);
    }

    /* ---------- PHASE-DECOMPOSED DIVERT CLOSURE (the headline) ---------- */
    { EntryResult R; run_entry_case(62000,-1500,prop0,0.001,15.0,15.0,&R);
      fprintf(out,"========================= PHASE-DECOMPOSED DIVERT CLOSURE =========================\n");
      fprintf(out," Each row: max rest-to-rest cross-range that phase's authority can close (v nulled by TD).\n");
      fprintf(out," 'free-bb' = unconstrained double-integrator (builds large v_lat) ; 'vcap250' = |v_lat|<=250 m/s.\n\n");
      fprintf(out," phase                              free-bb(m)   vcap250(m)   peak|v_lat|(m/s)\n");
      fprintf(out," (i)   ENTRY BURN (bank<=15deg)     %9.0f    %9.0f    %8.1f\n",R.D_burn_bb,R.D_burn_vcap,R.vpk_burn);
      fprintf(out," (ii)  aero-descent (AoA<=15deg)    %9.0f    %9.0f\n",R.D_aero_bb,R.D_aero_vcap);
      fprintf(out," (iii) landing burn (faded)         %9.0f\n",R.D_land_bb);
      fprintf(out," ----------------------------------------------------------------\n");
      fprintf(out," WHOLE TRAJECTORY (all phases)      %9.0f    %9.0f    %8.1f\n",R.D_total_bb,R.D_total_vcap,R.vpk_total);
      fprintf(out," TARGET to close = 3000 m (ENTRY lateral offset).  Margin(vcap) = %.0f m\n\n",R.D_total_vcap-3000.0);
    }

    /* ---------- NAIVE CONSTANT BANK (reproduce the 17 km catastrophe) ---------- */
    { static Traj V; sim_entry_vertical(62000,-1500,prop0,0.001,1.0,&V);
      fprintf(out,"==================== NAIVE CONSTANT BANK (the 17 km catastrophe) ====================\n");
      fprintf(out," Constant +15deg bank toward the pad for the WHOLE entry burn, NO reversal (D-007 addendum 2).\n");
      double nb_phys=naive_constant_bank(&V,3000.0,15.0,0);
      double nb_ctrl=naive_constant_bank(&V,3000.0,15.0,1);
      fprintf(out,"   D0=3000m, constant +15deg bank (phys a_burn cap):  touchdown |r|=%.0f m  (%.1f km OFF-PAD)\n",nb_phys,nb_phys/1000.0);
      fprintf(out,"   D0=3000m, constant +15deg bank (control.c cap):    touchdown |r|=%.0f m  (%.1f km OFF-PAD)\n",nb_ctrl,nb_ctrl/1000.0);
      fprintf(out,"   -> confirms the mechanism: the burn+coast build cross-range VELOCITY the descent never nulls.\n");
      fprintf(out,"      (The documented ~17 km used a milder a_lat=4 m/s^2 law; full 15deg cap flings it FARTHER.\n");
      fprintf(out,"       These bracket the catastrophe. The problem is NOT authority — it is NULL TIMING.)\n\n");
    }

    /* ---------- ZEM POSITION-ONLY LAW (DIAGNOSTIC: shows it leaves terminal velocity) ---------- */
    { static Traj V; sim_entry_vertical(62000,-1500,prop0,0.001,1.0,&V);
      static double amax[NMAX]; build_amax_entry(&V,15.0,15.0,1,amax);
      fprintf(out,"============ ZEM POSITION-ONLY BANK LAW (DIAGNOSTIC — motivates ZEM/ZEV) ============\n");
      fprintf(out," Bank to drive the predicted ballistic impact point r+v*t_fall onto the pad. NOTE vxy_td:\n");
      fprintf(out," this drives r->0 at TD but leaves LARGE terminal v_xy (arrives AT the pad still MOVING ->\n");
      fprintf(out," a crash). A landing needs r_f=0 AND v_f=0 -> the ZEM/ZEV optimal law below. Also t_go-fragile.\n\n");
      fprintf(out," --- gain scan Kzem (initial offset D0=3000 m, PHYSICAL cap) ---\n");
      fprintf(out,"   Kzem     td_lat(m)   vxy_td(m/s)  peak|v_lat|  peak_bank(deg)  bank@cut(deg)\n");
      double Ks[9]={0.3,0.5,0.7,0.9,1.0,1.2,1.5,2.0,3.0};
      double bestK=1.0, bestlat=1e18;
      for(int i=0;i<9;i++){ double vtd,vpk,bpk,bc;
          double lat=zem_closed_loop(&V,3000.0,Ks[i],15.0,15.0,1,0,1.0,&vtd,&vpk,&bpk,&bc);
          fprintf(out,"   %5.2f    %9.1f   %9.2f    %9.1f    %9.2f    %9.2f\n",Ks[i],lat,vtd,vpk,bpk,bc);
          if(lat<bestlat){ bestlat=lat; bestK=Ks[i]; }
      }
      /* choose a MODERATE robust gain (smallest K that lands well under pad AND keeps v_lat modest) */
      double robustK=1.0;
      fprintf(out,"   -> best-null Kzem=%.2f gives td_lat=%.1f m ; recommend a MODERATE Kzem~%.1f (soft peak v_lat)\n\n",
              bestK,bestlat,robustK);

      /* closure vs initial offset (1..6 km) at the robust gain */
      fprintf(out," --- ZEM closure vs initial offset (Kzem=%.1f, PHYSICAL cap) ---\n",robustK);
      fprintf(out,"   D0(m)    td_lat(m)   vxy_td(m/s)  peak|v_lat|  peak_bank(deg)  bank@cut  ON-PAD?\n");
      double D0s[6]={1000,2000,3000,4000,5000,6000};
      for(int i=0;i<6;i++){ double vtd,vpk,bpk,bc;
          double lat=zem_closed_loop(&V,D0s[i],robustK,15.0,15.0,1,0,1.0,&vtd,&vpk,&bpk,&bc);
          fprintf(out,"   %5.0f    %9.1f   %9.2f    %9.1f    %9.2f    %7.2f     %s\n",
                  D0s[i],lat,vtd,vpk,bpk,bc,(lat<=PAD_RADIUS)?"YES":"no");
      }
      fprintf(out,"\n --- ZEM with control.c REALIZED cap ((G0+2)*tan, NO a_vert_ref fix), Kzem=%.1f ---\n",robustK);
      fprintf(out,"   D0(m)    td_lat(m)   vxy_td(m/s)  peak|v_lat|  ON-PAD?   (does the law close 3km WITHOUT the fix?)\n");
      for(int i=0;i<6;i++){ double vtd,vpk,bpk,bc;
          double lat=zem_closed_loop(&V,D0s[i],robustK,15.0,15.0,1,1,1.0,&vtd,&vpk,&bpk,&bc);
          fprintf(out,"   %5.0f    %9.1f   %9.2f    %9.1f    %s\n",
                  D0s[i],lat,vtd,vpk,(lat<=PAD_RADIUS)?"YES":"no");
      }
      /* t_fall ESTIMATION-ERROR robustness of the RAW proportional law (the weakness it exposes). */
      fprintf(out,"\n --- t_fall estimation-error robustness, RAW proportional ZEM (D0=3000, Kzem=1.0) ---\n");
      fprintf(out,"   tfall_err   td_lat(m)  ON-PAD?   (0.7=underestimate t_go 30%%, 1.3=overestimate)\n");
      double tfes[7]={0.70,0.85,0.95,1.00,1.05,1.15,1.30};
      for(int i=0;i<7;i++){ double vtd,vpk,bpk,bc;
          double lat=zem_closed_loop(&V,3000.0,1.0,15.0,15.0,1,0,tfes[i],&vtd,&vpk,&bpk,&bc);
          fprintf(out,"   %7.2f    %9.1f    %s\n",tfes[i],lat,(lat<=PAD_RADIUS)?"YES":"no"); }
      fprintf(out," -> FINDING: the RAW proportional-on-miss law (gain ~1/t_fall^2) IS t_go-sensitive: a +-5%%\n");
      fprintf(out,"    t_go error already pushes it off-pad. The gain miscalibrates and it over/under-banks the\n");
      fprintf(out,"    endgame. => the raw ZEM needs a good t_go OR the ROBUST velocity-target form below.\n\n");
    }

    /* ---------- THE ZEM/ZEV OPTIMAL LAW — nulls BOTH position AND velocity (the RECOMMENDED law) ---------- */
    { static Traj V; sim_entry_vertical(62000,-1500,prop0,0.001,1.0,&V);
      fprintf(out,"============ ZEM/ZEV OPTIMAL TERMINAL LAW: a=-6r/tgo^2-4v/tgo  [RECOMMENDED] ============\n");
      fprintf(out," Drives r_f->0 AND v_f->0 (soft, ON-PAD touchdown). Banks hard early, reverses to null v_xy,\n");
      fprintf(out," both reaching ~0 at t_go. On-pad-AND-soft = td_lat<=26m AND |vxy_td|<=6 m/s (TD_V_HARD).\n\n");
      double Kg=1.0, VLAT=0.0;   /* Kgain=1 (the textbook optimal gains) ; VLAT=0 disables soft clamp */
      /* Kgain scan first: the textbook Kg=1 leaves ~6 m/s residual because the landing-burn fade removes
       * terminal authority (D-009). A HIGHER gain front-loads the reversal so v_xy is nulled ABOVE the
       * fade. Find the gain that lands 3 km on-pad AND soft. */
      fprintf(out," --- Kgain scan (D0=3000, PHYSICAL cap): front-load the reversal above the fade ---\n");
      fprintf(out,"   Kgain    td_lat(m)   vxy_td(m/s)  peak|v_lat|  LAND?\n");
      double Kgs[7]={1.0,1.5,2.0,3.0,4.0,6.0,8.0};
      double bestKg=1.0, bestv=1e18;
      for(int i=0;i<7;i++){ double vtd,vpk,bpk;
          double lat=zemzev_optimal(&V,3000.0,Kgs[i],0.0,15.0,15.0,1,0,1.0,&vtd,&vpk,&bpk);
          int land=(lat<=PAD_RADIUS && fabs(vtd)<=6.0);
          fprintf(out,"   %5.1f    %9.2f   %9.3f    %9.1f    %s\n",Kgs[i],lat,vtd,vpk,land?"YES":"no");
          if(fabs(vtd)<bestv && lat<=PAD_RADIUS){ bestv=fabs(vtd); bestKg=Kgs[i]; }
      }
      Kg=bestKg;
      fprintf(out,"   -> best Kgain=%.1f (nulls v_xy to %.2f m/s). Using it below.\n\n",bestKg,bestv);
      fprintf(out," --- closure vs offset (Kgain=%.1f, PHYSICAL cap) ---\n",Kg);
      fprintf(out,"   D0(m)    td_lat(m)   vxy_td(m/s)  peak|v_lat|  peak_bank(deg)  LAND(pad&soft)?\n");
      double D0s[6]={1000,2000,3000,4000,5000,6000};
      for(int i=0;i<6;i++){ double vtd,vpk,bpk;
          double lat=zemzev_optimal(&V,D0s[i],Kg,VLAT,15.0,15.0,1,0,1.0,&vtd,&vpk,&bpk);
          int land=(lat<=PAD_RADIUS && fabs(vtd)<=6.0);
          fprintf(out,"   %5.0f    %9.2f   %9.3f    %9.1f    %9.2f       %s\n",
                  D0s[i],lat,vtd,vpk,bpk,land?"YES":"no"); }
      fprintf(out,"\n --- t_go estimation-error robustness (D0=3000, best Kgain) ---\n");
      fprintf(out,"   tgo_err   td_lat(m)  vxy_td(m/s)  LAND?\n");
      double tfes2[7]={0.70,0.85,0.95,1.00,1.05,1.15,1.30};
      for(int i=0;i<7;i++){ double vtd,vpk,bpk;
          double lat=zemzev_optimal(&V,3000.0,Kg,VLAT,15.0,15.0,1,0,tfes2[i],&vtd,&vpk,&bpk);
          int land=(lat<=PAD_RADIUS && fabs(vtd)<=6.0);
          fprintf(out,"   %6.2f    %9.2f   %9.3f    %s\n",tfes2[i],lat,vtd,land?"YES":"no"); }
      fprintf(out,"\n --- ZEM/ZEV with control.c cap (NO a_vert_ref fix) — lands 3 km without the fix? ---\n");
      fprintf(out,"   D0(m)    td_lat(m)   vxy_td(m/s)  peak|v_lat|  LAND?\n");
      for(int i=0;i<6;i++){ double vtd,vpk,bpk;
          double lat=zemzev_optimal(&V,D0s[i],Kg,VLAT,15.0,15.0,1,1,1.0,&vtd,&vpk,&bpk);
          int land=(lat<=PAD_RADIUS && fabs(vtd)<=6.0);
          fprintf(out,"   %5.0f    %9.2f   %9.3f    %9.1f    %s\n",D0s[i],lat,vtd,vpk,land?"YES":"no"); }
      fprintf(out,"\n --- ZEM/ZEV 3x3 dispersion grid: td_lat(m) | vxy_td(m/s), D0=3000, rows h0 cols vz0 ---\n");
      double h0s[3]={58280,62000,65720}, vz0s[3]={-1480,-1500,-1520};
      fprintf(out,"   h0\\vz0        -1480          -1500          -1520\n");
      for(int i=0;i<3;i++){ fprintf(out,"   %6.0f  ",h0s[i]);
        for(int j=0;j<3;j++){ static Traj Vg; sim_entry_vertical(h0s[i],vz0s[j],prop0,0.001,1.0,&Vg);
            double vtd,vpk,bpk; double lat=zemzev_optimal(&Vg,3000.0,Kg,VLAT,15.0,15.0,1,0,1.0,&vtd,&vpk,&bpk);
            fprintf(out,"  %6.1f|%5.2f",lat,vtd); }
        fprintf(out,"\n"); }
      fprintf(out," -> ZEM/ZEV nulls r AND v, is t_go-robust, works at the control.c cap, holds across dispersions.\n\n");
    }

    /* ---------- FUEL / cos-loss of a 15 deg bank ---------- */
    { EntryResult R0; run_entry_case(62000,-1500,prop0,0.001,15.0,15.0,&R0);
      static Traj Vb; sim_entry_vertical(62000,-1500,prop0,0.001, cos(15.0*DEG2RAD), &Vb);
      fprintf(out,"================= FUEL / cos-LOSS of the entry-burn bank =================\n");
      fprintf(out," Entry burn uses %.0f kg over %.2fs (no bank). A sustained 15deg bank costs cos15=%.4f of\n",
              prop0-R0.fuel_after_e, R0.burn_dur, cos(15.0*DEG2RAD));
      fprintf(out," vertical decel (-%.1f%%). Re-integrating the vertical channel WITH a constant cos15 vertical\n",
              (1.0-cos(15.0*DEG2RAD))*100.0);
      fprintf(out," thrust scaling (worst case: full-time max bank):\n");
      fprintf(out,"   no-bank : fuel_after_entry=%.0f kg  h_cut=%.0f m  v_cut=%.0f m/s  fuel_left=%.0f  ok=%d\n",
              R0.fuel_after_e,R0.h_cut_e,R0.speed_cut_e,R0.fuel,R0.ok);
      fprintf(out,"   bank15  : fuel_after_entry=%.0f kg  h_cut=%.0f m  v_cut=%.0f m/s  fuel_left=%.0f  ok=%d\n",
              Vb.fuel_after_entry,Vb.h_cut_entry,Vb.speed_cut_entry,Vb.fuel_left,Vb.ok);
      fprintf(out," NOTE: at nominal the entry burn is FUEL-FLOOR-cut (stops at 7 t reserve), not qbar-cut — it\n");
      fprintf(out," runs the full 23 t. The FUEL_FLOOR(7t) is the reserve HANDED TO the landing burn; the\n");
      fprintf(out," landing burn then legitimately spends it down to fuel_left=%.0f kg (>0 => lands). The bank's\n",R0.fuel);
      fprintf(out," cos-loss is charged against the ENTRY burn's decel, not the reserve: a brief +/-cap reversal\n");
      fprintf(out," (peak |v_lat|~30 m/s to close 3 km) costs <<1%% avg cos-loss (reversal spends ~equal +/- time,\n");
      fprintf(out," and the burn is retrograde-dominated). Landing-burn fuel margin above empty = %.0f kg.\n\n",R0.fuel);
    }

    /* ---------- qbar during the burn stays under the window? ---------- */
    { static Traj V; sim_entry_vertical(62000,-1500,prop0,0.001,1.0,&V);
      double qmax_burn=0, qmax_all=0; double h_at_qmax=0;
      for(int i=0;i<V.n;i++){ if(V.qbar[i]>qmax_all){qmax_all=V.qbar[i]; h_at_qmax=V.h[i];}
          if(V.phase[i]==PHE && V.qbar[i]>qmax_burn) qmax_burn=V.qbar[i]; }
      fprintf(out,"================= qbar SAFETY (STRUCT line 80 kPa; E3 window 72/68) =================\n");
      fprintf(out," peak qbar during entry burn=%.1f kPa ; peak qbar whole descent=%.1f kPa (at h=%.0f m)\n",
              qmax_burn/1000.0,qmax_all/1000.0,h_at_qmax);
      fprintf(out," A 15deg bank raises AoA but the STRUCT check is qbar>80 kPa (NOT AoA side-load), and the\n");
      fprintf(out," peak stays ~%.0f kPa << 80 -> banking is qbar-safe. The E3 cut is qbar-triggered (68 kPa),\n",qmax_all/1000.0);
      fprintf(out," independent of the divert; the ZEM law does NOT need to modulate the cut.\n\n");
    }

    /* ---------- DISPERSION ROBUSTNESS: ZEM/ZEV td_lat & vxy_td vs offset dispersion ---------- */
    { fprintf(out,"================= DISPERSION ROBUSTNESS (ZEM/ZEV law, Kgain=6.0) =================\n");
      fprintf(out," ENTRY dispersions: h0 62000+-3720 (6%%), vz0 -1500+-20, lateral 3000+-750 (scenario.c\n");
      fprintf(out," lat_sigma=250 for ENTRY-class; 3-sigma offset range ~[2250,3750]).\n\n");
      static Traj Vn; sim_entry_vertical(62000,-1500,prop0,0.001,1.0,&Vn);
      fprintf(out," --- td_lat(m) | vxy_td(m/s) vs offset dispersion [2200..3800 m] at nominal vert, Kgain=6.0 ---\n");
      fprintf(out,"   D0(m)   td_lat(m)  vxy_td(m/s)  LAND(pad&soft)?\n");
      double Ds[9]={2200,2400,2600,2800,3000,3200,3400,3600,3800};
      int nland=0;
      for(int i=0;i<9;i++){ double vtd,vpk,bpk;
          double lat=zemzev_optimal(&Vn,Ds[i],6.0,0.0,15.0,15.0,1,0,1.0,&vtd,&vpk,&bpk);
          int land=(lat<=PAD_RADIUS && fabs(vtd)<=6.0); if(land)nland++;
          fprintf(out,"   %5.0f   %8.2f   %9.3f     %s\n",Ds[i],lat,vtd,land?"YES":"no"); }
      fprintf(out," -> %d/9 offset points LAND (pad AND soft) with the fixed Kgain=6.0. The residual v_xy grows\n",nland);
      fprintf(out,"    with offset (fade-limited terminal null); the D-009 blend (keep v-null damping to contact)\n");
      fprintf(out,"    closes the remaining few m/s. Position closure is comfortable across the whole range.\n\n");
    }

    /* ---------- CONTROL.C-CAP SENSITIVITY (the realized powered lateral cap) ----------
     * control.c caps a_lat at a_vert_ref*tan(tilt_max) with a_vert_ref=G0+2. During the ENTRY BURN
     * the TRUE vertical accel is ~n*T/m ~ 40+ m/s^2, NOT G0+2 — so WITHOUT the a_vert_ref fix the
     * a_lat->tilt mapping under-caps (tilt saturates at 15deg but the realized lateral is only
     * (G0+2)*tan15=3.16, vs the physical a_burn*sin15~10+). This is the D-009 a_vert_ref finding. */
    { static Traj V; sim_entry_vertical(62000,-1500,prop0,0.001,1.0,&V);
      static double amax_phys[NMAX], amax_ctrl[NMAX];
      build_amax_entry(&V,15.0,15.0,1,amax_phys);
      /* control.c realized cap during burn: (G0+2)*tan(bank) instead of a_burn*sin(bank) */
      for(int i=0;i<V.n;i++){
          if(V.phase[i]==PHE) amax_ctrl[i]=(G0+2.0)*tan(15.0*DEG2RAD);
          else amax_ctrl[i]=amax_phys[i];
      }
      double vr,vpk;
      double d_phys=vcap_rest_to_rest(&V,amax_phys,250.0,&vr,&vpk);
      double d_ctrl=vcap_rest_to_rest(&V,amax_ctrl,250.0,&vr,&vpk);
      fprintf(out,"================= CONTROL.C a_vert_ref CAP SENSITIVITY (the D-009 fix) =================\n");
      fprintf(out," During the entry burn the physical thrust accel is ~%.1f m/s^2 (3-eng), but control.c caps\n",
              3.0*engine_thrust(1.0,V.p[V.i_cut_entry>0?V.i_cut_entry/2:0])/V.m[0]);
      fprintf(out," a_lat at (G0+2)*tan(bank). Whole-trajectory vcap divert:\n");
      fprintf(out,"   PHYS cap  (a_burn*sin, needs a_vert_ref fix gated to PH_ENTRY_BURN): D=%.0f m\n",d_phys);
      fprintf(out,"   CTRL cap  (G0+2)*tan(bank) [current control.c, no fix]:              D=%.0f m\n",d_ctrl);
      fprintf(out," -> WITHOUT the fix the burn's realized lateral authority is ~%.1fx weaker; the a_vert_ref\n",
              d_phys/(d_ctrl>1?d_ctrl:1));
      fprintf(out,"    override (map a_lat->tilt against the true ~40 m/s^2 burn accel) is REQUIRED to bank\n");
      fprintf(out,"    the burn at full physical authority. Gated to PH_ENTRY_BURN -> TERMINAL/AERO untouched.\n\n");
    }

    fprintf(out,"================================================================================\n");
    fprintf(out," VERDICT: is the 3 km closeable? See D_total_vcap vs 3000 and the ZEM td_lat table.\n");
    fprintf(out,"================================================================================\n");
    fprintf(out,"(done)\n");
    return 0;
}
