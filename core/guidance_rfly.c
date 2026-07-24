/* guidance_rfly.c — GM_RFLY optimizer-in-the-loop over the native reactive stack
 * (see guidance_rfly.h for provenance + contracts; the CEM machinery mirrors the
 * Phase-A-proven guidance_cfly.c byte-clean pattern). */
#include "sim.h"
#include "guidance_rfly.h"
#include "guidance.h"
#include "constants.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

static double rclampd(double v, double lo, double hi){ return v<lo?lo:(v>hi?hi:v); }

/* ---- theta bounds. IDENTITY = the shipped D-030 reactive stack exactly. Bounds span the
 * regimes the ledger has already measured (e.g. D-030's sweep found ×4/×2.5 under EO — the
 * search may take EKR to 4.0; D-009/D-012 lore caps the rest at physically-sane ranges). */
static const double RT_LO[RFLY_N_THETA]  = { 0.30, 0.30, 0.60, 0.40, 0.25, 0.40, 0.50, 0.40, 0.00, 0.50 };
static const double RT_HI[RFLY_N_THETA]  = { 4.00, 4.00, 3.00, 2.50, 3.00, 2.50, 2.50, 3.50, 1.50, 2.50 };
static const double RT_STD0[RFLY_N_THETA]= { 0.50, 0.50, 0.35, 0.30, 0.40, 0.30, 0.30, 0.50, 0.30, 0.30 };
static const double RT_IDENTITY[RFLY_N_THETA] = { 1,1,1, 1,1,1, 1,1, 0, 1 };

/* ---- deterministic CEM sampler (local xorshift; seeded per replan from (seed, step);
 * a distinct mix constant from cfly's so the two searches draw independent streams) ---- */
static unsigned long long rfly_rng;
static double rf_urand(void){ rfly_rng^=rfly_rng<<13; rfly_rng^=rfly_rng>>7; rfly_rng^=rfly_rng<<17;
    return (double)((rfly_rng>>11)&0xFFFFFFFFFFFFFULL)/9007199254740992.0; }
static double rf_nrand(void){ double u1=rf_urand(),u2=rf_urand(); if(u1<1e-12)u1=1e-12;
    return sqrt(-2.0*log(u1))*cos(2.0*PI*u2); }

/* terminal cost of one candidate continuation — SANDBOX-FAITHFUL cost_of (no fuel term;
 * Phase A proved the fuel-margin term biases theta away from the law's optima). */
static double rfly_cost(const Sim* s2, const RunResult* R){
    double c=0;
    if(R->max_qbar>50000.0) c += (R->max_qbar-50000.0)/40.0;
    if(R->fault==F_STRUCT||R->fault==F_THERMAL||R->fault==F_LOC) c += 40000;
    if(s2->touched){
        c += 120.0*fmax(0.0,R->td_v-TD_V_TARGET);
        c += 60.0*R->td_lat;
        c += 200.0*R->td_tilt;
        if(R->td_lat>PAD_RADIUS)  c += 2500 + 100.0*(R->td_lat-PAD_RADIUS);
        if(R->td_v > TD_V_HARD)   c += 4000 + 200.0*(R->td_v-TD_V_HARD);
    } else {
        MassProps mp; mass_props(s2->st.y[S_MLOX],s2->st.y[S_MRP1],0,0,&mp);
        double hrem = fabs(s2->st.y[S_RZ]-mp.com-s2->se.deck_z);
        double miss = sqrt((s2->st.y[S_RX]-s2->gcmd.target_xy[0])*(s2->st.y[S_RX]-s2->gcmd.target_xy[0])
                         + (s2->st.y[S_RY]-s2->gcmd.target_xy[1])*(s2->st.y[S_RY]-s2->gcmd.target_xy[1]));
        c += 8000 + hrem + 3.0*miss;
    }
    if(R->fuel_margin<1.0 && !s2->touched) c += 3000;
    if(!isfinite(c)) c = 1e12;
    return c;
}

