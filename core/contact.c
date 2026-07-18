/* contact.c — leg contact dynamics: spring-damper + crush core + Coulomb friction. */
#include "contact.h"
#include "constants.h"
#include <math.h>

static void foot_body_pos(const State* st, int i, double com, double p_rel[3]){
    double az = (double)i * (PI*0.5);           /* 0,90,180,270 deg */
    double df = st->deploy_frac; if(df<0)df=0; if(df>1)df=1;
    double rad = VEH_RADIUS + df*(LEG_RADIUS - VEH_RADIUS);
    double zf  = 0.0 - df*1.0;                   /* feet dip ~1 m below base when out */
    p_rel[0]=rad*cos(az); p_rel[1]=rad*sin(az); p_rel[2]=zf - com;
}

BL_HD double lowest_point_z(const State* st){
    MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
    const double* q=&st->y[S_QX];
    double lo=1e30;
    for(int i=0;i<4;i++){
        double prel[3],wp[3]; foot_body_pos(st,i,mp.com,prel); q_rot(wp,q,prel);
        double z = st->y[S_RZ]+wp[2]; if(z<lo)lo=z;
    }
    /* base rim also (crash geometry) */
    for(int i=0;i<4;i++){
        double az=(double)i*(PI*0.5)+PI*0.25;
        double prel[3]={VEH_RADIUS*cos(az),VEH_RADIUS*sin(az),0.0-mp.com},wp[3];
        q_rot(wp,q,prel); double z=st->y[S_RZ]+wp[2]; if(z<lo)lo=z;
    }
    return lo;
}
BL_HD int near_ground(const State* st, double band){ return lowest_point_z(st) < band; }

BL_HD int contact_wrench(const State* st, double deck_z, double Fc[3], double Tc[3], double crush_rate[4]){
    MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
    const double* q=&st->y[S_QX];
    double v[3]={st->y[S_VX],st->y[S_VY],st->y[S_VZ]};
    double w[3]={st->y[S_WX],st->y[S_WY],st->y[S_WZ]};
    double om_w[3]; q_rot(om_w,q,w);
    Fc[0]=Fc[1]=Fc[2]=0; Tc[0]=Tc[1]=Tc[2]=0;
    int nc=0;
    for(int i=0;i<4;i++){
        crush_rate[i]=0.0;
        double prel[3],rw[3]; foot_body_pos(st,i,mp.com,prel); q_rot(rw,q,prel);
        double fz = st->y[S_RZ]+rw[2];
        double pen = deck_z - fz;
        if(pen<=0.0) continue;
        nc++;
        double fv[3]; double cr[3]; v3_cross(cr,om_w,rw); v3_add(fv,v,cr);
        double vz=fv[2];
        double elastic = pen - st->crush[i]; if(elastic<0)elastic=0;
        double avail = LEG_CRUSH_S - st->crush[i];
        double Fn;
        double Fs = LEG_K*elastic;
        if(Fs<=LEG_CRUSH_F || avail<=0.0){
            if(avail<=0.0) Fs = LEG_CRUSH_F + LEG_K*10.0*elastic;   /* bottomed: stiff */
            Fn = Fs;
        } else {
            Fn = LEG_CRUSH_F;                                        /* plateau: crush strokes */
            crush_rate[i] = (vz<0.0)? -vz : 0.0;
        }
        Fn += -LEG_C*vz;
        if(Fn<0) Fn=0;
        if(Fn>5e6) Fn=5e6;                                          /* safety cap */
        /* friction (kinetic) opposing tangential velocity */
        double vt=sqrt(fv[0]*fv[0]+fv[1]*fv[1]);
        double Ff[3]={0,0,0};
        if(vt>1e-3){ double mu=LEG_MU_K; Ff[0]=-mu*Fn*fv[0]/vt; Ff[1]=-mu*Fn*fv[1]/vt; }
        double Ffoot[3]={Ff[0],Ff[1],Fn};
        v3_add(Fc,Fc,Ffoot);
        double Tf[3]; v3_cross(Tf,rw,Ffoot); v3_add(Tc,Tc,Tf);      /* rw = foot - com (world) */
    }
    return nc;
}

BL_HD void contact_substep(State* st, const Actuators* act, const EnvCtx* env, double deck_z, double dt){
    const int NS=8; double h=dt/NS;
    for(int s=0;s<NS;s++){
        double dyf[NSTATE]; dynamics_deriv(st,act,env,dyf,0);
        double Fc[3],Tc[3],cr[4]; contact_wrench(st,deck_z,Fc,Tc,cr);
        MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
        double q[4]={st->y[S_QX],st->y[S_QY],st->y[S_QZ],st->y[S_QW]};
        /* accel: flight + contact */
        double a[3]={dyf[S_VX]+Fc[0]/mp.m, dyf[S_VY]+Fc[1]/mp.m, dyf[S_VZ]+Fc[2]/mp.m};
        /* wdot: flight + contact torque (world->body, /I) */
        double tcb[3]; q_rot_inv(tcb,q,Tc);
        double wdot[3]={ dyf[S_WX]+tcb[0]/mp.I_tr, dyf[S_WY]+tcb[1]/mp.I_tr, dyf[S_WZ]+tcb[2]/mp.I_ax };
        /* semi-implicit: update v then r */
        st->y[S_VX]+=a[0]*h; st->y[S_VY]+=a[1]*h; st->y[S_VZ]+=a[2]*h;
        st->y[S_RX]+=st->y[S_VX]*h; st->y[S_RY]+=st->y[S_VY]*h; st->y[S_RZ]+=st->y[S_VZ]*h;
        st->y[S_WX]+=wdot[0]*h; st->y[S_WY]+=wdot[1]*h; st->y[S_WZ]+=wdot[2]*h;
        double qd[4],wv[3]={st->y[S_WX],st->y[S_WY],st->y[S_WZ]};
        q_deriv(qd,q,wv);
        st->y[S_QX]+=qd[0]*h; st->y[S_QY]+=qd[1]*h; st->y[S_QZ]+=qd[2]*h; st->y[S_QW]+=qd[3]*h;
        q_normalize(&st->y[S_QX]);
        /* propellant + actuator states from flight deriv */
        st->y[S_MLOX]+=dyf[S_MLOX]*h; st->y[S_MRP1]+=dyf[S_MRP1]*h;
        if(st->y[S_MLOX]<0)st->y[S_MLOX]=0; if(st->y[S_MRP1]<0)st->y[S_MRP1]=0;
        st->y[S_THR]+=dyf[S_THR]*h; if(st->y[S_THR]<0)st->y[S_THR]=0; if(st->y[S_THR]>1)st->y[S_THR]=1;
        st->y[S_G0]+=dyf[S_G0]*h; st->y[S_G1]+=dyf[S_G1]*h;
        st->y[S_GR0]+=dyf[S_GR0]*h; st->y[S_GR1]+=dyf[S_GR1]*h;
        st->y[S_QHEAT]+=dyf[S_QHEAT]*h;
        for(int i=0;i<4;i++){ st->crush[i]+=cr[i]*h; if(st->crush[i]>LEG_CRUSH_S)st->crush[i]=LEG_CRUSH_S; }
    }
}
