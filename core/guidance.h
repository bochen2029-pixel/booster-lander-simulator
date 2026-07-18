/* guidance.h — guidance command produced at 50 Hz, consumed by control at 500 Hz. */
#ifndef BL_GUIDANCE_H
#define BL_GUIDANCE_H
#include "state.h"

typedef struct {
    double throttle;      /* 0..1 */
    double a_lat[2];      /* desired world lateral accel (steering) m/s^2 */
    int    engine_cmd;    /* request engine firing */
    int    n_eng;         /* engines to use */
    int    deploy_cmd;    /* legs */
    int    mode;          /* 0 none 1 hoverslam 2 mppi */
    int    solver_flags;  /* diagnostics */
    double t_go;          /* estimated time to touchdown s */
    /* plan tail for telemetry (filled by mppi later) */
} GuidanceCmd;

enum { GM_NONE=0, GM_HOVERSLAM=1, GM_MPPI=2 };
enum { SF_DEGRADED=1, SF_NO_ROOT=2, SF_TERMINAL=4 };

#endif
