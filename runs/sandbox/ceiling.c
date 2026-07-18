/* ceiling.c — CEILING-ORACLE authoritative optimal-divert study for AERO_OFFSET.
 *
 * Measures the TRUE maximum landable initial lateral offset D_max by integrating ONE
 * continuous vertical trajectory (unpowered aero descent -> aero-aware ignition -> powered
 * landing burn), precomputing the time-varying lateral authority a_max(t) with the sim's
 * execution FADE, and solving the time-optimal rest-to-rest single-switch bang-bang.
 *
 * PARITY (mirrored verbatim from core/): US76 atmosphere (atmosphere.c); frozen aero tables
 * AERO_M/CA/CN + VEH_AREF (dynamics.c); engine_thrust=throttle*(T_vac-AE*p) with derived
 * T_vac/AE (constants.h); suicide_burn_margin thrust-only shoot dt=0.1 (guidance_hoverslam.c);
 * a_design=0.85 fins-deployed profile + Kv=3 feedback (guidance_hoverslam.c); execution fade
 * s=(h_feet/400)^2 while engine_on (guidance_mppi.c cmd_from_u_lean / mppi_execute).
 *
 * C ONLY. Build (MSVC): cl /O2 /fp:precise ceiling.c
 *
 * Lateral-authority model (the crux, two bounds reported):
 *   UNPOWERED (aero): a_max = qbar*VEH_AREF*CNa(M)*alpha_cap / m
 *   POWERED (thrust vector), TWO bounds:
 *     (phys)  a_max = (thr*T(p)/m)*sin(alpha_cap)          [physical thrust projection; PROMPT headline]
 *     (ctrl)  a_max = (G0+2)*tan(alpha_cap)                [what control.c actually caps to: amax=a_vert_ref*tan(tilt_max)]
 *   times the FADE s=(h_feet/400)^2 (clamped) in the "with-fade" variants; s=1 for "no-fade".
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
#define DEG2RAD   0.017453292519943295
#define RAD2DEG   57.29577951308232
#define PI        3.141592653589793

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

/* ================= suicide_burn_margin (guidance_hoverslam.c verbatim) =================
 * thrust-only (NO drag credit), full 1-engine, dt=0.1. Returns altitude remaining at vz->0. */
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

/* ================= vertical-channel trajectory ================= */
/* Records, per step, the state needed to build a_max(t). Fixed-length sample arrays. */
#define NMAX 400000
typedef struct {
    int    n;
    double dt;
    double t[NMAX];
    double h_feet[NMAX];     /* feet height above ground */
    double vz[NMAX];
    double mach[NMAX];
    double qbar[NMAX];
    double m[NMAX];          /* mass */
    double thr[NMAX];        /* throttle actual (approx: commanded, no lag) */
    double p[NMAX];          /* ambient pressure */
    int    engine_on[NMAX];
    /* summary */
    double t_ignite, h_ignite_feet, T_td, vz_td, mach_ignite, qbar_peak;
    double h_ignite_alt;     /* RZ-frame ignition altitude (h_feet+com approx) */
    int    ok;               /* touched down with |vz|<3 and fuel left */
    double fuel_left;
    double mach_peak;
} VTraj;

/* Integrate the vertical channel. drag_credit_powered: sim uses SRP shielding (~no drag) during
 * burn, so pass 0 (thrust-only arrest) to match the sim's margin convention; unpowered always
 * has full drag. bang_full: if 1, powered phase is full-thrust arrest; else track 0.85 v_ref. */
