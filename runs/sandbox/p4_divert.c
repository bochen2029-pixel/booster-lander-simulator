/* p4_divert.c  — Agent P4 aero-divert physical-ceiling calculator.
 *
 * Standalone C. Mirrors the sim's US76 atmosphere (core/atmosphere.c) and the
 * frozen body aero tables (core/dynamics.c: AERO_M/AERO_CA/AERO_CN, VEH_AREF).
 * Integrates a 2-D point mass in the (lateral x, vertical h) plane during the
 * UNPOWERED aero descent (fins deployed, no engine) from a start state down to
 * the landing-burn ignition altitude, holding the maximum side-load-limited AoA.
 *
 * Physics per step (matches dynamics.c force split):
 *   speed = |v|,  mach = speed/a,  qbar = 0.5*rho*speed^2
 *   Drag  = qbar*Aref*CA(mach)          , anti-parallel to v
 *   Lift  = qbar*Aref*CNa(mach)*alpha    , perpendicular to v, in-plane (body lift)
 *   g_h   = G0*(Re/(Re+h))^2             , down
 * alpha is commanded by the steering law and clamped to the AoA-cap schedule.
 *
 * Goal of the divert: reach x=0 (over pad) AND vx=0 (no lateral rate) at the
 * burn-ignition altitude. We compute (1) the max initial |x| divertible to
 * (x~0, vx~0) with a bang-bang optimal profile, and (2) what a realistic PD
 * tracker achieves, across entry conditions and AoA caps.
 *
 * Build (MSVC): cl /O2 /fp:precise p4_divert.c
 * Run:          p4_divert.exe
 *
 * NO Python (house rule). NO sim dependency (self-contained).
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ---- constants mirrored from core/constants.h ---- */
#define G0        9.80665
#define R_EARTH   6356766.0
#define R_AIR     287.053
#define GAMMA_AIR 1.4
#define P0_ATM    101325.0
#define VEH_AREF  10.52
#define DEG2RAD   0.017453292519943295
#define RAD2DEG   57.29577951308232

/* mass at AERO_OFFSET: dry 25600 + prop0 10000 = 35600 (prompt "~35700") */
#define M_AERO    35600.0

/* ---- US76 atmosphere: exact copy of core/atmosphere.c atmo_eval ---- */
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

/* ---- frozen aero tables: exact copy of core/dynamics.c ---- */
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

/* ---- AoA-cap schedules (radians), functions of qbar [Pa] ---- */
/* control.c §5.7 flown cap: 8deg at qbar<=10kPa ramp to 3deg at qbar>=42kPa */
static double cap_controlc(double qbar){
    double d=8.0-5.0*((qbar-10000.0)/20000.0);
    if(d<3.0)d=3.0; if(d>8.0)d=8.0; return d*DEG2RAD;
}
/* Agent A schedule: 12deg at qbar<10kPa ramp to 4deg at qbar>30kPa */
static double cap_agentA(double qbar){
    if(qbar<10000.0) return 12.0*DEG2RAD;
    if(qbar>30000.0) return 4.0*DEG2RAD;
    return (12.0+(4.0-12.0)*(qbar-10000.0)/20000.0)*DEG2RAD;
}
/* hard physical envelope: STRUCT_FAIL only if |a|>15deg AND qbar>30kPa (>2s).
 * Absolute ceiling: 15deg where qbar>30kPa, unrestricted-ish (cap 25 for stall) below. */
static double cap_hard15(double qbar){
    if(qbar>30000.0) return 15.0*DEG2RAD;
    return 20.0*DEG2RAD; /* below 30 kPa the side-load line is inactive; fin stall ~25 */
}
/* fixed cap variants for sensitivity */
static double g_fixedcap_deg=0.0;
static double cap_fixed(double qbar){ (void)qbar; return g_fixedcap_deg*DEG2RAD; }

typedef double(*capfn)(double);

/* integration timestep (default 500 Hz, matches sim DT); flipped for convergence check */
double g_dt=0.002;

/* ================= integrator =================
 * State: x (lateral offset from pad, m; steer toward 0), vx, h, vz.
 * dt = 0.002 s (matches sim DT). Semi-implicit Euler like the sandbox refs but
 * fine step; RK4 would change nothing material at 500 Hz — verified by dt sweep.
 * Steering: signed AoA command 'alpha_cmd' from a control law, clamped to +-cap.
 * Returns final x, vx, and diagnostics.
 */
