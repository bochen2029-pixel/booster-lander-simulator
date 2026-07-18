/* quat_alpha.c — remove ALL remaining sign ambiguity by reproducing dynamics.c's
 * EXACT attitude->alpha->force->torque path with real quaternions, for a body
 * pitched by a small +theta about body-Y, in a base-first descent. Confirms:
 *  (a) sign of alpha vs theta, (b) sign of the resulting body-Y aero torque,
 *  (c) whether that torque REDUCES theta (restoring/stable) for xcp>com.
 *
 * Mirrors: q_rot_inv (world->body), cosa=-vhat[2], normal force -Fn*lh, torque
 * arm_cp=(0,0,xcp-com) x Faero_b.
 */
#define _USE_MATH_DEFINES
#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.141592653589793
#endif
#define VEH_STAGE_LEN 41.2
#define VEH_AREF 10.52
#define DEG2RAD 0.017453292519943295

/* quaternion (x,y,z,w), body->world. q_rot_inv: v_body = R(q)^T v_world. */
static void q_rot_inv(double out[3], const double q[4], const double v[3]){
    double x=q[0],y=q[1],z=q[2],w=q[3];
    /* R^T = R(conj q). conj = (-x,-y,-z,w). Apply rotation of v by conj q. */
    double cx=-x,cy=-y,cz=-z,cw=w;
    /* t = 2*cross(qv, v) */
    double tx=2*(cy*v[2]-cz*v[1]);
    double ty=2*(cz*v[0]-cx*v[2]);
    double tz=2*(cx*v[1]-cy*v[0]);
    out[0]=v[0]+cw*tx+(cy*tz-cz*ty);
    out[1]=v[1]+cw*ty+(cz*tx-cx*tz);
    out[2]=v[2]+cw*tz+(cx*ty-cy*tx);
}

int main(void){
    /* World descent velocity: straight down = (0,0,-V) (world z up). */
    double V=300.0; double v_world[3]={0,0,-V};
    double com=12.269;
    double xcp=0.32*VEH_STAGE_LEN; /* transonic peak, arm>0 case */
    double CNa=2.4, qbar=1.0;

    printf("Reproducing dynamics.c attitude->alpha->torque with real quaternions.\n");
    printf("World descent v=(0,0,-%.0f). Body pitched +theta about body-Y.\n", V);
    printf("xcp=%.2fm (arm=xcp-com=%+.2fm).\n\n", xcp, xcp-com);
    printf("%-8s %8s %10s %12s %10s\n","theta[d]","alpha[d]","Fbody_x","Torque_Y","dtheta?");
    for(int td=-4; td<=4; td+=2){
        double th=td*DEG2RAD;
        /* body pitched +theta about Y: q = (0, sin(th/2), 0, cos(th/2)).
         * The booster nominal attitude is base-down = body +Z points UP (nose up,
         * base/engine down toward the oncoming wind). We model the nominal as body
         * +Z aligned with world +Z (nose up), so relative wind (from below) hits the
         * base. A +theta pitch tips the nose toward +world-x. */
        double q[4]={0, sin(th/2), 0, cos(th/2)};
        double vb[3]; q_rot_inv(vb,q,v_world);
        double sp=sqrt(vb[0]*vb[0]+vb[1]*vb[1]+vb[2]*vb[2]);
        double vhat[3]={vb[0]/sp,vb[1]/sp,vb[2]/sp};
        double cosa=-vhat[2]; if(cosa>1)cosa=1; if(cosa<-1)cosa=-1;
        double alpha=acos(cosa);
        double lat[3]={vhat[0],vhat[1],0}; double latm=sqrt(lat[0]*lat[0]+lat[1]*lat[1]);
        double Fbx=0;
        double CN=CNa*alpha;
        if(latm>1e-9){ double lh0=lat[0]/latm; Fbx=-qbar*VEH_AREF*CN*lh0; }
        /* torque_Y = arm_z*Fbx (arm_x=0) */
        double arm=xcp-com;
        double Ty=arm*Fbx;
        /* Does Ty reduce theta? Ty about body-Y; +theta was about +Y. So Ty<0
         * opposes +theta => restoring. For theta>0 we want Ty<0 to be stable. */
        const char* tag;
        if(td==0) tag="(ref)";
        else if((td>0 && Ty<0)||(td<0 && Ty>0)) tag="RESTORING(stable)";
        else tag="DIVERGING(unstable)";
        printf("%-8d %8.3f %10.4f %12.5f %10s\n", td, alpha/DEG2RAD, Fbx, Ty, tag);
    }
    printf("\nConclusion: for arm=xcp-com>0, +theta gives +alpha and Ty<0 (opposes\n");
    printf("theta) => RESTORING => STABLE. Confirms xcp>com (CoP aft/high-z) is the\n");
    printf("STABLE condition base-first. (arm<0 flips every sign => unstable.)\n");
    return 0;
}
