/* atmosphere.h — US Standard Atmosphere 1976, 0..86 km. See CLAUDE_v1.md §4.2, App-A.1. */
#ifndef BL_ATMOSPHERE_H
#define BL_ATMOSPHERE_H

#include "vmath.h"

typedef struct {
    double T;    /* K */
    double p;    /* Pa */
    double rho;  /* kg/m^3 */
    double a;    /* m/s speed of sound */
} AtmoOut;

/* Geometric altitude h (m). Pure function; guidance uses the same one. */
BL_HD void atmo_eval(double h, AtmoOut* o);

#endif
