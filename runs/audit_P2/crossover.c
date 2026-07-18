/* crossover.c — pin the exact CoP/CoM crossover that decides bare-body stability
 * regime-by-regime, and show WHY the transonic bump flips it. Also test whether
 * a small change to the xcp_frac baseline would make the body uniformly unstable
 * (which is what canon 5.4/6.3 actually wants).
 */
#include <stdio.h>
#include <math.h>
#define VEH_STAGE_LEN 41.2
#define VEH_LEN 47.7
#define VEH_DIA 3.66
#define MIX_RATIO 2.33
#define LOX_RHO 1220.0
#define RP1_RHO 833.0
#define TANK_AREA 9.9
#define LOX_BASE_Z 16.0
#define RP1_BASE_Z 1.6
#define VEH_DRY 25600.0
#define VEH_DRY_COMZ 12.4

static double com_z(double m_lox,double m_rp1){
    double h_l=m_lox/(LOX_RHO*TANK_AREA), z_l=LOX_BASE_Z+0.5*h_l;
    double h_r=m_rp1/(RP1_RHO*TANK_AREA), z_r=RP1_BASE_Z+0.5*h_r;
    double M=VEH_DRY+m_lox+m_rp1;
    return (VEH_DRY*VEH_DRY_COMZ+m_lox*z_l+m_rp1*z_r)/M;
}
static double xcp_frac(double M){return 0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));}

int main(void){
    /* AERO-descent mass 10 t prop */
    double prop=10000.0, m_rp1=prop/(1+MIX_RATIO), m_lox=prop-m_rp1;
    double com=com_z(m_lox,m_rp1);
    double com_fracL=com/VEH_LEN, com_fracLs=com/VEH_STAGE_LEN;
    printf("AERO-descent com=%.3f m = %.4f L = %.4f Lstg\n", com, com_fracL, com_fracLs);
    printf("xcp_frac baseline=0.290 (sub/hyper), peak=0.320 (transonic).\n");
    printf("xcp in METRES = frac*Lstg: base=%.3f m, peak=%.3f m.\n\n",
           0.290*VEH_STAGE_LEN, 0.320*VEH_STAGE_LEN);

    printf("For STABILITY (base-first): need xcp > com (CoP aft of CoM).\n");
    printf(" com = %.3f m. xcp base %.3f m %s com; xcp peak %.3f m %s com.\n",
           com, 0.290*VEH_STAGE_LEN, (0.290*VEH_STAGE_LEN>com)?">":"<",
           0.320*VEH_STAGE_LEN, (0.320*VEH_STAGE_LEN>com)?">":"<");
    printf(" => baseline (sub/supersonic): %s ; transonic peak: %s\n\n",
           (0.290*VEH_STAGE_LEN>com)?"STABLE":"UNSTABLE",
           (0.320*VEH_STAGE_LEN>com)?"STABLE":"UNSTABLE");

    /* what baseline would make it UNIFORMLY unstable (xcp<com at ALL M incl peak)? */
    double need_frac_peak = com/VEH_STAGE_LEN;  /* xcp_peak must be < com */
    double need_base = need_frac_peak - 0.03;   /* since peak=base+0.03 */
    printf("To be UNIFORMLY unstable (canon 5.4/6.3 intent), need even the transonic\n");
    printf("PEAK xcp < com: peak_frac < %.4f => baseline < %.4f (currently 0.290).\n",
           need_frac_peak, need_base);
    printf("=> lowering xcp_frac baseline from 0.290 to ~%.2f (and keeping the +0.03\n", need_base);
    printf("   bump) would make the bare body unstable at EVERY Mach incl transonic,\n");
    printf("   matching canon. OR reduce the bump amplitude 0.03->~0.01.\n\n");

    /* Alternatively: if the intent is 'marginally unstable', the current hybrid
     * (unstable sub/supersonic, mildly stable transonic) is arguably FINE because
     * the transonic phase is brief. Quantify the transonic-stable window. */
    printf("Transonic-STABLE Mach window (xcp>com): solve 0.29+0.03*exp(-((M-1.05)/0.3)^2) > %.4f\n", com_fracLs);
    for(double M=0.7; M<=1.5; M+=0.05){
        double f=xcp_frac(M); int st=(f*VEH_STAGE_LEN>com);
        if(st) printf("  M=%.2f frac=%.4f xcp=%.2fm STABLE\n", M, f, f*VEH_STAGE_LEN);
    }
    printf("(Outside this window the bare body is unstable/neutral, as canon wants.)\n");
    return 0;
}
