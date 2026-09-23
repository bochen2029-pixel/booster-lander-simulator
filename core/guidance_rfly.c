/* guidance_rfly.c — GM_RFLY optimizer-in-the-loop over the native reactive stack
 * (see guidance_rfly.h for provenance + contracts; the CEM machinery mirrors the
 * Phase-A-proven guidance_cfly.c byte-clean pattern). */
#include "sim.h"
#include "guidance_rfly.h"
#include "guidance.h"
#include "constants.h"
#include "atmosphere.h"   /* E5: Mach / qbar features of the MLP gain policy */
#include "vmath.h"        /* E5: q_rot for the tilt feature */
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

/* E8 forward declarations. The rollout summary and its writers are DEFINED beside rfly_replan,
 * further down; rfly_replan_critic sits above them in this file and uses them for the phase-1.5
 * plant-labelled log. */
typedef struct { double cost; int touched, verdict; double td_v, td_lat, td_tilt, fuel_margin; } RflyEval;
static double rfly_eval_candidate_ex(const Sim* s, const double th[RFLY_N_THETA], double t_horizon, RflyEval* out);
static void   rfly_cand_log_row(const Sim* s, const RflyState* rf, int big, int it, int designed,
                                const double mean[RFLY_N_THETA], const double cand[RFLY_N_THETA], const RflyEval* ev);

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

/* E2 (2026-09-15, the C:\bl_e1 distillation experiment): two default-off levers that make the
 * search's answer a deterministic function of the state, so a student can learn it. Measured on the
 * July corpus and again on the legal 1/8-budget corpus: the theta the search returns at t=0 is
 * spread across runs by ~1.0 of the corpus width and is unpredictable from the state (R^2 ~ 0),
 * because the sampler's per-run stream picks an arbitrary point in a wide basin. See
 * C:\ONE\ONE_BOOSTER_NN-ROOT-CAUSE_2026-09-15_S3.md. Off => byte-identical.
 *   --rfly-det-stream    seed the CEM sampler from the tick alone (not the run): equal states draw
 *                        equal candidates, so the label no longer depends on the run's seed.
 *   --rfly-anchor-w W    add W * sum_i ((theta_i - identity_i)/(hi_i - lo_i))^2 to every candidate's
 *                        cost: a tie-break that returns the gentlest theta that lands, so the flat
 *                        directions of the basin stop selecting a random point. */
int    g_rfly_det_stream = 0;
double g_rfly_anchor_w   = 0.0;
/* E3: --rfly-anchor "v0,..,v9" sets BOTH the search's warm start (rfly_init) and the tie-break
 * target. With E2's constant stream the t=0 pick was deterministic but still spread across runs by
 * ~1.0 of the corpus width: the argmin among the same candidates flips with tiny state differences,
 * so the label is not a smooth function of the state and its conditional mean sits near identity,
 * the one point that crashes. Anchoring at a theta that lands on its own (D-047's constant, 67%)
 * makes every label "that constant plus the smallest consistent correction". Unset => identity
 * => rfly_init and the penalty are unchanged => byte-identical. */
double g_rfly_anchor[RFLY_N_THETA] = { 1,1,1, 1,1,1, 1,1, 0, 1 };
int    g_rfly_anchor_set = 0;
int rfly_parse_anchor(const char* s){
    double v[RFLY_N_THETA]; int n=0; const char* p=s;
    while(n<RFLY_N_THETA){ char* e; v[n]=strtod(p,&e); if(e==p) return 0; n++; if(*e==',') p=e+1; else break; }
    if(n!=RFLY_N_THETA) return 0;
    for(int i=0;i<RFLY_N_THETA;i++) g_rfly_anchor[i]=rclampd(v[i],RT_LO[i],RT_HI[i]);
    g_rfly_anchor_set=1;
    return 1;
}

