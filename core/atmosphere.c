/* atmosphere.c — US76 piecewise layers. Verified against table points in tests. */
#include "atmosphere.h"
#include "constants.h"
#include <math.h>

/* Geopotential base H' (km), base temp (K), lapse (K/km'), base pressure (Pa). App-A.1 */
static const double H_b[8]  = { 0.0, 11.0, 20.0, 32.0, 47.0, 51.0, 71.0, 84.852 };
static const double T_b[8]  = { 288.15,216.65,216.65,228.65,270.65,270.65,214.65,186.946 };
static const double L_b[7]  = { -6.5, 0.0, 1.0, 2.8, 0.0, -2.8, -2.0 };
static const double P_b[8]  = { 101325.0,22632.1,5474.89,868.019,110.906,66.9389,3.95642,0.373384 };

BL_HD void atmo_eval(double h, AtmoOut* o){
    if(h < 0.0) h = 0.0;
    /* geometric -> geopotential (km') */
    double Hp = (R_EARTH * h) / (R_EARTH + h) / 1000.0;
    if(Hp > 84.852) Hp = 84.852;
    int b = 0;
    for(int i=0;i<7;i++){ if(Hp >= H_b[i]) b = i; }
    double Tb = T_b[b], Lb = L_b[b], Pb = P_b[b], Hb = H_b[b];
    double dz = Hp - Hb;                 /* km' */
    double T = Tb + Lb*dz;
    double p;
    /* g0/(R*L) exponent uses g0 in m/s^2, R in J/(kg K), L in K/m => convert L per meter */
    if(fabs(Lb) > 1e-9){
        double Lm = Lb / 1000.0;         /* K/m */
        p = Pb * pow(T / Tb, -G0/(R_AIR*Lm));
    } else {
        p = Pb * exp(-G0*(dz*1000.0)/(R_AIR*Tb));
    }
    if(T < 1.0) T = 1.0;
    double rho = p / (R_AIR * T);
    o->T = T; o->p = p; o->rho = rho; o->a = sqrt(GAMMA_AIR*R_AIR*T);
}
