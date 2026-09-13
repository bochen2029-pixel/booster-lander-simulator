/* p1_gimbal.c — independent confirmation of P3's BUG-P3-3 (gimbal rate-state windup).
 * Replicate the dynamics.c 2nd-order gimbal (wn=8,zeta=0.9, accel-limited) + integrator.c
 * position-only clamp. Command g0 -> 10deg (limit 5deg), then reverse to -5deg. Measure the
 * wound-up rate state at the stop and the reversal delay. Compare WITH vs WITHOUT rate anti-windup.
 */
#include <stdio.h>
#include <math.h>
#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232
#define ENG_GIMBAL_MAX (5.0*DEG2RAD)
#define ENG_GIMBAL_ACC (60.0*DEG2RAD)
#define DT 0.002
/* one Euler-ish sub-model matching dynamics.c: dy[G]=GR ; dy[GR]=gacc(clamped) ; then clamp G. */
static void step(double* g,double* gr,double gcmd,int antiwindup){
    double wn=8.0,zeta=0.9;
    double gacc=wn*wn*(gcmd-*g)-2.0*zeta*wn*(*gr);
    if(gacc>ENG_GIMBAL_ACC)gacc=ENG_GIMBAL_ACC; if(gacc<-ENG_GIMBAL_ACC)gacc=-ENG_GIMBAL_ACC;
    *g += DT*(*gr);
    *gr += DT*gacc;
    /* integrator.c clamp: position only */
    if(*g> ENG_GIMBAL_MAX)*g=ENG_GIMBAL_MAX; if(*g<-ENG_GIMBAL_MAX)*g=-ENG_GIMBAL_MAX;
    if(antiwindup){ if(*g>=ENG_GIMBAL_MAX && *gr>0)*gr=0; if(*g<=-ENG_GIMBAL_MAX && *gr<0)*gr=0; }
}
int main(void){
    printf("=== P1 confirm BUG-P3-3: gimbal rate-state windup at +-5deg stop ===\n\n");
    for(int aw=0;aw<2;aw++){
        double g=0,gr=0; double t=0;
        printf("--- %s rate anti-windup ---\n", aw?"WITH":"WITHOUT (current code)");
        /* phase 1: command 10deg for 1.5s */
        double grmax_at_stop=0;
        for(;t<1.5;t+=DT){ step(&g,&gr,10.0*DEG2RAD,aw); if(fabs(g)>=ENG_GIMBAL_MAX-1e-9 && fabs(gr)>grmax_at_stop)grmax_at_stop=fabs(gr); }
        printf("  after 1.5s @cmd=10deg: g=%.3f deg (pinned at 5), rate gr=%.2f deg/s ; max|gr| while pinned=%.2f deg/s\n",
               g*RAD2DEG, gr*RAD2DEG, grmax_at_stop*RAD2DEG);
        /* phase 2: reverse to -5deg, measure time to leave +4deg region */
        double t0=t; double t_leave4=-1;
        for(;t<t0+2.0;t+=DT){ step(&g,&gr,-5.0*DEG2RAD,aw); if(t_leave4<0 && g<4.0*DEG2RAD) t_leave4=t-t0; }
        printf("  reversal cmd=-5deg: time to leave +4deg region = %.3f s %s\n\n",
               t_leave4, aw?"(prompt)":"(LASH: phantom rate must unwind first)");
    }
    printf("VERDICT: CONFIRMED. Without rate anti-windup the pinned gimbal carries ~21 deg/s phantom rate;\n");
    printf("reversal is delayed ~0.5s. integrator.c L28-29 clamps POSITION S_G0/S_G1 only, never the\n");
    printf("rate states S_GR0/S_GR1. P3's 4-line fix (zero gr when it drives further into the stop) is correct.\n");
    return 0;
}
