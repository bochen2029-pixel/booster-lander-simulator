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
#include "protocol.h"
#include "ws.h"
#include "guidance_neural.h"          /* N1 §9.8: GM_NEURAL forward pass (the KAT oracle + provenance) */
#include "neural_policy_weights.h"    /* NP_VERSION / NP_N_IN — the KAT is header-versioned */
#include "guidance_mppi.h"        /* MPPI_K/MPPI_H/MPPI_NCH for the CUDA harness (also via sim.h) */
#ifdef BL_HAVE_CUDA
#include "guidance_mppi_cuda.h"   /* M5: CUDA MPPI rollout + parity/perf harness (CUDA build only) */
#endif
#ifdef _WIN32
#  include <windows.h>   /* QueryPerformanceCounter / Sleep — PACING ONLY (never sim) */
#endif

/* M5: --mppi-cuda routes GM_MPPI solves to the GPU (defined in sim.c; exists in BOTH builds so the
 * flag parse compiles either way — in a no-CUDA build it stays 0 and the CLI refuses --mppi-cuda). */
extern int g_mppi_use_cuda;
extern int g_mppi_warm_neural;   /* E1 (D-029): --mppi-warm-neural arms the composite (student-warm-started MPPI); defined in sim.c */
extern int g_shadow_reactive;    /* E2' (D-032): --shadow-reactive logs the hoverslam divert as the DAgger teacher label; defined in sim.c */

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

/* N1 §9.8 / §13.5: the POLICY KAT (Known-Answer Test) — the Philox-KAT pattern applied to the net.
 * A hardcoded observation vector -> the fixed-order fp64 forward pass reproduces a BIT-EXACT expected
 * output. This proves the determinism export (freeze -> C-header -> fixed-order inference) is intact.
 *
 * REGENERATION: the expected vector below is computed FROM THE CURRENT neural_policy_weights.h. It MUST
 * be regenerated whenever the header regenerates (a new NP_VERSION = an ADR event). With the
 * NP_VERSION-0 PLACEHOLDER (mu=0, sd=1, all weights/biases 0): every layer accumulates 0 -> tanh(0)=0
 * -> logits 0, so for ANY input o: a_lat=(3.2*tanh(0), 3.2*tanh(0))=(0,0) and
 * throttle = ENG_THR_MIN + (1-ENG_THR_MIN)*0.5*(tanh(0)+1) = 0.40 + 0.60*0.5 = 0.70 exactly.
 * When real weights are exported, recompute EXP_* (e.g. dump neural_policy_forward(KAT_OBS) once and
 * paste, or have export_weights.py emit a KAT block) and bump NP_VERSION. bit-exact => tol 0.0. */
static void test_neural_kat(void){
    printf("[oracle: GM_NEURAL policy KAT] NP_VERSION=%d NP_N_IN=%d\n", NP_VERSION, NP_N_IN);
    /* a fixed, arbitrary-but-physical observation vector (values do not matter for the placeholder,
     * but a nonzero, varied vector proves the loops actually run over the weights when real). */
    double o[NP_N_IN];
    for(int i=0;i<NP_N_IN;i++) o[i] = 0.5*((double)((i*37+11)%13) - 6.0);  /* deterministic spread */
    double a[3];
    neural_policy_forward(o, a);
#if (NP_VERSION == 0)
    /* PLACEHOLDER expectation (zero-weights): a_lat=(0,0), throttle=0.70 — bit-exact. */
    const double EXP0=0.0, EXP1=0.0, EXP2=0.70;
    CHECKF(a[0], EXP0, 0.0, "NP KAT a_lat0 (placeholder bit-exact)");
    CHECKF(a[1], EXP1, 0.0, "NP KAT a_lat1 (placeholder bit-exact)");
    CHECKF(a[2], EXP2, 0.0, "NP KAT throttle (placeholder bit-exact)");
#else
    /* REAL-WEIGHTS expectation — NP_VERSION 6 (ENTRY clean round-3, weights_sha256[:16]=
     * b4734b4838c4d1b0, D-028). Dumped from THIS binary's fixed-order pass at %.17g (fp64
     * round-trips exactly); NEVER recompute in numpy (accumulation order differs).
     * Regenerate on every export. */
    const double EXP0 = -2.9905087230062684;
    const double EXP1 =  2.8676126619562528;
    const double EXP2 =  0.40000000368630478;
    CHECKF(a[0], EXP0, 0.0, "NP KAT a_lat0 (NP_VERSION 6 bit-exact)");
    CHECKF(a[1], EXP1, 0.0, "NP KAT a_lat1 (NP_VERSION 6 bit-exact)");
    CHECKF(a[2], EXP2, 0.0, "NP KAT throttle (NP_VERSION 6 bit-exact)");
#endif
    /* determinism: the forward pass is a pure function — twice on the same input is bit-identical. */
    double a2[3]; neural_policy_forward(o, a2);
    CHECK(a[0]==a2[0] && a[1]==a2[1] && a[2]==a2[2], "NP KAT forward pass is deterministic (bit-identical)");
    /* finiteness (the isfinite guard never emits NaN even on the zero net). */
    CHECK(isfinite(a[0]) && isfinite(a[1]) && isfinite(a[2]), "NP KAT output is finite");
}

static int cmd_selftest(void){
    g_fail=0;
    test_atmosphere(); test_rng(); test_quat();
    test_ballistic(); test_energy_quat(); test_massprops();
    test_hover_impossible(); test_fin_damping(); test_aero_stability(); test_determinism();
    test_neural_kat();
    if(g_fail==0){ printf("SELFTEST: PASS\n"); return 0; }
    printf("SELFTEST: FAIL (%d)\n", g_fail); return 1;
}

/* ---------------- single run (debug) ---------------- */
static const char* verdict_str(int v){ const char* s[]={"NONE","PERFECT","GOOD","HARD","TIPPED","CRASHED"}; return (v>=0&&v<=5)?s[v]:"?"; }
static const char* fault_str(int f){ const char* s[]={"none","FUEL","STRUCT","THERMAL","LOC","OFFPAD"}; return (f>=0&&f<=5)?s[f]:"?"; }

/* DIAL-A-GUST CLI parse (shared by --run and --headless). Grammar:
 *   --gust <peak_mps>@<alt_m>[:<halfwidth_m>]    e.g.  --gust 12@3000:400
 *   --gust-dir <deg>                              fixed horizontal bearing (0 => +x; optional)
 * Half-width defaults to GUST_HW_DEFAULT when the ':<hw>' is omitted. Returns 1 if a valid --gust
 * was parsed (peak>0), else 0. Writes peak/alt/hw/dir on success. dir is parsed independently and
 * persists across calls (pass the same &dir). A malformed spec disarms (returns 0) so the run stays
 * byte-identical rather than silently mis-firing. */
#define GUST_HW_DEFAULT 300.0
static int parse_gust_flag(const char* arg, const char* val, double* peak, double* alt, double* hw){
    if(strcmp(arg,"--gust")!=0 || !val) return 0;
    double p=0,a=0,w=GUST_HW_DEFAULT;
    const char* at=strchr(val,'@');
    if(!at){ fprintf(stderr,"error: --gust: expected <peak>@<alt>[:<halfwidth>] (got '%s')\n",val); return 0; }
    p=strtod(val,0);
    a=strtod(at+1,0);
    const char* colon=strchr(at+1,':');
    if(colon) w=strtod(colon+1,0);
    if(!(p>0.0) || !(w>0.0)){ fprintf(stderr,"error: --gust: peak and halfwidth must be > 0 (got peak=%g hw=%g)\n",p,w); return 0; }
    *peak=p; *alt=a; *hw=w;
    return 1;
}

/* N0 ENGINE-OUT CLI parse (engineout_design §E.1). Grammar:
 *   --engine-out <k>@<t>    k = 0 center / 1,2 sides;  t = sim-time of failure [s]  (e.g. 1@40)
 *   --engine-out random     seed a (side k, t) from the run's key (deterministic per seed)
 * Writes *eng (<0 disarmed on error) and *tsec. Returns 1 on a valid arm. `rnd` set for the seeded form
 * (the caller seeds it once sim_init has the seed/run). A malformed spec disarms (byte-identical run). */
static int parse_engine_out(const char* val, int* eng, double* tsec, int* rnd){
    *eng=-1; *tsec=0.0; *rnd=0;
    if(!val){ return 0; }
    if(!strcmp(val,"random")){ *rnd=1; *eng=1; *tsec=0.0; return 1; }
    const char* at=strchr(val,'@');
    if(!at){ fprintf(stderr,"error: --engine-out: expected <k>@<t> or 'random' (got '%s')\n",val); return 0; }
    int k=(int)strtol(val,0,10);
    double t=strtod(at+1,0);
    if(k<0||k>2){ fprintf(stderr,"error: --engine-out: engine k must be 0(center)/1/2(side) (got %d)\n",k); return 0; }
    *eng=k; *tsec=t; return 1;
}

/* N0 SEEDED TARGET CLI parse (target_sandbox_design §A). Grammar:
 *   --target seeded                      canonical seeded circular drift (amp/period from the run key)
 *   --target circle:<amp_m>:<period_s>   explicit circular drift
 *   --target line:<reach_m>:<dur_s>:<deg> explicit linear ramp along a bearing
 * Writes *mode (0 disarmed on error), *amp, *period, *bearing_deg. Returns 1 on a valid arm. */
