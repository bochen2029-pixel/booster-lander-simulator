# M5 — CUDA MPPI rollout port (report)

**Agents:** cuda-mppi lane — `fqkrauvv` (port author, opus-4.8) then `mqfyhpon` = **CUDA-2** (successor:
cleared the exe-lock, ran all gates, added the GPU-event bench, filled this report). **Date:** 2026-07-18 night.
**Worktree:** `C:\Booster_Lander_Simulator\_cuda_wt\` (gitignored; the main session integrates).
**Hardware:** RTX 4070 Ti SUPER, 16 GB, compute 8.9 (sm_89), driver 610.47. CUDA 13.1.80, MSVC 19.44 host.

> STATUS: gates run. Parity + determinism + latency are MEASURED. Two landed-rate batches were run under
> heavy fleet contention (6–8 concurrent MPPI batches on 16 cores). Every miss recorded honestly (directive 8/10).

---

## 0. What was ported and how

The K-loop of `mppi_step` (`core/guidance_mppi.c`): for each of K rollouts, sample the OU-colored
noise (Philox) and run `rollout_cost` (integrate the plant forward H=200 steps at MPPI_DT=25 ms under
`control_step`+`rk4_step`, event-terminate on ground, accumulate the cost). At K=256 this is the
per-replan hot loop; the design targets K=16384.

**Directive 7 (one dynamics source) — the load-bearing decision.** The plant physics
(`dynamics_deriv`, `control_step`, `rk4_step`, `atmo_eval`, `mass_props`, `engine_thrust`,
`lowest_point_z`, all of `vmath.h`) are already `BL_HD` and fp64. The `.cu` **unity-`#include`s the
plant `.c` translation units** so `nvcc` emits `__host__ __device__` code from **byte-identical
source**. No physics is reimplemented. To make the file-local `static` helpers + `static const`
aero/atmo tables device-callable, they were tagged `BL_HD` / moved to function-local `static const`
in the WORKTREE copies only — a **qualifier-only, value-identical change** (verified below: TERMINAL
194/200 byte-exact, so the CPU exe rebuilds identical).

The MPPI-specific rollout math (`converging_vdes`, `feas_margin`, `predict_tgo`, `cmd_from_u_lean`,
`rollout_cost`, `warm_start_nominal`, `compute_ignite_h`, `sgf_smooth`, the OU recurrence) is
extracted **verbatim** from `guidance_mppi.c` into a shared `BL_HD` header
`core/guidance_mppi_rollout.cuh`, `#include`d by the `.cu`. The CPU production path
(`guidance_mppi.c`) keeps its own copy UNCHANGED, so `--mppi` without `--mppi-cuda` is byte-identical.
The D-012 KDIV overspeed-brake schedule is present and mirrored (directive 7): `mr_cmd_from_u_lean`
computes the same `KDIV_SEEK/BRAKE/VBLEND` state-adaptive gain from the rollout state.

**What stays on the host** (byte-identical to the CPU `mppi_step`): the warm-start, `beta=min_k C_k`,
the ESS→λ bisection, the softmax weights `W[k]`, the Savitzky-Golay smoothing, the clamp, and
`mppi_execute` (which uses the real `hoverslam_step` for the vertical channel). Only the rollout costs
and the numerator reduction `Σ_k W[k]·ε_k[t][c]` are offloaded.

Flag: `--mppi-cuda` (routes GM_MPPI full-solves to the GPU; CPU path is default when absent).
Harnesses: `--mppi-cuda-verify` (cost parity + run-twice determinism + end-to-end p99 latency vs K);
`--mppi-cuda-bench` (**cudaEvent GPU-device-only** latency — contention-immune, added by CUDA-2 to
separate the true fp64 device cost from host-side CPU-scheduling noise under the 96-thread fleet).

---

## 1. PRECISION-ARCHITECTURE DECISION — **[A] fp64 everywhere** (chosen + justified)

The mission posed three options: **[A]** fp64 everywhere · **[B]** fp32 rollout + fp64 cost/reduction
· **[C]** fp32 throughout. **Decision: [A] fp64 everywhere.** Rationale, in priority order:

1. **Directive 7 is sacred and forbids [B]/[C].** "One dynamics source: plant, predictors, and MPPI
   rollouts share the same EOM including behavior changes." An fp32 rollout would be a **second,
   divergent dynamics source** — either a hand-maintained fp32 copy of `dynamics_deriv/control_step/
   rk4_step` (a directive-7 violation that D-012's leak-catch discipline exists specifically to
   prevent) or a templated `float/double` plant (a large, error-prone refactor of the entire frozen
   physics core). The unity-`#include` of the fp64 `.c` is the *only* way to guarantee the rollout runs
   byte-identical EOM to the plant.
