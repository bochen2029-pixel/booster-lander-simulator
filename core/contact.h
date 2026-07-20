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
 * Returns feet-in-contact count. deck_z is ground/deck height (0 for RTLS). deck_vz is the deck's
 * VERTICAL velocity (SEA module heave rate, §A.2) — the leg spring-damper works in the DECK frame, so
 * the closing rate is the vehicle-relative (vz − deck_vz): a rising deck loads the legs harder, a
 * falling deck softer. deck_vz==0 (SEA off / static pad) => byte-identical to the pre-SEA law. */
BL_HD int contact_wrench(const State* st, double deck_z, double deck_vz, double Fc[3], double Tc[3], double crush_rate[4]);

/* Semi-implicit substepped contact integrator (dt/8). Updates crush strokes. deck_vz: see contact_wrench. */
BL_HD void contact_substep(State* st, const Actuators* act, const EnvCtx* env, double deck_z, double deck_vz, double dt);

#endif