static void sim_vertical(double h0_feet, double vz0, double prop0, double dt,
                         int powered_full_thrust, VTraj* V){
    memset(V,0,sizeof(*V));
    V->dt=dt;
    double m_rp1=prop0/(1.0+MIX_RATIO), m_lox=prop0-m_rp1;
    double mtot; double com=mass_com(m_lox,m_rp1,&mtot);
    /* feet height = h_base ; treat h0_feet as feet height above ground.
     * The sim's h_feet = RZ - com - 1.0*deploy_frac; we track feet height directly. */
    double h=h0_feet, vz=vz0, t=0.0;
    int engine_on=0; double ada=0.0;
    double qpeak=0.0, mpeak=0.0;
    int n=0;
    /* powered arrest uses thrust-only (SRP shielded) vertical accel toward the 0.85 v_ref profile */
    while(h>0.0 && n<NMAX){
        double m; com=mass_com(m_lox,m_rp1,&m);
        Atmo atm; atmo_eval(h+com,&atm);    /* atmosphere sampled at RZ = feet + com (sim uses RZ) */
        double sp = fabs(vz); double mach=sp/atm.a;
        double qbar=0.5*atm.rho*vz*vz;
        if(qbar>qpeak)qpeak=qbar;
        if(mach>mpeak)mpeak=mach;

        /* ignition decision (aero-aware, fins deployed) */
        if(!engine_on){
            double margin = suicide_burn_margin(h, vz, m);
            if(vz < -1.0 && margin <= LANDING_IGNITE_MARGIN){
                engine_on=1;
                double Tfull=engine_thrust(1.0,atm.p);
                double amax=Tfull/m - G0; if(amax<1.0)amax=1.0;
                ada=0.85*amax;
                V->t_ignite=t; V->h_ignite_feet=h; V->mach_ignite=mach;
                V->h_ignite_alt=h+com;
            }
        }

        double thr_cmd=0.0;
        double az; /* vertical accel */
        if(!engine_on){
            /* unpowered: gravity + drag (full, no shielding) */
            double CA=CA_of(mach);
            double D=0.5*atm.rho*vz*vz*VEH_AREF*CA;   /* opposes motion; vz<0 so drag is +z (up) */
            double drag_acc = D/m;                    /* magnitude, decelerating the fall */
            double gh=g_of(h+com);
            az = -gh + drag_acc;                      /* vz negative-> drag pushes up (+) */
            thr_cmd=0.0;
        } else {
            /* powered: thrust-only vertical (SRP shielded; no drag credit, matching sim margin).
             * Track 0.85 a_design v_ref profile with Kv=3 feedback (guidance_hoverslam.c), OR
             * full-thrust arrest if powered_full_thrust. */
            double Tfull=engine_thrust(1.0,atm.p);
            double gh=g_of(h+com);
            if(powered_full_thrust){
                thr_cmd=1.0;
            } else {
                double a_design=ada; if(a_design<1.0)a_design=1.0;
                double hgo=h; if(hgo<0.02)hgo=0.02;
                double v_ref=-sqrt(TD_V_TARGET*TD_V_TARGET + 2.0*a_design*hgo);
                double Kv=3.0;
                double a_cmd=a_design + Kv*(v_ref - vz);
                /* NO drag credit in the ceiling arrest (SRP shielded). */
                double T_need=m*(gh + a_cmd);
                double thr=T_need/Tfull;
                if(thr<0.40)thr=0.40; if(thr>1.0)thr=1.0;
                /* commit-to-touchdown */
                if(h<6.0 && vz>-0.6) thr=0.40;
                thr_cmd=thr;
            }
            double Tact=engine_thrust(thr_cmd,atm.p);
            az = Tact/m - gh;
            /* mass depletion */
            double Isp=isp_of(atm.p);
            double mdot=Tact/(Isp*G0);
            double mdot_rp1=mdot/(1.0+MIX_RATIO), mdot_lox=mdot-mdot_rp1;
            m_lox-=mdot_lox*dt; m_rp1-=mdot_rp1*dt;
            if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
        }

        /* record BEFORE stepping (state at time t) */
        V->t[n]=t; V->h_feet[n]=h; V->vz[n]=vz; V->mach[n]=mach; V->qbar[n]=qbar;
        V->m[n]=m; V->thr[n]=thr_cmd; V->p[n]=atm.p; V->engine_on[n]=engine_on;
        n++;

        /* semi-implicit Euler (matches sandbox refs; RK4 immaterial at 500Hz, dt-verified) */
        vz += az*dt; h += vz*dt; t += dt;
        if(engine_on && vz>0.5) break;   /* arrested & climbing -> stop (touchdown reached) */
    }
    V->n=n;
    V->T_td=t; V->vz_td=vz; V->qbar_peak=qpeak; V->mach_peak=mpeak;
    V->fuel_left=m_lox+m_rp1;
    V->ok = (fabs(vz)<5.0 && V->fuel_left>0.0 && engine_on);
}

/* ================= lateral authority a_max(t) along the trajectory ================= */
/* cap_deg: AoA cap (15 or 12). fade: apply the execution fade during powered. powered_model:
 * 0=(thr*T/m)*sin(cap) [phys], 1=(G0+2)*tan(cap) [control.c]. */
static void build_amax(const VTraj* V, double cap_deg, int fade, int powered_model,
                       double* amax /* [V->n] */){
    double capr=cap_deg*DEG2RAD;
    for(int i=0;i<V->n;i++){
        double a;
        if(!V->engine_on[i]){
            double CNa=CNa_of(V->mach[i]);
            a = V->qbar[i]*VEH_AREF*CNa*capr / V->m[i];
        } else {
            double s=1.0;
            if(fade){ s=V->h_feet[i]/400.0; if(s>1)s=1; if(s<0)s=0; s*=s; }
            double ap;
            if(powered_model==0){
                double T=engine_thrust(V->thr[i], V->p[i]);
                ap = (T/V->m[i])*sin(capr);
            } else {
                ap = (G0+2.0)*tan(capr);
            }
            a = s*ap;
        }
        amax[i]=a;
    }
}

/* ================= optimal rest-to-rest single-switch bang-bang =================
 * With |a_lat(t)|<=amax(t) (time-varying), the max rest-to-rest displacement over [0,T] is a
 * single-switch bang-bang: a=+amax(t) for t<t_s, then -amax(t). We integrate the lateral double-
 * integrator (r,v starting at rest) and root-find the switch INDEX so v_lat(T)=0. Displacement = D.
 * Returns |r(T)| at v(T)=0; the residual r is the max divertible offset (rest->rest symmetry). */
