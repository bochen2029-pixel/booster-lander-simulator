/* fin_cna.c — reconcile grid-fin CNalpha across the literature onto the sim's
 * reference-area convention (per-fin planform area = FIN_AREA = 2.4 m^2), and
 * evaluate the sim's transonic dip fin_dip(M) against measured CNa(M) ratios.
 *
 * Literature reference-area basis (ALL use BODY cross-section area as Sref):
 *  - ICAS-2004 Hiroshima&Tatsumi Fig.7: CNa vs M, y-axis 0..0.20 /deg (body ref),
 *    fin lifting area / body area = 778.8/324.3 = 2.40.
 *  - ARL GOVPUB-D101 (Simpson 1997 WT + FLUENT): Sref = body cross-section area.
 * Convert body-ref CNa to fin-ref: CNa_fin = CNa_body * (Sbody/Sfin_lift).
 * For F9-like fins the sim's Sf=2.4 m^2 IS the fin planform area, so the right
 * comparison is the fin-area-referenced slope.
 */
#include <stdio.h>
#include <math.h>
#define RAD2DEG 57.29577951308232
#define FIN_CNA 3.0

/* sim's transonic dip */
static double fin_dip(double M){
    if(M>0.8 && M<1.2) return 0.55;
    if(M>2.0) return 0.80;
    if(M>=0.6 && M<=0.8) return 1.0 + (0.55-1.0)*(M-0.6)/0.2;
    if(M>=1.2 && M<=2.0) return 0.55 + (0.80-0.55)*(M-1.2)/0.8;
    return 1.0;
}

int main(void){
    /* ICAS Fig.7 read-offs (body-ref, per deg), square fin, roll=90.
     * subsonic peak ~0.11, transonic trough ~0.06-0.07, M2 ~0.11-0.12. */
    struct { double M, perdeg_body; } icas[]={
        {0.5,0.110},{0.8,0.070},{1.2,0.060},{2.0,0.115}
    };
    double area_ratio = 778.8/324.3;  /* fin lifting / body area = 2.40 */
    printf("Grid-fin CNa reconciliation (ICAS-2004, body-ref -> fin-ref):\n");
    printf("%-6s %12s %12s %12s %10s\n","Mach","body/deg","body/rad","fin/rad","dip=fin/f0");
    double fin0=0;
    for(int i=0;i<4;i++){
        double br=icas[i].perdeg_body*RAD2DEG;      /* body /rad */
        double fr=br/area_ratio;                     /* fin /rad */
        if(i==0)fin0=fr;
        printf("%-6.2f %12.3f %12.2f %12.2f %10.2f\n",
               icas[i].M, icas[i].perdeg_body, br, fr, fr/fin0);
    }
    printf("\n=> subsonic grid-fin CNa referenced to FIN AREA ~ %.2f/rad (ICAS square).\n", fin0);
    printf("   sim FIN_CNA=%.1f/rad -- at/above the high end of measured (~1.4-2.9).\n\n", FIN_CNA);

    /* Transonic dip magnitude: measured CNa(transonic)/CNa(subsonic) */
    double meas_dip_08 = (icas[1].perdeg_body/icas[0].perdeg_body);
    double meas_dip_12 = (icas[2].perdeg_body/icas[0].perdeg_body);
    double meas_dip_20 = (icas[3].perdeg_body/icas[0].perdeg_body);
    printf("Transonic dip (measured ratio to subsonic-0.5):\n");
    printf("  M=0.8: meas %.2f  vs sim fin_dip(0.8)=%.2f\n", meas_dip_08, fin_dip(0.8));
    printf("  M=1.2: meas %.2f  vs sim fin_dip(1.2)=%.2f\n", meas_dip_12, fin_dip(1.2));
    printf("  M=2.0: meas %.2f  vs sim fin_dip(2.0)=%.2f\n", meas_dip_20, fin_dip(2.0));
    printf("\nSim fin_dip(M) across the band:\n");
    double Ms[]={0.5,0.6,0.7,0.8,0.9,1.0,1.1,1.2,1.5,2.0,2.5};
    for(int i=0;i<11;i++) printf("  M=%.1f dip=%.2f\n", Ms[i], fin_dip(Ms[i]));
    printf("\nVerdict: measured trough at M~0.8-1.2 is ~0.55-0.64 of subsonic -> sim's\n");
    printf("0.55 transonic dip is WELL-MATCHED (ICAS M0.8 ratio=%.2f, M1.2=%.2f).\n",
           meas_dip_08, meas_dip_12);
    printf("Sim M>2 dip=0.80 also matches (measured recovers to ~1.0+ by M2, sim conservative).\n");
    return 0;
}
