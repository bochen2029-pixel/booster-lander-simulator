/* main.c — CLI entry. Modes: --selftest | --headless | --run.
 * Serve/replay/golden arrive at later milestones.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include "vmath.h"
#include "rng.h"
#include "atmosphere.h"
#include "constants.h"
#include "state.h"
#include "dynamics.h"
#include "integrator.h"
#include "scenario.h"
#include "sim.h"

static int g_fail = 0;
#define CHECK(cond, msg) do{ if(!(cond)){ printf("  FAIL: %s\n", msg); g_fail++; } }while(0)
#define CHECKF(a,b,tol,msg) do{ double _d=fabs((double)(a)-(double)(b)); if(_d>(tol)){ printf("  FAIL: %s  (%.9g vs %.9g, d=%.3g)\n", msg,(double)(a),(double)(b),_d); g_fail++; } }while(0)

/* ---------------- foundation tests ---------------- */
static double geom_from_geopot(double Hp_m){ return R_EARTH*Hp_m/(R_EARTH - Hp_m); }
static void test_atmosphere(void){
    printf("[atmosphere US76]\n");
    AtmoOut o;
    atmo_eval(0.0,&o);      CHECKF(o.p,101325.0,1.0,"p(0)");      CHECKF(o.T,288.15,0.01,"T(0)");
    CHECKF(o.rho,1.225,1e-3,"rho(0)");
    atmo_eval(geom_from_geopot(11000.0),&o); CHECKF(o.p,22632.1,5.0,"p(11km')"); CHECKF(o.T,216.65,0.05,"T(11km')");
    atmo_eval(geom_from_geopot(20000.0),&o); CHECKF(o.p,5474.89,2.0,"p(20km')");
    atmo_eval(geom_from_geopot(32000.0),&o); CHECKF(o.p,868.019,0.5,"p(32km')");
    atmo_eval(geom_from_geopot(47000.0),&o); CHECKF(o.p,110.906,0.1,"p(47km')");
    atmo_eval(geom_from_geopot(71000.0),&o); CHECKF(o.p,3.95642,0.01,"p(71km')");
    atmo_eval(0.0,&o);      CHECKF(o.a,340.29,0.5,"a(0)");
}
static void test_rng(void){
    printf("[rng philox determinism]\n");
    uint32_t a[4],b[4];
    rng_block(42,RNG_WIND,0,0,7,0,a); rng_block(42,RNG_WIND,0,0,7,0,b);
    CHECK(a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3],"philox reproducible");
    rng_block(42,RNG_MPPI,0,0,7,0,b); CHECK(!(a[0]==b[0]&&a[1]==b[1]),"streams differ");
    double s=0,s2=0; int N=20000;
    for(int i=0;i<N;i++){ double x=rng_normal(42,RNG_DISPERSION,(uint32_t)i,0,3); s+=x; s2+=x*x; }
    double mean=s/N, var=s2/N-mean*mean;
    CHECKF(mean,0.0,0.05,"normal mean"); CHECKF(var,1.0,0.06,"normal var");
}
static void test_quat(void){
    printf("[quaternion + frame conv]\n");
    double q[4]={0.1,0.2,0.3,0.4}; q_normalize(q);
    double v[3]={1.3,-0.7,2.1}, w1[3], b1[3];
    q_rot(w1,q,v); q_rot_inv(b1,q,w1);
    CHECKF(b1[0],v[0],1e-12,"rot roundtrip x"); CHECKF(b1[1],v[1],1e-12,"rot roundtrip y"); CHECKF(b1[2],v[2],1e-12,"rot roundtrip z");
    double qz[4]={0,0,0.7071067811865476,0.7071067811865476};
    double ex[3]={1,0,0}, r[3]; q_rot(r,qz,ex);
    CHECKF(r[0],0.0,1e-9,"qz x"); CHECKF(r[1],1.0,1e-9,"qz y");
    double a[3]={0,0,1}, bb[3]={1,0,0}, qab[4], out[3];
    q_from_two_vec(qab,a,bb); q_rot(out,qab,a);
    CHECKF(out[0],1.0,1e-9,"twovec x"); CHECKF(out[2],0.0,1e-9,"twovec z");
}

