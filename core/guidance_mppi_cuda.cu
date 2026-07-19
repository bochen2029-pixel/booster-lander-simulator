/* guidance_mppi_cuda.cu — M5 CUDA MPPI rollout port (sm_89). See runs/agentB_mppi_design.md §5.
 *
 * WHAT MOVES TO THE GPU: the K-loop of mppi_step — for each of K rollouts, regenerate the OU noise
 * (Philox, per-(rollout,replan,t,ch) counter — IDENTICAL scheme to the CPU) and run rollout_cost
 * (the shared BL_HD kernel in guidance_mppi_rollout.cuh, extracted verbatim from guidance_mppi.c).
 * The warm-start, the beta=min reduction, the ESS->lambda bisection, the softmax weights, the
 * Savitzky-Golay smoothing, the clamp, and mppi_execute all stay on the HOST, byte-identical to the
 * CPU path. Only rollout costs + the numerator reduction are offloaded.
 *
 * DETERMINISM (house rules + design §5):
 *  - sm_89, -fmad=false (set on the nvcc line), no --use_fast_math, NO float atomics.
 *  - K1: 1 thread/rollout; each thread's cost is a pure function of its Philox coordinates -> the
 *    C_k array is bit-identical run-to-run on the same GPU regardless of scheduling.
 *  - K2 numerator Sum_k W[k]*eps_k[t][c]: a FIXED-TOPOLOGY block reduction (warp-shuffle + one
 *    shared exchange, orrery lib/reduce.cuh shape) then a fixed serial fold over the block partials
 *    in ascending block index. No atomics -> bit-stable on this arch.
 *  - PER-ARCH determinism (same GPU+seed -> byte-identical) is the hard gate. CPU<->GPU parity is
 *    TOLERANCED (design §9.5): fp64 libm differs by ~1 ULP between MSVC and CUDA, so we report
 *    max|Delta cost| and top-64 rank agreement rather than bit-equality.
 *
 * Physics (dynamics_deriv/control_step/rk4_step/atmo_eval/mass_props/lowest_point_z) is compiled
 * from the SAME .c translation units via the unity #includes below, so nvcc emits __host__
 * __device__ code from byte-identical source (directive 7).
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cuda_runtime.h>

extern "C" {
#include "state.h"
#include "guidance.h"
#include "guidance_mppi.h"
#include "guidance_hoverslam.h"
#include "dynamics.h"
#include "control.h"
#include "integrator.h"
#include "contact.h"
#include "atmosphere.h"
#include "constants.h"
#include "scenario.h"
}
#include "rng.h"

/* ---- unity build: compile the plant physics as __host__ __device__ from the SAME source ---- */
/* These .c files are all BL_HD (state changes only through these), zero external deps. Including
 * them here makes nvcc emit device versions from byte-identical source (directive 7). They are also
 * compiled by the C target for the CPU exe; here we get an independent device compilation. To avoid
 * ODR/link collisions with the C objects, this .cu is the ONLY CUDA TU and links against the C
 * objects too — so we compile these as STATIC (file-local) here by wrapping in a namespace-free TU
 * that nvcc treats as its own object; the C exe's objects are separate. Symbols are identical bodies
 * so any linker de-dup is benign. (Verified: single-exe build links clean.) */
extern "C" {
#include "atmosphere.c"
#include "dynamics.c"
#include "integrator.c"
#include "contact.c"
#include "control.c"
}

/* the shared rollout math (verbatim from guidance_mppi.c, BL_HD) */
#include "guidance_mppi_rollout.cuh"

/* ============================ CUDA error helper ============================ */
#define CUDA_OK(call) do{ cudaError_t _e=(call); if(_e!=cudaSuccess){ \
    fprintf(stderr,"[cuda] %s:%d %s -> %s\n",__FILE__,__LINE__,#call,cudaGetErrorString(_e)); \
    return -1; } }while(0)

/* ============================ device-side driving-noise scale ============================
 * drive[c] = sigma_c * sqrt(2*theta - theta^2). Computed identically to mppi_step. Passed to the
 * kernels so the OU recurrence matches the CPU bit-for-bit (up to libm on the sqrt below, which is
 * the SAME expression the CPU evaluates -> host/device differ by <=1 ULP, the documented tolerance). */
