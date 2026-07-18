/* runs/ign_probe.c — SCRATCH analysis harness (NOT part of the build).
 * Links against the frozen core objects (everything except main.obj) and drives the
 * sim at full step resolution, printing a per-term force decomposition in the RADIAL
 * (outward-positive) world-lateral frame across the landing-burn ignition window.
 *
 * Decomposes, each step, the LATERAL world specific-force on the vehicle into:
 *   a_thr_lat  = thrust * sin(tilt) / m           projected onto radial dir  (thrust x tilt)
 *   a_aero_lat = the plant's Faero_b lateral (WITH SRP shielding) / m, radial (crosswind side-force)
 *   a_cmd_lat  = guidance g->a_lat (post steer_sign, post integral) radial   (what guidance WANTS)
 * plus reports CT, shield, qbar, tilt, thr, ramp, wind, vrad, lat.
 * Build (MSVC, reuse Release objects):
 *   cl /nologo /O2 /fp:precise /I core runs\ign_probe.c <core release objs except main.obj> ws2_32.lib
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "sim.h"
#include "dynamics.h"
#include "constants.h"
#include "atmosphere.h"
#include "scenario.h"
#include "vmath.h"

/* mirror of dynamics.c aero tables (read-only recompute of the plant's lateral aero force) */
static const double AM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0};
static const double ACA[9]={0.85,0.88,1.10,1.40,1.25,1.10,0.95,0.92,0.90};
static const double ACN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
static double tl(const double*xs,const double*ys,int n,double x){
    if(x<=xs[0])return ys[0]; if(x>=xs[n-1])return ys[n-1];
    for(int i=0;i<n-1;i++) if(x<=xs[i+1]){double t=(x-xs[i])/(xs[i+1]-xs[i]);return ys[i]+t*(ys[i+1]-ys[i]);}
    return ys[n-1];
}

