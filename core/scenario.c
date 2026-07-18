/* scenario.c — scenario table + seeded dispersion sampling. */
#include "scenario.h"
#include "dynamics.h"
#include "constants.h"
#include "rng.h"
#include <string.h>
#include <math.h>

typedef struct {
    const char* name;
    double h0, vz0, lateral, prop0, tilt0, u_ref, deck_z; int w20; int fins;
} ScenDef;

static const ScenDef DEFS[SCEN_COUNT] = {
 /* name           h0     vz0    lat    prop0   tilt0        uref deck w20 fins */
 {"terminal",     2000,  -180,    0,    8500,  3*DEG2RAD,    3.0, 0,  15, 0},
 {"aero_offset", 12000,  -330,  500,   10000,  5*DEG2RAD,    6.0, 0,  30, 1},
 {"entry",       62000, -1500, 3000,   30000,  6*DEG2RAD,    8.0, 0,  30, 1},
 {"asds_night",  12000,  -330,  800,   10000,  5*DEG2RAD,    8.0, 0,  30, 1},
 {"chaos",        9000,  -250,  600,    9000,  8*DEG2RAD,   12.0, 0,  45, 1},
};

const char* scenario_name(int s){ return (s>=0&&s<SCEN_COUNT)?DEFS[s].name:"?"; }
int scenario_from_name(const char* s){
    for(int i=0;i<SCEN_COUNT;i++) if(strcmp(s,DEFS[i].name)==0) return i;
    return -1;
}

static double disp(uint32_t seed, uint32_t run, uint32_t field, double sigma){
    if(run==0) return 0.0;
    return sigma*rng_normal(seed, RNG_DISPERSION, run, 0, field);
}

void scenario_init(State* st, int scen, uint32_t seed, uint32_t run_idx, ScenarioEnv* se){
    if(scen<0||scen>=SCEN_COUNT) scen=SCEN_TERMINAL;
    const ScenDef* d=&DEFS[scen];
    memset(st,0,sizeof(*st));
    q_identity(&st->y[S_QX]);

    /* propellant load (reserve) split by mixture ratio, with mass dispersion */
    double prop0 = d->prop0 * (1.0 + disp(seed,run_idx,10,0.02));
    double m_rp1 = prop0/(1.0+MIX_RATIO);
    double m_lox = prop0 - m_rp1;
    st->y[S_MLOX]=m_lox; st->y[S_MRP1]=m_rp1;

    MassProps mp; mass_props(m_lox,m_rp1,0,0,&mp);

    /* Lateral dispersion scaled to what the scenario's altitude can physically divert:
     * TERMINAL is a final-approach scenario (~30 m), big diverts belong to AERO_OFFSET. */
    /* Lateral dispersion scaled to each scenario's physical aero-divert ceiling (audit P4):
     * TERMINAL final-approach ~30 m; AERO_OFFSET ~150 m (mean 500) — the 800/250 was beyond the
     * ~860 m aero+burn ceiling without an entry burn; ENTRY-class keep wide. */
    double lat_sigma = (scen==SCEN_TERMINAL)?30.0:(scen==SCEN_AERO_OFFSET)?150.0:250.0;
    double h0 = d->h0 + disp(seed,run_idx,1, d->h0*0.06);
    double lat = d->lateral;
    double lx = lat + disp(seed,run_idx,2, lat_sigma);
    double ly = disp(seed,run_idx,3, lat_sigma);
    st->y[S_RX]=lx; st->y[S_RY]=ly; st->y[S_RZ]=h0 + mp.com;

    double vz0 = d->vz0 + disp(seed,run_idx,4, (scen==SCEN_TERMINAL?12.0:20.0));
    st->y[S_VX]=disp(seed,run_idx,5, 5.0);
    st->y[S_VY]=disp(seed,run_idx,6, 5.0);
    st->y[S_VZ]=vz0;

    /* attitude: small tilt from vertical, random azimuth */
    double tilt = fabs(d->tilt0 + disp(seed,run_idx,7, 2*DEG2RAD));
    double az = 6.2831853*rng_u01((uint32_t)(seed+run_idx*2654435761u));
    double zt[3]={sin(tilt)*cos(az), sin(tilt)*sin(az), cos(tilt)};
    double zb[3]={0,0,1}, qtilt[4]; q_from_two_vec(qtilt,zb,zt); q_copy(&st->y[S_QX],qtilt);

    st->y[S_WX]=disp(seed,run_idx,8, 0.02);
    st->y[S_WY]=disp(seed,run_idx,9, 0.02);
    st->y[S_WZ]=disp(seed,run_idx,11,0.02);

    st->y[S_THR]=0.0;
    st->engine_on=0; st->n_eng=1; st->ign_timer=-1.0; st->relights_left=2;
    st->deploy_frac=0.0; st->deploy_cmd=0;
    st->N2=RCS_N2; st->t=0.0; st->step=0; st->phase=PH_COAST;
    st->verdict=V_NONE; st->fault=F_NONE; st->fins_deployed=d->fins;

    if(se){ se->u_ref=d->u_ref; se->wind_az=az; se->deck_z=d->deck_z; se->w20_kt=d->w20; }
}