struct MppiCudaCfg {
    uint32_t seed;
    uint32_t replan;
    double   ignite_h;
    double   gamma;
    double   m0;
    double   drive[MPPI_NCH];
};

/* ============================ K1: rollout cost ============================
 * One thread per rollout k. Regenerate this rollout's OU noise into a local [H][NCH] buffer, then
 * run the verbatim rollout. Local memory: 200*3*8 = 4800 B/thread (fits; spills to local, fine at
 * 128 threads/block on 16 GB). Writes C_k[k]. */
__global__ void rollout_kernel(const State* __restrict__ d_st0, const EnvCtx* __restrict__ d_env,
                               const double* __restrict__ d_ubar /*[H*NCH]*/,
                               MppiCudaCfg cfg, double* __restrict__ d_C, int K){
    int k = blockIdx.x*blockDim.x + threadIdx.x;
    if(k>=K) return;

    /* regenerate OU noise for this rollout (all channels) — local buffer */
    double eps[MPPI_H][MPPI_NCH];
    for(int c=0;c<MPPI_NCH;c++){
        double col[MPPI_H];
        mr_ou_channel(cfg.seed, cfg.replan, (uint32_t)k, c, cfg.drive[c], col);
        for(int t=0;t<MPPI_H;t++) eps[t][c]=col[t];
    }
    /* ubar as [H][NCH] view */
    const double (*ubar)[MPPI_NCH] = (const double(*)[MPPI_NCH])d_ubar;

    double Ck = mr_rollout_cost(cfg.ignite_h, d_st0, d_env, ubar, eps, cfg.gamma, cfg.m0);
    if(!isfinite(Ck)) Ck = MR_COST_CLIP;   /* CPU-parity hardening (D-009) */
    d_C[k] = Ck;
}

/* ============================ K2: weighted-eps numerator (one (t,ch) per launch-slice) ============
 * Compute num[t][c] = Sum_k W[k]*eps_k[t][c] for ALL (t,c) with a FIXED reduction order.
 * Layout: grid.y indexes the (t*NCH+c) output element (H*NCH of them); grid.x*block covers k.
 * Each block reduces its k-range with a deterministic warp-shuffle tree, writes ONE partial per
 * block to d_partial[elem][blockIdx.x]. A tiny second kernel folds the partials in ascending block
 * order (fixed serial sum) -> bit-stable. No atomics. */
__device__ __forceinline__ double blockReduceSumD(double v, double* sh){
    int lane=threadIdx.x&31, wid=threadIdx.x>>5, nw=(blockDim.x+31)>>5;
    for(int o=16;o>0;o>>=1) v+=__shfl_down_sync(0xffffffffu,v,o);
    if(lane==0) sh[wid]=v; __syncthreads();
    double r=0.0;
    if(wid==0){ r=(lane<nw)?sh[lane]:0.0; for(int o=16;o>0;o>>=1) r+=__shfl_down_sync(0xffffffffu,r,o); if(lane==0) sh[0]=r; }
    __syncthreads(); return sh[0];
}

__global__ void numer_partial_kernel(const State* __restrict__ d_st0 /*unused, kept for symmetry*/,
                                     MppiCudaCfg cfg, const double* __restrict__ d_W,
                                     double* __restrict__ d_partial /*[H*NCH][gridDim.x]*/,
                                     int K, int nblk){
    (void)d_st0;
    int elem = blockIdx.y;            /* which (t*NCH + c) */
    int t = elem / MPPI_NCH;
    int c = elem % MPPI_NCH;
    int k = blockIdx.x*blockDim.x + threadIdx.x;

    double val = 0.0;
    if(k<K){
        /* regenerate ONLY channel c up to step t (cheap: O(t)); need eps_k[t][c] and W[k]. */
        double prev=0.0, e=0.0;
        for(int tt=0; tt<=t; tt++){
            double n = rng_normal(cfg.seed, RNG_MPPI, cfg.replan, (uint32_t)(tt*MPPI_NCH+c), (uint32_t)k);
            e = (1.0-MR_OU_THETA)*prev + cfg.drive[c]*n;
            prev = e;
        }
        val = d_W[k]*e;
    }
    extern __shared__ double sh[];
    double bs = blockReduceSumD(val, sh);
    if(threadIdx.x==0) d_partial[(size_t)elem*nblk + blockIdx.x] = bs;
}