static int parse_target(const char* val, int* mode, double* amp, double* period, double* bearing){
    *mode=0; *amp=0; *period=0; *bearing=0;
    if(!val){ return 0; }
    if(!strcmp(val,"seeded")){ *mode=1; *amp=0.0; *period=0.0; return 1; }  /* amp/period seeded in sim */
    if(!strncmp(val,"circle:",7)){
        const char* p=val+7; *amp=strtod(p,0);
        const char* c=strchr(p,':'); if(c) *period=strtod(c+1,0);
        if(!(*amp>0.0)||!(*period>0.0)){ fprintf(stderr,"error: --target circle: amp and period must be > 0\n"); *mode=0; return 0; }
        *mode=1; return 1;
    }
    if(!strncmp(val,"line:",5)){
        const char* p=val+5; *amp=strtod(p,0);   /* reach */
        const char* c1=strchr(p,':'); if(c1){ *period=strtod(c1+1,0);  /* duration */
            const char* c2=strchr(c1+1,':'); if(c2) *bearing=strtod(c2+1,0); }
        if(!(*amp>0.0)||!(*period>0.0)){ fprintf(stderr,"error: --target line: reach and duration must be > 0\n"); *mode=0; return 0; }
        *mode=2; return 1;
    }
    fprintf(stderr,"error: --target: expected 'seeded' | 'circle:<amp>:<period>' | 'line:<reach>:<dur>:<deg>' (got '%s')\n",val);
    return 0;
}

/* N0: arm the seeded target on a Sim after sim_init (mirrors sim_set_gust). mode 1=circle,2=line. For
 * the 'seeded' form (amp==0), draw amp/period/phase from the `target` Philox key so different seeds move
 * differently and a run replays bit-exact. amp<=0 non-seeded disarms (byte-identical). */
static void sim_arm_target(Sim* s, int mode, double amp, double period, double bearing_deg){
    if(mode<=0){ s->tgt_mode=0; return; }
    s->tgt_mode=mode;
    if(mode==1){
        if(amp<=0.0){   /* seeded canonical: amp ~ [8,20] m, period ~ [40,80] s, random phases */
            double u1=rng_u01((uint32_t)(s->tgt_seed + s->tgt_run*2654435761u + 707u));
            double u2=rng_u01((uint32_t)(s->tgt_seed + s->tgt_run*2654435761u + 808u));
            double u3=rng_u01((uint32_t)(s->tgt_seed + s->tgt_run*2654435761u + 909u));
            double u4=rng_u01((uint32_t)(s->tgt_seed + s->tgt_run*2654435761u + 111u));
            amp    = 8.0 + 12.0*u1;
            period = 40.0 + 40.0*u2;
            s->tgt_phase[0]=6.2831853071795864*u3;
            s->tgt_phase[1]=6.2831853071795864*u4;
        } else {
            s->tgt_phase[0]=0.0; s->tgt_phase[1]=0.0;
        }
        s->tgt_amp=amp;
        s->tgt_omega=(period>0.0)?(6.2831853071795864/period):0.0;
    } else { /* mode 2 linear ramp: amp=reach, period=duration, bearing */
        s->tgt_amp=amp;
        s->tgt_omega=(period>0.0)?(1.0/period):0.0;   /* ramp fraction rate [1/s] */
        s->tgt_phase[0]=bearing_deg*DEG2RAD;
        s->tgt_phase[1]=0.0;
    }
}

/* SEA (§4.4, target_sandbox_design §A.1, D-035): arm the P-M droneship deck on a Sim after sim_init.
 * The deck is seeded per (seed,run) so a farm samples independent sea states and each run replays
 * bit-exact. MOD_SEA gates the feature; this only builds the 48-component spectrum table (sea_deck_pose
 * is evaluated per physics step in sim_step). Hs is the significant wave height [m] (the sea-state knob). */
static void sim_arm_sea(Sim* s, uint32_t seed, uint32_t run, double Hs, double wander){
    sea_init(&s->sea, seed + run*2654435761u + 313u, Hs, wander);
}

/* N0: arm the engine-out event on a Sim after sim_init (engineout_design §E.3). For the seeded
 * 'random' form (rnd), draw (side k, t-in-entry-burn-window) from the run key so it replays bit-exact
 * and different seeds fail differently. eng<0 disarms (byte-identical). */
static void arm_engine_out(Sim* s, int eng, double tsec, int rnd, uint32_t seed, uint32_t run){
    if(eng<0){ s->eo_engine=-1; return; }
    if(rnd){
        double u1=rng_u01((uint32_t)(seed + run*2654435761u + 404u));   /* which side engine */
        double u2=rng_u01((uint32_t)(seed + run*2654435761u + 505u));   /* time within the entry-burn window */
        s->eo_engine = (u1<0.5)?1:2;                 /* a SIDE engine (the dramatic case) */
        /* The 3-engine ENTRY burn runs ~t=0.5..25 s (measured: ph=2 from ignition to the qbar cut).
         * Seed the failure into [4,18] s — mid-burn, three engines firing, so the induced torque has
         * a real fight and the "burn longer" make-up is exercised (engineout_design §F cadence). */
        s->eo_time   = 4.0 + u2*14.0;                /* seeded within the entry-burn window [4,18] s */
    } else {
        s->eo_engine=eng; s->eo_time=tsec;
    }
    s->eo_fired=0;
}

static int cmd_run(int argc, char** argv){
    int scen=SCEN_TERMINAL; uint32_t seed=42, run=0; int verbose=0; int gmode=GM_HOVERSLAM;
    int modules=MOD_TURB;
    double g_peak=0, g_alt=0, g_hw=0, g_dir=0;   /* DIAL-A-GUST (peak=0 => OFF => byte-identical) */
    int eo_eng=-1, eo_rnd=0; double eo_t=0;      /* N0 engine-out (eng<0 => OFF => byte-identical) */
    int tm=0; double t_amp=0,t_per=0,t_brg=0;    /* N0 seeded target (mode 0 => OFF => byte-identical) */
    double sea_hs=3.0;                            /* SEA sea-state Hs [m] (armed by --sea; MOD_SEA => OFF => byte-identical) */
    double sea_wander=0.0;                        /* SEA Stage-1c ±horizontal wander [m] (--sea-wander; 0 => heave-only) */
    const char* policy_log=0;                    /* N1 S0 teacher tap (--policy-log; NULL => OFF => byte-identical) */
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--scenario")&&i+1<argc){ scen=scenario_from_name(argv[++i]); if(scen<0)scen=SCEN_TERMINAL; }
        else if(!strcmp(argv[i],"--seed")&&i+1<argc) seed=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--run")&&i+1<argc) run=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--policy-log")&&i+1<argc) policy_log=argv[++i];  /* N1 S0 (o,a*) tap path */
        else if(!strcmp(argv[i],"--verbose")) verbose=1;
        else if(!strcmp(argv[i],"--inject")) modules|=MOD_INJECT;
        else if(!strcmp(argv[i],"--nav-noisy")) modules|=MOD_NAV_NOISY; /* §8.1 noisy measurement layer */
        else if(!strcmp(argv[i],"--mppi")) gmode=GM_MPPI;   /* HIER MPPI controller (track 4-B) */
        else if(!strcmp(argv[i],"--mppi-cuda")){ gmode=GM_MPPI; g_mppi_use_cuda=1; }  /* M5 GPU rollout */
        else if(!strcmp(argv[i],"--neural")) gmode=GM_NEURAL;   /* N1 §9.8 tier-3 learned policy */
        else if(!strcmp(argv[i],"--mppi-warm-neural")){ gmode=GM_MPPI; g_mppi_warm_neural=1; }  /* E1 D-029: composite = student-warm-started MPPI */
        else if(!strcmp(argv[i],"--shadow-reactive")) g_shadow_reactive=1;   /* E2' D-032: reactive (hoverslam) DAgger teacher label */
        else if(!strcmp(argv[i],"--gust")&&i+1<argc) parse_gust_flag(argv[i],argv[i+1],&g_peak,&g_alt,&g_hw),i++;
        else if(!strcmp(argv[i],"--gust-dir")&&i+1<argc) g_dir=strtod(argv[++i],0);
        else if(!strcmp(argv[i],"--engine-out")&&i+1<argc){ if(parse_engine_out(argv[++i],&eo_eng,&eo_t,&eo_rnd)) modules|=MOD_ENGINE_OUT; }
        else if(!strcmp(argv[i],"--target")&&i+1<argc){ if(parse_target(argv[++i],&tm,&t_amp,&t_per,&t_brg)) modules|=MOD_TARGET; }
        else if(!strcmp(argv[i],"--sea")){ modules|=MOD_SEA; if(i+1<argc && argv[i+1][0]!='-') sea_hs=strtod(argv[++i],0); }  /* SEA §4.4: heaving deck, optional Hs [m] (default 3.0) */
        else if(!strcmp(argv[i],"--sea-wander")){ modules|=MOD_SEA; sea_wander=3.0; if(i+1<argc && argv[i+1][0]!='-') sea_wander=strtod(argv[++i],0); }  /* SEA §4.4 Stage-1c: ±wander [m] slow station-keeping (default 3.0) */
    }
#ifndef BL_HAVE_CUDA
    if(g_mppi_use_cuda){ fprintf(stderr,"error: --mppi-cuda: this build has no CUDA support "
        "(configure with -DBL_CUDA=ON and a CUDA toolkit). Use --mppi for the CPU path.\n"); return 4; }
