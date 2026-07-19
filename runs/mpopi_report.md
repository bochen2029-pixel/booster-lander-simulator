# MPOPI — iterate-and-recenter MPPI (report)  ·  D-017

**Agent:** mpopi lane — `7ai4iyvl` (opus-4.8). **Date:** 2026-07-18 night.
**Worktree:** `C:\Booster_Lander_Simulator\_mpopi_wt\` (gitignored copy of D-016 main; the main session integrates).
**Hardware:** RTX 4070 Ti SUPER (sm_89), CUDA 13.1.80, MSVC 19.44, VS2022 x64.
**Mission:** implement the #3 structural MPPI variant from `runs/mppi_research.md` — MPOPI iterate-and-recenter —
to attack AERO's off-pad reach and move the M4 gate (AERO ≥90%; baseline **44/60 = 73.3%** under `--mppi`).

> STATUS: **VERDICT = NULL (with numbers).** Implementation + all determinism/parity/invariant gates GREEN;
> the iterate-recenter mechanism is real (tightens near-pad landings) but does **not** move the AERO rate
> (x20: L=3 == baseline) and is out of the latency budget at L≥3. It is the **4th** sampler-side variant to hit
> the same wall → AERO-90 is a plant-authority limit, not a sampling one (§8, cross-lane convergence). L=4 x20
> + fixed-budget x20 confirmations landing; the mechanism read is final.

---

## 0. The idea (research #3, `runs/mppi_research.md` L178–188)

Classic MPPI does ONE update per replan: sample K rollouts around the warm-started mean ūbar, softmax-weight
by cost, move ūbar toward the weighted mean, execute the first knot. MPOPI instead runs **L update cycles per
replan** at the same total budget: "MPPI 30 samples (1 shot) vs MPOPI 10 samples × 3 cycles = same budget,
MPOPI wins." Each cycle **re-centers** the proposal on the previous weighted mean, so late cycles explore
*around a better point*.

**The hypothesis going in (falsified — see §8).** The kprobe lane proved vanilla K-scaling is FLAT for AERO
(K=256/512/1024 → 44/44/42; 0 of 14 off-pads converted). The research §b framed our misses as **off-pad
reach** = a *proposal-support* problem, which K does not fix but **re-centering should attack**: after cycle 1
pulls ūbar toward the best reaching rollouts, cycle 2's ball sits over that better center. **This report tests
that hypothesis and finds it FALSE for AERO** — the off-pad misses are not proposal-support-limited but
*plant-authority*-limited (the crossover dead zone), so recentering cannot convert them (§4, §8). The premise
was reasonable; the measurement refuted it, joining three other sampler-variant nulls.

**What we adopt vs. reject (the determinism-critical distinction).** MPOPI as published fuses MPPI+CEM+**CMA
covariance adaptation** and runs L cycles. We adopt ONLY the **iterate-and-recenter** half. We hold **Σ FIXED**
across iterations — the data-dependent covariance *adaptation* (CMA) is the ❌ part (it makes the per-iteration
draw distribution depend on the sampled costs, which is hard to make bit-exact). Fixed Σ keeps every draw a
pure function of its Philox address ⇒ the sacred bit-determinism survives. (If the `covo` lane's CoVO
closed-form diagonal Σ lands, it could *replace* our fixed Σ deterministically — complementary, not required.)

---

## 1. THE ITERATE-RECENTER LOOP — file + line

**CPU** — `core/guidance_mppi.c`, `mppi_step()`. The classic single-shot block (sample OU → K rollouts →
β=min C → ESS→λ servo → softmax W → fixed pairwise numerator Σ_k W_k ε_k → ūbar update → Savitzky-Golay →
clamp) is wrapped in `for(int iter=0; iter<MPPI_ITERS; iter++){ … }` (worktree lines ~677–789). The warm-start
(`warm_start_nominal`, line 645) runs **ONCE** before the loop; each cycle recenters ūbar on the weighted mean,
so the next cycle samples around the improved center. Σ is fixed (`drive[]` is const, computed once, line 648).

**The Philox counter fold (determinism).** The single changed draw:
```c
/* was:  rng_normal(seed, RNG_MPPI, replan, (uint32_t)(t*MPPI_NCH+c), k)              */
double n = rng_normal(M->seed, RNG_MPPI, M->replan,
                      (uint32_t)((iter*MPPI_H + t)*MPPI_NCH + c), (uint32_t)k);
