/* p5_cloop.c - closed-loop AoA-hold verification, single steering plane.
 * Reduced pitch model (alpha, q) driven by the EXACT plant body+fin+Cmq moment from
 * dynamics.c, WITH fin rate limit (20 deg/s) + deflection limit (20 deg) + transonic dip.
 * Modes:
 *   0 = baseline: cancel body moment (at current alpha) + PD only  (reproduces the drift-to-2deg)
 *   1 = P5 move-the-trim-point: exact delta_ff (plant truth) + PD damping
 *   2 = P5 realistic: delta_ff from CONTROLLER ESTIMATE (cn_scale,xcp_bias) + PD  (shows mismatch resid)
 *   3 = P5 + integral: mode-2 feedforward + PI + anti-windup  (nulls the mismatch residual)
 * C ONLY. Build: p1_build.cmd.
 */
#include <stdio.h>
#include <math.h>
#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232
#define PI 3.141592653589793
#define G0 9.80665
#define VEH_LEN 47.7
#define VEH_STAGE_LEN 41.2
#define VEH_DIA 3.66
#define VEH_RADIUS (VEH_DIA*0.5)
#define VEH_AREF 10.52
#define VEH_DRY 25600.0
#define VEH_DRY_COMZ 12.4
#define TANK_AREA 9.9
#define LOX_RHO 1220.0
#define RP1_RHO 833.0
#define LOX_BASE_Z 16.0
#define RP1_BASE_Z 1.6
#define FIN_Z 45.0
#define FIN_AREA 2.4
#define FIN_CNA 3.0
#define FIN_STALL (25.0*DEG2RAD)
#define FIN_CT_DELTA_FRAC 0.35
#define FIN_DEFL_MAX (20.0*DEG2RAD)
#define FIN_RATE (20.0*DEG2RAD)
#define BODY_CMQ_CDC 0.6

static const double AERO_M[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0};
static const double AERO_CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
static double tbl(const double*xs,const double*ys,int n,double x){
    if(x<=xs[0])return ys[0]; if(x>=xs[n-1])return ys[n-1];
    for(int i=0;i<n-1;i++) if(x<=xs[i+1]){double t=(x-xs[i])/(xs[i+1]-xs[i]);return ys[i]+t*(ys[i+1]-ys[i]);}
    return ys[n-1];
}
static double fin_dip(double M){
    if(M>0.8&&M<1.2)return 0.55; if(M>2.0)return 0.80;
    if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;
    if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8; return 1.0;
}
static double xcp_frac(double M,double alpha){
    double base=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));
    double amod=0.015*fmin(fabs(alpha)/(15.0*DEG2RAD),1.0);
    return base-amod;
}
static void mass_props(double m_lox,double m_rp1,double*com,double*Itr,double*m){
    if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
    const double rt2=TANK_AREA/PI, R2=VEH_RADIUS*VEH_RADIUS;
    double m_dry=VEH_DRY,z_dry=VEH_DRY_COMZ;
    double Itl_dry=m_dry*(6.0*R2+VEH_LEN*VEH_LEN)/12.0;
    double h_l=m_lox/(LOX_RHO*TANK_AREA), z_l=LOX_BASE_Z+0.5*h_l;
    double h_r=m_rp1/(RP1_RHO*TANK_AREA), z_r=RP1_BASE_Z+0.5*h_r;
    double Itl_l=m_lox*(3.0*rt2+h_l*h_l)/12.0, Itl_r=m_rp1*(3.0*rt2+h_r*h_r)/12.0;
    double M=m_dry+m_lox+m_rp1;
    double c=(m_dry*z_dry+m_lox*z_l+m_rp1*z_r)/M;
    double I=Itl_dry+m_dry*(z_dry-c)*(z_dry-c)+Itl_l+m_lox*(z_l-c)*(z_l-c)+Itl_r+m_rp1*(z_r-c)*(z_r-c);
    *com=c; *Itr=I; *m=M;
}
/* Full plant pitch moment about +Y for AoA=alpha, pitch-rate q, 4 explicit fin deflections.
 * Body normal-force moment + fin radial lift + fin tangential-cant (P3's 35% term) + Cmq. */