#endif
    /* N1 S0 teacher tap: open the (o,a*) binary log ONCE (fail loudly — the tap is a data artifact,
     * it must not silently vanish). Rows are written only on GM_MPPI gticks (policy_tap.h). Absent
     * flag => tap disarmed => byte-identical. */
    FILE* tapf=0;
    if(policy_log){
        tapf=fopen(policy_log,"wb");
        if(!tapf){ int e=errno; fprintf(stderr,"error: --policy-log: cannot open '%s' for writing: %s (errno=%d)\n"
                                              "       (the parent directory must already exist)\n", policy_log, strerror(e), e); return 3; }
        if(gmode!=GM_MPPI && gmode!=GM_NEURAL) fprintf(stderr,"warning: --policy-log logs only under --mppi (executed teacher) or --neural (DAgger shadow teacher, D-023)\n");
    }
    Sim s; sim_init(&s,scen,seed,run,modules,gmode);
    sim_set_gust(&s, g_peak, g_alt, g_hw, g_dir);   /* DIAL-A-GUST arm (no-op when g_peak==0) */
    if(modules&MOD_ENGINE_OUT){ arm_engine_out(&s, eo_eng, eo_t, eo_rnd, seed, run); }
    if(modules&MOD_TARGET){ sim_arm_target(&s, tm, t_amp, t_per, t_brg); }
    if(modules&MOD_SEA){ sim_arm_sea(&s, seed, run, sea_hs, sea_wander); }
    if(tapf){ s.tap.f=tapf; s.tap.seed=seed; s.tap.run=run; }   /* attach the tap AFTER sim_init (memset) */
    printf("scenario=%s seed=%u run=%u  h0=%.0f m  vz0=%.1f m/s\n",
        scenario_name(scen),seed,run, s.st.y[S_RZ], s.st.y[S_VZ]);
    if(s.gust.peak!=0.0) printf("  GUST: peak=%.1f m/s @ alt=%.0f m  hw=%.0f m (band %.0f..%.0f)  dir=%.0f deg (%.2f,%.2f)\n",
        s.gust.peak, s.gust.alt, s.gust.hw, s.gust.alt-s.gust.hw, s.gust.alt+s.gust.hw, g_dir, s.gust.dirx, s.gust.diry);
    if(modules&MOD_ENGINE_OUT) printf("  ENGINE-OUT: engine k=%d fails at t=%.2f s%s\n", s.eo_engine, s.eo_time, eo_rnd?" (seeded)":"");
    if(modules&MOD_TARGET) printf("  TARGET: SEEDED mode=%d amp=%.1f m omega=%.4f rad/s (drift)\n", s.tgt_mode, s.tgt_amp, s.tgt_omega);
    if(modules&MOD_SEA) printf("  SEA: heaving deck Hs=%.2f m  wander=±%.1f m (%d P-M components, seeded)\n", s.sea.Hs, s.sea.wander_amp[0], SEA_N);
    long n=0;
    while(sim_step(&s)){
        n++;
        if(verbose && (s.st.step % 250 == 0)){
            MassProps mp; mass_props(s.st.y[S_MLOX],s.st.y[S_MRP1],0,0,&mp);
            double lat=sqrt(s.st.y[S_RX]*s.st.y[S_RX]+s.st.y[S_RY]*s.st.y[S_RY]);
            double wperp=sqrt(s.st.y[S_WX]*s.st.y[S_WX]+s.st.y[S_WY]*s.st.y[S_WY]);
            double vrad=(lat>1e-3)?(s.st.y[S_VX]*s.st.y[S_RX]+s.st.y[S_VY]*s.st.y[S_RY])/lat:0.0; /* + = outward */
            /* horizontal wind the plant is currently applying (mean+Dryden+GUST), world m/s — makes
             * the 1-cosine pulse visible in-trace as the vehicle penetrates the band (guidance never
             * sees this number; it only feels the resulting lat/vrad drift). */
            double wind=sqrt(s.env.wind_world[0]*s.env.wind_world[0]+s.env.wind_world[1]*s.env.wind_world[1]);
            printf("  t=%6.2f h=%8.1f vz=%7.1f thr=%.2f tilt=%5.2f lat=%7.1f vrad=%6.1f qbar=%6.0f wperp=%5.2f wind=%5.1f m=%8.0f ph=%d\n",
                s.st.t, s.st.y[S_RZ]-mp.com, s.st.y[S_VZ], s.st.y[S_THR],
                sim_body_tilt(&s.st)*RAD2DEG, lat, vrad, s.diag.qbar, wperp, wind, mp.m, s.st.phase);
        }
        if(s.st.t>300.0) break;
    }
    RunResult res; sim_run(&s,&res,300.0);  /* already done; fills res */
    printf("RESULT: %s  fault=%s  td_v=%.2f m/s  lat=%.2f m  tilt=%.2f deg  fuel=%.0f kg  t=%.1f s  maxq=%.0f Pa\n",
        verdict_str(s.st.verdict), fault_str(s.st.fault), s.impact_v, s.impact_lat,
        sim_body_tilt(&s.st)*RAD2DEG, s.st.y[S_MLOX]+s.st.y[S_MRP1], s.st.t, s.max_qbar);
    if(tapf){ s.tap.f=0; if(fclose(tapf)!=0){ int e=errno; fprintf(stderr,"error: --policy-log: failed to finalize '%s': %s (errno=%d)\n", policy_log, strerror(e), e); return 3; }
              fprintf(stderr,"  policy-log: wrote %s (%d bytes/row)\n", policy_log, POLICY_TAP_ROW_BYTES); }
    return 0;
}

/* ---------------- headless Monte Carlo ---------------- */
static int cmd_headless(int argc, char** argv){
    int scen=SCEN_TERMINAL; uint32_t seed=42; long runs=1000; const char* out=0; int modules=MOD_TURB; int gmode=GM_HOVERSLAM;
    double g_peak=0, g_alt=0, g_hw=0, g_dir=0;   /* DIAL-A-GUST (peak=0 => OFF => byte-identical) */
    int eo_eng=-1, eo_rnd=0; double eo_t=0;      /* N0 engine-out (eng<0 => OFF => byte-identical) */
    int tm=0; double t_amp=0,t_per=0,t_brg=0;    /* N0 seeded target (mode 0 => OFF => byte-identical) */
    double sea_hs=3.0;                            /* SEA sea-state Hs [m] (armed by --sea; MOD_SEA => OFF => byte-identical) */
    double sea_wander=0.0;                        /* SEA Stage-1c ±horizontal wander [m] (--sea-wander; 0 => heave-only) */
    const char* policy_log=0;                    /* N1 S0 teacher tap (--policy-log; NULL => OFF => byte-identical) */
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--scenario")&&i+1<argc){ scen=scenario_from_name(argv[++i]); if(scen<0)scen=SCEN_TERMINAL; }
        else if(!strcmp(argv[i],"--seed")&&i+1<argc) seed=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--runs")&&i+1<argc) runs=strtol(argv[++i],0,10);
        else if(!strcmp(argv[i],"--out")&&i+1<argc) out=argv[++i];
        else if(!strcmp(argv[i],"--policy-log")&&i+1<argc) policy_log=argv[++i];  /* N1 S0 (o,a*) tap path */
        else if(!strcmp(argv[i],"--no-turb")) modules&=~MOD_TURB;
        else if(!strcmp(argv[i],"--inject")) modules|=MOD_INJECT;   /* Tier-B plant disturbances (F4) */
        else if(!strcmp(argv[i],"--nav-noisy")) modules|=MOD_NAV_NOISY; /* §8.1 noisy measurement layer */
        else if(!strcmp(argv[i],"--mppi")) gmode=GM_MPPI;           /* HIER MPPI controller (track 4-B) */
        else if(!strcmp(argv[i],"--mppi-cuda")){ gmode=GM_MPPI; g_mppi_use_cuda=1; }  /* M5 GPU rollout */
        else if(!strcmp(argv[i],"--neural")) gmode=GM_NEURAL;   /* N1 §9.8 tier-3 learned policy */
        else if(!strcmp(argv[i],"--mppi-warm-neural")){ gmode=GM_MPPI; g_mppi_warm_neural=1; }  /* E1 D-029: composite = student-warm-started MPPI */
        else if(!strcmp(argv[i],"--shadow-reactive")) g_shadow_reactive=1;   /* E2' D-032: reactive (hoverslam) DAgger teacher label */
        else if(!strcmp(argv[i],"--gust")&&i+1<argc) parse_gust_flag(argv[i],argv[i+1],&g_peak,&g_alt,&g_hw),i++;
        else if(!strcmp(argv[i],"--gust-dir")&&i+1<argc) g_dir=strtod(argv[++i],0);
        else if(!strcmp(argv[i],"--engine-out")&&i+1<argc){ if(parse_engine_out(argv[++i],&eo_eng,&eo_t,&eo_rnd)) modules|=MOD_ENGINE_OUT; }
        else if(!strcmp(argv[i],"--target")&&i+1<argc){ if(parse_target(argv[++i],&tm,&t_amp,&t_per,&t_brg)) modules|=MOD_TARGET; }
        else if(!strcmp(argv[i],"--sea")){ modules|=MOD_SEA; if(i+1<argc && argv[i+1][0]!='-') sea_hs=strtod(argv[++i],0); }  /* SEA §4.4: heaving deck, optional Hs [m] (default 3.0) */
        else if(!strcmp(argv[i],"--sea-wander")){ modules|=MOD_SEA; sea_wander=3.0; if(i+1<argc && argv[i+1][0]!='-') sea_wander=strtod(argv[++i],0); }  /* SEA §4.4 Stage-1c: ±wander [m] slow station-keeping (default 3.0) */
    }
#ifndef BL_HAVE_CUDA
    if(g_mppi_use_cuda){ fprintf(stderr,"error: --mppi-cuda: this build has no CUDA support "
        "(configure with -DBL_CUDA=ON and a CUDA toolkit). Use --mppi for the CPU path.\n"); return 4; }
