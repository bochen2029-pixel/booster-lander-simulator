/* p1_trimfix.c — CORRECTED trim test. Prior harness had an AXIS MISMATCH: it put AoA in +x
 * (aero moment about +y) but countered with the PITCH pattern [1,1,-1,-1] (which makes tau_x),
 * so it only trimmed through the 35% cross-coupling -> inflated the trim deflection ~2.86x.
 *
 * FIX: AoA in +x is a pitch-plane (about y) disturbance. Counter with the YAW pattern
 * [-1,1,1,-1] which is the pure tau_y generator. Equivalently AoA in +y with pitch pattern.
 * Now d(fin_tau_y)/d(dyaw) should be the full 4*0.707*A and the trim deflection should DROP.
 *
 * This reconciles P1 with P5 (~0.73 deg/deg) and gives the CORRECT delta_ff.
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
static double com_of(double ml,double mr){double md=VEH_DRY,zd=VEH_DRY_COMZ;double hl=ml/(LOX_RHO*TANK_AREA),zl=LOX_BASE_Z+0.5*hl;double hr=mr/(RP1_RHO*TANK_AREA),zr=RP1_BASE_Z+0.5*hr;double M=md+ml+mr;return(md*zd+ml*zl+mr*zr)/M;}
static double fin_dip(double M){if(M>0.8&&M<1.2)return 0.55;if(M>2.0)return 0.80;if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;return 1.0;}
static double body_cna(double M){double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0},CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};if(M<=XM[0])return CN[0];if(M>=XM[8])return CN[8];for(int i=0;i<8;i++)if(M<=XM[i+1]){double t=(M-XM[i])/(XM[i+1]-XM[i]);return CN[i]+t*(CN[i+1]-CN[i]);}return CN[8];}
static double xcp_frac(double M,double a){double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));double amod=0.015*fmin(fabs(a)/(15.0*DEG2RAD),1.0);return base-amod;}

/* full plant: body + fins, arbitrary AoA plane, arbitrary per-fin deflections. returns Tb[3]. */
static void plant_moment(const double vb[3],const double delta[4],double rho,double M,double com,int body,int fins,double Tb[3]){
    Tb[0]=Tb[1]=Tb[2]=0;double qbar=0.5*rho*v3_norm(vb)*v3_norm(vb);double sp=v3_norm(vb);double vh[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};
    if(body){double cosa=-vh[2];if(cosa>1)cosa=1;if(cosa<-1)cosa=-1;double al=acos(cosa);double latm=sqrt(vh[0]*vh[0]+vh[1]*vh[1]);if(latm>1e-9){double lh[3]={vh[0]/latm,vh[1]/latm,0};double Fn=qbar*VEH_AREF*body_cna(M)*al;double Fa[3]={-Fn*lh[0],-Fn*lh[1],0};double arm[3]={0,0,xcp_frac(M,al)*VEH_STAGE_LEN-com};double T[3];v3_cross(T,arm,Fa);Tb[0]+=T[0];Tb[1]+=T[1];Tb[2]+=T[2];}}
    if(fins){double CNa_f=FIN_CNA*fin_dip(M);for(int i=0;i<4;i++){double phi=(45.0+90.0*i)*DEG2RAD;double er[3]={cos(phi),sin(phi),0},et[3]={-sin(phi),cos(phi),0};double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};double vi[3]={vb[0],vb[1],vb[2]};double vsp=v3_norm(vi);if(vsp<1.0)continue;double qbi=0.5*rho*vsp*vsp;double w_ax=-vi[2],w_r=v3_dot(vi,er);double d=delta[i];double aeff=d+atan2(w_r,w_ax);if(aeff>FIN_STALL)aeff=FIN_STALL;if(aeff<-FIN_STALL)aeff=-FIN_STALL;double L=qbi*FIN_AREA*CNa_f*aeff,Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*d;double Ff[3]={-L*er[0]-Ft*et[0],-L*er[1]-Ft*et[1],0};double arm[3]={rm[0],rm[1],rm[2]-com};double T[3];v3_cross(T,arm,Ff);Tb[0]+=T[0];Tb[1]+=T[1];Tb[2]+=T[2];}}
}

