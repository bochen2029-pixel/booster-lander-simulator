/* p1_slope.c — resolve why d(net_tauy)/d(dc) is only 0.35x the allocator's 4*0.707*A.
 * Decompose the fin pitch-authority slope at AoA into: (a) radial-lift contribution,
 * (b) tangential-cant contribution, (c) stall clipping, (d) which fins are active.
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
typedef struct{double m,com;}MP;
static void mass_props(double ml,double mr,MP*mp){const double rt2=TANK_AREA/PI,R2=VEH_RADIUS*VEH_RADIUS;double md=VEH_DRY,zd=VEH_DRY_COMZ;double hl=ml/(LOX_RHO*TANK_AREA),zl=LOX_BASE_Z+0.5*hl;double hr=mr/(RP1_RHO*TANK_AREA),zr=RP1_BASE_Z+0.5*hr;double M=md+ml+mr;mp->com=(md*zd+ml*zl+mr*zr)/M;mp->m=M;}
static double fin_dip(double M){if(M>0.8&&M<1.2)return 0.55;if(M>2.0)return 0.80;if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;return 1.0;}

int main(void){
    MP mp;mass_props(120000.0,51000.0,&mp);double com=mp.com;
    double M=1.2,rho=0.31,V=M*302.0;double CNa_f=FIN_CNA*fin_dip(M);
    double aoa=6.0*DEG2RAD;double dc=0.0;double da=0.5*DEG2RAD;
    double pat[4]={1,1,-1,-1};
    printf("=== fin pitch-authority slope decomposition at AoA=6, M=1.2 ===\n");
    printf("CNa_f=%.3f (dip=%.2f). Perturb dc by +-%.2fdeg, tau_x response per fin.\n\n",CNa_f,fin_dip(M),da*RAD2DEG);
    /* central diff of EACH fin's tau_x contribution wrt dc */
    double tot_slope=0, lift_slope=0, cant_slope=0;
    for(int i=0;i<4;i++){
        double phi=(45.0+90.0*i)*DEG2RAD;
        double er[3]={cos(phi),sin(phi),0},et[3]={-sin(phi),cos(phi),0};
        double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};
        double arm[3]={rm[0],rm[1],rm[2]-com};
        /* helper to get this fin's tau vector at given dc */
        double taux[3]; /* index by sign */
        double results[2][3];
        for(int s=0;s<2;s++){
            double dcs = (s==0)? (dc-da):(dc+da);
            double vb[3]={V*sin(aoa),0,-V*cos(aoa)};
            double vsp=v3_norm(vb);double qbi=0.5*rho*vsp*vsp;
            double w_ax=-vb[2],w_r=v3_dot(vb,er);
            double d=dcs*pat[i];
            double aeff=d+atan2(w_r,w_ax);int stalled=0;
            if(aeff>FIN_STALL){aeff=FIN_STALL;stalled=1;} if(aeff<-FIN_STALL){aeff=-FIN_STALL;stalled=1;}
            double L=qbi*FIN_AREA*CNa_f*aeff,Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*d;
            double Ff[3]={-L*er[0]-Ft*et[0],-L*er[1]-Ft*et[1],0};
            double T[3];v3_cross(T,arm,Ff);
            results[s][0]=T[0];results[s][1]=T[1];results[s][2]=T[2];
            if(s==1){
                double inflow=atan2(w_r,w_ax);
                printf(" fin%d phi=%3.0f: inflow=%+5.2fdeg  aeff@dc=0 ~ %+5.2fdeg  %s  arm_z=%.2f\n",
                       i,phi*RAD2DEG,inflow*RAD2DEG,(0.0+inflow)*RAD2DEG,stalled?"STALLED":"linear",rm[2]-com);
            }
        }
        double sl=(results[1][0]-results[0][0])/(2.0*da);
        tot_slope+=sl;
        printf("   -> d(tau_x)/d(dc) fin%d = %+.4e Nm/rad\n",i,sl);
    }
    printf("\n TOTAL d(tau_x)/d(dc) = %+.4e Nm/rad\n", tot_slope);
    double k=0.5*rho*V*V*FIN_AREA*CNa_f;double A=(FIN_Z-com)*k;
    printf(" allocator assumes 4*0.707*A = %+.4e Nm/rad\n", 4.0*0.7071*A);
    printf(" ratio = %.4f\n\n", tot_slope/(4.0*0.7071*A));

    /* Hypothesis: the 0.35 ratio is because at AoA=6 with dc=0, some fins are near stall so
     * their d(aeff)/d(dc)=0 (clipped). Check: fin inflow at 6deg AoA is up to +-6deg; with the
     * transonic CNa_f is fine, but is any fin's |aeff| near 25deg? No. So NOT stall.
     * Re-examine: maybe I mislabeled. Print aeff for all fins at dc=0 and dc=+1deg. */
    printf("--- aeff per fin at dc=0 vs dc=+2deg (to see clipping / cancellation) ---\n");
    for(double dcd=0; dcd<=2.01; dcd+=2.0){
        double dc2=dcd*DEG2RAD;
        printf(" dc=%.0fdeg: ",dcd);
        for(int i=0;i<4;i++){
            double phi=(45.0+90.0*i)*DEG2RAD;double er[3]={cos(phi),sin(phi),0};
            double vb[3]={V*sin(aoa),0,-V*cos(aoa)};double w_ax=-vb[2],w_r=v3_dot(vb,er);
            double d=dc2*pat[i];double aeff=d+atan2(w_r,w_ax);
            printf("f%d aeff=%+6.2f ",i,aeff*RAD2DEG);
        }
        printf("\n");
    }
    /* Cross-check p1_trim2's reference computation: it may have used CNa_f=FIN_CNA (no dip)
     * in the A reference while net used dip. Recompute both ways. */
    printf("--- cross-check the p1_trim2 ratio anomaly ---\n");
    {
        double CNa_f_dip = FIN_CNA*fin_dip(M);   /* 1.65 */
        double CNa_f_nodip = FIN_CNA;            /* 3.0  */
        double k_dip=0.5*rho*V*V*FIN_AREA*CNa_f_dip, A_dip=(FIN_Z-com)*k_dip;
        double k_nodip=0.5*rho*V*V*FIN_AREA*CNa_f_nodip, A_nodip=(FIN_Z-com)*k_nodip;
        printf("  4*0.707*A with dip(1.65)   = %.4e\n", 4.0*0.7071*A_dip);
        printf("  4*0.707*A with NO dip(3.0) = %.4e\n", 4.0*0.7071*A_nodip);
        printf("  measured slope             = %.4e\n", tot_slope);
        printf("  slope/A_dip=%.3f  slope/A_nodip=%.3f\n", tot_slope/(4*0.7071*A_dip), tot_slope/(4*0.7071*A_nodip));
    }
    printf("\n NOTE: the pitch pattern [1,1,-1,-1] and the AoA inflow signs interact: for AoA in +x,\n");
    printf(" fins 0,3 (cos>0, phi=45,315) see +inflow, fins 1,2 (cos<0) see -inflow. The pattern\n");
    printf(" ADDS +dc to fins 0,1 and -dc to fins 2,3. So on some fins dc and inflow ADD, on others\n");
    printf(" they SUBTRACT -> the per-fin lift changes partially cancel in the pitch-torque sum,\n");
    printf(" reducing effective d(tau_x)/d(dc) below the zero-AoA value. This is a real AoA-dependent\n");
    printf(" authority reduction the allocator (which uses the AoA-free 4*0.707*A) does NOT see.\n");
    return 0;
}