/* fold block partials in ascending block index (fixed serial order) -> num[elem]. */
__global__ void numer_fold_kernel(const double* __restrict__ d_partial, double* __restrict__ d_num,
                                  int nblk){
    int elem = blockIdx.x*blockDim.x + threadIdx.x;
    if(elem >= MPPI_H*MPPI_NCH) return;
    double s=0.0;
    for(int b=0;b<nblk;b++) s += d_partial[(size_t)elem*nblk + b];
    d_num[elem]=s;
}

/* ============================ persistent device buffers ============================ */
static State*  d_st0   = nullptr;
static EnvCtx* d_env   = nullptr;
static double* d_ubar  = nullptr;   /* [H*NCH] */
static double* d_C     = nullptr;   /* [K] */
static double* d_W     = nullptr;   /* [K] */
static double* d_partial = nullptr; /* [H*NCH * nblk] */
static double* d_num   = nullptr;   /* [H*NCH] */
static int     g_K     = 0;
static int     g_inited= 0;
static int     g_nblk  = 0;

/* host mirror of C_k / W_k (malloc'd at init to size K) */
static double* hC_buf = nullptr;

extern "C" int mppi_cuda_init(int K){
    if(g_inited && g_K==K) return 0;
    if(g_inited){ /* re-init with new K */
        cudaFree(d_C); cudaFree(d_W); cudaFree(d_partial); free(hC_buf);
        d_C=d_W=d_partial=nullptr; hC_buf=nullptr;
    } else {
        cudaError_t e=cudaFree(0); if(e!=cudaSuccess){ fprintf(stderr,"[cuda] no device: %s\n",cudaGetErrorString(e)); return -1; }
        CUDA_OK(cudaMalloc(&d_st0, sizeof(State)));
        CUDA_OK(cudaMalloc(&d_env, sizeof(EnvCtx)));
        CUDA_OK(cudaMalloc(&d_ubar, sizeof(double)*MPPI_H*MPPI_NCH));
        CUDA_OK(cudaMalloc(&d_num,  sizeof(double)*MPPI_H*MPPI_NCH));
    }
    g_K = K;
    int block = 128;
    g_nblk = (K + block - 1)/block;
    CUDA_OK(cudaMalloc(&d_C, sizeof(double)*K));
    CUDA_OK(cudaMalloc(&d_W, sizeof(double)*K));
    CUDA_OK(cudaMalloc(&d_partial, sizeof(double)*(size_t)MPPI_H*MPPI_NCH*g_nblk));
    hC_buf = (double*)malloc(sizeof(double)*K);
    g_inited = 1;
    return 0;
}

/* Compute all K rollout costs on the GPU into host buffer outC[K]. Also uploads st/env/ubar.
 * Returns 0 on success. This is the parity/perf primitive. */
extern "C" int mppi_cuda_rollout_costs(uint32_t seed, uint32_t replan, double ignite_h,
                                       double gamma, double m0,
                                       const State* st, const EnvCtx* env,
                                       const double ubar[MPPI_H][MPPI_NCH],
                                       double* outC, int K){
    if(mppi_cuda_init(K)!=0) return -1;
    MppiCudaCfg cfg; cfg.seed=seed; cfg.replan=replan; cfg.ignite_h=ignite_h; cfg.gamma=gamma; cfg.m0=m0;
    for(int c=0;c<MPPI_NCH;c++){
        double sig = (c==0)?MR_SIG_THR:MR_SIG_ALAT;
        cfg.drive[c] = sig*sqrt(2.0*MR_OU_THETA - MR_OU_THETA*MR_OU_THETA);
    }
    CUDA_OK(cudaMemcpy(d_st0, st, sizeof(State), cudaMemcpyHostToDevice));
    CUDA_OK(cudaMemcpy(d_env, env, sizeof(EnvCtx), cudaMemcpyHostToDevice));
    CUDA_OK(cudaMemcpy(d_ubar, ubar, sizeof(double)*MPPI_H*MPPI_NCH, cudaMemcpyHostToDevice));
    int block=128, grid=(K+block-1)/block;
    rollout_kernel<<<grid,block>>>(d_st0,d_env,d_ubar,cfg,d_C,K);
    CUDA_OK(cudaGetLastError());
    CUDA_OK(cudaMemcpy(outC, d_C, sizeof(double)*K, cudaMemcpyDeviceToHost));
    CUDA_OK(cudaDeviceSynchronize());
    return 0;
}