2. **Parity is only meaningful against what actually flies.** The shipped controller (`guidance_mppi.c`)
   integrates the rollout in **fp64**. The design doc's §2 "fp32 planner" note is **explicitly
   superseded** by the shipped implementation (documented in `guidance_mppi_rollout.cuh` header). A
   toleranced host↔device parity check (design §9.5) is only interpretable if both sides compute the
   same fp64 reference; an fp32 device rollout would diverge from the fp64 CPU MPPI by *dynamics* error,
   not just libm ULPs, making the parity gate meaningless.
3. **[A] achieves essentially perfect parity** (§3: max relative Δ = 8.7e-16 ≈ 1 ULP), which is the
   strongest possible evidence the port is a faithful GPU image of the CPU controller.
4. **The cost of [A] is latency, and the latency is measured honestly (§5).** fp64 on consumer sm_89 is
   ~1/64 of fp32 throughput; [A] misses the aspirational 6 ms gate. But — see §5 — it remains real-time
   viable at the K where added samples still help (vanilla MPPI saturates ~K=1–2k per the research lane),
   so the fp64 penalty does **not** cost landings. **Per-arch determinism (the MANDATORY gate) is met**
   (§4); host↔device parity is TOLERANCED per §9.5 and passes with room to spare.

> If a future gate demands hard-real-time p99≤6 ms at K=16384, the realistic path is **not** fp32 (which
> breaks directive 7) but **[B'] MPOPI iterate-per-replan** (e.g. K=4096×4 instead of K=16384×1) and/or a
> tightened reduction kernel (§5, §7) — recorded as future work, not attempted here.

---

## 2. Build incantation + gate results

CMake wiring (worktree `CMakeLists.txt` + `core/CMakeLists.txt`): `project(... C CUDA)`,
`CUDA_ARCHITECTURES 89`, static MSVC + static cudart, `-fmad=false` on the CUDA language only, NO
`--use_fast_math`. The plant `.c` files are compiled by the `.cu` (unity include), NOT the C target.

```powershell
$vc='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vc`" >nul 2>&1 && cmake -S C:\Booster_Lander_Simulator\_cuda_wt -B C:\Booster_Lander_Simulator\_cuda_wt\build -G `"Visual Studio 17 2022`" -A x64"
cmd /c "`"$vc`" >nul 2>&1 && cmake --build C:\Booster_Lander_Simulator\_cuda_wt\build --config Release"
$exe='C:\Booster_Lander_Simulator\_cuda_wt\build\bin\Release\booster-core.exe'
```

**Build status:** compiles + links CLEAN under `nvcc 13.1.80 -arch=sm_89 -fmad=false` (only two
suppressed unused-var warnings). ptxas: `rollout_kernel` 255 regs / 6800 B stack (368/656 B spill —
inherent to the fp64 RK4+aero cost machinery per thread, occupancy-capped, representative of the honest
fp64 rollout); `numer_partial_kernel` 46 regs / 96 B stack.

> **Historical note (the trap that killed 5 predecessors tonight):** the port compiled clean on the
> first pass but the LINK died with `LNK1104: cannot open booster-core.exe` — a stale `--headless`
> batch (this lane's own orphaned CPU-MPPI baseline, PID 23120) held the exe lock. CUDA-2 cleared that
> single orphan and the relink succeeded with **no recompile**. The gates below then ran.

| Gate | Requirement | Result |
|---|---|---|
| `--selftest` | `SELFTEST: PASS` | **PASS** ✅ |
| `--headless --scenario terminal --seed 42 --runs 200` | EXACTLY 194/200 | **194/200 = 97.0%** ✅ (15 PERFECT / 167 GOOD / 12 HARD / 0 TIPPED / 6 CRASHED off-pad — byte-exact vs D-012; CPU path proven untouched) |
| CPU `--mppi` AERO s42 x60 | 44/60 (bit-determinism vs D-012 headline) | **44/60 = 73.3%** ✅ (worktree bit-check; matches D-012 golden — off-pad 13/th 2/fuel 1, td_v 2.95, lat 14.37) |
| CUDA `--mppi-cuda` AERO s42 x60 | landed rate vs the 44/60 CPU baseline | **44/60 = 73.3%** ✅ (**identical** decomposition + td_v 2.95 + lat 14.37 → GPU path faithful end-to-end, §6) |

---

## 3. Parity (CPU-ref `__host__` vs GPU `__device__`) — design §9.5 — measured at K=256 AND K=16384

Captured at a real mid-descent replan: AERO_OFFSET seed=42 run=3 @step 6000 (h=7805 m, vz=−311 m/s,
lat=553 m, ignite_h=3499 m, replan=120, m0=35686 kg). CPU-ref (`__host__` compilation of the shared
`.cuh`) vs GPU (`__device__`), identical Philox OU noise, identical warm-started ūbar. **The K=16384 row
is a separate `-DMPPI_K=16384` build** (`build_k16384/`, selftest PASS) — parity at the design's TARGET K.

| metric | K=256 | **K=16384** | interpretation |
|---|---|---|---|
| cost range (cpu-ref min) | 4920.12 | 4663.05 | costs O(5000), well-conditioned |
| **max \|Δcost\|** | 4.55e-12 | **6.37e-12** | absolute; ~1e-15 relative to cost magnitude |
| mean \|Δcost\| | 5.44e-13 | 5.98e-13 | round-off floor, K-independent |
| **max relative Δ** | 8.72e-16 | **1.28e-15** | **≈ 1 ULP** — exactly the documented MSVC↔CUDA fp64 libm divergence (§9.5) |
| **top-16 rank agreement** | 100% (16/16) | **100% (16/16)** | the best rollouts are identical sets |
| **top-64 rank agreement** | 100% (64/64) | **100% (64/64)** | softmax weights the same set → executed plan matches |

**Verdict: PARITY GREEN at both K.** The port is a faithful fp64 image of the CPU controller; the only
difference is the last-ULP libm rounding, which does not perturb the rollout ranking at all — even at
K=16384 where the reduction spans a full 2¹⁴ tree. This is why the CUDA landed rate tracks the CPU rate (§6).

## 4. Per-arch determinism (GPU run-twice, same seed) — the MANDATORY hard gate — K=256 AND K=16384

Same GPU + same seed, two independent `mppi_cuda_rollout_costs` calls, `memcmp` of the full C_k[K]:

- K=256:   **C_k bit-identical run-twice = YES** (max\|Δ\| = 0). ✅
- **K=16384: C_k bit-identical run-twice = YES** (max\|Δ\| = 0). ✅ ← proven at the design target K.

Mechanism: K1 `rollout_kernel` — 1 thread/rollout, each cost a pure function of its Philox coordinates
(lane=k, counter=(replan, t·NCH+ch)), so scheduling-order-independent. K2 numerator — fixed-topology
warp-shuffle block reduction + fixed serial fold over block partials in ascending index; **no float
atomics**. `-fmad=false` pins fma contraction. K=16384 = 2¹⁴ is a perfect balanced tree for the fixed
reduction, so the bit-identity is exact at the target K. Per design §5's L2 (MPPI fixed-seed →
control-sequence determinism), this is the sm_89-pinned determinism layer, and it holds bit-exactly.

## 5. Replan latency vs K — the honest fp64 miss, measured two ways

**(a) GPU-DEVICE-ONLY** (cudaEvent brackets the kernels; contention-immune; the true fp64 floor).
Confirmed GPU-bound: nvidia-smi showed **85–94% GPU util** during the K=16384 phase.

| K | rollout p50 (ms) | rollout p99 | rollout max | reduce p50 | **total p99** |
|---|---|---|---|---|---|
| 256   | 42.63 | 42.71 | 42.71 | 3.64  | 46.35 |
| 1024  | 42.64 | 51.33 | 51.33 | 13.47 | 64.80 |
| 4096  | 42.85 | 42.92 | 42.92 | 53.64 | 96.56 |
| 8192  | 42.81 | 50.69 | 50.69 | 107.10 | 157.78 |
| 16384 | 84.86 | 84.97 | 84.97 | 214.03 | **298.99** |

**(b) END-TO-END** (`mppi_cuda_solve`, incl. H2D, host ESS-servo + weights, D2H, reduction) — measured
under **full fleet contention** (6 concurrent MPPI batches, 96 OpenMP threads on 16 cores); the
host-side portion is dominated by CPU scheduling stall, so these are an **upper bound**, not the fp64
cost:

| K | p50 (ms) | p90 | p99 | max |
|---|---|---|---|---|
| 256   | 91.1  | 100.0 | 104.1 | 104.1 |
| 1024  | 111.2 | 134.6 | 171.3 | 171.3 |
| 4096  | 138.7 | 226.8 | 238.5 | 238.5 |
| 8192  | 165.9 | 181.1 | 202.9 | 202.9 |
| 16384 | 331.6 | 361.6 | 432.0 | 432.0 |

**Perf gate: p99 ≤ 6 ms at K=16384. RESULT: MISS — honestly and by a lot.**
- Device-only rollout p99 @K16384 = **85.0 ms = ~14× over** the 6 ms gate.
- Device-only total (rollout+reduction) @K16384 = **299 ms = ~50× over**.
- Even at K=256 the device rollout p99 = 42.7 ms = **~7× over** — the fp64 rollout is *latency-bound*
  (flat ~42.6 ms K=256→8192: one wave of 255-register fp64 rollouts under-fills the GPU until ~K=8192),
  so no small-K setting clears 6 ms.

**This is exactly the fp64-on-sm_89 reality the research lane predicted** (`runs/mppi_research.md` §d:
"on sm_89 fp64 throughput is 1/64 of fp32 … if fp64@16384 blows the 6 ms budget, the MPOPI route or a
mixed-precision rollout is the fallback"). The 6 ms gate is **infeasible in fp64 on this GPU** — and fp64
is required by directive 7 (§1). **This is the central, honest finding of the M5 port.**

**The reframe that matters for the mission (not a gate-dodge — the operating truth):** the shipped
controller replans the MPPI full-solve at **10 Hz (MPPI_REPLAN_DECIM=5 on 50 Hz guidance) = a 100 ms
wall budget**, holding the plan between via the cheap `mppi_execute` knot-shift. Against the **real 100 ms
operating budget**, fp64 GPU-device-only is comfortably viable through **K≈1024** (total 65 ms) and
marginal at K≈2048; it exceeds 100 ms device-only only at K≥4096. Since vanilla MPPI **saturates at
K≈1–2k** (research §b; the kprobe lane's CPU K-sweep corroborates), **the fp64 port is real-time-viable
across the entire K range where extra samples still improve the plan.** The 6 ms design gate was written
for a hard-real-time/fp32 target; it is the wrong bar for this fp64-by-directive controller at 10 Hz.

> **Cross-lane convergence (independent corroboration).** The `kprobe` lane (CPU K-scaling) and
> `mppi-research` (variant/saturation theory) independently reached the same verdict from this report's
> GPU floor: *"the 6 ms design gate is dead, real budget = 100 ms/10 Hz, fp64 viable only to ~K1024
> (299 ms @16384) → the CUDA port should target K≈1024, NOT 16384 (unnecessary AND fp64-infeasible);
> AERO→90% needs a colored-noise/CoVO variant, not more K."* The three lanes agree: **K≈1024 is the
> operating point; capacity beyond it is fp64-infeasible here and, per the saturation result, would not
> buy landings anyway.** The M5 deliverable is therefore the *validated, deterministic, parity-exact
> fp64 GPU rollout at the useful K* — with the 16384 target formally re-scoped by measurement.

## 6. AERO s42 x60 landed rate — CUDA vs the 44/60 CPU baseline — **MATCH**

| path | landed | breakdown | landed means |
|---|---|---|---|
| CPU `--mppi` (D-012 golden; triple-confirmed by entry-mppi/decknull/kprobe lanes) | **44/60 = 73.3%** | GOOD 12 / HARD 32 / CRASHED 16; off-pad 13 / too-hard 2 / fuel 1 | td_v 2.95, lat 14.37 |
| **CUDA `--mppi-cuda`** (GPU rollouts, this port) | **44/60 = 73.3%** | **GOOD 12 / HARD 32 / CRASHED 16; off-pad 13 / too-hard 2 / fuel 1** | **td_v 2.95, lat 14.37, tilt 2.64, fuel 4553** |

**The CUDA path reproduces the CPU golden to every reported decimal** — not merely the rate (44/60) but
the exact fault decomposition (off-pad 13, too-hard 2, fuel-out 1), the exact touchdown-velocity mean
(2.95 m/s), and the exact landing-offset mean (14.37 m), across 60 dispersed seeds. The ~1-ULP libm cost
differences and the fixed-but-reordered numerator reduction **did not flip a single landing outcome**.
This is the definitive end-to-end proof that the GPU rollout is a faithful image of the CPU controller:
parity holds not just at the cost level (§3) but all the way through to landings.

**Both batches were run in this worktree and their summaries are line-for-line identical** (CPU `--mppi`
also 44/60, GOOD 12 / HARD 32 / CRASHED 16, off-pad 13 / too-hard 2 / fuel 1, td_v 2.95, lat 14.37,
tilt 2.64, fuel 4553 — matching the D-012 golden and the CUDA batch exactly). So the worktree's CPU path
is byte-identical to D-012 (as §2's TERMINAL 194 already established), and the CUDA path is byte-identical
to the worktree CPU path at the landing-outcome level. Directive-7 fidelity is proven from source to
touchdown. (Both ran under the peak fleet contention — CPU batch 42 min wall, CUDA batch 33 min, for a
job that is ~9 min solo; the rates are contention-invariant by determinism.)

---

## 7. Failures / limitations (honest ledger)

1. **p99 ≤ 6 ms gate: MISSED in fp64** (§5) — ~14× (rollout) to ~50× (total) over at K=16384; ~7× over
   even at K=256. Root cause = fp64 on sm_89 (1/64 fp32) + directive-7-mandated single fp64 dynamics
   source. Not fixable without abandoning fp64 (which breaks directive 7). **Viable at 10 Hz/100 ms to
   K≈1024–2048**, which covers the useful (pre-saturation) K range.
2. **`numer_partial_kernel` scales poorly** — 214 ms @K16384, the dominant term in the device total. It
   regenerates OU noise inline with an O(t) Philox loop **per (t,c) output element** → O(K·H²·NCH) work.
   The design (§5) calls for a fixed power-of-two pairwise reduction that *reuses* the rollout's already-
   generated ε rather than regenerating it. Fixing this (store ε per rollout in the K1 kernel and reduce
   in place, or a proper segmented pairwise tree) would cut the device total to ~roughly the rollout time
   (85 ms @K16384). Still over 6 ms, but it removes the reduction from the critical path. **Future work.**
3. **Latency end-to-end numbers (§5b) are contention-inflated** and reported only as an upper bound; the
   device-only bench (§5a) is the fair fp64 measurement. A clean-machine end-to-end sweep would land
   between the two but is not achievable while the fleet runs.
4. ~~K=16384 parity/determinism only by construction.~~ **RESOLVED** — a dedicated `-DMPPI_K=16384`
   build (`build_k16384/`) was configured, compiled, and run: parity max rel Δ = 1.28e-15 (≈1 ULP),
   top-64 agreement 100%, and run-twice bit-identity = YES (max\|Δ\|=0) — all GREEN at the design's
   target K (§3, §4). The sm_89 determinism golden can be frozen at K=16384 with confidence.
5. **The worktree's telemetry/serve schema is behind main** (protocol v2 / no `pred_impact`+`ignite_h`;
   `bl_predict_ignite_h` absent) — the worktree was branched pre-D-011. **Irrelevant to this port**
   (headless MPPI + CUDA only), but the integration into main must rebase onto the current protocol v3.
6. **Not attempted:** ENTRY-under-`--mppi-cuda` (MPPI runs AERO only via the flag; ENTRY uses the
   supervisor + GM_HOVERSLAM), the fat-binary sm_90/compute_120 PTX (built sm_89-only), and any of the
   research-lane variants (colored-noise tuning, CoVO, MPOPI) — all correctly out of M5 port scope.

## 8. Integration notes (for the main session)

- New files: `core/guidance_mppi_cuda.{h,cu}`, `core/guidance_mppi_rollout.cuh`. CMake: `project(C CUDA)`,
  `CUDA_ARCHITECTURES 89`, per-language `-fmad=false`, unity-include of plant `.c` in the `.cu` only.
- Behavior-preserving worktree edits to fold: `BL_HD` qualifiers + function-local `static const` tables
  in `atmosphere.c`/`contact.c`/`control.c`/`dynamics.c` (value-identical; TERMINAL-194 proves it);
  `g_mppi_use_cuda` flag + branch in `sim.c`; `--mppi-cuda`/`--mppi-cuda-verify`/`--mppi-cuda-bench` in
  `main.c`. **Rebase onto main's protocol v3 first** (§7.5).
- The CPU `--mppi` path is byte-identical whether or not CUDA is compiled in (the flag defaults off).
- ADR: this port + the fp64-vs-6ms-gate finding warrants a `DECISIONS.md` entry (fp64-everywhere chosen
  under directive 7; 6 ms gate re-scoped to the 10 Hz/100 ms operating budget; reduction-kernel and
  MPOPI as the paths to a tighter latency if ever gated).
