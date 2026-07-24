/* sim.c — the fixed-step driver. */
#include "sim.h"
#include "integrator.h"
#include "contact.h"
#include "control.h"
#include "guidance_hoverslam.h"
#include "guidance_mppi.h"
#include "guidance_neural.h"   /* N1 §9.8: tier-3 GM_NEURAL learned policy (default OFF => dead code) */
#include "guidance_cfly.h"    /* N2-S2: GM_CFLY optimizer-in-the-loop (default OFF => dead code) */
#ifdef BL_HAVE_CUDA
#include "guidance_mppi_cuda.h"   /* M5: --mppi-cuda routes the full-solve to the GPU (CUDA build only) */
#endif
#include "atmosphere.h"
#include "constants.h"
#include "rng.h"
#include <math.h>
#include <string.h>

/* M5 CUDA MPPI flag (set from the CLI in main.c). When 1 AND guidance_mode==GM_MPPI, the full
 * replan goes through mppi_step_cuda (GPU rollout); otherwise the CPU mppi_step is used. The
 * between-solve mppi_execute and everything else are identical either way. Default 0 -> CPU path
 * byte-identical (directive: CPU stays default and unchanged when the flag is off).
 * The flag exists in BOTH builds: in a CPU-only (no-CUDA) build main.c refuses --mppi-cuda at the
 * CLI, so this stays 0 and the GPU branch below is compiled out entirely. */
int g_mppi_use_cuda = 0;
/* E1 (D-029): when 1 AND the run uses the MPPI planner (GM_MPPI, or the GM_NEURAL DAgger shadow),
 * the planner warm-starts its mean from the STUDENT policy instead of the hoverslam recipe — the
 * expert-iteration COMPOSITE operator (the neural-warm-start teacher for the engine-out axis,
 * expert_iteration_design.md §2). Set from the CLI in main.c (--mppi-warm-neural); copied into
 * MppiState.warm_neural at sim_init. 0 => byte-identical (the §13.6 leak gate). */
int g_mppi_warm_neural = 0;
/* E2' (D-032): when 1 (and --policy-log under GM_NEURAL), the DAgger shadow logs the REACTIVE
 * (hoverslam) divert a_lat as the teacher label instead of running the MPPI shadow — for distilling
 * the BEST engine-out teacher. D-031 found the MPPI teacher (~10%) is WORSE than the student on EO
 * (v6 8/60 ≈ 13%) so distilling it regressed; reactive (hoverslam+D-030) is 9–10/60, the best. Set
 * from main.c (--shadow-reactive). 0 => the MPPI shadow (D-023) => byte-identical. */
int g_shadow_reactive = 0;

double sim_body_tilt(const State* st){
    double zb[3]={0,0,1}, zw[3]; q_rot(zw,&st->y[S_QX],zb);
    double c=zw[2]; if(c>1)c=1; if(c<-1)c=-1; return acos(c);
}

/* N0 ENGINE-OUT (§4.6, engineout_design §2.3): the surviving-cluster geometric centroid, body
 * lateral. Layout = center engine on the axis (0,0) + a symmetric side pair at (+R,0)/(−R,0). One
 * failure removes an engine; the centroid of the survivors is where the resultant thrust acts.
 *   center out (k=0): survivors (+R,0),(−R,0) -> centroid (0,0)      => ON-axis, NO induced torque
 *   side out   (k=1): survivors (0,0),(−R,0)  -> centroid (−R/2,0)   => OFF-axis, induced torque
 *   side out   (k=2): survivors (0,0),(+R,0)  -> centroid (+R/2,0)   => mirror
 * Pure function; fires once per run (no rollout-hot-path concern). */
static void survivor_centroid(int n_eng_before, int failed_k, double c[2]){
    const double R = ENG_RING_R;
    double px[3]={0.0, +R, -R}, py[3]={0.0, 0.0, 0.0};   /* center, side+, side- */
    double sx=0.0, sy=0.0; int ns=0;
    for(int i=0;i<n_eng_before && i<3;i++){
        if(i==failed_k) continue;
        sx+=px[i]; sy+=py[i]; ns++;
    }
    if(ns>0){ c[0]=sx/ns; c[1]=sy/ns; } else { c[0]=c[1]=0.0; }
}

/* N0 SEEDED MOVABLE TARGET (§4.5, target_sandbox_design §A.1b): a deterministic closed-form
 * HORIZONTAL target drift (the SEEDED source — the training source; Stage-1a "pad drift", the
 * cleanest moving target — no vertical/contact coupling at N0). PURE function of (config, t) exactly
 * like wind_sample's mean profile => replay-safe by construction (no filter memory). The `target`
 * Philox stream keys the amplitude/rate/phase from (seed,run) so an armed run replays bit-exact and
 * different seeds move differently. tgt_mode==0 (default) => origin => byte-identical. */
static void target_sample(const Sim* s, double t, double out_xy[2], double out_vxy[2]){
    out_xy[0]=out_xy[1]=0.0; out_vxy[0]=out_vxy[1]=0.0;
    if(s->tgt_mode==0) return;
    if(s->tgt_mode==1){
        /* circular drift: a slow seeded wander around the origin (the ±few-m station-keeping analog) */
        double a=s->tgt_amp, w=s->tgt_omega, ph0=s->tgt_phase[0], ph1=s->tgt_phase[1];
        out_xy[0]  =  a*cos(w*t+ph0);
        out_xy[1]  =  a*sin(w*t+ph1);
        out_vxy[0] = -a*w*sin(w*t+ph0);
        out_vxy[1] =  a*w*cos(w*t+ph1);
    } else if(s->tgt_mode==2){
        /* linear ramp along a seeded bearing (a scripted pad drift) */
        double a=s->tgt_amp, w=s->tgt_omega;   /* here w reused as ramp speed [1/s]; a as reach [m] */
        double dx=cos(s->tgt_phase[0]), dy=sin(s->tgt_phase[0]);
        double f=w*t; if(f>1.0) f=1.0;         /* saturate at full reach */
        out_xy[0]=a*f*dx; out_xy[1]=a*f*dy;
        if(f<1.0){ out_vxy[0]=a*w*dx; out_vxy[1]=a*w*dy; }
    }
}