/* ---- E5: the runtime-loaded MLP gain policy (see guidance_rfly.h). Default off. ---------------- */
int g_rfly_mlp_on = 0;
int g_rfly_mlp_warm = 0;   /* E6: --rfly-mlp-warm FILE — the MLP seeds the CEM mean at every replan; the search stays ON */
static int    mlp_nhid = 0;
static double mlp_wlin[RFLY_N_THETA][RFLY_MLP_NIN];
static double mlp_b2[RFLY_N_THETA];
static double mlp_w1[RFLY_MLP_MAXH][RFLY_MLP_NIN];
static double mlp_b1[RFLY_MLP_MAXH];
static double mlp_w2[RFLY_N_THETA][RFLY_MLP_MAXH];

int rfly_load_mlp(const char* path){
    FILE* f = fopen(path, "r");
    if(!f) return 0;
    int nin=0, nh=0;
    if(fscanf(f, "%d %d", &nin, &nh)!=2 || nin!=RFLY_MLP_NIN || nh<1 || nh>RFLY_MLP_MAXH){ fclose(f); return 0; }
    int ok = 1;
    for(int o=0;o<RFLY_N_THETA && ok;o++) for(int i=0;i<nin && ok;i++) ok = (fscanf(f,"%lf",&mlp_wlin[o][i])==1);
    for(int o=0;o<RFLY_N_THETA && ok;o++) ok = (fscanf(f,"%lf",&mlp_b2[o])==1);
    for(int h=0;h<nh && ok;h++) for(int i=0;i<nin && ok;i++) ok = (fscanf(f,"%lf",&mlp_w1[h][i])==1);
    for(int h=0;h<nh && ok;h++) ok = (fscanf(f,"%lf",&mlp_b1[h])==1);
    for(int o=0;o<RFLY_N_THETA && ok;o++) for(int h=0;h<nh && ok;h++) ok = (fscanf(f,"%lf",&mlp_w2[o][h])==1);
    fclose(f);
    if(!ok) return 0;
    mlp_nhid = nh;
    return 1;
}

/* Twelve LEGAL features from the nav view and the flight-state flags (never truth, never the
 * fault's future): the D-050 six, engine_on, fins_deployed, time since the sensed engine count
 * changed (saturating at 20 s; 1.0 = never), Mach/5, qbar/60 kPa, cos(tilt). Fixed evaluation
 * order, fp64, no reductions out of order: bit-deterministic like every other law here. */
void rfly_features(struct Sim* s, const State* nav, double phi[RFLY_MLP_NIN]){
    RflyState* rf = &s->rfly;
    if(rf->mlp_last_n_eng==0){ rf->mlp_last_n_eng = nav->n_eng; }
    else if(nav->n_eng != rf->mlp_last_n_eng){ rf->t_n_eng_change = nav->t; rf->mlp_last_n_eng = nav->n_eng; }
    const double* ny = nav->y;
    double rxy = sqrt(ny[S_RX]*ny[S_RX] + ny[S_RY]*ny[S_RY]);
    double vxy = sqrt(ny[S_VX]*ny[S_VX] + ny[S_VY]*ny[S_VY]);
    double vsp = sqrt(vxy*vxy + ny[S_VZ]*ny[S_VZ]);
    AtmoOut atm; atmo_eval(ny[S_RZ], &atm);
    double zb[3]={0.0,0.0,1.0}, zw[3]; q_rot(zw, &ny[S_QX], zb);
    phi[0]  = ny[S_RZ] / 62000.0;
    phi[1]  = rxy / 3000.0;
    phi[2]  = ny[S_VZ] / 1500.0;
    phi[3]  = vxy / 300.0;
    phi[4]  = (double)nav->n_eng / 3.0;
    phi[5]  = (ny[S_MLOX]+ny[S_MRP1]) / 30000.0;
    phi[6]  = nav->engine_on ? 1.0 : 0.0;
    phi[7]  = nav->fins_deployed ? 1.0 : 0.0;
    phi[8]  = (rf->t_n_eng_change < -1e8) ? 1.0 : fmin(1.0, (nav->t - rf->t_n_eng_change)/20.0);
    phi[9]  = (atm.a > 1e-9) ? (vsp/atm.a)/5.0 : 0.0;
    phi[10] = 0.5*atm.rho*vsp*vsp / 60000.0;
    phi[11] = zw[2];
}

FILE* g_rfly_cand_log = NULL;