static double plant_moment_Y(double qbar,double M,double alpha,double q_rate,double com,
                             double speed,double rho,const double fin[4]){
    double ty=0.0;
    double CNa=tbl(AERO_M,AERO_CN,9,M);
    double Fn=qbar*VEH_AREF*(CNa*alpha);
    double xcp=xcp_frac(M,alpha)*VEH_STAGE_LEN;
    ty += (xcp-com)*(-Fn);
    double CNa_f=FIN_CNA*fin_dip(M);
    double vhx=sin(alpha), vhz=-cos(alpha);
    for(int i=0;i<4;i++){
        double phi=(45.0+90.0*i)*DEG2RAD;
        double er[3]={cos(phi),sin(phi),0.0}, et[3]={-sin(phi),cos(phi),0.0};
        double rmx=VEH_RADIUS*cos(phi), rmz=FIN_Z;
        double wxr_x=q_rate*rmz, wxr_z=-q_rate*rmx;      /* w=(0,q,0) x rm */
        double vix=speed*vhx+wxr_x, viy=0.0, viz=speed*vhz+wxr_z;
        double vsp=sqrt(vix*vix+viy*viy+viz*viz); if(vsp<1.0)continue;
        double qbi=0.5*rho*vsp*vsp;
        double w_ax=-viz, w_r=vix*er[0]+viy*er[1];
        double delta=fin[i];
        double alpha_i=delta+atan2(w_r,w_ax);
        double aeff=alpha_i; if(aeff>FIN_STALL)aeff=FIN_STALL; if(aeff<-FIN_STALL)aeff=-FIN_STALL;
        double L=qbi*FIN_AREA*CNa_f*aeff;
        double Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*delta;
        double Fx=-L*er[0]-Ft*et[0];
        double az=rmz-com, ax=rmx;
        ty += az*Fx;                                     /* Fz=0 */
        (void)ax; (void)et;
    }
    double zc=com,L2=VEH_LEN; double J=L2*L2*L2/3.0-zc*L2*L2+zc*zc*L2;
    double Cdamp=0.5*rho*speed*BODY_CMQ_CDC*VEH_DIA*J;
    ty -= Cdamp*q_rate;
    return ty;
}
/* exact trim (plant truth) via root find, yaw pattern amplitude. */
static double delta_ff_exact(double qbar,double M,double alpha_cmd,double com,double speed,double rho){
    double patY[4]={-1,1,1,-1};
    double lo=-25*DEG2RAD,hi=25*DEG2RAD, fl[4],fh[4];
    for(int i=0;i<4;i++){fl[i]=patY[i]*lo; fh[i]=patY[i]*hi;}
    double flo=plant_moment_Y(qbar,M,alpha_cmd,0,com,speed,rho,fl);
    double fhi=plant_moment_Y(qbar,M,alpha_cmd,0,com,speed,rho,fh);
    if(flo*fhi>0)return NAN;
    for(int it=0;it<60;it++){double mid=0.5*(lo+hi);double fm[4];for(int i=0;i<4;i++)fm[i]=patY[i]*mid;
        double f=plant_moment_Y(qbar,M,alpha_cmd,0,com,speed,rho,fm);if(flo*f<=0){hi=mid;fhi=f;}else{lo=mid;flo=f;}}
    return 0.5*(lo+hi);
}
/* controller ESTIMATE feedforward (closed-form, small-angle), cn_scale & xcp_bias = model error.
 * delta_ff = Tbody_est / (4*0.707*A_est) with the yaw-pattern convention of control.c. */
static double delta_ff_est(double qbar,double M,double alpha_cmd,double com,double cn_scale,double xcp_bias){
    double CNa=tbl(AERO_M,AERO_CN,9,M)*cn_scale;
    double xcp=xcp_frac(M,alpha_cmd)*VEH_STAGE_LEN + xcp_bias;
    double Fn=qbar*VEH_AREF*(CNa*alpha_cmd);
    double Tbody_est=(xcp-com)*(-Fn);
    double CNa_f=FIN_CNA*fin_dip(M);
    double A=(FIN_Z-com)*qbar*FIN_AREA*CNa_f;
    if(fabs(A)<1.0)return 0.0;
    return Tbody_est/(4.0*0.7071*A);
}

/* simulate one case. cn_scale/xcp_bias only used in modes 2,3. dist_deg = an AoA-equivalent
 * disturbance moment applied from t=3s (models a gust). Returns final alpha (deg). */
