/* p1_trim2.c — find the TRUE trim deflection at alpha_cmd (wide sweep incl stall),
 * and give P5 the exact delta_trim(alpha) the move-the-trim-point FF should command.
 * Also confirm whether it is even achievable within +-20 deg fin limit.
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
typedef struct{double m,com,I_ax,I_tr;}MP;
static void mass_props(double ml,double mr,MP*mp){if(ml<0)ml=0;if(mr<0)mr=0;const double rt2=TANK_AREA/PI,R2=VEH_RADIUS*VEH_RADIUS;double md=VEH_DRY,zd=VEH_DRY_COMZ;double Id=md*(6.0*R2+VEH_LEN*VEH_LEN)/12.0,Ia=md*R2;double hl=ml/(LOX_RHO*TANK_AREA),zl=LOX_BASE_Z+0.5*hl;double hr=mr/(RP1_RHO*TANK_AREA),zr=RP1_BASE_Z+0.5*hr;double Ial=0.5*ml*rt2,Itl=ml*(3.0*rt2+hl*hl)/12.0;double Iar=0.5*mr*rt2,Itr=mr*(3.0*rt2+hr*hr)/12.0;double M=md+ml+mr;double com=(md*zd+ml*zl+mr*zr)/M;double I_tr=Id+md*(zd-com)*(zd-com)+Itl+ml*(zl-com)*(zl-com)+Itr+mr*(zr-com)*(zr-com);mp->m=M;mp->com=com;mp->I_ax=Ia+Ial+Iar;mp->I_tr=I_tr;}
static double fin_dip(double M){if(M>0.8&&M<1.2)return 0.55;if(M>2.0)return 0.80;if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;return 1.0;}
static double body_cna(double M){double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0},CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};if(M<=XM[0])return CN[0];if(M>=XM[8])return CN[8];for(int i=0;i<8;i++)if(M<=XM[i+1]){double t=(M-XM[i])/(XM[i+1]-XM[i]);return CN[i]+t*(CN[i+1]-CN[i]);}return CN[8];}
static double xcp_frac(double M,double a){double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));double amod=0.015*fmin(fabs(a)/(15.0*DEG2RAD),1.0);return base-amod;}
/* net pitch moment tau_y(alpha,dc) with body+fins; dc applied as pitch pattern [1,1,-1,-1] */
static double net_tauy(double aoa,double dc,double rho,double V,double M,double com){
    double vb[3]={V*sin(aoa),0,-V*cos(aoa)};double qbar=0.5*rho*V*V;double sp=v3_norm(vb);double vh[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};
    double tau=0;
    /* body */
    double cosa=-vh[2];if(cosa>1)cosa=1;double al=acos(cosa);double latm=sqrt(vh[0]*vh[0]+vh[1]*vh[1]);
    if(latm>1e-9){double lh[3]={vh[0]/latm,vh[1]/latm,0};double Fn=qbar*VEH_AREF*body_cna(M)*al;double Fa[3]={-Fn*lh[0],-Fn*lh[1],0};double arm[3]={0,0,xcp_frac(M,al)*VEH_STAGE_LEN-com};double T[3];v3_cross(T,arm,Fa);tau+=T[1];}
    /* fins */
    double CNa_f=FIN_CNA*fin_dip(M);double pat[4]={1,1,-1,-1};
    for(int i=0;i<4;i++){double phi=(45.0+90.0*i)*DEG2RAD;double er[3]={cos(phi),sin(phi),0},et[3]={-sin(phi),cos(phi),0};double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};double vi[3]={vb[0],vb[1],vb[2]};double vsp=v3_norm(vi);if(vsp<1.0)continue;double qbi=0.5*rho*vsp*vsp;double w_ax=-vi[2],w_r=v3_dot(vi,er);double d=dc*pat[i];double aeff=d+atan2(w_r,w_ax);if(aeff>FIN_STALL)aeff=FIN_STALL;if(aeff<-FIN_STALL)aeff=-FIN_STALL;double L=qbi*FIN_AREA*CNa_f*aeff,Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*d;double Ff[3]={-L*er[0]-Ft*et[0],-L*er[1]-Ft*et[1],0};double arm[3]={rm[0],rm[1],rm[2]-com};double T[3];v3_cross(T,arm,Ff);tau+=T[1];}
    return tau;
}
int main(void){
    MP mp;mass_props(120000.0,51000.0,&mp);double com=mp.com;
    printf("=== P1 TRUE trim-deflection search (com=%.3f) ===\n\n",com);
    double M=1.2,rho=0.31,V=M*302.0;
    printf("M=%.1f rho=%.2f V=%.0f qbar=%.0f. For each alpha_cmd, bisect dc in [-25,25]deg for net tau_y=0.\n",M,rho,V,0.5*rho*V*V);
    printf("alpha_cmd  dc_trim(deg)  achievable(|dc|<20)?  fin_inflow_at_alpha(deg)\n");
    for(double ad=2.0;ad<=12.01;ad+=2.0){
        double aoa=ad*DEG2RAD;
        /* wide sweep to find sign change */
        double lo=-25*DEG2RAD,hi=25*DEG2RAD;double flo=net_tauy(aoa,lo,rho,V,M,com),fhi=net_tauy(aoa,hi,rho,V,M,com);
        double dctrim=NAN;
        if(flo*fhi<0){for(int it=0;it<80;it++){double mid=0.5*(lo+hi);double fm=net_tauy(aoa,mid,rho,V,M,com);if(flo*fm<=0){hi=mid;fhi=fm;}else{lo=mid;flo=fm;}}dctrim=0.5*(lo+hi);}
        double inflow_wind=ad; /* windward fin sees ~ +alpha inflow */
        printf("  %4.1f      %s%.3f%s        %s              ~%.1f\n", ad,
               isnan(dctrim)?"":"", isnan(dctrim)?0.0:dctrim*RAD2DEG, isnan(dctrim)?" (none in +-25)":"",
               isnan(dctrim)?"NO trim exists":((fabs(dctrim)*RAD2DEG<20)?"YES":"NO (saturates)"), inflow_wind);
    }
    printf("\nKEY: dc_trim is the pitch-pattern deflection the FF must command to HOLD that AoA.\n");
    printf("The current control commands only dc=-Tbody/(4*.707*A) ~ -1.4deg at 6deg (WRONG target).\n\n");

    /* Also: the closed form P5 wants. Small-angle: net_tauy(alpha,dc)=0.
     * body:  +qbar*Aref*CNa_b*alpha*|xcp-com|  [destabilizing, +]
     * fins:  each fin aeff ~ (dc*pat_i + s_i*alpha) where s_i=sign of inflow projection.
     * For pitch-plane AoA in +x: windward fins (phi=45,315 cos>0) see +alpha inflow; (135,225) see -alpha.
     * The RESTORING fin moment at dc=0 is ~ -(FIN_Z-com)*qbar*Sf*CNa_f * (something)*alpha.
     * Provide the numeric effective gains so P5 can build the exact delta_ff(alpha). */
    printf("--- numeric gains for P5's delta_ff(alpha) closed form (M=1.2) ---\n");
    {
        double aoa=6.0*DEG2RAD;
        double t_body = net_tauy(aoa,0,rho,V,M,com); /* includes fins... separate: */
        /* isolate body and fin slopes via finite diff */
        double da=0.5*DEG2RAD;
        /* d(net)/d(dc): fin pitch authority incl its own coupling */
        double dnet_ddc = (net_tauy(aoa,da,rho,V,M,com)-net_tauy(aoa,-da,rho,V,M,com))/(2.0*da);
        /* net at dc=0 = the moment the FF must cancel with dc */
        double net0 = net_tauy(aoa,0,rho,V,M,com);
        double dc_needed = -net0/dnet_ddc;
        printf("  at alpha=6deg: net_tau_y(dc=0)=%+.4e Nm ; d(net)/d(dc)=%+.4e Nm/rad\n", net0, dnet_ddc);
        printf("  => delta_ff = -net0/(dnet/ddc) = %+.3f deg  (linearized move-the-trim-point)\n", dc_needed*RAD2DEG);
        printf("  Effective fin pitch-authority slope d(net)/d(dc) = %.4e Nm/rad = 4*0.707*A_eff.\n", dnet_ddc);
        double CNa_f=FIN_CNA*fin_dip(M);double k=0.5*rho*V*V*FIN_AREA*CNa_f;double A=(FIN_Z-com)*k;
        printf("  For reference 4*0.707*A(alloc) = %.4e Nm/rad (allocator's assumed gain).\n", 4.0*0.7071*A);
        printf("  ratio true/assumed = %.3f (dc authority the allocator gets right within this factor)\n", dnet_ddc/(4.0*0.7071*A));
    }
    return 0;
}