/* ---- E7: the critic (see guidance_rfly.h). ------------------------------------------------------ */
extern double g_rfly_budget, g_rfly_pop_scale, g_rfly_iters_scale;   /* defined below rfly_eval_candidate */
int g_rfly_critic_on = 0;
static int    cr_nh = 0;
static double cr_mu[RFLY_CRITIC_NIN], cr_sd[RFLY_CRITIC_NIN];
static double cr_w1[RFLY_CRITIC_MAXH][RFLY_CRITIC_NIN], cr_b1[RFLY_CRITIC_MAXH];
static double cr_w2[RFLY_CRITIC_MAXH][RFLY_CRITIC_MAXH], cr_b2[RFLY_CRITIC_MAXH];
static double cr_w3[RFLY_CRITIC_MAXH], cr_b3, cr_ymu, cr_ysd;

int rfly_load_critic(const char* path){
    FILE* f = fopen(path, "r");
    if(!f) return 0;
    int nin=0, nh=0, ok=1;
    if(fscanf(f, "%d %d", &nin, &nh)!=2 || nin!=RFLY_CRITIC_NIN || nh<1 || nh>RFLY_CRITIC_MAXH){ fclose(f); return 0; }
    for(int i=0;i<nin && ok;i++) ok = (fscanf(f,"%lf",&cr_mu[i])==1);
    for(int i=0;i<nin && ok;i++) ok = (fscanf(f,"%lf",&cr_sd[i])==1);
    for(int h=0;h<nh && ok;h++) for(int i=0;i<nin && ok;i++) ok = (fscanf(f,"%lf",&cr_w1[h][i])==1);
    for(int h=0;h<nh && ok;h++) ok = (fscanf(f,"%lf",&cr_b1[h])==1);
    for(int h=0;h<nh && ok;h++) for(int i=0;i<nh && ok;i++) ok = (fscanf(f,"%lf",&cr_w2[h][i])==1);
    for(int h=0;h<nh && ok;h++) ok = (fscanf(f,"%lf",&cr_b2[h])==1);
    for(int h=0;h<nh && ok;h++) ok = (fscanf(f,"%lf",&cr_w3[h])==1);
    if(ok) ok = (fscanf(f,"%lf",&cr_b3)==1);
    if(ok) ok = (fscanf(f,"%lf %lf",&cr_ymu,&cr_ysd)==2);
    fclose(f);
    if(!ok) return 0;
    cr_nh = nh;
    return 1;
}

/* predicted log cost of candidate theta at the replan's features. Fixed order, fp64. */
/* E8: Q(obs39, mean, cand) -> standardised log cost. Input order MUST match the candidate-log row
 * (obs39, mean_theta, cand_theta) and the trainer's export. Dead observation channels arrive with
 * sd=1 and mu=their constant, so they contribute exactly 0. */
static double rfly_critic_eval(const double obs[RFLY_OBS_N], const double mean[RFLY_N_THETA], const double th[RFLY_N_THETA]){
    double raw[RFLY_CRITIC_NIN]; int k=0;
    for(int i=0;i<RFLY_OBS_N;i++)   raw[k++]=obs[i];
    for(int i=0;i<RFLY_N_THETA;i++) raw[k++]=mean[i];
    for(int i=0;i<RFLY_N_THETA;i++) raw[k++]=th[i];
    double x[RFLY_CRITIC_NIN];
    for(int i=0;i<RFLY_CRITIC_NIN;i++) x[i] = (raw[i]-cr_mu[i])/cr_sd[i];
    double h1[RFLY_CRITIC_MAXH], h2[RFLY_CRITIC_MAXH];
    for(int j=0;j<cr_nh;j++){ double a=cr_b1[j]; for(int i=0;i<RFLY_CRITIC_NIN;i++) a+=cr_w1[j][i]*x[i]; h1[j]=tanh(a); }
    for(int j=0;j<cr_nh;j++){ double a=cr_b2[j]; for(int i=0;i<cr_nh;i++) a+=cr_w2[j][i]*h1[i]; h2[j]=tanh(a); }
    double o=cr_b3; for(int j=0;j<cr_nh;j++) o+=cr_w3[j]*h2[j];
    double c = cr_ymu + cr_ysd*o;
    return isfinite(c) ? c : 1e12;
}