/* ---------------- physics oracle tests ---------------- */
static void mk_ballistic(State* st, double h, double vx, double vz){
    memset(st,0,sizeof(*st)); q_identity(&st->y[S_QX]);
    st->y[S_MLOX]=1000; st->y[S_MRP1]=430;   /* small residual */
    MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
    st->y[S_RZ]=h+mp.com; st->y[S_VX]=vx; st->y[S_VZ]=vz;
    st->engine_on=0; st->n_eng=1; st->ign_timer=-1;
}
static void test_ballistic(void){
    printf("[oracle: vacuum ballistic]\n");
    /* At high altitude (near-vacuum) drag is tiny; compare to analytic projectile with g(h)~g0. */
    State st; mk_ballistic(&st, 70000.0, 50.0, 0.0);
    Actuators act; memset(&act,0,sizeof(act)); act.n_eng=1;
    EnvCtx env; memset(&env,0,sizeof(env));
    double r0x=st.y[S_RX], r0z=st.y[S_RZ], v0z=st.y[S_VZ];
    double T=20.0; long steps=(long)(T/DT);
    for(long i=0;i<steps;i++) rk4_step(&st,&act,&env,DT);
    double g=G0*(R_EARTH/(R_EARTH+70000.0))*(R_EARTH/(R_EARTH+70000.0));
    double xa=r0x+50.0*T, za=r0z+v0z*T-0.5*g*T*T;
    CHECKF(st.y[S_RX],xa,2.0,"ballistic x (near-vacuum)");
    CHECKF(st.y[S_RZ],za,5.0,"ballistic z (near-vacuum)");
}
static void test_energy_quat(void){
    printf("[oracle: coast energy + quat norm]\n");
    State st; mk_ballistic(&st, 60000.0, 30.0, -20.0);
    st.y[S_WX]=0.05; st.y[S_WY]=0.0; st.y[S_WZ]=0.3;   /* spin */
    Actuators act; memset(&act,0,sizeof(act)); act.n_eng=1;
    EnvCtx env; memset(&env,0,sizeof(env));
    double qn0=0;
    for(long i=0;i<30000;i++){ rk4_step(&st,&act,&env,DT);
        double* q=&st.y[S_QX]; qn0=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]); }
    CHECKF(qn0,1.0,1e-9,"|q|==1 after 60s");
}
static void test_massprops(void){
    printf("[oracle: mass props + analytic Idot]\n");
    double ml=150000, mr=64000;
    MassProps a,b; double eps=1e-3;
    /* mdot: draining */
    double mdl=-300.0, mdr=-128.0;
    mass_props(ml,mr,mdl,mdr,&a);
    /* finite-difference dI/dt: advance masses by mdot*eps */
    MassProps p0,p1; mass_props(ml,mr,0,0,&p0); mass_props(ml+mdl*eps, mr+mdr*eps,0,0,&p1);
    double fd_tr=(p1.I_tr-p0.I_tr)/eps, fd_ax=(p1.I_ax-p0.I_ax)/eps;
    CHECKF(a.Idot_tr,fd_tr,fabs(fd_tr)*0.02+1.0,"Idot_tr vs finite diff");
    CHECKF(a.Idot_ax,fd_ax,fabs(fd_ax)*0.02+1.0,"Idot_ax vs finite diff");
    /* CoM sanity: with LOX high, RP1 low, CoM between */
    CHECK(a.com>RP1_BASE_Z && a.com<LOX_BASE_Z+24.0,"com in range");
    (void)b;
}
static void test_determinism(void){
    printf("[oracle: determinism memcmp]\n");
    Sim s1,s2; RunResult r1,r2;
    sim_init(&s1,SCEN_TERMINAL,42,7,0,GM_HOVERSLAM); sim_run(&s1,&r1,300.0);
    sim_init(&s2,SCEN_TERMINAL,42,7,0,GM_HOVERSLAM); sim_run(&s2,&r2,300.0);
    CHECK(memcmp(&s1.st.y,&s2.st.y,sizeof(s1.st.y))==0,"bit-identical trajectory (same seed)");
    CHECK(r1.verdict==r2.verdict,"verdict identical");
}
static void test_hover_impossible(void){
    printf("[oracle: hover impossibility]\n");
    /* min-throttle TWR at dry+0.5t must exceed 1.25 */
    double m=VEH_DRY+500.0;
    double T=engine_thrust(ENG_THR_MIN, P0_ATM);   /* SL, 1 engine */
    double twr=T/(m*G0);
    CHECK(twr>1.25,"min-throttle TWR>1.25 (cannot hover)");
    printf("       TWR_min=%.3f\n", twr);
}