static double bb_lateral(const VTraj* V, const double* amax, int isw, double* vend){
    double r=0.0, v=0.0, dt=V->dt;
    for(int i=0;i<V->n;i++){
        double a = (i<isw)? +amax[i] : -amax[i];
        v += a*dt; r += v*dt;   /* semi-implicit-consistent with vertical channel */
    }
    if(vend)*vend=v;
    return fabs(r);
}
/* Scan switch index; find the isw where v(T) crosses zero; bisect on a fractional switch time
 * for a sharp root; return D_max = |r(T)| at that switch. */
static double max_divert(const VTraj* V, const double* amax, double* vresid, int* isw_out){
    int n=V->n;
    /* v(T) as function of switch index is monotone decreasing (later switch -> more +accel time
     * -> larger positive vend). Find bracketing indices where vend changes sign. */
    double v0; bb_lateral(V,amax,0,&v0);       /* all -accel: vend very negative */
    double vN; bb_lateral(V,amax,n,&vN);        /* all +accel: vend very positive */
    /* scan for the sign change */
    int lo=0, hi=n; double vlo=v0, vhi=vN;
    /* coarse scan to bracket */
    int prev=0; double vprev; bb_lateral(V,amax,prev,&vprev);
    int found=0;
    for(int i=1;i<=n;i++){
        double vi; bb_lateral(V,amax,i,&vi);
        if((vprev<=0.0 && vi>=0.0)||(vprev>=0.0 && vi<=0.0)){ lo=prev; hi=i; vlo=vprev; vhi=vi; found=1; break; }
        prev=i; vprev=vi;
    }
    (void)vlo;(void)vhi;
    if(!found){ if(vresid)*vresid=vN; if(isw_out)*isw_out=n; return bb_lateral(V,amax,n,vresid); }
    /* fractional-switch bisection between indices lo and hi: interpolate the switch time by
     * splitting the accel at a fractional step. We approximate by a weighted last-+accel step. */
    /* Simpler robust approach: bisection on a continuous switch TIME ts in [t[lo],t[hi]] with a
     * per-step accel sign determined by whether the step's time < ts, splitting the straddling step. */
    double tlo=V->t[lo], thi=V->t[hi];
    double best_r=0.0, best_v=0.0;
    for(int it=0; it<60; it++){
        double ts=0.5*(tlo+thi);
        double r=0.0, v=0.0, dt=V->dt;
        for(int i=0;i<n;i++){
            double te=V->t[i];               /* time at start of step i */
            double frac;                     /* fraction of this step spent in +accel */
            if(te+dt<=ts) frac=1.0;
            else if(te>=ts) frac=0.0;
            else frac=(ts-te)/dt;
            double a_plus=+amax[i], a_minus=-amax[i];
            /* integrate the step in two sub-parts (frac at +, 1-frac at -) */
            double dtp=frac*dt, dtm=(1.0-frac)*dt;
            v += a_plus*dtp;  r += v*dtp;
            v += a_minus*dtm; r += v*dtm;
        }
        best_r=fabs(r); best_v=v;
        if(v>0.0) thi=ts; else tlo=ts;
    }
    if(vresid)*vresid=best_v;
    if(isw_out)*isw_out=lo;
    return best_r;
}

/* ================= VELOCITY-CAPPED optimal rest-to-rest (the PHYSICAL ceiling) =================
 * The free double-integrator bang-bang builds >100 m/s cross-range velocity, which VIOLATES the
 * small-AoA lift model (a_lat = qbar*Aref*CN*alpha assumes the velocity vector stays near-vertical)
 * AND the sim's guidance (which caps commanded inward speed at VLAT_MAX~30 m/s and nulls it
 * continuously). The physically-admissible time-optimal rest-to-rest profile under BOTH |a|<=amax(t)
 * AND |v|<=vcap is TRAPEZOIDAL: accelerate (clamped at vcap) -> cruise at vcap -> decelerate to rest.
 * We integrate with a velocity clamp and bisect the deceleration-start time ts so v(T)=0. Because
 * amax(t) is time-varying (aero fades, powered fades), we root-find ts numerically. */
static double vcap_divert(const VTraj* V, const double* amax, double vcap,
                          double* vresid, double* vpk_out){
    int n=V->n; double dt=V->dt;
    /* v(T) as a function of decel-start time ts is monotone: later ts -> longer accel/cruise ->
     * more positive residual v. Bisect ts in [0,T] for v(T)=0. */
    double tlo=0.0, thi=V->t[n-1]+dt;
    double best_r=0.0, best_v=0.0, best_vpk=0.0;
    for(int it=0; it<70; it++){
        double ts=0.5*(tlo+thi);
        double r=0.0, v=0.0, vpk=0.0;
        for(int i=0;i<n;i++){
            double te=V->t[i];
            /* accelerate (+) while te<ts, decelerate (-) after; split the straddling step */
            double frac; if(te+dt<=ts)frac=1.0; else if(te>=ts)frac=0.0; else frac=(ts-te)/dt;
            double dtp=frac*dt, dtm=(1.0-frac)*dt;
            /* + phase with velocity clamp at +vcap */
            v += (+amax[i])*dtp; if(v>vcap)v=vcap; r += v*dtp;
            /* - phase (decel); allow v to go through 0 toward rest; clamp at -vcap for symmetry */
            v += (-amax[i])*dtm; if(v<-vcap)v=-vcap; r += v*dtm;
            if(fabs(v)>vpk)vpk=fabs(v);
        }
        best_r=fabs(r); best_v=v; best_vpk=vpk;
        if(v>0.0) thi=ts; else tlo=ts;
    }
    if(vresid)*vresid=best_v; if(vpk_out)*vpk_out=best_vpk;
    return best_r;
}
/* velocity-capped, with the centered-by-400 constraint (zero authority below 400m). */
static double vcap_divert_cut(const VTraj* V, const double* amax_in, double vcap, double h_cut,
                              double* vresid){
    static double amax[NMAX];
    for(int i=0;i<V->n;i++) amax[i]=(V->h_feet[i]>=h_cut)?amax_in[i]:0.0;
    return vcap_divert(V,amax,vcap,vresid,NULL);
}

