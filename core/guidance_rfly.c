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
/* D-046 ①b: --rfly-blind. 0 => the D-040 clairvoyant search exactly (byte-clean, leak GREEN).
 * Defined here, above its only use, so the candidate evaluator can read it. */
int g_rfly_blind = 0;

/* D-047 ①d: a SECOND constant theta, selected on the DIRECTLY OBSERVED engine count.
 * A single constant must serve both regimes at once, and D-047's structure says those regimes
 * want OPPOSITE things: the engine-out divert wants the divert family at its ceiling and the
 * damping family on its floor, which is very likely wrong for undisturbed flight. Conditioning
 * on n_eng costs no inference and no privilege — eng_health rides the legal socket as
 * OBS_EH0/EH1/EH2 — and D-030 already proved n_eng<3 is the right switch. Default off =>
 * g_rfly_fixed_eo_on==0 => the arming site is untouched => byte-identical. */
int    g_rfly_fixed_eo_on = 0;
double g_rfly_fixed_eo[RFLY_N_THETA];

/* D-047 ①e: THREE constant thetas, selected on FLIGHT PHASE. This supersedes ①d as the primary
 * conditioning on the engine-out battery, for a reason the ①c quality data made plain:
 *   - the fault fires at t in [4,18] s of a ~130 s flight, so n_eng<3 for ~90% of every draw and
 *     the ①d switch barely discriminates HERE (it still protects clean flight, so it stays);
 *   - ①c's leader lands 35/60 with ZERO PERFECT — mean td_v 4.09 m/s, 11.37 m off centre —
 *     because ONE constant must choose between the aggressive divert that buys lateral closure
 *     and the gentle damping that buys a soft, centred arrival. It picks aggressive and pays at
 *     touchdown. D-042 said the same thing from the other side: run-0's own converged theta held
 *     constant CRASHES its own draw, so theta must vary BY PHASE.
 * Phase is §4.3-legal (it is the vehicle's own flight state, not a prediction), so this is still
 * a reflex with search-settable parameters: no inference, no privilege, no net, 30 numbers.
 * Default off => byte-identical. Takes precedence over ①d when both are armed. */
int    g_rfly_fixed_ph_on = 0;
double g_rfly_fixed_ph[3][RFLY_N_THETA];   /* [0]=entry burn/pre  [1]=aero  [2]=landing burn on */

/* D-050 — A LEARNED CONDITIONAL POLICY:  theta = clamp( b + W . phi(legal state) ).
 *
 * Why this shape and not the 39k-param theta-hat net: that net is trained by REGRESSION onto the
 * CEM's labels, and D-047 measured why that is ill-posed here — eight IDENTICAL flights flip
 * outcome from a 3.5% gain change, so the target surface is discontinuous and least-squares onto
 * its conditional mean cannot represent it. This path is trained instead by OPTIMISING ACTUAL
 * LANDING OUTCOME (no labels, no teacher, no privilege), which is indifferent to that
 * discontinuity — and 70 parameters is a budget the estate can actually search at 0.39 s/flight,
 * where 39,434 is not.
 *
 * phi is six §4.3-LEGAL observables read from the NAV view (never truth): normalised altitude,
 * lateral offset, vertical speed, horizontal speed, engine count, propellant fraction. Nothing
 * about the fault's future is consulted, so this is legal to deploy in a way the privileged
 * search is not. Default off => byte-identical. */
#define RFLY_POL_NF 6
int    g_rfly_policy_on = 0;
double g_rfly_policy[RFLY_N_THETA][RFLY_POL_NF + 1];   /* [out][0]=bias, [out][1..NF]=weights */

/* D-052 — EVENT-TRIGGERED REPLAN. `--rfly-event-replan`. Default off => byte-identical.
 *
 * The cadence is PURELY PERIODIC (RFLY_REPLAN_DT = 10 s) and the fault fires at t in [4,18] s, so
 * a fault at t=11 leaves the vehicle flying a plan computed for THREE engines while running on
 * two, for up to nine seconds, during the entry burn. The blind arm's dominant failure is LOC
 * (13 of 22 crashes at full budget, against ZERO for the clairvoyant arm) and this is the
 * suspected mechanism: not that the search is blind, but that it does not RE-PLAN when the thing
 * it was blind to actually happens.
 *
 * Reacting to n_eng is not privilege. n_eng is the §4.3-legal sensed firing count — the same
 * quantity D-030 already switches its bank cap on, and eng_health rides the legal socket as
 * OBS_EH0/EH1/EH2. What is illegal is knowing the fault BEFORE it fires; noticing it AFTER is
 * what any flight computer does.
 *
 * Seeded lazily: last_n_eng==0 means "first gtick", which arms without firing. */
int g_rfly_event_replan = 0;

int rfly_event_due(struct Sim* s, int n_eng_now){
    RflyState* rf = &((Sim*)s)->rfly;
    int prev = rf->last_n_eng;
    rf->last_n_eng = n_eng_now;
    if(!g_rfly_event_replan) return 0;
    if(prev == 0) return 0;                 /* first observation: arm, do not fire */
    return (n_eng_now != prev);             /* the engine count changed => the plan is stale */
}

/* exported so the arming site in sim.c can clamp into the same box the CEM uses */
void rfly_clamp_theta(double th[RFLY_N_THETA]){
    for(int i=0;i<RFLY_N_THETA;i++) th[i]=rclampd(th[i],RT_LO[i],RT_HI[i]);
}