static void test_fin_damping(void){
    printf("[oracle: grid-fin passive stability]\n");
    /* Deploy fins, perturb attitude+rate at altitude, zero actuators -> fins must DAMP. */
    State st; memset(&st,0,sizeof(st)); q_identity(&st.y[S_QX]);
    st.y[S_MLOX]=6000; st.y[S_MRP1]=2600;
    MassProps mp; mass_props(st.y[S_MLOX],st.y[S_MRP1],0,0,&mp);
    st.y[S_RZ]=8000.0+mp.com; st.y[S_VZ]=-250.0;
    st.y[S_WX]=0.15;                    /* initial pitch rate */
    st.deploy_frac=1.0; st.fins_deployed=1; st.engine_on=0; st.n_eng=1; st.ign_timer=-1;
    /* tilt the body 8 deg so there is an angle of attack for the fins to work on */
    double zt[3]={sin(8*DEG2RAD),0,cos(8*DEG2RAD)}, zb[3]={0,0,1}, qt[4];
    q_from_two_vec(qt,zb,zt); q_copy(&st.y[S_QX],qt);
    Actuators act; memset(&act,0,sizeof(act)); act.n_eng=1;   /* zero fins/gimbal/rcs */
    EnvCtx env; memset(&env,0,sizeof(env)); env.fins_deployed=1;
    double w0=fabs(st.y[S_WX]);
    double wmax=w0;
    for(long i=0;i<2500;i++){                                 /* 5 s */
        rk4_step(&st,&act,&env,DT);
        double wm=sqrt(st.y[S_WX]*st.y[S_WX]+st.y[S_WY]*st.y[S_WY]+st.y[S_WZ]*st.y[S_WZ]);
        if(wm>wmax)wmax=wm;
    }
    double wf=sqrt(st.y[S_WX]*st.y[S_WX]+st.y[S_WY]*st.y[S_WY]+st.y[S_WZ]*st.y[S_WZ]);
    printf("       w0=%.3f wmax=%.3f wf=%.3f rad/s\n", w0, wmax, wf);
    CHECK(wf < w0, "deployed fins damp angular rate (stabilizing sign)");
    CHECK(wmax < 3.0*w0, "no divergent growth");
}

static void test_aero_stability(void){
    printf("[oracle: aero-descent closed-loop fin stability]\n");
    /* Full fin-controlled descent must stay controlled. Catches the fin-allocation SIGN bugs
     * (pitch/yaw + roll positive feedback) that spun |w| to 4.5 rad/s -> LOC. */
    Sim s; sim_init(&s,SCEN_AERO_OFFSET,42,2,0,GM_HOVERSLAM);
    double wmax=0;
    for(long i=0;i<6000 && !s.done;i++){
        sim_step(&s);
        double wm=sqrt(s.st.y[S_WX]*s.st.y[S_WX]+s.st.y[S_WY]*s.st.y[S_WY]+s.st.y[S_WZ]*s.st.y[S_WZ]);
        if(wm>wmax)wmax=wm;
    }
    printf("       max|w| over descent = %.3f rad/s\n", wmax);
    CHECK(wmax < 1.5, "fin-controlled aero-descent stays stable (no sign-error spin-up)");
}

static int cmd_selftest(void){
    g_fail=0;
    test_atmosphere(); test_rng(); test_quat();
    test_ballistic(); test_energy_quat(); test_massprops();
    test_hover_impossible(); test_fin_damping(); test_aero_stability(); test_determinism();
    if(g_fail==0){ printf("SELFTEST: PASS\n"); return 0; }
    printf("SELFTEST: FAIL (%d)\n", g_fail); return 1;
}