/* The CEM of rfly_replan with the plant rollouts replaced by the critic: same POP/ITERS/sd
 * schedule, same sampler stream, same elitism and best tracking. No Sim copies, no rollouts. */
void rfly_replan_critic(Sim* s, int big){
    RflyState* rf=&s->rfly;
    int POP   = (int)(((big ? 192 : 48) * g_rfly_budget) * g_rfly_pop_scale);   if(POP<8)  POP=8;
    int ITERS = (int)(((big ? 10  : 4 ) * g_rfly_budget) * g_rfly_iters_scale); if(ITERS<2)ITERS=2;
    double sd_scale = big ? 1.0 : 0.35;
    rfly_rng = g_rfly_det_stream
             ? 0xD1B54A32D192ED03ULL
             : (0xD1B54A32D192ED03ULL ^ ((unsigned long long)s->seed<<32) ^ (unsigned long long)s->st.step);
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
        for(int i=0;i<RFLY_N_THETA;i++) cand[i]=gtheta[i];
        for(int p=0;p<POP;p++) cost[p]=rfly_critic_eval(rf->obs39, mean, &cand[p*RFLY_N_THETA]);   /* E8: full obs + this iteration's mean */
        /* E8 phase 1.5 — EXPERT ITERATION FOR THE CRITIC. With --rfly-cand-log also armed, every
         * candidate the critic's search proposes is ALSO rolled out on the plant and logged with the
         * plant's cost as the label (designed=2). The search keeps using the critic's scores — no
         * RNG draw, no change to cost[]/mean/sd — so the flight is byte-identical to the same run
         * without the log. Where the critic is wrong is exactly where its own search goes; this
         * collects the plant's answer there. Retrain on the union, repeat (DAgger, for a critic). */
        if(g_rfly_cand_log){
            double t_horizon = s->st.t + 160.0; if(t_horizon < 210.0) t_horizon = 210.0;
            RflyEval* ev=(RflyEval*)malloc((size_t)POP*sizeof(RflyEval));
            int pp;
            #pragma omp parallel for schedule(dynamic)
            for(pp=0;pp<POP;pp++) rfly_eval_candidate_ex(s, &cand[pp*RFLY_N_THETA], t_horizon, &ev[pp]);
            for(int q=0;q<POP;q++) rfly_cand_log_row(s, rf, big, it, 2, mean, &cand[q*RFLY_N_THETA], &ev[q]);
            fflush(g_rfly_cand_log); free(ev);
        }
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
    fprintf(stderr, "  [rfly_critic t=%.1f big=%d] predicted log-cost %.3f | EKR=%.2f EKV=%.2f EBANK=%.2f ADEC=%.2f TLD=%.2f KDIV=%.2f KVN=%.2f IGN=%.2f TGL=%.2f KV=%.2f\n",
            s->st.t, big, gbest, gtheta[0], gtheta[1], gtheta[2], gtheta[3], gtheta[4], gtheta[5], gtheta[6], gtheta[7], gtheta[8], gtheta[9]);
    free(cand); free(cost); free(idx);
}