#endif
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
    /* N1 S0 teacher tap: open the (o,a*) binary log ONCE for the whole batch (rows from every run go
     * to this one file, keyed by (seed,run) in cols 1,2). Fail loudly (data artifact, not optional).
     * The per-run FILE* is re-attached inside the loop after each sim_init (which memsets the tap).
     * Rows are written only on GM_MPPI ticks (policy_tap.h). Absent flag => byte-identical. */
    FILE* tapf=0;
    if(policy_log){
        tapf=fopen(policy_log,"wb");
        if(!tapf){ int e=errno; fprintf(stderr,"error: --policy-log: cannot open '%s' for writing: %s (errno=%d)\n"
                                              "       (the parent directory must already exist)\n", policy_log, strerror(e), e);
                   if(f) fclose(f); return 3; }
        if(gmode!=GM_MPPI && gmode!=GM_NEURAL) fprintf(stderr,"warning: --policy-log logs only under --mppi (executed teacher) or --neural (DAgger shadow teacher, D-023)\n");
    }
    long cnt[6]={0}; long fault[6]={0};
    long c_offpad=0, c_hard=0, c_fuel=0, c_other=0;
    double sv=0,slat=0,stilt=0,sfuel=0; long good=0;
    double vmax=0;
    for(long r=0;r<runs;r++){
        Sim s; RunResult res; sim_init(&s,scen,seed,(uint32_t)(r+1),modules,gmode);
        if(tapf){ s.tap.f=tapf; s.tap.seed=seed; s.tap.run=(uint32_t)(r+1); }   /* attach tap after sim_init (memset) */
        sim_set_gust(&s, g_peak, g_alt, g_hw, g_dir);   /* DIAL-A-GUST arm (no-op when g_peak==0) */
        if(modules&MOD_ENGINE_OUT){ arm_engine_out(&s, eo_eng, eo_t, eo_rnd, seed, (uint32_t)(r+1)); }
        if(modules&MOD_TARGET){ sim_arm_target(&s, tm, t_amp, t_per, t_brg); }
        if(modules&MOD_SEA){ sim_arm_sea(&s, seed, (uint32_t)(r+1), sea_hs, sea_wander); }
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
    if(tapf){ if(fclose(tapf)!=0){ int e=errno; fprintf(stderr,"error: --policy-log: failed to finalize '%s': %s (errno=%d)\n", policy_log, strerror(e), e); return 3; }
              fprintf(stderr,"  policy-log: wrote %s (%d bytes/row)\n", policy_log, POLICY_TAP_ROW_BYTES); }
    long landed=cnt[V_PERFECT]+cnt[V_GOOD]+cnt[V_HARD];
    double rate=100.0*landed/runs;
    /* Wilson 95% CI */
    double p=(double)landed/runs, z=1.96, den=1+z*z/runs;
    double centre=(p+z*z/(2*runs))/den, halfw=z*sqrt(p*(1-p)/runs + z*z/(4.0*runs*runs))/den;
    printf("========= HEADLESS MONTE CARLO =========\n");
    printf("scenario=%s seed=%u runs=%ld turb=%d\n", scenario_name(scen),seed,runs,(modules&MOD_TURB)!=0);
    if(g_peak>0.0) printf("GUST: peak=%.1f m/s @ alt=%.0f m  hw=%.0f m (band %.0f..%.0f)  dir=%.0f deg\n",
        g_peak, g_alt, g_hw, g_alt-g_hw, g_alt+g_hw, g_dir);
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

/* ---------------- live telemetry server (--serve) ----------------
 * ADDITIVE mode (directive 3): reads sim state, serializes, streams to the
 * three.js/WebGPU renderer over a single WebSocket. The sim/integration path is
 * NOT touched — no RNG, no state writes, dt stays 2 ms. Wall-clock is used ONLY
 * to pace emission to ~1x real time; it never enters sim_step/dynamics.
 */
#ifdef _WIN32

/* Fill a BlTlmFixed from current sim state. Pure read of *s (directive 2). */
static void fill_tlm(const Sim* s, BlTlmFixed* p, uint32_t seq){
    const State* st = &s->st;
    memset(p, 0, sizeof(*p));
    p->magic = BL_MAGIC_TLM;
    p->ver   = BL_PROTO_VERSION;
    p->flags = 0;
    if(s->modules & MOD_SEA)       p->flags |= BL_TLM_FLAG_SEA_ACTIVE;
    if(s->modules & MOD_NAV_NOISY) p->flags |= BL_TLM_FLAG_NAV_NOISY;
    if(s->modules & MOD_NAV_NOISY) p->flags |= BL_TLM_FLAG_NAV_NOISY;

    p->step = st->step;
    p->t    = st->t;            /* the only f64 on the wire */
    p->seq  = seq;

    /* state -> fp32 (world frame, Z-up, quat xyzw) */
    for(int i=0;i<3;i++){ p->r[i]=(float)st->y[S_RX+i]; p->v[i]=(float)st->y[S_VX+i]; }
    for(int i=0;i<4;i++)  p->quat[i]=(float)st->y[S_QX+i];
    for(int i=0;i<3;i++)  p->w[i]=(float)st->y[S_WX+i];

    /* mass block */
    MassProps mp; mass_props(st->y[S_MLOX], st->y[S_MRP1], 0, 0, &mp);
    p->mass  = (float)mp.m;
    p->com_z = (float)mp.com;
    p->I_diag[0]=(float)mp.I_tr; p->I_diag[1]=(float)mp.I_tr; p->I_diag[2]=(float)mp.I_ax;
    p->prop_lox=(float)st->y[S_MLOX];
    p->prop_rp1=(float)st->y[S_MRP1];

    /* actuators: cmd from guidance/allocator, act from integrated state */
    p->throttle_cmd = (float)s->gcmd.throttle;   /* commanded [0.40..1] or 0 */
    p->throttle_act = (float)st->y[S_THR];        /* post-lag actual */
    p->gimbal_cmd[0]=(float)s->act.gimbal[0]; p->gimbal_cmd[1]=(float)s->act.gimbal[1];
    p->gimbal_act[0]=(float)st->y[S_G0];      p->gimbal_act[1]=(float)st->y[S_G1];
    for(int i=0;i<4;i++) p->fins_act[i]=(float)st->y[S_F0+i];

    /* status island. rcs_mask: no per-nozzle history in the plant — expose a single
     * "RCS firing this step" bit (bit0) derived from cold-gas flow; 0 otherwise. */
    p->rcs_mask = (s->act.rcs_dm > 0.0) ? 0x01u : 0x00u;
    p->n_eng = (uint8_t)((st->engine_on && st->ign_timer>=0.0) ? st->n_eng : 0);
    p->phase = (uint8_t)st->phase;               /* PH_* order == BlPhase order */
    p->guidance_mode = (uint8_t)s->guidance_mode;
    p->verdict = (uint8_t)st->verdict;           /* V_* order == BlVerdict order */
    p->solver_flags = (uint16_t)s->gcmd.solver_flags;

    /* environment & derived (from the spare-deriv Diag populated each step) */
    p->mach       = (float)s->diag.mach;
    p->qbar       = (float)s->diag.qbar;
    p->alpha_total= (float)s->diag.alpha;
    p->p_amb      = (float)s->diag.p_amb;
    p->p_chamber  = (float)(st->y[S_THR] * PC_REF);   /* directive D: plume p_0 */
    for(int i=0;i<3;i++) p->wind_local[i]=(float)s->env.wind_world[i];
    for(int i=0;i<3;i++) p->a_body[i]=(float)s->diag.a_body[i];
    p->qdot_heat  = (float)s->diag.qdot_heat;
    p->Q_heat     = (float)st->y[S_QHEAT];            /* integrated heat load */

    /* guidance-derived */
    p->t_go = (float)s->gcmd.t_go;
    {   /* slant distance to pad (pad at world origin, ground at deck_z) */
        double dx=st->y[S_RX], dy=st->y[S_RY], dz=st->y[S_RZ]-s->se.deck_z;
        p->dist_pad = (float)sqrt(dx*dx+dy*dy+dz*dz);
    }
    /* v3: predicted impact point + ignition altitude — the D-010 diegetic marker. Both are
     * PURE READS of state/gcmd (directive 2): guidance reads nothing new, no feedback path.
     * pred_impact = ballistic/ZEM projection r_xy + v_xy*t_go (v1 formula): the world-XY the
     * current lateral state coasts to over the estimated time-to-go. It CONVERGES onto the pad
     * as the solve tightens (t_go and v_xy shrink toward a centered touchdown) — "it actually
     * solved it," drawn from 62 km. Limitation: kinematic only (no wind / future burn steering /
     * aero); a first-order marker, not the solver's own touchdown estimate. Same semantic for
     * hoverslam AND mppi so the renderer has one consistent marker. */
    {
        double tg = s->gcmd.t_go; if(tg < 0.0) tg = 0.0; if(tg > 60.0) tg = 60.0;
        p->pred_impact[0] = (float)(st->y[S_RX] + st->y[S_VX]*tg);
        p->pred_impact[1] = (float)(st->y[S_RY] + st->y[S_VY]*tg);
    }
    /* ignite_h: aero-aware landing-burn ignition altitude (thrust-only suicide-burn shoot),
     * the same value the MPPI planner precomputes per replan (mppi.ignite_h) and hoverslam's
     * trigger implies. Computed fresh from the live state each frame via the shared predictor. */
    p->ignite_h = (float)bl_predict_ignite_h(st);

    /* v4 THE WIDE SOCKET (§8.1/§10.9): the TargetEstimate view + EngineHealth, pure reads of the
     * plant-filled truth (directive 2/5 clean — no feedback path). Nominal at N0 (origin/FIXED/valid,
     * all engines healthy) so v3 clients... would reject v4 loudly, intended. The renderer draws the
     * estimate marker + uncertainty ellipse distinct from the truth deck. */
    p->target_est_xy[0]  = (float)st->tgt.target_xy[0];
    p->target_est_xy[1]  = (float)st->tgt.target_xy[1];
    p->target_est_vxy[0] = (float)st->tgt.target_vxy[0];
    p->target_est_vxy[1] = (float)st->tgt.target_vxy[1];
    p->target_cov[0] = (float)st->tgt.target_cov[0];
    p->target_cov[1] = (float)st->tgt.target_cov[1];
    p->target_cov[2] = (float)st->tgt.target_cov[2];
    p->target_src    = st->tgt.target_src;
    p->target_valid  = st->tgt.target_valid;
    p->_pad1         = 0;
    p->target_age    = (float)st->tgt.target_age;
    /* engine-health BITMASK: bit i set iff engine i healthy; eng_n = engines available this run. */
    { uint8_t hb=0; int nh=0; for(int i=0;i<3;i++){ if(st->eng_health[i]){ hb|=(uint8_t)(1u<<i); nh++; } }
      p->eng_health = hb; p->eng_n = (uint8_t)st->n_eng; (void)nh; }
    p->guidance_np_ver = (uint16_t)((s->guidance_mode==GM_NEURAL) ? neural_policy_version() : 0);   /* N1: the EXACT flown policy (v4 provenance — a replay is attributable to it); 0 for non-NEURAL => every golden byte-identical (goldens emit under GM_HOVERSLAM) */
    /* v4 flags: target-movable / engine-out visibility for the client (nominal => neither set). */
    if(s->modules & MOD_TARGET)     p->flags |= BL_TLM_FLAG_TARGET_MOVABLE;
    if((s->modules & MOD_ENGINE_OUT) && (st->eng_health[0]==0||st->eng_health[1]==0||st->eng_health[2]==0))
        p->flags |= BL_TLM_FLAG_ENGINE_OUT;

    /* legs */
    p->deploy_frac = (float)st->deploy_frac;
    for(int i=0;i<4;i++) p->stroke[i]=(float)st->crush[i];

    /* aero force for HUD/VFX (world) */
    for(int i=0;i<3;i++) p->f_aero[i]=(float)s->diag.f_aero_world[i];

    /* ASDS deck pose only if SEA active; identity otherwise */
    p->deck_z = (s->modules & MOD_SEA) ? (float)s->se.deck_z : 0.0f;
    p->deck_quat[0]=0.0f; p->deck_quat[1]=0.0f; p->deck_quat[2]=0.0f; p->deck_quat[3]=1.0f;

    /* MPPI tails not wired yet (directive B) */
    p->plan_n = 0; p->cloud_n = 0;
}

static void fill_hello(const Sim* s, uint32_t seed, uint32_t run_idx, BlHello* h){
    memset(h,0,sizeof(*h));
    h->magic = BL_MAGIC_HELLO;
    h->ver   = BL_PROTO_VERSION;
    h->flags = 0;
    h->t0    = s->st.t;                 /* 0.0 at session start */
    h->seed  = (uint64_t)seed;
    h->dt    = (float)DT;
    h->tlm_hz= (float)(1.0/(DT*TLM_DECIM));   /* 125 Hz */
    h->tlm_decim = (uint32_t)TLM_DECIM;
    h->run_idx = run_idx;
    h->veh_len = (float)VEH_LEN;
    h->veh_dia = (float)VEH_DIA;
    h->leg_span= (float)LEG_SPAN;
    h->pad_radius=(float)PAD_RADIUS;
    h->deck_z  = (float)s->se.deck_z;
    h->pc_ref  = (float)PC_REF;
    h->plan_max = (uint16_t)BL_PLAN_MAX;
    h->cloud_max= (uint16_t)BL_CLOUD_MAX;
    h->scenario = (uint8_t)s->scenario;
    h->guidance_mode = (uint8_t)s->guidance_mode;
    h->modules = (uint8_t)s->modules;   /* module mask (BL_MOD_* incl. v4 TARGET/ENGINE_OUT) */
    /* v4 (§4.7/§10.9): World id+hash (Earth=World #0, pinned) + neural-policy version (0 at N0). A
     * replay is thereby attributable to the exact world and frozen policy that flew it. */
    h->world_id   = (uint8_t)WORLD_EARTH_ID;
    h->world_hash = (uint32_t)WORLD_EARTH_HASH;
    h->np_version = (uint16_t)((s->guidance_mode==GM_NEURAL) ? neural_policy_version() : 0);   /* N1: the EXACT flown policy; 0 for non-NEURAL => golden byte-identical */
    h->_pad1      = 0;
}

static void emit_evt(const Sim* s, uint16_t code, float a0, float a1){
    BlEvt e; memset(&e,0,sizeof(e));
    e.magic=BL_MAGIC_EVT; e.code=code; e.step=s->st.step; e.t=s->st.t;
    e.args[0]=a0; e.args[1]=a1;
    ws_send_binary(&e, sizeof(e));
}

/* Mode 2 (§M2): apply a client-injected BlCmd to the LIVE running sim. This DELIBERATELY
 * mutates the descent at wall-clock time (non-deterministic — the fenced interactive path,
 * default OFF). Every injection is JOURNALED to stderr with the sim-time so the exact run
 * can be replayed by re-injecting at the logged times. Only reached under --interactive. */
static void apply_command(Sim* s, const BlCmd* c, uint32_t seed, uint32_t run){
    if(c->type==BL_CMD_GUST){
        double peak = (c->p[0]!=0.0f)? (double)c->p[0] : 18.0;   /* default a stiff 18 m/s shear */
        double dir  = (double)c->p[1];
        double alt  = s->st.y[S_RZ];                              /* band centered at the CURRENT altitude */
        double hw   = (c->p[2]>0.0f)? (double)c->p[2] : 400.0;    /* band half-width [m] */
        sim_set_gust(s, peak, alt, hw, dir);                     /* the vehicle is at band center => hits now */
        emit_evt(s, BL_EVT_GUST, (float)peak, (float)dir);       /* announce on the reliable EVT channel */
        fprintf(stderr,"serve: [INJECT] t=%.2f GUST peak=%.1f alt=%.0f hw=%.0f dir=%.0f (seq=%u)\n",
                s->st.t, peak, alt, hw, dir, c->seq);
    } else if(c->type==BL_CMD_ENGINE_OUT){
        int eng = (int)c->p[0];
        if(eng!=1 && eng!=2) eng = 1 + (int)(c->seq & 1u);       /* a SIDE engine (the dramatic case) */
        s->modules |= MOD_ENGINE_OUT;                           /* enable the fire path (fill_tlm flags it) */
        arm_engine_out(s, eng, s->st.t, 0, seed, run);          /* fires on the next >1-engine burn; EVT on fire */
        fprintf(stderr,"serve: [INJECT] t=%.2f ENGINE-OUT eng=%d (fires on next multi-eng burn) (seq=%u)\n",
                s->st.t, eng, c->seq);
    } else if(c->type==BL_CMD_THRUST_LOSS){
        double frac = (c->p[0]>0.05f && c->p[0]<=1.0f)? (double)c->p[0] : 0.65;  /* sudden underperformance */
        s->env.thrust_scale = frac;                             /* the live plant multiplier (same field MOD_INJECT uses) */
        emit_evt(s, BL_EVT_FAULT, (float)MOD_INJECT, (float)frac);
        fprintf(stderr,"serve: [INJECT] t=%.2f THRUST-LOSS scale=%.2f (seq=%u)\n", s->st.t, frac, c->seq);
    } else {
        fprintf(stderr,"serve: [INJECT] unknown cmd type=%u (seq=%u) — ignored\n", c->type, c->seq);
    }
}

static int cmd_serve(int argc, char** argv){
    int scen=SCEN_TERMINAL; uint32_t seed=42, run=1; unsigned short port=8080;
    int modules=MOD_TURB;
    /* N0: the play-menu contract (D-017/D-019, supervisor.rs Launch). The shell passes the
     * disturbance specs to --serve VERBATIM; pre-N0 cmd_serve silently DROPPED them (unknown-arg
     * skip) — the picker clicked into a void. Serve now parses + arms EXACTLY like cmd_run:
     * absent => modules unchanged => sim_init identical => stream byte-identical (off-gate).
     * Malformed specs disarm-with-stderr (the shell's stderr panel shows why), run continues
     * nominal — a cockpit misclick never wedges the stream. */
    double g_peak=0, g_alt=0, g_hw=0, g_dir=0;   /* DIAL-A-GUST (peak=0 => OFF => byte-identical) */
    int eo_eng=-1, eo_rnd=0; double eo_t=0;      /* N0 engine-out (eng<0 => OFF => byte-identical) */
    int tm=0; double t_amp=0,t_per=0,t_brg=0;    /* N0 seeded target (mode 0 => OFF => byte-identical) */
    double sea_hs=3.0;                            /* SEA sea-state Hs [m] (armed by --sea; MOD_SEA => OFF => byte-identical) */
    double sea_wander=0.0;                        /* SEA Stage-1c ±horizontal wander [m] (--sea-wander; 0 => heave-only) */
    int interactive=0;                            /* Mode 2 (§M2): honor client-injected commands (default OFF = pure observer) */
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--scenario")&&i+1<argc){ scen=scenario_from_name(argv[++i]); if(scen<0)scen=SCEN_TERMINAL; }
        else if(!strcmp(argv[i],"--seed")&&i+1<argc) seed=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--run")&&i+1<argc)  run=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--nav-noisy")) modules|=MOD_NAV_NOISY; /* §8.1 noisy measurement layer */
        else if(!strcmp(argv[i],"--port")&&i+1<argc) port=(unsigned short)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--gust")&&i+1<argc) parse_gust_flag(argv[i],argv[i+1],&g_peak,&g_alt,&g_hw),i++;
        else if(!strcmp(argv[i],"--gust-dir")&&i+1<argc) g_dir=strtod(argv[++i],0);
        else if(!strcmp(argv[i],"--engine-out")&&i+1<argc){ if(parse_engine_out(argv[++i],&eo_eng,&eo_t,&eo_rnd)) modules|=MOD_ENGINE_OUT; }
        else if(!strcmp(argv[i],"--target")&&i+1<argc){ if(parse_target(argv[++i],&tm,&t_amp,&t_per,&t_brg)) modules|=MOD_TARGET; }
        else if(!strcmp(argv[i],"--sea")){ modules|=MOD_SEA; if(i+1<argc && argv[i+1][0]!='-') sea_hs=strtod(argv[++i],0); }  /* SEA §4.4: heaving deck, optional Hs [m] (default 3.0) */
        else if(!strcmp(argv[i],"--sea-wander")){ modules|=MOD_SEA; sea_wander=3.0; if(i+1<argc && argv[i+1][0]!='-') sea_wander=strtod(argv[++i],0); }  /* SEA §4.4 Stage-1c: ±wander [m] slow station-keeping (default 3.0) */
        else if(!strcmp(argv[i],"--interactive")) interactive=1;   /* Mode 2 (§M2): open the inbound command channel (gust/engine-out buttons) */
    }

    /* Same sim config as --run: turbulence module + hoverslam guidance. This does
     * NOT change determinism — identical seed/run reproduces the headless path. */
    Sim s; sim_init(&s, scen, seed, run, modules, GM_HOVERSLAM);
    sim_set_gust(&s, g_peak, g_alt, g_hw, g_dir);   /* DIAL-A-GUST arm (no-op when g_peak==0) */
    if(modules&MOD_ENGINE_OUT){ arm_engine_out(&s, eo_eng, eo_t, eo_rnd, seed, run); }
    if(modules&MOD_TARGET){ sim_arm_target(&s, tm, t_amp, t_per, t_brg); }
    if(modules&MOD_SEA){ sim_arm_sea(&s, seed, run, sea_hs, sea_wander); }
    if(g_peak!=0.0) fprintf(stderr,"serve: GUST armed peak=%.1f m/s @ %.0f m hw=%.0f dir=%.0f deg\n", g_peak, g_alt, g_hw, g_dir);
    if(modules&MOD_ENGINE_OUT) fprintf(stderr,"serve: ENGINE-OUT armed k=%d t=%.2f s%s\n", s.eo_engine, s.eo_time, eo_rnd?" (seeded)":"");
    if(modules&MOD_TARGET) fprintf(stderr,"serve: TARGET armed mode=%d amp=%.1f m omega=%.4f rad/s\n", s.tgt_mode, s.tgt_amp, s.tgt_omega);

    if(ws_serve_init(port)!=0){ fprintf(stderr,"serve: could not start WS server on port %u\n", port); return 4; }
    ws_set_interactive(interactive);   /* Mode 2 gate: OFF => client data frames dropped (pure observer) */
    if(interactive) fprintf(stderr,"serve: INTERACTIVE inbound channel ON (§M2 — determinism WAIVED; "
                                   "injections journaled below as [INJECT])\n");

    /* One HELLO up front so the renderer can build the scene. */
    BlHello hello; fill_hello(&s, seed, run, &hello);
    if(ws_send_binary(&hello, sizeof(hello))!=0){ fprintf(stderr,"serve: client gone before HELLO\n"); ws_close(); return 0; }
    fprintf(stderr,"serve: scenario=%s seed=%u run=%u — streaming @125 Hz\n", scenario_name(scen), seed, run);

    /* --- pacing setup (wall-clock lives HERE only) --- */
    LARGE_INTEGER freq, t_start; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t_start);
    const double frame_dt = DT*TLM_DECIM;             /* 0.008 s per emitted frame */
    LARGE_INTEGER t_stats_last=t_start; double stats_accum_frames=0;

    /* phase/verdict edge tracking for events */
    int last_phase=s.st.phase, last_verdict=s.st.verdict;
    int engine_was_on=0, green_flashed=0, legs_deployed_evt=0, touched_evt=0, mach1_evt=0;
    int eo_evt=0;   /* N0: engine-out FAULT emitted? (edge on s.eo_fired) */

    uint32_t seq=0; uint32_t emitted=0;
    int running=1;
    while(running){
        /* one physics step (identical to headless) */
        int alive = sim_step(&s);

        /* --- edge-triggered events (read-only) --- */
        if(s.st.phase != last_phase){
            emit_evt(&s, BL_EVT_PHASE_CHANGE, (float)s.st.phase, (float)last_phase);
            last_phase = s.st.phase;
        }
        int eng_now = (s.st.engine_on && s.st.ign_timer>=0.0);
        if(eng_now && !engine_was_on){ emit_evt(&s, BL_EVT_IGNITION_CMD, (float)s.st.n_eng, 0.0f); }
        if(eng_now && !green_flashed && s.st.ign_timer>=ENG_IGN_GREEN){
            emit_evt(&s, BL_EVT_GREEN_FLASH, 0.0f, 0.0f); green_flashed=1;
        }
        if(eng_now && !engine_was_on && s.st.ign_timer>=0.0){ emit_evt(&s, BL_EVT_ENGINE_START, (float)s.st.n_eng, 0.0f); }
        if(!eng_now && engine_was_on){ emit_evt(&s, BL_EVT_ENGINE_SHUTDOWN, 0.0f, 0.0f); }
        engine_was_on = eng_now;
        /* N0 ENGINE-OUT (§4.6/§10.4): the failure announces on the reliable EVT channel as
         * FAULT(type=ENGINE_OUT, args=k) — a read-only notification (directive 5). */
        if(!eo_evt && s.eo_fired){ emit_evt(&s, BL_EVT_FAULT, (float)MOD_ENGINE_OUT, (float)s.eo_engine); eo_evt=1; }
        if(!mach1_evt && s.diag.mach>=1.0 && s.diag.mach<50.0){
            emit_evt(&s, BL_EVT_MACH1_CROSS, (float)s.st.y[S_RX], (float)s.st.y[S_RY]);
            mach1_evt=1;
        }
        if(!legs_deployed_evt && s.st.deploy_cmd){ emit_evt(&s, BL_EVT_LEG_DEPLOY, 0.0f, 0.0f); legs_deployed_evt=1; }
        if(!touched_evt && s.touched){
            emit_evt(&s, BL_EVT_TOUCHDOWN, (float)s.impact_v, (float)s.impact_tilt);
            touched_evt=1;
        }
        if(s.st.verdict != last_verdict && s.st.verdict != V_NONE){
            emit_evt(&s, BL_EVT_VERDICT, (float)s.st.verdict, 0.0f);
            last_verdict = s.st.verdict;
        }

        /* --- telemetry every 4th step (125 Hz) --- */
        if(s.st.step % TLM_DECIM == 0){
            BlTlmFixed tlm; fill_tlm(&s, &tlm, seq++);
            if(ws_send_binary(&tlm, sizeof(tlm))!=0){ fprintf(stderr,"serve: client disconnected\n"); break; }
            emitted++;
            stats_accum_frames++;

            /* STATS ~10 Hz (every ~12 emitted frames @125 Hz) */
            if((emitted % 12)==0){
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                double dts = (double)(now.QuadPart - t_stats_last.QuadPart)/(double)freq.QuadPart;
                BlStats stt; memset(&stt,0,sizeof(stt));
                stt.magic=BL_MAGIC_STATS; stt.ver=BL_PROTO_VERSION;
                stt.step=s.st.step; stt.t=s.st.t;
                stt.max_qbar=(float)s.max_qbar; stt.peak_qdot=(float)s.peak_qdot;
                stt.fuel_kg=(float)(s.st.y[S_MLOX]+s.st.y[S_MRP1]);
                stt.twr=(float)s.diag.twr;
                stt.tlm_seq=(float)seq;
                stt.fps_emit=(float)((dts>1e-6)? stats_accum_frames/dts : 0.0);
                ws_send_binary(&stt, sizeof(stt));
                t_stats_last=now; stats_accum_frames=0;
            }

            /* service client control frames (PING/CLOSE); never blocks */
            if(ws_poll_client()){ fprintf(stderr,"serve: client requested close\n"); break; }

            /* Mode 2 (§M2): drain + apply a client-injected command (--interactive only).
             * ws_poll_client stashed the frame; here we MUTATE the live sim. Default OFF =>
             * ws_take_inbound returns 0 => no-op => the pure-observer stream is byte-identical. */
            if(interactive){
                BlCmd cmd;
                if(ws_take_inbound(&cmd, (int)sizeof(cmd)) >= (int)sizeof(cmd) && cmd.magic==BL_MAGIC_CMD)
                    apply_command(&s, &cmd, seed, run);
            }

            /* pace this emitted frame to wall-clock so the descent plays at 1x */
            double target = frame_dt * (double)emitted;
            for(;;){
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                double elapsed = (double)(now.QuadPart - t_start.QuadPart)/(double)freq.QuadPart;
                double slack = target - elapsed;
                if(slack <= 0.0) break;
                if(slack > 0.002) Sleep((DWORD)((slack-0.0015)*1000.0));  /* coarse sleep */
                /* else spin the last <2 ms for tight alignment */
            }
        }

        if(!alive){ running=0; }
        if(s.st.t > 300.0){ running=0; }   /* hard safety cap */
    }

    /* final flush: one last TLM + verdict so the renderer settles on the end state */
    { BlTlmFixed tlm; fill_tlm(&s, &tlm, seq++); ws_send_binary(&tlm, sizeof(tlm)); }
    fprintf(stderr,"serve: done — verdict=%s emitted=%u frames, t=%.1f s\n",
        verdict_str(s.st.verdict), emitted, s.st.t);
    ws_close();
    return 0;
}
/* Emit canonical packet bytes as hex to stdout (for goldens/protocol/*.hex).
 * Deterministic: nominal terminal run, seed 42, run 1, advanced to a fixed step.
 * Uses the SAME fill_hello/fill_tlm/emit path the server uses, so the golden is
 * byte-identical to the live wire frame. Prints three lines: HELLO, TLM, EVT. */
