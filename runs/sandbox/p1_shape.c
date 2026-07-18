/* p1_shape.c — tabulate net tau_y(dc) finely at alpha=6, M=1.2 to see the true nonlinearity
 * and settle the trim magnitude (bisection 8.1 vs linearized 2.95 vs P5 4.2).
 * Also print per-fin aeff and whether the pattern makes fins CROSS zero-lift (sign flip)
 * which would explain a kink. Print d(fin_tauy)/d(dc) locally at several dc.
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
#define FIN_STALL (25.0*DEG2RAD)
#define FIN_CT_DELTA_FRAC 0.35
static void v3_cross(double o[3],const double a[3],const double b[3]){double x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0];o[0]=x;o[1]=y;o[2]=z;}
static double v3_dot(const double a[3],const double b[3]){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static double v3_norm(const double a[3]){return sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
static double com_of(double ml,double mr){double md=VEH_DRY,zd=VEH_DRY_COMZ;double hl=ml/(LOX_RHO*TANK_AREA),zl=LOX_BASE_Z+0.5*hl;double hr=mr/(RP1_RHO*TANK_AREA),zr=RP1_BASE_Z+0.5*hr;double M=md+ml+mr;return(md*zd+ml*zl+mr*zr)/M;}
static double fin_dip(double M){if(M>0.8&&M<1.2)return 0.55;if(M>2.0)return 0.80;if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;return 1.0;}
static double body_cna(double M){double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0},CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};if(M<=XM[0])return CN[0];if(M>=XM[8])return CN[8];for(int i=0;i<8;i++)if(M<=XM[i+1]){double t=(M-XM[i])/(XM[i+1]-XM[i]);return CN[i]+t*(CN[i+1]-CN[i]);}return CN[8];}
static double xcp_frac(double M,double a){double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));double amod=0.015*fmin(fabs(a)/(15.0*DEG2RAD),1.0);return base-amod;}
static double body_tauy(double aoa,double rho,double V,double M,double com){double vb[3]={V*sin(aoa),0,-V*cos(aoa)};double qbar=0.5*rho*V*V;double sp=v3_norm(vb);double vh[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};double cosa=-vh[2];if(cosa>1)cosa=1;double al=acos(cosa);double latm=sqrt(vh[0]*vh[0]+vh[1]*vh[1]);if(latm<1e-9)return 0;double lh[3]={vh[0]/latm,vh[1]/latm,0};double Fn=qbar*VEH_AREF*body_cna(M)*al;double Fa[3]={-Fn*lh[0],-Fn*lh[1],0};double arm[3]={0,0,xcp_frac(M,al)*VEH_STAGE_LEN-com};double T[3];v3_cross(T,arm,Fa);return T[1];}
static double fin_tauy(double aoa,double dc,double rho,double V,double M,double com,int verbose){double vb[3]={V*sin(aoa),0,-V*cos(aoa)};double CNa_f=FIN_CNA*fin_dip(M);double pat[4]={1,1,-1,-1};double tau=0;for(int i=0;i<4;i++){double phi=(45.0+90.0*i)*DEG2RAD;double er[3]={cos(phi),sin(phi),0},et[3]={-sin(phi),cos(phi),0};double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};double vi[3]={vb[0],vb[1],vb[2]};double vsp=v3_norm(vi);if(vsp<1.0)continue;double qbi=0.5*rho*vsp*vsp;double w_ax=-vi[2],w_r=v3_dot(vi,er);double d=dc*pat[i];double raw=d+atan2(w_r,w_ax);double aeff=raw;int st=0;if(aeff>FIN_STALL){aeff=FIN_STALL;st=1;}if(aeff<-FIN_STALL){aeff=-FIN_STALL;st=1;}double L=qbi*FIN_AREA*CNa_f*aeff,Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*d;double Ff[3]={-L*er[0]-Ft*et[0],-L*er[1]-Ft*et[1],0};double arm[3]={rm[0],rm[1],rm[2]-com};double T[3];v3_cross(T,arm,Ff);tau+=T[1];if(verbose)printf("    f%d raw_aeff=%+6.2f%s Tf_y=%+.3e\n",i,raw*RAD2DEG,st?"(STALL)":"",T[1]);}return tau;}
int main(void){
    double com=com_of(120000.0,51000.0);double M=1.2,rho=0.31,V=M*302.0,aoa=6*DEG2RAD;
    printf("=== net tau_y(dc) at alpha=6deg M=1.2 (com=%.3f) ===\n",com);
    double bt=body_tauy(aoa,rho,V,M,com);
    printf("body_tau_y (const in dc)=%+.4e\n",bt);
    printf(" dc(deg)  fin_tau_y     net_tau_y    local d(net)/d(dc)[Nm/rad]\n");
    double prev=NAN,pdc=NAN;
    for(double dcd=-2.0;dcd<=12.01;dcd+=1.0){
        double dc=dcd*DEG2RAD;double ft=fin_tauy(aoa,dc,rho,V,M,com,0);double net=bt+ft;
        double slope=NAN;if(!isnan(prev))slope=(net-prev)/((dcd-pdc)*DEG2RAD);
        printf(" %+5.1f   %+.4e  %+.4e   %s%.3e\n",dcd,ft,net,isnan(slope)?"":"",isnan(slope)?0.0:slope);
        prev=net;pdc=dcd;
    }
    printf("\n Per-fin aeff at the bisected trim dc=+8.12deg:\n");
    fin_tauy(aoa,8.119*DEG2RAD,rho,V,M,com,1);
    printf("\n Per-fin aeff at dc=+2.95deg (linearized guess):\n");
    fin_tauy(aoa,2.95*DEG2RAD,rho,V,M,com,1);
    printf("\n Local slope d(fin)/d(dc) near dc=0 (small pert) and near dc=8deg:\n");
    double e=0.25*DEG2RAD;
    double s0=(fin_tauy(aoa,e,rho,V,M,com,0)-fin_tauy(aoa,-e,rho,V,M,com,0))/(2*e);
    double s8=(fin_tauy(aoa,(8.119*DEG2RAD)+e,rho,V,M,com,0)-fin_tauy(aoa,(8.119*DEG2RAD)-e,rho,V,M,com,0))/(2*e);
    printf("   d(fin)/d(dc)@dc0 = %.4e ; @dc8 = %.4e (if equal -> linear -> trim is where line hits -bt)\n",s0,s8);
    /* linear prediction using s0: dc_lin = -(bt+fin(0))/s0 */
    double f0=fin_tauy(aoa,0,rho,V,M,com,0);
    printf("   fin(0)=%.4e ; linear trim dc = -(bt+fin0)/s0 = %.3f deg\n",f0,(-(bt+f0)/s0)*RAD2DEG);
    printf("\n If d(fin)/d(dc) is the SAME at dc0 and dc8, the net is a straight line and the ONLY trim is\n");
    printf(" the bisection value; the '2.95' came from using a WRONG fin AoA-slope. The bisection is truth.\n");
    return 0;
}