/* Full GPU replan: mirrors mppi_step but runs the K-loop + numerator reduction on the device.
 * Everything else (warm-start via the passed ubar, beta, lambda servo, weights, SGF, clamp) is on
 * the host, byte-identical to the CPU. The caller (mppi_step_cuda) supplies the warm-started ubar
 * and receives the updated ubar. Returns 0 on success. Fills telemetry via out-params. */
extern "C" int mppi_cuda_solve(uint32_t seed, uint32_t replan, double ignite_h, double gamma,
                               double m0, const State* st, const EnvCtx* env,
                               double ubar[MPPI_H][MPPI_NCH], double lambda_in,
                               double* lambda_out, double* ess_out, double* beta_out, int K){
    if(mppi_cuda_init(K)!=0) return -1;
    MppiCudaCfg cfg; cfg.seed=seed; cfg.replan=replan; cfg.ignite_h=ignite_h; cfg.gamma=gamma; cfg.m0=m0;
    for(int c=0;c<MPPI_NCH;c++){
        double sig=(c==0)?MR_SIG_THR:MR_SIG_ALAT;
        cfg.drive[c]=sig*sqrt(2.0*MR_OU_THETA - MR_OU_THETA*MR_OU_THETA);
    }
    CUDA_OK(cudaMemcpy(d_st0, st, sizeof(State), cudaMemcpyHostToDevice));
    CUDA_OK(cudaMemcpy(d_env, env, sizeof(EnvCtx), cudaMemcpyHostToDevice));
    CUDA_OK(cudaMemcpy(d_ubar, ubar, sizeof(double)*MPPI_H*MPPI_NCH, cudaMemcpyHostToDevice));

    int block=128, grid=(K+block-1)/block;
    rollout_kernel<<<grid,block>>>(d_st0,d_env,d_ubar,cfg,d_C,K);
    CUDA_OK(cudaGetLastError());
    CUDA_OK(cudaMemcpy(hC_buf, d_C, sizeof(double)*K, cudaMemcpyDeviceToHost));
    CUDA_OK(cudaDeviceSynchronize());

    /* ---- host: beta = min_k C_k (CPU-parity: serial min over ascending k) ---- */
    double beta = hC_buf[0];
    for(int k=1;k<K;k++) if(hC_buf[k]<beta) beta=hC_buf[k];

    /* ---- host: ESS-servo lambda (bisection) — VERBATIM from mppi_step ---- */
    double lam_lo=LAMBDA_MIN_C, lam_hi=LAMBDA_MAX_C, lam=lambda_in;
    if(lam<lam_lo)lam=lam_lo; if(lam>lam_hi)lam=lam_hi;
    double ess_target_lo=ESS_LO_FRAC_C*K, ess_target_hi=ESS_HI_FRAC_C*K;
    for(int it=0;it<40;it++){
        double s1=0.0,s2=0.0;
        for(int k=0;k<K;k++){ double w=exp(-(hC_buf[k]-beta)/lam); s1+=w; s2+=w*w; }
        double ess=(s2>1e-300)?(s1*s1/s2):1.0;
        if(ess<ess_target_lo) lam_lo=lam;
        else if(ess>ess_target_hi) lam_hi=lam;
        else break;
        lam=0.5*(lam_lo+lam_hi);
    }

    /* ---- host: weights W[k] = exp(-(C-beta)/lam); eta ---- */
    double eta=0.0;
    for(int k=0;k<K;k++){ double w=exp(-(hC_buf[k]-beta)/lam); hC_buf[k]=w; eta+=w; }  /* reuse buffer as W */
    double inv_eta=(eta>1e-300)?1.0/eta:0.0;
    CUDA_OK(cudaMemcpy(d_W, hC_buf, sizeof(double)*K, cudaMemcpyHostToDevice));

    /* ---- GPU: numerator num[t][c] = Sum_k W[k]*eps_k[t][c] (fixed-order reduction) ---- */
    int nblk=(K+block-1)/block;
    dim3 g2(nblk, MPPI_H*MPPI_NCH);
    size_t shmem = ((block+31)/32)*sizeof(double);
    numer_partial_kernel<<<g2,block,shmem>>>(d_st0,cfg,d_W,d_partial,K,nblk);
    CUDA_OK(cudaGetLastError());
    int fold_block=256, fold_grid=(MPPI_H*MPPI_NCH+fold_block-1)/fold_block;
    numer_fold_kernel<<<fold_grid,fold_block>>>(d_partial,d_num,nblk);
    CUDA_OK(cudaGetLastError());
    static double h_num[MPPI_H*MPPI_NCH];
    CUDA_OK(cudaMemcpy(h_num, d_num, sizeof(double)*MPPI_H*MPPI_NCH, cudaMemcpyDeviceToHost));
    CUDA_OK(cudaDeviceSynchronize());

    /* ---- host: ubar += inv_eta*num ; then SGF + clamp (VERBATIM from mppi_step) ---- */
    for(int t=0;t<MPPI_H;t++)
        for(int c=0;c<MPPI_NCH;c++)
            ubar[t][c] += inv_eta*h_num[t*MPPI_NCH+c];

    /* telemetry */
    { double s1=0.0,s2=0.0; for(int k=0;k<K;k++){ s1+=hC_buf[k]; s2+=hC_buf[k]*hC_buf[k]; }
      *ess_out=(s2>1e-300)?s1*s1/s2:0.0; }
    *lambda_out=lam; *beta_out=beta;
    return 0;
}

