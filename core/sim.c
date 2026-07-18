/* sim.c — the fixed-step driver. */
#include "sim.h"
#include "integrator.h"
#include "contact.h"
#include "control.h"
#include "guidance_hoverslam.h"
#include "atmosphere.h"
#include "constants.h"
#include "rng.h"
#include <math.h>
#include <string.h>

double sim_body_tilt(const State* st){
    double zb[3]={0,0,1}, zw[3]; q_rot(zw,&st->y[S_QX],zb);
    double c=zw[2]; if(c>1)c=1; if(c<-1)c=-1; return acos(c);
}

/* Mean wind profile (world horizontal), altitude-scaled. */
static void wind_sample(Sim* s, double h, double out[3]){
    out[0]=out[1]=out[2]=0.0;
    double u=s->se.u_ref;
    if(u<=0.0) return;
    double hh = h<10.0?10.0:h;
    double scale = pow(hh/10.0, 0.14);
    if(h>1000.0){ double t=(h-1000.0)/10000.0; if(t>1)t=1; scale *= (1.0+1.2*t); }
    if(h>25000.0) scale *= 0.4;
    double sp=u*scale;
    out[0]=sp*cos(s->se.wind_az); out[1]=sp*sin(s->se.wind_az);
    /* Dryden turbulence (module) — low-altitude first-order approx on horizontal */
    if(s->modules & MOD_TURB){
        double W20 = s->se.w20_kt*0.514444;   /* kt->m/s */
        double hb = h<3.0?3.0:h;
        double hft = hb/0.3048;
        double Lu = hft/pow(0.177+0.000823*hft,1.2)*0.3048;
        double sw = 0.1*W20;
        double su = sw/pow(0.177+0.000823*hft,0.4);
        double V = 60.0;  /* nominal airspeed scale */
        double beta = V*DT/Lu; if(beta>1)beta=1;
        for(int k=0;k<2;k++){
            double eta = rng_normal(s->seed, RNG_WIND, (uint32_t)s->st.step, (uint32_t)k, 0);
            s->st.wind_filt[k] = (1.0-beta)*s->st.wind_filt[k] + su*sqrt(2.0*beta)*eta;
            out[k]+=s->st.wind_filt[k];
        }
    }
}

void sim_init(Sim* s, int scenario, uint32_t seed, uint32_t run_idx, int modules, int guidance_mode){
    memset(s,0,sizeof(*s));
    s->seed=seed; s->modules=modules; s->scenario=scenario; s->guidance_mode=guidance_mode;
    scenario_init(&s->st, scenario, seed, run_idx, &s->se);
    s->env.module_mask=modules; s->env.fins_deployed=s->st.fins_deployed;
    s->env.wind_world[0]=s->env.wind_world[1]=s->env.wind_world[2]=0.0;
    s->impact_v=s->impact_tilt=s->impact_lat=0.0;
    s->max_qbar=0; s->peak_qdot=0; s->done=0; s->touched=0;
    s->gcmd.mode=guidance_mode; s->gcmd.n_eng=1;
}

static void set_verdict(Sim* s){
    State* st=&s->st;
    double lat = sqrt(st->y[S_RX]*st->y[S_RX]+st->y[S_RY]*st->y[S_RY]);
    double tilt = sim_body_tilt(st);
    double maxcrush=0; for(int i=0;i<4;i++) if(st->crush[i]>maxcrush)maxcrush=st->crush[i];
    double crush_frac=maxcrush/LEG_CRUSH_S;
    int on_pad = (lat<=PAD_RADIUS);
    double iv=s->impact_v;
    int grade;
    if(iv>TD_V_HARD || !on_pad) grade=V_CRASHED;
    else if(tilt>30.0*DEG2RAD) grade=V_TIPPED;
    else if(iv<=TD_V_PERFECT && lat<=TD_LAT_PERFECT && tilt<=TD_TILT_PERFECT && crush_frac<0.10) grade=V_PERFECT;
    else if(iv<=TD_V_GOOD && lat<=TD_LAT_GOOD && tilt<=TD_TILT_GOOD) grade=V_GOOD;
    else if(iv<=TD_V_HARD) grade=V_HARD;
    else grade=V_CRASHED;
    st->verdict=grade;
    if(grade==V_CRASHED) st->phase=PH_CRASHED;
    else if(grade==V_TIPPED) st->phase=PH_TIPPED;
    else st->phase=PH_LANDED;
}

/* ENTRY-BURN SUPERVISOR (E3, DECISIONS D-007). Sits ABOVE hoverslam. A high-energy entry (62 km,
 * ~1.5 km/s) would blow through the structural qbar line (measured: STRUCT at ~16 km, 105 kPa) if
 * left to a futile 1-engine hoverslam ignition at altitude. This runs a predictive-peak-qbar
 * 3-engine RETROGRADE burn (Agent A §9): ignite while the ballistic-to-touchdown peak qbar would
 * breach the line, cut once the remaining descent is predicted safe. Decelerating up high means the
 * downstream landing burn happens at low qbar where thrust dominates aero (the burn-phase-aero fix).
 * Cross-range divert is deferred to the aero-descent (Agent A §10). Returns 1 if it commanded
 * guidance this tick (skip hoverslam), 0 to let hoverslam run. Inert for low-energy scenarios
 * (TERMINAL/AERO_OFFSET predict < IGNITE, so gcmd/state untouched -> determinism preserved). */