typedef struct {
    double x,vx,h,vz,t;
    double qmax, aoamax_deg, mach_at_burn, sideload_fail_s;
    int    hit_ground; /* h reached burn alt normally =0; ran to t limit=2; below 0 =1 */
} Traj;

/* One descent. mode: 0=PD-null (track x->0,vx->0), 1=bang-bang open-loop with
 * given switch fraction 'u' of the ALTITUDE band (accelerate then decel), sign s0. */
static void integrate(double x0,double vx0,double h0,double vz0,double h_burn,
                      double m, capfn cap, int mode,double bb_switch_h,double bb_sign,
                      Traj* out)
{
    extern double g_dt;
    const double dt=g_dt;
    double x=x0,vx=vx0,h=h0,vz=vz0,t=0.0;
    double qmax=0,aoamax=0,slfail=0,mach_burn=0;
    int guard=0;
    while(h>h_burn && t<200.0){
        Atmo A; atmo_eval(h,&A);
        double sp=sqrt(vx*vx+vz*vz); if(sp<1e-3) sp=1e-3;
        double M=sp/A.a;
        double qbar=0.5*A.rho*sp*sp;
        if(qbar>qmax) qmax=qbar;
        double CNa=CNa_of(M), CA=CA_of(M);
        double capr=cap(qbar);

        /* commanded AoA (signed). alpha>0 pushes lift toward -x? We define sign so
         * positive alpha produces lateral accel in +x_steer = toward pad if x>0.
         * Lift direction: perpendicular to v, in-plane, chosen by control sign. */
        double alpha;
        if(mode==0){
            /* Realistic tracker mirroring the sim guidance (guidance_hoverslam.c):
             * command an INWARD lateral velocity proportional to offset (capped), then
             * track it -> first-order approach, no overshoot, and it naturally reverses
             * AoA to null vx as x->0. vdes = -Kpos*x (toward pad). a_dem = Kvel*(vdes-vx).
             * Kpos raised vs the sim's 0.20 because here AoA is the only actuator and we
             * want it to use the full side-load cap for large offsets. */
            double Kpos=0.35, vlat_max=90.0, Kvel=0.9;
            double vdes = -Kpos*x; if(vdes> vlat_max)vdes=vlat_max; if(vdes<-vlat_max)vdes=-vlat_max;
            double a_dem = Kvel*(vdes - vx);
            /* AoA needed for that lateral accel: a_lat = qbar*Aref*CNa*alpha/m */
            double denom=qbar*VEH_AREF*CNa;
            alpha = (denom>1.0)? a_dem*m/denom : 0.0;
        } else {
            /* bang-bang: full +cap toward pad until switch altitude, then full -cap
             * to arrest lateral velocity. bb_sign = initial steer direction (toward pad). */
            double dir = (h>bb_switch_h)? bb_sign : -bb_sign;
            alpha = dir*capr;
        }
        if(alpha> capr) alpha= capr;
        if(alpha<-capr) alpha=-capr;
        double aad=fabs(alpha)*RAD2DEG; if(aad>aoamax) aoamax=aad;
        if(qbar>30000.0 && aad>15.0) slfail+=dt;

        /* forces */
        double D=qbar*VEH_AREF*CA;          /* drag magnitude, anti-parallel v */
        double Lf=qbar*VEH_AREF*CNa*alpha;  /* signed lift magnitude, perp to v */
        /* unit velocity */
        double ux=vx/sp, uz=vz/sp;
        /* perpendicular to v in-plane (rotate v by +90deg): (-uz, ux).
         * Positive alpha -> lift along +perp. We want positive alpha (when steering
         * toward pad, x>0 -> a_dem<0 -> alpha<0) to push -x. Check: x>0,vz<0 (uz<0),
         * perp=(-uz,ux)=(+,~0) so +perp is +x. alpha<0 -> lift along -perp = -x. Good. */
        double px=-uz, pz=ux;
        double ax=(-D*ux + Lf*px)/m;
        double gz=G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));
        double az=(-D*uz + Lf*pz)/m - gz;

        vx+=ax*dt; vz+=az*dt; x+=vx*dt; h+=vz*dt; t+=dt;
        if(++guard>200000) break;
    }
    { Atmo A; atmo_eval(h,&A); double sp=sqrt(vx*vx+vz*vz); mach_burn=sp/A.a; }
    out->x=x; out->vx=vx; out->h=h; out->vz=vz; out->t=t;
    out->qmax=qmax; out->aoamax_deg=aoamax; out->sideload_fail_s=slfail;
    out->mach_at_burn=mach_burn;
    out->hit_ground = (h<=h_burn)?0:2;
}

