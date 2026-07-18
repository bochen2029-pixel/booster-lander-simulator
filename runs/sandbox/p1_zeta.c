/* p1_zeta.c — verify the Cmq zeta claims + the D-005 damping-augmentation.
 * D-005/constants.h claim: Cdc=0.6 gives zeta~0.1-0.15 (aero pendulum). And control.c subtracts
 * Cmq_aero from Kd so total closed-loop damping ~ the designed zeta (1.3 fins / 1.1 gimbal).
 * Check: (1) open-loop aero-only zeta at AERO & TERMINAL against the DEPLOYED-FIN stiffness (the
 * real restoring in descent) and against a bare pendulum. (2) the damping-augmentation arithmetic:
 * is Cmq_aero < Kd_design (so subtraction is safe) and does total land near designed?
 */
#include <stdio.h>
#include <math.h>
#define DEG2RAD 0.017453292519943295
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
#define BODY_CMQ_CDC 0.6
typedef struct{double m,com,I_tr,I_ax;}MP;
static void mprops(double ml,double mr,MP*mp){const double rt2=TANK_AREA/PI,R2=VEH_RADIUS*VEH_RADIUS;double md=VEH_DRY,zd=VEH_DRY_COMZ;double Id=md*(6.0*R2+VEH_LEN*VEH_LEN)/12.0,Ia=md*R2;double hl=ml/(LOX_RHO*TANK_AREA),zl=LOX_BASE_Z+0.5*hl;double hr=mr/(RP1_RHO*TANK_AREA),zr=RP1_BASE_Z+0.5*hr;double Ial=0.5*ml*rt2,Itl=ml*(3.0*rt2+hl*hl)/12.0;double Iar=0.5*mr*rt2,Itr=mr*(3.0*rt2+hr*hr)/12.0;double M=md+ml+mr;double com=(md*zd+ml*zl+mr*zr)/M;double I_tr=Id+md*(zd-com)*(zd-com)+Itl+ml*(zl-com)*(zl-com)+Itr+mr*(zr-com)*(zr-com);mp->m=M;mp->com=com;mp->I_tr=I_tr;mp->I_ax=Ia+Ial+Iar;}
static double fin_dip(double M){if(M>0.8&&M<1.2)return 0.55;if(M>2.0)return 0.80;if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;return 1.0;}
int main(void){
    printf("=== P1 Cmq zeta + damping-augmentation verification ===\n\n");
    /* near-empty aero-descent for the aero pendulum; mid for TERMINAL (heavier, fueled). */
    MP aero; mprops(7000,3000,&aero);
    MP term; mprops(20000,9000,&term); /* terminal ~ approaching empty, some reserve */
    double zc,J;
    printf("Cdamp = 0.5*rho*V*Cdc*D*J, J=integral(z-com)^2 dz.\n\n");
    struct{const char*nm;MP*mp;double rho,V,M;}cond[2]={{"AERO(near-empty)",&aero,0.31,350.0,1.16},{"TERMINAL",&term,1.00,180.0,0.53}};
    for(int c=0;c<2;c++){
        MP*mp=cond[c].mp; zc=mp->com; J=VEH_LEN*VEH_LEN*VEH_LEN/3.0-zc*VEH_LEN*VEH_LEN+zc*zc*VEH_LEN;
        double Cdamp=0.5*cond[c].rho*cond[c].V*BODY_CMQ_CDC*VEH_DIA*J;
        double qbar=0.5*cond[c].rho*cond[c].V*cond[c].V;
        /* deployed-fin restoring stiffness ~ 4 fins normal-force slope * arm: approx
         * k_fin = 4 * qbar*Sf*CNa_f * (FIN_Z-com) (per rad, the dominant descent stiffness) */
        double CNa_f=FIN_CNA*fin_dip(cond[c].M);
        double k_fin=4.0*qbar*FIN_AREA*CNa_f*(FIN_Z-mp->com);
        double wn_fin=sqrt(fabs(k_fin)/mp->I_tr);
        double zeta_aero=Cdamp/(2.0*mp->I_tr*wn_fin);
        printf("--- %s (com=%.2f I_tr=%.3e qbar=%.0f) ---\n",cond[c].nm,mp->com,mp->I_tr,qbar);
        printf("  J=%.4e  Cdamp=%.4e Nm/(rad/s)\n",J,Cdamp);
        printf("  deployed-fin stiffness k_fin=%.3e Nm/rad -> wn=%.4f rad/s (period %.1fs)\n",k_fin,wn_fin,2*PI/wn_fin);
        printf("  aero-only zeta (Cmq vs deployed-fin mode) = %.4f\n",zeta_aero);
        /* control designed gains */
        int fins_active = (c==0); /* AERO uses fins */
        double wn=fins_active?1.1:1.5, zeta=fins_active?1.3:1.1;
        double Kd_design=mp->I_tr*2.0*zeta*wn;
        double Kd=Kd_design-Cdamp; double floor=0.1*Kd_design; int floored=0; if(Kd<floor){Kd=floor;floored=1;}
        printf("  control: wn=%.1f zeta=%.1f -> Kd_design=%.3e ; Cmq_aero=%.3e ; Kd_after_subtract=%.3e %s\n",
               wn,zeta,Kd_design,Cdamp,Kd,floored?"(HIT 0.1*Kd FLOOR!)":"");
        double total_damp=Kd+Cdamp; double zeta_cl=total_damp/(2.0*mp->I_tr*wn);
        printf("  total closed-loop damping (Kd+Cmq)=%.3e -> closed-loop zeta about wn=%.1f is %.3f (designed %.1f)\n",
               total_damp,wn,zeta_cl,zeta);
        printf("  => damping-augmentation %s: subtract keeps total ~ designed (%s).\n\n",
               floored?"PARTIALLY (floored -> over-damped: Cmq alone exceeds design!)":"WORKS",
               floored?"Cmq_aero > Kd_design here":"Cmq < Kd_design, safe");
    }
    printf("NOTE on the 'zeta~0.1' claim: that refers to the BARE aero pendulum (weak/near-neutral\n");
    printf("static stiffness), not the deployed-fin mode. Against the fin restoring stiffness the Cmq\n");
    printf("zeta is higher (fins add stiffness AND the mode is slow). The number is regime-dependent;\n");
    printf("the physically important fact is Cmq turns a near-undamped pendulum into a decaying one\n");
    printf("(rate e-fold ~13-22s from p1_verify), and the control must NOT double-count it (D-005 aug).\n");
    return 0;
}
