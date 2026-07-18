/* vmath.h — minimal vec3 + scalar-last quaternion math, host/device ready.
 * All double precision (CPU plant path). Header-only, inline.
 * Quaternion convention: (x,y,z,w) scalar-last, body->world. See CLAUDE_v1.md §4.1.
 */
#ifndef BL_VMATH_H
#define BL_VMATH_H

#include <math.h>

#ifndef BL_HD
#  if defined(__CUDACC__)
#    define BL_HD __host__ __device__
#  else
#    define BL_HD
#  endif
#endif

/* ---- vec3 ---- */
BL_HD static inline void v3_set(double o[3], double x, double y, double z){ o[0]=x;o[1]=y;o[2]=z; }
BL_HD static inline void v3_copy(double o[3], const double a[3]){ o[0]=a[0];o[1]=a[1];o[2]=a[2]; }
BL_HD static inline void v3_add(double o[3], const double a[3], const double b[3]){ o[0]=a[0]+b[0];o[1]=a[1]+b[1];o[2]=a[2]+b[2]; }
BL_HD static inline void v3_sub(double o[3], const double a[3], const double b[3]){ o[0]=a[0]-b[0];o[1]=a[1]-b[1];o[2]=a[2]-b[2]; }
BL_HD static inline void v3_scale(double o[3], const double a[3], double s){ o[0]=a[0]*s;o[1]=a[1]*s;o[2]=a[2]*s; }
BL_HD static inline void v3_madd(double o[3], const double a[3], const double b[3], double s){ o[0]=a[0]+b[0]*s;o[1]=a[1]+b[1]*s;o[2]=a[2]+b[2]*s; }
BL_HD static inline double v3_dot(const double a[3], const double b[3]){ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
BL_HD static inline void v3_cross(double o[3], const double a[3], const double b[3]){
    double x=a[1]*b[2]-a[2]*b[1], y=a[2]*b[0]-a[0]*b[2], z=a[0]*b[1]-a[1]*b[0];
    o[0]=x;o[1]=y;o[2]=z;
}
BL_HD static inline double v3_norm(const double a[3]){ return sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }
BL_HD static inline double v3_norm2(const double a[3]){ return a[0]*a[0]+a[1]*a[1]+a[2]*a[2]; }
BL_HD static inline double v3_normalize(double o[3], const double a[3]){
    double n=v3_norm(a); if(n>1e-300){ double inv=1.0/n; o[0]=a[0]*inv;o[1]=a[1]*inv;o[2]=a[2]*inv; } else { o[0]=o[1]=o[2]=0.0; }
    return n;
}

/* ---- quaternion (xyzw) ---- */
BL_HD static inline void q_identity(double q[4]){ q[0]=q[1]=q[2]=0.0; q[3]=1.0; }
BL_HD static inline void q_copy(double o[4], const double a[4]){ o[0]=a[0];o[1]=a[1];o[2]=a[2];o[3]=a[3]; }

/* Hamilton product p (x) r, both xyzw. */
BL_HD static inline void q_mul(double o[4], const double p[4], const double r[4]){
    double px=p[0],py=p[1],pz=p[2],pw=p[3];
    double rx=r[0],ry=r[1],rz=r[2],rw=r[3];
    double x = pw*rx + px*rw + py*rz - pz*ry;
    double y = pw*ry - px*rz + py*rw + pz*rx;
    double z = pw*rz + px*ry - py*rx + pz*rw;
    double w = pw*rw - px*rx - py*ry - pz*rz;
    o[0]=x;o[1]=y;o[2]=z;o[3]=w;
}
BL_HD static inline void q_conj(double o[4], const double a[4]){ o[0]=-a[0];o[1]=-a[1];o[2]=-a[2];o[3]=a[3]; }
BL_HD static inline double q_normalize(double q[4]){
    double n=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if(n>1e-300){ double inv=1.0/n; q[0]*=inv;q[1]*=inv;q[2]*=inv;q[3]*=inv; }
    return n;
}
/* Rotate body vector v into world frame: out = q * v * q^-1 (optimized). */
BL_HD static inline void q_rot(double out[3], const double q[4], const double v[3]){
    double x=q[0],y=q[1],z=q[2],w=q[3];
    double tx = 2.0*(y*v[2]-z*v[1]);
    double ty = 2.0*(z*v[0]-x*v[2]);
    double tz = 2.0*(x*v[1]-y*v[0]);
    out[0] = v[0] + w*tx + (y*tz - z*ty);
    out[1] = v[1] + w*ty + (z*tx - x*tz);
    out[2] = v[2] + w*tz + (x*ty - y*tx);
}
/* Rotate world vector into body frame: uses conjugate. */
BL_HD static inline void q_rot_inv(double out[3], const double q[4], const double v[3]){
    double qc[4]; q_conj(qc,q); q_rot(out,qc,v);
}
/* Quaternion time derivative from body angular velocity w: qdot = 0.5 * q (x) [w,0]. */
BL_HD static inline void q_deriv(double qd[4], const double q[4], const double w[3]){
    double wq[4]; wq[0]=w[0]; wq[1]=w[1]; wq[2]=w[2]; wq[3]=0.0;
    double t[4]; q_mul(t,q,wq);
    qd[0]=0.5*t[0]; qd[1]=0.5*t[1]; qd[2]=0.5*t[2]; qd[3]=0.5*t[3];
}
/* Smallest quaternion rotating unit vector a onto unit vector b (both world). xyzw. */
BL_HD static inline void q_from_two_vec(double q[4], const double a[3], const double b[3]){
    double d=v3_dot(a,b);
    double c[3]; v3_cross(c,a,b);
    if(d < -0.999999){
        /* opposite: rotate 180 about any axis perpendicular to a */
        double ax[3]; double tmp[3]={1,0,0}; v3_cross(ax,tmp,a);
        if(v3_norm2(ax)<1e-12){ double t2[3]={0,1,0}; v3_cross(ax,t2,a); }
        v3_normalize(ax,ax);
        q[0]=ax[0]; q[1]=ax[1]; q[2]=ax[2]; q[3]=0.0; return;
    }
    q[0]=c[0]; q[1]=c[1]; q[2]=c[2]; q[3]=1.0+d;
    q_normalize(q);
}
/* Rotation angle (rad) of a quaternion from identity. */
BL_HD static inline double q_angle(const double q[4]){
    double w=fabs(q[3]); if(w>1.0)w=1.0; return 2.0*acos(w);
}

#endif /* BL_VMATH_H */