int main(int argc,char**argv){
    int scen=SCEN_ENTRY; uint32_t seed=42,run=1; double t0=110.0,t1=125.0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--scenario")&&i+1<argc) scen=scenario_from_name(argv[++i]);
        else if(!strcmp(argv[i],"--seed")&&i+1<argc) seed=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--run")&&i+1<argc) run=(uint32_t)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--t0")&&i+1<argc) t0=atof(argv[++i]);
        else if(!strcmp(argv[i],"--t1")&&i+1<argc) t1=atof(argv[++i]);
    }
    Sim s; sim_init(&s,scen,seed,run,MOD_TURB,GM_HOVERSLAM);
    printf("# scenario=%s seed=%u run=%u  window t=[%.1f,%.1f]  wind_az=%.4f rad u_ref=%.1f\n",
        scenario_name(scen),seed,run,t0,t1,s.se.wind_az,s.se.u_ref);
    printf("# t h vz thr ramp tilt lat vrad qbar CT shield  a_thrLat a_aeroLat a_cmdLat  windR windMag  steersgn amax tiltcap  phase eng\n");
    while(sim_step(&s)){
        double t=s.st.t;
        if(s.st.step % 25 == 0 && t>=t0 && t<=t1){   /* 25 steps = 0.05 s */
            const double* y=s.st.y;
            MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
            double h=y[S_RZ]-mp.com;
            double lat=sqrt(y[S_RX]*y[S_RX]+y[S_RY]*y[S_RY]);
            double rhat[2]={0,0}; if(lat>1e-6){rhat[0]=y[S_RX]/lat; rhat[1]=y[S_RY]/lat;}
            double vrad=rhat[0]*y[S_VX]+rhat[1]*y[S_VY];
            /* wind (recompute via a spare deriv already done in sim; use env.wind_world) */
            double wind[3]={s.env.wind_world[0],s.env.wind_world[1],s.env.wind_world[2]};
            double windR=rhat[0]*wind[0]+rhat[1]*wind[1];
            double windMag=sqrt(wind[0]*wind[0]+wind[1]*wind[1]);
            /* recompute the plant lateral aero force exactly as dynamics.c does */
            AtmoOut atm; atmo_eval(y[S_RZ],&atm);
            double q[4]={y[S_QX],y[S_QY],y[S_QZ],y[S_QW]};
            double v[3]={y[S_VX],y[S_VY],y[S_VZ]};
            double vrel_w[3]={v[0]-wind[0],v[1]-wind[1],v[2]-wind[2]};
            double vrel_b[3]; q_rot_inv(vrel_b,q,vrel_w);
            double speed=v3_norm(vrel_b);
            double mach=speed/atm.a;
            double qbar=0.5*atm.rho*speed*speed;
            double ramp=s.st.engine_on?ignition_ramp(s.st.ign_timer):0.0;
            double thr=s.st.n_eng*engine_thrust(y[S_THR],atm.p)*ramp;
            double CT=0.0, shield=1.0, aeroLatW=0.0;
            if(speed>0.2&&qbar>1e-4){
                double vhat[3]={vrel_b[0]/speed,vrel_b[1]/speed,vrel_b[2]/speed};
                double cosa=-vhat[2]; if(cosa>1)cosa=1; if(cosa<-1)cosa=-1;
                double alpha=acos(cosa);
                double CNa=tl(AM,ACN,9,mach); double CN=CNa*alpha;
                if(thr>0.0){ CT=thr/(qbar*VEH_AREF);
                    if(CT>0.5){double tt=(CT-0.5)/(3.0-0.5); if(tt>1)tt=1; shield=1.0+tt*(0.05-1.0);} CN*=shield; }
                double lath[3]={vhat[0],vhat[1],0}; double latm=sqrt(lath[0]*lath[0]+lath[1]*lath[1]);
                double Faero_b[3]={0,0,0};
                if(latm>1e-6){double lh[3]={lath[0]/latm,lath[1]/latm,0};double Fn=qbar*VEH_AREF*CN;
                    Faero_b[0]-=Fn*lh[0];Faero_b[1]-=Fn*lh[1];}
                double Faw[3]; q_rot(Faw,q,Faero_b);
                aeroLatW=rhat[0]*Faw[0]+rhat[1]*Faw[1];
            }
            double a_aeroLat=aeroLatW/mp.m;
            /* thrust x tilt lateral: thrust vector world radial component.
               thrust dir body = (sin g0, sin g1, ~cos); rotate to world, minus vertical part. */
            double g0=y[S_G0],g1=y[S_G1];
            double tdir_b[3]={sin(g0),sin(g1),0}; double tz2=1-tdir_b[0]*tdir_b[0]-tdir_b[1]*tdir_b[1];
            tdir_b[2]=tz2>0?sqrt(tz2):0;
            double tdir_w[3]; q_rot(tdir_w,q,tdir_b);
            double a_thrLat=thr*(rhat[0]*tdir_w[0]+rhat[1]*tdir_w[1])/mp.m;
            /* guidance command radial (post steer_sign + integral, as applied to gcmd) */
            double a_cmdLat=rhat[0]*s.gcmd.a_lat[0]+rhat[1]*s.gcmd.a_lat[1];
            double tilt=sim_body_tilt(&s.st)*RAD2DEG;
            /* recompute steer_sign, tilt cap, amax, a_vert_ref exactly as control.c does */
            double mach_v=v3_norm(v)/atm.a; double qbar_v=0.5*atm.rho*v3_norm(v)*v3_norm(v);
            double CNb[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
            double cna=tl(AM,CNb,9,mach_v);
            double aero_lat_auth=qbar_v*VEH_AREF*cna;
            double ramp0=s.st.engine_on?ignition_ramp(s.st.ign_timer):0.0;
            double thr_est0=s.st.n_eng*engine_thrust(y[S_THR],atm.p)*ramp0;
            double steer_sign=1.0;
            if(s.env.fins_deployed && qbar_v>200.0){ double den=thr_est0>aero_lat_auth?thr_est0:aero_lat_auth; if(den<1.0)den=1.0; steer_sign=(thr_est0-aero_lat_auth)/den; }
            /* a_vert_ref (PH_ENTRY_BURN raises it; landing burn uses G0+2) */
            double a_vert_ref=G0+2.0;
            double h_base=y[S_RZ]-mp.com;
            double tmax_hi=s.env.fins_deployed?15.0:12.0;
            double tilt_max=(4.0+(tmax_hi-4.0)*((h_base-20.0)/180.0))*DEG2RAD;
            if(tilt_max<4.0*DEG2RAD)tilt_max=4.0*DEG2RAD; if(tilt_max>tmax_hi*DEG2RAD)tilt_max=tmax_hi*DEG2RAD;
            if(s.env.fins_deployed){ double qcap=15.0*DEG2RAD; if(qbar_v>50000.0){qcap=(15.0-4.0*((qbar_v-50000.0)/30000.0))*DEG2RAD; if(qcap<9.0*DEG2RAD)qcap=9.0*DEG2RAD;} if(tilt_max>qcap)tilt_max=qcap; }
            double amax=a_vert_ref*tan(tilt_max);
            printf("%7.3f %8.1f %7.1f %.2f %.2f %6.2f %7.1f %6.2f %7.0f %5.2f %5.2f  %8.3f %9.3f %8.3f  %6.2f %6.2f  %5.2f %6.2f %6.2f  %d %d\n",
                t,h,y[S_VZ],y[S_THR],ramp,tilt,lat,vrad,qbar,CT,shield,
                a_thrLat,a_aeroLat,a_cmdLat, windR,windMag, steer_sign, amax, tilt_max*RAD2DEG, s.st.phase,
                (s.st.engine_on&&s.st.ign_timer>=0.0)?s.st.n_eng:0);
        }
        if(t>t1+2.0) { /* keep running to end for verdict but stop printing */ }
        if(s.st.t>300.0) break;
    }
    printf("# RESULT verdict=%d td_v=%.2f lat=%.2f tilt=%.2f fuel=%.0f\n",
        s.st.verdict,s.impact_v,s.impact_lat,sim_body_tilt(&s.st)*RAD2DEG,s.st.y[S_MLOX]+s.st.y[S_MRP1]);
    return 0;
}