extern "C" void mppi_cuda_shutdown(void){
    if(!g_inited) return;
    cudaFree(d_st0); cudaFree(d_env); cudaFree(d_ubar); cudaFree(d_C); cudaFree(d_W);
    cudaFree(d_partial); cudaFree(d_num); free(hC_buf);
    d_st0=nullptr; d_env=nullptr; d_ubar=nullptr; d_C=nullptr; d_W=nullptr; d_partial=nullptr; d_num=nullptr; hC_buf=nullptr;
    g_inited=0; g_K=0;
}

/* ============================ GPU-EVENT KERNEL BENCH (contention-immune) ============================
 * cudaEvent timing brackets ONLY the device rollout_kernel + numerator kernels — this measures the
 * true fp64 device cost, immune to host-side CPU scheduling (the 96-thread fleet oversubscription that
 * inflates the end-to-end QPC latency). Reports, over `iters` samples at a given K:
 *   k_roll_ms  = device time for rollout_kernel alone (the fp64-ALU-bound K-loop; the 6ms-gate driver)
 *   k_num_ms   = device time for numer_partial + numer_fold (the fixed-order reduction)
 *   k_tot_ms   = device time for the full GPU portion (rollout + reduction), events only
 * Fills the caller's arrays (size iters) so the host can compute p50/p99. H2D of st/env/ubar is done
 * ONCE up front (K-independent, ~2KB) and excluded — it is a fixed ~microsecond cost, not K-scaling. */
