/* rng.h — Philox4x32-10 counter-based RNG, host/device identical.
 * Keyed by (master_seed, stream_id); counter by (lo, hi, lane, sub).
 * No state arrays -> trivially deterministic and parallel. See CLAUDE_v1.md §9.6.
 */
#ifndef BL_RNG_H
#define BL_RNG_H

#include <stdint.h>
#include <math.h>
#include "vmath.h"

/* Stream ids */
enum { RNG_WIND=1, RNG_DISPERSION=2, RNG_MPPI=3, RNG_NAV=4 };

BL_HD static inline void philox_mulhilo(uint32_t a, uint32_t b, uint32_t* hi, uint32_t* lo){
    uint64_t p = (uint64_t)a * (uint64_t)b;
    *hi = (uint32_t)(p >> 32);
    *lo = (uint32_t)p;
}

/* One block of 4x uint32 from a 128-bit counter and 64-bit key. */
BL_HD static inline void philox4x32_10(const uint32_t ctr_in[4], const uint32_t key_in[2], uint32_t out[4]){
    const uint32_t M0=0xD2511F53u, M1=0xCD9E8D57u;
    const uint32_t W0=0x9E3779B9u, W1=0xBB67AE85u;
    uint32_t c0=ctr_in[0], c1=ctr_in[1], c2=ctr_in[2], c3=ctr_in[3];
    uint32_t k0=key_in[0], k1=key_in[1];
    for(int i=0;i<10;i++){
        uint32_t hi0,lo0,hi1,lo1;
        philox_mulhilo(M0,c0,&hi0,&lo0);
        philox_mulhilo(M1,c2,&hi1,&lo1);
        uint32_t n0 = hi1 ^ c1 ^ k0;
        uint32_t n1 = lo1;
        uint32_t n2 = hi0 ^ c3 ^ k1;
        uint32_t n3 = lo0;
        c0=n0; c1=n1; c2=n2; c3=n3;
        k0 += W0; k1 += W1;
    }
    out[0]=c0; out[1]=c1; out[2]=c2; out[3]=c3;
}

/* Convenience: 4 uint32 for (seed, stream, ctrlo, ctrhi, lane, sub). */
BL_HD static inline void rng_block(uint32_t seed, uint32_t stream, uint32_t ctrlo, uint32_t ctrhi,
                                   uint32_t lane, uint32_t sub, uint32_t out[4]){
    uint32_t ctr[4] = { ctrlo, ctrhi, lane, sub };
    uint32_t key[2] = { seed, stream };
    /* fold stream into counter too so different streams never alias */
    ctr[3] ^= stream * 0x85EBCA6Bu;
    philox4x32_10(ctr, key, out);
}

/* Uniform double in [0,1). */
BL_HD static inline double rng_u01(uint32_t u){ return (double)u * (1.0/4294967296.0); }

/* Two independent standard normals via Box-Muller from a philox block index. */
BL_HD static inline void rng_normal2(uint32_t seed, uint32_t stream, uint32_t ctrlo, uint32_t ctrhi,
                                     uint32_t lane, double* n0, double* n1){
    uint32_t o[4]; rng_block(seed,stream,ctrlo,ctrhi,lane,0u,o);
    double u1 = rng_u01(o[0]); double u2 = rng_u01(o[1]);
    if(u1 < 1e-300) u1 = 1e-300;
    double r = sqrt(-2.0*log(u1));
    double a = 6.283185307179586*u2;
    *n0 = r*cos(a); *n1 = r*sin(a);
}
/* Single standard normal (uses lane/sub addressing for uniqueness). */
BL_HD static inline double rng_normal(uint32_t seed, uint32_t stream, uint32_t ctrlo, uint32_t ctrhi, uint32_t lane){
    double a,b; rng_normal2(seed,stream,ctrlo,ctrhi,lane,&a,&b); return a;
}

#endif /* BL_RNG_H */