static double simulate(int mode,double M,double qbar,double alpha_cmd_deg,double cn_scale,double xcp_bias,
                       double dist_alpha_deg,int verbose){
    double m_target=42400.0, prop=m_target-VEH_DRY;
    double m_rp1=prop/(1.0+2.33), m_lox=prop-m_rp1;
    double com,Itr,mtot; mass_props(m_lox,m_rp1,&com,&Itr,&mtot);
    double a_snd=300.0, speed=M*a_snd, rho=2.0*qbar/(speed*speed);
    double alpha_cmd=alpha_cmd_deg*DEG2RAD;

    double wn=1.1, zeta=1.3;
    double Kp=Itr*wn*wn, Kd=Itr*2.0*zeta*wn;
    double zc=com,L2=VEH_LEN; double J=L2*L2*L2/3.0-zc*L2*L2+zc*zc*L2;
    double Cmq=0.5*rho*speed*BODY_CMQ_CDC*VEH_DIA*J;
    double Kd_use=Kd-Cmq; if(Kd_use<0.1*Kd)Kd_use=0.1*Kd;
    double CNa_f=FIN_CNA*fin_dip(M);
    double A=(FIN_Z-com)*qbar*FIN_AREA*CNa_f;
    double patY[4]={-1,1,1,-1};
    /* DEFLECTION-DOMAIN integrator: trims the residual AoA error directly on the fin trim amplitude.
     * Size Ki_d so a persistent 1 deg AoA error drives the trim amplitude at ~ (K_ff)*(1/tau_i) i.e.
     * closes the residual in tau_i ~ 1.5 s. K_ff ~ 0.73 (deg fin per deg AoA), so
     * Ki_d = K_ff / tau_i [ (rad fin)/(rad AoA * s) ]. */
    double tau_i=1.5;
    double Kff_nom=0.73;               /* from p5_trim.c linear gain */
    double Ki_d=Kff_nom/tau_i;         /* rad fin amplitude per (rad AoA-error * s) */

    double dff=0.0;
    if(mode==1){ dff=delta_ff_exact(qbar,M,alpha_cmd,com,speed,rho); if(isnan(dff))dff=0.0; }
    if(mode>=2){ dff=delta_ff_est(qbar,M,alpha_cmd,com,cn_scale,xcp_bias); }

    double alpha=0.0,q=0.0,fin[4]={0,0,0,0};
    double eint=0.0;                             /* integral of AoA error (rad*s) */
    double dt=0.002; int steps=(int)(6.0/dt);
    double dist_moment=0.0;
    for(int k=0;k<=steps;k++){
        double t=k*dt;
        dist_moment = (t>=3.0)? (dist_alpha_deg*DEG2RAD)*1.0e5 : 0.0;  /* 1e5 Nm/deg gust from t=3 */
        double aerr=alpha_cmd-alpha;
        /* PD torque command -> deflection via yaw-pattern unit gain */
        double tau_pd=Kp*aerr - Kd_use*q;
        double dpd=(fabs(A)>1.0)? tau_pd/(4.0*0.7071*A):0.0;
        /* deflection-domain trim: feedforward + integral (mode 3). eint is here the INTEGRAL of
         * AoA error [rad*s]; the integral trim amplitude = Ki_d*eint [rad fin]. */
        double dtrim = dff + ((mode==3)? Ki_d*eint : 0.0);
        double d_amp = dtrim + dpd;
        if(mode==0) d_amp=dpd;                   /* baseline: no feedforward, no integral */
        /* command deflections + clamp */
        double fin_cmd[4]; int sat=0;
        for(int i=0;i<4;i++){ double c=patY[i]*d_amp;
            if(c>FIN_DEFL_MAX){c=FIN_DEFL_MAX;sat=1;} if(c<-FIN_DEFL_MAX){c=-FIN_DEFL_MAX;sat=1;} fin_cmd[i]=c; }
        /* rate-limited actuation */
        int rate_sat=0;
        for(int i=0;i<4;i++){ double e=fin_cmd[i]-fin[i]; double rr=e/0.05;
            if(rr>FIN_RATE){rr=FIN_RATE;rate_sat=1;} if(rr<-FIN_RATE){rr=-FIN_RATE;rate_sat=1;} fin[i]+=rr*dt; }
        /* integral update with ANTI-WINDUP: stop integrating while deflection or rate saturated
         * (conditional integration / clamping method). */
        if(mode==3){
            if(!sat && !rate_sat) eint += aerr*dt;
            /* hard clamp the integral trim contribution to +-8 deg fin amplitude */
            double eint_max=(8.0*DEG2RAD)/Ki_d;
            if(eint> eint_max)eint=eint_max; if(eint<-eint_max)eint=-eint_max;
        }
        /* controller body-moment cancel (control.c does this in ALL fin modes). Uses estimate. */
        double CNa_c=tbl(AERO_M,AERO_CN,9,M)*((mode>=2)?cn_scale:1.0);
        double Fn_cur=qbar*VEH_AREF*(CNa_c*alpha);
        double xcp_c=xcp_frac(M,alpha)*VEH_STAGE_LEN + ((mode>=2)?xcp_bias:0.0);
        double tau_bodycancel=-((xcp_c-com)*(-Fn_cur));
        /* NOTE: in mode 1 (exact-trim demo) we ALSO cancel body (matches control.c). In a real
         * integration the feedforward REPLACES the need for cancel; here we keep cancel ON in all
         * modes to model control.c faithfully and prove the trim-ff adds the missing piece. */
        double tauY=plant_moment_Y(qbar,M,alpha,q,com,speed,rho,fin);
        tauY += tau_bodycancel + dist_moment;
        double qdot=tauY/Itr;
        q+=qdot*dt; alpha+=q*dt;
        if(verbose && (k%(int)(0.5/dt)==0))
            printf("  t=%.2f  alpha=%6.3f  q=%7.4f  fin_amp=%6.3f  eint=%.4f\n",
                   t,alpha*RAD2DEG,q*RAD2DEG,d_amp*RAD2DEG,eint*RAD2DEG);
    }
    return alpha*RAD2DEG;
}