#define ENTRY_QBAR_IGNITE  72000.0   /* predicted peak qbar to arm the entry burn [Pa] */
#define ENTRY_QBAR_CUT     68000.0   /* predicted remaining peak qbar to cut the burn [Pa] */
#define ENTRY_FUEL_FLOOR   7000.0    /* kg: reserve at least this for the landing burn */
static int entry_supervisor(Sim* s){
    State* st=&s->st;
    if(st->phase!=PH_COAST && st->phase!=PH_ENTRY_BURN) return 0;   /* only pre-landing regimes */
    MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
    double vx=st->y[S_VX],vy=st->y[S_VY],vz=st->y[S_VZ];
    double speed=sqrt(vx*vx+vy*vy+vz*vz);
    double fuel=st->y[S_MLOX]+st->y[S_MRP1];
    double qpk=entry_predict_peak_qbar(st->y[S_RZ], speed, mp.m);

    if(st->phase==PH_ENTRY_BURN){
        if(qpk<=ENTRY_QBAR_CUT || fuel<=ENTRY_FUEL_FLOOR){
            /* CUT: shut down, hand off to the unpowered aero-descent (hoverslam takes it) */
            st->engine_on=0; st->ign_timer=-1.0; st->n_eng=1;
            s->gcmd.engine_cmd=0; s->gcmd.throttle=0.0; s->gcmd.n_eng=1;
            s->gcmd.a_lat[0]=s->gcmd.a_lat[1]=0.0;
            st->phase=PH_AERO;
            return 0;
        }
        /* continue: 3-engine, full throttle, RETROGRADE (a_lat=0 -> control holds ~vertical, which
         * is ~anti-velocity for a steep entry); pure deceleration, no cross-range yet. */
        s->gcmd.mode=GM_HOVERSLAM; s->gcmd.engine_cmd=1; s->gcmd.throttle=1.0; s->gcmd.n_eng=3;
        s->gcmd.a_lat[0]=s->gcmd.a_lat[1]=0.0; s->gcmd.deploy_cmd=0; s->gcmd.solver_flags=0;
        s->gcmd.t_go=(vz<-0.1)?(st->y[S_RZ]/(-vz)):5.0;
        return 1;
    }
    /* PH_COAST: arm the entry burn iff the ballistic descent would over-pressure and we can spare
     * the fuel (below the floor a burn is worse than the aero-descent's own qbar management). */
    if(qpk>=ENTRY_QBAR_IGNITE && fuel>ENTRY_FUEL_FLOOR){
        st->phase=PH_ENTRY_BURN;
        s->gcmd.mode=GM_HOVERSLAM; s->gcmd.engine_cmd=1; s->gcmd.throttle=1.0; s->gcmd.n_eng=3;
        s->gcmd.a_lat[0]=s->gcmd.a_lat[1]=0.0; s->gcmd.deploy_cmd=0; s->gcmd.solver_flags=0;
        return 1;
    }
    return 0;
}