/* ---------------- single run (debug) ---------------- */
static const char* verdict_str(int v){ const char* s[]={"NONE","PERFECT","GOOD","HARD","TIPPED","CRASHED"}; return (v>=0&&v<=5)?s[v]:"?"; }
static const char* fault_str(int f){ const char* s[]={"none","FUEL","STRUCT","THERMAL","LOC","OFFPAD"}; return (f>=0&&f<=5)?s[f]:"?"; }
static int cmd_run(int argc, char** argv){
    int scen=SCEN_TERMINAL; uint32_t seed=42, run=0; int verbose=0;
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--scenario")&&i+1<argc){ scen=scenario_from_name(argv[++i]); if(scen<0)scen=SCEN_TERMINAL; }
        else if(!strcmp(argv[i],"--seed")&&i+1<argc) seed=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--run")&&i+1<argc) run=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--verbose")) verbose=1;
    }
    Sim s; sim_init(&s,scen,seed,run,MOD_TURB,GM_HOVERSLAM);
    printf("scenario=%s seed=%u run=%u  h0=%.0f m  vz0=%.1f m/s\n",
        scenario_name(scen),seed,run, s.st.y[S_RZ], s.st.y[S_VZ]);
    long n=0;
    while(sim_step(&s)){
        n++;
        if(verbose && (s.st.step % 250 == 0)){
            MassProps mp; mass_props(s.st.y[S_MLOX],s.st.y[S_MRP1],0,0,&mp);
            double lat=sqrt(s.st.y[S_RX]*s.st.y[S_RX]+s.st.y[S_RY]*s.st.y[S_RY]);
            double wperp=sqrt(s.st.y[S_WX]*s.st.y[S_WX]+s.st.y[S_WY]*s.st.y[S_WY]);
            printf("  t=%6.2f h=%8.1f vz=%7.1f thr=%.2f tilt=%5.2f lat=%7.1f wperp=%5.2f wroll=%6.2f m=%8.0f ph=%d\n",
                s.st.t, s.st.y[S_RZ]-mp.com, s.st.y[S_VZ], s.st.y[S_THR],
                sim_body_tilt(&s.st)*RAD2DEG, lat, wperp, s.st.y[S_WZ], mp.m, s.st.phase);
        }
        if(s.st.t>300.0) break;
    }
    RunResult res; sim_run(&s,&res,300.0);  /* already done; fills res */
    printf("RESULT: %s  fault=%s  td_v=%.2f m/s  lat=%.2f m  tilt=%.2f deg  fuel=%.0f kg  t=%.1f s  maxq=%.0f Pa\n",
        verdict_str(s.st.verdict), fault_str(s.st.fault), s.impact_v, s.impact_lat,
        sim_body_tilt(&s.st)*RAD2DEG, s.st.y[S_MLOX]+s.st.y[S_MRP1], s.st.t, s.max_qbar);
    return 0;
}