void rfly_mlp_theta(struct Sim* s, const State* nav, double th[RFLY_N_THETA]){
    RflyState* rf = &s->rfly;
    if(rf->mlp_last_n_eng==0){ rf->mlp_last_n_eng = nav->n_eng; }
    else if(nav->n_eng != rf->mlp_last_n_eng){ rf->t_n_eng_change = nav->t; rf->mlp_last_n_eng = nav->n_eng; }
    const double* ny = nav->y;
    double rxy = sqrt(ny[S_RX]*ny[S_RX] + ny[S_RY]*ny[S_RY]);
    double vxy = sqrt(ny[S_VX]*ny[S_VX] + ny[S_VY]*ny[S_VY]);
    double vsp = sqrt(vxy*vxy + ny[S_VZ]*ny[S_VZ]);
    AtmoOut atm; atmo_eval(ny[S_RZ], &atm);
    double zb[3]={0.0,0.0,1.0}, zw[3]; q_rot(zw, &ny[S_QX], zb);
    double phi[RFLY_MLP_NIN];
    phi[0]  = ny[S_RZ] / 62000.0;
    phi[1]  = rxy / 3000.0;
    phi[2]  = ny[S_VZ] / 1500.0;
    phi[3]  = vxy / 300.0;
    phi[4]  = (double)nav->n_eng / 3.0;
    phi[5]  = (ny[S_MLOX]+ny[S_MRP1]) / 30000.0;
    phi[6]  = nav->engine_on ? 1.0 : 0.0;
    phi[7]  = nav->fins_deployed ? 1.0 : 0.0;
    phi[8]  = (rf->t_n_eng_change < -1e8) ? 1.0 : fmin(1.0, (nav->t - rf->t_n_eng_change)/20.0);
    phi[9]  = (atm.a > 1e-9) ? (vsp/atm.a)/5.0 : 0.0;
    phi[10] = 0.5*atm.rho*vsp*vsp / 60000.0;
    phi[11] = zw[2];
    double h[RFLY_MLP_MAXH];
    for(int k=0;k<mlp_nhid;k++){ double a=mlp_b1[k]; for(int i=0;i<RFLY_MLP_NIN;i++) a += mlp_w1[k][i]*phi[i]; h[k]=tanh(a); }
    for(int o=0;o<RFLY_N_THETA;o++){
        double a = mlp_b2[o];
        for(int i=0;i<RFLY_MLP_NIN;i++) a += mlp_wlin[o][i]*phi[i];
        for(int k=0;k<mlp_nhid;k++)   a += mlp_w2[o][k]*h[k];
        th[o] = isfinite(a) ? a : RT_IDENTITY[o];
    }
    rfly_clamp_theta(th);
}

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

/* E8: the rollout's terminal summary (RflyEval, declared at the top of this file), beside its
 * cost — what the critic is trained to predict. */
static double rfly_eval_candidate_ex(const Sim* s, const double th[RFLY_N_THETA], double t_horizon, RflyEval* out){
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
    double c = rfly_cost(&c2, &R);
    /* E2 tie-break (default 0.0 => the line below never runs => byte-identical). Box-normalised
     * distance from identity, on the CLAMPED candidate the rollout actually flew. */
    if(g_rfly_anchor_w > 0.0){
        double p = 0.0;
        for(int i=0;i<RFLY_N_THETA;i++){ double d=(c2.rfly.th[i]-g_rfly_anchor[i])/(RT_HI[i]-RT_LO[i]); p += d*d; }
        c += g_rfly_anchor_w * p;
    }
    if(out){ out->cost=c; out->touched=c2.touched; out->verdict=R.verdict; out->td_v=R.td_v;
             out->td_lat=R.td_lat; out->td_tilt=R.td_tilt; out->fuel_margin=R.fuel_margin; }
    return c;
}
/* the original entry point, unchanged for every other caller */
static double rfly_eval_candidate(const Sim* s, const double th[RFLY_N_THETA], double t_horizon){
    return rfly_eval_candidate_ex(s, th, t_horizon, NULL);
}

/* E8: --rfly-cand-design (see header). Default off => the designed block never runs. */
int g_rfly_cand_design = 0;

