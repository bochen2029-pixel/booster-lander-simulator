/* cop_sign.c — verify the STABILITY SIGN of the sim's aero moment directly,
 * by reproducing the exact force/torque construction in dynamics.c for a small
 * AoA perturbation of a base-first descent. Answers: does the sim's aero moment
 * RESTORE (stable) or DIVERGE (unstable) the perturbation, for NEW vs OLD xcp?
 *
 * dynamics.c geometry (body frame): +Z is up the body (nose at +Z, base/engine at 0).
 * Base-first descent => vehicle falls with base (z=0) leading, so velocity in world
 * is downward and vrel_b points mostly along -Z (relative wind hits the base).
 * Code: cosa = -vhat[2]; alpha = acos(cosa).  Axial force along -copysign(...,vhat[2]).
 * Normal force Fn = qbar*Aref*CN applied opposite the lateral flow dir.
 * Torque arm arm_cp = {0,0, xcp - com}; Taero = arm_cp x Faero_b.
 */
#include <stdio.h>
#include <math.h>
#define VEH_STAGE_LEN 41.2
#define VEH_DIA 3.66
#define VEH_AREF 10.52
#define DEG2RAD 0.017453292519943295

static double xcp_new(double M){return 0.29+0.03*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));}
static double xcp_old(double M){return 0.62+0.04*exp(-((M-1.05)/0.3)*((M-1.05)/0.3));}

/* reproduce the sim aero force+torque for a given small AoA in the x-z plane.
 * Return the pitch torque about +Y (Tb[1]); its sign vs the perturbation tells
 * stability. We tilt the vehicle so the relative wind has a small +x component. */
static double aero_pitch_torque(double M, double alpha, double com, double (*xcpf)(double)){
    double qbar=1.0; /* unit qbar; only sign/relative magnitude matters */
    double CNa=2.4;  /* representative body slope */
    double CN=CNa*alpha;
    /* vrel_b: base-first => mostly -Z. Perturb by AoA about Y so a small +X appears.
     * vhat = (sin a, 0, -cos a). */
    double vhat[3]={sin(alpha),0.0,-cos(alpha)};
    double Faero_b[3]={0,0,0};
    /* axial along body Z, opposing motion */
    double CA=1.0;
    double Fax=-copysign(qbar*VEH_AREF*CA, vhat[2]); /* vhat[2]<0 => Fax>0 (+Z), pushes up the body: correct drag on base */
    Faero_b[2]+=Fax;
    /* normal force opposite lateral flow */
    double lat[3]={vhat[0],vhat[1],0.0}; double latm=sqrt(lat[0]*lat[0]+lat[1]*lat[1]);
    if(latm>1e-9){ double lh[3]={lat[0]/latm,lat[1]/latm,0}; double Fn=qbar*VEH_AREF*CN;
        Faero_b[0]-=Fn*lh[0]; Faero_b[1]-=Fn*lh[1]; }
    double xcp=xcpf(M)*VEH_STAGE_LEN;
    double arm[3]={0,0,xcp-com};
    /* Taero = arm x Faero_b ; Y component = arm_z*F_x - arm_x*F_z = arm_z*F_x (arm_x=0) */
    double Ty = arm[2]*Faero_b[0] - arm[0]*Faero_b[2];
    return Ty;
}

int main(void){
    /* A +alpha (nose swings so +X wind appears) is STABLE if the resulting pitch
     * torque about +Y is NEGATIVE (restoring, pushes alpha back to 0), i.e.
     * d(Ty)/d(alpha) < 0 => stable. */
    double com=12.27; /* AERO-descent mass, from mass_props */
    double a=2.0*DEG2RAD;
    double Ms[]={0.3,0.9,1.05,1.2,2.0};
    printf("Stability sign check (base-first, com=%.2f m).\n", com);
    printf("Stable if d(Ty)/d(alpha) < 0 (restoring). Ty at alpha=+2deg:\n");
    printf("%-6s | NEW Ty  d/da  verdict | OLD Ty  d/da  verdict\n","Mach");
    for(int i=0;i<5;i++){
        double M=Ms[i];
        double TyN=aero_pitch_torque(M,a,com,xcp_new);
        double TyN2=aero_pitch_torque(M,2*a,com,xcp_new);
        double dN=(TyN2-TyN)/a; /* slope proxy */
        double TyO=aero_pitch_torque(M,a,com,xcp_old);
        double TyO2=aero_pitch_torque(M,2*a,com,xcp_old);
        double dO=(TyO2-TyO)/a;
        printf("%-6.2f | %+6.3f %+6.3f %-7s | %+6.3f %+6.3f %-7s\n",
            M, TyN, dN, dN<0?"STABLE":"UNSTAB", TyO, dO, dO<0?"STABLE":"UNSTAB");
    }
    printf("\nNote: (xcp - com) sign is the whole story. xcp>com => arm_z>0 =>\n");
    printf("with Fx<0 (normal force opposes +X flow) => Ty<0 => RESTORING => STABLE.\n");
    printf("xcp<com => arm_z<0 => Ty>0 => DIVERGING => UNSTABLE.\n");
    printf("So CoP ABOVE CoM (toward nose, larger z) = stable base-first. Confirmed.\n");
    return 0;
}
