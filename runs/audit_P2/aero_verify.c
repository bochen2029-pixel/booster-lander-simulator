/* aero_verify.c  — Agent P2 aero-coefficient verification harness.
 * HARD RULE compliance: C only, no Python.
 * Reconstructs the sim's mass properties + CoP model from constants.h values,
 * and checks them against slender-body / base-first-cylinder literature.
 *
 * Sources encoded as reference values in comments at each check.
 * Build: cl /O2 /fp:precise aero_verify.c
 */
#include <stdio.h>
#include <math.h>

/* ---- KESTREL-9 constants copied verbatim from core/constants.h ---- */
#define VEH_LEN       47.7
#define VEH_STAGE_LEN 41.2
#define VEH_DIA       3.66
#define VEH_RADIUS    (VEH_DIA*0.5)
#define VEH_AREF      10.52
#define VEH_DRY       25600.0
#define VEH_DRY_COMZ  12.4
#define LOX_RHO       1220.0
#define RP1_RHO       833.0
#define TANK_AREA     9.9
#define LOX_BASE_Z    16.0
#define RP1_BASE_Z    1.6
#define MIX_RATIO     2.33
#define FIN_AREA      2.4
#define FIN_Z         45.0
#define FIN_CNA       3.0
#define BODY_CMQ_CDC  0.6
#define PI            3.141592653589793
#define DEG2RAD       0.017453292519943295
#define RAD2DEG       57.29577951308232

/* ---- sim's CoP models ---- */
/* NEW (D-005, in dynamics.c now): fraction of STAGE length from base */
static double xcp_frac_new(double M){
    return 0.29 + 0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));
}
/* OLD (App-A.6 spec table line 1078): fraction of length from base */
static double xcp_frac_old(double M){
    return 0.62 + 0.04*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));
}

/* mass_props: reproduce dynamics.c CoM (z measured from base, +Z up the body) */
static double com_z(double m_lox, double m_rp1){
    double m_dry=VEH_DRY, z_dry=VEH_DRY_COMZ;
    double h_l=m_lox/(LOX_RHO*TANK_AREA), z_l=LOX_BASE_Z+0.5*h_l;
    double h_r=m_rp1/(RP1_RHO*TANK_AREA), z_r=RP1_BASE_Z+0.5*h_r;
    double M=m_dry+m_lox+m_rp1;
    return (m_dry*z_dry+m_lox*z_l+m_rp1*z_r)/M;
}