static void print_hex(const char* label, const void* p, size_t n){
    const unsigned char* b=(const unsigned char*)p;
    printf("%s %zu ", label, n);
    for(size_t i=0;i<n;i++) printf("%02x", b[i]);
    printf("\n");
}
static int cmd_golden(int argc, char** argv){
    (void)argc; (void)argv;
    Sim s; sim_init(&s, SCEN_TERMINAL, 42u, 1u, MOD_TURB, GM_HOVERSLAM);
    /* HELLO reflects session start (t=0) */
    BlHello hello; fill_hello(&s, 42u, 1u, &hello);
    print_hex("HELLO", &hello, sizeof(hello));
    /* advance to a representative mid-descent frame: 500 steps = 1.0 s sim.
     * step%TLM_DECIM==0 so it is a real emit boundary. */
    for(int i=0;i<500;i++) sim_step(&s);
    BlTlmFixed tlm; fill_tlm(&s, &tlm, 125u /* seq at 1.0s @125Hz */);
    print_hex("TLM", &tlm, sizeof(tlm));
    /* a representative EVT: PHASE_CHANGE to the current phase at this step */
    BlEvt e; memset(&e,0,sizeof(e));
    e.magic=BL_MAGIC_EVT; e.code=BL_EVT_PHASE_CHANGE; e.step=s.st.step; e.t=s.st.t;
    e.args[0]=(float)s.st.phase; e.args[1]=(float)PH_COAST;
    print_hex("EVT", &e, sizeof(e));
    return 0;
}
#else
static int cmd_serve(int argc, char** argv){ (void)argc; (void)argv;
    fprintf(stderr,"--serve requires Windows (Winsock2).\n"); return 2; }
