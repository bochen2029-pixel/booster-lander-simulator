/* guidance_mppi_cuda.h — public interface to the M5 CUDA MPPI rollout port.
 * All functions are extern "C" (defined in guidance_mppi_cuda.cu). The CPU path (guidance_mppi.c)
 * is untouched; --mppi-cuda routes GM_MPPI full-solves through mppi_step_cuda instead of mppi_step. */
#ifndef BL_GUIDANCE_MPPI_CUDA_H
#define BL_GUIDANCE_MPPI_CUDA_H

#include "state.h"
#include "guidance.h"
#include "guidance_mppi.h"
#include "dynamics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate persistent device buffers for K rollouts. Idempotent; re-inits on K change.
 * Returns 0 on success, -1 if no CUDA device / alloc failure. */
int  mppi_cuda_init(int K);
void mppi_cuda_shutdown(void);

/* PARITY PRIMITIVE: compute all K rollout costs on the GPU into outC[K]. Uploads st/env/ubar.
 * warm-started ubar[H][NCH] and ignite_h come from the caller (identical to the CPU replan). */
int  mppi_cuda_rollout_costs(uint32_t seed, uint32_t replan, double ignite_h,
                             double gamma, double m0,
                             const State* st, const EnvCtx* env,
                             const double ubar[MPPI_H][MPPI_NCH],
                             double* outC, int K);

/* FULL GPU SOLVE: K-loop + numerator reduction on the device; beta/lambda/weights/update on host
 * (byte-identical to mppi_step). In/out ubar is updated in place. Returns 0 on success. */
int  mppi_cuda_solve(uint32_t seed, uint32_t replan, double ignite_h, double gamma,
                     double m0, const State* st, const EnvCtx* env,
                     double ubar[MPPI_H][MPPI_NCH], double lambda_in,
                     double* lambda_out, double* ess_out, double* beta_out, int K);

/* Drop-in replacement for mppi_step (sim.c routes here under --mppi-cuda). Runs the warm-start on
 * the host (same as CPU), solves on the GPU, then calls mppi_execute (CPU, unchanged). */
void mppi_step_cuda(MppiState* M, const State* st, const EnvCtx* env, GuidanceCmd* g);

/* CPU-REFERENCE rollout costs (host compilation of the shared rollout source; parity oracle §9.5). */
int  mppi_cpuref_rollout_costs(uint32_t seed, uint32_t replan, double ignite_h,
                               double gamma, double m0,
                               const State* st, const EnvCtx* env,
                               const double ubar[MPPI_H][MPPI_NCH],
                               double* outC, int K);

/* Shims exposing the shared warm-start math to C (the mr_* live in the .cuh). */
double mppi_cuda_compute_ignite_h(const State* st);
void   mppi_cuda_warm_start(double ubar[MPPI_H][MPPI_NCH], double ignite_h,
                            const State* st, const EnvCtx* env);

/* GPU-EVENT kernel-only benchmark: cudaEvent-timed device cost (rollout / reduction / total) over
 * `iters` samples, immune to host CPU contention. Fills roll_ms/num_ms/tot_ms[iters]. Returns 0 ok. */
int mppi_cuda_bench_kernel(uint32_t seed, uint32_t replan, double ignite_h, double gamma,
                           double m0, const State* st, const EnvCtx* env,
                           const double ubar[MPPI_H][MPPI_NCH], int K, int iters,
                           double* roll_ms, double* num_ms, double* tot_ms);

#ifdef __cplusplus
}
#endif

#endif /* BL_GUIDANCE_MPPI_CUDA_H */