/* ============ MAX REST-TO-REST DIVERT (bang-bang, direct) ============
 * Physical question: starting over a point (x=0, vx=0), how far sideways can the
 * vehicle translate and STILL arrive at the burn altitude with vx~=0? That net
 * lateral displacement IS the maximum divert (by symmetry it equals the largest
 * initial offset that can be nulled to rest over the pad).
 *
 * Time-optimal rest-to-rest under an altitude-varying accel limit is bang-bang:
 * hold +cap (accelerate), switch once at altitude h_sw, hold -cap (decelerate) to
 * arrive at vx~=0. We scan h_sw across [h_burn, h0]; each gives a (Dx, vx_final).
 * As h_sw drops from h0->h_burn the accelerate phase lengthens: Dx grows and
 * vx_final grows (less time/altitude left to null it). The MAX divert is the
 * profile with the largest |Dx| whose |vx_final| <= vx_tol. Dense scan, then
 * refine the crossing — no fragile sign-bisection, no trivial root.
 *
 * Note we start from REST at x=0 and let it fly out (bb_sign=+1: accelerate +x),
 * so Dx>0. Symmetric for -x.
 */
static double max_divert_bb_ex(double h0,double vz0,double h_burn,double m,capfn cap,
                               double vx_tol,Traj* best_out){
    const double bb_sign=+1.0;    /* accelerate toward +x from rest */
    double best_dx=0.0; Traj best; memset(&best,0,sizeof best);
    int have=0;
    /* dense scan of switch altitude (coarse then we refine near the tol crossing) */
    double dh=(h0-h_burn)/240.0; if(dh<1.0) dh=1.0;
    double prev_dx=0.0, prev_vx=0.0, prev_sw=h0; int first=1;
    for(double sw=h0; sw>=h_burn-0.5; sw-=dh){
        Traj tt; integrate(0.0,0.0,h0,vz0,h_burn,m,cap,1,sw,bb_sign,&tt);
        double dx=fabs(tt.x), vxf=fabs(tt.vx);
        if(vxf<=vx_tol && dx>best_dx){ best_dx=dx; best=tt; have=1; }
        /* refine: if we crossed the vx_tol boundary between prev and now, bisect sw */
        if(!first){
            int a_ok=(prev_vx<=vx_tol), b_ok=(vxf<=vx_tol);
            if(a_ok!=b_ok){
                double slo=prev_sw, shi=sw;
                for(int it=0; it<30; it++){
                    double sm=0.5*(slo+shi);
                    Traj tm; integrate(0.0,0.0,h0,vz0,h_burn,m,cap,1,sm,bb_sign,&tm);
                    if(fabs(tm.vx)<=vx_tol){ if(fabs(tm.x)>best_dx){best_dx=fabs(tm.x);best=tm;have=1;}
                        /* keep moving toward larger accel phase (lower sw) while still ok */
                        if(b_ok) shi=sm; else slo=sm; }
                    else { if(b_ok) slo=sm; else shi=sm; }
                }
            }
        }
        prev_dx=dx; prev_vx=vxf; prev_sw=sw; first=0;
    }
    if(best_out) *best_out=best;
    (void)prev_dx; (void)have;
    return best_dx;
}
static double max_divert_bb(double h0,double vz0,double h_burn,double m,capfn cap,
                            double x_tol,double vx_tol){
    (void)x_tol;
    return max_divert_bb_ex(h0,vz0,h_burn,m,cap,vx_tol,NULL);
}

/* Max divert (PD tracker, realistic closed loop): largest initial offset x0 the PD
 * steering law brings to (|x|<=x_tol, |vx|<=vx_tol) at burn. Monotone in x0 (bigger
 * offset -> bigger residual), so bisect. */
