/* cop_physics.c — where does the bare-body CoP of a base-first L/D~13 cylinder
 * REALLY sit, from Allen-Perkins crossflow theory? This is the ground truth to
 * judge both the sim NEW (0.29 Lstg from base) and OLD (0.62 L) models.
 *
 * Allen-Perkins (NACA Rep. 1048) local normal-force loading on a body at AoA:
 *   dN/dx = q * [ dA/dx * sin(2a)cos(a/2)   (potential, acts where AREA CHANGES)
 *              + eta*Cdc*(2r) * sin^2(a) ]  (viscous crossflow, acts along the
 *                                            constant-section BARREL, ~uniformly)
 * The CoP is the N-weighted centroid of dN/dx.
 *
 * Key geometric fact for a booster: the body is nearly a CONSTANT-diameter
 * cylinder (dA/dx = 0 along the barrel). ALL the potential lift is generated
 * where the cross-section changes: the nose taper AND the blunt base.
 * For a base-FIRST descent the flow sees the FLAT BASE as the "nose" (a very
 * blunt leading body) and the (former) nose taper is now the DOWNSTREAM boat-tail.
 *
 * We model the KESTREL-9 as: flat blunt base at z=0 (leading), constant barrel to
 * the interstage, mild taper near the top. We compute CoP from the crossflow
 * loading, measured from the BASE (z=0), to compare directly to the sim fraction.
 */
#define _USE_MATH_DEFINES
#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.141592653589793
#endif
#define L      41.2      /* stage length modelled (m) */
#define D      3.66
#define R      (D*0.5)
#define AREF   10.52
#define DEG2RAD 0.017453292519943295

int main(void){
    /* Discretize the body along z (0=base=leading, L=top=interstage/downstream). */
    const int N=4120; double dz=L/N;
    double a_list[]={2,4,6,8,10};  /* deg AoA */
    double eta=0.65, Cdc=1.2;

    printf("Bare-body CoP from Allen-Perkins crossflow loading (base-first).\n");
    printf("Body: mostly constant-diameter cylinder => potential lift only at\n");
    printf("area changes; viscous crossflow ~uniform along the barrel.\n\n");

    /* Radius profile r(z): flat base (blunt) at z<~0.5 m ramps quickly to full R,
     * constant barrel, then a mild boat-tail/taper in the top 3 m to model the
     * interstage necking (small). This is the DESCENT shape (legs stowed). */
    #define RPROF(z) ( (z)<0.4 ? R*sqrt(fmax(0.0,(z)/0.4)) /*blunt hemispherical-ish cap face*/ \
                     : (z)>(L-3.0) ? R*(1.0-0.10*(( (z)-(L-3.0))/3.0)) /*mild 10% taper up top*/ \
                     : R )

    for(int ai=0; ai<5; ai++){
        double a=a_list[ai]*DEG2RAD;
        double Ntot=0, Nmom=0;
        for(int i=0;i<N;i++){
            double z0=i*dz, z1=(i+1)*dz, zc=0.5*(z0+z1);
            double r0=RPROF(z0), r1=RPROF(z1);
            double A0=M_PI*r0*r0, A1=M_PI*r1*r1;
            double dA=A1-A0;
            double rc=RPROF(zc);
            /* potential loading (proportional to dA/dz) */
            double dN_pot = dA * sin(2*a)*cos(a/2.0);
            /* viscous crossflow loading (proportional to local width 2r) */
            double dN_vis = eta*Cdc*(2.0*rc)*dz * sin(a)*sin(a);
            double dN = dN_pot + dN_vis;
            Ntot += dN;
            Nmom += dN*zc;
        }
        double xcp = Nmom/Ntot;            /* measured from base */
        printf("alpha=%2.0f deg : CoP = %.2f m from base = %.3f L = %.3f Lstg\n",
               a_list[ai], xcp, xcp/47.7, xcp/L);
    }
    printf("\nInterpretation:\n");
    printf(" - Pure viscous crossflow on a uniform barrel puts CoP at the AREA\n");
    printf("   CENTROID of the wetted side => ~mid-body (~0.5 L) if truly uniform.\n");
    printf(" - The BLUNT LEADING BASE adds a big potential-lift patch RIGHT AT z=0\n");
    printf("   (flow stagnates/expands around the flat face) pulling CoP toward the\n");
    printf("   base (low z). The downstream taper adds a (smaller) patch up high.\n");
    printf(" - Net: bare CoP for a base-first blunt cylinder sits FORWARD of the\n");
    printf("   geometric mid-body, i.e. shifted toward the leading base => LOW z.\n");
    return 0;
}