extern "C" int mppi_cuda_bench_kernel(uint32_t seed, uint32_t replan, double ignite_h, double gamma,
                                      double m0, const State* st, const EnvCtx* env,
                                      const double ubar[MPPI_H][MPPI_NCH], int K, int iters,
                                      double* roll_ms, double* num_ms, double* tot_ms){
    if(mppi_cuda_init(K)!=0) return -1;
    MppiCudaCfg cfg; cfg.seed=seed; cfg.replan=replan; cfg.ignite_h=ignite_h; cfg.gamma=gamma; cfg.m0=m0;
    for(int c=0;c<MPPI_NCH;c++){ double sig=(c==0)?MR_SIG_THR:MR_SIG_ALAT;
        cfg.drive[c]=sig*sqrt(2.0*MR_OU_THETA - MR_OU_THETA*MR_OU_THETA); }
    CUDA_OK(cudaMemcpy(d_st0, st, sizeof(State), cudaMemcpyHostToDevice));
    CUDA_OK(cudaMemcpy(d_env, env, sizeof(EnvCtx), cudaMemcpyHostToDevice));
    CUDA_OK(cudaMemcpy(d_ubar, ubar, sizeof(double)*MPPI_H*MPPI_NCH, cudaMemcpyHostToDevice));
    /* a plausible W (uniform) so the numerator kernel does representative work */
    { double one=1.0; for(int k=0;k<K;k++) hC_buf[k]=one; CUDA_OK(cudaMemcpy(d_W,hC_buf,sizeof(double)*K,cudaMemcpyHostToDevice)); }

    int block=128, grid=(K+block-1)/block, nblk=grid;
    dim3 g2(nblk, MPPI_H*MPPI_NCH);
    size_t shmem=((block+31)/32)*sizeof(double);
    int fold_block=256, fold_grid=(MPPI_H*MPPI_NCH+fold_block-1)/fold_block;

    cudaEvent_t e0,e1,e2; CUDA_OK(cudaEventCreate(&e0)); CUDA_OK(cudaEventCreate(&e1)); CUDA_OK(cudaEventCreate(&e2));
    /* warm up */
    for(int w=0;w<5;w++){ rollout_kernel<<<grid,block>>>(d_st0,d_env,d_ubar,cfg,d_C,K); }
    CUDA_OK(cudaDeviceSynchronize());
    for(int it=0; it<iters; it++){
        CUDA_OK(cudaEventRecord(e0));
        rollout_kernel<<<grid,block>>>(d_st0,d_env,d_ubar,cfg,d_C,K);
        CUDA_OK(cudaEventRecord(e1));
        numer_partial_kernel<<<g2,block,shmem>>>(d_st0,cfg,d_W,d_partial,K,nblk);
        numer_fold_kernel<<<fold_grid,fold_block>>>(d_partial,d_num,nblk);
        CUDA_OK(cudaEventRecord(e2));
        CUDA_OK(cudaEventSynchronize(e2));
        float ms_roll=0, ms_num=0, ms_tot=0;
        CUDA_OK(cudaEventElapsedTime(&ms_roll, e0, e1));
        CUDA_OK(cudaEventElapsedTime(&ms_num,  e1, e2));
        CUDA_OK(cudaEventElapsedTime(&ms_tot,  e0, e2));
        roll_ms[it]=ms_roll; num_ms[it]=ms_num; tot_ms[it]=ms_tot;
    }
    cudaEventDestroy(e0); cudaEventDestroy(e1); cudaEventDestroy(e2);
    return 0;
}

/* ============================ CPU-REFERENCE rollout costs (host, shared header) ============================
 * Runs mr_rollout_cost (the __host__ compilation of the SAME shared source the device runs) for all
 * K rollouts, regenerating the SAME OU noise. This is the parity oracle of design §9.5: CPU-ref
 * (__host__) vs GPU (__device__), same source, tolerance-checked. Because the shared header is a
 * verbatim extraction of guidance_mppi.c, these ALSO equal the production CPU MPPI's rollout costs. */
extern "C" int mppi_cpuref_rollout_costs(uint32_t seed, uint32_t replan, double ignite_h,
                                         double gamma, double m0,
                                         const State* st, const EnvCtx* env,
                                         const double ubar[MPPI_H][MPPI_NCH],
                                         double* outC, int K){
    double drive[MPPI_NCH];
    for(int c=0;c<MPPI_NCH;c++){
        double sig=(c==0)?MR_SIG_THR:MR_SIG_ALAT;
        drive[c]=sig*sqrt(2.0*MR_OU_THETA - MR_OU_THETA*MR_OU_THETA);
    }
    for(int k=0;k<K;k++){
        double eps[MPPI_H][MPPI_NCH];
        for(int c=0;c<MPPI_NCH;c++){
            double col[MPPI_H];
            mr_ou_channel(seed, replan, (uint32_t)k, c, drive[c], col);
            for(int t=0;t<MPPI_H;t++) eps[t][c]=col[t];
        }
        double Ck = mr_rollout_cost(ignite_h, st, env, ubar, eps, gamma, m0);
        if(!isfinite(Ck)) Ck=MR_COST_CLIP;
        outC[k]=Ck;
    }
    return 0;
}