static double max_divert_pd(double h0,double vz0,double h_burn,double m,capfn cap,
                            double x_tol,double vx_tol){
    double lo=0.0, hi=6000.0;
    for(int it=0; it<34; it++){
        double mid=0.5*(lo+hi);
        Traj t; integrate(mid,0.0,h0,vz0,h_burn,m,cap,0,0,0,&t);
        int ok=(fabs(t.x)<=x_tol && fabs(t.vx)<=vx_tol);
        if(ok) lo=mid; else hi=mid;
    }
    return lo;
}
/* wrapper matching old bangbang_best signature for the PART-1 detail table:
 * for a given x0, report the null quality achievable (residual x, vx). We do it by
 * finding the switch altitude that best nulls vx for exactly this x0 (dense scan). */
static void bangbang_best(double x0,double h0,double vz0,double h_burn,double m,capfn cap,
                          double* res_x,double* res_vx,Traj* bt){
    double bb_sign=(x0>=0)?-1.0:1.0;   /* steer toward pad */
    double dh=(h0-h_burn)/240.0; if(dh<1.0)dh=1.0;
    Traj best; double best_absvx=1e18, best_x=x0; int have=0;
    for(double sw=h0; sw>=h_burn-0.5; sw-=dh){
        Traj tt; integrate(x0,0.0,h0,vz0,h_burn,m,cap,1,sw,bb_sign,&tt);
        if(fabs(tt.vx)<best_absvx){ best_absvx=fabs(tt.vx); best_x=tt.x; best=tt; have=1; }
    }
    /* refine around the best switch with a local bisection on |vx| */
    *res_x=best_x; *res_vx=best_absvx; if(bt&&have)*bt=best;
}

