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
    int    mode;          /* 0 none 1 hoverslam 2 mppi 3 neural */
    int    solver_flags;  /* diagnostics */
    double t_go;          /* estimated time to touchdown s */
    /* N0 movable target (§4.5, target_sandbox_design §B.3): the target's CURRENT pose, the
     * §8.1-legal quantity guidance nulls the offset to (r_xy − target_xy). sim.c fills these each
     * guidance tick from the seeded/nominal target slot; hoverslam_step + mppi_step read them.
     * ZERO by default (memset / FIXED-at-origin) => TERMINAL/AERO/ENTRY byte-identical. */
    double target_xy[2];  /* target position, world XY [m] (0 => pad at origin, v1 behavior) */
    double target_vxy[2]; /* target velocity, world XY [m/s] (0 for a static/slow target) */
    /* SEA (§4.4 / §A.4 Option-i, D-035): the deck's CURRENT vertical pose deck_z(t_now), the §8.1-legal
     * quantity the reactive vertical law nulls its height against (h_base becomes y_z − deck_z − com), so
     * the vehicle tracks the heaving deck instead of the static z=0. sim.c fills it each step under
     * MOD_SEA; ZERO by default (memset / static pad) => TERMINAL/AERO/ENTRY byte-identical (− 0.0). */
    double deck_z;        /* current deck height, world Z [m] (0 => static pad at z=0, v1 behavior) */
    /* plan tail for telemetry (filled by mppi later) */
} GuidanceCmd;

enum { GM_NONE=0, GM_HOVERSLAM=1, GM_MPPI=2, GM_NEURAL=3 };  /* N1 §9.8: tier-3 learned policy */
enum { SF_DEGRADED=1, SF_NO_ROOT=2, SF_TERMINAL=4 };

#endif
