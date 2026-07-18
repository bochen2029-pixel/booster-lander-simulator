/* p5_trim.c - AoA-hold trim derivation, mirrors core/dynamics.c plant EXACTLY.
 * Computes: static margin (x_cp-com), body pitch stiffness dTau/dalpha, the fin
 * deflection delta_trim(alpha_cmd) that zeros total pitch moment, and the max
 * holdable AoA under the +-20deg deflection limit & transonic dip.
 * C ONLY (house rule). Build with p1_build.cmd or vcvars64 + cl /O2 /fp:precise.
 */
#include <stdio.h>
#include <math.h>

#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232
#define PI      3.141592653589793

/* ---- constants copied from core/constants.h (source of truth) ---- */
#define G0 9.80665
#define VEH_LEN 47.7
#define VEH_STAGE_LEN 41.2
#define VEH_DIA 3.66
#define VEH_RADIUS (VEH_DIA*0.5)
#define VEH_AREF 10.52
#define VEH_DRY 25600.0
#define VEH_DRY_COMZ 12.4
#define LOX_MAX 287400.0
#define RP1_MAX 123500.0
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
#define FIN_DEFL_MAX (20.0*DEG2RAD)
#define BODY_CMQ_CDC 0.6

/* ---- plant aero tables (dynamics.c) ---- */
static const double AERO_M[9]  = {0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0};
static const double AERO_CA[9] = {0.85,0.88,1.10,1.40,1.25,1.10,0.95,0.92,0.90};
static const double AERO_CN[9] = {2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
static double tbl(const double*xs,const double*ys,int n,double x){
    if(x<=xs[0])return ys[0]; if(x>=xs[n-1])return ys[n-1];
    for(int i=0;i<n-1;i++) if(x<=xs[i+1]){double t=(x-xs[i])/(xs[i+1]-xs[i]);return ys[i]+t*(ys[i+1]-ys[i]);}
    return ys[n-1];
}
static double fin_dip(double M){
    if(M>0.8&&M<1.2)return 0.55;
    if(M>2.0)return 0.80;
    if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;
    if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;
    return 1.0;
}
static double xcp_frac(double M,double alpha){
    double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));
    double amod=0.015*fmin(fabs(alpha)/(15.0*DEG2RAD),1.0);
    return base-amod;
}
/* mass_props: return com and I_tr (dynamics.c mass_props) */
static void mass_props(double m_lox,double m_rp1,double*com_out,double*Itr_out,double*m_out){
    if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
    const double rt2=TANK_AREA/PI, R2=VEH_RADIUS*VEH_RADIUS;
    double m_dry=VEH_DRY,z_dry=VEH_DRY_COMZ;
    double Itl_dry=m_dry*(6.0*R2+VEH_LEN*VEH_LEN)/12.0;
    double h_l=m_lox/(LOX_RHO*TANK_AREA), z_l=LOX_BASE_Z+0.5*h_l;
    double h_r=m_rp1/(RP1_RHO*TANK_AREA), z_r=RP1_BASE_Z+0.5*h_r;
    double Itl_l=m_lox*(3.0*rt2+h_l*h_l)/12.0;
    double Itl_r=m_rp1*(3.0*rt2+h_r*h_r)/12.0;
    double M=m_dry+m_lox+m_rp1;
    double com=(m_dry*z_dry+m_lox*z_l+m_rp1*z_r)/M;
    double I_tr=Itl_dry+m_dry*(z_dry-com)*(z_dry-com)
              +Itl_l+m_lox*(z_l-com)*(z_l-com)
              +Itl_r+m_rp1*(z_r-com)*(z_r-com);
    *com_out=com; *Itr_out=I_tr; *m_out=M;
}

/* globals for mass state (set in main per case) */
double m_lox_g, m_rp1_g;

/* ---- fin pitch moment about CoM for a symmetric "pitch pattern" deflection delta_p ----
 * dynamics.c fin model: each fin radial lift L_i = qbar_i*S_f*CNa_f*aeff_i, aeff_i=delta_i+incidence.
 * Force = -L_i*e_r_i (radial). Torque about CoM = arm_i x F_i, arm_i=(R cosphi,R sinphi,FIN_Z-com).
 * Agent A / control.c: pitch pattern patP=[1,1,-1,-1] about body X gives (dynamics-matched)
 *   tau_x = A*0.7071*(d1+d2-d3-d4),  A=(FIN_Z-com)*qbar*S_f*CNa_f.
 * For a pure symmetric pitch deflection scaled by patP with amplitude delta_p (each |d_i|=delta_p):
 *   tau_x_fin = A*0.7071*(4*delta_p) = 4*0.7071*A*delta_p.
 * We also fold in the fin's OWN incidence response to alpha (the fins see body alpha too):
 * a body AoA alpha presents each fin an incidence ~ alpha*(projection), producing a restoring
 * fin moment. We compute the full nonlinear fin moment numerically below for accuracy. */

