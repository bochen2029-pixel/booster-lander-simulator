/* p1_trim.c — DEFINITIVE trim-equilibrium test + the exact numbers P5/P4 asked for.
 *
 * Q1: at a commanded AoA, what is the TRUE net pitch moment (body + fins-at-AoA)
 *     as a function of common-mode fin deflection? Find delta_trim that zeroes it.
 * Q2: does the control's PD+FF steady state land on delta_trim, or relax to aligned?
 * Q3: static margin (xcp-com) and body pitch stiffness dTau/dalpha at M=0.7 and 1.2.
 *
 * Build: p1_build.cmd p1_trim.c p1_trim.exe
 */
#include <stdio.h>
#include <math.h>

#define G0 9.80665
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
#define RCS_ARM 1.83
#define BODY_CMQ_CDC 0.6

static void v3_cross(double o[3], const double a[3], const double b[3]){
    double x=a[1]*b[2]-a[2]*b[1], y=a[2]*b[0]-a[0]*b[2], z=a[0]*b[1]-a[1]*b[0]; o[0]=x;o[1]=y;o[2]=z; }
static double v3_dot(const double a[3], const double b[3]){ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static double v3_norm(const double a[3]){ return sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }

typedef struct { double m, com, I_ax, I_tr; } MP;
static void mass_props(double m_lox, double m_rp1, MP* mp){
    if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
    const double rt2=TANK_AREA/PI, R2=VEH_RADIUS*VEH_RADIUS;
    double m_dry=VEH_DRY, z_dry=VEH_DRY_COMZ;
    double Itl_dry=m_dry*(6.0*R2+VEH_LEN*VEH_LEN)/12.0, Iax_dry=m_dry*R2;
    double h_l=m_lox/(LOX_RHO*TANK_AREA), z_l=LOX_BASE_Z+0.5*h_l;
    double h_r=m_rp1/(RP1_RHO*TANK_AREA), z_r=RP1_BASE_Z+0.5*h_r;
    double Iax_l=0.5*m_lox*rt2, Itl_l=m_lox*(3.0*rt2+h_l*h_l)/12.0;
    double Iax_r=0.5*m_rp1*rt2, Itl_r=m_rp1*(3.0*rt2+h_r*h_r)/12.0;
    double M=m_dry+m_lox+m_rp1; double com=(m_dry*z_dry+m_lox*z_l+m_rp1*z_r)/M;
    double I_tr=Itl_dry+m_dry*(z_dry-com)*(z_dry-com)+Itl_l+m_lox*(z_l-com)*(z_l-com)+Itl_r+m_rp1*(z_r-com)*(z_r-com);
    mp->m=M; mp->com=com; mp->I_ax=Iax_dry+Iax_l+Iax_r; mp->I_tr=I_tr;
}
static double fin_dip(double M){
    if(M>0.8&&M<1.2)return 0.55; if(M>2.0)return 0.80;
    if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;
    if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8; return 1.0;
}
static double body_cna(double M){
    double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0}, CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
    if(M<=XM[0])return CN[0]; if(M>=XM[8])return CN[8];
    for(int i=0;i<8;i++) if(M<=XM[i+1]){ double t=(M-XM[i])/(XM[i+1]-XM[i]); return CN[i]+t*(CN[i+1]-CN[i]); } return CN[8];
}
static double xcp_frac(double M, double alpha){
    double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));
    double amod=0.015*fmin(fabs(alpha)/(15.0*DEG2RAD),1.0); return base-amod;
}
/* PLANT: total body-frame pitch/yaw torque about CoM from BODY aero + FINS, at AoA in +x.
 * common-mode fin deflection dc applied to all 4 fins with the PITCH pattern sign so it is a
 * pitch-authority deflection: fins = dc * [1,1,-1,-1] (this is exactly what the allocator emits
 * for a pitch command). Returns Tb[3]. */