/* candidate continuation: copy the Sim, arm the candidate theta, fly the REAL plant to the
 * horizon under the native reactive stack (the GM_RFLY block in sim.c re-arms gcmd.rt from
 * c2.rfly.th every gtick; noreplan=1 skips the nested search). */
static double rfly_eval_candidate(const Sim* s, const double th[RFLY_N_THETA], double t_horizon){
    Sim c2 = *s;
    for(int i=0;i<RFLY_N_THETA;i++) c2.rfly.th[i]=rclampd(th[i],RT_LO[i],RT_HI[i]);
    c2.rfly.noreplan=1;
    c2.tap.f=NULL;                     /* never touch the shared tap file */
    RunResult R; memset(&R,0,sizeof(R));
    sim_run(&c2, &R, t_horizon);
    return rfly_cost(&c2, &R);
}

void rfly_replan(Sim* s, int big){
    RflyState* rf=&s->rfly;
    int POP   = big ? 192 : 48;
    int ITERS = big ? 10  : 4;
    double sd_scale = big ? 1.0 : 0.35;
    double t_horizon = s->st.t + 160.0;              /* the reactive descent is ~117-140 s */
    if(t_horizon < 210.0) t_horizon = 210.0;
    rfly_rng = 0xD1B54A32D192ED03ULL ^ ((unsigned long long)s->seed<<32) ^ (unsigned long long)s->st.step;

    double mean[RFLY_N_THETA], sd[RFLY_N_THETA];
    for(int i=0;i<RFLY_N_THETA;i++){ mean[i]=rf->th[i]; sd[i]=RT_STD0[i]*sd_scale; }
    double* cand=(double*)malloc((size_t)POP*RFLY_N_THETA*sizeof(double));
    double* cost=(double*)malloc((size_t)POP*sizeof(double));
    int*    idx =(int*)malloc((size_t)POP*sizeof(int));
    int ELITE=POP/8; if(ELITE<2)ELITE=2;
    double gbest=1e300; double gtheta[RFLY_N_THETA]; for(int i=0;i<RFLY_N_THETA;i++) gtheta[i]=mean[i];

    for(int it=0; it<ITERS; it++){
        for(int p=0;p<POP;p++)
            for(int i=0;i<RFLY_N_THETA;i++)
                cand[p*RFLY_N_THETA+i]=rclampd(mean[i]+sd[i]*rf_nrand(), RT_LO[i], RT_HI[i]);
        /* elitism slot 0 = best-so-far; at t=0 iter 0 this IS the warm start (= identity on the
         * first solve) — plain hoverslam is in-population, so gbest <= the baseline's cost. */
        for(int i=0;i<RFLY_N_THETA;i++) cand[i]=gtheta[i];
        int p;
        #pragma omp parallel for schedule(dynamic)
        for(p=0;p<POP;p++) cost[p]=rfly_eval_candidate(s, &cand[p*RFLY_N_THETA], t_horizon);
        for(int q=0;q<POP;q++) idx[q]=q;
        for(int a=0;a<ELITE;a++){ int m=a; for(int b=a+1;b<POP;b++) if(cost[idx[b]]<cost[idx[m]]) m=b; int t=idx[a];idx[a]=idx[m];idx[m]=t; }
        if(cost[idx[0]]<gbest){ gbest=cost[idx[0]]; for(int i=0;i<RFLY_N_THETA;i++) gtheta[i]=cand[idx[0]*RFLY_N_THETA+i]; }
        for(int i=0;i<RFLY_N_THETA;i++){
            double mu=0; for(int a=0;a<ELITE;a++) mu+=cand[idx[a]*RFLY_N_THETA+i]; mu/=ELITE;
            double var=0; for(int a=0;a<ELITE;a++){ double d=cand[idx[a]*RFLY_N_THETA+i]-mu; var+=d*d; } var/=ELITE;
            mean[i]=mu; sd[i]=sqrt(var)+0.02*RT_STD0[i];
        }
    }
    for(int i=0;i<RFLY_N_THETA;i++) rf->th[i]=gtheta[i];
    fprintf(stderr, "  [rfly_replan t=%.1f big=%d] gbest=%.1f | EKR=%.2f EKV=%.2f EBANK=%.2f ADEC=%.2f TLD=%.2f KDIV=%.2f KVN=%.2f IGN=%.2f TGL=%.2f KV=%.2f\n",
            s->st.t, big, gbest, gtheta[RT_EKR], gtheta[RT_EKV], gtheta[RT_EBANK], gtheta[RT_ADECEL],
            gtheta[RT_TLEAD], gtheta[RT_KDIV], gtheta[RT_KVNEAR], gtheta[RT_IGNM], gtheta[RT_TGTLEAD], gtheta[RT_KV]);
    free(cand); free(cost); free(idx);
}