static int cmd_golden(int argc, char** argv){ (void)argc; (void)argv;
    fprintf(stderr,"--golden requires Windows.\n"); return 2; }
#endif /* _WIN32 */

/* ================= M5 CUDA MPPI parity + perf harness (CUDA build only) =================
 * These commands drive the guidance_mppi_cuda.{h,cu} device path. They are compiled ONLY when the
 * build linked the CUDA toolkit (BL_HAVE_CUDA). In a CPU-only (CI) build they degrade to a graceful
 * stub that explains how to enable CUDA — never a link error against absent mppi_cuda_* symbols. */
#ifdef BL_HAVE_CUDA
/* ---------------- M5 CUDA MPPI parity + perf harness (--mppi-cuda-verify) ---------------- */
static int dcmp_desc(const void* a, const void* b){ double x=*(const double*)a,y=*(const double*)b; return (x<y)-(x>y); }
static int dcmp_asc(const void* a, const void* b){ double x=*(const double*)a,y=*(const double*)b; return (x>y)-(x<y); }

/* top-N rank agreement: fraction of the CPU-ref top-N rollout indices that are ALSO in the GPU
 * top-N (set overlap; order within the set not required — softmax weights the SET). */
static double topN_agreement(const double* cpu, const double* gpu, int K, int N){
    /* find the N smallest-cost indices for each (min-cost = best rollout). O(K*N), N small. */
    int* cpu_idx=(int*)malloc(sizeof(int)*N); int* gpu_idx=(int*)malloc(sizeof(int)*N);
    char* used=(char*)calloc(K,1);
    for(int n=0;n<N;n++){ int best=-1; double bv=1e300; for(int k=0;k<K;k++) if(!used[k]&&cpu[k]<bv){bv=cpu[k];best=k;} used[best]=1; cpu_idx[n]=best; }
    memset(used,0,K);
    for(int n=0;n<N;n++){ int best=-1; double bv=1e300; for(int k=0;k<K;k++) if(!used[k]&&gpu[k]<bv){bv=gpu[k];best=k;} used[best]=1; gpu_idx[n]=best; }
    int hit=0;
    for(int i=0;i<N;i++){ for(int j=0;j<N;j++) if(cpu_idx[i]==gpu_idx[j]){ hit++; break; } }
    free(cpu_idx); free(gpu_idx); free(used);
    return (double)hit/(double)N;
}

