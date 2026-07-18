/* dynamics.h — shared equations of motion (host now, __host__ __device__ at M4).
 * The single dynamics source (directive 7): plant, hoverslam predictor, MPPI rollouts.
 */
#ifndef BL_DYNAMICS_H
#define BL_DYNAMICS_H

#include "state.h"
#include "vmath.h"

typedef struct {
    double m;            /* total mass kg */
    double com;          /* CoM height above base (body z) m */
    double I_ax;         /* axial inertia about centerline */
    double I_tr;         /* transverse inertia about CoM */
    double Idot_ax;      /* d/dt axial from mdot */
    double Idot_tr;      /* d/dt transverse from mdot */
} MassProps;

/* Environment sample held constant across a physics step. */
typedef struct {
    double wind_world[3];    /* mean+turbulence wind, world m/s */
    int    module_mask;
    int    fins_deployed;
    /* INJECT_DISTURBANCE (Tier-B robustness, seeded → replayable). 0 is read as nominal so any
     * EnvCtx zero-initialized by a caller (oracles) stays bit-identical. */
    double thrust_scale;     /* multiplicative thrust deficit (1.0 nominal; 0 → treated as 1.0) */
    double isp_scale;        /* multiplicative Isp deficit    (1.0 nominal; 0 → treated as 1.0) */
    double com_offset[2];    /* lateral CoM offset [m] → thrust-misalignment disturbance torque   */
} EnvCtx;

/* Diagnostics filled during a derivative eval (for telemetry / termination). */
typedef struct {
    double mach, qbar, alpha, p_amb, rho;
    double qdot_heat;        /* Sutton-Graves W/m^2 */
    double thrust;           /* current thrust magnitude N (all engines) */
    double f_aero_world[3];
    double a_body[3];        /* specific force (accel) in body frame, for HUD accel */
    double twr;
} Diag;

/* Mass properties + analytic Idot from mdot (App-A.4). */
BL_HD void mass_props(double m_lox, double m_rp1, double mdot_lox, double mdot_rp1, MassProps* mp);

/* Compute continuous-state derivative dy given full state, actuator intents, environment.
 * ign_timer/engine_on/n_eng are read from *st (held constant across the step's substeps).
 * Optionally fills diag (may be NULL). */
BL_HD void dynamics_deriv(const State* st, const Actuators* act, const EnvCtx* env,
                          double dy[NSTATE], Diag* diag);

/* Thrust magnitude for one engine at ambient pressure p (N). */
BL_HD double engine_thrust(double throttle_act, double p_amb);

/* Ignition ramp fraction [0,1] from time since ignition command. */
BL_HD double ignition_ramp(double ign_timer);

#endif
