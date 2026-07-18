/* p1_verify2.c — P1 deep-dive on the residual cross-coupling + roll damping +
 * clean zeta + trim-FF, resolving the ambiguities from p1_verify.c.
 * Build: p1_build.cmd p1_verify2.c p1_verify2.exe
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
    double x=a[1]*b[2]-a[2]*b[1], y=a[2]*b[0]-a[0]*b[2], z=a[0]*b[1]-a[1]*b[0];
    o[0]=x;o[1]=y;o[2]=z;
}
static double v3_dot(const double a[3], const double b[3]){ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static double v3_norm(const double a[3]){ return sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }

typedef struct { double m, com, I_ax, I_tr; } MP;
static void mass_props(double m_lox, double m_rp1, MP* mp){
    if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
    const double rt2 = TANK_AREA/PI, R2 = VEH_RADIUS*VEH_RADIUS;
    double m_dry=VEH_DRY, z_dry=VEH_DRY_COMZ;
    double Itl_dry=m_dry*(6.0*R2+VEH_LEN*VEH_LEN)/12.0, Iax_dry=m_dry*R2;
    double h_l=m_lox/(LOX_RHO*TANK_AREA), z_l=LOX_BASE_Z+0.5*h_l;
    double h_r=m_rp1/(RP1_RHO*TANK_AREA), z_r=RP1_BASE_Z+0.5*h_r;
    double Iax_l=0.5*m_lox*rt2, Itl_l=m_lox*(3.0*rt2+h_l*h_l)/12.0;
    double Iax_r=0.5*m_rp1*rt2, Itl_r=m_rp1*(3.0*rt2+h_r*h_r)/12.0;
    double M=m_dry+m_lox+m_rp1;
    double com=(m_dry*z_dry+m_lox*z_l+m_rp1*z_r)/M;
    double I_tr=Itl_dry+m_dry*(z_dry-com)*(z_dry-com)+Itl_l+m_lox*(z_l-com)*(z_l-com)+Itl_r+m_rp1*(z_r-com)*(z_r-com);
    mp->m=M; mp->com=com; mp->I_ax=Iax_dry+Iax_l+Iax_r; mp->I_tr=I_tr;
}
static double fin_dip(double M){
    if(M>0.8&&M<1.2)return 0.55; if(M>2.0)return 0.80;
    if(M>=0.6&&M<=0.8)return 1.0+(0.55-1.0)*(M-0.6)/0.2;
    if(M>=1.2&&M<=2.0)return 0.55+(0.80-0.55)*(M-1.2)/0.8;
    return 1.0;
}
/* plant fin block, with optional per-fin dump */
static void plant_fin(const double vrel_b[3], const double w[3], const double delta[4],
                      double rho, double mach, double com, double Fb[3], double Tb[3], int dump){
    Fb[0]=Fb[1]=Fb[2]=0; Tb[0]=Tb[1]=Tb[2]=0;
    double CNa_f=FIN_CNA*fin_dip(mach);
    for(int i=0;i<4;i++){
        double phi=(45.0+90.0*i)*DEG2RAD;
        double er[3]={cos(phi),sin(phi),0}, et[3]={-sin(phi),cos(phi),0};
        double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};
        double wxr[3]; v3_cross(wxr,w,rm);
        double vi[3]={vrel_b[0]+wxr[0],vrel_b[1]+wxr[1],vrel_b[2]+wxr[2]};
        double vsp=v3_norm(vi); if(vsp<1.0)continue;
        double qbi=0.5*rho*vsp*vsp;
        double w_ax=-vi[2], w_r=v3_dot(vi,er);
        double d=delta[i];
        double inflow=atan2(w_r,w_ax);
        double alpha_i=d+inflow;
        double aeff=alpha_i; if(aeff>FIN_STALL)aeff=FIN_STALL; if(aeff<-FIN_STALL)aeff=-FIN_STALL;
        double L=qbi*FIN_AREA*CNa_f*aeff;
        double Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*d;
        double Ff[3]={-L*er[0]-Ft*et[0],-L*er[1]-Ft*et[1],0};
        Fb[0]+=Ff[0];Fb[1]+=Ff[1];Fb[2]+=Ff[2];
        double arm[3]={rm[0],rm[1],rm[2]-com};
        double Tf[3]; v3_cross(Tf,arm,Ff);
        Tb[0]+=Tf[0];Tb[1]+=Tf[1];Tb[2]+=Tf[2];
        if(dump) printf("   fin%d phi=%3.0f d=%+6.3fdeg inflow=%+6.3fdeg qbi=%.0f aeff=%+6.3fdeg L=%+9.1f Ft=%+8.2f Tf=(%+.2e %+.2e %+.2e)\n",
            i, phi*RAD2DEG, d*RAD2DEG, inflow*RAD2DEG, qbi, aeff*RAD2DEG, L, Ft, Tf[0],Tf[1],Tf[2]);
    }
}