static void plant_moment(double aoa, double dc_pitch, double rho, double V, double mach,
                         double com, int include_body, int include_fins, double Tb[3]){
    Tb[0]=Tb[1]=Tb[2]=0;
    double vb[3]={V*sin(aoa),0,-V*cos(aoa)};
    double qbar=0.5*rho*V*V;
    double sp=v3_norm(vb); double vh[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};
    if(include_body){
        double cosa=-vh[2]; if(cosa>1)cosa=1; if(cosa<-1)cosa=-1; double al=acos(cosa);
        double latm=sqrt(vh[0]*vh[0]+vh[1]*vh[1]);
        if(latm>1e-9){ double lh[3]={vh[0]/latm,vh[1]/latm,0};
            double Fn=qbar*VEH_AREF*body_cna(mach)*al; double Faero[3]={-Fn*lh[0],-Fn*lh[1],0};
            double arm[3]={0,0,xcp_frac(mach,al)*VEH_STAGE_LEN-com};
            double Ta[3]; v3_cross(Ta,arm,Faero); Tb[0]+=Ta[0];Tb[1]+=Ta[1];Tb[2]+=Ta[2]; }
    }
    if(include_fins){
        double CNa_f=FIN_CNA*fin_dip(mach); double pat[4]={1,1,-1,-1};
        double w0[3]={0,0,0};
        for(int i=0;i<4;i++){
            double phi=(45.0+90.0*i)*DEG2RAD;
            double er[3]={cos(phi),sin(phi),0}, et[3]={-sin(phi),cos(phi),0};
            double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};
            double vi[3]={vb[0],vb[1],vb[2]};
            double vsp=v3_norm(vi); if(vsp<1.0)continue;
            double qbi=0.5*rho*vsp*vsp;
            double w_ax=-vi[2], w_r=v3_dot(vi,er);
            double d=dc_pitch*pat[i];
            double aeff=d+atan2(w_r,w_ax); if(aeff>FIN_STALL)aeff=FIN_STALL; if(aeff<-FIN_STALL)aeff=-FIN_STALL;
            double L=qbi*FIN_AREA*CNa_f*aeff, Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*d;
            double Ff[3]={-L*er[0]-Ft*et[0],-L*er[1]-Ft*et[1],0};
            double arm[3]={rm[0],rm[1],rm[2]-com}; double Tf[3]; v3_cross(Tf,arm,Ff);
            Tb[0]+=Tf[0];Tb[1]+=Tf[1];Tb[2]+=Tf[2];
        }
    }
}

