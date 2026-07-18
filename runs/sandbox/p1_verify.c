/* p1_verify.c  — Agent P1 INDEPENDENT verification of D-005 plant/control math.
 *
 * Self-contained. Re-implements (a) my OWN from-scratch derivation of fin torque,
 * Cmq damping, and trim, and (b) a faithful copy of the plant/control code blocks,
 * then compares. No dependency on the project build; constants hard-coded from
 * constants.h so a divergence in constants would show as a mismatch too.
 *
 * Build: p1_build.cmd p1_verify.c p1_verify.exe
 */
#include <stdio.h>
#include <math.h>

/* ---- constants (mirrored from core/constants.h) ---- */
#define G0            9.80665
#define DEG2RAD       0.017453292519943295
#define RAD2DEG       57.29577951308232
#define PI            3.141592653589793

#define VEH_LEN       47.7
#define VEH_STAGE_LEN 41.2
#define VEH_DIA       3.66
#define VEH_RADIUS    (VEH_DIA*0.5)     /* 1.83 */
#define VEH_AREF      10.52
#define VEH_DRY       25600.0
#define VEH_DRY_COMZ  12.4

#define LOX_MAX       287400.0
#define RP1_MAX       123500.0
#define LOX_RHO       1220.0
#define RP1_RHO       833.0
#define TANK_AREA     9.9
#define LOX_BASE_Z    16.0
#define RP1_BASE_Z    1.6
#define MIX_RATIO     2.33

#define FIN_COUNT     4
#define FIN_Z         45.0
#define FIN_AREA      2.4
#define FIN_DEFL_MAX  (20.0*DEG2RAD)
#define FIN_CNA       3.0
#define FIN_STALL     (25.0*DEG2RAD)
#define FIN_CT_DELTA_FRAC 0.35
#define RCS_ARM       1.83
#define BODY_CMQ_CDC  0.6