/* Full nonlinear fin pitch moment about CoM, plane = body X-Z (alpha tilts about +Y so the
 * relative wind lateral component is along +X in body). Deflections applied as pitch pattern
 * patP=[1,1,-1,-1]*delta_p. Steady (omega=0). Returns tau_y? We keep everything in the single
 * steering plane and return the scalar pitch moment that adds to Tbody. */
static double Tfin_nonlin(double qbar,double M,double alpha,double delta_p,double com,double speed,double rho){
    double CNa_f=FIN_CNA*fin_dip(M);
    /* relative wind unit in body: base-first => large -Z; lateral comp from alpha in +X plane.
     * vhat_b = (sin alpha, 0, -cos alpha) (AoA measured off -Z). AoA is a tilt about +Y, so the
     * airframe must be trimmed by a moment about +Y. In control.c's mapping the +Y ("yaw")
     * channel uses pattern patY=[1,-1,-1,1] (tau_y=-A*0.707*(d1-d2-d3+d4)). We apply delta_p
     * with patY so the fin deflection produces a MOMENT ABOUT +Y that opposes the body moment. */
    double vhx=sin(alpha), vhz=-cos(alpha);
    double patY[4]={1,-1,-1,1};
    double tau=0.0;
    for(int i=0;i<4;i++){
        double phi=(45.0+90.0*i)*DEG2RAD;
        double er[3]={cos(phi),sin(phi),0.0};
        /* local flow (no omega): vi = speed*vhat_b */
        double vix=speed*vhx, viy=0.0, viz=speed*vhz;
        double vsp=sqrt(vix*vix+viy*viy+viz*viz); if(vsp<1.0)continue;
        double qbi=0.5*rho*vsp*vsp;
        double w_ax=-viz;                       /* -Z body component */
        double w_r = vix*er[0]+viy*er[1];       /* radial comp */
        double delta=patY[i]*delta_p;
        double alpha_i=delta+atan2(w_r,w_ax);
        double aeff=alpha_i; if(aeff>FIN_STALL)aeff=FIN_STALL; if(aeff<-FIN_STALL)aeff=-FIN_STALL;
        double L=qbi*FIN_AREA*CNa_f*aeff;
        /* force radial: -L*er (x,y); torque about CoM = arm x F. arm=(R cos,R sin,FIN_Z-com) */
        double Fx=-L*er[0], Fy=-L*er[1], Fz=0.0;
        double ax=VEH_RADIUS*cos(phi), ay=VEH_RADIUS*sin(phi), az=FIN_Z-com;
        /* torque about +Y = az*Fx - ax*Fz = az*Fx  (Fz=0 here) */
        double ty=az*Fx-ax*Fz;
        tau+=ty;
    }
    return tau; /* Nm about +Y */
}
/* Body pitch moment about +Y consistent with the same convention as Tfin_nonlin.
 * body normal force Fn=qbar*Aref*CN, CN=CNa*alpha, force lateral = -Fn * xhat (opposes +X flow),
 * applied at xcp. torque_y = (xcp-com)*Fx_body = (xcp-com)*(-Fn). */
static double Tbody_nonlin(double qbar,double M,double alpha,double com){
    double CNa=tbl(AERO_M,AERO_CN,9,M);
    double CN=CNa*alpha;
    double Fn=qbar*VEH_AREF*CN;           /* magnitude */
    double Fx=-Fn;                         /* lateral force along -X (opposes +X flow comp) */
    double xcp=xcp_frac(M,alpha)*VEH_STAGE_LEN;
    double az=xcp-com;
    double ty=az*Fx;                       /* torque about +Y */
    return ty;
}