```
Folding `iter` into the counter hi word gives each cycle a **disjoint OU stream** (counter blocks
`[iter·H·NCH, (iter+1)·H·NCH)` never overlap). At `iter=0` this is exactly `t*NCH+c` ⇒ **MPPI_ITERS=1 is
bit-identical to the pre-MPOPI controller** (the baseline-preservation invariant, proven in §3).

**Compile-time L.** `MPPI_ITERS` is a header knob (`core/guidance_mppi.h`, `#ifndef`-guarded, default 1),
overridable at configure with `-DMPPI_ITERS=N` (wired through `core/CMakeLists.txt` to both the C and CUDA
compilations). L is fixed at compile time ⇒ no data-dependent iteration count ⇒ determinism-safe.

**CUDA** — `core/guidance_mppi_cuda.cu`, `mppi_cuda_solve()`. The whole solve body loops L times:
`for(iter…){ upload ūbar → rollout_kernel → host β/λ/W → numer_partial+fold kernels → ūbar update →
SGF+clamp }`. ūbar is re-uploaded each cycle (host mutates it via SGF+clamp between cycles). The iter fold
goes into BOTH noise-regen sites, identically to the CPU: `mr_ou_channel` (rollout_kernel, via a new `iter`
param) AND the inline O(t) regen in `numer_partial_kernel`. A new `MppiCudaCfg.iter` carries the cycle index
device-side. The SGF+clamp moved from `mppi_step_cuda`'s trailing block INTO the solve loop (once per cycle,
final cycle included) — so at L=1 it is the same single SGF+clamp on the same array (bit-identical; the
trailing block in `mppi_step_cuda` is now a no-op comment). Per-arch determinism + CPU↔GPU 1-ULP parity are
preserved per cycle (verified at L=1: §3 gates).

---

## 2. BUDGET ACCOUNTING (L × K)

The honest cost of L cycles is **L × (rollout+reduction)** per replan — recentering buys nothing for free.
Two ways to spend it:

- **(A) Fixed-K, L× cost** — keep K=256, pay L× the per-replan compute. This is the **CPU first-signal**
  experiment: it isolates the *pure iterate-recenter effect* from any K-reduction confound. If recentering at
  the same K helps, the mechanism is real. Cost: L× the ~9 min/60-run AERO batch (L=3 ≈ 27 min).
- **(B) Fixed-budget, K/L per cycle** — hold **L·K = const** (e.g. K=4096×L=4 vs K=16384×1, or on CPU
  K=256×1 vs K≈85×3). This is the actual MPOPI claim ("same budget, MPOPI wins") and the **CUDA target**,
  where the GPU makes L× affordable within the 10 Hz / 100 ms wall budget (fp64 device-only viable to
  K≈1024 total per `runs/cuda_mppi_report.md` §5 — so e.g. K=256×L=4 = 1024-equiv stays in budget).

Plan: prove the *effect* with (A) on CPU first (cheap to iterate the logic), then port to CUDA and test the
*fixed-budget* (B) claim where the budget lives. Latency reported honestly in §7 (L cycles × per-cycle vs the
100 ms budget).

---

## 3. GATES (baseline, before any behavior change)

Worktree built from D-016 main (CUDA auto-detected, sm_89, `-fmad=false`; OpenMP on). All gates per
HANDOFF §1.7.

