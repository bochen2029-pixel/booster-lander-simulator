/* guidance_cfly.c — the OPTIMIZER-IN-THE-LOOP (see guidance_cfly.h for provenance + contracts). */
#include "sim.h"            /* the full Sim (the header forward-declares for sim.h) */
#include "guidance_cfly.h"
#include "guidance.h"
#include "constants.h"
#include "vmath.h"
#include "dynamics.h"
#include "atmosphere.h"
#include "state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

static double clampd(double v, double lo, double hi){ return v<lo?lo:(v>hi?hi:v); }

/* ---- theta bounds + the proven compound nominal (LODESTAR NOMINAL_TH — warm-start + fallback) ---- */
static const double TH_LO[CFLY_N_THETA] = { 24000, 0.45,  90, 0.0,  6500,  350, 2.0, 1.0, 0.45 };
static const double TH_HI[CFLY_N_THETA] = { 58000, 1.00, 800,22.0, 24000, 9000,13.0, 9.0, 2.2  };
static const double TH_STD0[CFLY_N_THETA]= {  9000, 0.15, 130, 4.0,  4500, 1400, 2.2, 1.6, 0.35 };
static const double CFLY_NOMINAL_TH[CFLY_N_THETA] = { 55521, 1.00, 90, 15.2, 10342, 9000, 5.735, 1.0, 1.545 };

/* ---- the deterministic CEM sampler (local xorshift; seeded per replan from (seed, step)) ---- */
static unsigned long long cfly_rng;
static double cf_urand(void){ cfly_rng^=cfly_rng<<13; cfly_rng^=cfly_rng>>7; cfly_rng^=cfly_rng<<17;
    return (double)((cfly_rng>>11)&0xFFFFFFFFFFFFFULL)/9007199254740992.0; }
static double cf_nrand(void){ double u1=cf_urand(),u2=cf_urand(); if(u1<1e-12)u1=1e-12;
    return sqrt(-2.0*log(u1))*cos(2.0*PI*u2); }

/* ============================ the theta-law (cfly_law) ============================
 * Ported from the LODESTAR sandbox guidance() (bit-for-bit structure), adapted to the main-tree
 * sockets: deck pose/velocity + target pose/velocity come from the Sim's module blocks (D-034…D-039),
 * healthy-engine count from the chamber-P flags. Closed-loop: reads ONLY the current state. */