int main(void){
    struct FC{double M,qbar;} fcs[]={{0.7,30000},{0.9,45000},{1.05,50000},{1.2,50000},{1.5,45000},{2.0,35000}};
    int nf=6;

    printf("=== AoA-HOLD, alpha_cmd=6deg, no model error, no disturbance ===\n");
    printf("%-6s %-9s %-12s %-12s\n","M","qbar","baseline","P5_exact");
    for(int i=0;i<nf;i++){
        double b=simulate(0,fcs[i].M,fcs[i].qbar,6,1,0,0,0);
        double p=simulate(1,fcs[i].M,fcs[i].qbar,6,1,0,0,0);
        printf("%-6.2f %-9.0f %-12.3f %-12.3f\n",fcs[i].M,fcs[i].qbar,b,p);
    }

    printf("\n=== MODEL-MISMATCH: controller CNa 15%% high + CoP est 0.3m aft (typ ctrl-vs-plant error) ===\n");
    printf("alpha_cmd=6deg. Shows feedforward residual, and integral nulling it.\n");
    printf("%-6s %-9s %-16s %-16s\n","M","qbar","P5_est(no I)","P5_est+I");
    for(int i=0;i<nf;i++){
        double p2=simulate(2,fcs[i].M,fcs[i].qbar,6,1.15,0.3,0,0);
        double p3=simulate(3,fcs[i].M,fcs[i].qbar,6,1.15,0.3,0,0);
        printf("%-6.2f %-9.0f %-16.3f %-16.3f\n",fcs[i].M,fcs[i].qbar,p2,p3);
    }

    printf("\n=== DISTURBANCE REJECTION: gust moment (=~3deg body moment) applied from t=3s ===\n");
    printf("alpha_cmd=6deg, exact feedforward. Final alpha shows offset w/o I; +I rejects it.\n");
    printf("%-6s %-9s %-16s %-16s\n","M","qbar","P5_exact+gust","P5+I+gust");
    for(int i=0;i<nf;i++){
        double pg=simulate(1,fcs[i].M,fcs[i].qbar,6,1,0,3.0,0);
        double pgi=simulate(3,fcs[i].M,fcs[i].qbar,6,1.0,0.0,3.0,0);
        printf("%-6.2f %-9.0f %-16.3f %-16.3f\n",fcs[i].M,fcs[i].qbar,pg,pgi);
    }

    printf("\n=== TRACE: P5_est+I at M=1.05 (transonic dip, 15%% CNa error), alpha_cmd=6 ===\n");
    simulate(3,1.05,50000,6,1.15,0.3,0,1);
    return 0;
}