/* ---- tiny vec3 ---- */
static void v3_cross(double o[3], const double a[3], const double b[3]){
    double x=a[1]*b[2]-a[2]*b[1], y=a[2]*b[0]-a[0]*b[2], z=a[0]*b[1]-a[1]*b[0];
    o[0]=x;o[1]=y;o[2]=z;
}
static double v3_dot(const double a[3], const double b[3]){ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static double v3_norm(const double a[3]){ return sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }

/* ============================================================================
 * Mass properties (mirror of dynamics.c mass_props) — need com for arms & Cmq.
 * ==========================================================================*/
typedef struct { double m, com, I_ax, I_tr; } MP;
static void mass_props(double m_lox, double m_rp1, MP* mp){
    if(m_lox<0)m_lox=0; if(m_rp1<0)m_rp1=0;
    const double rt2 = TANK_AREA/PI;
    const double R2  = VEH_RADIUS*VEH_RADIUS;
    double m_dry = VEH_DRY, z_dry = VEH_DRY_COMZ;
    double Itl_dry = m_dry*(6.0*R2 + VEH_LEN*VEH_LEN)/12.0;
    double Iax_dry = m_dry*R2;
    double h_l = m_lox/(LOX_RHO*TANK_AREA), z_l = LOX_BASE_Z + 0.5*h_l;
    double h_r = m_rp1/(RP1_RHO*TANK_AREA), z_r = RP1_BASE_Z + 0.5*h_r;
    double Iax_l = 0.5*m_lox*rt2, Itl_l = m_lox*(3.0*rt2 + h_l*h_l)/12.0;
    double Iax_r = 0.5*m_rp1*rt2, Itl_r = m_rp1*(3.0*rt2 + h_r*h_r)/12.0;
    double M = m_dry + m_lox + m_rp1;
    double com = (m_dry*z_dry + m_lox*z_l + m_rp1*z_r)/M;
    double I_tr = Itl_dry + m_dry*(z_dry-com)*(z_dry-com)
                + Itl_l   + m_lox*(z_l-com)*(z_l-com)
                + Itl_r   + m_rp1*(z_r-com)*(z_r-com);
    double I_ax = Iax_dry + Iax_l + Iax_r;
    mp->m=M; mp->com=com; mp->I_ax=I_ax; mp->I_tr=I_tr;
}

static double fin_dip(double M){
    if(M>0.8 && M<1.2) return 0.55;
    if(M>2.0) return 0.80;
    if(M>=0.6 && M<=0.8) return 1.0 + (0.55-1.0)*(M-0.6)/0.2;
    if(M>=1.2 && M<=2.0) return 0.55 + (0.80-0.55)*(M-1.2)/0.8;
    return 1.0;
}

/* ============================================================================
 * PLANT fin torque — faithful copy of dynamics.c grid-fin block.
 * Given: body-frame relative velocity vrel_b, body rate w, per-fin deflections
 * delta[4], atmo rho, mach, com. Returns total fin force Fb[3] and torque Tb[3].
 * ==========================================================================*/
static void plant_fin_torque(const double vrel_b[3], const double w[3],
                             const double delta[4], double rho, double mach,
                             double com, double Fb[3], double Tb[3]){
    Fb[0]=Fb[1]=Fb[2]=0; Tb[0]=Tb[1]=Tb[2]=0;
    double CNa_f = FIN_CNA*fin_dip(mach);
    for(int i=0;i<4;i++){
        double phi=(45.0+90.0*i)*DEG2RAD;
        double er[3]={cos(phi),sin(phi),0.0};
        double et[3]={-sin(phi),cos(phi),0.0};
        double rm[3]={VEH_RADIUS*cos(phi),VEH_RADIUS*sin(phi),FIN_Z};
        double wxr[3]; v3_cross(wxr,w,rm);
        double vi[3]={vrel_b[0]+wxr[0], vrel_b[1]+wxr[1], vrel_b[2]+wxr[2]};
        double vsp=v3_norm(vi); if(vsp<1.0) continue;
        double qbi=0.5*rho*vsp*vsp;
        double w_ax=-vi[2];
        double w_r=v3_dot(vi,er);
        double d=delta[i];
        double alpha_i=d + atan2(w_r,w_ax);
        double aeff=alpha_i; if(aeff>FIN_STALL)aeff=FIN_STALL; if(aeff<-FIN_STALL)aeff=-FIN_STALL;
        double L=qbi*FIN_AREA*CNa_f*aeff;
        double Ft=qbi*FIN_AREA*(FIN_CT_DELTA_FRAC*CNa_f)*d;
        double Ff[3]={ -L*er[0]-Ft*et[0], -L*er[1]-Ft*et[1], 0.0 };
        Fb[0]+=Ff[0]; Fb[1]+=Ff[1]; Fb[2]+=Ff[2];
        double arm[3]={rm[0],rm[1],rm[2]-com};
        double Tf[3]; v3_cross(Tf,arm,Ff);
        Tb[0]+=Tf[0]; Tb[1]+=Tf[1]; Tb[2]+=Tf[2];
    }
}

/* ============================================================================
 * CONTROL allocation — faithful copy of control.c fins_active branch.
 * Given commanded torque tau_cmd[3], returns fin deflections fins[4].
 * ==========================================================================*/
static void control_alloc(const double tau_cmd[3], double qbar, double mach,
                          double com, double fins[4]){
    double CNa_f=FIN_CNA*fin_dip(mach);
    double k=qbar*FIN_AREA*CNa_f;
    double A=(FIN_Z-com)*k;
    double C=RCS_ARM*(FIN_CT_DELTA_FRAC*k);
    double patP[4]={1,1,-1,-1}, patY[4]={-1,1,1,-1};
    double dpitch = (fabs(A)>1.0)? tau_cmd[0]/(4.0*0.7071*A):0.0;
    double dyaw   = (fabs(A)>1.0)? tau_cmd[1]/(4.0*0.7071*A):0.0;
    double droll  = (fabs(C)>1.0)? -tau_cmd[2]/(4.0*C):0.0;
    for(int i=0;i<4;i++){
        double d=droll + dpitch*patP[i] + dyaw*patY[i];
        if(d>FIN_DEFL_MAX)d=FIN_DEFL_MAX; if(d<-FIN_DEFL_MAX)d=-FIN_DEFL_MAX;
        fins[i]=d;
    }
}

int main(void){
    printf("=================================================================\n");
    printf("  AGENT P1 — INDEPENDENT VERIFICATION OF D-005 PLANT/CONTROL MATH\n");
    printf("=================================================================\n\n");

    /* Representative AERO-descent condition (from kickoff / D-005). */
    double rho_aero = 0.31, V_aero = 350.0, mach_aero = 350.0/302.0; /* ~1.16, transonic dip band */
    /* Use a clean supersonic case too to avoid the dip messing with the readback */
    double rho_s = 0.31, V_s = 700.0, mach_s = 2.5;
    MP mp; mass_props(120000.0, 51000.0, &mp);  /* mid aero-descent mass ~ 197t? adjust below */

    printf("[mass props @ m_lox=120t m_rp1=51t]  m=%.1f  com=%.3f m  I_tr=%.4e  I_ax=%.4e\n\n",
           mp.m, mp.com, mp.I_tr, mp.I_ax);

    /* --------------------------------------------------------------------
     * TEST 1: Plant per-fin torque vs my closed-form, zero rate, small defl.
     * ------------------------------------------------------------------*/
    printf("----- TEST 1: fin torque mapping (plant vs closed form) -----\n");
    double com_t = mp.com;
    double mach_use = mach_s;              /* clean, dip=0.8 */
    double rho_use = rho_s, V_use = V_s;
    double CNa_f = FIN_CNA*fin_dip(mach_use);
    double qbar = 0.5*rho_use*V_use*V_use;
    double k = qbar*FIN_AREA*CNa_f;
    double A = (FIN_Z-com_t)*k;
    double C = RCS_ARM*(FIN_CT_DELTA_FRAC*k);
    printf("qbar=%.1f Pa  CNa_f=%.3f  k=%.4e  A=(FIN_Z-com)*k=%.4e  C=%.4e\n",
           qbar, CNa_f, k, A, C);
    printf("(FIN_Z-com)=%.3f m ; note RCS_ARM=%.2f == VEH_RADIUS=%.2f\n\n",
           FIN_Z-com_t, (double)RCS_ARM, (double)VEH_RADIUS);

    /* pure axial flow, no rate: vrel_b = (0,0,-V) means base-first (leading -Z). */
    double vrelb[3]={0,0,-V_use};
    double w0[3]={0,0,0};
    double dsmall=0.5*DEG2RAD;

    /* pitch pattern [1,1,-1,-1]: expect tau_x = A*0.707*(d1+d2-d3-d4)=A*0.707*4*dsmall */
    {
        double d[4]={ dsmall, dsmall, -dsmall, -dsmall };
        double Fb[3],Tb[3]; plant_fin_torque(vrelb,w0,d,rho_use,mach_use,com_t,Fb,Tb);
        double tx_expect = A*0.7071*(d[0]+d[1]-d[2]-d[3]);
        printf("PITCH cmd [+,+,-,-]: plant Tb=(%.2f, %.2f, %.2f)\n", Tb[0],Tb[1],Tb[2]);
        printf("   closed-form tau_x=A*0.707*(d1+d2-d3-d4)=%.2f  -> ratio plant/cf = %.5f\n",
               tx_expect, Tb[0]/tx_expect);
        printf("   cross terms tau_y=%.3e tau_z=%.3e (want ~0)\n\n", Tb[1], Tb[2]);
    }
    /* yaw pattern [-1,1,1,-1]: expect tau_y */
    {
        double d[4]={ -dsmall, dsmall, dsmall, -dsmall };
        double Fb[3],Tb[3]; plant_fin_torque(vrelb,w0,d,rho_use,mach_use,com_t,Fb,Tb);
        /* closed form for tau_y from lift: tau_y = -A*0.707*(d1-d2-d3+d4) */
        double ty_expect = -A*0.7071*(d[0]-d[1]-d[2]+d[3]);
        printf("YAW cmd [-,+,+,-]: plant Tb=(%.2f, %.2f, %.2f)\n", Tb[0],Tb[1],Tb[2]);
        printf("   closed-form tau_y=-A*0.707*(d1-d2-d3+d4)=%.2f -> ratio plant/cf = %.5f\n",
               ty_expect, Tb[1]/ty_expect);
        printf("   cross terms tau_x=%.3e tau_z=%.3e (want ~0)\n\n", Tb[0], Tb[2]);
    }
    /* roll pattern [1,1,1,1]: expect tau_z = -4C*droll (droll=dsmall) */
    {
        double d[4]={ dsmall, dsmall, dsmall, dsmall };
        double Fb[3],Tb[3]; plant_fin_torque(vrelb,w0,d,rho_use,mach_use,com_t,Fb,Tb);
        double tz_expect = -4.0*C*dsmall;
        printf("ROLL cmd [+,+,+,+]: plant Tb=(%.3e, %.3e, %.2f)\n", Tb[0],Tb[1],Tb[2]);
        printf("   closed-form tau_z=-4C*droll=%.2f  -> ratio plant/cf = %.5f\n",
               tz_expect, Tb[2]/tz_expect);
        printf("   cross terms tau_x=%.3e tau_y=%.3e (want ~0)\n\n", Tb[0], Tb[1]);
    }

    /* --------------------------------------------------------------------
     * TEST 2: CLOSED-LOOP GAIN of the allocation. Command a unit torque per
     * axis, run it through control_alloc, then feed the resulting fins into
     * the PLANT and read back the realized torque. Ratio realized/commanded
     * == closed-loop gain; sign must be POSITIVE (negative = pos. feedback).
     * ------------------------------------------------------------------*/
    printf("----- TEST 2: allocation UNIT-GAIN + SIGN (cmd -> alloc -> plant) -----\n");
    double taus[3]={ 5.0e6, 5.0e6, 2.0e5 }; /* representative pitch,yaw,roll cmds (Nm) */
    const char* axn[3]={"PITCH(tau_x)","YAW(tau_y)","ROLL(tau_z)"};
    for(int ax=0; ax<3; ax++){
        double tc[3]={0,0,0}; tc[ax]=taus[ax];
        double fins[4]; control_alloc(tc, qbar, mach_use, com_t, fins);
        double Fb[3],Tb[3]; plant_fin_torque(vrelb,w0,fins,rho_use,mach_use,com_t,Fb,Tb);
        printf("cmd %s = %.3e Nm  -> fins(deg)=[%.3f %.3f %.3f %.3f]\n",
               axn[ax], tc[ax], fins[0]*RAD2DEG, fins[1]*RAD2DEG, fins[2]*RAD2DEG, fins[3]*RAD2DEG);
        printf("   realized Tb=(%.3e, %.3e, %.3e)\n", Tb[0],Tb[1],Tb[2]);
        printf("   GAIN realized/cmd on axis %d = %+.4f  (want +1.00; NEGATIVE=positive feedback!)\n",
               ax, Tb[ax]/tc[ax]);
        double off0 = (ax==0)?Tb[1]:Tb[0];
        double off1 = (ax==2)?Tb[1]:Tb[2];
        printf("   off-axis leakage: %.3e , %.3e\n\n", off0, off1);
    }

    /* --------------------------------------------------------------------
     * TEST 3: residual cross-coupling ROLL-from-PITCH at nonzero AoA.
     * At AoA, the four fins see different qbi (windward vs leeward) so a pure
     * pitch command may leak a net roll torque. Quantify it.
     * ------------------------------------------------------------------*/
    printf("----- TEST 3: roll-from-pitch cross-coupling at AoA (per-fin qbi diff) -----\n");
    for(double aoa_deg=0; aoa_deg<=10.0; aoa_deg+=5.0){
        double aoa=aoa_deg*DEG2RAD;
        /* vrel_b with AoA in the x-z plane: leading -Z, tilt so vx>0 */
        double vb[3]={ V_use*sin(aoa), 0.0, -V_use*cos(aoa) };
        /* command pure pitch */
        double tc[3]={ 5.0e6, 0, 0 };
        double fins[4]; control_alloc(tc, qbar, mach_use, com_t, fins);
        double Fb[3],Tb[3]; plant_fin_torque(vb,w0,fins,rho_use,mach_use,com_t,Fb,Tb);
        printf("AoA=%4.1f deg: realized Tb=(%.3e, %.3e, %.3e)  roll/pitch leak=%.3e (%.4f%%)\n",
               aoa_deg, Tb[0],Tb[1],Tb[2], Tb[2], 100.0*Tb[2]/Tb[0]);
    }
    printf("\n");

    /* --------------------------------------------------------------------
     * TEST 4: Cmq strip-theory damping — J integral, torque form, and zeta.
     * ------------------------------------------------------------------*/
    printf("----- TEST 4: Cmq strip-theory damping (J, units, zeta) -----\n");
    {
        double zc=mp.com, L=VEH_LEN;
        /* code's closed form */
        double J_code = L*L*L/3.0 - zc*L*L + zc*zc*L;
        /* my independent numeric integral of (z-zc)^2 dz from 0..L (Simpson, N=100000) */
        int N=100000; double hh=L/N, Jn=0;
        for(int i=0;i<=N;i++){
            double z=i*hh; double f=(z-zc)*(z-zc);
            double wgt = (i==0||i==N)?1.0 : ((i&1)?4.0:2.0);
            Jn += wgt*f;
        }
        Jn *= hh/3.0;
        printf("J: code closed-form=%.6e   my Simpson integral=%.6e   rel.err=%.3e\n",
               J_code, Jn, fabs(J_code-Jn)/Jn);
        /* units: 0.5*rho*V*Cdc*D*J*w has units kg/m^3 * m/s * m * m^3 * 1/s
         *        = kg/m^3 * m/s * m^4 / s = kg*m^2/s^2 = N*m . check dimensionally below. */
        printf("units of 0.5*rho*V*Cdc*D*J*w = (kg/m^3)(m/s)(m)(m^3)(1/s) = kg*m^2/s^2 = N*m  [OK]\n\n");

        /* zeta at AERO and TERMINAL conditions.
         * linearized pitch: I_tr*thetaddot = -Cdamp*thetadot - k_alpha*theta (+ ...)
         * Cmq alone gives the damping coefficient Cdamp = 0.5*rho*V*Cdc*D*J.
         * For zeta we need the pitch stiffness k_alpha. Two references:
         *  (a) bare-body aero pitch stiffness from CoP-CoM (destabilizing => neg, no real zeta)
         *  (b) the fin-provided restoring stiffness (what actually sets omega_n in descent)
         * Report zeta against a representative restoring stiffness = qbar*Aref*CNa*(xcp-com)
         * magnitude, i.e. treat the airframe+fins as a spring of that |stiffness|. */
        struct { const char* name; double rho,V,Itr; } cond[2] = {
            {"AERO   ", 0.31, 350.0, mp.I_tr},
            {"TERMINAL",1.00, 180.0, mp.I_tr}
        };
        for(int c=0;c<2;c++){
            double Cdamp = 0.5*cond[c].rho*cond[c].V*BODY_CMQ_CDC*VEH_DIA*J_code;
            /* representative pitch stiffness: use fin static margin ~ body CNa*(0.02L arm)
             * As a proxy spring use k_sp = qbar*Aref*CNa_body*(0.02*L). CNa_body~2.4. */
            double qb = 0.5*cond[c].rho*cond[c].V*cond[c].V;
            double k_sp = qb*VEH_AREF*2.4*(0.02*VEH_LEN); /* ~1 m static margin from fins/body */
            double wn = sqrt(fabs(k_sp)/cond[c].Itr);
            double zeta = Cdamp/(2.0*cond[c].Itr*wn);
            printf("%s rho=%.2f V=%.0f: Cdamp=%.4e Nm/(rad/s)  qbar=%.0f k_sp~%.3e  wn=%.4f rad/s  zeta=%.4f\n",
                   cond[c].name, cond[c].rho, cond[c].V, Cdamp, qb, k_sp, wn, zeta);
        }
        printf("\n(Also report the pure damping-time-constant tau = 2*I_tr/Cdamp, independent of stiffness:)\n");
        for(int c=0;c<2;c++){
            double Cdamp = 0.5*cond[c].rho*cond[c].V*BODY_CMQ_CDC*VEH_DIA*J_code;
            double tau = 2.0*cond[c].Itr/Cdamp;   /* e-folding of rate under pure damping */
            double halflife = 0.6931*cond[c].Itr/Cdamp;
            printf("%s: rate e-fold tau=%.2f s, half-life=%.2f s (I_tr=%.3e)\n",
                   cond[c].name, tau, halflife, cond[c].Itr);
        }
    }
    printf("\n");

    /* --------------------------------------------------------------------
     * TEST 5: ROLL damping — is body/fin roll damping really negligible?
     * Compare available aero roll damping to the pitch damping and to the
     * roll inertia.  Fins DO have roll damping via omega x r (tangential),
     * but the BODY Cmq block only damps x,y (pitch/yaw), NOT z (roll).
     * ------------------------------------------------------------------*/
    printf("----- TEST 5: ROLL (w_z) damping presence/magnitude -----\n");
    {
        /* Give the vehicle a pure roll rate and read the plant fin torque tau_z. */
        double wz = 0.2; /* rad/s roll */
        double w[3]={0,0,wz};
        double d0[4]={0,0,0,0};
        double Fb[3],Tb[3]; plant_fin_torque(vrelb,w,d0,rho_use,mach_use,com_t,Fb,Tb);
        printf("pure roll wz=%.2f rad/s, zero deflection: plant fin Tb=(%.3e,%.3e,%.3e)\n",
               Tb[0],Tb[1],Tb[2]);
        printf("   -> fin roll damping torque tau_z=%.4e Nm ; roll-damp coeff=%.4e Nm/(rad/s)\n",
               Tb[2], Tb[2]/wz);
        /* body Cmq contributes ZERO to z by construction (Tb[0],Tb[1] only). Confirm claim. */
        double zc=mp.com, L=VEH_LEN;
        double J_code = L*L*L/3.0 - zc*L*L + zc*zc*L;
        double Cdamp_pitch = 0.5*rho_use*V_use*BODY_CMQ_CDC*VEH_DIA*J_code;
        printf("   body Cmq pitch-damp coeff=%.4e Nm/(rad/s); body roll-damp = 0 (block omits z)\n",
               Cdamp_pitch);
        printf("   I_ax=%.3e  -> fin roll e-fold tau=%.2f s (weak). If fins stowed/low-q: NO roll damping at all except RCS.\n",
               mp.I_ax, (fabs(Tb[2])>0)? mp.I_ax/fabs(Tb[2]/wz):INFINITY);
    }
    printf("\n");

    /* --------------------------------------------------------------------
     * TEST 6: TRIM feedforward sign check. The plant body aero moment at AoA
     * about CoM, vs the control's trim-FF term that is SUBTRACTED from tau_cmd.
     * For the fins to HOLD an AoA, the FF must ADD a torque equal to the
     * aero moment the fins must counter, i.e. tau_cmd must contain -(body
     * aero moment). Verify control's sign matches plant's actual moment.
     * ------------------------------------------------------------------*/
    printf("----- TEST 6: trim feedforward sign vs plant body aero moment -----\n");
    {
        double aoa=6.0*DEG2RAD;
        double vb[3]={ V_use*sin(aoa),0,-V_use*cos(aoa) };
        double sp=v3_norm(vb);
        double vhat[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};
        double cosa=-vhat[2]; if(cosa>1)cosa=1; if(cosa<-1)cosa=-1;
        double al=acos(cosa);
        /* PLANT body aero moment (mirror dynamics.c aero block) */
        double CNa_body=2.4; /* mach 2.5 ~ 2.2..2.3; use table would be ~2.25; approx */
        double Fn=qbar*VEH_AREF*CNa_body*al;
        double lat2=vhat[0]*vhat[0]+vhat[1]*vhat[1];
        double latm=sqrt(lat2), lh[3]={vhat[0]/latm,vhat[1]/latm,0};
        double Faero[3]={-Fn*lh[0],-Fn*lh[1],0};
        double xcp_frac = 0.29+0.03*exp(-((mach_use-1.05)/0.3)*((mach_use-1.05)/0.3));
        double xcp = xcp_frac*VEH_STAGE_LEN;
        double arm[3]={0,0,xcp-com_t};
        double Tbody[3]; v3_cross(Tbody,arm,Faero);
        printf("AoA=6deg: plant body aero moment about CoM Tbody=(%.3e,%.3e,%.3e)\n",
               Tbody[0],Tbody[1],Tbody[2]);
        printf("   xcp=%.2f m, com=%.2f m, xcp-com=%.2f m (NEGATIVE => CoP below CoM => destabilizing)\n",
               xcp, com_t, xcp-com_t);
        /* CONTROL trim-FF: it computes the SAME Tbody and does tau_cmd -= Tbody.
         * So control adds -Tbody to the fin command. For static trim we need the
         * FIN torque to CANCEL the body moment: tau_fin = -Tbody. control_alloc
         * of tau_cmd=-Tbody should realize plant fin torque ~ -Tbody. Verify. */
        double tc[3]={-Tbody[0],-Tbody[1],-Tbody[2]};
        double fins[4]; control_alloc(tc,qbar,mach_use,com_t,fins);
        double Fbf[3],Tbf[3]; plant_fin_torque(vb,w0,fins,rho_use,mach_use,com_t,Fbf,Tbf);
        printf("   control FF sets tau_cmd=-Tbody; realized fin torque=(%.3e,%.3e,%.3e)\n",
               Tbf[0],Tbf[1],Tbf[2]);
        printf("   net moment body+fin (should be ~0 for trim): (%.3e,%.3e,%.3e)\n",
               Tbody[0]+Tbf[0], Tbody[1]+Tbf[1], Tbody[2]+Tbf[2]);
        printf("   pitch residual / body moment = %.4f  (0 => fins fully cancel body => AoA holds)\n",
               (Tbody[0]+Tbf[0])/Tbody[0]);
    }
    printf("\n=================================================================\n");
    printf("  END P1 VERIFICATION\n");
    printf("=================================================================\n");
    return 0;
}