void cfly_law(Sim* s, const State* nav, GuidanceCmd* g){
    CflyState* cf=&s->cfly;
    const double* th=cf->theta;
    const double* y=nav->y;
    MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
    AtmoOut atm; atmo_eval(y[S_RZ],&atm);
    double dz = s->gcmd.deck_z;                      /* live deck pose (module-written; 0 default) */
    double h  = y[S_RZ] - mp.com - dz;               /* height above the (moving) deck */
    double v[3]={y[S_VX],y[S_VY],y[S_VZ]}, vz=v[2];
    double sp=v3_norm(v);
    double prop=y[S_MLOX]+y[S_MRP1];
    int n_healthy = (nav->eng_health[0]>0?1:0)+(nav->eng_health[1]>0?1:0)+(nav->eng_health[2]>0?1:0);
    if(n_healthy<1) n_healthy=1;
    double eng1 = engine_thrust(1.0, atm.p);         /* per-engine thrust now [N] */
    double Tland= eng1;                              /* LANDING is single-engine (hoverslam TWR lands) */

    const double* txy = s->gcmd.target_xy;
    const double* tvxy = s->gcmd.target_vxy;
    double deck_vz = s->deck_vz_live;

    double qbar_now = 0.5*atm.rho*sp*sp;
    double vtgt   = deck_vz - TD_V_TARGET;
    double a_avail= (mp.m>1.0? Tland/mp.m : 1.0) - G0; if(a_avail<1.0) a_avail=1.0;  /* 1-engine net decel */
    double a_req  = (vz*vz - vtgt*vtgt)/(2.0*fmax(h,1.0));
    double h_stop = (vz*vz - vtgt*vtgt)/(2.0*fmax(0.85*a_avail,1.0));   /* suicide-burn braking distance */

    /* ---- UNIFIED LATERAL: ZEM/ZEV to the pad every tick, tgo from the vertical descent ---- */
    double tgo = clampd(h/fmax(5.0,-vz)*th[TH_TGO], 0.8, 80.0);
    double ex=y[S_RX]-txy[0], ey=y[S_RY]-txy[1];
    double vrx=v[0]-tvxy[0], vry=v[1]-tvxy[1];
    double alx = -(th[TH_KR]/(tgo*tgo))*(ex + vrx*tgo) - (th[TH_KV]/tgo)*vrx;
    double aly = -(th[TH_KR]/(tgo*tgo))*(ey + vry*tgo) - (th[TH_KV]/tgo)*vry;
    { double am=sqrt(alx*alx+aly*aly), acap=28.0; if(am>acap){ alx*=acap/am; aly*=acap/am; } }

    int entry = !cf->land_on && !cf->entry_done && h < th[TH_HENTRY] && sp > th[TH_VCUT] && prop > th[TH_FRES];
    if(!entry && !cf->land_on && h < th[TH_HENTRY] && (sp<=th[TH_VCUT] || prop<=th[TH_FRES])) cf->entry_done=1;
    int maxq  = !cf->land_on && prop > 4500.0 && qbar_now > 58000.0;   /* max-Q cap (until landing-fuel floor) — SANDBOX-FAITHFUL 58 kPa (the D-040 40 kPa retune fired the cap burn early and fed the terminal fuel-starve) */
    int trig  = (h < th[TH_HLAND]) && (h <= 1.05*h_stop) && (vz < -3.0);
    if(trig) cf->land_on=1;
    g->solver_flags=0;

    if(cf->land_on){
        cf->entry_done=1; g->n_eng=1;                    /* SINGLE-engine hoverslam */
        /* COAST-RELIGHT (the hover-starve fix): arriving too slow to need braking — or CLIMBING
         * (min-throttle TWR>1 forbids a slow hold: hover = climb + drift + starve) — cut the engine,
         * coast ballistic (free), relight when a real braking descent has built. Hysteresis [0.8,1.3];
         * hover-rescue gated h>15 so the last-15m flare never coasts. */
        if(!cf->coast_done && ((vz>-2.0 && h>15.0) || (h>200.0 && a_req<0.8))) cf->coast_done=1;
        else if(cf->coast_done && vz<=-2.0 && a_req>1.3) cf->coast_done=0;
        /* SANDBOX-FAITHFUL landing branch (oracle_t200.c:187-197). The D-040 additions —
         * the min-throttle coast trap and the h<60 lateral damping — are REVERTED: the trap
         * interacted with the coast-relight latch into the descent-stretching sawtooth that
         * fuel-starved every compound run, and the proven law never had either. */
        if(cf->coast_done){
            g->engine_cmd=0; g->throttle=0.0;
            g->a_lat[0]=alx; g->a_lat[1]=aly;            /* fin-steer while coasting */
        } else {
            g->engine_cmd=1;
            double a_vert=fmax(a_req,0.0);               /* suicide burn: null vz to vtgt at deck */
            double F[3]={ mp.m*alx, mp.m*aly, mp.m*(a_vert+G0) };
            g->throttle=clampd(Tland>1.0? v3_norm(F)/Tland:1.0, ENG_THR_MIN, 1.0);
            g->a_lat[0]=alx; g->a_lat[1]=aly;
            if(h < 60.0) g->solver_flags=SF_TERMINAL;    /* straighten for touchdown */
        }
    } else if(entry || maxq){
        g->engine_cmd=1; g->n_eng=n_healthy;             /* entry / max-Q burn on all healthy engines */
        g->throttle = maxq ? 1.0 : clampd(th[TH_ENTHR], ENG_THR_MIN, 1.0);
        g->a_lat[0]=alx; g->a_lat[1]=aly;                /* decel + ZEM cross-range divert */
    } else {
        g->engine_cmd=0; g->n_eng=n_healthy; g->throttle=0.0;
        g->a_lat[0]=alx; g->a_lat[1]=aly;                /* aero-steer the ZEM divert */
    }
    g->deploy_cmd = (h < LEG_DEPLOY_H) ? 1 : 0;
    g->mode=GM_CFLY;
}

