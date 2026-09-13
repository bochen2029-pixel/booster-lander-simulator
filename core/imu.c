/* imu.c — gimbaled inertial platform. See imu.h. Port of the stabilisation loop in
 * assets/apollo_imu/gimbal-scene.js (Core.step, 2 ms) to the plant rate; no RNG, no wall clock. */
#include "imu.h"
#include "constants.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define IMU_DEG2RAD 0.017453292519943295
#define IMU_PI      3.14159265358979323846

/* ---- the mount: case X = body Z, case Y = body X, case Z = body Y ----------------------------
 * As a rotation taking case-frame vectors to body-frame vectors: columns = case axes in body coords
 * (0,0,1) (1,0,0) (0,1,0) => the cyclic permutation x->z, y->x, z->y = a -120 deg turn about
 * (1,1,1)/sqrt(3): q = (-1/2, -1/2, -1/2, +1/2). The same permutation maps SM axes to world axes for
 * the landing-site REFSMMAT (SM X = up, Y = east, Z = north). */
static const double Q_CASE_IN_BODY[4] = { -0.5, -0.5, -0.5, 0.5 };
static const double Q_SM_IN_WORLD[4]  = { -0.5, -0.5, -0.5, 0.5 };

void imu_mount_quat(double out[4]){ memcpy(out, Q_CASE_IN_BODY, sizeof(Q_CASE_IN_BODY)); }

static inline double wrap_pi(double a){ while(a> IMU_PI) a-=2*IMU_PI; while(a<-IMU_PI) a+=2*IMU_PI; return a; }
static inline double clampd(double v, double lo, double hi){ return v<lo?lo:(v>hi?hi:v); }

static inline void q_axis(double o[4], int axis, double a){
    double s=sin(0.5*a), c=cos(0.5*a);
    o[0]=o[1]=o[2]=0.0; o[axis]=s; o[3]=c;
}
/* SM->case rotation from the gimbal angles: Rg = Rx(o) * Rz(m) * Ry(i)  (v_case = Rg v_SM) */
static void q_gimbals(const double ga[3], double out[4]){
    double qx[4], qz[4], qy[4], t[4];
    q_axis(qx, 0, ga[0]); q_axis(qz, 2, ga[1]); q_axis(qy, 1, ga[2]);
    q_mul(t, qz, qy); q_mul(out, qx, t);
}
/* rotation matrix (row-major m[r][c]) of a unit quaternion xyzw */
static void q_to_mat(const double q[4], double m[3][3]){
    double x=q[0], y=q[1], z=q[2], w=q[3];
    m[0][0]=1-2*(y*y+z*z); m[0][1]=2*(x*y-w*z);   m[0][2]=2*(x*z+w*y);
    m[1][0]=2*(x*y+w*z);   m[1][1]=1-2*(x*x+z*z); m[1][2]=2*(y*z-w*x);
    m[2][0]=2*(x*z-w*y);   m[2][1]=2*(y*z+w*x);   m[2][2]=1-2*(x*x+y*y);
}
/* (o, m, i) from R = Rx(o) Rz(m) Ry(i):  m01 = -sin m,  m21 = sin o cos m,  m11 = cos o cos m,
 * m02 = cos m sin i,  m00 = cos m cos i  (the same extraction as ui/src/hud/imu.ts). */
static void angles_from_q(const double q[4], double omi[3]){
    double m[3][3]; q_to_mat(q, m);
    omi[1] = asin(clampd(-m[0][1], -1.0, 1.0));
    omi[0] = atan2(m[2][1], m[1][1]);
    omi[2] = atan2(m[0][2], m[0][0]);
}

void imu_ideal_angles(const double q_body_world[4], const double q_ref[4], double out_omi[3]){
    /* SM->case = (case->world)^-1 * (SM->world) = (q_body * q_cb)^-1 * q_ref */
    double q_case[4], q_case_inv[4], rel[4];
    q_mul(q_case, q_body_world, Q_CASE_IN_BODY);
    q_conj(q_case_inv, q_case);
    q_mul(rel, q_case_inv, q_ref);
    q_normalize(rel);
    angles_from_q(rel, out_omi);
}

void imu_init(ImuState* imu, int on, double rate_max_deg_s, const double q_body_world[4]){
    memset(imu, 0, sizeof(*imu));
    imu->on = on ? 1 : 0;
    /* gimbal-scene.js defaults: wn 120 rad/s, zeta 0.75, acc 6000 deg/s^2, rate 180 deg/s, floats 3 deg */
    imu->wn = 120.0; imu->zeta = 0.75; imu->acc_max = 6000.0*IMU_DEG2RAD;
    imu->rate_max = (rate_max_deg_s > 0.0 ? rate_max_deg_s : 180.0) * IMU_DEG2RAD;
    imu->float_max = 3.0*IMU_DEG2RAD; imu->sec_floor = 0.02; imu->ki = 1;
    memcpy(imu->q_ref, Q_SM_IN_WORLD, sizeof(imu->q_ref));
    imu_ideal_angles(q_body_world, imu->q_ref, imu->ga);      /* ALIGN: gimbals at the ideal angles */
    /* the belief equals the truth at alignment */
    memcpy(imu->q_meas, q_body_world, sizeof(imu->q_meas));
    imu->margin_min = 0.5*IMU_PI;
    /* seed the float integrator with the real SM pose so the first step sees no phantom rotation */
    {
        double q_case[4], q_g[4];
        q_mul(q_case, q_body_world, Q_CASE_IN_BODY);
        q_gimbals(imu->ga, q_g);
        q_mul(imu->q_sm_prev, q_case, q_g);
        q_normalize(imu->q_sm_prev);
        imu->have_prev = 1;
    }
}

