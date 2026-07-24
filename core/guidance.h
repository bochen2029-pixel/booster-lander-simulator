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
    /* RFLY (D-040 pivot): per-scenario GAIN OVERRIDES for the native reactive stack, selected by
     * the GM_RFLY optimizer-in-the-loop. Carried HERE (not a global) so concurrent candidate
     * rollouts each read their own theta (OpenMP-safe), flowing through the same socket the
     * target/deck pose already uses. MULTIPLIERS around identity (rt_add slots around 0):
     * rt_on==0 (memset default) => rt_gain()==1.0 / rt_add()==0.0 => x*1.0 / x+0.0 are IEEE-exact
     * => GM_HOVERSLAM/GM_MPPI/GM_NEURAL byte-identical by construction (the §13.6 leak gate). */
    double rt[10];        /* theta (see guidance_rfly.h RT_* index enum) */
    int    rt_on;         /* 0 => identity (every legacy path) */
    /* plan tail for telemetry (filled by mppi later) */
} GuidanceCmd;

/* device-safe (mirrored into the MPPI CUDA rollout translation unit) */
#ifdef __CUDACC__
#  define BL_GD __host__ __device__
#else
#  define BL_GD
#endif
BL_GD static inline double rt_gain(const GuidanceCmd* g, int i){ return g->rt_on ? g->rt[i] : 1.0; }
BL_GD static inline double rt_add (const GuidanceCmd* g, int i){ return g->rt_on ? g->rt[i] : 0.0; }

enum { GM_NONE=0, GM_HOVERSLAM=1, GM_MPPI=2, GM_NEURAL=3, GM_CFLY=4, GM_RFLY=5 };  /* N1 §9.8: tier-3 learned policy; N2-S2: optimizer-in-the-loop (D-040); GM_RFLY: the D-040 PIVOT — CEM over the native reactive stack's gains */
enum { SF_DEGRADED=1, SF_NO_ROOT=2, SF_TERMINAL=4 };

#endif