/* ============================ the replan (warm-started local CEM) ============================ */

/* terminal cost of one candidate continuation (ported from the sandbox cost_of, main-tree fields) */
static double cfly_cost(const Sim* s2, const RunResult* R){
    double c=0;
    if(R->max_qbar>50000.0) c += (R->max_qbar-50000.0)/40.0;   /* gradient: pull max-Q down early */
    if(R->fault==F_STRUCT||R->fault==F_THERMAL||R->fault==F_LOC) c += 40000;   /* HARD reject */
    if(s2->touched){
        c += 120.0*fmax(0.0,R->td_v-TD_V_TARGET);
        c += 60.0*R->td_lat;
        c += 200.0*R->td_tilt;
        if(R->td_lat>PAD_RADIUS)  c += 2500 + 100.0*(R->td_lat-PAD_RADIUS);
        if(R->td_v > TD_V_HARD)   c += 4000 + 200.0*(R->td_v-TD_V_HARD);
        /* (SANDBOX-FAITHFUL: no fuel-margin term. The D-040 `c -= 0.022*fuel_margin` biased
         * the CEM's theta choices away from the proven law's optima — reverted.) */
    } else {
        /* didn't land by the horizon: reward getting low + on-target (reads the final state) */
        MassProps mp; mass_props(s2->st.y[S_MLOX],s2->st.y[S_MRP1],0,0,&mp);
        double hrem = fabs(s2->st.y[S_RZ]-mp.com-s2->se.deck_z);
        double miss = sqrt((s2->st.y[S_RX]-s2->gcmd.target_xy[0])*(s2->st.y[S_RX]-s2->gcmd.target_xy[0])
                         + (s2->st.y[S_RY]-s2->gcmd.target_xy[1])*(s2->st.y[S_RY]-s2->gcmd.target_xy[1]));
        c += 8000 + hrem + 3.0*miss;
    }
    if(R->fuel_margin<1.0 && !s2->touched) c += 3000;   /* starved without touching */
    if(!isfinite(c)) c = 1e12;
    return c;
}

/* candidate continuation: copy the Sim, arm the candidate theta, fly to the horizon. */
static double cfly_eval_candidate(const Sim* s, const double th[CFLY_N_THETA], double t_horizon){
    Sim c2 = *s;                       /* full-context copy (RNG is pure-per-(seed,step) => replays) */
    for(int i=0;i<CFLY_N_THETA;i++) c2.cfly.theta[i]=clampd(th[i],TH_LO[i],TH_HI[i]);
    c2.cfly.noreplan=1;                /* candidates never replan (no recursion) */
    c2.tap.f=NULL;                     /* never touch the shared tap file */
    RunResult R; memset(&R,0,sizeof(R));
    sim_run(&c2, &R, t_horizon);
    return cfly_cost(&c2, &R);
}

