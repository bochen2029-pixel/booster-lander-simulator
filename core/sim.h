/* sim.h — simulation driver: guidance/control/integrate/contact/phase/termination. */
#ifndef BL_SIM_H
#define BL_SIM_H
#include "state.h"
#include "guidance.h"
#include "dynamics.h"
#include "scenario.h"
#include "guidance_mppi.h"
#include "nav.h"

typedef struct {
    State st;
    GuidanceCmd gcmd;
    Actuators   act;
    EnvCtx      env;
    ScenarioEnv se;
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

double sim_body_tilt(const State* st);     /* angle of body +Z from world +Z (rad) */

#endif