int main(void){
    double com=com_of(120000.0,51000.0);
    printf("=== CORRECTED trim: AoA in +x countered by YAW pattern [-1,1,1,-1] (pure tau_y) ===\n");
    printf("(com=%.3f)\n\n",com);
    struct{const char*nm;double M,rho,V;}cc[4]={{"M=0.70",0.70,0.31,0.70*302.0},{"M=1.05",1.05,0.31,1.05*302.0},{"M=1.20",1.20,0.31,1.20*302.0},{"M=2.00",2.00,0.31,2.00*302.0}};
    double patY[4]={-1,1,1,-1};
    printf(" cond    alpha  body_tau_y   fin_tau_y@0   d(fin_y)/d(dyaw)   TRIM dyaw(deg)  deg/deg\n");
    for(int c=0;c<4;c++){
        double M=cc[c].M,rho=cc[c].rho,V=cc[c].V;
        for(double ad=6.0;ad<=6.01;ad+=6){
            double aoa=ad*DEG2RAD;double vb[3]={V*sin(aoa),0,-V*cos(aoa)};
            double d0[4]={0,0,0,0};double Tb0[3];plant_moment(vb,d0,rho,M,com,1,0,Tb0);/*body*/
            double Tf0[3];plant_moment(vb,d0,rho,M,com,0,1,Tf0);/*fin at 0*/
            /* slope d(fin_tau_y)/d(dyaw): perturb along yaw pattern */
            double e=0.25*DEG2RAD;double dp[4],dm[4];for(int i=0;i<4;i++){dp[i]=e*patY[i];dm[i]=-e*patY[i];}
            double Tp[3],Tm[3];plant_moment(vb,dp,rho,M,com,0,1,Tp);plant_moment(vb,dm,rho,M,com,0,1,Tm);
            double slope=(Tp[1]-Tm[1])/(2*e);
            /* bisect trim over dyaw */
            double lo=-25*DEG2RAD,hi=25*DEG2RAD;
            double flo,fhi;{double dd[4];for(int i=0;i<4;i++)dd[i]=lo*patY[i];double T[3];plant_moment(vb,dd,rho,M,com,1,1,T);flo=T[1];}
            {double dd[4];for(int i=0;i<4;i++)dd[i]=hi*patY[i];double T[3];plant_moment(vb,dd,rho,M,com,1,1,T);fhi=T[1];}
            double trim=NAN;if(flo*fhi<0){for(int it=0;it<100;it++){double mid=0.5*(lo+hi);double dd[4];for(int i=0;i<4;i++)dd[i]=mid*patY[i];double T[3];plant_moment(vb,dd,rho,M,com,1,1,T);double fm=T[1];if(flo*fm<=0)hi=mid;else{lo=mid;flo=fm;}}trim=0.5*(lo+hi);}
            printf(" %s  %4.1f  %+.3e  %+.3e   %+.4e   %+8.3f      %.3f\n",
                   cc[c].nm,ad,Tb0[1],Tf0[1],slope,isnan(trim)?0.0:trim*RAD2DEG,isnan(trim)?0.0:fabs(trim)*RAD2DEG/ad);
        }
    }
    printf("\n Compare the slope to 4*0.707*A: at M=1.2 that is 6.70e6; the yaw pattern should realize\n");
    printf(" the full slope (minus small cross), NOT the 2.34e6 the pitch pattern gave on the y-axis.\n\n");
    /* explicit M=1.2 detail */
    {
        double M=1.2,rho=0.31,V=M*302.0,aoa=6*DEG2RAD;double vb[3]={V*sin(aoa),0,-V*cos(aoa)};
        double CNa_f=FIN_CNA*fin_dip(M);double k=0.5*rho*V*V*FIN_AREA*CNa_f;double A=(FIN_Z-com)*k;
        printf(" M=1.2 detail: 4*0.707*A=%.4e ; body_tau_y@6deg=%+.4e ; current control cancels body only:\n",4*0.7071*A,0.0);
        double Tb0[3];double d0[4]={0,0,0,0};plant_moment(vb,d0,rho,M,com,1,0,Tb0);
        double dc_ctrl=(-Tb0[1])/(4.0*0.7071*A);
        printf("   dc_ctrl(cancel-body)=%+.3f deg via yaw pattern (this is the FF the code emits, ~small).\n",dc_ctrl*RAD2DEG);
        printf("   The TRUE trim (table above) is what P5's move-the-trim-point must command instead.\n");
    }
    return 0;
}