/* E8: one 71-column row of the candidate log (layout in guidance_rfly.h, RFLY_CAND_ROW). */
static void rfly_cand_log_row(const Sim* s, const RflyState* rf, int big, int it, int designed,
                              const double mean[RFLY_N_THETA], const double cand[RFLY_N_THETA], const RflyEval* ev){
    double row[RFLY_CAND_ROW]; int k=0;
    row[k++]=s->st.t; row[k++]=(double)s->seed; row[k++]=(double)s->tap.run; row[k++]=(double)big;
    row[k++]=(double)it; row[k++]=(double)designed;
    for(int i=0;i<RFLY_OBS_N;i++)   row[k++]=rf->obs39[i];
    for(int i=0;i<RFLY_N_THETA;i++) row[k++]=mean[i];
    for(int i=0;i<RFLY_N_THETA;i++) row[k++]=rclampd(cand[i], RT_LO[i], RT_HI[i]);
    row[k++]=ev->cost;
    row[k++]=(ev->touched && (ev->verdict==V_PERFECT||ev->verdict==V_GOOD||ev->verdict==V_HARD)) ? 1.0 : 0.0;
    row[k++]=ev->td_v; row[k++]=ev->td_lat; row[k++]=ev->td_tilt; row[k++]=ev->fuel_margin;
    fwrite(row, sizeof(double), RFLY_CAND_ROW, g_rfly_cand_log);
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
    /* E2: with --rfly-det-stream every replan draws the SAME relative candidate offsets (a constant
     * stream), so the pick is a deterministic function of (current mean, state) and two runs in the
     * same state choose the same theta. A tick-seeded stream was tried first and is wrong here: after
     * an event replan every later replan sits on a fault-time-dependent tick, so post-fault labels
     * would still differ across runs. Off => the original (seed, step) stream exactly. */
    rfly_rng = g_rfly_det_stream
             ? 0xD1B54A32D192ED03ULL
             : (0xD1B54A32D192ED03ULL ^ ((unsigned long long)s->seed<<32) ^ (unsigned long long)s->st.step);

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
        RflyEval* ev=(RflyEval*)malloc((size_t)POP*sizeof(RflyEval));
        #pragma omp parallel for schedule(dynamic)
        for(p=0;p<POP;p++) cost[p]=rfly_eval_candidate_ex(s, &cand[p*RFLY_N_THETA], t_horizon, &ev[p]);
        /* E8: the search's JUDGMENT, logged — one 71-column row per candidate: the full legal
         * observation at this replan, the CEM mean it was drawn around, the candidate as flown, and
         * the rollout's cost + terminal summary. Default NULL => skipped => byte-identical. */
        if(g_rfly_cand_log){
            for(int q=0;q<POP;q++) rfly_cand_log_row(s, rf, big, it, 0, mean, &cand[q*RFLY_N_THETA], &ev[q]);
            /* E8: the DESIGNED set, iteration 0 only (mean == the replan's start point there):
             * the mean itself, then +-0.5 and +-1.5 sd one-coordinate steps on each gain — 41
             * rollouts. No RNG draws, never enters elite selection: the flight is byte-identical. */
            if(g_rfly_cand_design && it==0){
                static const double STEPS[4] = { -1.5, -0.5, 0.5, 1.5 };
                const int ND = 1 + 4*RFLY_N_THETA;
                double*   dc=(double*)malloc((size_t)ND*RFLY_N_THETA*sizeof(double));
                RflyEval* de=(RflyEval*)malloc((size_t)ND*sizeof(RflyEval));
                for(int i=0;i<RFLY_N_THETA;i++) dc[i]=mean[i];
                for(int i=0;i<RFLY_N_THETA;i++) for(int kk=0;kk<4;kk++){
                    int d=1+i*4+kk;
                    for(int j=0;j<RFLY_N_THETA;j++) dc[d*RFLY_N_THETA+j]=mean[j];
                    dc[d*RFLY_N_THETA+i]=rclampd(mean[i]+STEPS[kk]*sd[i], RT_LO[i], RT_HI[i]);
                }
                int d;
                #pragma omp parallel for schedule(dynamic)
                for(d=0;d<ND;d++) rfly_eval_candidate_ex(s, &dc[d*RFLY_N_THETA], t_horizon, &de[d]);
                for(int q=0;q<ND;q++) rfly_cand_log_row(s, rf, big, it, 1, mean, &dc[q*RFLY_N_THETA], &de[q]);
                free(dc); free(de);
            }
            fflush(g_rfly_cand_log);   /* E7's torn-file race: the manifest must never outrun the rows */
        }
        free(ev);
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
    for(int i=0;i<RFLY_N_THETA;i++) s->rfly.th[i]= g_rfly_anchor_set ? g_rfly_anchor[i] : RT_IDENTITY[i];   /* E3: anchored warm start; unset => identity exactly */
    s->rfly.next_replan_t=0.0;
    s->rfly.noreplan=0;
    s->rfly.last_n_eng=0;   /* D-052: 0 = not yet observed; arms on the first gtick without firing */
    s->rfly.t_n_eng_change=-1e9; s->rfly.mlp_last_n_eng=0;   /* E5: the MLP policy's own memory */
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