static double rfly_eval_candidate(const Sim* s, const double th[RFLY_N_THETA], double t_horizon){
    Sim c2 = *s;
    for(int i=0;i<RFLY_N_THETA;i++) c2.rfly.th[i]=rclampd(th[i],RT_LO[i],RT_HI[i]);
    c2.rfly.noreplan=1;
    c2.tap.f=NULL;                     /* never touch the shared tap file */
    c2.imu.quiet=1;                    /* D-058: candidate rollouts carry the platform but never journal */
    /* D-046 ①b — THE BLIND TEACHER (default OFF => byte-identical).
     * GM_RFLY is a PRIVILEGED oracle: this copy carries eo_engine/eo_time, so a candidate is
     * scored by flying the TRUE realization — the search knows which engine fails and when,
     * before it happens. That makes its 180/180 an upper bound and its labels UNLEARNABLE by
     * construction: they are conditioned on information the 39-D observation cannot contain.
     * Blinding hides only UNFIRED faults. An already-fired one keeps propagating (eo_fired is
     * latched at sim.c:392, n_eng is decremented and eng_health is set), because a vehicle that
     * has lost an engine legitimately knows it — that is honest self-sensed state (the sf_z
     * 52->37 signature, D-041), not foreknowledge. What is removed is only the future. */
    if(g_rfly_blind && !c2.eo_fired) c2.eo_engine = -1;   /* sim.c:390 fires only when >=0 */
    /* A candidate must be scored by flying THE RFLY LAW — that is what theta parameterises. Under
     * the plain GM_RFLY path this is already the mode and the assignment is a no-op. It matters for
     * the ORACLE-DAGGER shadow (D-041), where the search runs while the SIM's mode is GM_NEURAL:
     * without this the candidates would fly the student policy, and the CEM would be scoring theta
     * against a controller that never reads it. */
    c2.guidance_mode = GM_RFLY;
    RunResult R; memset(&R,0,sizeof(R));
    sim_run(&c2, &R, t_horizon);
    return rfly_cost(&c2, &R);
}

/* R2b (D-042): CEM budget scale. 1.0 => the D-040 POP/ITERS exactly (byte-clean). <1 shrinks the
 * search — the lever that shows how much θ̂-warm-starting saves. Set by --rfly-budget. */
double g_rfly_budget = 1.0;
/* D-057 (PLAN.md Phase 0.3): --rfly-budget scales POP and ITERS TOGETHER, and ITERS floors at 2 —
 * so below ~1/5 only POP moves and the budget curve is confounded. These two scale ONE axis each,
 * multiplied AFTER the budget. Default 1.0: x*1.0 is exact in IEEE, so (int)((base*budget)*1.0)
 * == (int)(base*budget) bit for bit and the flags are byte-clean when absent. */
double g_rfly_pop_scale   = 1.0;
double g_rfly_iters_scale = 1.0;
static int g_rfly_scale_logged = 0;

void rfly_replan(Sim* s, int big){
    RflyState* rf=&s->rfly;
    int POP   = (int)(((big ? 192 : 48) * g_rfly_budget) * g_rfly_pop_scale);   if(POP<8)  POP=8;
    int ITERS = (int)(((big ? 10  : 4 ) * g_rfly_budget) * g_rfly_iters_scale); if(ITERS<2)ITERS=2;
    if(!g_rfly_scale_logged && (g_rfly_pop_scale!=1.0 || g_rfly_iters_scale!=1.0)){
        int POPs   = (int)((48 * g_rfly_budget) * g_rfly_pop_scale);   if(POPs<8)  POPs=8;
        int ITERSs = (int)((4  * g_rfly_budget) * g_rfly_iters_scale); if(ITERSs<2)ITERSs=2;
        int POPb   = (int)((192* g_rfly_budget) * g_rfly_pop_scale);   if(POPb<8)  POPb=8;
        int ITERSb = (int)((10 * g_rfly_budget) * g_rfly_iters_scale); if(ITERSb<2)ITERSb=2;
        fprintf(stderr, "  [rfly_budget] budget=%.4g pop_scale=%.4g iters_scale=%.4g => big POP=%d ITERS=%d (%d evals) | small POP=%d ITERS=%d (%d evals)\n",
                g_rfly_budget, g_rfly_pop_scale, g_rfly_iters_scale, POPb, ITERSb, POPb*ITERSb, POPs, ITERSs, POPs*ITERSs);
        g_rfly_scale_logged = 1;
    }
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
    s->rfly.last_n_eng=0;   /* D-052: 0 = not yet observed; arms on the first gtick without firing */
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
    /* TERMINAL FREEZE: once the landing burn is lit, the flare flies with LOCKED gains.
     * Async results are solved from a ~5-9 s-stale snapshot — a theta optimized for the
     * pre-ignition descent, applied mid-flare, destabilized the first live demo into LOC
     * at 13 m. The mission plan + last pre-ignition replan own the terminal (the sync
     * path replans from the CURRENT state, so it never had this failure mode). */
    if(s->st.phase==PH_LANDING_BURN && s->st.engine_on){
        static int frozen_logged=0;
        if(!frozen_logged){ fprintf(stderr, "  [rfly_async] terminal freeze at t=%.1f (flare flies locked gains)\n", s->st.t); frozen_logged=1; }
        return;
    }
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
        g_ra.snap.imu.quiet = 1;
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