void cfly_replan(Sim* s, int big){
    CflyState* cf=&s->cfly;
    int POP   = big ? 192 : 48;
    int ITERS = big ? 10  : 4;
    double sd_scale = big ? 1.0 : 0.35;
    double t_horizon = s->st.t + 140.0;              /* the descent is ~100-135 s; bound the candidate */
    if(t_horizon < 200.0) t_horizon = 200.0;
    /* deterministic sampler seed (replayable): seed + absolute step */
    cfly_rng = 0x9E3779B97F4A7C15ULL ^ ((unsigned long long)s->seed<<32) ^ (unsigned long long)s->st.step;

    double mean[CFLY_N_THETA], sd[CFLY_N_THETA];
    const double* warm = cf->theta;
    if(!big){ for(int i=0;i<CFLY_N_THETA;i++){ mean[i]=warm[i]; sd[i]=TH_STD0[i]*sd_scale; } }
    else    { for(int i=0;i<CFLY_N_THETA;i++){ mean[i]=warm[i]; sd[i]=TH_STD0[i]*sd_scale; } }
    double* cand=(double*)malloc((size_t)POP*CFLY_N_THETA*sizeof(double));
    double* cost=(double*)malloc((size_t)POP*sizeof(double));
    int*    idx =(int*)malloc((size_t)POP*sizeof(int));
    int ELITE=POP/8; if(ELITE<2)ELITE=2;
    double gbest=1e300; double gtheta[CFLY_N_THETA]; for(int i=0;i<CFLY_N_THETA;i++) gtheta[i]=mean[i];

    for(int it=0; it<ITERS; it++){
        for(int p=0;p<POP;p++)
            for(int i=0;i<CFLY_N_THETA;i++)
                cand[p*CFLY_N_THETA+i]=clampd(mean[i]+sd[i]*cf_nrand(), TH_LO[i], TH_HI[i]);
        for(int i=0;i<CFLY_N_THETA;i++) cand[i]=gtheta[i];       /* elitism: cand 0 = best-so-far */
        int p;
        #pragma omp parallel for schedule(dynamic)
        for(p=0;p<POP;p++) cost[p]=cfly_eval_candidate(s, &cand[p*CFLY_N_THETA], t_horizon);
        for(int p=0;p<POP;p++) idx[p]=p;
        for(int a=0;a<ELITE;a++){ int m=a; for(int b=a+1;b<POP;b++) if(cost[idx[b]]<cost[idx[m]]) m=b; int t=idx[a];idx[a]=idx[m];idx[m]=t; }
        if(cost[idx[0]]<gbest){ gbest=cost[idx[0]]; for(int i=0;i<CFLY_N_THETA;i++) gtheta[i]=cand[idx[0]*CFLY_N_THETA+i]; }
        for(int i=0;i<CFLY_N_THETA;i++){
            double mu=0; for(int a=0;a<ELITE;a++) mu+=cand[idx[a]*CFLY_N_THETA+i]; mu/=ELITE;
            double var=0; for(int a=0;a<ELITE;a++){ double d=cand[idx[a]*CFLY_N_THETA+i]-mu; var+=d*d; } var/=ELITE;
            mean[i]=mu; sd[i]=sqrt(var)+0.02*TH_STD0[i];
        }
    }
    for(int i=0;i<CFLY_N_THETA;i++) cf->theta[i]=gtheta[i];
    fprintf(stderr, "  [cfly_replan t=%.1f big=%d] gbest=%.1f | HENTRY=%.0f ENTHR=%.2f VCUT=%.0f FRES=%.0f HLAND=%.0f KR=%.2f KV=%.2f TGO=%.2f\n",
            s->st.t, big, gbest, gtheta[TH_HENTRY], gtheta[TH_ENTHR], gtheta[TH_VCUT],
            gtheta[TH_FRES], gtheta[TH_HLAND], gtheta[TH_KR], gtheta[TH_KV], gtheta[TH_TGO]);
    free(cand); free(cost); free(idx);
}

/* init helper (called from sim_init under GM_CFLY): the warm start is the proven nominal. */
void cfly_init(Sim* s){
    for(int i=0;i<CFLY_N_THETA;i++) s->cfly.theta[i]=CFLY_NOMINAL_TH[i];
    s->cfly.next_replan_t=0.0;   /* the big t=0 solve fires on the first gtick */
    s->cfly.entry_done=s->cfly.land_on=s->cfly.coast_done=0;
    s->cfly.noreplan=0;
}