static int cmd_mppi_cuda_verify(int argc, char** argv){
#ifdef _WIN32
    int scen=SCEN_AERO_OFFSET; uint32_t seed=42, run=3; long target=6000;
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--scenario")&&i+1<argc){ scen=scenario_from_name(argv[++i]); if(scen<0)scen=SCEN_AERO_OFFSET; }
        else if(!strcmp(argv[i],"--seed")&&i+1<argc) seed=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--run")&&i+1<argc) run=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--step")&&i+1<argc) target=strtol(argv[++i],0,10);
    }
    printf("========= M5 CUDA MPPI PARITY + PERF (%s seed=%u run=%u, capture @step %ld) =========\n",
           scenario_name(scen), seed, run, target);
    if(mppi_cuda_init(MPPI_K)!=0){ fprintf(stderr,"FATAL: no CUDA device / init failed\n"); return 5; }

    /* --- capture a real mid-descent replan state via the CPU Sim driver --- */
    State st; EnvCtx env; static double ubar[MPPI_H][MPPI_NCH];
    double ignite_h=0, m0=0; uint32_t replan=0;
    {
        Sim s; sim_init(&s, scen, seed, run, MOD_TURB, GM_MPPI);
        g_mppi_use_cuda = 0;   /* advance on the CPU sim; snapshot the state a solve would see */
        long n=0; int alive=1;
        while(alive && n<target){ alive=sim_step(&s); n++; }
        st = s.st; env = s.env; replan = s.mppi.replan;
        MassProps mp; mass_props(st.y[S_MLOX],st.y[S_MRP1],0,0,&mp); m0=mp.m;
        ignite_h = mppi_cuda_compute_ignite_h(&st);
        mppi_cuda_warm_start(ubar, ignite_h, NULL, &st, &env);   /* N0: NULL target => origin (nominal parity) */
        double lat=sqrt(st.y[S_RX]*st.y[S_RX]+st.y[S_RY]*st.y[S_RY]);
        printf("captured @step %ld (alive=%d): h=%.1f m  vz=%.1f  lat=%.1f m  ignite_h=%.1f  replan=%u  m0=%.0f kg\n",
               n, alive, st.y[S_RZ]-mp.com, st.y[S_VZ], lat, ignite_h, replan, m0);
    }

    double gamma=1.0;   /* GAMMA_IS */
    static double cpuC[MPPI_K], gpuC[MPPI_K], gpuC2[MPPI_K];
    mppi_cpuref_rollout_costs(seed, replan, ignite_h, gamma, m0, &st, &env, ubar, NULL, cpuC, MPPI_K);
    mppi_cuda_rollout_costs  (seed, replan, ignite_h, gamma, m0, &st, &env, ubar, NULL, gpuC, MPPI_K);
    mppi_cuda_rollout_costs  (seed, replan, ignite_h, gamma, m0, &st, &env, ubar, NULL, gpuC2, MPPI_K);

    /* --- CPU<->GPU cost parity (design §9.5 tolerance) --- */
    double maxad=0, maxrel=0, sumabs=0; int nfin=0;
    double cmin=1e300; for(int k=0;k<MPPI_K;k++){ if(cpuC[k]<cmin)cmin=cpuC[k]; }
    for(int k=0;k<MPPI_K;k++){
        double d=fabs(cpuC[k]-gpuC[k]); if(d>maxad)maxad=d; sumabs+=d; nfin++;
        double denom=fabs(cpuC[k])>1.0?fabs(cpuC[k]):1.0; double rel=d/denom; if(rel>maxrel)maxrel=rel;
    }
    /* --- GPU run-twice determinism (same GPU+seed -> bit-identical) --- */
    int det_bitexact = (memcmp(gpuC,gpuC2,sizeof(double)*MPPI_K)==0);
    double det_maxad=0; for(int k=0;k<MPPI_K;k++){ double d=fabs(gpuC[k]-gpuC2[k]); if(d>det_maxad)det_maxad=d; }

    double agree64 = topN_agreement(cpuC, gpuC, MPPI_K, 64);
    double agree16 = topN_agreement(cpuC, gpuC, MPPI_K, 16);

    printf("\n--- ROLLOUT COST PARITY (CPU-ref __host__  vs  GPU __device__, K=%d) ---\n", MPPI_K);
    printf("  cost range (cpu-ref): min=%.4f\n", cmin);
    printf("  max |Delta cost|      = %.6g\n", maxad);
    printf("  mean |Delta cost|     = %.6g\n", sumabs/nfin);
    printf("  max relative delta    = %.3e\n", maxrel);
    printf("  top-16 rank agreement = %.1f%%  (%d/16)\n", 100*agree16, (int)(agree16*16+0.5));
    printf("  top-64 rank agreement = %.1f%%  (%d/64)\n", 100*agree64, (int)(agree64*64+0.5));
    printf("\n--- PER-ARCH DETERMINISM (GPU run-twice, same seed) ---\n");
    printf("  C_k bit-identical run-twice = %s  (max|Delta|=%.3g)\n", det_bitexact?"YES":"NO", det_maxad);

    /* --- perf: p50/p99 replan latency vs K (end-to-end incl. transfers + reduction) --- */
    printf("\n--- REPLAN LATENCY vs K (mppi_cuda_solve end-to-end, ms) ---\n");
    printf("  %8s  %8s  %8s  %8s  %8s\n","K","p50","p90","p99","max");
    int Ks[]={256,1024,4096,8192,16384};
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    for(int ki=0; ki<(int)(sizeof(Ks)/sizeof(Ks[0])); ki++){
        int K=Ks[ki];
        if(mppi_cuda_init(K)!=0){ printf("  K=%d init failed (OOM?) — stopping perf sweep\n",K); break; }
        /* warm-start ubar is K-independent (H*NCH); reuse the captured one. */
        int iters=60; double* ms=(double*)malloc(sizeof(double)*iters);
        double lo,eo,bo;
        /* 3 warmup */
        for(int w=0; w<3; w++) mppi_cuda_solve(seed,replan,ignite_h,gamma,m0,&st,&env,ubar,30.0,NULL,&lo,&eo,&bo,K);
        for(int it=0; it<iters; it++){
            /* NOTE: solve mutates ubar (warm-start += correction), but the latency is K-dominated
             * (rollout + reduction), not ubar-value-dependent, so we time the steady-state solve. */
            LARGE_INTEGER t0,t1; QueryPerformanceCounter(&t0);
            mppi_cuda_solve(seed,replan,ignite_h,gamma,m0,&st,&env,ubar,30.0,NULL,&lo,&eo,&bo,K);
            QueryPerformanceCounter(&t1);
            ms[it]=1000.0*(double)(t1.QuadPart-t0.QuadPart)/(double)fq.QuadPart;
        }
        qsort(ms,iters,sizeof(double),dcmp_asc);
        double p50=ms[(int)(0.50*iters)], p90=ms[(int)(0.90*iters)], p99=ms[(int)(0.99*iters)], mx=ms[iters-1];
        printf("  %8d  %8.3f  %8.3f  %8.3f  %8.3f\n", K,p50,p90,p99,mx);
        free(ms);
    }
    (void)dcmp_desc;
    printf("\n  perf gate: p99 <= 6 ms at K=16384\n");
    mppi_cuda_shutdown();
    return 0;