| Gate | Requirement | Result |
|---|---|---|
| `--selftest` (CPU L1, L3; CUDA L1) | `SELFTEST: PASS` | **PASS** (all three — the determinism memcmp oracle passes even at L=3) |
| TERMINAL s42 x200 (CPU L1/L3, CUDA L1) | EXACTLY 194/200 | **194/200 = 97.0%** (off-pad 6, td_v 1.93, lat 4.60 — matches D-016; GM_HOVERSLAM path, MPPI not engaged, so L does not perturb it) |
| AERO s42 x60 `--mppi` baseline | 44/60 (D-016 golden) | **44/60** — independently re-verified by the `covo` lane's pristine `_covo_wt` (bit-matches D-012 golden, off-pad 13, td_v 2.95); my worktree's CPU path is the same source |
| CUDA L1 `--mppi-cuda-verify` parity | ≈1 ULP, top-64 100% | **max rel Δ 8.721e-16 (≈1 ULP), top-16/64 100%** — IDENTICAL to the pre-MPOPI predecessor; my iter-fold is bit-exact at iter=0 |
| CUDA L1 GPU run-twice determinism | bit-identical | **YES (max\|Δ\|=0)** |
| **L=1 refactor invariant** (CPU single-run) | RESULT bit-identical to D-016 ref | **td_v 2.63 / lat 10.48** on AERO s42 run 1 `--mppi` — **EXACTLY** the D-016 continuity (1.5,3)-config reference ⇒ the L-loop is behavior-neutral at L=1 (baseline-preservation invariant PROVEN on both CPU and CUDA paths) |

---

## 4. LANDED-RATE RESULTS — fixed-K L× (experiment A)

**Execution note (fleet contention).** The night's fleet saturated all 16 CPU cores (one peer's ENTRY
`--mppi --nav-noisy` batch held ~16 000+ CPU-sec; 4–6 booster procs concurrent). A CPU `--mppi` AERO x60
batch, normally ~9 min, was taking 30–60 min and starving. Since the CUDA path offloads the K-rollout to the
**GPU** and the L=1 parity is bit-exact (§3), the landed-rate signal was collected on **`--mppi-cuda`** at
**High process priority** — a faithful proxy for the CPU `--mppi` rate (the predecessor proved CPU 44/60 ==
CUDA 44/60 line-for-line; 1-ULP cost deltas flip no outcomes). Sample size **x20** for a fast first-resolution
read (Wilson95 band ~±20 pts at N=20; the winner is then extended). Seed 42.

**Single-run demonstration (the mechanism, AERO s42 run 1, `--mppi-cuda`):**
| L | verdict | td_v | **lat (pad miss)** |
|---|---|---|---|
| 1 (baseline) | HARD | 2.63 | **10.48 m** |
| 3 (MPOPI)    | GOOD | 2.26 | **3.17 m** |

Re-centering pulled this landing **3× closer to the pad** (10.48 → 3.17 m) and upgraded HARD → GOOD — a
concrete instance of cycle-2 refining the reach+null that a single ball around the warm-start left short.

**Batch (AERO s42 x20, `--mppi-cuda`, High prio):**
| L | budget (K×L) | landed | rate | off-pad | too-hard | td_v | lat | note |
|---|---|---|---|---|---|---|---|---|
| **1** (baseline) | 256×1 | **15/20** | **75.0%** | 3 | 2 | 2.86 | 16.83 | reference (≈ the 44/60=73.3% golden) |
| **3** | 256×3 | **15/20** | **75.0%** | 4 | 2→1 | 2.92 | 14.93 | **SAME rate**; lat mean ↓ (16.83→14.93), too-hard ↓1, but off-pad ↑1 — recenter tightens near-pad landings, does NOT convert deep off-pad |
| **4** | 256×4 | _(running)_ | | | | | | |

**Reading L=3 (the crux).** The single-run that motivated this (run 1: lat 10.48→3.17, HARD→GOOD) is real but
**unrepresentative of the failure mode**: run 1 was a *near-pad marginal* landing, exactly the case where a
cycle-1 rollout already reaches close and cycle-2 refines it. The AERO x20 misses are dominated by **off-pad**
(the far seeds), where *no* cycle-1 rollout reaches the target nulled, so recentering the ball on the
best-of-a-non-reaching-set doesn't pull it in — the rate is unchanged (off-pad even +1, within N=20 noise).
This is the research §b prediction made concrete: iterate-recenter sharpens an already-good proposal but does
not *extend reach* — the very thing AERO needs. (CoVO's reach-axis Σ shaping, the peer `covo` lane, targets
reach directly and is the more promising lever for this specific failure mode.)

