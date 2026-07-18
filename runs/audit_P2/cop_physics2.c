/* cop_physics2.c — refined bare-body CoP estimate for the base-first booster,
 * WITHOUT the singular base-potential artifact of v1.
 *
 * Physical model (base-first, blunt flat base leads):
 *  (A) The constant-diameter BARREL carries viscous crossflow loading uniformly:
 *      dN/dz = q * eta*Cdc*(2R) * sin^2(a).  Its centroid is the barrel mid-length.
 *  (B) The BLUNT FLAT LEADING BASE behaves like a bluff disc: it carries a
 *      concentrated normal load from the asymmetric base/afterbody pressure at AoA.
 *      Model it as a lumped normal force at z~=0 (the leading face) with an
 *      effective "base normal" coefficient CN_base,alpha that we sweep.
 *  (C) The DOWNSTREAM top (former nose taper + stowed-fin wake) is in separated
 *      flow base-first => contributes little clean normal force; we neglect it
 *      (conservative: neglecting a high-z contribution makes CoP LOWER, i.e. errs
 *      toward MORE instability, so if we still get near-neutral it's robust).
 *
 * Then x_cp = (N_barrel*z_barrel + N_base*z_base) / (N_barrel + N_base).
 * We report x_cp/L and the resulting static margin vs empty CoM=12.3 m (0.30L wait
 * -> 0.26 L of total; 0.30 of stage). Compare to sim NEW 0.29 Lstg.
 */
#include <stdio.h>
#include <math.h>
#define L      41.2
#define D      3.66
#define R      (D*0.5)
#define DEG2RAD 0.017453292519943295

int main(void){
    double eta=0.65, Cdc=1.2;
    double a=6.0*DEG2RAD;                 /* representative descent AoA */
    double z_barrel = L*0.5;              /* barrel crossflow centroid ~ mid-body */
    double z_base   = 0.6;               /* leading flat face, just above z=0 */
    double com_dry  = 12.40;             /* from mass_props, empty */

    /* barrel normal force per unit q (integrated over full L) */
    double N_barrel = eta*Cdc*(2.0*R)*L * sin(a)*sin(a);

    printf("Refined base-first CoP (barrel crossflow + lumped blunt-base normal).\n");
    printf("alpha=%.0f deg, eta=%.2f, Cdc=%.1f, empty CoM=%.2f m (%.3f L).\n\n",
           a*57.2958, eta, Cdc, com_dry, com_dry/L);
    printf("N_barrel(per q) = %.3f  at z=%.1f m (0.50 L)\n\n", N_barrel, z_barrel);

    /* Sweep the base normal contribution as a fraction f of the barrel's, since
     * the blunt flat base is a strong bluff feature. Report resulting CoP + SM. */
    printf("%-8s %10s %10s %10s %10s\n","N_base/Nb","x_cp[m]","x_cp/L","SM[cal]","verdict");
    double fs[]={0.0,0.25,0.5,0.75,1.0,1.5,2.0};
    for(int i=0;i<7;i++){
        double f=fs[i];
        double N_base=f*N_barrel;
        double xcp=(N_barrel*z_barrel + N_base*z_base)/(N_barrel+N_base);
        double sm=(xcp-com_dry)/D;
        printf("%-8.2f %10.2f %10.3f %+10.2f %10s\n",
               f, xcp, xcp/L, sm, sm<0?"UNSTABLE":"stable");
    }
    printf("\nsim NEW model: xcp=0.29..0.32 Lstg = %.2f..%.2f m from base = %.3f..%.3f L\n",
           0.29*L, 0.32*L, 0.29*L/47.7*47.7/L*0.29* (L/L) /*placeholder*/, 0.32);
    printf("  -> sim NEW xcp in TOTAL-L terms: %.3f..%.3f L (0.29..0.32 of stage 41.2m).\n",
           0.29*L/47.7, 0.32*L/47.7);
    printf("  -> sim NEW xcp in metres: %.2f..%.2f m from base.\n", 0.29*L, 0.32*L);
    printf("\nKEY: for the bare body to be NEUTRAL/UNSTABLE (SM<=0) with CoM at %.1f m,\n", com_dry);
    printf("     the blunt LEADING BASE must contribute a normal force comparable to\n");
    printf("     or larger than the barrel crossflow (N_base/Nb >~ 0.9). For a FLAT\n");
    printf("     blunt base that is physically plausible (bluff-body base loads are\n");
    printf("     large). If the base contributes LESS, CoP rises toward mid-body and\n");
    printf("     the bare body becomes mildly STABLE.\n");
    return 0;
}