int main(void){
    printf("=================================================================\n");
    printf(" P2 AERO VERIFICATION  (KESTREL-9 / Falcon-9-class, base-first)\n");
    printf("=================================================================\n\n");

    /* ---- 1. Geometry sanity ---- */
    double fineness = VEH_LEN/VEH_DIA;
    double stage_fineness = VEH_STAGE_LEN/VEH_DIA;
    printf("[GEOM] total L=%.1f m, stage L=%.1f m, D=%.2f m\n", VEH_LEN,VEH_STAGE_LEN,VEH_DIA);
    printf("       fineness L/D = %.2f (total), %.2f (stage)\n", fineness, stage_fineness);
    printf("       Aref (pi D^2/4) = %.3f m^2  (constants.h says %.2f)\n",
           PI*VEH_DIA*VEH_DIA/4.0, VEH_AREF);
    printf("\n");

    /* ---- 2. CoM location at representative descent masses ---- */
    /* AERO_OFFSET prop0 = 10 t; ENTRY prop0 = 30 t; TERMINAL prop0 = 8 t.
     * Split by mixture ratio 2.33 (LOX:RP1). */
    struct { const char* name; double prop_t; } scen[] = {
        {"dry (empty)", 0.0}, {"TERMINAL 8t", 8.0}, {"AERO 10t", 10.0}, {"ENTRY 30t", 30.0},
    };
    printf("[CoM] measured from BASE (engine end). Body is base-first: base LEADS.\n");
    printf("      %-14s  %8s  %8s  %8s\n","scenario","com[m]","com/L","com/Lstg");
    double com_terminal=0, com_aero=0, com_entry=0, com_dry=0;
    for(int i=0;i<4;i++){
        double prop=scen[i].prop_t*1000.0;
        double m_rp1=prop/(1.0+MIX_RATIO), m_lox=prop-m_rp1;
        double c=com_z(m_lox,m_rp1);
        printf("      %-14s  %8.2f  %8.3f  %8.3f\n", scen[i].name, c, c/VEH_LEN, c/VEH_STAGE_LEN);
        if(i==0)com_dry=c; if(i==1)com_terminal=c; if(i==2)com_aero=c; if(i==3)com_entry=c;
    }
    printf("\n");

    /* ---- 3. CoP vs CoM static margin, NEW vs OLD, over Mach ---- *
     * dynamics.c computes xcp = xcp_frac(M)*VEH_STAGE_LEN, and the aero torque
     * arm is (xcp - com). Static stability of a base-first (base-leading) body:
     * the LEADING end is the base (z=0). A stable pendulum needs CoP DOWNSTREAM
     * (aft, i.e. toward the nose / larger z... NO -- careful):
     *
     * For a body falling base-first, the relative wind comes from BELOW (base end).
     * The aero restoring moment is stabilizing when CoP is on the DOWNWIND side of
     * CoM, i.e. FARTHER FROM the leading (base) end => LARGER z than com.
     * So: STATIC MARGIN (calibers, stabilizing +) = (xcp - com)/D.
     *   xcp > com  => CoP aft of (above) CoM => STABLE (weathercocks base-first).
     *   xcp < com  => CoP fwd of (below, toward base) CoM => UNSTABLE.  */
    printf("[COP] Static margin SM = (xcp - com)/D  [calibers]; +stable, -unstable.\n");
    printf("      Using AERO-descent mass (com=%.2f m). xcp = frac * Lstg(%.1f).\n", com_aero, VEH_STAGE_LEN);
    printf("      %-6s | NEW xcp/L  xcp[m]  SM[cal] | OLD xcp/L  xcp[m]  SM[cal]\n","Mach");
    double Ms[]={0.3,0.6,0.8,0.9,1.05,1.2,1.5,2.0,3.0,5.0};
    for(int i=0;i<10;i++){
        double M=Ms[i];
        double fn=xcp_frac_new(M), fo=xcp_frac_old(M);
        double xn=fn*VEH_STAGE_LEN, xo=fo*VEH_STAGE_LEN;
        double smn=(xn-com_aero)/VEH_DIA, smo=(xo-com_aero)/VEH_DIA;
        printf("      %-6.2f | %8.3f %7.2f %7.2f | %8.3f %7.2f %7.2f\n",
               M, fn/ (VEH_LEN/VEH_STAGE_LEN), xn, smn,  fo/(VEH_LEN/VEH_STAGE_LEN), xo, smo);
    }
    printf("      (NEW xcp/L and OLD xcp/L columns re-expressed as fraction of TOTAL L\n");
    printf("       for apples-to-apples, since sim multiplies frac by Lstg not L.)\n\n");

    /* Also show SM at the transonic bump for each mass, NEW model */
    printf("[COP-mass] NEW model SM[cal] at M=1.05 (transonic, worst) by mass:\n");
    double xcp105=xcp_frac_new(1.05)*VEH_STAGE_LEN;
    printf("      dry    com=%.2f  SM=%+.2f cal\n", com_dry,     (xcp105-com_dry)/VEH_DIA);
    printf("      8t     com=%.2f  SM=%+.2f cal\n", com_terminal,(xcp105-com_terminal)/VEH_DIA);
    printf("      10t    com=%.2f  SM=%+.2f cal\n", com_aero,    (xcp105-com_aero)/VEH_DIA);
    printf("      30t    com=%.2f  SM=%+.2f cal\n", com_entry,   (xcp105-com_entry)/VEH_DIA);
    printf("\n");

    /* ---- 4. Slender-body / Allen-Perkins CN_alpha for the body ---- *
     * Allen-Perkins (NACA 1048) / Jorgensen (NASA TR R-474):
     *   CN = (Sb/Sref) sin(2a)cos(a/2)   [potential/slender-body, base area Sb]
     *      + eta*(Splan/Sref)*Cdc*sin^2(a)  [viscous crossflow]
     * At small a, first term -> 2a * (Sb/Sref). With Sref = Sb (base area, = Aref),
     * potential CN_alpha = 2.0 /rad EXACTLY (this is the classic slender-body result).
     * The viscous term is O(a^2) so contributes ~0 slope at a=0 but adds strongly by
     * 5-15 deg. Cdc (crossflow drag coeff of a circular cyl) ~ 1.2 (subsonic) up to
     * ~1.4 transonic; eta (finite-length factor) ~ 0.6-0.7 for L/D~13.        */
    printf("[CN_a] Allen-Perkins/Jorgensen body normal force (Sref=Aref=base area):\n");
    double Sb=VEH_AREF; /* base area = ref area */
    double Splan=VEH_LEN*VEH_DIA; /* planform (side) area of the cylinder */
    double eta=0.65, Cdc=1.2;
    printf("      potential-only CN_alpha(a->0) = 2.0 /rad  (exact slender-body)\n");
    printf("      Splan/Sref = %.2f  (long body => big crossflow lever at AoA)\n", Splan/Sb);
    printf("      effective CN(a) and secant slope CN/a (incl. viscous, eta=%.2f Cdc=%.1f):\n", eta, Cdc);
    printf("      %-8s %10s %12s\n","alpha[d]","CN","CN/alpha[/rad]");
    for(int d=1; d<=15; d+=2){
        double a=d*DEG2RAD;
        double CN = (Sb/Sb)*sin(2*a)*cos(a/2.0) + eta*(Splan/Sb)*Cdc*sin(a)*sin(a);
        printf("      %-8d %10.4f %12.3f\n", d, CN, CN/a);
    }
    printf("      -> secant slope CN/alpha RISES with alpha (viscous crossflow);\n");
    printf("         sim table CNa=2.0..2.5 is a LINEARIZED single-slope fit.\n\n");

    /* ---- 5. Grid-fin CN_alpha reference-area conversion ---- *
     * ICAS-2004 (Hiroshima&Tatsumi) Fig.7: CN_alpha vs Mach, y-axis 0..0.20 PER DEG,
     * referenced to BODY cross-section area (324.3 mm^2), fin lifting-surface area
     * 778.8 mm^2 (square). So fin-area-referenced slope = body-referenced * (Sbody/Sfin).
     * Read subsonic peak ~0.10-0.12/deg (body ref). Convert: */
    double icas_body_area=324.3, icas_fin_area=778.8;
    printf("[FIN]  ICAS-2004 grid-fin CN_alpha (body-area ref) -> fin-area ref:\n");
    double icas_bodyref_perdeg[]={0.11, 0.075, 0.06, 0.12}; /* approx read subsonic .5, .8dip, 1.2dip, 2.0 */
    const char* icas_M[]={"0.5","0.8","1.2","2.0"};
    for(int i=0;i<4;i++){
        double perdeg_body=icas_bodyref_perdeg[i];
        double perrad_body=perdeg_body*RAD2DEG;
        double perrad_fin =perrad_body*(icas_body_area/icas_fin_area);
        printf("      M=%-4s body-ref=%.3f/deg = %.2f/rad -> fin-ref=%.2f/rad\n",
               icas_M[i], perdeg_body, perrad_body, perrad_fin);
    }
    printf("      (ICAS lift-surface/body-area ratio = %.2f; F9-like fin ~2.4 m^2)\n",
           icas_fin_area/icas_body_area);
    printf("      sim FIN_CNA=%.1f/rad referenced to per-fin area %.1f m^2.\n", FIN_CNA, FIN_AREA);
    printf("\n");

    /* ---- 6. Base drag / axial force decomposition ---- *
     * Base-pressure coefficient of a flat-based bluff cylinder, subsonic:
     *   Cp_base ~ -0.6 to -0.78 (measured, base-flow eddy shedding).
     * Base drag contribution to CA (ref base area): CA_base = -Cp_base ~ 0.6-0.78.
     * Forebody friction+pressure on the (now-downstream) nose+skin adds ~0.1-0.2.
     * So a base-FIRST cylinder CA(subsonic) ~ 0.7-1.0. Transonic base drag PEAKS
     * (wave drag on the leading flat face) -> CA can reach ~1.2-1.5.            */
    printf("[CA]   Base-first axial force build-up (Sref=base area):\n");
    double Cp_base_lo=-0.6, Cp_base_hi=-0.78;
    printf("      subsonic base drag CA_base = -Cp_base = %.2f..%.2f\n", -Cp_base_hi, -Cp_base_lo);
    printf("      + forebody/skin ~0.10..0.20 => CA_sub ~ %.2f..%.2f\n",
           -Cp_base_hi+0.10, -Cp_base_lo+0.20);
    printf("      sim CA table: 0.85 sub -> 1.40 transonic(M1.1) -> 0.95 by M3.\n");
    printf("\n");

    printf("=================================================================\n");
    printf(" DONE. See stdout table for numeric adjudication.\n");
    printf("=================================================================\n");
    return 0;
}
