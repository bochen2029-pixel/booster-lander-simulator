/* integrator.c — classic RK4 over the flat continuous state. */
#include "integrator.h"
#include "vmath.h"
#include "constants.h"

BL_HD void rk4_step(State* st, const Actuators* act, const EnvCtx* env, double dt){
    double y0[NSTATE], k1[NSTATE], k2[NSTATE], k3[NSTATE], k4[NSTATE];
    for(int i=0;i<NSTATE;i++) y0[i]=st->y[i];
    State tmp = *st;   /* copies engine_on/n_eng/ign_timer held constant across substeps */

    dynamics_deriv(st, act, env, k1, 0);
    for(int i=0;i<NSTATE;i++) tmp.y[i]=y0[i]+0.5*dt*k1[i];
    dynamics_deriv(&tmp, act, env, k2, 0);
    for(int i=0;i<NSTATE;i++) tmp.y[i]=y0[i]+0.5*dt*k2[i];
    dynamics_deriv(&tmp, act, env, k3, 0);
    for(int i=0;i<NSTATE;i++) tmp.y[i]=y0[i]+dt*k3[i];
    dynamics_deriv(&tmp, act, env, k4, 0);

    for(int i=0;i<NSTATE;i++)
        st->y[i]=y0[i]+(dt/6.0)*(k1[i]+2.0*k2[i]+2.0*k3[i]+k4[i]);

    /* renormalize quaternion (once, after the full step) */
    double* q=&st->y[S_QX]; q_normalize(q);
    /* clamp propellant */
    if(st->y[S_MLOX]<0) st->y[S_MLOX]=0;
    if(st->y[S_MRP1]<0) st->y[S_MRP1]=0;
    /* gimbal saturation with RATE ANTI-WINDUP (audit BUG-1): clamp position AND zero the 2nd-
     * order rate state when it drives further into the stop, else the rate winds to ~22 deg/s
     * while pinned and causes ~0.55 s reversal lash on every powered landing. */
    for(int i=0;i<2;i++){ double* g=&st->y[S_G0+i]; double* gr=&st->y[S_GR0+i];
        if(*g> ENG_GIMBAL_MAX){ *g= ENG_GIMBAL_MAX; if(*gr>0)*gr=0; }
        if(*g<-ENG_GIMBAL_MAX){ *g=-ENG_GIMBAL_MAX; if(*gr<0)*gr=0; } }
    if(st->y[S_THR]<0) st->y[S_THR]=0; if(st->y[S_THR]>1) st->y[S_THR]=1;
    for(int i=0;i<4;i++){ double* f=&st->y[S_F0+i];
        if(*f> FIN_DEFL_MAX)*f=FIN_DEFL_MAX; if(*f<-FIN_DEFL_MAX)*f=-FIN_DEFL_MAX; }
}
