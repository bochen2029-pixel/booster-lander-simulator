/* stability_sim.c — DEFINITIVE bare-body pitch stability test: integrate a small
 * AoA perturbation forward in time using the sim's EXACT aero pitch model and see
 * whether |alpha| GROWS (unstable) or DECAYS/oscillates-bounded (stable). No verbal
 * "static margin sign" convention needed -- just watch the number.
 *
 * Reduced planar pitch model (x-z plane), matching dynamics.c:
 *  - vehicle axial speed V (mostly downward, base-first) fixed for the test.
 *  - pitch angle theta (body tilt from velocity vector) == AoA alpha at zero rate.
 *  - I_tr transverse inertia (from mass_props, ~ big).
 *  - aero normal force Fn = qbar*Aref*CNa*alpha at xcp; torque = (xcp-com)*Fx.
 *  - INCLUDE the body Cmq damping exactly as dynamics.c (strip theory) so we see
 *    the true damped response.
 * Bare body only (NO fins) -- this is the D-005 "bare body (un)stable" question.
 */
#define _USE_MATH_DEFINES
#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.141592653589793
#endif
#define VEH_STAGE_LEN 41.2
#define VEH_LEN 47.7
#define VEH_DIA 3.66
#define VEH_AREF 10.52
#define BODY_CMQ_CDC 0.6
#define DEG2RAD 0.017453292519943295

static double xcp_frac(double M){return 0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));}

/* transverse inertia about CoM for the descent mass (approx, thin cylinder + prop).
 * Use a representative I_tr ~ 1/12 m L^2 with m=42.4t (aero-descent) -> good enough
 * for the sign of the response (magnitude sets timescale only). */
static double I_tr(double m){ return m*(VEH_LEN*VEH_LEN)/12.0*0.5; } /* 0.5 fudge for hollow */

int main(void){
    double m=42400.0, com=12.27;
    double rho=0.30, a_snd=310.0;          /* ~8-10 km US76-ish */
    double Ms[]={0.30,0.70,0.90,1.05,1.20,1.50,2.00};
    double CNa=2.4;
    double dt=0.002; int steps=6000;       /* 12 s */

    printf("Bare-body AoA perturbation growth test (start alpha=2deg, w=0).\n");
    printf("Report alpha at t=2,4,8,12 s. GROWS=>unstable, decays/bounded=>stable.\n");
    printf("(Cmq strip-theory damping INCLUDED, exactly as dynamics.c.)\n\n");
    printf("%-6s %8s | %8s %8s %8s %8s | verdict\n","Mach","xcp-com","a(0)","a(2s)","a(4s)","a(12s)");
    for(int mi=0; mi<7; mi++){
        double M=Ms[mi], V=M*a_snd;
        double qbar=0.5*rho*V*V;
        double xcp=xcp_frac(M)*VEH_STAGE_LEN;
        double arm=xcp-com;
        double I=I_tr(m);
        /* Cmq (strip theory) J integral like dynamics.c */
        double zc=com, L=VEH_LEN;
        double J=L*L*L/3.0 - zc*L*L + zc*zc*L;
        double Cdamp=0.5*rho*V*BODY_CMQ_CDC*VEH_DIA*J;
        /* state: alpha (rad), q=alpha_dot (rad/s) */
        double alpha=2.0*DEG2RAD, q=0.0;
        double a0=alpha, a2=0,a4=0,a12=0;
        for(int k=0;k<steps;k++){
            /* aero pitch torque about +Y for current alpha (small angle):
             * Fx = -qbar*Aref*CNa*alpha (normal force opposes +X wind for +alpha).
             * Ty = arm*Fx = -arm*qbar*Aref*CNa*alpha.  Then damping -Cdamp*q. */
            double Ty = -arm*qbar*VEH_AREF*CNa*alpha - Cdamp*q;
            double qd = Ty/I;
            /* the AoA rate: for a body pitching at rate q in a steady descent, alpha
             * changes at ~q (the velocity vector is ~inertially fixed over 12 s). */
            q += qd*dt;
            alpha += q*dt;
            double t=(k+1)*dt;
            if(fabs(t-2.0)<dt) a2=alpha;
            if(fabs(t-4.0)<dt) a4=alpha;
            if(fabs(t-12.0)<dt) a12=alpha;
        }
        const char* v = (fabs(a12)>fabs(a0)*1.5)?"UNSTABLE (grows)":
                        (fabs(a12)<fabs(a0)*0.7)?"STABLE (decays)":"~neutral";
        printf("%-6.2f %+8.3f | %8.3f %8.3f %8.3f %8.3f | %s\n",
               M, arm, a0/DEG2RAD, a2/DEG2RAD, a4/DEG2RAD, a12/DEG2RAD, v);
    }
    printf("\nSIGN LAW (settled): Ty = -arm*qbar*Aref*CNa*alpha, arm=xcp-com.\n");
    printf(" arm>0 (xcp ABOVE/aft of com): Ty opposes alpha => RESTORING => STABLE.\n");
    printf(" arm<0 (xcp BELOW/fwd of com, toward leading base): Ty grows alpha => UNSTABLE.\n");
    return 0;
}
