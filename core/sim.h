/* sim.h — simulation driver: guidance/control/integrate/contact/phase/termination. */
#ifndef BL_SIM_H
#define BL_SIM_H
#include "state.h"
#include "guidance.h"
#include "dynamics.h"
#include "scenario.h"
#include "guidance_mppi.h"
#include "nav.h"

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
    uint32_t seed;
    int modules;
    int scenario;
    int guidance_mode;
    /* termination bookkeeping */
    int touched;
    double impact_v, impact_tilt, impact_lat;
    double settle_timer, qbar_over_timer, qdot_over_timer, loc_timer;
    double max_qbar, peak_qdot;
    int done;
    Diag diag;   /* last-step diagnostics */
    MppiState mppi;   /* HIER MPPI planner state (GM_MPPI only, track 4-B) */
    double lat_eint[2];   /* D-009 wind-rejection integral trim (fins-deployed LANDING burn only) */
    NavState nav;     /* D-010 §8.1 measurement layer (NAV_TRUTH pass-through / NAV_NOISY) */
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