static void servo_step(ImuState* imu, int k, double cmd, double dt){
    double err = wrap_pi(cmd - imu->ga[k]);
    double ki_gain = imu->ki ? 0.1*imu->wn*imu->wn*imu->wn : 0.0;
    double acc = imu->wn*imu->wn*err + ki_gain*imu->gi[k] - 2.0*imu->zeta*imu->wn*imu->gr[k];
    int sat = fabs(acc) > imu->acc_max;
    acc = clampd(acc, -imu->acc_max, imu->acc_max);
    if(ki_gain>0.0 && !sat) imu->gi[k] = clampd(imu->gi[k] + err*dt, -imu->acc_max/ki_gain, imu->acc_max/ki_gain);
    imu->ga[k] += dt*imu->gr[k];
    imu->gr[k] += dt*acc;
    if(fabs(imu->gr[k]) > imu->rate_max){ imu->gr[k] = (imu->gr[k]<0?-1.0:1.0)*imu->rate_max; sat = 1; }
    if(sat) imu->sat_steps++;
    if(fabs(imu->gr[k]) > imu->rate_peak) imu->rate_peak = fabs(imu->gr[k]);
}

void imu_step(ImuState* imu, const double q_body_world[4], double t, double dt){
    if(!imu->on) return;
    /* 1. rigid kinematics: where the SM actually is, from the case attitude and the ACTUAL gimbals */
    double q_case[4], q_g[4], q_sm[4];
    q_mul(q_case, q_body_world, Q_CASE_IN_BODY);
    q_gimbals(imu->ga, q_g);
    q_mul(q_sm, q_case, q_g);
    q_normalize(q_sm);
    /* 2. IRIG floats integrate the SM's inertial rotation about the SM axes; stops at +-float_max */
    if(imu->have_prev){
        double prev_inv[4], dq[4];
        q_conj(prev_inv, imu->q_sm_prev);
        q_mul(dq, prev_inv, q_sm);
        double sg = dq[3] < 0.0 ? -2.0 : 2.0;
        imu->th[0] += sg*dq[0]; imu->th[1] += sg*dq[1]; imu->th[2] += sg*dq[2];
    }
    memcpy(imu->q_sm_prev, q_sm, sizeof(q_sm)); imu->have_prev = 1;
    for(int k=0;k<3;k++){
        int hit = 0;
        if(imu->th[k] >  imu->float_max){ imu->th[k] =  imu->float_max; hit=1; }
        if(imu->th[k] < -imu->float_max){ imu->th[k] = -imu->float_max; hit=1; }
        if(hit && !imu->lost){
            imu->lost=1; imu->t_lost=t;
            /* journal (stderr, module-on only): when, which float, the gimbal rates at that moment */
            fprintf(stderr, "  [imu] reference LOST t=%.2f float=%c gimbal rates o/m/i = %.0f/%.0f/%.0f deg/s (limit %.0f) MGA=%.1f\n",
                    t, "xyz"[k], imu->gr[0]*57.29577951308232, imu->gr[1]*57.29577951308232, imu->gr[2]*57.29577951308232,
                    imu->rate_max*57.29577951308232, imu->ga[1]*57.29577951308232);
        }
    }
    /* 3. resolver chain: float angles -> gimbal displacements (sec MGA on the outer axis) */
    double ci=cos(imu->ga[2]), si=sin(imu->ga[2]), cm=cos(imu->ga[1]), sm=sin(imu->ga[1]);
    double cmL = cm; if(fabs(cmL) < imu->sec_floor) cmL = (cmL<0?-1.0:1.0)*imu->sec_floor;
    double dO = -(imu->th[0]*ci + imu->th[2]*si)/cmL;
    double dM = -(imu->th[2]*ci - imu->th[0]*si);
    double dI = -imu->th[1] + sm*dO;
    /* 4. torque-motor servos */
    servo_step(imu, 0, imu->ga[0] + dO, dt);
    servo_step(imu, 1, imu->ga[1] + dM, dt);
    servo_step(imu, 2, imu->ga[2] + dI, dt);
    /* 5. the belief: the platform assumes the SM sits at the reference, so
     *    case->world(meas) = q_ref * (SM->case)^-1 ; body->world(meas) = that * (case<-body)^-1 */
    {
        double q_g2[4], q_g_inv[4], q_case_meas[4], q_cb_inv[4];
        q_gimbals(imu->ga, q_g2);
        q_conj(q_g_inv, q_g2);
        q_mul(q_case_meas, imu->q_ref, q_g_inv);
        q_conj(q_cb_inv, Q_CASE_IN_BODY);
        q_mul(imu->q_meas, q_case_meas, q_cb_inv);
        q_normalize(imu->q_meas);
    }
    /* diagnostics: platform error and the lock margin */
    {
        double d = fabs(q_sm[0]*imu->q_ref[0] + q_sm[1]*imu->q_ref[1] + q_sm[2]*imu->q_ref[2] + q_sm[3]*imu->q_ref[3]);
        imu->err = 2.0*acos(clampd(d, -1.0, 1.0));
        if(imu->err > imu->max_err) imu->max_err = imu->err;
        double margin = 0.5*IMU_PI - fabs(wrap_pi(imu->ga[1]));
        if(margin < imu->margin_min) imu->margin_min = margin;
    }
}