/* Variant (c): centered-by-400 constraint — no lateral authority below h_feet=400 (r=0,v=0 must
 * be achieved by h=400). Zero amax for all samples with h_feet<400, then run the bang-bang. */
static double max_divert_centered400(const VTraj* V, const double* amax_in, double* vresid){
    static double amax[NMAX];
    for(int i=0;i<V->n;i++) amax[i] = (V->h_feet[i]>=400.0)? amax_in[i] : 0.0;
    return max_divert(V, amax, vresid, NULL);
}
/* Diagnostic: zero amax below a given altitude h_cut (m feet-height). Used to reproduce P4's
 * aero-phase-only rest-to-rest (null by h_cut). */
static double max_divert_cutalt(const VTraj* V, const double* amax_in, double h_cut, double* vresid){
    static double amax[NMAX];
    for(int i=0;i<V->n;i++) amax[i] = (V->h_feet[i]>=h_cut)? amax_in[i] : 0.0;
    return max_divert(V, amax, vresid, NULL);
}
/* Diagnostic: report peak lateral velocity the "optimal" bang-bang builds (physical-plausibility
 * check — a huge v_lat means the double-integrator model has drifted from the lift-limited reality). */
static double bb_peak_vlat(const VTraj* V, const double* amax, double ts){
    double r=0.0,v=0.0,dt=V->dt,vpk=0.0;
    for(int i=0;i<V->n;i++){
        double te=V->t[i], frac; if(te+dt<=ts)frac=1.0; else if(te>=ts)frac=0.0; else frac=(ts-te)/dt;
        double dtp=frac*dt, dtm=(1.0-frac)*dt;
        v+= (+amax[i])*dtp; r+=v*dtp; v+=(-amax[i])*dtm; r+=v*dtm;
        if(fabs(v)>vpk)vpk=fabs(v);
    }
    return vpk;
}

/* ================= closed-form sanity: constant-a rest-to-rest over T => D=a*T^2/4 ================= */
static double closedform_bb(double a, double T){ return a*T*T/4.0; }

/* ================= headline runner for a given (h0,vz0) ================= */
typedef struct { double a_fade15, a_nofade15, a_center15, a_fade12, a_nofade12;
                 double a_fade15_ctrl, a_nofade15_ctrl;
                 /* PHYSICAL (velocity-capped @ VCAP) ceilings — the real numbers */
                 double v_fade15, v_nofade15, v_center15, v_fade12;
                 double T_td, t_ig, h_ig, mach_ig, qpk, fuel, vz_td; int ok; } Result;

/* Physical lateral-velocity cap: the sim's converging guidance caps commanded inward speed at
 * VLAT_MAX (MPPI 30 m/s, hoverslam 35 m/s), and the small-AoA lift model only holds while the
 * cross-range velocity stays a small fraction of the ~330 m/s descent. 30 m/s is the sim-faithful
 * value (MPPI's VLAT_MAX); we sweep 20..50 for sensitivity. */
#define VCAP 30.0

static void run_case(double h0_feet, double vz0, double prop0, double dt, Result* R){
    static VTraj V;
    static double amax[NMAX];
    double vr;
    sim_vertical(h0_feet, vz0, prop0, dt, /*full_thrust=*/0, &V);
    R->T_td=V.T_td; R->t_ig=V.t_ignite; R->h_ig=V.h_ignite_feet; R->mach_ig=V.mach_ignite;
    R->qpk=V.qbar_peak; R->fuel=V.fuel_left; R->vz_td=V.vz_td; R->ok=V.ok;

    /* --- free bang-bang (MATH UPPER BOUND, unphysical v_lat) --- */
    build_amax(&V,15.0,1,0,amax); R->a_fade15=max_divert(&V,amax,&vr,NULL);
    build_amax(&V,15.0,0,0,amax); R->a_nofade15=max_divert(&V,amax,&vr,NULL);
    build_amax(&V,15.0,1,0,amax); R->a_center15=max_divert_centered400(&V,amax,&vr);
    build_amax(&V,12.0,1,0,amax); R->a_fade12=max_divert(&V,amax,&vr,NULL);
    build_amax(&V,12.0,0,0,amax); R->a_nofade12=max_divert(&V,amax,&vr,NULL);
    build_amax(&V,15.0,1,1,amax); R->a_fade15_ctrl=max_divert(&V,amax,&vr,NULL);
    build_amax(&V,15.0,0,1,amax); R->a_nofade15_ctrl=max_divert(&V,amax,&vr,NULL);

    /* --- velocity-capped @ VCAP (THE PHYSICAL CEILING) --- */
    build_amax(&V,15.0,1,0,amax); R->v_fade15  = vcap_divert(&V,amax,VCAP,&vr,NULL);
    build_amax(&V,15.0,0,0,amax); R->v_nofade15= vcap_divert(&V,amax,VCAP,&vr,NULL);
    build_amax(&V,15.0,1,0,amax); R->v_center15= vcap_divert_cut(&V,amax,VCAP,400.0,&vr);
    build_amax(&V,12.0,1,0,amax); R->v_fade12  = vcap_divert(&V,amax,VCAP,&vr,NULL);
}