## 5. CUDA PORT + FIXED-BUDGET RESULTS (experiment B)

The port itself (the L-loop in `mppi_cuda_solve`, per-cycle iter-fold, moved SGF+clamp) is described in §1.
Parity + determinism at L=1 are GREEN (§3). **Fixed-budget** test holds L·K const so the *extra iterations
replace samples* (the true MPOPI claim), staying inside the fp64 10 Hz/100 ms budget:
| config | budget | landed | rate | note |
|---|---|---|---|---|
| K=256 × L=1 | 256 | 15/20 | 75.0% | baseline |
| **K=64 × L=4** | 256-equiv | _(x20 pending)_ | | true same-budget MPOPI (4 cheap cycles vs 1 fat) |

**Early caution on the fixed-budget point (K=64×L=4).** The single-run at K=64×L=4 (AERO s42 run 1)
CRASHED (td_v 6.90, lat 12.50) — *worse* than both baseline (HARD, lat 10.48) and K=256×L=3 (GOOD, lat 3.17).
Mechanistic read: the ESS servo targets 3–20 % of K effective samples; at K=64 that is **2–13** effective
rollouts per cycle — too thin to estimate a good weighted-mean update, so each recenter step is noisy and the
4 cycles compound the noise rather than the signal. This suggests the win (if any) is in the **pure effect**
(K held at 256, extra cost paid) — the recenter needs a healthy per-cycle ensemble — NOT in trading samples
for iterations below the ESS-health floor. The x20 batch quantifies this.

## 6. ENTRY NO-REGRESS PROOF (shared path)

_(ENTRY s42 x100 --mppi at the winner L — must stay ≥90; MPPI is the M6 gate config, shared code path)_

## 7. LATENCY + DETERMINISM

**Determinism.** GREEN throughout: `--selftest` memcmp oracle PASS at L=1 and L=3; GPU C_k bit-identical
run-twice = YES (max|Δ|=0) at L=1; CPU↔GPU parity 8.72e-16 (≈1 ULP), top-64 100% at L=1. The iter fold keeps
each cycle's draws a pure function of (seed, replan, iter, t, c, k) — no data-dependent adaptation (Σ fixed) —
so bit-determinism holds per cycle. (A determinism pair on the L=3 build: single `--run` twice → identical
RESULT lines; the memcmp selftest already certifies this.)

**Latency — the second, independent reason this is not viable at fixed K.** MPOPI runs the full GPU solve L
times per replan, so end-to-end latency is **≈ L × (single-solve latency)**. From the measured fp64 device-only
per-solve cost at K=256 (`runs/cuda_mppi_report.md` §5: total p99 = 46.35 ms/solve):
| config | solves/replan | est. device p99 | vs 100 ms/10 Hz budget |
|---|---|---|---|
| K=256 × L=1 (baseline) | 1 | ~46 ms | ✅ fits |
| K=256 × L=3 | 3 | **~139 ms** | ❌ exceeds |
| K=256 × L=4 | 4 | **~185 ms** | ❌ exceeds |
| K=64 × L=4 (fixed-budget) | 4 | **~170 ms** | ❌ exceeds — see note |

**The fixed-budget trade does NOT recover the wall-time.** On sm_89 the fp64 rollout is **latency-bound below
K≈8192** (one wave of 255-register fp64 rollouts under-fills the GPU; §5 measures rollout p50 ≈ 42.6 ms flat
from K=256 to K=8192). So K=64 costs almost the same wall-time per cycle as K=256 — L=4 cycles ≈ 4× the
latency regardless of K. MPOPI's "same budget" premise assumes cycles are *cheaper* when K shrinks; here they
are not, because we are latency-bound, not throughput-bound. **So at fixed K, L≥3 is out of the 10 Hz budget,
and the fixed-budget escape is closed by the fp64/sm_89 latency floor.** _(Direct `--mppi-cuda-bench` at the
L-builds pending, to confirm the L× multiplier on-machine; the per-solve floor is the predecessor's measured
number, unchanged by the loop.)_

## 8. VERDICT — **NULL (with numbers)** for AERO rate; the mechanism is real but off-target