int main(void){
    /* representative aero-descent mass: near-dry + small residuals (booster is light in aero
     * descent; most prop already used in entry burn). Use landing-ish mass. */
    /* Agent A used m=42.4 t for aero descent. Reconstruct residual props giving ~42.4t. */
    double m_target=42400.0;
    /* dry=25600; need ~16800 kg prop at MR 2.33 */
    double prop=m_target-VEH_DRY;
    m_rp1_g=prop/(1.0+2.33); m_lox_g=prop-m_rp1_g;
    double com,Itr,mtot; mass_props(m_lox_g,m_rp1_g,&com,&Itr,&mtot);
    printf("=== MASS STATE (aero-descent) ===\n");
    printf("m_lox=%.0f m_rp1=%.0f  m=%.1f t  com=%.3f m  I_tr=%.4e kg m^2\n\n",m_lox_g,m_rp1_g,mtot/1000.0,com,Itr);

    /* representative flight conditions: (M, qbar, rho, speed). qbar<=80kPa. Use canon-ish. */
    struct FC{double M,qbar;} fcs[]={
        {0.7,30000},{0.9,45000},{1.05,50000},{1.2,50000},{1.5,45000},{2.0,35000},{3.0,20000}
    };
    int nf=(int)(sizeof(fcs)/sizeof(fcs[0]));

    printf("=== STATIC MARGIN & BODY PITCH STIFFNESS (about CoM) ===\n");
    printf("%-6s %-9s %-9s %-10s %-12s %-14s\n","M","qbar","x_cp[m]","SM=xcp-com","CNa","dTb/dalpha[Nm/rad]");
    for(int i=0;i<nf;i++){
        double M=fcs[i].M,qbar=fcs[i].qbar;
        double xcp=xcp_frac(M,0.0)*VEH_STAGE_LEN;
        double CNa=tbl(AERO_M,AERO_CN,9,M);
        double kbody=qbar*VEH_AREF*CNa*(xcp-com); /* signed: <0 = unstable */
        printf("%-6.2f %-9.0f %-9.3f %-10.3f %-12.3f %-14.4e\n",M,qbar,xcp,xcp-com,CNa,kbody);
    }
    printf("(SM<0 => CoP ahead of CoM in base-first => bare body UNSTABLE, per D-005.)\n\n");

    /* ---- TRIM: find delta_p that zeros total pitch moment at alpha_cmd ----
     * Total(alpha,delta_p) = Tbody_nonlin(alpha) + Tfin_nonlin(alpha,delta_p) = 0.
     * Solve for delta_p by bisection (monotone in delta_p). Report per (M, alpha_cmd). */
    double alphas_deg[]={2.0,4.0,6.0,8.0,10.0,12.0};
    int na=(int)(sizeof(alphas_deg)/sizeof(alphas_deg[0]));
    printf("=== TRIM DEFLECTION delta_trim(alpha_cmd) [deg] that zeros total pitch moment ===\n");
    printf("(NaN/--- = cannot trim: fin saturates at 20deg or stalls; that AoA is UNHOLDABLE)\n");
    printf("%-6s","M\\aoa");
    for(int a=0;a<na;a++) printf(" a=%-6.0f",alphas_deg[a]);
    printf("\n");
    for(int i=0;i<nf;i++){
        double M=fcs[i].M,qbar=fcs[i].qbar;
        double rho=2.0*qbar; /* rho*speed^2 = 2qbar; need speed to split. Use M and a std a? */
        /* we need rho and speed separately for qbar_i. Reconstruct from qbar and a nominal a.
         * a (speed of sound) ~ 300 m/s in low strat. speed=M*a. rho=2*qbar/speed^2. */
        double a_snd=300.0; double speed=M*a_snd; rho=2.0*qbar/(speed*speed);
        printf("%-6.2f",M);
        for(int a=0;a<na;a++){
            double alpha=alphas_deg[a]*DEG2RAD;
            /* bisection on delta_p in [-25,25] deg */
            double lo=-25*DEG2RAD, hi=25*DEG2RAD;
            double flo=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,lo,com,speed,rho);
            double fhi=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,hi,com,speed,rho);
            if(flo*fhi>0){ printf("   ---- "); continue; } /* no sign change: unholdable */
            for(int it=0;it<60;it++){
                double mid=0.5*(lo+hi);
                double fm=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,mid,com,speed,rho);
                if(flo*fm<=0){hi=mid;fhi=fm;}else{lo=mid;flo=fm;}
            }
            double dtrim=0.5*(lo+hi)*RAD2DEG;
            if(fabs(dtrim)>20.0) printf("  >20!  "); /* exceeds deflection limit */
            else printf(" %6.2f ",dtrim);
        }
        printf("\n");
    }

    /* ---- LINEARIZED feedforward gain: delta_ff = K_ff(M) * alpha_cmd ----
     * From small-angle: Tbody ~ kb*alpha (kb=qbar*Aref*CNa*(xcp-com)),
     * Tfin ~ -Kfin_delta*delta_p - Kfin_alpha*alpha (fins respond to both).
     * Set total=0: delta_ff = -(kb - Kfin_alpha)/Kfin_delta * alpha  (signs from numeric slopes).
     * Report the linear slope delta_ff/alpha (deg/deg) from a small-alpha finite difference. */
    printf("\n=== LINEAR FEEDFORWARD GAIN  K_ff = d(delta_trim)/d(alpha_cmd)  [deg fin per deg AoA] ===\n");
    printf("%-6s %-14s %-14s %-14s\n","M","K_ff[deg/deg]","Kfin_d[Nm/rad]","kbody[Nm/rad]");
    for(int i=0;i<nf;i++){
        double M=fcs[i].M,qbar=fcs[i].qbar;
        double a_snd=300.0; double speed=M*a_snd; double rho=2.0*qbar/(speed*speed);
        double da=0.5*DEG2RAD;
        /* trim at +da and -da, slope */
        double dt[2]; double al2[2]={da,-da};
        int ok=1;
        for(int s=0;s<2;s++){
            double alpha=al2[s];
            double lo=-25*DEG2RAD,hi=25*DEG2RAD;
            double flo=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,lo,com,speed,rho);
            double fhi=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,hi,com,speed,rho);
            if(flo*fhi>0){ok=0;break;}
            for(int it=0;it<60;it++){double mid=0.5*(lo+hi);double fm=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,mid,com,speed,rho);if(flo*fm<=0){hi=mid;fhi=fm;}else{lo=mid;flo=fm;}}
            dt[s]=0.5*(lo+hi);
        }
        /* Kfin_delta: slope of Tfin vs delta_p at alpha=0 */
        double d1=1.0*DEG2RAD;
        double Tf1=Tfin_nonlin(qbar,M,0.0,d1,com,speed,rho);
        double Tf0=Tfin_nonlin(qbar,M,0.0,0.0,com,speed,rho);
        double Kfin_d=(Tf1-Tf0)/d1; /* Nm per rad of delta_p */
        double CNa=tbl(AERO_M,AERO_CN,9,M);
        double xcp=xcp_frac(M,0.0)*VEH_STAGE_LEN;
        double kbody=qbar*VEH_AREF*CNa*(xcp-com);
        if(ok){
            double Kff=(dt[0]-dt[1])/(da-(-da)); /* rad fin per rad aoa */
            printf("%-6.2f %-14.3f %-14.4e %-14.4e\n",M,Kff,Kfin_d,kbody);
        } else {
            printf("%-6.2f %-14s %-14.4e %-14.4e\n",M,"(unholdable)",Kfin_d,kbody);
        }
    }

    /* ---- RATE-LIMIT time constant: how long to slew delta_ff at 20 deg/s ---- */
    printf("\n=== FIN SLEW TIME to reach delta_trim(6deg) at 20 deg/s ===\n");
    for(int i=0;i<nf;i++){
        double M=fcs[i].M,qbar=fcs[i].qbar;
        double a_snd=300.0; double speed=M*a_snd; double rho=2.0*qbar/(speed*speed);
        double alpha=6.0*DEG2RAD;
        double lo=-25*DEG2RAD,hi=25*DEG2RAD;
        double flo=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,lo,com,speed,rho);
        double fhi=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,hi,com,speed,rho);
        if(flo*fhi>0){printf("M=%.2f: 6deg UNHOLDABLE\n",M);continue;}
        for(int it=0;it<60;it++){double mid=0.5*(lo+hi);double fm=Tbody_nonlin(qbar,M,alpha,com)+Tfin_nonlin(qbar,M,alpha,mid,com,speed,rho);if(flo*fm<=0){hi=mid;fhi=fm;}else{lo=mid;flo=fm;}}
        double dtrim=0.5*(lo+hi)*RAD2DEG;
        printf("M=%.2f: delta_trim=%.2f deg -> slew time = %.3f s\n",M,dtrim,fabs(dtrim)/20.0);
    }
    return 0;
}
