/* contact.h — leg spring-damper-crush, friction. CLAUDE_v1.md §7. */
#ifndef BL_CONTACT_H
#define BL_CONTACT_H
#include "state.h"
#include "dynamics.h"

/* World position of the lowest vehicle point (feet if deployed, else base rim). */
BL_HD double lowest_point_z(const State* st);

/* Any foot within contact band of the ground/deck. */
BL_HD int near_ground(const State* st, double band);

/* Contact wrench: world force Fc, world torque Tc about CoM, per-leg crush rate.
 * Returns feet-in-contact count. deck_z is ground height (0 for RTLS). */
BL_HD int contact_wrench(const State* st, double deck_z, double Fc[3], double Tc[3], double crush_rate[4]);

/* Semi-implicit substepped contact integrator (dt/8). Updates crush strokes. */
BL_HD void contact_substep(State* st, const Actuators* act, const EnvCtx* env, double deck_z, double dt);

#endif