/* Mean wind profile (world horizontal), altitude-scaled. */
static void wind_sample(Sim* s, double h, double out[3]){
    out[0]=out[1]=out[2]=0.0;
    double u=s->se.u_ref;
    if(u<=0.0) return;
    double hh = h<10.0?10.0:h;
    double scale = pow(hh/10.0, 0.14);
    if(h>1000.0){ double t=(h-1000.0)/10000.0; if(t>1)t=1; scale *= (1.0+1.2*t); }
    if(h>25000.0) scale *= 0.4;
    double sp=u*scale;
    out[0]=sp*cos(s->se.wind_az); out[1]=sp*sin(s->se.wind_az);
    /* Dryden turbulence (module) — low-altitude first-order approx on horizontal */
    if(s->modules & MOD_TURB){
        double W20 = s->se.w20_kt*0.514444;   /* kt->m/s */
        double hb = h<3.0?3.0:h;
        double hft = hb/0.3048;
        double Lu = hft/pow(0.177+0.000823*hft,1.2)*0.3048;
        double sw = 0.1*W20;
        double su = sw/pow(0.177+0.000823*hft,0.4);
        double V = 60.0;  /* nominal airspeed scale */
        double beta = V*DT/Lu; if(beta>1)beta=1;
        for(int k=0;k<2;k++){
            double eta = rng_normal(s->seed, RNG_WIND, (uint32_t)s->st.step, (uint32_t)k, 0);
            s->st.wind_filt[k] = (1.0-beta)*s->st.wind_filt[k] + su*sqrt(2.0*beta)*eta;
            out[k]+=s->st.wind_filt[k];
        }
    }
    /* DIAL-A-GUST (canon §4.3 layer 3, §10.6 INJECT_DISTURBANCE type=gust). Discrete 1-cosine
     * wind-shear pulse SUPERPOSED on the mean+Dryden wind, in a FIXED horizontal direction. Pure
     * function of altitude h (== the vehicle CoM world-Z, the same argument the mean/Dryden layers
     * use) — NO RNG, so it neither perturbs the seeded turbulence stream above nor breaks replay:
     * (seed, gust) is bit-identical run-to-run, and peak==0 skips the block entirely => byte-exact
     * to the no-gust tree (the TERMINAL-194 parity gate). The vehicle penetrates the band
     * [alt-hw, alt+hw]; over that penetration distance dm=2*hw the pulse magnitude follows the
     * canonical 1-cosine  V = 0.5*peak*(1 - cos(2*pi*x/dm)),  x = h-(alt-hw) in [0, 2*hw]:
     *   0 at the band edges, rising to +peak at band center (x=hw), back to 0 at the far edge.
     * Guidance never sees it (§4.3): the plant integrates v_rel = v - wind_world; the MPPI planner
     * zeroes env.wind_world in its rollouts (guidance_mppi.c), so it re-solves purely against the
     * lateral drift the gust induces — the honest wind-shear-rejection test. */
    if(s->gust.peak != 0.0 && s->gust.hw > 0.0){
        double x = h - (s->gust.alt - s->gust.hw);
        if(x > 0.0 && x < 2.0*s->gust.hw){
            double V = 0.5*s->gust.peak*(1.0 - cos((PI*x)/s->gust.hw));  /* 2*pi*x/(2*hw) = pi*x/hw */
            out[0] += V*s->gust.dirx;
            out[1] += V*s->gust.diry;
        }
    }
}

/* DIAL-A-GUST arming (call AFTER sim_init, which memsets s->gust to OFF). Stores the fixed
 * horizontal direction as a unit vector from the bearing dir_deg (0 => +x). Disarms (peak=0) on a
 * non-positive peak or half-width so a malformed/absent flag leaves the run byte-identical. */
void sim_set_gust(Sim* s, double peak, double alt, double hw, double dir_deg){
    if(peak<=0.0 || hw<=0.0){ s->gust.peak=0.0; return; }
    s->gust.peak=peak; s->gust.alt=alt; s->gust.hw=hw;
    double th=dir_deg*DEG2RAD;
    s->gust.dirx=cos(th); s->gust.diry=sin(th);
}

void sim_init(Sim* s, int scenario, uint32_t seed, uint32_t run_idx, int modules, int guidance_mode){
    memset(s,0,sizeof(*s));
    s->seed=seed; s->modules=modules; s->scenario=scenario; s->guidance_mode=guidance_mode;
    scenario_init(&s->st, scenario, seed, run_idx, &s->se);
    s->env.module_mask=modules; s->env.fins_deployed=s->st.fins_deployed;
    s->env.wind_world[0]=s->env.wind_world[1]=s->env.wind_world[2]=0.0;
    /* INJECT_DISTURBANCE (Tier-B, Agent C F4): up to -8% thrust, -1% Isp, 2 cm CoM offset. Seeded
     * per (seed,run) so disturbed runs replay bit-exact (directive 4). Nominal (1,1,0) otherwise. */
    s->env.thrust_scale=1.0; s->env.isp_scale=1.0; s->env.com_offset[0]=s->env.com_offset[1]=0.0;
    if(modules & MOD_INJECT){
        double u1=rng_u01((uint32_t)(seed+run_idx*2654435761u+101u));
        double u2=rng_u01((uint32_t)(seed+run_idx*2654435761u+202u));
        double u3=rng_u01((uint32_t)(seed+run_idx*2654435761u+303u));
        s->env.thrust_scale=1.0-0.08*u1;
        s->env.isp_scale   =1.0-0.01*u2;
        double ca=6.2831853071795864*u3;
        s->env.com_offset[0]=0.02*cos(ca); s->env.com_offset[1]=0.02*sin(ca);
    }
    s->impact_v=s->impact_tilt=s->impact_lat=0.0; s->impact_target_xy[0]=s->impact_target_xy[1]=0.0;
    s->max_qbar=0; s->peak_qdot=0; s->done=0; s->touched=0;
    s->gcmd.mode=guidance_mode; s->gcmd.n_eng=1;
    if(guidance_mode==GM_RFLY) rfly_init(s);    /* D-040 pivot: identity warm-start + replan arming */
    if(guidance_mode==GM_CFLY){ cfly_init(s);   /* N2-S2 (D-040): theta warm-start + replan arming */
        s->st.relights_left=4; }   /* GM_CFLY coast-relight needs >2 ignitions (entry + landing + 2 coast
                                 * cycles); mode-scoped plant config (Merlin-class relight budget).
                                 * guidance_mode!=GM_CFLY => stays 2 => byte-identical. */
    /* N0 §8.1 WIDE SOCKET nominal fill (memset zeroed everything; nominal needs valid=1, all engines
     * healthy). target=origin, vxy=0, deck_z=0, src=FIXED, age=0, valid=1; cov=tiny const so the
     * renderer's uncertainty ellipse is a point, not degenerate. These ride truth st; nav passes them
     * through. gcmd.target_xy stays (0,0) => hoverslam/MPPI null the origin (v1 behavior, byte-exact).
     * The seeded target (§4.5) / engine-out (§4.6) below OVERWRITE these when armed. */
    for(int i=0;i<3;i++) s->st.eng_health[i]=1;                 /* all healthy */
    s->st.tgt.target_xy[0]=s->st.tgt.target_xy[1]=0.0;          /* FIXED at origin */
    s->st.tgt.target_vxy[0]=s->st.tgt.target_vxy[1]=0.0;
    s->st.tgt.target_cov[0]=s->st.tgt.target_cov[1]=1e-4;       /* σ_target^2 tiny (near-certain) */
    s->st.tgt.target_cov[2]=0.0;                                /* xy covariance */
    s->st.tgt.deck_z=0.0;
    s->st.tgt.target_age=0.0;
    s->st.tgt.target_src=TGT_FIXED;
    s->st.tgt.target_valid=1;
    s->gcmd.target_xy[0]=s->gcmd.target_xy[1]=0.0;
    s->gcmd.target_vxy[0]=s->gcmd.target_vxy[1]=0.0;
    /* N0 engine-out event state (armed via --engine-out; default OFF => never fires => byte-identical) */
    s->eo_engine=-1; s->eo_time=0.0; s->eo_fired=0;
    /* N0 seeded-target state (armed via --target; default OFF => FIXED origin => byte-identical) */
    s->tgt_mode=0; s->tgt_seed=seed; s->tgt_run=run_idx;
    s->tgt_amp=0.0; s->tgt_omega=0.0; s->tgt_phase[0]=s->tgt_phase[1]=0.0;
    /* §8.1 measurement layer: NAV_TRUTH pass-through by default; NAV_NOISY under
     * MOD_NAV_NOISY. Keyed by (seed, run_idx) for replayable per-run noise. (D-010) */
    nav_init(&s->nav, modules, seed, run_idx);
    /* HIER MPPI planner (track 4-B). Also initialized under GM_NEURAL for the DAgger SHADOW
     * TEACHER (D-023): with --policy-log armed, the neural block runs MPPI in shadow to label
     * the states the POLICY visits (neural_policy_design §B.1). Init alone touches only
     * s->mppi (no other stream/state), so an un-tapped --neural flight is byte-identical. */
    if(guidance_mode==GM_MPPI || guidance_mode==GM_NEURAL){
        mppi_init(&s->mppi, seed, scenario);
        s->mppi.warm_neural = g_mppi_warm_neural;   /* E1 (D-029): composite warm-start arm; 0 => byte-identical */
    }
}