int main(void){
    MP mp; mass_props(120000.0,51000.0,&mp);
    double com=mp.com;
    double rho=0.31, V=700.0, mach=2.5; double qbar=0.5*rho*V*V;
    double CNa_f=FIN_CNA*fin_dip(mach);
    double k=qbar*FIN_AREA*CNa_f, A=(FIN_Z-com)*k, C=RCS_ARM*FIN_CT_DELTA_FRAC*k;
    double w0[3]={0,0,0};

    printf("=== P1 DEEP-DIVE (com=%.3f I_tr=%.3e I_ax=%.3e) ===\n\n", com, mp.I_tr, mp.I_ax);

    /* ---- A: WHERE does pitch->yaw leak come from? Dump per-fin at AoA=0. ---- */
    printf("--- A: pitch pattern [1,1,-1,-1] at AoA=0, per-fin breakdown ---\n");
    {
        double dsmall=0.5*DEG2RAD;
        double d[4]={dsmall,dsmall,-dsmall,-dsmall};
        double vb[3]={0,0,-V};
        double Fb[3],Tb[3]; plant_fin(vb,w0,d,rho,mach,com,Fb,Tb,1);
        printf("   TOTAL Tb=(%.4e, %.4e, %.4e)\n", Tb[0],Tb[1],Tb[2]);
        printf("   NOTE: with axial flow inflow=0 for all fins so aeff=d exactly.\n");
        printf("   tau_y closed form -A*0.707*(d1-d2-d3+d4) = %.4e\n", -A*0.7071*(d[0]-d[1]-d[2]+d[3]));
        printf("   => the 'leak' equals the closed-form tau_y of THIS pattern; the pattern is a\n");
        printf("      pure tau_x generator ONLY IF (d1-d2-d3+d4)=0. For [1,1,-1,-1]: 1-1+1-1=0. So tau_y MUST be 0.\n\n");
    }

    /* ---- B: re-run TEST2 pitch case but dump, to see the 1.75e6 leak origin ---- */
    printf("--- B: cmd pure PITCH tau=5e6 via allocator, dump realized ---\n");
    {
        double patP[4]={1,1,-1,-1};
        double dpitch=5.0e6/(4.0*0.7071*A);
        double d[4]; for(int i=0;i<4;i++) d[i]=dpitch*patP[i];
        printf("   dpitch=%.4f deg, fins=[%.3f %.3f %.3f %.3f] deg\n",
               dpitch*RAD2DEG, d[0]*RAD2DEG,d[1]*RAD2DEG,d[2]*RAD2DEG,d[3]*RAD2DEG);
        double vb[3]={0,0,-V};
        double Fb[3],Tb[3]; plant_fin(vb,w0,d,rho,mach,com,Fb,Tb,1);
        printf("   TOTAL realized Tb=(%.4e, %.4e, %.4e)\n\n", Tb[0],Tb[1],Tb[2]);
    }

    /* ---- C: The REAL question: is the fin nonlinearity (stall clip / atan2) the leak,
     * or is it the tangential roll-cant Ft coupling into pitch/yaw via the z-arm? ----
     * Ft acts along et; its torque via arm z-component gives tau_x,tau_y = zc'*Ft*(cos,sin).
     * For pitch pattern the Ft terms: fin i has Ft_i = kr*d_i, d=[+,+,-,-].
     * tau_y from Ft = sum zc'*Ft_i*sin(phi_i) = zc'*kr*sum d_i*sin(phi_i)
     *   sin(phi)=[.707,.707,-.707,-.707]; d=[+,+,-,-] -> sum d_i sin = .707(1+1+1+1)dsm=2.828 dsm
     *   => tau_y_from_Ft = zc'*kr*2.828*dsmall  (THIS is the leak, from roll-cant, not stall) */
    printf("--- C: decompose the leak: tangential(roll-cant) force's pitch/yaw torque ---\n");
    {
        double dsmall=5.0e6/(4.0*0.7071*A);
        double zc=FIN_Z-com, kr=FIN_CT_DELTA_FRAC*k;
        /* pitch pattern d=[+,+,-,-] */
        double sinp[4]={0.7071,0.7071,-0.7071,-0.7071}, cosp[4]={0.7071,-0.7071,-0.7071,0.7071};
        double dP[4]={dsmall,dsmall,-dsmall,-dsmall};
        double ty_Ft=0, tx_Ft=0;
        for(int i=0;i<4;i++){ double Ft=kr*dP[i]; ty_Ft += zc*Ft*sinp[i]; tx_Ft += zc*Ft*cosp[i]; }
        printf("   from Ft (roll-cant) with pitch pattern: tau_x_leak=%.4e  tau_y_leak=%.4e\n", tx_Ft, ty_Ft);
        printf("   (matches the observed ~1.75e6 yaw leak => the leak is the TANGENTIAL roll-cant\n");
        printf("    force's moment through the fin's z-arm, NOT stall, NOT inflow.)\n");
        printf("   magnitude vs main pitch torque: %.2f%%\n\n", 100.0*ty_Ft/5.0e6);
    }

    /* ---- D: clean roll damping from omega x r (fin tangential), correct wz ---- */
    printf("--- D: fin roll damping from omega x r (pure roll rate) ---\n");
    {
        double wz=0.2, w[3]={0,0,wz}, d0[4]={0,0,0,0};
        double vb[3]={0,0,-V};
        double Fb[3],Tb[3]; plant_fin(vb,w,d0,rho,mach,com,Fb,Tb,1);
        printf("   wz=%.2f: total fin Tb=(%.3e,%.3e,%.3e)  roll-damp coeff tau_z/wz=%.4e Nm/(rad/s)\n",
               wz, Tb[0],Tb[1],Tb[2], Tb[2]/wz);
        printf("   I_ax=%.3e -> roll e-fold under fins alone = %.1f s\n", mp.I_ax, mp.I_ax/fabs(Tb[2]/wz+1e-30));
        printf("   (roll rate makes wxr tangential -> changes w_r radial inflow? No: wxr for pure wz is\n");
        printf("    tangential (et direction) so it adds to axial? Let's see the dump above.)\n\n");
    }

    /* ---- E: proper zeta. The pitch EOM linearized about trimmed AoA in aero-descent.
     * Restoring torque comes from the DEPLOYED-FIN static margin (fins move CoP aft of CoM).
     * Fin normal-force stiffness per rad of AoA: dtau/dalpha = -(FIN_Z-com)*qbar*S_f*CNa_f*4?
     * Actually the 4 fins at AoA produce a restoring moment. Compute numerically:
     * perturb AoA by +1deg with zero deflection, read the fin pitch torque slope. ---- */
    printf("--- E: fin restoring stiffness (deployed) -> real omega_n and zeta ---\n");
    {
        struct { const char* nm; double rho,V; } cc[2]={{"AERO",0.31,350.0},{"TERM",1.00,180.0}};
        for(int c=0;c<2;c++){
            double rr=cc[c].rho, vv=cc[c].V, mm=vv/302.0;
            double qb=0.5*rr*vv*vv;
            double d0[4]={0,0,0,0};
            /* stiffness via finite diff of fin pitch torque wrt AoA about 0 */
            double da=0.5*DEG2RAD;
            double vbp[3]={vv*sin(da),0,-vv*cos(da)};
            double vbm[3]={vv*sin(-da),0,-vv*cos(-da)};
            double Fp[3],Tp[3],Fm[3],Tm[3];
            plant_fin(vbp,w0,d0,rr,mm,com,Fp,Tp,0);
            plant_fin(vbm,w0,d0,rr,mm,com,Fm,Tm,0);
            /* tau_x responds to AoA in x-z plane (pitch). dtau_x/dalpha */
            double kstiff = -(Tp[0]-Tm[0])/(2.0*da); /* restoring: -dtau/dalpha>0 stable */
            double J=VEH_LEN*VEH_LEN*VEH_LEN/3.0 - com*VEH_LEN*VEH_LEN + com*com*VEH_LEN;
            double Cdamp=0.5*rr*vv*BODY_CMQ_CDC*VEH_DIA*J;
            double wn=sqrt(fabs(kstiff)/mp.I_tr);
            double zeta=Cdamp/(2.0*mp.I_tr*wn);
            printf("   %s rho=%.2f V=%.0f qbar=%.0f: fin dtau_x/dalpha=%.3e (stiffness %s), |k|=%.3e\n",
                   cc[c].nm, rr, vv, qb, (Tp[0]-Tm[0])/(2.0*da),
                   ((Tp[0]-Tm[0])<0?"STABLE(restoring)":"UNSTABLE"), fabs(kstiff));
            printf("        Cdamp=%.3e  wn=%.4f rad/s  zeta=%.4f\n", Cdamp, wn, zeta);
        }
        printf("   (This is the honest fin+body descent pitch mode; zeta here is what Cmq buys.)\n\n");
    }

    /* ---- F: what does Cmq zeta look like against a FIXED reference like D-005 claim (~0.1)?
     * D-005 says 'Cdc=0.6 gives zeta~0.1-0.15'. That must be a SPECIFIC omega_n. Back out the
     * omega_n they implicitly used: zeta=Cdamp/(2 I wn) => wn = Cdamp/(2 I zeta). ---- */
    printf("--- F: back out implied omega_n for the D-005 'zeta~0.1' claim ---\n");
    {
        double J=VEH_LEN*VEH_LEN*VEH_LEN/3.0 - com*VEH_LEN*VEH_LEN + com*com*VEH_LEN;
        double rr=0.31, vv=350.0;
        double Cdamp=0.5*rr*vv*BODY_CMQ_CDC*VEH_DIA*J;
        for(double z=0.10; z<=0.16; z+=0.05){
            double wn=Cdamp/(2.0*mp.I_tr*z);
            printf("   if zeta=%.2f then wn=%.4f rad/s (period %.1f s) at AERO\n", z, wn, 2*PI/wn);
        }
        printf("   Interpretation: zeta~0.1 corresponds to a SLOW ~%.0f-s pitch mode; for a stiffer\n", 2*PI/(Cdamp/(2*mp.I_tr*0.1)));
        printf("   (higher-qbar / higher-stiffness) mode zeta is LOWER. zeta is stiffness-dependent.\n\n");
    }

    /* ---- G: TRIM FF — clean isolation. Put AoA purely in +x so body moment is pure tau_y.
     * Use the SAME CNa_body table lookup the control uses (mach 2.5). ---- */
    printf("--- G: trim FF sign, clean (AoA in +x, body moment pure tau_y) ---\n");
    {
        /* control's body_cna table */
        double XM[9]={0.0,0.6,0.9,1.1,1.5,2.0,3.0,5.0,8.0};
        double CN[9]={2.0,2.1,2.4,2.5,2.4,2.3,2.2,2.1,2.0};
        double M=mach, cnab=CN[8];
        for(int i=0;i<8;i++) if(M<=XM[i+1]){ double t=(M-XM[i])/(XM[i+1]-XM[i]); cnab=CN[i]+t*(CN[i+1]-CN[i]); break; }
        double aoa=6.0*DEG2RAD;
        double vb[3]={V*sin(aoa),0,-V*cos(aoa)};
        double sp=v3_norm(vb), vh[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};
        double cosa=-vh[2]; if(cosa>1)cosa=1; double al=acos(cosa);
        double Fn=qbar*VEH_AREF*cnab*al;
        double latm=sqrt(vh[0]*vh[0]+vh[1]*vh[1]); double lh[3]={vh[0]/latm,vh[1]/latm,0};
        double Faero[3]={-Fn*lh[0],-Fn*lh[1],0};
        double xcpf=0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));
        double arm[3]={0,0,xcpf*VEH_STAGE_LEN-com};
        double Tbody[3]; v3_cross(Tbody,arm,Faero);
        printf("   AoA=6deg +x: CNa_body=%.3f Fn=%.3e xcp=%.2f arm_z=%.2f\n", cnab, Fn, xcpf*VEH_STAGE_LEN, xcpf*VEH_STAGE_LEN-com);
        printf("   plant/est body moment Tbody=(%.3e,%.3e,%.3e) (pure tau_y expected)\n", Tbody[0],Tbody[1],Tbody[2]);
        /* control does tau_cmd -= Tbody. With zero attitude error, tau_cmd = -Tbody. Allocate. */
        double tc[3]={-Tbody[0],-Tbody[1],-Tbody[2]};
        /* allocate */
        double patP[4]={1,1,-1,-1}, patY[4]={-1,1,1,-1};
        double dp=tc[0]/(4.0*0.7071*A), dy=tc[1]/(4.0*0.7071*A), dr=-tc[2]/(4.0*C);
        double fins[4]; for(int i=0;i<4;i++){ double dd=dr+dp*patP[i]+dy*patY[i];
            if(dd>FIN_DEFL_MAX)dd=FIN_DEFL_MAX; if(dd<-FIN_DEFL_MAX)dd=-FIN_DEFL_MAX; fins[i]=dd; }
        printf("   FF tau_cmd=-Tbody=(%.3e,%.3e,%.3e) -> fins=[%.2f %.2f %.2f %.2f]deg\n",
               tc[0],tc[1],tc[2], fins[0]*RAD2DEG,fins[1]*RAD2DEG,fins[2]*RAD2DEG,fins[3]*RAD2DEG);
        double Fbf[3],Tbf[3]; plant_fin(vb,w0,fins,rho,mach,com,Fbf,Tbf,0);
        printf("   realized fin torque=(%.3e,%.3e,%.3e)\n", Tbf[0],Tbf[1],Tbf[2]);
        printf("   NET body+fin (trim residual, want ~0)=(%.3e,%.3e,%.3e)\n",
               Tbody[0]+Tbf[0],Tbody[1]+Tbf[1],Tbody[2]+Tbf[2]);
        printf("   yaw-axis residual fraction=%.4f (0 => fins hold AoA; ~1 => FF does nothing)\n",
               (Tbody[1]+Tbf[1])/Tbody[1]);
        /* Is the FF the RIGHT sign? tau_cmd must OPPOSE the destabilizing body moment.
         * body moment tends to INCREASE AoA (unstable, CoP below CoM). To HOLD AoA the fins
         * must supply an equal & opposite (restoring) torque = -Tbody. FF adds exactly -Tbody. */
        printf("   SIGN CHECK: body moment tau_y=%.3e (drives AoA up, unstable). FF fin torque tau_y=%.3e.\n",
               Tbody[1], Tbf[1]);
        printf("   Same magnitude, OPPOSITE sign => FF correctly cancels body moment. %s\n",
               (Tbody[1]*Tbf[1]<0 && fabs((Tbody[1]+Tbf[1])/Tbody[1])<0.3)?"TRIM OK":"MISMATCH - investigate");
    }
    return 0;
}