int main(void){
    printf("=== Agent P4: aero-divert physical ceiling (Booster Lander Sim) ===\n");
    printf("mass=%.0f kg  Aref=%.2f m^2  US76 atmo  CN/CA from frozen tables\n\n",M_AERO,VEH_AREF);

    double h_burn=4600.0;   /* landing-burn ignition altitude (App narrative 4.6 km) */
    double x_tol =50.0;     /* residual offset the landing-burn gimbal divert can still absorb
                               (landing burn adds ~550 m; leave margin: any <=~500 lands, but
                               we quote the AERO-phase's own null quality at 50 m) */
    double vx_tol=8.0;      /* residual lateral velocity acceptable at burn handoff [m/s] */

    /* ---------- PART 1: baseline AERO_OFFSET (12 km, -330 m/s) ---------- */
    printf("--- PART 1: baseline AERO_OFFSET  h0=12000 vz0=-330  m=%.0f ---\n",M_AERO);
    printf("AoA-cap schedule = control.c flown (8deg@<=10kPa -> 3deg@>=42kPa)\n\n");

    /* MAX rest-to-rest divert profile diagnostics for the two candidate caps */
    printf("[max rest-to-rest divert: fly out at max side-load AoA, reverse to null vx by %.1f km]\n",h_burn/1000);
    printf(" cap-schedule          maxDivert(m)  qbar_pk(kPa)  AoAmax(deg)  M@burn  vx@burn  SLfail(s)\n");
    { Traj bt;
      double d=max_divert_bb_ex(12000,-330,h_burn,M_AERO,cap_controlc,vx_tol,&bt);
      printf(" control.c(8->3deg)    %8.0f      %7.1f     %7.1f    %5.2f   %6.2f    %5.2f\n",
             d,bt.qmax/1000,bt.aoamax_deg,bt.mach_at_burn,bt.vx,bt.sideload_fail_s);
      d=max_divert_bb_ex(12000,-330,h_burn,M_AERO,cap_agentA,vx_tol,&bt);
      printf(" AgentA   (12->4deg)   %8.0f      %7.1f     %7.1f    %5.2f   %6.2f    %5.2f\n",
             d,bt.qmax/1000,bt.aoamax_deg,bt.mach_at_burn,bt.vx,bt.sideload_fail_s);
      d=max_divert_bb_ex(12000,-330,h_burn,M_AERO,cap_hard15,vx_tol,&bt);
      printf(" hard STRUCT(<=15deg)  %8.0f      %7.1f     %7.1f    %5.2f   %6.2f    %5.2f\n",
             d,bt.qmax/1000,bt.aoamax_deg,bt.mach_at_burn,bt.vx,bt.sideload_fail_s);
    }
    double mdc = max_divert_bb(12000,-330,h_burn,M_AERO,cap_controlc,x_tol,vx_tol);
    double mdp = max_divert_pd(12000,-330,h_burn,M_AERO,cap_controlc,x_tol,vx_tol);
    printf("\n MAX aero divert (bang-bang optimal, control.c cap): %.0f m\n",mdc);
    printf(" MAX aero divert (sim-style PD tracker, control.c cap): %.0f m  (x<=%.0f, vx<=%.0f)\n\n",mdp,x_tol,vx_tol);

    /* ---------- PART 2a: AoA-cap sensitivity ---------- */
    printf("--- PART 2a: max aero divert vs AoA-cap schedule (12km, -330) ---\n");
    printf(" schedule                          max_divert_bb(m)  max_divert_pd(m)\n");
    struct { const char* nm; capfn f; } caps[]={
        {"control.c flown (8->3 deg)", cap_controlc},
        {"AgentA        (12->4 deg)", cap_agentA},
        {"hard STRUCT   (<=15 deg)  ", cap_hard15},
    };
    for(int i=0;i<3;i++){
        double a=max_divert_bb(12000,-330,h_burn,M_AERO,caps[i].f,x_tol,vx_tol);
        double b=max_divert_pd(12000,-330,h_burn,M_AERO,caps[i].f,x_tol,vx_tol);
        printf("  %-32s   %8.0f          %8.0f\n",caps[i].nm,a,b);
    }
    /* fixed-cap sweep (ideal, ignores schedule) */
    printf("\n fixed constant AoA cap (ideal ceiling, bang-bang):\n  cap(deg)  max_divert(m)  qbar_pk(kPa)@that_x\n");
    for(double c=2;c<=12;c+=2){
        g_fixedcap_deg=c;
        double a=max_divert_bb(12000,-330,h_burn,M_AERO,cap_fixed,x_tol,vx_tol);
        double rx,rvx; Traj bt; bangbang_best(a,12000,-330,h_burn,M_AERO,cap_fixed,&rx,&rvx,&bt);
        printf("   %5.1f     %8.0f        %6.1f\n",c,a,bt.qmax/1000);
    }
    printf("\n");

    /* ---------- PART 2b: start-altitude sensitivity ---------- */
    printf("--- PART 2b: max aero divert vs start altitude (vz0=-330, control.c cap) ---\n");
    printf("  h0(km)   max_divert_bb(m)   max_divert_pd(m)\n");
    for(double hk=8; hk<=16; hk+=2){
        double a=max_divert_bb(hk*1000,-330,h_burn,M_AERO,cap_controlc,x_tol,vx_tol);
        double b=max_divert_pd(hk*1000,-330,h_burn,M_AERO,cap_controlc,x_tol,vx_tol);
        printf("   %4.0f     %8.0f           %8.0f\n",hk,a,b);
    }
    printf("\n");

    /* ---------- PART 2c: entry-burn present (subsonic entry, lower speed) ---------- */
    printf("--- PART 2c: entry-burn present -> enters aero phase SLOWER ---\n");
    printf("(entry burn bleeds speed; vehicle starts aero phase near Mach 0.7-1.0)\n");
    printf("  entry vz0(m/s)  Mach0   max_divert_bb(m)  qbar_pk(kPa)  max_divert_pd(m)\n");
    double vze[]={-330,-250,-230,-200,-170};
    for(int i=0;i<5;i++){
        Atmo A0; atmo_eval(12000,&A0);
        double M0=fabs(vze[i])/A0.a;
        double a=max_divert_bb(12000,vze[i],h_burn,M_AERO,cap_controlc,x_tol,vx_tol);
        double b=max_divert_pd(12000,vze[i],h_burn,M_AERO,cap_controlc,x_tol,vx_tol);
        double rx,rvx; Traj bt; bangbang_best(a,12000,vze[i],h_burn,M_AERO,cap_controlc,&rx,&rvx,&bt);
        printf("   %6.0f        %5.2f    %8.0f         %6.1f       %8.0f\n",
               vze[i],M0,a,bt.qmax/1000,b);
    }
    printf("\n");

    /* ---------- PART 3: verdict table — total capability vs demand ---------- */
    printf("--- PART 3: AERO_OFFSET well-posedness (total = aero + landing-burn) ---\n");
    double lb_divert=550.0;  /* landing-burn gimbal divert (prompt), m */
    printf("landing-burn gimbal divert budget: %.0f m (added below burn)\n",lb_divert);
    printf("AERO_OFFSET demand: mean 800 m, 1-sigma 250 m -> 3-sigma tail ~1550 m\n\n");
    double aero_ceils[3]; const char* nm3[3];
    aero_ceils[0]=max_divert_bb(12000,-330,h_burn,M_AERO,cap_controlc,x_tol,vx_tol); nm3[0]="control.c cap (flown)";
    aero_ceils[1]=max_divert_bb(12000,-330,h_burn,M_AERO,cap_agentA,x_tol,vx_tol);   nm3[1]="AgentA cap (12->4)";
    aero_ceils[2]=max_divert_bb(12000,-330,h_burn,M_AERO,cap_hard15,x_tol,vx_tol);   nm3[2]="hard 15deg STRUCT limit";
    printf(" %-26s  aero(m)  +burn(m)  =total(m)   covers 800?  covers 1050(mu+1s)? 1550(3s)?\n",
           "cap schedule");
    for(int i=0;i<3;i++){
        double tot=aero_ceils[i]+lb_divert;
        printf("  %-26s %6.0f   %5.0f    %7.0f     %-8s    %-8s        %-8s\n",
               nm3[i],aero_ceils[i],lb_divert,tot,
               (tot>=800)?"YES":"NO",(tot>=1050)?"YES":"NO",(tot>=1550)?"YES":"NO");
    }
    /* ---------- For P5: the v_xy-null switch altitude of the max-divert profile ---------- */
    printf("\n--- For P5 sequencing: bang-bang switch altitude (accel->null) at max divert ---\n");
    printf(" cap-schedule        maxDivert(m)  switch_alt(m)  frac_of_band  divert_at_switch(m)\n");
    { struct{const char*nm;capfn f;} cc[]={{"control.c(8->3)",cap_controlc},{"AgentA(12->4)",cap_agentA}};
      for(int k=0;k<2;k++){
        /* re-scan to recover the switch altitude that produced the max divert */
        double bb_sign=+1.0, dh=(12000.0-h_burn)/240.0, best_dx=0, best_sw=h_burn, dsw=0;
        for(double sw=12000; sw>=h_burn-0.5; sw-=dh){
            Traj tt; integrate(0.0,0.0,12000,-330,h_burn,M_AERO,cc[k].f,1,sw,bb_sign,&tt);
            if(fabs(tt.vx)<=vx_tol && fabs(tt.x)>best_dx){ best_dx=fabs(tt.x); best_sw=sw;
                /* divert accumulated by the switch point: rerun to switch alt */
                Traj th; integrate(0.0,0.0,12000,-330,sw,M_AERO,cc[k].f,1,sw,bb_sign,&th); dsw=fabs(th.x);
            }
        }
        double frac=(12000-best_sw)/(12000-h_burn);
        printf(" %-18s   %8.0f     %8.0f      %5.2f         %8.0f\n",cc[k].nm,best_dx,best_sw,frac,dsw);
      }
    }

    /* ---------- VERIFICATION: dt convergence + energy/qbar sanity ---------- */
    printf("\n--- VERIFY: integrator dt-convergence (AgentA cap, 12km/-330) ---\n");
    printf("  dt(s)    maxDivert_bb(m)\n");
    double dts[3]={0.004,0.002,0.001};
    for(int i=0;i<3;i++){ g_dt=dts[i];
        double d=max_divert_bb(12000,-330,h_burn,M_AERO,cap_agentA,x_tol,vx_tol);
        printf("  %5.3f   %8.1f\n",dts[i],d);
    }
    g_dt=0.002;
    /* independent closed-form cross-check: rest-to-rest bang-bang under a REPRESENTATIVE
     * constant a_lat over the effective maneuver time. Divert_max ~ a_lat * (t/2)^2
     * (accel t/2, decel t/2, symmetric). Use qbar~30kPa, 6deg, CNa~2.2. */
    { double qb=30000, alp=6*DEG2RAD, CNa=2.2, alat=qb*VEH_AREF*CNa*alp/M_AERO;
      double tfall=22.6, cf=alat*(tfall*0.5)*(tfall*0.5);
      printf("  closed-form check (6deg,qbar~30kPa,t=22.6s): a_lat=%.2f m/s^2 -> ~%.0f m (vs sim ~313)\n",alat,cf);
    }

    printf("\n(done)\n");
    return 0;
}
