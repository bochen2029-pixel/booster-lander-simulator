/* integrator.h — RK4 flight step + helpers. CLAUDE_v1.md §6.7. */
#ifndef BL_INTEGRATOR_H
#define BL_INTEGRATOR_H
#include "state.h"
#include "dynamics.h"

/* One fixed RK4 step of the continuous block (flight, no contact).
 * Renormalizes quaternion after the full step; clamps propellant >= 0. */
BL_HD void rk4_step(State* st, const Actuators* act, const EnvCtx* env, double dt);

#endif
