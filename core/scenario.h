/* scenario.h — initial conditions + dispersions. CLAUDE_v1.md App-D. */
#ifndef BL_SCENARIO_H
#define BL_SCENARIO_H
#include "state.h"
#include <stdint.h>

enum { SCEN_TERMINAL=0, SCEN_AERO_OFFSET=1, SCEN_ENTRY=2, SCEN_ASDS_NIGHT=3, SCEN_CHAOS=4, SCEN_COUNT };

typedef struct {
    double u_ref;     /* mean wind at 10 m (m/s) */
    double wind_az;   /* rad */
    double deck_z;    /* ground/deck height (0 RTLS) */
    int    w20_kt;    /* Dryden severity */
} ScenarioEnv;

const char* scenario_name(int scen);
int scenario_from_name(const char* s);
/* Initialize state + environment for (scenario, seed, run_idx). run_idx=0 => nominal (no dispersion). */
void scenario_init(State* st, int scen, uint32_t seed, uint32_t run_idx, ScenarioEnv* se);

#endif