static void set_verdict(Sim* s){
    State* st=&s->st;
    /* Target Stage-1 (D-034 §A.3): the on-pad test is target-relative — distance from the target pose
     * latched at first contact (impact_target_xy). (0,0) for FIXED => sqrt(rx^2+ry^2) => byte-identical. */
    double dlx=st->y[S_RX]-s->impact_target_xy[0], dly=st->y[S_RY]-s->impact_target_xy[1];
    double lat = sqrt(dlx*dlx+dly*dly);
    double tilt = sim_body_tilt(st);
    double maxcrush=0; for(int i=0;i<4;i++) if(st->crush[i]>maxcrush)maxcrush=st->crush[i];
    double crush_frac=maxcrush/LEG_CRUSH_S;
    int on_pad = (lat<=PAD_RADIUS);
    double iv=s->impact_v;
    int grade;
    if(iv>TD_V_HARD || !on_pad) grade=V_CRASHED;
    else if(tilt>30.0*DEG2RAD) grade=V_TIPPED;
    else if(iv<=TD_V_PERFECT && lat<=TD_LAT_PERFECT && tilt<=TD_TILT_PERFECT && crush_frac<0.10) grade=V_PERFECT;
    else if(iv<=TD_V_GOOD && lat<=TD_LAT_GOOD && tilt<=TD_TILT_GOOD) grade=V_GOOD;
    else if(iv<=TD_V_HARD) grade=V_HARD;
    else grade=V_CRASHED;
    st->verdict=grade;
    if(grade==V_CRASHED) st->phase=PH_CRASHED;
    else if(grade==V_TIPPED) st->phase=PH_TIPPED;
    else st->phase=PH_LANDED;
}

/* ENTRY-BURN SUPERVISOR (E3, DECISIONS D-007). Sits ABOVE hoverslam. A high-energy entry (62 km,
 * ~1.5 km/s) would blow through the structural qbar line (measured: STRUCT at ~16 km, 105 kPa) if
 * left to a futile 1-engine hoverslam ignition at altitude. This runs a predictive-peak-qbar
 * 3-engine RETROGRADE burn (Agent A §9): ignite while the ballistic-to-touchdown peak qbar would
 * breach the line, cut once the remaining descent is predicted safe. Decelerating up high means the
 * downstream landing burn happens at low qbar where thrust dominates aero (the burn-phase-aero fix).
 * Cross-range divert is deferred to the aero-descent (Agent A §10). Returns 1 if it commanded
 * guidance this tick (skip hoverslam), 0 to let hoverslam run. Inert for low-energy scenarios
 * (TERMINAL/AERO_OFFSET predict < IGNITE, so gcmd/state untouched -> determinism preserved). */
/* D-009 ENTRY divert support: crude ballistic time-to-ground from (h, vz) with drag CA~1.0 —
 * deliberately a LOW estimate (the landing burn extends the real time; the study shows a low t_go
 * TIGHTENS the ZEM/ZEV null, the safe direction — lands for tgo_err down to 0.70). Coarse dt: a
 * trigger-grade shoot, not the plant. */
static double entry_tgo_estimate(double h, double vz, double m){
    double v=vz, t=0.0; const double dt=0.5;
    for(int i=0;i<600 && h>0.0;i++){
        AtmoOut atm; atmo_eval(h,&atm);
        double gh=G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));
        double a_drag=0.5*atm.rho*v*v*VEH_AREF*1.0/m;
        double a=-gh + ((v<0.0)? a_drag : -a_drag);
        v+=a*dt; h+=v*dt; t+=dt;
    }
    if(t<1.0) t=1.0;
    return t;
}

/* D-009 ENTRY-burn collision-course divert — ZEM/ZEV optimal-terminal bank (design:
 * runs/d009_entry_divert_design.md; feasibility study runs/sandbox/entrydiv.c: nominal 3 km closes
 * to ~5.8 m / -4.8 m/s, 8/9 across the dispersion grid). Per world-lateral axis:
 *     a_cmd = Kg * ( -6 r / t_go^2  -  4 v / t_go )
 * The FIRST term builds the collision course (position closure); the SECOND times the REVERSAL so
 * r and v null together — the piece both hand-tuned banking attempts (17 km naive / 2363 m
 * velocity-null sqrt-decel, D-007/D-009) were missing. Kg=6 front-loads the reversal above the
 * landing-burn fade. Saturated at the 15-deg bank of the 3-engine burn. Called ONLY while
 * PH_ENTRY_BURN -> TERMINAL/AERO_OFFSET never see it (determinism preserved by construction). */
/* SPLIT, OVERDAMPED gains (D-009; the D-002 zeta=1.1 lesson at the entry scale). Uniform Kg scaling
 * breaks the terminal boundary condition: Kg=6 front-loads closure and crosses r=0 mid-coast still
 * carrying -29 m/s (overshoot bias ~150 m, med 148 tight); Kg=1 preserves the timing but barely uses
 * the burn's authority (med 536, far seeds under-driven). KR>1 spends real authority on closure;
 * KV>KR overdamps the approach so r->0 is asymptotic (no crossing), arriving slow and aligned for
 * the aero trim. */
#define ENTRY_DIVERT_KR 2.0      /* position-closure gain (x the optimal -6r/tgo^2) */
#define ENTRY_DIVERT_KV 3.5      /* velocity-null gain   (x the optimal -4v/tgo) — overdamped */
/* D-030 engine-out entry-divert RE-AUTHORIZATION (applied ONLY when n_eng<3; n_eng==3 => 15°/KR/KV
 * EXACTLY => the clean entry burn is byte-identical, the §13.6 leak gate). Rationale (D-029
 * phase-attribution): the gross EO cluster is lost at the entry-burn CUT — the 2-engine ZEM/ZEV divert
 * closes only ~830 m and carries +22.9 m/s OUTBOUND (under-driven AND under-damped-for-the-regime), its
 * authority `amax = n_eng·thrust·sin(15°)/m` down to 2/3. The entry burn runs at LOW qbar (~0.2-40 kPa vs
 * the ~80 kPa STRUCT line) => huge headroom to OPEN the bank cap. FROZEN sweep winner (neural EO ×60 s42
 * 1/60 -> 8/60, reactive 0/60 -> 9/60, generalizes to held-out s7/s99): effective KR 8.0 / KV 8.75
 * (near-critical) vs the baseline 2.0/3.5 (ratio 1.75, overdamped) — under 2-engine the divert needs
 * FASTER closure with LESS over-damping to beat the qbar/fuel cut. (Tuned via a getenv sweep, then frozen
 * here; more/less aggression both measured worse: 45/8/4 -> 4, 35/5/2 -> 0, 35/4/3 -> 3.) */