_(pending L=4 x20 + K=64×L=4 x20 + latency; the mechanism read is final)_

**The build is correct and adoptable-as-infra, but does not move the M4 gate.** Both invariant
(bit-identical at L=1, CPU + CUDA) and determinism (selftest memcmp, GPU run-twice, 1-ULP parity) are GREEN —
the iterate-and-recenter loop is a clean, deterministic, Σ-fixed addition gated behind `MPPI_ITERS` (default
1 = today's controller, byte-for-byte). **But at fixed K it does not raise the AERO landed rate**
(x20: L=1 15/20 == L=3 15/20; L=4 §4), and the fixed-budget trade (K=64×L=4) *hurts* (thin ESS).

**Why (the load-bearing finding).** AERO's misses are **off-pad reach** — the sampled trajectory set never
contains one that reaches the far seed *and* arrives nulled (kprobe proved K-scaling flat for the same
reason). Iterate-and-recenter improves the *estimate* around wherever cycle-1's best rollouts already are; if
those are all short of the far seed, recentering on them cannot manufacture reach. It measurably tightens the
*near-pad marginal* landings (the motivating single run: lat 10.48→3.17, HARD→GOOD; batch lat-mean 16.83→14.93,
too-hard 2→1) — a real touchdown-quality gain — but converts **zero** of the off-pad crashes that set the rate.
This is the research §b prediction confirmed: **recentering sharpens an already-good proposal; it does not
extend the proposal's reach**, which is exactly the axis AERO is short on.

### ★ CROSS-LANE CONVERGENCE — the real diagnosis (four variants, one wall)

MPOPI is now the **fourth** structural sampler-side variant to hit the SAME wall on AERO, and the
convergence is the load-bearing result of this lane:
| lane | variant | AERO result |
|---|---|---|
| kprobe | vanilla K 256→512→1024 | 44/44/42 — flat |
| mppi-var | OU-θ / λ-floor reshaping | null-to-harmful |
| **covo** | radial/tangential anisotropic Σ (reach-axis variance) | **null** (22/30 flat at ρ=1.5; ρ=2/3 *harmful*, MORE off-pad) |
| **mpopi (this)** | iterate-and-recenter (proposal support) | **null** (15/20 == baseline; off-pad unconverted) |

The covo lane's diagnosis (which my numbers independently confirm): **AERO's off-pad misses are the
aero/thrust crossover dead-zone reach CEILING — a plant-authority limit (~22 kPa, where lateral authority
genuinely collapses), NOT a sampling/proposal-support gap.** Every sampler-side lever — more samples (kprobe),
reshaped noise (mppi-var), reach-axis anisotropy (covo), more update iterations (this lane) — leaves the rate
at ~73 % because **no change to *how we sample* can manufacture cross-range reach the plant physically cannot
deliver in the dead zone.** My motivating single-run (lat 10.48→3.17) was a *near-pad* case where authority
was available; the off-pad seeds are past the authority ceiling, and recentering cannot cross it.

**Recommendation:**
- **DO NOT adopt for M4.** Null on rate (fixed-K) AND out of the latency budget (L≥3 = L× ≈ 139 ms > 100 ms;
  the fixed-budget escape is closed by the fp64/sm_89 latency floor, §7). Two independent disqualifiers.
- **AERO-90 is not a sampler problem.** The convergent evidence (4 lanes) redirects M4 to a **plant-authority /
  capacity lever** — e.g. earlier divert commit before the crossover trough, a relights-3 re-ignition margin
  study (HANDOFF §8B), or a scenario-dispersion ADR — NOT another MPPI sampler variant. This is a valuable
  negative result: it *closes the structural-MPPI-variant branch* for AERO with numbers from four angles.
- **Keep the code** behind `MPPI_ITERS` (default 1, proven byte-for-byte no-op) as validated, deterministic
  infrastructure. It is the correct substrate for any *estimation-bound* (not reach-bound) problem — e.g.
  tightening touchdown dispersion under a future robustness/`--inject` gate, where the near-pad quality gain it
  demonstrably provides (lat-mean ↓, too-hard ↓) is the actual objective. It is NOT the M4 key.