/* init: warm start = IDENTITY = the shipped D-030 reactive stack. */
void rfly_init(Sim* s){
    _Static_assert(RFLY_N_THETA==10, "GuidanceCmd.rt[10] must match RFLY_N_THETA");
    for(int i=0;i<RFLY_N_THETA;i++) s->rfly.th[i]=RT_IDENTITY[i];
    s->rfly.next_replan_t=0.0;
    s->rfly.noreplan=0;
}

/* ============================ ASYNC live replans (N3; see header) ============================ */
#ifdef _WIN32
#include <windows.h>

static volatile LONG g_rfly_async = 0;
void rfly_set_async(int on){ InterlockedExchange(&g_rfly_async, on?1:0); }
int  rfly_async_on(void){ return g_rfly_async!=0; }

typedef struct {
    Sim    snap;                      /* the state the worker solves from (full copy) */
    double out[RFLY_N_THETA];         /* staging result */
    volatile LONG busy;               /* worker in flight */
    volatile LONG ready;              /* staging valid — sim swaps on next poll */
    int    big;
} RflyAsync;
static RflyAsync g_ra;                /* one sim per serve process => one flight slot */

static DWORD WINAPI rfly_worker(LPVOID p){
    RflyAsync* ra=(RflyAsync*)p;
#ifdef _OPENMP
    /* leave headroom so the real-time serve loop + WS stay glassy while we grind */
    int n=omp_get_num_procs()-2; if(n<2)n=2; omp_set_num_threads(n);
#endif
    rfly_replan(&ra->snap, ra->big);                       /* solves against the snapshot */
    memcpy((void*)ra->out, ra->snap.rfly.th, sizeof(ra->out));
    InterlockedExchange(&ra->ready, 1);
    InterlockedExchange(&ra->busy, 0);
    return 0;
}

void rfly_async_poll(Sim* s){
    /* 1) a completed solve? swap the staged theta in (single-writer/single-reader:
     * ready is set by the worker AFTER out is written; we clear it before reading —
     * the worker is idle by then (busy dropped with ready set). */
    if(InterlockedCompareExchange(&g_ra.ready, 0, 1)==1){
        memcpy(s->rfly.th, (const void*)g_ra.out, sizeof(s->rfly.th));
        fprintf(stderr, "  [rfly_async] theta SWAPPED at t=%.1f (live replan answered)\n", s->st.t);
    }
    /* 2) cadence due + no worker in flight? snapshot + launch. If a solve overruns the
     * 10-s cadence the next one just waits — the sim keeps flying the current theta. */
    if(!g_ra.busy && !s->rfly.noreplan && s->st.t >= s->rfly.next_replan_t){
        int big = (s->rfly.next_replan_t<=0.0);
        g_ra.snap = *s;
        g_ra.snap.tap.f = NULL;
        g_ra.big = big;
        InterlockedExchange(&g_ra.busy, 1);
        HANDLE h = CreateThread(NULL, 0, rfly_worker, &g_ra, 0, NULL);
        if(h) CloseHandle(h);
        else  InterlockedExchange(&g_ra.busy, 0);          /* thread-launch failure => retry next tick */
        s->rfly.next_replan_t = s->st.t + RFLY_REPLAN_DT;
    }
}
#else
void rfly_set_async(int on){ (void)on; }
int  rfly_async_on(void){ return 0; }
void rfly_async_poll(Sim* s){ (void)s; }
#endif