#define EO_DIVERT_BANK_DEG 35.0   /* bank cap opened from 15° under n_eng<3 (low-qbar entry burn, STRUCT-safe) */
#define EO_DIVERT_KR_MUL   4.0    /* × ENTRY_DIVERT_KR under n_eng<3 (faster position closure) */
#define EO_DIVERT_KV_MUL   2.5    /* × ENTRY_DIVERT_KV under n_eng<3 (keep near-critical, not over-damped) */
static void entry_divert_step(const State* st, GuidanceCmd* g){
    const double* y=st->y;
    MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
    AtmoOut atm; atmo_eval(y[S_RZ],&atm);
    /* N0 ENGINE-OUT (§4.6, engineout_design §C.3): the entry divert authority is n_eng-sized. Was the
     * literal 3.0; now (double)st->n_eng so a mid-burn engine-out shrinks the ZEM/ZEV divert (the
     * honest "burn longer with less authority" — far-offset seeds may become un-divertable, directive
     * 3). BYTE-SAFETY: with no engine-out, st->n_eng == 3 throughout the entry burn (nothing decrements
     * it), so this == 3.0 exactly — verified by the ENTRY baseline reproducing 95/100. st->n_eng is the
     * §4.3-legal sensed firing count (nav pass-through), not privileged information. */
    double a_burn = (double)st->n_eng*engine_thrust(1.0, atm.p)/mp.m;   /* ~50 m/s^2 near-vacuum (3 eng) */
    /* D-030: under engine-out (n_eng<3) OPEN the bank cap (recover the lost sin-authority in the low-qbar
     * entry burn) and stiffen the ZEM/ZEV gains. n_eng==3 => 15°/KR/KV exactly => byte-identical. */
    double bank_deg = 15.0, kr = ENTRY_DIVERT_KR, kv = ENTRY_DIVERT_KV;
    if(st->n_eng>0 && st->n_eng<3){ bank_deg = EO_DIVERT_BANK_DEG; kr = ENTRY_DIVERT_KR*EO_DIVERT_KR_MUL; kv = ENTRY_DIVERT_KV*EO_DIVERT_KV_MUL; }
    /* GM_RFLY per-scenario override (guidance_rfly.h). rt_on==0 => ×1.0 exactly => byte-identical;
     * armed, the bank cap is still ceilinged at 45 deg (the STRUCT-safe envelope D-030 mapped). */
    kr *= rt_gain(g,RT_EKR); kv *= rt_gain(g,RT_EKV); bank_deg *= rt_gain(g,RT_EBANK);
    if(bank_deg>45.0) bank_deg=45.0;
    double amax = a_burn*sin(bank_deg*DEG2RAD);
    double t_go = entry_tgo_estimate(y[S_RZ]-mp.com, y[S_VZ], mp.m);
    double rr[2]={y[S_RX],y[S_RY]}, vv[2]={y[S_VX],y[S_VY]};
    for(int ax=0; ax<2; ax++){
        double a_cmd = kr*(-6.0*rr[ax]/(t_go*t_go)) + kv*(-4.0*vv[ax]/t_go);
        if(a_cmd> amax) a_cmd= amax;
        if(a_cmd<-amax) a_cmd=-amax;
        g->a_lat[ax]=a_cmd;
    }
}

#define ENTRY_QBAR_IGNITE  72000.0   /* predicted peak qbar to arm the entry burn [Pa] */
#define ENTRY_QBAR_CUT     68000.0   /* predicted remaining peak qbar to cut the burn [Pa] */
#define ENTRY_FUEL_FLOOR   7000.0    /* kg: reserve at least this for the landing burn */
/* §8.1 (D-010): `nav` is the measurement view (r/v/q/w = estimate, rest = truth). All GUIDANCE
 * readouts — the peak-qbar predictor inputs, the ZEM/ZEV divert, the t_go — read `nav`; the PLANT
 * state machine (phase, engine latch/cut) writes truth via `s->st`. In NAV_TRUTH `nav` IS a byte
 * copy of truth, so this path stays bit-identical. */
static int entry_supervisor(Sim* s, const State* nav){
    State* st=&s->st;
    if(st->phase!=PH_COAST && st->phase!=PH_ENTRY_BURN) return 0;   /* only pre-landing regimes (truth) */
    MassProps mp; mass_props(nav->y[S_MLOX],nav->y[S_MRP1],0,0,&mp);   /* mass = truth (pass-through) */
    double vx=nav->y[S_VX],vy=nav->y[S_VY],vz=nav->y[S_VZ];            /* velocity = estimate */
    double speed=sqrt(vx*vx+vy*vy+vz*vz);
    double fuel=nav->y[S_MLOX]+nav->y[S_MRP1];                         /* fuel = truth */
    double qpk=entry_predict_peak_qbar(nav->y[S_RZ], speed, mp.m);     /* altitude = estimate */

    if(st->phase==PH_ENTRY_BURN){
        if(qpk<=ENTRY_QBAR_CUT || fuel<=ENTRY_FUEL_FLOOR){
            /* CUT: shut down, hand off to the unpowered aero-descent (hoverslam takes it) */
            st->engine_on=0; st->ign_timer=-1.0; st->n_eng=1;
            s->gcmd.engine_cmd=0; s->gcmd.throttle=0.0; s->gcmd.n_eng=1;
            s->gcmd.a_lat[0]=s->gcmd.a_lat[1]=0.0;
            st->phase=PH_AERO;
            return 0;
        }
        /* continue: 3-engine, full throttle, ~retrograde with the D-009 ZEM/ZEV collision-course
         * BANK (entry_divert_step above; design runs/d009_entry_divert_design.md). Two hand-tuned
         * banks failed before (naive constant -> 17 km off; velocity-null sqrt-decel -> 2363 m,
         * worse than retrograde's 2050 m): both MISTIMED THE REVERSAL. The ZEM/ZEV pair nulls
         * position AND velocity together, so the burn + the ~87 s coast drift land ON the pad
         * instead of flying past it. The E3 cut logic is untouched (qbar/fuel-floor binds; the
         * divert never modulates the cut). */
        s->gcmd.mode=GM_HOVERSLAM; s->gcmd.engine_cmd=1; s->gcmd.throttle=1.0; s->gcmd.n_eng=3;
        entry_divert_step(nav, &s->gcmd);   /* divert reads the estimate (§8.1) */
        s->gcmd.deploy_cmd=0; s->gcmd.solver_flags=0;
        s->gcmd.t_go=(vz<-0.1)?(nav->y[S_RZ]/(-vz)):5.0;
        return 1;
    }
    /* PH_COAST: arm the entry burn iff the ballistic descent would over-pressure and we can spare
     * the fuel (below the floor a burn is worse than the aero-descent's own qbar management). */
    if(qpk>=ENTRY_QBAR_IGNITE && fuel>ENTRY_FUEL_FLOOR){
        st->phase=PH_ENTRY_BURN;
        s->gcmd.mode=GM_HOVERSLAM; s->gcmd.engine_cmd=1; s->gcmd.throttle=1.0; s->gcmd.n_eng=3;
        s->gcmd.a_lat[0]=s->gcmd.a_lat[1]=0.0; s->gcmd.deploy_cmd=0; s->gcmd.solver_flags=0;
        return 1;
    }
    return 0;
}