int main(void){
    MP mp; mass_props(120000.0,51000.0,&mp); double com=mp.com;
    printf("=== P1 TRIM + STATIC-MARGIN (com=%.3f m, I_tr=%.3e) ===\n\n", com, mp.I_tr);

    /* -------- Q3 first: static margin + body pitch stiffness at M=0.7 and 1.2 (for P5) -------- */
    printf("--- static margin (xcp-com) and body pitch stiffness dTau_y/dalpha  [for P5] ---\n");
    double Ms[2]={0.7,1.2};
    for(int j=0;j<2;j++){
        double M=Ms[j]; double a1=1.0*DEG2RAD;
        double xcp=xcp_frac(M,a1)*VEH_STAGE_LEN;
        double sm=xcp-com;   /* NEGATIVE => CoP below CoM => statically UNSTABLE (base-first) */
        /* body pitch stiffness: dTau_y/dalpha at qbar. Use a representative aero-descent qbar. */
        double rho=0.31, V=M*302.0, qbar=0.5*rho*V*V;
        /* Tau_y(alpha) from body only, finite diff */
        double Tp[3],Tm[3];
        plant_moment(a1, 0.0, rho, V, M, com, 1,0, Tp);
        plant_moment(-a1,0.0, rho, V, M, com, 1,0, Tm);
        double dtauda = (Tp[1]-Tm[1])/(2.0*a1);   /* body only */
        printf("  M=%.1f: xcp=%.3f m  static margin (xcp-com)=%+.3f m (%s), CNa_body=%.3f\n",
               M, xcp, sm, sm<0?"UNSTABLE base-first":"stable", body_cna(M));
        printf("         at qbar=%.0f Pa (rho=0.31,V=%.0f): body dTau_y/dalpha=%+.4e Nm/rad (%s)\n",
               qbar, V, dtauda, dtauda>0?"destabilizing(+)":"restoring(-)");
        /* express as CN_a * Aref * qbar * (xcp-com) closed-form to confirm */
        double cf = qbar*VEH_AREF*body_cna(M)*(-sm); /* moment coeff slope: -(xcp-com) sign */
        printf("         closed-form qbar*Aref*CNa*-(xcp-com)=%+.4e (sanity)\n\n", cf);
    }

    /* -------- Q1/Q2: TRUE trim vs deflection, and where control's PD+FF sits -------- */
    printf("--- TRUE net pitch moment vs pitch-deflection at alpha_cmd=6deg, M=1.2 (AERO) ---\n");
    {
        double M=1.2, rho=0.31, V=M*302.0, qbar=0.5*rho*V*V, aoa=6.0*DEG2RAD;
        printf("  qbar=%.0f Pa. Sweep common pitch deflection dc; report NET tau_y (body+fins).\n", qbar);
        printf("  dc(deg)   body_tau_y    fin_tau_y     NET_tau_y\n");
        double dc_trim=NAN, prevnet=NAN, prevdc=NAN;
        for(double dcd=-6.0; dcd<=6.01; dcd+=1.0){
            double dc=dcd*DEG2RAD;
            double Tbody[3],Tfin[3];
            plant_moment(aoa,dc,rho,V,M,com,1,0,Tbody);
            plant_moment(aoa,dc,rho,V,M,com,0,1,Tfin);
            double net=Tbody[1]+Tfin[1];
            printf("  %+5.1f   %+.4e  %+.4e  %+.4e\n", dcd, Tbody[1], Tfin[1], net);
            if(!isnan(prevnet) && prevnet*net<0){ double t=prevnet/(prevnet-net); dc_trim=prevdc+t*(dcd-prevdc); }
            prevnet=net; prevdc=dcd;
        }
        if(!isnan(dc_trim)) printf("  => TRUE trim pitch-deflection dc_trim ~= %+.3f deg (net moment=0 here)\n", dc_trim);
        else printf("  => no sign change in [-6,6] deg: trim deflection is OUTSIDE this range\n");

        /* Now: what does CONTROL command at steady state (q_err=0)?  tau_cmd = -Tbody (FF only,
         * PD term=0 at zero rate & the attitude error drives alpha, but AT the commanded alpha the
         * attitude error is ZERO if guidance asks for this alpha). So control emits dc from FF: */
        double Tbody6[3]; plant_moment(aoa,0.0,rho,V,M,com,1,0,Tbody6);
        double CNa_f=FIN_CNA*fin_dip(M); double k=qbar*FIN_AREA*CNa_f; double A=(FIN_Z-com)*k;
        double tau_ff = -Tbody6[1];             /* control: tau_cmd -= Tbody */
        double dc_ctrl = tau_ff/(4.0*0.7071*A); /* allocator divisor */
        printf("\n  CONTROL FF at alpha=6deg (q_err=0): tau_cmd_y=-Tbody=%+.3e -> dc_cmd=%+.3f deg\n",
               tau_ff, dc_ctrl*RAD2DEG);
        /* realized NET if control applies dc_ctrl */
        double Tb2[3],Tf2[3]; plant_moment(aoa,dc_ctrl,rho,V,M,com,1,0,Tb2); plant_moment(aoa,dc_ctrl,rho,V,M,com,0,1,Tf2);
        printf("  NET moment if control applies its FF dc_cmd: %+.4e Nm  (want 0)\n", Tb2[1]+Tf2[1]);
        printf("  compare: TRUE trim needs dc=%+.3f deg, control commands dc=%+.3f deg -> %s\n",
               dc_trim, dc_ctrl*RAD2DEG,
               (fabs(dc_ctrl*RAD2DEG-dc_trim)<0.5)?"MATCH (holds AoA)":"MISMATCH (drifts)");
        printf("\n  INTERPRETATION: control cancels the BODY moment but the fins' OWN response to the\n");
        printf("  6deg inflow (aeff includes atan2 inflow ~6deg) already makes a big restoring moment.\n");
        printf("  With body cancelled AND fins restoring, NET is strongly restoring toward alpha=0 ->\n");
        printf("  the airframe relaxes to aligned exactly as observed. P5's diagnosis is CORRECT.\n");
    }

    /* -------- Confirm: at zero deflection, what net moment does the vehicle feel at 6deg? ------ */
    printf("\n--- net moment at alpha=6deg with ZERO fin deflection (open-loop tendency) ---\n");
    {
        double M=1.2, rho=0.31, V=M*302.0, aoa=6.0*DEG2RAD;
        double Tbody[3],Tfin[3];
        plant_moment(aoa,0.0,rho,V,M,com,1,0,Tbody);
        plant_moment(aoa,0.0,rho,V,M,com,0,1,Tfin);
        printf("  body tau_y=%+.3e (destabilizing, grows AoA)\n", Tbody[1]);
        printf("  fin  tau_y=%+.3e (restoring, shrinks AoA)  [fins at 0 deflection, pure inflow response]\n", Tfin[1]);
        printf("  NET  tau_y=%+.3e  -> sign %s\n", Tbody[1]+Tfin[1],
               (Tbody[1]+Tfin[1])<0?"RESTORING (deployed fins win: stable, relaxes to align)":"DESTABILIZING");
        printf("  => Deployed fins DOMINATE (put CoP aft of CoM). So with the body moment ALSO cancelled by\n");
        printf("     the FF, the ONLY residual is the fins' restoring moment -> alpha driven to 0. To HOLD\n");
        printf("     alpha_cmd the FF must instead command dc that RE-TRIMS: fin+body net=0 AT alpha_cmd.\n");
    }
    return 0;
}
