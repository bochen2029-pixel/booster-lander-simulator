/* imu.h — the gimbaled inertial platform as a MEASUREMENT of attitude that can be LOST.
 * PLAN.md Phase 2.2 (D-058, 2026-09-12). Default OFF => byte-identical: sim.c hands control_step
 * and the nav view the SAME truth pointers it always did; the module is a guarded pointer swap.
 *
 * WHAT IT MODELS (the operator's Apollo Block II asset, assets/apollo_imu/gimbal-scene.js, ported
 * to the plant rate; the ~60-line kernel in ui/src/hud/imu.ts is the display twin):
 *   A stable member (SM) is held inertially fixed inside three gimbals — outer (OGA, case X),
 *   middle (MGA, case Z), inner (IGA, case Y) — by torque-motor servos that null the SM's gyro
 *   float angles through the resolver chain  dO = -(thx cos i + thz sin i)/cos m,
 *   dM = -(thz cos i - thx sin i),  dI = -thy + sin m * dO.  The outer command carries sec(MGA):
 *   as |MGA| -> 90 deg the outer gimbal must slew infinitely fast (gimbal lock). Each servo is the
 *   2nd-order, acceleration- and RATE-limited loop of uploads/p1_gimbal.c. When the case rotates
 *   faster than the gimbals can follow, the SM is dragged, the floats grow, and at the float
 *   stops (+-3 deg) the gyros saturate: the reference is LOST — from then on the platform's belief
 *   of the case attitude is wrong by the accumulated drift until a re-alignment, which the
 *   descent never has time for.
 *
 * MOUNT (one convention, stated once, same as the FDAI): outer axis along the vehicle long axis —
 *   case X = body Z, case Y = body X, case Z = body Y (a proper rotation). REFSMMAT = the landing-site
 *   frame under the same permutation (SM X = world up, Y = east, Z = north), so an upright vehicle
 *   at heading zero reads 0/0/0 with 90 deg of MGA margin; a tilt about north is MGA (the lock axis).
 *
 * WHAT THE FLIGHT COMPUTER SEES when the module is on: q_meas = the platform's belief, at 500 Hz in
 *   control_step and in the 50 Hz nav view. omega is untouched (the rate gyros are body-mounted).
 *
 * THE TRAP, restated: this is a platform-REFERENCE failure. F_LOC (sim.c) is a control-AUTHORITY
 *   failure. They can cause each other; they are not the same quantity. */
#ifndef BL_IMU_H
#define BL_IMU_H

#include "vmath.h"

typedef struct {
    int    on;              /* MOD_IMU */
    /* servo + float parameters (gimbal-scene.js defaults unless overridden by the CLI) */
    double wn, zeta, acc_max, rate_max, float_max, sec_floor;
    int    ki;              /* loop integrator on (gimbal-scene.js p.ki) */
    /* platform state */
    double q_ref[4];        /* REFSMMAT: SM -> world (xyzw) */
    double ga[3], gr[3], gi[3]; /* gimbal angle, rate, integrator per axis: 0=outer 1=middle 2=inner [rad] */
    double th[3];           /* IRIG float angles, SM axes [rad] */
    double q_sm_prev[4];    /* SM -> world last step */
    int    have_prev;
    /* outputs / diagnostics */
    double q_meas[4];       /* the platform's belief of body -> world (xyzw) */
    double err;             /* platform error: angle between the real SM and the reference [rad] */
    double max_err;         /* over the flight */
    double margin_min;      /* min (90 deg - |MGA|) over the flight [rad] */
    double rate_peak;       /* peak |gimbal rate demand| seen [rad/s] */
    int    lost;            /* latched: floats saturated => reference lost */
    double t_lost;          /* sim time of first loss [s] */
    int    sat_steps;       /* steps with a rate-saturated servo */
    int    quiet;           /* 1 in a search candidate / async snapshot: no stderr journal */
} ImuState;

/* Align the platform: REFSMMAT = landing-site frame, gimbals at the ideal angles for the initial
 * attitude, floats zero. q_body_world = the truth attitude at t0. rate_max_deg_s <= 0 => default. */
void imu_init(ImuState* imu, int on, double rate_max_deg_s, const double q_body_world[4]);

/* One plant step (dt = DT): kinematics -> floats -> resolvers -> servos -> belief. Reads the TRUE
 * body->world attitude; writes imu->q_meas and the diagnostics. */
void imu_step(ImuState* imu, const double q_body_world[4], double t, double dt);

/* Gimbal angles (o, m, i) [rad] of the platform's IDEAL solution for a body attitude against a
 * reference — the pure kinematic kernel, exposed for the selftest oracle. */
void imu_ideal_angles(const double q_body_world[4], const double q_ref[4], double out_omi[3]);

/* The case<-body mount permutation as a quaternion (xyzw), exposed for the selftest. */
void imu_mount_quat(double out[4]);

#endif