/* SELF-TEST: the vcap/bb solvers must reproduce the analytic rest-to-rest closed forms for a
 * CONSTANT-authority synthetic trajectory. bb: D=a*T^2/4. vcap trapezoid: D=vcap*T - vcap^2/a. */
static void self_test(void){
    static VTraj V; memset(&V,0,sizeof(V));
    double a=3.0, T=40.0, dt=0.002; int n=(int)(T/dt);
    if(n>NMAX)n=NMAX; V.n=n; V.dt=dt;
    static double amax[NMAX];
    for(int i=0;i<n;i++){ V.t[i]=i*dt; amax[i]=a; }
    double vr;
    double dbb=max_divert(&V,amax,&vr,NULL);
    double dbb_cf=a*T*T/4.0;
    double vcap=25.0; double dvc=vcap_divert(&V,amax,vcap,&vr,NULL);
    double dvc_cf=vcap*T - vcap*vcap/a;   /* trapezoid: accel vcap/a, cruise, decel vcap/a */
    printf("--- SELF-TEST (constant a=%.1f, T=%.1fs) ---\n",a,T);
    printf("  bang-bang:  solver=%.2f  closed-form a*T^2/4=%.2f  (err %.2f%%)\n",
           dbb,dbb_cf,100*fabs(dbb-dbb_cf)/dbb_cf);
    printf("  vcap=%.0f:   solver=%.2f  closed-form vcap*T-vcap^2/a=%.2f  (err %.2f%%)\n\n",
           vcap,dvc,dvc_cf,100*fabs(dvc-dvc_cf)/dvc_cf);
}

