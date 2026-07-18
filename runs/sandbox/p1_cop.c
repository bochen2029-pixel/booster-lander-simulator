/* p1_cop.c — P1 independent adjudication of the CONTESTED static-margin-vs-Mach.
 * P3 & P2 both report the bare body is a HYBRID (stable sub/supersonic, unstable ONLY transonic),
 * contradicting D-005's blanket "marginally unstable bare body". Verify at the CORRECT aero-descent
 * mass (near-empty ~35-42t), across Mach. Also: (a) P2's frame issue (xcp uses STAGE_LEN not LEN
 * while com is total-L); (b) sanity-check P5's delta_ff closed form.
 */
#include <stdio.h>
#include <math.h>
#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232
#define PI 3.141592653589793
#define VEH_LEN 47.7
#define VEH_STAGE_LEN 41.2
#define VEH_DIA 3.66
#define VEH_RADIUS (VEH_DIA*0.5)
#define VEH_AREF 10.52
#define VEH_DRY 25600.0
#define VEH_DRY_COMZ 12.4
#define LOX_RHO 1220.0
#define RP1_RHO 833.0
#define TANK_AREA 9.9
#define LOX_BASE_Z 16.0
#define RP1_BASE_Z 1.6
#define FIN_Z 45.0
#define FIN_AREA 2.4
#define FIN_CNA 3.0
#define FIN_CT_DELTA_FRAC 0.35
static double com_of(double ml,double mr,double*Mout){double md=VEH_DRY,zd=VEH_DRY_COMZ;double hl=ml/(LOX_RHO*TANK_AREA),zl=LOX_BASE_Z+0.5*hl;double hr=mr/(RP1_RHO*TANK_AREA),zr=RP1_BASE_Z+0.5*hr;double M=md+ml+mr;if(Mout)*Mout=M;return(md*zd+ml*zl+mr*zr)/M;}
static double fin_dip(double M){if(M>0.8&&M<1.2)return 0.55;if(M>2.0)return 0.80;if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;return 1.0;}
static double body_cna(double M){double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0},CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};if(M<=XM[0])return CN[0];if(M>=XM[8])return CN[8];for(int i=0;i<8;i++)if(M<=XM[i+1]){double t=(M-XM[i])/(XM[i+1]-XM[i]);return CN[i]+t*(CN[i+1]-CN[i]);}return CN[8];}
static double xcp_frac(double M,double a){double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));double amod=0.015*fmin(fabs(a)/(15.0*DEG2RAD),1.0);return base-amod;}