#else
    (void)argc; (void)argv; fprintf(stderr,"--mppi-cuda-verify requires Windows.\n"); return 2;
#endif
}

/* --------------- M5 GPU-event kernel latency (contention-immune) --------------- */
static int cmd_mppi_cuda_bench(int argc, char** argv){
    int scen=SCEN_AERO_OFFSET; uint32_t seed=42, run=3; long target=6000;
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--scenario")&&i+1<argc){ scen=scenario_from_name(argv[++i]); if(scen<0)scen=SCEN_AERO_OFFSET; }
        else if(!strcmp(argv[i],"--seed")&&i+1<argc) seed=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--run")&&i+1<argc) run=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--step")&&i+1<argc) target=strtol(argv[++i],0,10);
    }
    printf("===== M5 CUDA MPPI GPU-EVENT KERNEL LATENCY (%s seed=%u run=%u @step %ld) =====\n",
           scenario_name(scen), seed, run, target);
    if(mppi_cuda_init(256)!=0){ fprintf(stderr,"FATAL: no CUDA device / init failed\n"); return 5; }
    State st; EnvCtx env; static double ubar[MPPI_H][MPPI_NCH];
    double ignite_h=0, m0=0; uint32_t replan=0;
    {
        Sim s; sim_init(&s, scen, seed, run, MOD_TURB, GM_MPPI);
        g_mppi_use_cuda = 0;
        long n=0; int alive=1; while(alive && n<target){ alive=sim_step(&s); n++; }
        st=s.st; env=s.env; replan=s.mppi.replan;
        MassProps mp; mass_props(st.y[S_MLOX],st.y[S_MRP1],0,0,&mp); m0=mp.m;
        ignite_h=mppi_cuda_compute_ignite_h(&st);
        mppi_cuda_warm_start(ubar, ignite_h, NULL, &st, &env);   /* N0: NULL target => origin (perf bench) */
    }
    double gamma=1.0;
    printf("GPU-only device time (cudaEvent; excludes host ESS-servo + CPU contention). ms:\n");
    printf("  %8s  %10s %10s %10s  %10s %10s\n","K","roll_p50","roll_p99","roll_max","reduce_p50","total_p99");
    int Ks[]={256,1024,4096,8192,16384}; int iters=80;
    static double rm[80], nm[80], tm[80];
    for(int ki=0; ki<(int)(sizeof(Ks)/sizeof(Ks[0])); ki++){
        int K=Ks[ki];
        if(mppi_cuda_bench_kernel(seed,replan,ignite_h,gamma,m0,&st,&env,ubar,K,iters,rm,nm,tm)!=0){
            printf("  K=%d bench failed (OOM?)\n",K); break; }
        qsort(rm,iters,sizeof(double),dcmp_asc); qsort(nm,iters,sizeof(double),dcmp_asc); qsort(tm,iters,sizeof(double),dcmp_asc);
        printf("  %8d  %10.4f %10.4f %10.4f  %10.4f %10.4f\n",
               K, rm[iters/2], rm[(int)(0.99*iters)], rm[iters-1], nm[iters/2], tm[(int)(0.99*iters)]);
    }
    printf("\n  perf gate: p99 <= 6 ms at K=16384 (GPU device time is the fp64-feasibility floor)\n");
    mppi_cuda_shutdown();
    return 0;
}
#else  /* !BL_HAVE_CUDA — CPU-only build: graceful stubs, no mppi_cuda_* references */
static int cmd_mppi_cuda_verify(int argc, char** argv){ (void)argc; (void)argv;
    fprintf(stderr,"--mppi-cuda-verify: this build has no CUDA support "
        "(configure with -DBL_CUDA=ON and a CUDA toolkit).\n"); return 4; }
static int cmd_mppi_cuda_bench(int argc, char** argv){ (void)argc; (void)argv;
    fprintf(stderr,"--mppi-cuda-bench: this build has no CUDA support "
        "(configure with -DBL_CUDA=ON and a CUDA toolkit).\n"); return 4; }
#endif /* BL_HAVE_CUDA */

int main(int argc, char** argv){
    const char* mode = (argc>1)? argv[1] : "--selftest";
    if(!strcmp(mode,"--selftest")) return cmd_selftest();
    if(!strcmp(mode,"--headless")) return cmd_headless(argc,argv);
    if(!strcmp(mode,"--run")) return cmd_run(argc,argv);
    if(!strcmp(mode,"--serve")) return cmd_serve(argc,argv);
    if(!strcmp(mode,"--golden")) return cmd_golden(argc,argv);
    if(!strcmp(mode,"--mppi-cuda-verify")) return cmd_mppi_cuda_verify(argc,argv);  /* M5 (CUDA build) */
    if(!strcmp(mode,"--mppi-cuda-bench")) return cmd_mppi_cuda_bench(argc,argv);    /* M5 (CUDA build) */
    printf("booster-core modes: --selftest | --headless [--scenario S --seed N --runs N --out csv --no-turb --inject --nav-noisy --mppi|--mppi-cuda|--neural --gust P@A[:HW] --gust-dir DEG --policy-log FILE] | --run [--scenario S --seed N --run N --verbose --inject --nav-noisy --mppi|--mppi-cuda|--neural --gust P@A[:HW] --gust-dir DEG --policy-log FILE] | --serve [--scenario S --seed N --run N --port P --nav-noisy --gust P@A[:HW] --gust-dir DEG --engine-out K@T|random --target SPEC] | --mppi-cuda-verify [--scenario S --seed N --run N --step N] | --mppi-cuda-bench [...]\n");
    printf("  --neural  N1 tier-3 GM_NEURAL learned policy (canon 9.8): frozen fp64 C forward pass (neural_policy_weights.h). Default OFF => byte-identical; ships DEAD on the NP_VERSION-0 placeholder (flies badly, honest crash).\n");
    printf("  --policy-log <file>  N1 S0 teacher tap: with --mppi, append one binary (o,a*) row per guidance tick for the offline distillation trainer (trainer/rowformat.py). Read-only => byte-identical.\n");
    printf("  --gust <peak_mps>@<alt_m>[:<halfwidth_m>]  DIAL-A-GUST: deterministic 1-cosine wind-shear pulse (canon 4.3/10.6), default hw=300 m, default dir +x; --gust-dir sets the bearing in deg. Default OFF => byte-identical.\n");
    printf("  --engine-out <k>@<t> | random  N0 ENGINE-OUT (canon 4.6): seeded engine failure during a multi-engine burn (k=0 center/1,2 side, t=sim-time s). Default OFF => byte-identical.\n");
    printf("  --target seeded | circle:<amp_m>:<period_s> | line:<reach_m>:<dur_s>:<deg>  N0 MOVABLE TARGET (canon 4.5): seeded closed-form target drift the guidance chases. Default OFF (FIXED origin) => byte-identical.\n");
    return 2;
}
