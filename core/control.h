/* control.h — attitude PD + control allocation (gimbal / RCS). CLAUDE_v1.md §8.3. */
#ifndef BL_CONTROL_H
#define BL_CONTROL_H
#include "state.h"
#include "guidance.h"
#include "dynamics.h"

/* Map guidance intent + current state to actuator commands. */
BL_HD void control_step(const State* st, const GuidanceCmd* g, const EnvCtx* env, Actuators* act);

#endif