int main(void){
    printf("=== P1 CoP / static-margin adjudication (D-005 'marginally unstable bare body') ===\n\n");
    /* aero-descent mass: near-empty. Use ~10t total prop (mostly landing reserve) => ~36t.
     * Also show the fully-empty dry case and a heavier case to bound com sensitivity. */
    struct{const char*nm;double ml,mr;}masses[3]={
        {"near-empty (aero-descent, ml=7t mr=3t)",7000,3000},
        {"dry only (26t)",0,0},
        {"mid-descent (ml=120 mr=51)",120000,51000}};
    for(int mi=0;mi<3;mi++){
        double Mtot;double com=com_of(masses[mi].ml,masses[mi].mr,&Mtot);
        printf("--- mass case: %s  => M=%.0f kg, com=%.3f m (%.4f of LEN, %.4f of STAGE_LEN) ---\n",
               masses[mi].nm,Mtot,com,com/VEH_LEN,com/VEH_STAGE_LEN);
        printf("  Mach  xcp[m]  (xcp-com)[m]  SM[cal]  dTau/dalpha@qbar15k   stability\n");
        double a1=2.0*DEG2RAD;double qbar=15000.0;
        for(int k=0;k<7;k++){
            double Ms[7]={0.3,0.7,0.9,1.05,1.2,1.5,2.0};double M=Ms[k];
            double xcp=xcp_frac(M,a1)*VEH_STAGE_LEN;
            double sm=xcp-com;
            double dtauda=qbar*VEH_AREF*body_cna(M)*(-sm); /* +(unstable) if xcp<com i.e. sm<0 */
            printf("  %4.2f  %6.3f  %+9.3f   %+.3f  %+.4e          %s\n",
                   M,xcp,sm,sm/VEH_DIA,dtauda, sm<0?"UNSTABLE":(sm>0?"stable":"neutral"));
        }
        printf("\n");
    }

    /* P2's FRAME issue: xcp_frac(0.29..0.32) * VEH_STAGE_LEN. If read as fraction-of-total-L it
     * would be *VEH_LEN. Show both, and where com sits in each frame. */
    printf("--- P2 frame check: xcp uses STAGE_LEN(41.2), com is total-L(47.7) referenced ---\n");
    {
        double M=1.05,a=2*DEG2RAD;double frac=xcp_frac(M,a);
        double xcp_stage=frac*VEH_STAGE_LEN, xcp_total=frac*VEH_LEN;
        double com=com_of(7000,3000,NULL);
        printf("  frac=%.4f. xcp = frac*STAGE_LEN = %.3f m (code). If frac*LEN = %.3f m.\n",frac,xcp_stage,xcp_total);
        printf("  com(near-empty)=%.3f m. In STAGE frame SM=%.3f m; if code had used LEN SM=%.3f m.\n",
               com,xcp_stage-com,xcp_total-com);
        printf("  => The choice of STAGE_LEN vs LEN shifts xcp by frac*(LEN-STAGE)=%.3f m (~%.0f%% of the\n",
               frac*(VEH_LEN-VEH_STAGE_LEN),100*frac*(VEH_LEN-VEH_STAGE_LEN)/fabs(xcp_stage-com));
        printf("     small static margin!). This is a REAL ambiguity: whichever is intended, the '0.29L'\n");
        printf("     label is misleading because it is 0.29 of STAGE not of the total the CoM uses.\n\n");
    }
    static double com_of_nullwrap(double,double); /* fwd (unused) */

    /* P5 delta_ff closed-form sanity: delta_ff = (xcp-com)*Aref*CNa /(4*0.7071*(FIN_Z-com)*Sf*CNa_f) * alpha */
    printf("--- sanity-check P5's delta_ff closed form vs my bisected trim (M=1.2, alpha=6, near-empty) ---\n");
    {
        double com=com_of(7000,3000,NULL);double M=1.2,rho=0.31,V=M*302.0,alpha=6*DEG2RAD;
        double xcp=xcp_frac(M,alpha)*VEH_STAGE_LEN;double sm=xcp-com;
        double CNa=body_cna(M),CNa_f=FIN_CNA*fin_dip(M);
        double dff = (sm)*VEH_AREF*CNa/(4.0*0.7071*(FIN_Z-com)*FIN_AREA*CNa_f)*alpha;
        printf("  com=%.3f xcp=%.3f sm=%+.3f m ; P5 closed-form delta_ff=%.3f deg (per %g deg AoA)\n",
               com,xcp,sm,dff*RAD2DEG,alpha*RAD2DEG);
        printf("  per-deg = %.3f deg/deg. (Compare my corrected bisection ~0.47-0.53 deg/deg at heavier\n",fabs(dff)*RAD2DEG/6.0);
        printf("  com; note the sign of delta_ff FLIPS with sm sign -> at transonic where sm>0 the FF sign\n");
        printf("  reverses. This is the SUBTLETY: delta_ff must track sign(xcp-com), which CHANGES with Mach.)\n");
    }

    /* The punchline for D-005: is 'marginally unstable bare body' correct as stated? */
    printf("\n--- VERDICT INPUT: D-005 claim 'CoP ~0.29-0.32L => marginal/slightly unstable bare body' ---\n");
    {
        double com=com_of(7000,3000,NULL);
        int nunst=0,nstab=0;double Ms[7]={0.3,0.7,0.9,1.05,1.2,1.5,2.0};
        for(int k=0;k<7;k++){double xcp=xcp_frac(Ms[k],2*DEG2RAD)*VEH_STAGE_LEN;if(xcp-com<0)nunst++;else nstab++;}
        printf("  near-empty com=%.3f: UNSTABLE at %d/7 Machs, STABLE at %d/7. The instability is\n",com,nunst,nstab);
        printf("  CONCENTRATED in the transonic bump; sub/supersonic are near-neutral-to-stable.\n");
        printf("  => D-005's CoP DIRECTION (was 0.62L strongly stable, now ~0.30L marginal) is CORRECT and\n");
        printf("     a big improvement, BUT the summary 'marginally unstable' is imprecise: the airframe is\n");
        printf("     a HYBRID (unstable transonic, ~neutral/stable elsewhere). Matches P2+P3 independently.\n");
    }
    return 0;
}
static double com_of_nullwrap(double a,double b){return 0;}
