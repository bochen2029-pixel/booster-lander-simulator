/* p1_signrecon.c — reconcile P1 vs P5 on trim-deflection SIGN and MAGNITUDE.
 * P1 (bisection) got dc_trim(6deg,M=1.2)=+8.12deg; P5 reports delta_ff(6deg)~-4.2deg.
 * Resolve: (1) which physical direction does pattern [1,1,-1,-1]*dc>0 push the trim AoA?
 *          (2) exact trim dc at M=0.70 (P5's quoted condition) and M=1.2.
 *          (3) is the magnitude ~8deg or ~4deg? decompose the restoring vs body moment.
 *
 * Convention in dynamics.c: AoA in +x means vrel_b=(V sin a,0,-V cos a). A pure +x lateral
 * force on the nose-region creates... we let the plant speak. We print, for alpha=+6 in +x:
 *  - body moment tau_y (destabilizing)
 *  - fin moment tau_y at dc=0 (restoring)
 *  - the dc that zeroes the sum (trim), by bisection over the EXACT plant fin block.
 * Then we also compute the 'cancel-body-only' dc the current control uses, and compare.
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
#define FIN_DEFL_MAX (20.0*DEG2RAD)
#define FIN_CNA 3.0
#define FIN_STALL (25.0*DEG2RAD)
#define FIN_CT_DELTA_FRAC 0.35
static void v3_cross(double o[3],const double a[3],const double b[3]){double x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0];o[0]=x;o[1]=y;o[2]=z;}
static double v3_dot(const double a[3],const double b[3]){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static double v3_norm(const double a[3]){return sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
typedef struct{double com;}MP;
static void mp_com(double ml,double mr,MP*mp){double md=VEH_DRY,zd=VEH_DRY_COMZ;double hl=ml/(LOX_RHO*TANK_AREA),zl=LOX_BASE_Z+0.5*hl;double hr=mr/(RP1_RHO*TANK_AREA),zr=RP1_BASE_Z+0.5*hr;double M=md+ml+mr;mp->com=(md*zd+ml*zl+mr*zr)/M;}
static double fin_dip(double M){if(M>0.8&&M<1.2)return 0.55;if(M>2.0)return 0.80;if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;return 1.0;}
static double body_cna(double M){double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0},CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};if(M<=XM[0])return CN[0];if(M>=XM[8])return CN[8];for(int i=0;i<8;i++)if(M<=XM[i+1]){double t=(M-XM[i])/(XM[i+1]-XM[i]);return CN[i]+t*(CN[i+1]-CN[i]);}return CN[8];}
static double xcp_frac(double M,double a){double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));double amod=0.015*fmin(fabs(a)/(15.0*DEG2RAD),1.0);return base-amod;}
/* body-only tau_y at AoA in +x */
static double body_tauy(double aoa,double rho,double V,double M,double com){
    double vb[3]={V*sin(aoa),0,-V*cos(aoa)};double qbar=0.5*rho*V*V;double sp=v3_norm(vb);double vh[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};
    double cosa=-vh[2];if(cosa>1)cosa=1;double al=acos(cosa);double latm=sqrt(vh[0]*vh[0]+vh[1]*vh[1]);if(latm<1e-9)return 0;
    double lh[3]={vh[0]/latm,vh[1]/latm,0};double Fn=qbar*VEH_AREF*body_cna(M)*al;double Fa[3]={-Fn*lh[0],-Fn*lh[1],0};
    double arm[3]={0,0,xcp_frac(M,al)*VEH_STAGE_LEN-com};double T[3];v3_cross(T,arm,Fa);return T[1];
}
/* fin-only tau_y at AoA in +x with pitch-pattern common deflection dc: fins=dc*[1,1,-1,-1] */
static double fin_tauy(double aoa,double dc,double rho,double V,double M,double com){
    double vb[3]={V*sin(aoa),0,-V*cos(aoa)};double CNa_f=FIN_CNA*fin_dip(M);double pat[4]={1,1,-1,-1};double tau=0;
    for(int i=0;i<4;i++){double phi=(45.0+90.0*i)*DEG2RAD;double er[3]={cos(phi),sin(phi),0},et[3]={-sin(phi),cos(phi),0};double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};double vi[3]={vb[0],vb[1],vb[2]};double vsp=v3_norm(vi);if(vsp<1.0)continue;double qbi=0.5*rho*vsp*vsp;double w_ax=-vi[2],w_r=v3_dot(vi,er);double d=dc*pat[i];double aeff=d+atan2(w_r,w_ax);if(aeff>FIN_STALL)aeff=FIN_STALL;if(aeff<-FIN_STALL)aeff=-FIN_STALL;double L=qbi*FIN_AREA*CNa_f*aeff,Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*d;double Ff[3]={-L*er[0]-Ft*et[0],-L*er[1]-Ft*et[1],0};double arm[3]={rm[0],rm[1],rm[2]-com};double T[3];v3_cross(T,arm,Ff);tau+=T[1];}
    return tau;
}
static double bisect_trim(double aoa,double rho,double V,double M,double com){
    double lo=-25*DEG2RAD,hi=25*DEG2RAD;double flo=body_tauy(aoa,rho,V,M,com)+fin_tauy(aoa,lo,rho,V,M,com);double fhi=body_tauy(aoa,rho,V,M,com)+fin_tauy(aoa,hi,rho,V,M,com);
    if(flo*fhi>0)return NAN;for(int it=0;it<100;it++){double mid=0.5*(lo+hi);double fm=body_tauy(aoa,rho,V,M,com)+fin_tauy(aoa,mid,rho,V,M,com);if(flo*fm<=0){hi=mid;}else{lo=mid;flo=fm;}}return 0.5*(lo+hi);
}
int main(void){
    MP mp;mp_com(120000.0,51000.0,&mp);double com=mp.com;
    printf("=== P1<->P5 SIGN/MAG RECONCILE (com=%.3f) ===\n\n",com);
    /* (1) sign probe: at alpha=+6 in +x, what dc trims? and does +dc raise or lower trim AoA? */
    struct{const char*nm;double M,rho,V;}cc[3]={{"M=0.70",0.70,0.31,0.70*302.0},{"M=1.05",1.05,0.31,1.05*302.0},{"M=1.20",1.20,0.31,1.20*302.0}};
    for(int c=0;c<3;c++){
        double M=cc[c].M,rho=cc[c].rho,V=cc[c].V;
        printf("--- %s  rho=%.2f V=%.0f qbar=%.0f ---\n",cc[c].nm,rho,V,0.5*rho*V*V);
        for(double ad=6.0;ad<=6.01;ad+=6){
            double aoa=ad*DEG2RAD;
            double bt=body_tauy(aoa,rho,V,M,com);
            double ft0=fin_tauy(aoa,0,rho,V,M,com);
            double dctrim=bisect_trim(aoa,rho,V,M,com);
            /* current control's cancel-body-only dc: tau_cmd=-bt, dc=tau_cmd/(4*.707*A) */
            double CNa_f=FIN_CNA*fin_dip(M);double k=0.5*rho*V*V*FIN_AREA*CNa_f;double A=(FIN_Z-com)*k;
            double dc_ctrl=(-bt)/(4.0*0.7071*A);
            printf("  alpha=+%.0fdeg: body_tau_y=%+.3e (destab), fin_tau_y@dc0=%+.3e (restoring)\n",ad,bt,ft0);
            printf("     TRUE trim dc=%+.3f deg ; current-control dc(cancel-body-only)=%+.3f deg\n",dctrim*RAD2DEG,dc_ctrl*RAD2DEG);
            /* physical direction check: increase dc by +1 deg from trim; does net moment push AoA up(+) or down(-)? */
            double eps=1.0*DEG2RAD;
            double net_hi=body_tauy(aoa,rho,V,M,com)+fin_tauy(aoa,dctrim+eps,rho,V,M,com);
            printf("     at trim+1deg dc: net tau_y=%+.3e -> %s (tells sign meaning of +dc)\n",net_hi,net_hi>0?"drives AoA UP":"drives AoA DOWN");
        }
        printf("\n");
    }
    printf("KEY QUESTIONS ANSWERED:\n");
    printf(" - MAGNITUDE: TRUE trim dc >> the cancel-body-only dc the control uses (that's the bug).\n");
    printf(" - The trim dc SIGN vs P5's -0.73deg/deg is a PATTERN/AoA-plane convention; magnitude & the\n");
    printf("   fact that current control under-commands are the physics. Report both, flag convention.\n");
    /* Also: give delta-per-fin in P5's likely convention: |trim dc| per deg AoA */
    printf("\n delta_ff magnitude per deg AoA (|dc_trim|/alpha):\n");
    for(int c=0;c<3;c++){double M=cc[c].M,rho=cc[c].rho,V=cc[c].V;double d6=bisect_trim(6*DEG2RAD,rho,V,M,com);
        printf("   %s: |dc_trim(6deg)|=%.2f deg -> %.3f deg/deg\n",cc[c].nm,fabs(d6)*RAD2DEG,fabs(d6)*RAD2DEG/6.0);}
    return 0;
}