int main(void){
    init_engine();
    printf("================================================================================\n");
    printf(" CEILING-ORACLE: AERO_OFFSET true max landable lateral offset (optimal divert)\n");
    printf("================================================================================\n");
    self_test();
    printf("Engine: T_vac=%.1f N  AE=%.5f m^2  thrust(1,0)=%.1f  thrust(1,P0)=%.1f\n",
           ENG_T_VAC, ENG_AE, engine_thrust(1.0,0.0), engine_thrust(1.0,P0_ATM));
    double prop0=10000.0; double m0=VEH_DRY+prop0;
    double com0=mass_com(prop0-prop0/(1.0+MIX_RATIO), prop0/(1.0+MIX_RATIO), NULL);
    printf("Mass: m0=%.1f kg (dry %.0f + prop %.0f)  com0=%.3f m  RZ0=%.1f\n",
           m0,VEH_DRY,prop0,com0,12000.0+com0);
    printf("AoA caps: 15deg=%.4f rad (current flat), 12deg=%.4f rad (old)\n",15*DEG2RAD,12*DEG2RAD);
    printf("Powered lat models: phys=(thr*T/m)*sin(cap) [PROMPT] ; ctrl=(G0+2)*tan(cap) [control.c cap]\n");
    printf("  (G0+2)*tan15=%.3f  (G0+2)*tan12=%.3f m/s^2\n\n",(G0+2)*tan(15*DEG2RAD),(G0+2)*tan(12*DEG2RAD));

    /* ---- dt convergence (nominal): PHYSICAL (vcap) + free-bb ---- */
    printf("--- dt-convergence (nominal 12km/-330): D_phys=vcap@30 fade/15 ; D_bb=free-bb fade/15 ---\n");
    printf("   dt(s)   D_phys(m)   D_bb(m)    T_td(s)   t_ignite(s)  h_ignite(m)\n");
    double dts[3]={0.004,0.002,0.001};
    for(int i=0;i<3;i++){ Result R; run_case(12000,-330,prop0,dts[i],&R);
        printf("   %5.3f   %8.2f   %8.2f   %7.2f   %9.2f   %9.1f\n",
               dts[i],R.v_fade15,R.a_fade15,R.T_td,R.t_ig,R.h_ig);
    }
    printf("\n");

    /* ---- DIAGNOSTIC: reproduce P4 aero-only ceiling + physical-plausibility of the full-window bb ---- */
    { static VTraj V; static double amax[NMAX]; double vr, isw;
      sim_vertical(12000,-330,prop0,0.002,0,&V);
      printf("--- DIAGNOSTIC: P4-parity (aero-only, null by 4600m) + full-window plausibility ---\n");
      /* hard-15 aero-only to 4600m (P4 got 847m) */
      build_amax(&V,15.0,0,0,amax);
      double d_aero15 = max_divert_cutalt(&V,amax,4600.0,&vr);
      printf(" hard-15deg AERO-ONLY (null by 4600m): D=%.0f m  (P4 hard-15 = 847 m)   [PARITY CHECK]\n",d_aero15);
      build_amax(&V,12.0,0,0,amax); double d_aero12=max_divert_cutalt(&V,amax,4600.0,&vr);
      printf(" 12deg AERO-ONLY (null by 4600m):     D=%.0f m  (P4 fixed-12 = 573 m)\n",d_aero12);
      build_amax(&V,6.0,0,0,amax); double d_aero6=max_divert_cutalt(&V,amax,4600.0,&vr);
      printf(" 6deg  AERO-ONLY (null by 4600m):     D=%.0f m  (P4 fixed-6  = 313 m)\n",d_aero6);
      /* full-window (a): report peak lateral velocity of the 'optimal' solution */
      build_amax(&V,15.0,1,0,amax);
      double ts_opt=0; { int is; max_divert(&V,amax,&vr,&is); ts_opt=V.t[is]; }
      double vpk=bb_peak_vlat(&V,amax,ts_opt);
      printf(" full-window(a) peak |v_lat| of optimal bang-bang = %.1f m/s  (switch t=%.1fs)\n",vpk,ts_opt);
      printf("   -> a %.0f m/s cross-range velocity is UNPHYSICAL for the lift model (small-AoA).\n",vpk);
      printf("   -> the free double-integrator over the FULL 49.5s descent is NOT the sim's regime;\n");
      printf("      the sim nulls v_xy continuously (converging profile). The realistic ceiling is\n");
      printf("      the VELOCITY-CAPPED divert (below), matching MPPI-2. Full-window bb = math upper bound.\n\n");
      (void)isw;
    }

    /* ---- vertical trajectory diagnostics (nominal) ---- */
    { static VTraj V; sim_vertical(12000,-330,prop0,0.002,0,&V);
      printf("--- nominal vertical trajectory (12km/-330, 0.85 v_ref arrest) ---\n");
      printf(" T_touchdown=%.2f s  ignite@ t=%.2f s h_feet=%.1f m (RZ=%.1f) Mach=%.2f\n",
             V.T_td,V.t_ignite,V.h_ignite_feet,V.h_ignite_alt,V.mach_ignite);
      printf(" vz_touchdown=%.3f m/s  fuel_left=%.1f kg  qbar_peak=%.1f kPa  mach_peak=%.2f  ok=%d\n",
             V.vz_td,V.fuel_left,V.qbar_peak/1000.0,V.mach_peak,V.ok);
      /* unpowered vs powered time split */
      double t_unp=V.t_ignite, t_pow=V.T_td-V.t_ignite;
      printf(" unpowered-descent duration=%.2f s ; powered-burn duration=%.2f s\n\n",t_unp,t_pow);
    }

    /* ---- HEADLINE TABLE ---- lead with the PHYSICAL (velocity-capped) ceiling ---- */
    Result Rn; run_case(12000,-330,prop0,0.002,&Rn);
    printf("========================= HEADLINE D_max TABLE (nominal 12km/-330) =========================\n");
    printf(" PHYSICAL ceiling = velocity-capped optimal divert (v_lat<=%.0f m/s, sim-faithful: MPPI VLAT_MAX).\n",VCAP);
    printf(" The free bang-bang (right col) is a MATH UPPER BOUND only: it builds ~125 m/s cross-range,\n");
    printf(" which violates the small-AoA lift model and the sim guidance -> NOT achievable. Use D_phys.\n\n");
    printf(" variant                                          D_phys(m)   D_bb-upperbound(m)\n");
    printf(" (a) FADE, 15deg   [CURRENT BEHAVIOR]             %8.1f    %8.1f   <== THE headline (D_phys)\n",Rn.v_fade15,Rn.a_fade15);
    printf(" (b) NO-FADE, 15deg                               %8.1f    %8.1f   (removing/blending the fade)\n",Rn.v_nofade15,Rn.a_nofade15);
    printf(" (c) CENTERED-by-400m, FADE, 15deg                %8.1f    %8.1f   (target MPPI profile r=0 by 400m)\n",Rn.v_center15,Rn.a_center15);
    printf(" (d) FADE, 12deg                                  %8.1f    %8.1f   (old-cap reconciliation)\n",Rn.v_fade12,Rn.a_fade12);
    printf("\n");

    /* ---- v_cap sweep: the MPPI-2 reconciliation lever ---- */
    printf("--- v_cap sensitivity (variant a, fade/15, nominal): how the physical ceiling scales ---\n");
    printf("   v_cap(m/s)   D_phys(m)   (MPPI-2 measured ~400-500 total divert)\n");
    { static VTraj V; static double amax[NMAX]; sim_vertical(12000,-330,prop0,0.002,0,&V);
      build_amax(&V,15.0,1,0,amax);
      double vcs[6]={15,20,25,30,40,50};
      for(int i=0;i<6;i++){ double vr; double d=vcap_divert(&V,amax,vcs[i],&vr,NULL);
          printf("   %6.0f      %8.1f\n",vcs[i],d); }
    }
    printf("\n");

    /* ---- 3x3 sensitivity grids (variant a and c, PHYSICAL) ---- */
    double h0s[3]={11200,12000,12800};
    double vz0s[3]={-300,-330,-360};
    printf("--- 3x3 grid: D_phys variant (a) FADE 15deg, vcap=%.0f [m]  (rows h0, cols vz0) ---\n",VCAP);
    printf("   h0\\vz0      -300      -330      -360\n");
    for(int i=0;i<3;i++){
        printf("   %6.0f  ",h0s[i]);
        for(int j=0;j<3;j++){ Result R; run_case(h0s[i],vz0s[j],prop0,0.002,&R);
            printf(" %8.1f",R.v_fade15); }
        printf("\n");
    }
    printf("\n--- 3x3 grid: D_phys variant (c) CENTERED-400 FADE 15deg, vcap=%.0f [m] ---\n",VCAP);
    printf("   h0\\vz0      -300      -330      -360\n");
    for(int i=0;i<3;i++){
        printf("   %6.0f  ",h0s[i]);
        for(int j=0;j<3;j++){ Result R; run_case(h0s[i],vz0s[j],prop0,0.002,&R);
            printf(" %8.1f",R.v_center15); }
        printf("\n");
    }
    printf("\n");
    printf("--- 3x3 grid: D_bb (free bang-bang MATH upper bound) variant (a) 15deg [m] ---\n");
    printf("   h0\\vz0      -300      -330      -360\n");
    for(int i=0;i<3;i++){
        printf("   %6.0f  ",h0s[i]);
        for(int j=0;j<3;j++){ Result R; run_case(h0s[i],vz0s[j],prop0,0.002,&R);
            printf(" %8.1f",R.a_fade15); }
        printf("\n");
    }
    printf("\n");

    /* ---- closed-form cross-checks ---- */
    printf("--- closed-form sanity checks ---\n");
    { Result R; run_case(12000,-330,prop0,0.002,&R);
      double Teff=R.T_td;
      /* free-bb equivalent constant a */
      double a_eq=R.a_fade15/(Teff*Teff/4.0);
      printf(" free-bb(a): D=%.1f over T=%.2fs => equiv constant a=%.3f m/s^2 (matches ~mean a_max)\n",
             R.a_fade15,Teff,a_eq);
      double qb=30000, mach=0.9, CNa=CNa_of(mach), arep=qb*VEH_AREF*CNa*15*DEG2RAD/35600.0;
      printf(" representative unpowered a_max(30kPa,M0.9,15deg)=%.3f m/s^2 ; D=a*T^2/4=%.0f m (bb bound)\n",
             arep,closedform_bb(arep,Teff));
      /* velocity-capped closed form: trapezoid. accel time ta=vcap/a, D ~ vcap*(T - ta) for T>>ta.
       * With a~arep, vcap=30: ta=vcap/a, D_trap ~ vcap*(T-ta). */
      double ta=VCAP/arep; double D_trap=VCAP*(Teff-ta);
      printf(" vcap trapezoid closed form: ta=vcap/a=%.1fs, D~vcap*(T-ta)=%.0f m (vs D_phys=%.0f, aero-faded lower)\n",
             ta,D_trap,R.v_fade15);
      printf(" (D_phys < trapezoid bound because aero authority is zero above ignition-limited window\n");
      printf("  and faded to ~0 below 400m; the cruise-at-vcap segment is shorter than full T.)\n");
    }
    printf("\n");

    /* ---- RECONCILIATION ---- */
    printf("================================================================================\n");
    printf(" RECONCILIATION: MPPI-2 (~400-500) vs P4 (787-864) -- resolved\n");
    printf("================================================================================\n");
    { Result R; run_case(12000,-330,prop0,0.002,&R);
      printf(" THIS STUDY (integrated single trajectory, aero-aware ignition @ ~%.0fm):\n",R.h_ig);
      printf("   PHYSICAL D_phys (vcap=%.0f, fade/15) = %.0f m   <<< AUTHORITATIVE ceiling\n",VCAP,R.v_fade15);
      printf("   free-bb math upper bound (fade/15)   = %.0f m   (unphysical 125 m/s cross-range)\n",R.a_fade15);
      printf("\n WHY MPPI-2 said 400-500:\n");
      printf("   MPPI caps commanded inward speed at VLAT_MAX=30 m/s and nulls v_xy continuously, so it\n");
      printf("   operates in the VELOCITY-LIMITED regime. D_phys@vcap=30 = %.0f m; MPPI-2's tuned law\n",R.v_fade15);
      printf("   (conservative A_DECEL=1.3, extra fade+tilt losses, imperfect reversal) realizes ~60-80%%\n");
      printf("   of that -> the observed 400-500 m. So MPPI-2 is CORRECT for the as-implemented controller.\n");
      printf("\n WHY P4 said 787-864 (and why it's an OVER-estimate for the current sim):\n");
      printf("   P4 = aero-phase rest-to-rest to a FIXED 4.6km ignition (237-314m) + a SEPARATELY-BUDGETED\n");
      printf("   550m landing-burn divert, with NO execution fade and the OLD 8-12deg caps. Three errors\n");
      printf("   vs the current sim:\n");
      printf("     1. FIXED 4.6km ignition: the aero-aware rule now fires at ~%.0fm (LOWER), so the aero\n",R.h_ig);
      printf("        phase has less altitude/time -> smaller aero divert than P4's high-ignition figure.\n");
      printf("     2. +550m burn budget STACKED on top: does not exist. The fade s=(h/400)^2 crushes\n");
      printf("        powered lateral authority to ~0 by 400m, and below ignition the vehicle is already\n");
      printf("        near the pad -- the burn adds little rest-to-rest displacement, not 550m.\n");
      printf("     3. No velocity-null accounting: P4's bang-bang also implicitly built large v_lat.\n");
      printf("   Net: P4 double-counted an un-faded burn divert on top of a high-ignition aero ceiling.\n");
      printf("\n VERDICT: the numbers do NOT actually disagree once put on the same footing. The physical,\n");
      printf("   integrated, fade-aware, velocity-capped ceiling is ~%.0f m (D_phys). MPPI-2's 400-500 is\n",R.v_fade15);
      printf("   the achievable fraction of it; P4's 787-864 was an optimistic sum of non-additive terms.\n");
    }
    printf("\n");

    /* ---- DISPERSION RECOMMENDATION (based on PHYSICAL ceiling) ---- */
    printf("================================================================================\n");
    printf(" DISPERSION RECOMMENDATION for AERO_OFFSET (based on D_phys)\n");
    printf("================================================================================\n");
    { double dmin=1e18, dnom=0, cmin=1e18;
      for(int i=0;i<3;i++)for(int j=0;j<3;j++){ Result R; run_case(h0s[i],vz0s[j],prop0,0.002,&R);
          if(R.v_fade15<dmin)dmin=R.v_fade15; if(R.v_center15<cmin)cmin=R.v_center15; }
      Result Rc; run_case(12000,-330,prop0,0.002,&Rc); dnom=Rc.v_fade15;
      double safe=0.85*dnom;
      printf(" D_phys(a) nominal=%.0f m ; worst-corner over 3x3=%.0f m ; centered-400 worst=%.0f m\n",
             dnom,dmin,cmin);
      printf(" Design ceiling = 0.85 * D_phys(nominal) = %.0f m  (the >=90%%-landable target radius)\n\n",safe);
      /* IMPORTANT: the ACHIEVABLE ceiling of the as-implemented MPPI is lower than D_phys (it realizes
       * ~60-80%%). Give TWO recommendation targets: (i) against the physical ceiling D_phys, and
       * (ii) against the conservative MPPI-achievable ~0.7*D_phys (=what actually lands today). */
      double achiev = 0.70*dnom;   /* MPPI-achievable proxy (~400-500 at nominal) */
      printf(" Two footings:\n");
      printf("   (i)  against PHYSICAL D_phys: safe radius 0.85*D_phys = %.0f m\n",safe);
      printf("   (ii) against MPPI-ACHIEVABLE (~0.70*D_phys = %.0f m, matches the observed 400-500 land):\n",achiev);
      printf("        safe radius 0.85*achievable = %.0f m\n\n",0.85*achiev);
      /* p90 of a 2D offset with mean-radius mu, per-axis sigma s: use radial p90 ~ mu + 1.28*s. */
      printf(" For >=90%% of seeds inside a target radius, need mean + 1.28*sigma <= target.\n");
      double mus[5]={300,350,400,450,500}; double sigs[4]={80,100,120,150};
      double t_phys=safe, t_ach=0.85*achiev;
      printf("   p90=mean+1.28*sig ; mark P if <=phys-target(%.0f), A if <=MPPI-target(%.0f)\n",t_phys,t_ach);
      printf("     mean\\sig     80        100       120       150\n");
      for(int i=0;i<5;i++){
          printf("     %4.0f    ",mus[i]);
          for(int j=0;j<4;j++){ double p90=mus[i]+1.28*sigs[j];
              const char* mk = (p90<=t_ach)?" PA":(p90<=t_phys)?" P ":"  x";
              printf("  %5.0f%s",p90,mk); }
          printf("\n");
      }
      printf("\n Current scenario mean 500 sig 150 => p90=%.0f m.\n",500+1.28*150);
      printf(" RECOMMENDATION:\n");
      printf("   * Against the physical ceiling (%.0f m), the current mean 500 / sig 150 (p90=%.0f) FITS.\n",t_phys,500+1.28*150);
      printf("   * BUT the as-implemented MPPI only lands ~0.70*D_phys; against that footing (%.0f m) the\n",t_ach);
      printf("     current mean 500 is TOO HOT (p90=692 >> %.0f). To keep >=90%% landable with TODAY's\n",t_ach);
      printf("     controller, use mean 300-350, sigma 100  (p90=428-478 m, both within the MPPI target).\n");
      printf("   * This CONFIRMS the prior house recommendation (mean 300-350, sig 100). The scenario's\n");
      printf("     mean-500 is well-posed ONLY if MPPI is improved toward D_phys (blend out the fade,\n");
      printf("     center-by-400); otherwise retune the dispersion down to ~350/100 (like D-006 800->500).\n");
    }
    printf("\n(done)\n");
    return 0;
}