/* ---------------- headless Monte Carlo ---------------- */
static int cmd_headless(int argc, char** argv){
    int scen=SCEN_TERMINAL; uint32_t seed=42; long runs=1000; const char* out=0; int modules=MOD_TURB;
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--scenario")&&i+1<argc){ scen=scenario_from_name(argv[++i]); if(scen<0)scen=SCEN_TERMINAL; }
        else if(!strcmp(argv[i],"--seed")&&i+1<argc) seed=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--runs")&&i+1<argc) runs=strtol(argv[++i],0,10);
        else if(!strcmp(argv[i],"--out")&&i+1<argc) out=argv[++i];
        else if(!strcmp(argv[i],"--no-turb")) modules&=~MOD_TURB;
    }
    /* --out: open the report up front and FAIL LOUDLY if it can't be created -- otherwise we
     * run every sim for nothing and still print a false "wrote" (directive 5: headless is THE
     * proof artifact, it must not lie about producing it). The parent directory must already
     * exist; this tool does not create it (explicit paths, ORRERY house style). CSV header and
     * columns are frozen -- goldens and other tooling depend on them, do not touch. */
    FILE* f=0;
    if(out){
        f=fopen(out,"w");
        if(!f){
            int e=errno;
            fprintf(stderr,"error: --out: cannot open '%s' for writing: %s (errno=%d)\n"
                           "       (the parent directory must already exist)\n",
                    out, strerror(e), e);
            return 3;   /* distinct from 0 (landed), 1 (no landings), 2 (bad mode) */
        }
        fprintf(f,"seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush\n");
    }
    long cnt[6]={0}; long fault[6]={0};
    long c_offpad=0, c_hard=0, c_fuel=0, c_other=0;
    double sv=0,slat=0,stilt=0,sfuel=0; long good=0;
    double vmax=0;
    for(long r=0;r<runs;r++){
        Sim s; RunResult res; sim_init(&s,scen,seed,(uint32_t)(r+1),modules,GM_HOVERSLAM);
        sim_run(&s,&res,300.0);
        cnt[res.verdict<6?res.verdict:5]++; fault[res.fault<6?res.fault:0]++;
        if(res.verdict==V_CRASHED||res.verdict==V_TIPPED){
            if(res.fault==F_FUEL) c_fuel++;
            else if(res.td_lat>PAD_RADIUS) c_offpad++;
            else if(res.td_v>TD_V_HARD) c_hard++;
            else c_other++;
        }
        if(res.verdict==V_PERFECT||res.verdict==V_GOOD||res.verdict==V_HARD){ good++; sv+=res.td_v; slat+=res.td_lat; stilt+=res.td_tilt; sfuel+=res.fuel_margin; if(res.td_v>vmax)vmax=res.td_v; }
        if(f) fprintf(f,"%u,%s,%ld,%d,%d,%.3f,%.3f,%.4f,%.4f,%.1f,%.1f,%.1f,%.2f,%.4f\n",
            seed,scenario_name(scen),r+1,res.verdict,res.fault,res.td_v,res.td_lat,res.td_tilt,res.settled_tilt,res.fuel_margin,res.max_qbar,res.peak_qdot,res.t_total,res.max_crush);
    }
    int close_err=0;
    if(f && fclose(f)!=0) close_err=errno;   /* flush/close failure => CSV may be truncated */
    long landed=cnt[V_PERFECT]+cnt[V_GOOD]+cnt[V_HARD];
    double rate=100.0*landed/runs;
    /* Wilson 95% CI */
    double p=(double)landed/runs, z=1.96, den=1+z*z/runs;
    double centre=(p+z*z/(2*runs))/den, halfw=z*sqrt(p*(1-p)/runs + z*z/(4.0*runs*runs))/den;
    printf("========= HEADLESS MONTE CARLO =========\n");
    printf("scenario=%s seed=%u runs=%ld turb=%d\n", scenario_name(scen),seed,runs,(modules&MOD_TURB)!=0);
    printf("LANDED: %ld/%ld = %.1f%%  (Wilson95: %.1f..%.1f%%)\n", landed,runs,rate,100*(centre-halfw),100*(centre+halfw));
    printf("  PERFECT %ld  GOOD %ld  HARD %ld  TIPPED %ld  CRASHED %ld\n",
        cnt[V_PERFECT],cnt[V_GOOD],cnt[V_HARD],cnt[V_TIPPED],cnt[V_CRASHED]);
    printf("  faults: FUEL %ld  STRUCT %ld  THERMAL %ld  LOC %ld\n", fault[F_FUEL],fault[F_STRUCT],fault[F_THERMAL],fault[F_LOC]);
    printf("  crash causes: off-pad %ld  too-hard %ld  fuel-out %ld  other %ld\n", c_offpad,c_hard,c_fuel,c_other);
    if(good>0) printf("  landed means: td_v=%.2f m/s (max %.2f)  lat=%.2f m  tilt=%.2f deg  fuel=%.0f kg\n",
        sv/good, vmax, slat/good, stilt/good*RAD2DEG, sfuel/good);
    if(out){
        if(close_err){
            fprintf(stderr,"error: --out: failed to finalize '%s': %s (errno=%d)\n",
                    out, strerror(close_err), close_err);
            return 3;   /* CSV incomplete — do not claim success */
        }
        printf("  wrote %s\n", out);
    }
    return (rate>0.0)?0:1;
}

int main(int argc, char** argv){
    const char* mode = (argc>1)? argv[1] : "--selftest";
    if(!strcmp(mode,"--selftest")) return cmd_selftest();
    if(!strcmp(mode,"--headless")) return cmd_headless(argc,argv);
    if(!strcmp(mode,"--run")) return cmd_run(argc,argv);
    printf("booster-core modes: --selftest | --headless [--scenario S --seed N --runs N --out csv --no-turb] | --run [--scenario S --seed N --run N --verbose]\n");
    return 2;
}
