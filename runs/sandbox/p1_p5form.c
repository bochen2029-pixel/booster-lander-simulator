/* p1_p5form.c — evaluate P5's EXACT delta_ff formula and compare to my full-plant trim.
 * P5 formula (yaw-pattern amplitude):
 *   delta_ff = -[(xcp-com)*Aref*CNa(M)] / [4*0.7071*(FIN_Z-com)*S_f*CNa_f(M)] * alpha_cmd
 * This is BODY-moment / fin-authority. Compare to full-plant bisected trim (~0.73 deg/deg).
 * Resolve whether P5's formula gives 0.73 (agrees) or the tiny body-only value.
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
#define FIN_STALL (25.0*DEG2RAD)
#define FIN_CNA 3.0
#define FIN_CT_DELTA_FRAC 0.35
static void v3_cross(double o[3],const double a[3],const double b[3]){double x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0];o[0]=x;o[1]=y;o[2]=z;}
static double v3_dot(const double a[3],const double b[3]){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static double v3_norm(const double a[3]){return sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
static double com_of(double ml,double mr){double md=VEH_DRY,zd=VEH_DRY_COMZ;double hl=ml/(LOX_RHO*TANK_AREA),zl=LOX_BASE_Z+0.5*hl;double hr=mr/(RP1_RHO*TANK_AREA),zr=RP1_BASE_Z+0.5*hr;double M=md+ml+mr;return(md*zd+ml*zl+mr*zr)/M;}
static double fin_dip(double M){if(M>0.8&&M<1.2)return 0.55;if(M>2.0)return 0.80;if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;return 1.0;}
static double body_cna(double M){double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0},CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};if(M<=XM[0])return CN[0];if(M>=XM[8])return CN[8];for(int i=0;i<8;i++)if(M<=XM[i+1]){double t=(M-XM[i])/(XM[i+1]-XM[i]);return CN[i]+t*(CN[i+1]-CN[i]);}return CN[8];}
static double xcp_frac(double M,double a){double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));double amod=0.015*fmin(fabs(a)/(15.0*DEG2RAD),1.0);return base-amod;}
static void plant_M(const double vb[3],const double d[4],double rho,double M,double com,int body,int fins,double Tb[3]){Tb[0]=Tb[1]=Tb[2]=0;double qbar=0.5*rho*v3_norm(vb)*v3_norm(vb);double sp=v3_norm(vb);double vh[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};if(body){double cosa=-vh[2];if(cosa>1)cosa=1;if(cosa<-1)cosa=-1;double al=acos(cosa);double latm=sqrt(vh[0]*vh[0]+vh[1]*vh[1]);if(latm>1e-9){double lh[3]={vh[0]/latm,vh[1]/latm,0};double Fn=qbar*VEH_AREF*body_cna(M)*al;double Fa[3]={-Fn*lh[0],-Fn*lh[1],0};double arm[3]={0,0,xcp_frac(M,al)*VEH_STAGE_LEN-com};double T[3];v3_cross(T,arm,Fa);Tb[0]+=T[0];Tb[1]+=T[1];Tb[2]+=T[2];}}if(fins){double CNa_f=FIN_CNA*fin_dip(M);for(int i=0;i<4;i++){double phi=(45.0+90.0*i)*DEG2RAD;double er[3]={cos(phi),sin(phi),0},et[3]={-sin(phi),cos(phi),0};double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};double vi[3]={vb[0],vb[1],vb[2]};double vsp=v3_norm(vi);if(vsp<1.0)continue;double qbi=0.5*rho*vsp*vsp;double w_ax=-vi[2],w_r=v3_dot(vi,er);double dd=d[i];double aeff=dd+atan2(w_r,w_ax);if(aeff>FIN_STALL)aeff=FIN_STALL;if(aeff<-FIN_STALL)aeff=-FIN_STALL;double L=qbi*FIN_AREA*CNa_f*aeff,Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*dd;double Ff[3]={-L*er[0]-Ft*et[0],-L*er[1]-Ft*et[1],0};double arm[3]={rm[0],rm[1],rm[2]-com};double T[3];v3_cross(T,arm,Ff);Tb[0]+=T[0];Tb[1]+=T[1];Tb[2]+=T[2];}}}
static double trim_yaw(double aoa,double rho,double M,double com,double V){double vb[3]={V*sin(aoa),0,-V*cos(aoa)};double patY[4]={-1,1,1,-1};double lo=-25*DEG2RAD,hi=25*DEG2RAD;double flo;{double dd[4];for(int i=0;i<4;i++)dd[i]=lo*patY[i];double T[3];plant_M(vb,dd,rho,M,com,1,1,T);flo=T[1];}double fhi;{double dd[4];for(int i=0;i<4;i++)dd[i]=hi*patY[i];double T[3];plant_M(vb,dd,rho,M,com,1,1,T);fhi=T[1];}if(flo*fhi>0)return NAN;for(int it=0;it<100;it++){double mid=0.5*(lo+hi);double dd[4];for(int i=0;i<4;i++)dd[i]=mid*patY[i];double T[3];plant_M(vb,dd,rho,M,com,1,1,T);double fm=T[1];if(flo*fm<=0)hi=mid;else{lo=mid;flo=fm;}}return 0.5*(lo+hi);}
int main(void){
    double com=com_of(7000,3000);double alpha=6*DEG2RAD;
    printf("=== P5 formula vs P1 full-plant trim (near-empty com=%.3f, alpha=6deg) ===\n\n",com);
    printf(" Mach   P5_formula(deg)  P1_fullplant_trim(deg)   agree?\n");
    double Ms[5]={0.70,0.90,1.05,1.20,2.00};
    for(int k=0;k<5;k++){
        double M=Ms[k],rho=0.31,V=M*302.0,qbar=0.5*rho*V*V;
        double CNa=body_cna(M),CNa_f=FIN_CNA*fin_dip(M);
        double xcp=xcp_frac(M,alpha)*VEH_STAGE_LEN, sm=xcp-com;
        /* P5 formula: note qbar cancels (body Fn/fin authority both ~ qbar) so it's qbar-free */
        double p5 = -(sm*VEH_AREF*CNa)/(4.0*0.7071*(FIN_Z-com)*FIN_AREA*CNa_f)*alpha;
        double p1 = trim_yaw(alpha,rho,M,com,V);
        printf(" %4.2f   %+8.3f        %+8.3f              %s\n",M,p5*RAD2DEG,isnan(p1)?0:p1*RAD2DEG,
               (fabs(p5*RAD2DEG-(isnan(p1)?0:p1*RAD2DEG))<1.0)?"YES":"NO (differ)");
    }
    printf("\nIf P5 differs and is small/sign-flipping while P1 is ~0.73 single-sign: P5's WRITTEN\n");
    printf("formula is body-only (misses fins' restoring inflow). If they AGREE, P5's formula is\n");
    printf("secretly capturing the full effect (e.g. the body CN already dominates at this com).\n");
    /* Diagnostic: at near-empty com, how big is body Fn vs fins-restoring? */
    printf("\nDIAGNOSTIC (M=1.05): body_tau@6deg=%.3e, fin_tau@0defl=%.3e -> ratio fin/body=%.2f\n",
           0.0,0.0,0.0);
    {double M=1.05,rho=0.31,V=M*302.0;double vb[3]={V*sin(alpha),0,-V*cos(alpha)};double d0[4]={0,0,0,0};double Tb[3],Tf[3];plant_M(vb,d0,rho,M,com,1,0,Tb);plant_M(vb,d0,rho,M,com,0,1,Tf);
     printf("  body_tau_y=%.3e  fin_tau_y@0=%.3e  |fin/body|=%.2f  => the fin restoring is the bigger term\n",Tb[1],Tf[1],fabs(Tf[1]/Tb[1]));}
    return 0;
}