int sim_step(Sim* s){
    State* st=&s->st;
    if(s->done) return 0;

    /* advance ignition timer */
    if(st->engine_on && st->ign_timer>=0.0) st->ign_timer += DT;

    /* ---- N0 ENGINE-OUT (§4.6, engineout_design §E.2): a seeded, time-triggered failure. Fires ONCE
     * when armed (MOD_ENGINE_OUT) and t reaches eo_time during a MULTI-engine burn (only a burn with
     * >1 engine can survive/absorb it — the 3-engine entry burn is the money regime). On fire: drop
     * n_eng (thrust + mdot + gimbal-allocation denom all scale via st->n_eng), set the survivor-
     * centroid thrust_offset (the induced torque rides the existing arm_thr lever), flag the failed
     * engine's chamber-P health (the §4.3-legal signal guidance may read), and emit EVT FAULT. No
     * runtime RNG => replayable. Absent flag => never enters => byte-identical. */
    if((s->modules & MOD_ENGINE_OUT) && !s->eo_fired && s->eo_engine>=0 && s->eo_engine<3
       && st->t >= s->eo_time && st->engine_on && st->n_eng > 1){
        s->eo_fired=1;
        double c[2]; survivor_centroid(st->n_eng, s->eo_engine, c);
        st->n_eng = st->n_eng - 1;                          /* 3 -> 2 (thrust/mdot/allocation follow) */
        s->env.thrust_offset[0]=c[0]; s->env.thrust_offset[1]=c[1];  /* induced torque (arm_thr) */
        st->eng_health[s->eo_engine]=0;                     /* chamber-P health flag (§8.1 LEGAL) */
    }

    /* ---- N0 SEEDED MOVABLE TARGET (§4.5, target_sandbox_design §B.3): write the target slot from the
     * seeded closed form each step (like wind_sample), fill the guidance-visible target (gcmd) so
     * hoverslam/MPPI null (r − target_xy), and stream it as the nav TargetEstimate (src=SEEDED). Absent
     * (tgt_mode==0) => origin, gcmd.target_xy stays (0,0) => guidance nulls the origin => byte-identical. */
    if(s->modules & MOD_TARGET){
        double txy[2], tvxy[2];
        target_sample(s, st->t, txy, tvxy);
        s->gcmd.target_xy[0]=txy[0];  s->gcmd.target_xy[1]=txy[1];
        s->gcmd.target_vxy[0]=tvxy[0]; s->gcmd.target_vxy[1]=tvxy[1];
        /* nav TargetEstimate view (truth pass-through at N0 — no target noise, §8.4) */
        st->tgt.target_xy[0]=txy[0];  st->tgt.target_xy[1]=txy[1];
        st->tgt.target_vxy[0]=tvxy[0]; st->tgt.target_vxy[1]=tvxy[1];
        st->tgt.target_src=TGT_SEEDED; st->tgt.target_valid=1; st->tgt.target_age=0.0;
    }

    /* ---- SEA (§4.4 / target_sandbox_design §A.2, D-035): the heaving ASDS deck moves in the PLANT.
     * Each physics step, overwrite the (static scenario) deck level with the live closed-form heave
     * deck_z(t) and expose deck_vz(t) for the deck-relative leg-load coupling (contact_substep below).
     * Guidance does NOT read deck_z (§1.2: hoverslam uses height above z=0), so the vehicle feels the
     * deck only through the contact event — the honest "land on a moving deck" physics. Absent (MOD_SEA
     * unset) => se.deck_z stays the scenario scalar and deck_vz_live stays 0 => byte-identical. */
    if(s->modules & MOD_SEA){
        double dz, dvz, tx, ty;
        sea_deck_pose(&s->sea, st->t, &dz, &dvz, 0, &tx, &ty);
        s->se.deck_z = dz;
        s->deck_vz_live = dvz;
        s->gcmd.deck_z = dz;      /* §A.4 Option-i: guidance nulls its height against the current deck pose */
        s->st.tgt.deck_z = dz;    /* §8.1 nav socket: deck pose is part of NavState (renderer/telemetry) */
        /* Stage-1c (D-036) horizontal station: the deck's slow ±wander drives the guidance target, exactly
         * like the MOD_TARGET seeded drift (hoverslam/MPPI null r_xy − target_xy). Wander off => tx=ty=0 =>
         * gcmd.target_xy stays (0,0) => guidance nulls the origin => byte-identical to Stage-1b heave-only. */
        s->gcmd.target_xy[0]=tx; s->gcmd.target_xy[1]=ty;
        s->st.tgt.target_xy[0]=tx; s->st.tgt.target_xy[1]=ty;
        s->st.tgt.target_src=TGT_SEEDED; s->st.tgt.target_valid=1; s->st.tgt.target_age=0.0;
    }

    /* ---- Mode 2 (§M2) LIVE interactive target OVERRIDE (D-039): the operator-dragged pad WINS over the
     * seeded target / SEA sources, so guidance chases wherever it is dragged (r_xy = y − target_xy). Set
     * ONLY by apply_command under --interactive (serve-only, fenced non-deterministic). live_tgt_on=0 in
     * every headless/gate path => no override => byte-identical (the sacred determinism is untouched). */
    if(s->live_tgt_on){
        s->gcmd.target_xy[0]=s->live_tgt[0];   s->gcmd.target_xy[1]=s->live_tgt[1];
        s->gcmd.target_vxy[0]=s->live_tgt_vxy[0]; s->gcmd.target_vxy[1]=s->live_tgt_vxy[1];
        s->st.tgt.target_xy[0]=s->live_tgt[0];  s->st.tgt.target_xy[1]=s->live_tgt[1];
        s->st.tgt.target_vxy[0]=s->live_tgt_vxy[0]; s->st.tgt.target_vxy[1]=s->live_tgt_vxy[1];
        s->st.tgt.target_src=TGT_SEEDED; s->st.tgt.target_valid=1; s->st.tgt.target_age=0.0;
    }

    /* §8.2 measurement layer (D-010): build the nav view ONCE per 50 Hz guidance tick (not at
     * the 500 Hz physics rate). All guidance layers below consume `nav` instead of raw truth.
     * In NAV_TRUTH `nav` is a byte copy of `*st` -> the guidance path is bit-identical to
     * pre-nav (the hard acceptance gate). The ignition latch, ada freeze, and every PLANT
     * state write stay on truth `st`. */
    int is_gtick = (st->step % (long)(GUIDANCE_DT/DT) == 0);
    State nav;
    if(is_gtick) nav_measure(&s->nav, st, st->step, &nav);

    /* guidance at 50 Hz */
    if(is_gtick && s->guidance_mode==GM_HOVERSLAM){
        int entry_handled = entry_supervisor(s, &nav);   /* E3: 3-engine entry burn above hoverslam (D-007) */
        /* entry_supervisor's CUT flips engine_on/phase/ign_timer on TRUTH mid-tick; refresh the
         * nav view's pass-through fields so hoverslam does not read a stale engine/phase snapshot
         * (the perturbed r/v/q/w estimate is preserved). No-op-equivalent when nothing was cut. */
        if(!entry_handled){ nav_resync(st, &nav); hoverslam_step(&nav,&s->gcmd); }
        /* engine ignition latch */
        if(s->gcmd.engine_cmd && !st->engine_on && st->relights_left>0){
            st->engine_on=1; st->ign_timer=0.0; st->n_eng=s->gcmd.n_eng; st->relights_left--;
            if(st->phase==PH_ENTRY_BURN){
                /* entry burn = retrograde deceleration, NOT the suicide-burn profile -> no ada freeze */
            } else {
                if(st->phase==PH_COAST||st->phase==PH_AERO) st->phase=PH_LANDING_BURN;
                /* freeze the design deceleration from the ACTUAL ignition mass/thrust */
                AtmoOut atm; atmo_eval(st->y[S_RZ],&atm);
                MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
                double Tf = st->n_eng*engine_thrust(1.0,atm.p);
                double amax = Tf/mp.m - G0; if(amax<1.0)amax=1.0;
                st->ada = (st->fins_deployed?0.85:0.58)*amax;   /* fins-deployed = hard suicide burn from the aero handoff (D-007) */
            }
        }
        st->deploy_cmd = s->gcmd.deploy_cmd;
    }

    /* ---- GM_MPPI guidance block (track 4-B, HIER MPPI CPU) — WELL-DELIMITED, additive (MPPI-2). ----
     * Mirrors the GM_HOVERSLAM block: E3 entry_supervisor stays ABOVE MPPI, then mppi_step owns the
     * aero-descent divert + landing burn (full solve every MPPI_REPLAN_DECIM ticks; cheap knot-emit
     * between), then the SAME ignition latch/ada freeze. Guarded by GM_MPPI so GM_HOVERSLAM is
     * byte-for-byte untouched (goldens, selftest, determinism). */
    if(is_gtick && s->guidance_mode==GM_MPPI){
        int entry_handled = entry_supervisor(s, &nav);   /* E3 above MPPI (reads the estimate) */
        if(!entry_handled){
            nav_resync(st, &nav);   /* refresh pass-through fields after a possible entry-burn CUT */
            if((s->mppi.gtick % MPPI_REPLAN_DECIM)==0){
#ifdef BL_HAVE_CUDA
                if(g_mppi_use_cuda) mppi_step_cuda(&s->mppi, &nav, &s->env, &s->gcmd);  /* M5 GPU rollout */
                else                mppi_step(&s->mppi, &nav, &s->env, &s->gcmd);         /* CPU rollout */
#else
                mppi_step(&s->mppi, &nav, &s->env, &s->gcmd);   /* CPU rollout (no-CUDA build) */
#endif
            }
            else mppi_execute(&s->mppi, &nav, &s->gcmd);   /* emit next warm-start knot + shift (cheap) */
        }
        s->mppi.gtick++;
        if(s->gcmd.engine_cmd && !st->engine_on && st->relights_left>0){
            st->engine_on=1; st->ign_timer=0.0; st->n_eng=s->gcmd.n_eng; st->relights_left--;
            if(st->phase==PH_ENTRY_BURN){ /* entry burn = retrograde; no ada freeze */ }
            else {
                if(st->phase==PH_COAST||st->phase==PH_AERO) st->phase=PH_LANDING_BURN;
                AtmoOut atm; atmo_eval(st->y[S_RZ],&atm);
                MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
                double Tf = st->n_eng*engine_thrust(1.0,atm.p);
                double amax = Tf/mp.m - G0; if(amax<1.0)amax=1.0;
                st->ada = (st->fins_deployed?0.85:0.58)*amax;
            }
        }
        st->deploy_cmd = s->gcmd.deploy_cmd;

        /* ---- N1 S0 TEACHER TAP (--policy-log; policy_tap.h). Log ONE (o, a*) row for this GM_MPPI
         * guidance tick: o = policy_build_obs(nav, gcmd) — the EXACT legal features GM_NEURAL will
         * read (the resync'd nav view the guidance layer just consumed, pass-through fields pre-latch,
         * i.e. what guidance saw when it produced the command); a* = the EXECUTED gcmd (a_lat[0],
         * a_lat[1], throttle) mppi_execute/entry_divert emitted this tick. READ-ONLY: policy_tap_write
         * does no RNG and no state write, and is a no-op when the flag is absent (tap.f==NULL) => the
         * sim path stays byte-identical (the D-014 instrument-without-touching gate). It is placed
         * AFTER the ignition latch so nothing downstream can still change the executed command. */
        policy_tap_write(&s->tap, &nav, &s->gcmd);
    }

    /* ---- GM_NEURAL guidance block (N1 §9.8, tier-3 learned policy) — MIRRORS the GM_MPPI block:
     * the E3 entry_supervisor stays ABOVE (the 3-engine entry burn), then neural_policy_step owns the
     * aero-descent divert + landing burn (a single <10 µs forward pass EVERY gtick — no rollouts, no
     * decimation), then the SAME ignition latch + ada freeze + leg deploy. Guarded by GM_NEURAL so
     * GM_HOVERSLAM/GM_MPPI are byte-for-byte untouched; default OFF => this whole block is dead code
     * (the §13.6 leak gate). neural_policy_step reads the resync'd nav view (§8.1) and is isfinite-
     * guarded (an absurd net => honest crash, never a NaN cascade). */
    if(is_gtick && s->guidance_mode==GM_NEURAL){
        int entry_handled = entry_supervisor(s, &nav);   /* E3 above the policy (reads the estimate) */
        if(!entry_handled){
            nav_resync(st, &nav);   /* refresh pass-through fields after a possible entry-burn CUT */
            /* Tier A (§C.2/App-G): ANALYTIC ignition + legs, LEARNED steering + throttle. Run the
             * proven hoverslam law first to set engine_cmd / deploy_cmd / n_eng (the same aero-aware
             * ignition trigger + LEG_DEPLOY_H gate GM_MPPI inherits via mppi_execute), THEN let the
             * policy OVERRIDE a_lat[2] + throttle. neural_policy_step writes only a_lat/throttle/mode,
             * so engine_cmd/deploy_cmd/n_eng from hoverslam survive — the policy owns the continuous
             * channels, the analytic triggers own ignition/legs. */
            hoverslam_step(&nav, &s->gcmd);
            /* ---- DAGGER SHADOW TEACHER (D-023; --policy-log under GM_NEURAL; §B.1). With the tap
             * armed, run the FULL GM_MPPI machinery in shadow on the SAME resync'd nav (10 Hz replan
             * + knot-hold, identical cadence to the GM_MPPI block) into a LOCAL GuidanceCmd, and log
             * (o, a_MPPI) — the teacher's answer AT THE STATE THE POLICY VISITS — while the plant
             * flies the NEURAL command. Pure observation of the flight: the shadow writes only
             * s->mppi (its own planner memory) + the tap file; s->gcmd and every plant/nav/RNG
             * stream the flight consumes are untouched, so --neural with and without the tap are
             * byte-identical trajectories (gated). CPU mppi_step only (bit-parity with CUDA anyway). */
            if(s->tap.f){
                GuidanceCmd shadow = s->gcmd;   /* carries target_xy + the HOVERSLAM divert a_lat (set by hoverslam_step above) */
                /* E2' (D-032): reactive-shadow mode logs hoverslam's OWN a_lat — which `shadow` already
                 * holds from the hoverslam_step above — distilling the reactive divert (the best EO
                 * teacher, D-031). Default: the MPPI shadow OVERWRITES shadow.a_lat with the planner's
                 * answer (D-023). g_shadow_reactive==0 => byte-identical to the D-023 MPPI shadow. */
                if(!g_shadow_reactive){
                    if((s->mppi.gtick % MPPI_REPLAN_DECIM)==0) mppi_step(&s->mppi, &nav, &s->env, &shadow);
                    else                                        mppi_execute(&s->mppi, &nav, &shadow);
                    s->mppi.gtick++;
                }
                policy_tap_write(&s->tap, &nav, &shadow);
            }
            neural_policy_step(&nav, &s->gcmd);   /* pi_theta(legal obs) -> a_lat[2] + throttle */
        }
        if(s->gcmd.engine_cmd && !st->engine_on && st->relights_left>0){
            st->engine_on=1; st->ign_timer=0.0; st->n_eng=s->gcmd.n_eng; st->relights_left--;
            if(st->phase==PH_ENTRY_BURN){ /* entry burn = retrograde; no ada freeze */ }
            else {
                if(st->phase==PH_COAST||st->phase==PH_AERO) st->phase=PH_LANDING_BURN;
                AtmoOut atm; atmo_eval(st->y[S_RZ],&atm);
                MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
                double Tf = st->n_eng*engine_thrust(1.0,atm.p);
                double amax = Tf/mp.m - G0; if(amax<1.0)amax=1.0;
                st->ada = (st->fins_deployed?0.85:0.58)*amax;
            }
        }
        st->deploy_cmd = s->gcmd.deploy_cmd;
    }

    /* ---- GM_CFLY guidance block (N2-S2, D-040, the OPTIMIZER-IN-THE-LOOP) — WELL-DELIMITED:
     * the theta-law owns ALL phases (NO entry_supervisor above — the law IS the entry/max-Q rule),
     * the warm-started local CEM replans on cadence (big t=0 solve = the mission plan; cheap warm
     * tracking replans = the onboard replan), the law flies the current theta with the coast-relight
     * latches, then the SAME ignition latch/ada freeze. Guarded by GM_CFLY; default OFF => byte-
     * identical (goldens/selftest/determinism). Deterministic: the plant RNG is pure-per-(seed,step)
     * and the CEM sampler is a seeded xorshift => the whole replan replays bit-exact. */
    if(is_gtick && s->guidance_mode==GM_CFLY){
        nav_resync(st, &nav);
        if(!s->cfly.noreplan && st->t >= s->cfly.next_replan_t){
            int big = (s->cfly.next_replan_t<=0.0);   /* the t=0 oracle-strength plan, then warm tracking */
            cfly_replan(s, big);
            s->cfly.next_replan_t = st->t + CFLY_REPLAN_DT;
        }
        cfly_law(s, &nav, &s->gcmd);
        if(s->gcmd.engine_cmd && !st->engine_on && st->relights_left>0){
            st->engine_on=1; st->ign_timer=0.0; st->n_eng=s->gcmd.n_eng; st->relights_left--;
            if(st->phase==PH_ENTRY_BURN){ /* entry burn = retrograde; no ada freeze */ }
            else {
                if(st->phase==PH_COAST||st->phase==PH_AERO) st->phase=PH_LANDING_BURN;
                AtmoOut atm; atmo_eval(st->y[S_RZ],&atm);
                MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
                double Tf = st->n_eng*engine_thrust(1.0,atm.p);
                double amax = Tf/mp.m - G0; if(amax<1.0)amax=1.0;
                st->ada = (st->fins_deployed?0.85:0.58)*amax;
            }
        }
        st->deploy_cmd = s->gcmd.deploy_cmd;
    }

    /* ---- GM_RFLY guidance block (the D-040 PIVOT) — the NATIVE reactive stack, gain-searched.
     * The flown law is EXACTLY the GM_HOVERSLAM pipeline (entry_supervisor + hoverslam_step + the
     * D-010 wind-trim below), but the frozen gains are scaled by the per-scenario theta the CEM
     * selects (big t=0 solve = the mission plan; warm replans = the onboard replan). Theta rides
     * GuidanceCmd.rt (OpenMP-safe for candidates); identity theta == plain hoverslam EXACTLY, and
     * elitism keeps it in-population, so the search can only match-or-beat the baseline. Guarded
     * by GM_RFLY; default OFF => rt_on never set => every legacy mode byte-identical. */
    if(is_gtick && s->guidance_mode==GM_RFLY){
        if(!s->rfly.noreplan && st->t >= s->rfly.next_replan_t){
            int big = (s->rfly.next_replan_t<=0.0);
            rfly_replan(s, big);
            s->rfly.next_replan_t = st->t + RFLY_REPLAN_DT;
        }
        memcpy(s->gcmd.rt, s->rfly.th, sizeof(s->gcmd.rt));
        s->gcmd.rt_on = 1;
        int entry_handled = entry_supervisor(s, &nav);   /* E3 above the law (reads the estimate) */
        if(!entry_handled){
            nav_resync(st, &nav);
            hoverslam_step(&nav, &s->gcmd);
        }
        if(s->gcmd.engine_cmd && !st->engine_on && st->relights_left>0){
            st->engine_on=1; st->ign_timer=0.0; st->n_eng=s->gcmd.n_eng; st->relights_left--;
            if(st->phase==PH_ENTRY_BURN){ /* entry burn = retrograde; no ada freeze */ }
            else {
                if(st->phase==PH_COAST||st->phase==PH_AERO) st->phase=PH_LANDING_BURN;
                AtmoOut atm; atmo_eval(st->y[S_RZ],&atm);
                MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
                double Tf = st->n_eng*engine_thrust(1.0,atm.p);
                double amax = Tf/mp.m - G0; if(amax<1.0)amax=1.0;
                st->ada = (st->fins_deployed?0.85:0.58)*amax;
            }
        }
        st->deploy_cmd = s->gcmd.deploy_cmd;
    }

    /* D-009 WIND-REJECTION INTEGRAL TRIM (fins-deployed LANDING burn only). Zero-wind ENTRY/AERO
     * land 32%/71% with median miss 5-6 m; with the spec winds every law floors at ~120-150 m — the
     * mean-wind aero push (~1.6 m/s^2 at 20-40 kPa) leaks through the ignition-hold/fade/dead-zone
     * windows as a steady-state drift no proportional law can null. Classic disturbance rejection:
     * slow INTEGRAL action on the position error during the burn builds the counter-tilt that trims
     * the wind out. Pure state feedback (guidance never reads v_wind — canon §4.3). Conditional
     * integration: only PH_LANDING_BURN + fins deployed + past the 2 s ignition hold; capped
     * (anti-windup); reset otherwise. TERMINAL (fins stowed) and the entry burn (ZEM/ZEV owns it)
     * never integrate. Applied to gcmd on guidance ticks for BOTH GM_HOVERSLAM and GM_MPPI. */
    if(is_gtick && (s->guidance_mode==GM_HOVERSLAM || s->guidance_mode==GM_RFLY)){
        /* D-010: the wind-trim integral is REACTIVE-LAW medicine. MPPI replans on the fresh
         * estimate every tick and compensates the wind implicitly; layering an integral on top
         * double-corrects and pushed its batch 63->40% (off-pad 24). GM_HOVERSLAM only —
         * and GM_RFLY, which flies the SAME reactive law (the integral is part of the stack
         * the theta search tunes around; candidates inherit lat_eint via the Sim copy). */
        if(st->engine_on && st->fins_deployed && st->phase==PH_LANDING_BURN && st->ign_timer>=2.0){
            /* D-010 sweep winner C14: STRONG trim (Ki 0.012) whose OUTPUT fades near the ground.
             * The 16-config grid resolved the interaction the single-knob probes could not: the
             * strong integral only wins when (a) its near-ground output is faded (D-002 straighten
             * lesson — the memory keeps integrating, only the applied command fades) AND (b) the
             * divert Kvel is 0.9 (C13, the same trim at Kvel 1.2, scores 109 vs C14's 149).
             * §8.1: position feedback reads the NAV estimate; the gates are plant truth. */
            const double KI_WIND=0.012, EINT_CAP=2.0;
            MassProps mpw; mass_props(nav.y[S_MLOX],nav.y[S_MRP1],0,0,&mpw);
            double h_feet_w = nav.y[S_RZ]-mpw.com-1.0*st->deploy_frac;
            double wfade=(h_feet_w-40.0)/160.0; if(wfade<0.0)wfade=0.0; if(wfade>1.0)wfade=1.0;
            double rr[2]={nav.y[S_RX],nav.y[S_RY]};
            for(int ax=0;ax<2;ax++){
                s->lat_eint[ax] += KI_WIND*rr[ax]*GUIDANCE_DT;
                if(s->lat_eint[ax]> EINT_CAP) s->lat_eint[ax]= EINT_CAP;
                if(s->lat_eint[ax]<-EINT_CAP) s->lat_eint[ax]=-EINT_CAP;
                s->gcmd.a_lat[ax] -= s->lat_eint[ax]*wfade;
            }
        } else { s->lat_eint[0]=0.0; s->lat_eint[1]=0.0; }
    }

    /* control at 500 Hz */
    control_step(st,&s->gcmd,&s->env,&s->act);

    /* environment */
    wind_sample(s, st->y[S_RZ], s->env.wind_world);

    /* deploy ramp */
    if(st->deploy_cmd){ st->deploy_frac += DT/LEG_DEPLOY_T; if(st->deploy_frac>1)st->deploy_frac=1; }

    /* N2 depletion */
    st->N2 -= s->act.rcs_dm*DT; if(st->N2<0)st->N2=0;

    /* integrate: contact-substep near ground, else flight RK4 */
    int in_contact = near_ground(st, 0.5);
    double lo_before = lowest_point_z(st);
    if(in_contact) contact_substep(st,&s->act,&s->env,s->se.deck_z,s->deck_vz_live,DT);
    else rk4_step(st,&s->act,&s->env,DT);

    /* diagnostics via a spare deriv eval */
    double dyt[NSTATE]; dynamics_deriv(st,&s->act,&s->env,dyt,&s->diag);
    if(s->diag.qbar>s->max_qbar) s->max_qbar=s->diag.qbar;
    if(s->diag.qdot_heat>s->peak_qdot) s->peak_qdot=s->diag.qdot_heat;

    /* touchdown capture (first contact) */
    double lo_after=lowest_point_z(st);
    if(!s->touched && lo_after<=s->se.deck_z+1e-3 && lo_before>s->se.deck_z){
        s->touched=1;
        s->impact_v=sqrt(st->y[S_VX]*st->y[S_VX]+st->y[S_VY]*st->y[S_VY]+st->y[S_VZ]*st->y[S_VZ]);
        s->impact_tilt=sim_body_tilt(st);
        /* Target Stage-1 (D-034 §A.3): latch the target pose at FIRST CONTACT and measure the touchdown
         * offset from it (a moving pad is landed-on where it IS at contact). gcmd.target_xy is the current
         * seeded target — (0,0) for FIXED => the sqrt(rx^2+ry^2) origin measure, byte-identical. */
        s->impact_target_xy[0]=s->gcmd.target_xy[0]; s->impact_target_xy[1]=s->gcmd.target_xy[1];
        { double dlx=st->y[S_RX]-s->impact_target_xy[0], dly=st->y[S_RY]-s->impact_target_xy[1];
          s->impact_lat=sqrt(dlx*dlx+dly*dly); }
        if(st->phase<PH_TOUCHDOWN) st->phase=PH_TOUCHDOWN;
    }

    st->t += DT; st->step++;

    /* ---- termination checks ---- */
    /* struct: qbar over limit sustained */
    if(s->diag.qbar>QBAR_MAX){ s->qbar_over_timer+=DT; if(s->qbar_over_timer>2.0){ st->fault=F_STRUCT; st->phase=PH_STRUCT_FAIL; s->done=1; return 0; } }
    else s->qbar_over_timer=0;
    /* thermal */
    if(s->diag.qdot_heat>HEAT_QDOT_MAX){ s->qdot_over_timer+=DT; if(s->qdot_over_timer>5.0){ st->fault=F_THERMAL; st->phase=PH_THERMAL_FAIL; s->done=1; return 0; } }
    else s->qdot_over_timer=0;
    /* loss of control */
    double wmag=sqrt(st->y[S_WX]*st->y[S_WX]+st->y[S_WY]*st->y[S_WY]+st->y[S_WZ]*st->y[S_WZ]);
    if(wmag>2.0){ s->loc_timer+=DT; if(s->loc_timer>3.0){ st->fault=F_LOC; st->phase=PH_LOC; s->done=1; return 0; } }
    else s->loc_timer=0;

    /* fuel depletion note (keep simulating ballistic) */
    if((st->y[S_MLOX]<=0.0||st->y[S_MRP1]<=0.0) && st->engine_on){ st->fault=F_FUEL; }

    /* settling / verdict */
    if(s->touched){
        if(st->phase<PH_SETTLING && st->phase!=PH_LANDING_BURN) st->phase=PH_SETTLING;
        st->phase=PH_SETTLING;
        double ke = st->y[S_VX]*st->y[S_VX]+st->y[S_VY]*st->y[S_VY]+st->y[S_VZ]*st->y[S_VZ]
                  + wmag*wmag*4.0;
        if(ke<0.02) s->settle_timer+=DT; else s->settle_timer=0;
        /* stop thrust after touchdown */
        s->gcmd.throttle=0.0; s->gcmd.engine_cmd=0; st->engine_on=0;
        if(s->settle_timer>1.5 || st->t>200.0){ set_verdict(s); s->done=1; return 0; }
        /* immediate crash if buried fast */
        if(s->impact_v>TD_V_HARD){ set_verdict(s); s->done=1; return 0; }
    }
    return 1;
}

long sim_run(Sim* s, RunResult* res, double t_max){
    long n=0;
    while(sim_step(s)){ n++; if(s->st.t>t_max){ s->st.fault=F_NONE; break; } }
    State* st=&s->st;
    if(res){
        res->verdict=st->verdict; res->fault=st->fault; res->phase=st->phase;
        res->td_v=s->impact_v; res->td_lat=s->impact_lat; res->td_tilt=s->impact_tilt;
        res->settled_tilt=sim_body_tilt(st);
        res->fuel_margin=st->y[S_MLOX]+st->y[S_MRP1];
        res->max_qbar=s->max_qbar; res->peak_qdot=s->peak_qdot; res->t_total=st->t;
        double mc=0; for(int i=0;i<4;i++) if(st->crush[i]>mc)mc=st->crush[i]; res->max_crush=mc;
        if(st->verdict==V_NONE){ res->verdict=V_CRASHED; res->phase=PH_CRASHED; }
    }
    return n;
}