int sim_step(Sim* s){
    State* st=&s->st;
    if(s->done) return 0;

    /* advance ignition timer */
    if(st->engine_on && st->ign_timer>=0.0) st->ign_timer += DT;

    /* guidance at 50 Hz */
    if(st->step % (long)(GUIDANCE_DT/DT) == 0 && s->guidance_mode==GM_HOVERSLAM){
        int entry_handled = entry_supervisor(s);   /* E3: 3-engine entry burn above hoverslam (D-007) */
        if(!entry_handled) hoverslam_step(st,&s->gcmd);
        /* engine ignition latch */
        if(s->gcmd.engine_cmd && !st->engine_on && st->relights_left>0){
            st->engine_on=1; st->ign_timer=0.0; st->n_eng=s->gcmd.n_eng; st->relights_left--;
            if(st->phase==PH_ENTRY_BURN){
                /* entry burn = retrograde deceleration, NOT the suicide-burn profile -> no ada freeze */
            } else {
                if(st->phase==PH_COAST||st->phase==PH_AERO) st->phase=PH_LANDING_BURN;
                /* freeze the design deceleration from the ACTUAL ignition mass/thrust */
                AtmoOut atm; atmo_eval(st->y[S_RZ],&atm);
                MassProps mp; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp);
                double Tf = st->n_eng*engine_thrust(1.0,atm.p);
                double amax = Tf/mp.m - G0; if(amax<1.0)amax=1.0;
                st->ada = 0.58*amax;
            }
        }
        st->deploy_cmd = s->gcmd.deploy_cmd;
    }

    /* control at 500 Hz */
    control_step(st,&s->gcmd,&s->env,&s->act);

    /* environment */
    wind_sample(s, st->y[S_RZ], s->env.wind_world);

    /* deploy ramp */
    if(st->deploy_cmd){ st->deploy_frac += DT/LEG_DEPLOY_T; if(st->deploy_frac>1)st->deploy_frac=1; }

    /* N2 depletion */
    st->N2 -= s->act.rcs_dm*DT; if(st->N2<0)st->N2=0;

    /* integrate: contact-substep near ground, else flight RK4 */
    int in_contact = near_ground(st, 0.5);
    double lo_before = lowest_point_z(st);
    if(in_contact) contact_substep(st,&s->act,&s->env,s->se.deck_z,DT);
    else rk4_step(st,&s->act,&s->env,DT);

    /* diagnostics via a spare deriv eval */
    double dyt[NSTATE]; dynamics_deriv(st,&s->act,&s->env,dyt,&s->diag);
    if(s->diag.qbar>s->max_qbar) s->max_qbar=s->diag.qbar;
    if(s->diag.qdot_heat>s->peak_qdot) s->peak_qdot=s->diag.qdot_heat;

    /* touchdown capture (first contact) */
    double lo_after=lowest_point_z(st);
    if(!s->touched && lo_after<=s->se.deck_z+1e-3 && lo_before>s->se.deck_z){
        s->touched=1;
        s->impact_v=sqrt(st->y[S_VX]*st->y[S_VX]+st->y[S_VY]*st->y[S_VY]+st->y[S_VZ]*st->y[S_VZ]);
        s->impact_tilt=sim_body_tilt(st);
        s->impact_lat=sqrt(st->y[S_RX]*st->y[S_RX]+st->y[S_RY]*st->y[S_RY]);
        if(st->phase<PH_TOUCHDOWN) st->phase=PH_TOUCHDOWN;
    }

    st->t += DT; st->step++;

    /* ---- termination checks ---- */
    /* struct: qbar over limit sustained */
    if(s->diag.qbar>QBAR_MAX){ s->qbar_over_timer+=DT; if(s->qbar_over_timer>2.0){ st->fault=F_STRUCT; st->phase=PH_STRUCT_FAIL; s->done=1; return 0; } }
    else s->qbar_over_timer=0;
    /* thermal */
    if(s->diag.qdot_heat>HEAT_QDOT_MAX){ s->qdot_over_timer+=DT; if(s->qdot_over_timer>5.0){ st->fault=F_THERMAL; st->phase=PH_THERMAL_FAIL; s->done=1; return 0; } }
    else s->qdot_over_timer=0;
    /* loss of control */
    double wmag=sqrt(st->y[S_WX]*st->y[S_WX]+st->y[S_WY]*st->y[S_WY]+st->y[S_WZ]*st->y[S_WZ]);
    if(wmag>2.0){ s->loc_timer+=DT; if(s->loc_timer>3.0){ st->fault=F_LOC; st->phase=PH_LOC; s->done=1; return 0; } }
    else s->loc_timer=0;

    /* fuel depletion note (keep simulating ballistic) */
    if((st->y[S_MLOX]<=0.0||st->y[S_MRP1]<=0.0) && st->engine_on){ st->fault=F_FUEL; }

    /* settling / verdict */
    if(s->touched){
        if(st->phase<PH_SETTLING && st->phase!=PH_LANDING_BURN) st->phase=PH_SETTLING;
        st->phase=PH_SETTLING;
        double ke = st->y[S_VX]*st->y[S_VX]+st->y[S_VY]*st->y[S_VY]+st->y[S_VZ]*st->y[S_VZ]
                  + wmag*wmag*4.0;
        if(ke<0.02) s->settle_timer+=DT; else s->settle_timer=0;
        /* stop thrust after touchdown */
        s->gcmd.throttle=0.0; s->gcmd.engine_cmd=0; st->engine_on=0;
        if(s->settle_timer>1.5 || st->t>200.0){ set_verdict(s); s->done=1; return 0; }
        /* immediate crash if buried fast */
        if(s->impact_v>TD_V_HARD){ set_verdict(s); s->done=1; return 0; }
    }
    return 1;
}

long sim_run(Sim* s, RunResult* res, double t_max){
    long n=0;
    while(sim_step(s)){ n++; if(s->st.t>t_max){ s->st.fault=F_NONE; break; } }
    State* st=&s->st;
    if(res){
        res->verdict=st->verdict; res->fault=st->fault; res->phase=st->phase;
        res->td_v=s->impact_v; res->td_lat=s->impact_lat; res->td_tilt=s->impact_tilt;
        res->settled_tilt=sim_body_tilt(st);
        res->fuel_margin=st->y[S_MLOX]+st->y[S_MRP1];
        res->max_qbar=s->max_qbar; res->peak_qdot=s->peak_qdot; res->t_total=st->t;
        double mc=0; for(int i=0;i<4;i++) if(st->crush[i]>mc)mc=st->crush[i]; res->max_crush=mc;
        if(st->verdict==V_NONE){ res->verdict=V_CRASHED; res->phase=PH_CRASHED; }
    }
    return n;
}
