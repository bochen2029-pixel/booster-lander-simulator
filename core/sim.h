/* sim.h — simulation driver: guidance/control/integrate/contact/phase/termination. */
#ifndef BL_SIM_H
#define BL_SIM_H
#include "state.h"
#include "guidance.h"
#include "dynamics.h"
#include "scenario.h"
#include "guidance_mppi.h"
#include "nav.h"
#include "policy_tap.h"   /* N1 S0: the teacher (o,a) logging tap (--policy-log; GM_MPPI only) */
#include "sea.h"          /* SEA module (§4.4): P-M droneship deck-motion spectrum (--sea; default OFF) */

/* DIAL-A-GUST (canon §10.6 INJECT_DISTURBANCE type=gust; §4.3 layer-3 discrete 1-cosine).
 * A deterministic discrete wind-shear EVENT: a horizontal wind pulse the vehicle penetrates as it
 * passes through the altitude band [alt-hw, alt+hw]. It is a pure function of altitude (NO RNG) so
 * it is fully replayable — (seed, gust) -> bit-identical. It is a PLANT input ONLY: it superposes
 * on the mean+Dryden wind in wind_sample() and enters the dynamics through v_rel exactly like the
 * existing wind. Guidance NEVER reads it (canon §4.3): the MPPI rollouts zero env.wind_world (the
 * "nominal planner", guidance_mppi.c), so the controller feels the gust solely as the lateral
 * state drift it induces and re-solves against that. peak==0 (flag absent) => the whole block is
 * skipped and the run is byte-identical to today (protects the sacred TERMINAL-194 parity gate). */
typedef struct {
    double peak;    /* pulse peak wind speed [m/s]; 0 => gust OFF (the default) */
    double alt;     /* band-center altitude [m], world Z (same frame as S_RZ / the mean profile) */
    double hw;      /* band half-width [m]; full 1-cosine penetration distance dm = 2*hw */
    double dirx;    /* fixed horizontal direction unit vector (default +x => 1,0), from --gust-dir */
    double diry;
} GustCfg;

typedef struct {
    State st;
    GuidanceCmd gcmd;
    Actuators   act;
    EnvCtx      env;
    ScenarioEnv se;
    GustCfg     gust;   /* DIAL-A-GUST config (peak==0 => OFF => byte-identical). Set post-init. */
    /* ---- SEA module (§4.4 / target_sandbox_design §A.2, D-035). Armed by --sea [Hs] (MOD_SEA). The
     * heaving ASDS deck moves in the PLANT: sim_step overwrites se.deck_z with the live closed-form
     * heave deck_z(t) and feeds deck_vz_live (the heave rate) to the contact solver for the deck-relative
     * leg loads. Default OFF (memset 0 => sea never evaluated, deck_vz_live=0) => byte-identical. */
    SeaState    sea;
    double      deck_vz_live;   /* live deck heave rate deck_vz(t) [m/s]; 0 when SEA off */
    uint32_t seed;
    int modules;
    int scenario;
    int guidance_mode;
    /* termination bookkeeping */
    int touched;
    double impact_v, impact_tilt, impact_lat;
    double impact_target_xy[2];  /* Target Stage-1 (D-034 §A.3): target pose LATCHED at first contact; the verdict + td_lat measure distance from THIS, not the origin. (0,0) for FIXED => byte-identical. */
    double settle_timer, qbar_over_timer, qdot_over_timer, loc_timer;
    double max_qbar, peak_qdot;
    int done;
    Diag diag;   /* last-step diagnostics */
    MppiState mppi;   /* HIER MPPI planner state (GM_MPPI only, track 4-B) */
    double lat_eint[2];   /* D-009 wind-rejection integral trim (fins-deployed LANDING burn only) */
    NavState nav;     /* D-010 §8.1 measurement layer (NAV_TRUTH pass-through / NAV_NOISY) */
    /* ---- N0 ENGINE-OUT (§4.6, engineout_design). Armed by --engine-out k@t (MOD_ENGINE_OUT).
     * A seeded, time-triggered failure during a multi-engine burn: decrement n_eng + set the
     * survivor-centroid thrust_offset (induced torque via the existing arm_thr lever) + flag the
     * chamber-P health. Default OFF (eo_engine<0) => never fires => byte-identical. */
    int    eo_engine;     /* which engine fails: 0=center,1/2=sides; <0 => disarmed */
    double eo_time;       /* sim-time of failure [s] */
    int    eo_fired;      /* latched once fired (a permanent loss for the burn) */
    /* ---- N0 SEEDED MOVABLE TARGET (§4.5, target_sandbox_design). Armed by --target (MOD_TARGET).
     * A deterministic closed-form horizontal drift (the SEEDED source): target_xy(t) written each
     * guidance tick, read by guidance as the offset target + streamed as the nav TargetEstimate.
     * Default OFF (tgt_mode==0) => FIXED origin => byte-identical. */
    int    tgt_mode;      /* 0 FIXED/off, 1 seeded circular drift, 2 seeded linear ramp */
    uint32_t tgt_seed, tgt_run;  /* the `target` Philox key (seed,run) */
    double tgt_amp;       /* drift amplitude [m] */
    double tgt_omega;     /* drift angular rate [rad/s] */
    double tgt_phase[2];  /* seeded phase / direction */
    /* ---- N1 S0 TEACHER TAP (--policy-log; GM_MPPI only). READ-ONLY (o,a) logging for the offline
     * distillation trainer. Disarmed by default (tap.f==NULL, memset in sim_init) => byte-identical.
     * Attached post-sim_init by the CLI handler (the FILE* is opened ONCE per process, shared across
     * headless runs; seed/run identify each run's rows). Fires in the GM_MPPI gtick path after the
     * command is resolved (policy_tap.h). No RNG, no state writes — the D-014 instrument-without-
     * touching discipline; the byte-equality gate proves it. */
    PolicyTap tap;
} Sim;

typedef struct {
    int verdict, fault, phase;
    double td_v, td_lat, td_tilt, settled_tilt;
    double fuel_margin, max_qbar, peak_qdot, t_total;
    double max_crush;
} RunResult;

void sim_init(Sim* s, int scenario, uint32_t seed, uint32_t run_idx, int modules, int guidance_mode);
/* Advance one physics step. Returns 1 while running, 0 when terminated. */
int  sim_step(Sim* s);
/* Run to termination (headless). Fills result. Returns steps taken. */
long sim_run(Sim* s, RunResult* res, double t_max);

/* DIAL-A-GUST: arm the discrete 1-cosine wind-shear pulse on this Sim. Call AFTER sim_init (which
 * zeroes s->gust => OFF by default). peak in m/s, alt/hw in m (world Z), dir_deg = fixed horizontal
 * bearing in degrees measured from +x toward +y (default 0 => +x). peak<=0 or hw<=0 disarms it. */
void sim_set_gust(Sim* s, double peak, double alt, double hw, double dir_deg);

double sim_body_tilt(const State* st);     /* angle of body +Z from world +Z (rad) */

#endif
