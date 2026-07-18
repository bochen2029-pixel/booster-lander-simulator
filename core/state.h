/* state.h — vehicle state, actuator commands, module mask, phases. CLAUDE_v1.md §6.1. */
#ifndef BL_STATE_H
#define BL_STATE_H

#include <stdint.h>
#include "vmath.h"

/* Continuous state integrated by RK4, packed flat for a generic integrator. */
enum {
    S_RX,S_RY,S_RZ,          /* position world */
    S_VX,S_VY,S_VZ,          /* velocity world */
    S_QX,S_QY,S_QZ,S_QW,     /* quaternion body->world xyzw */
    S_WX,S_WY,S_WZ,          /* angular velocity body */
    S_MLOX,S_MRP1,           /* propellant masses */
    S_THR,                   /* throttle actual */
    S_G0,S_G1,               /* gimbal actual (rad) */
    S_GR0,S_GR1,             /* gimbal rate (rad/s) */
    S_F0,S_F1,S_F2,S_F3,     /* fin actual deflection */
    S_SL0,S_SL1,S_SL2,S_SL3, /* slosh angles: tank0(x,y) tank1(x,y) */
    S_SV0,S_SV1,S_SV2,S_SV3, /* slosh rates */
    S_QHEAT,                 /* integrated stagnation heat load J/m^2 */
    NSTATE
};

/* Hybrid / event state kept outside the RK4 vector. */
typedef struct {
    double y[NSTATE];        /* continuous block */
    /* engine (discrete/hybrid) */
    int    engine_on;        /* commanded firing */
    int    n_eng;            /* engines this burn (1 or 3) */
    double ign_timer;        /* s since ignition cmd; <0 when off */
    int    relights_left;
    double ada;              /* design deceleration frozen at ignition (hoverslam) */
    /* legs */
    double deploy_frac;      /* 0..1 */
    int    deploy_cmd;
    double crush[4];         /* plastic stroke per leg (monotonic) */
    /* consumables */
    double N2;               /* cold-gas remaining kg */
    /* wind filter memory (Dryden) */
    double wind_filt[6];
    /* bookkeeping */
    double t;                /* sim time s */
    uint64_t step;
    int    phase;
    int    verdict;          /* grade code, set at settle */
    int    fault;            /* fault code if any */
    int    fins_deployed;    /* grid fins out (aero active) */
} State;

/* Actuator command intents from control layer (held constant across a physics step). */
typedef struct {
    double throttle;         /* 0..1 commanded */
    double gimbal[2];        /* rad commanded */
    double fins[4];          /* rad commanded */
    double rcs_torque[3];    /* body-frame torque from RCS this step (N m) */
    double rcs_dm;           /* N2 mass flow this step (kg/s) */
    double fin_eint[2];      /* persistent AoA-hold integral (pitch,yaw) — P5 move-the-trim-point */
    int    engine_cmd;       /* request engine on/off */
    int    n_eng;
    int    deploy_cmd;
} Actuators;

/* Module mask bits. */
enum { MOD_SLOSH=1, MOD_SEA=2, MOD_NAV_NOISY=4, MOD_FINS=8, MOD_TURB=16, MOD_INJECT=32 };

/* Phases (§6.8). */
enum { PH_INIT,PH_COAST,PH_ENTRY_BURN,PH_AERO,PH_LANDING_BURN,PH_TOUCHDOWN,PH_SETTLING,
       PH_LANDED,PH_TIPPED,PH_CRASHED,PH_FUEL_DEPLETED,PH_STRUCT_FAIL,PH_THERMAL_FAIL,PH_LOC };

/* Verdict grades (§7.3). */
enum { V_NONE,V_PERFECT,V_GOOD,V_HARD,V_TIPPED,V_CRASHED };

/* Fault codes. */
enum { F_NONE,F_FUEL,F_STRUCT,F_THERMAL,F_LOC,F_OFFPAD };

#endif /* BL_STATE_H */