/* C-callable shims so main.c (C) can build the warm-start with the SAME shared math the GPU uses
 * (mr_* live in the .cuh, unreachable from C). Used by the --mppi-cuda-verify capture in main.c. */
extern "C" double mppi_cuda_compute_ignite_h(const State* st){ return mr_compute_ignite_h(st); }
extern "C" void   mppi_cuda_warm_start(double ubar[MPPI_H][MPPI_NCH], double ignite_h,
                                       const State* st, const EnvCtx* env){
    mr_warm_start_nominal(ubar, ignite_h, st, env);
}

/* ============================ mppi_step_cuda — drop-in for mppi_step ============================
 * Host-side prologue (compute_ignite_h + warm_start_nominal) is byte-identical to the CPU (shared
 * verbatim math), then the K-loop + numerator reduction run on the GPU, then SGF+clamp on host, then
 * mppi_execute (CPU, unchanged) emits the knot. The ONLY numerical difference vs the CPU --mppi path
 * is: (a) rollout costs computed by CUDA libm (~1 ULP), and (b) the numerator reduced in a different
 * (but fixed, deterministic) order. Both are within the design §9.5 tolerance; the executed plan is
 * then shaped by the same clamp/SGF and, crucially, the EXECUTION uses hoverslam_step (identical). */
extern "C" void mppi_step_cuda(MppiState* M, const State* st, const EnvCtx* env, GuidanceCmd* g){
    if(!M->inited) mppi_init(M, M->seed, M->scenario);

    MassProps mp0; mass_props(st->y[S_MLOX],st->y[S_MRP1],0,0,&mp0);
    double m0 = mp0.m;

    /* ignition altitude (once per replan) — verbatim compute_ignite_h */
    M->ignite_h = mr_compute_ignite_h(st);

    /* warm-start the mean (verbatim warm_start_nominal) */
    mr_warm_start_nominal(M->ubar, M->ignite_h, st, env);

    double gamma = MR_GAMMA_IS;
    double lam_out=M->lambda, ess_out=0.0, beta_out=0.0;
    int rc = mppi_cuda_solve(M->seed, M->replan, M->ignite_h, gamma, m0, st, env,
                             M->ubar, M->lambda, &lam_out, &ess_out, &beta_out, MPPI_K);
    if(rc!=0){
        /* CUDA failed mid-run: fall back to the CPU replan so the sim still completes honestly. */
        fprintf(stderr,"[mppi-cuda] solve failed (rc=%d) at replan %u — falling back to CPU mppi_step\n",
                rc, M->replan);
        mppi_step(M, st, env, g);   /* CPU path (guidance_mppi.c) */
        return;
    }
    M->lambda = lam_out;

    /* ---- Savitzky-Golay smooth + clamp (VERBATIM from mppi_step) ---- */
    {
        double col[MPPI_H];
        for(int c=0;c<MPPI_NCH;c++){
            for(int t=0;t<MPPI_H;t++) col[t]=M->ubar[t][c];
            mr_sgf_smooth(col, MPPI_H);
            for(int t=0;t<MPPI_H;t++) M->ubar[t][c]=col[t];
        }
        for(int t=0;t<MPPI_H;t++){
            M->ubar[t][0] = mr_clampd(M->ubar[t][0], 0.0, 1.0);
            for(int c=1;c<MPPI_NCH;c++)
                M->ubar[t][c] = mr_clampd(M->ubar[t][c], -MR_A_LAT_GAMUT, MR_A_LAT_GAMUT);
        }
    }

    M->ess = ess_out; M->beta = beta_out; M->cmin = beta_out;
    M->replan++;

    /* emit the first knot + shift (CPU mppi_execute, unchanged — uses hoverslam_step) */
    mppi_execute(M, st, g);
}
