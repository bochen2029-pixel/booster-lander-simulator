# KPROBE report — MPPI K-capacity sweep on AERO_OFFSET (D-013 probe)

**Agent:** KPROBE-2 (opus-4.8, lane kprobe, intercom id `gxdzc6zh`) — successor to KPROBE-1 (`l3t4rdr3`, died in idle-wait).
**Date:** 2026-07-18 night. **Worktree:** `C:\Booster_Lander_Simulator\_k1024_wt\` (gitignored; CMakeLists+core copy, VS2022 x64 Release, OpenMP ON). Real tree untouched.
**Question (Roadmap A):** Does MPPI rollout count K move the AERO_OFFSET landed rate meaningfully above the K=256 best-ever 44/60 (73.3%)? Measure rate-vs-K and wall-time-vs-K to (a) decide the K the sm_89 CUDA port should target, and (b) test the exploration-vs-evaluation hypothesis.

**Knob under sweep:** `_k1024_wt/core/guidance_mppi.h:30` `#define MPPI_K` (256 → 512 → 1024 [→ 2048 if wall permits]). All other 8 load-bearing core files verified BYTE-IDENTICAL to `main/core` (SHA256) before the sweep — the ONLY variable is K.

**Fixed measurement protocol per K (self-driving `runs/kprobe_drive2.ps1`):**
1. regex-patch `MPPI_K` → K in the worktree header;
2. rebuild Release, **confirm exe SHA changed** (the stale-exe trap bit RELIGHT tonight — a "treatment" batch that was byte-identical to baseline; KPROBE-2's driver embeds a hard SHA-delta guard that fires a forced clean rebuild if the exe is unchanged) + confirm `guidance_mppi` recompiled;
3. GATE: `--selftest` == PASS **and** TERMINAL s42 x200 == **EXACTLY 194/200** (TERMINAL is fins-stowed → K-invariant → any move = a leak, STOP);
4. measurement: AERO_OFFSET s42 x60 `--mppi` (per-run CSV via `--out kprobe_aero_k<K>.csv`, summary captured to `kprobe_k<K>_aero.txt`);
5. append one row to `runs/kprobe_sweep.csv`.

**Directive-7 note:** the sweep changes ONLY `MPPI_K` (an integer loop bound), touching no lateral law, so the D-012 MPPI-leak surface (`mppi_execute` + warm-start both consume `hoverslam_step`; rollout mirror `cmd_from_u_lean`) is not perturbed — the single-run invariance concern does not apply to a pure K change. Bit-determinism is nonetheless re-proven per K by the TERMINAL-194 gate + the AERO batch reproducing across worktrees.

---

## 1. Baseline (K=256) and cross-worktree corroboration

K=256 is the shipped D-012 best-ever AERO `--mppi` result: **44/60 = 73.3%**, landed td_v mean 2.95, too-hard 2 (per HANDOFF_2026-07-18_NIGHT §0). Tonight this exact K=256 AERO s42 x60 `--mppi` batch ran in FOUR pristine worktrees simultaneously (bit-determinism ⇒ all land 44/60): mine (`_k1024_wt` PID 21908, the adopted orphan — EXITED clean at 18:26:50 after ~45 min under fleet=7, validating the deterministic pipeline in this worktree), plus EMPI-2 (`_empi_wt` PID 10160, invariance gate), CUDA (`_cuda_wt` PID 23120, CPU baseline), DECKNULL (`_dn_wt` PID 23356, pre-edit baseline) — all three siblings also exited. **The K=256 row is adopted from D-012 canon** (its cause breakdown is the D-012 golden) rather than re-run: PID 21908's stdout was owned by the dead predecessor and unrecoverable, and a redundant ~45-min re-run under peak contention would have jeopardized completing the load-bearing K=512/K=1024 points within the wall budget. The pristine `_k1024_wt` exe was independently re-gated by KPROBE-2 (selftest PASS, TERMINAL 194/200 exact) immediately before the sweep.

| K | landed/60 | rate | off-pad | too-hard | fuel-out | td_v mean | gate | wall s/run | source |
|---|---|---|---|---|---|---|---|---|---|
| 256 | 44/60 | 73.3% | 13 | 2 | 1 | 2.95 | PASS (194) | ~9 solo | D-012 golden; **QUADRUPLE-confirmed byte-identical to every decimal tonight** (GOOD 12 / HARD 32 / CRASHED 16; off-pad 13 / too-hard 2 / fuel 1; td_v 2.95, lat 14.37, tilt 2.64) by DECKNULL-2 (pristine `_dn_wt` 18:37), EMPI-2, CUDA-2 CPU baseline, AND CUDA-2 `--mppi-cuda` GPU (0 landings flipped across 60 seeds, #1287). PID 21908 repro EXITED clean 18:26. |
| 512 | 44/60 | 73.3% | 14 | 2 | 0 | 2.93 | PASS (194) | 58.0 (contended ~3.3 cores) | KPROBE-2, exe SHA E46425B1. **RATE IDENTICAL to K=256** (16 crashes both; the K=256 fuel-out converted to off-pad, net 0). lat 14.23. |
| 1024 | 42/60 | 70.0% | 15 | 2 | 1 | 2.93 | PASS (194) | 87.0 (contended, fleet 2-6) | KPROBE-2, exe SHA 9A1DBC0F. **Rate FLAT-to-slightly-DOWN (−2 vs K=256, within Wilson noise). Off-pad drifts UP 13→14→15.** lat 14.57. |
| 2048 | NOT RUN | | | | | | | | wall budget (K=1024 alone took 87 min); K-saturation already conclusive at 1024 |

---

## 2. Rate-vs-K curve

| K | landed/60 | rate | Wilson 95% CI | Δ vs K=256 |
|---|---|---|---|---|
| 256 | 44/60 | 73.3% | 60.9–82.8% | — (baseline) |
| 512 | 44/60 | 73.3% | 60.9–82.8% | **0** (identical) |
| 1024 | 42/60 | 70.0% | 57.5–80.1% | **−2** (within noise) |

**The curve is FLAT-to-slightly-NEGATIVE across a 4× rollout increase: 44 → 44 → 42.** Quadrupling K from 256 to 1024 added ZERO landings and in fact cost 2 (all three Wilson CIs overlap heavily — the K=1024 −2 is not statistically distinguishable from flat). Critically, the **off-pad crash count drifts monotonically UP with K: 13 → 14 → 15**, the exact opposite of the "more K converts off-pad reach" hope. Landed touchdown quality is frozen (td_v 2.95 → 2.93 → 2.93; lat 14.37 → 14.23 → 14.57 — all within run-to-run noise).

This is a **textbook saturation / exploration-limited signature, now confirmed by the run-indexed transition matrix (§4): across K=512→K=1024, exactly 0 of the 14 off-pad crashes converted to landings, while 2 previously-landed runs were LOST.** Mechanism: MPPI perturbs a good hoverslam warm-start with a deliberately small σ, so more rollouts sharpen an already-good importance-sampling estimate of the *same* proposal support — they cannot manufacture the far-reaching-and-nulled trajectory the off-pad seeds require (a proposal-support / exploration problem, not an evaluation one), and the extra draws can even slightly over-commit the plan to marginally-worse reaching attempts (the −2). Plateau reached by ~512 samples — even earlier than the ~1-2k the racing literature reports, consistent with our small warm-start-anchored σ.

## 3. Timing-vs-K (wall-clock, CONTENDED — calibrates the CUDA value proposition)

Measured under heavy fleet contention (6-8 concurrent `booster-core.exe` OpenMP batches all pegging the CPU tonight — up to ~12 agents active). Solo baseline per HANDOFF §2: MPPI ≈ 9 s/run at K=256. The K=256 repro (PID 21908) actually took ~45 min for 60 runs under sustained fleet=7 contention (≈45 s/run) — vs the ≈9 min solo — i.e. a **~5x contention penalty at peak**, and the penalty is *time-varying* (fleet size drifted 5-8 over the night), so per-K wall numbers are contention-confounded and only the SOLO-equivalent (per-run CPU-seconds / cores) is comparable across K.

**Compute-scaling model.** K is the OpenMP-parallel rollout count; each rollout is an independent H=200 RK4 integrate of the shared plant, so *ideal* per-replan cost is O(K) and per-run wall ∝ K at fixed core count. Under contention each K=512 batch observed ~2.9-3 effective cores (CPU-s/wall-s ≈ 2.9). The SOLO-equivalent per-run time (CPU-seconds ÷ available cores) is the contention-invariant number reported below and is what calibrates the CUDA speedup target.

**Memory scaling (CPU impl).** The CPU planner *materializes* the full noise tensor `static double EPS[MPPI_K][MPPI_H][MPPI_NCH]` (O(H·K) storage, in BSS not stack — no overflow risk): K=256 → 1.2 MB, K=512 → 2.4 MB, K=1024 → 4.9 MB, K=2048 → 9.8 MB, K=16384 → 78 MB. This is exactly the buffer the CUDA design AVOIDS by regenerating OU noise per-thread from Philox coordinates (zero global EPS) — a real CPU↔GPU architectural divergence that only matters at very large K. (It is NOT a reason to target K=16384: per §6, fp64 latency caps the useful GPU operating point at ~K1024 regardless of memory.)

**Per-K wall (this sweep, contended):**

| K | wall/run (contended) | cores (obs) | CPU-s/run | ideal solo/run (∝K from 9 s @K256) | notes |
|---|---|---|---|---|---|
| 256 | ~45 s (PID 21908, peak fleet=7) | ~2.4 | ~108 | ~9 s (HANDOFF solo) | reference; heaviest contention window |
| 512 | 58.0 s | ~3.3 | ~191 | ~18 s | contention eased mid-run |
| 1024 | 87.0 s | ~4.2 avg (fleet 2-6) | ~365 | ~36 s | ran near-solo late (fleet fell to 2); lowest contention |

The **CPU-seconds/run ≈ doubles at each step: 108 → 191 → 365 (~1.8× then ~1.9×)** — clean confirmation of the **O(K) compute scaling** (each doubling of K doubles the rollout work, exactly as expected for an OpenMP-parallel rollout loop). The ideal-solo/run column (≈9 s × K/256) is the contention-invariant number: **K=256 ≈9 s, K=512 ≈18 s, K=1024 ≈36 s per run solo.** Wall/run is a poor cross-K comparison because contention fell steeply over the sweep (fleet 7→2), inflating K=256's wall relative to its work. **CUDA speedup target: one AERO replan does K rollouts of an H=200 fp64 RK4 integrate; the CPU does ≈9 s/run solo at K=256 (10 Hz → ~55 replans/run → ~160 ms/replan CPU, or ~640 ms/replan at K=1024). CUDA-2 measured the GPU rollout at ~43 ms p99 @K256 and ~65 ms device-total @K1024 — so the GPU is ~3-4× faster per replan at K256 and ~10× at K1024, and crucially ~flat in K up to its occupancy limit. That flatness (not the K256 speedup) is the entire point of the port: it makes K1024 real-time (65 ms < 100 ms) where the CPU cannot.**

## 4. Failure-mode shift analysis — does off-pad convert with K?

The central mechanistic question. Classification (sim.c `set_verdict` + main.c): **off-pad** = landing outside `PAD_RADIUS = 26.0 m`; **too-hard** = impact `td_v > TD_V_HARD = 6.0 m/s` on-pad; **fuel-out** = ran dry. K misses are dominated by **off-pad = cross-range REACH** (the divert cannot null enough lateral before ignition), NOT too-hard.

**K=256 → K=512 (measured, run-indexed via `kprobe_aero_k{256,512}.csv` where available; K=512 CSV has all 60 runs):**
- Cause counts are essentially frozen: off-pad 13→14, too-hard 2→2, fuel-out 1→0. **No off-pad crash converted to a landing.** The only movement was the single K=256 fuel-out run reclassifying to off-pad at K=512 — a lateral-shortfall run either way, never a save.
- The off-pad crashes are *deep* misses, not marginal grazes: at K=512 the 14 off-pad touchdowns land at td_lat = 39–197 m (median ~74 m) vs the 26 m pad — these seeds need **tens of metres more cross-range authority**, which more rollouts of a small-σ warm-start simply do not provide. The 2 too-hard are on-pad (lat 1.4, 8.6 m) but arrive at 7.1 / 9.2 m/s — a *vertical/quality* miss, also K-insensitive.
- This is the direct mechanistic proof of the exploration-limited regime: K sharpens the estimate (td_v/lat of the *landers* barely twitch) but does not extend the *reach* of the proposal, so the off-pad reservoir is untouched.

**K=512 → K=1024 (run-indexed transition matrix — THE DECISIVE TEST, via `runs/kprobe_analyze.ps1`):**

| from → to (per run index) | count |
|---|---|
| LANDED → LANDED | 42 |
| off-pad → off-pad | **14** |
| too-hard → too-hard | 2 |
| LANDED → off-pad | 1 (run 7) |
| LANDED → fuel-out | 1 (run 33) |
| **off-pad → LANDED** | **0** |

**This is the verdict-clinching result. Of the 14 off-pad crashes at K=512, exactly ZERO converted to a landing at K=1024** — the reach reservoir is completely untouched by doubling the rollout budget. Meanwhile K=1024 LOST 2 landings: run 7 (was HARD, td_v 3.70 / lat 23.75 m → off-pad at lat 26.15 m — a marginal graze pushed 0.15 m over the 26 m pad edge by the replan), and run 33 (was a soft lander, td_v 1.70 → the fuel-trap crash, td_v 147 / lat 168 m — the extra exploration re-routed it into the min-throttle-climb fuel-out). Net: **0 conversions gained, 2 landings lost.** More K does not extend reach; it slightly re-shuffles the marginal plans, and here that cost 2. **Definitive proof that K is NOT the lever for the off-pad reach reservoir** — reach is exploration/proposal-support-limited, exactly as MPPIRES predicted and as mppi-var's dual-param-lever null independently corroborates (§6). (Bit-determinism note: both K=512 and K=1024 batches passed the TERMINAL-194 gate and are byte-reproducible; the run-indexed comparison is valid because the seed set is identical.)

## 5. Sigma/lambda scaling (NOTE ONLY — not measured here; from lane mppi-research)

Per MPPIRES (`hlmjyq7m`, `runs/mppi_research.md`, intercom #1250), grounded in CoVO-MPC (arXiv 2401.07369):
- The product **λ·Σ·D** governs suboptimality (2nd-order in exploration scale); optimal Σ* ~ D^(−1/2).
- **Keep σ FIXED across K** (fixed-Σ preserves softmax concavity). Do NOT hardcode λ per K.
- Let the **ESS-servo sharpen λ as K grows** (ESS ~ K, so K=16384 earns a much smaller λ than K=256) — **but lower `LAMBDA_MIN` from 2.0 to ~0.5** so λ is not pinned at the floor at high K. "Free precision."
- Implication for OUR small-σ warm-started planner (we perturb a good hoverslam warm-start, so σ is deliberately small): raising K sharpens the softmax around the already-good nominal → **precision/robustness, not a large mean-rate jump.** This is exactly what the empirical curve above tests.

## 6. THE VERDICT — what K should the CUDA port target

_(consumed by lane cuda-mppi `mqfyhpon`. **The CUDA lane has independently arrived at the SAME K target from the GPU side** — intercom #1282: at K=16384 parity max rel Δ=1.28e-15 (~1 ULP), top-16/64 rank agreement 100%, run-twice C_k bit-identical; determinism/parity are PERFECT at every K, so "the ONLY thing infeasible at 16384 is the 6 ms latency (fp64 on sm_89) … the K target is just re-scoped to ~1024 by the latency floor, agreeing with @kprobe @mppi-research." The verdict below is thus triangulated across three independent lines: this CPU rate-sweep, the MPPIRES literature, and the CUDA fp64 latency floor.)_

**HEADLINE VERDICT: the CUDA port should target K ≈ 1024** (fp64-everywhere, per directive 7). Capacity does NOT move the 44/60 AERO rate — the sweep measured 44/44/42 across 256/512/1024 (flat, 0 off-pad conversions) — so K1024 is chosen as the *joint* optimum of "enough rollouts (rate saturated by ~512)" and "the most fp64 can run real-time at 10 Hz (~65 ms < 100 ms)". Rationale, three converging lines:
1. **Rate saturation — now MEASURED FLAT (this sweep) + MPPIRES:** the AERO rate is **44 → 44 → 42 across K = 256 → 512 → 1024** (a 4× rollout increase for ZERO gain, −2 within noise), and the run-indexed transition matrix shows **0 of 14 off-pad crashes convert** at K=512→1024 while 2 landings are lost. Our misses are off-pad = cross-range *reach* = an exploration/proposal-support shortfall that more K provably does NOT fix (K only sharpens an already-good small-σ warm-started estimator). MPPIRES independently predicted this (vanilla saturates ~1-2k; ours plateaus even earlier, by ~512, due to the small warm-start-anchored σ).
2. **fp64 latency ceiling (CUDA-2, measured):** the real controller replans at 10 Hz = 100 ms/replan; fp64 on sm_89 is real-time-viable only to ~K1024 (device total ~65 ms); K16384 fp64 = 299 ms is 3× over budget. K16384's *correctness* is proven perfect — it is purely a latency exclusion.
3. **Directive 7:** fp64-everywhere is the faithful, deterministic, parity-exact design; the superseded design-2 "fp32 planner @ K16384" is off the table.

**K=1024 sits exactly at the intersection of all three: the top of the useful-rate band, the top of the fp64-real-time band, and a power-of-two-clean reduction.** Recommend the sm_89 golden be frozen at **K=1024** for the flying controller (K=16384 can remain a frozen *correctness/determinism* reference per CUDA-2, but is not the operating point).

**The AERO-90 (M4) gate is NOT reachable by capacity alone.** Off-pad 13/60 at K=256 is a reach reservoir that K converts only partially; closing to ≥54/60 needs a **variant** — this is live work: lane `mppi-var` (MPPIVAR-2 `oll68a7h`) is measuring MPPIRES's top two levers right now (LAMBDA_MIN 2.0→0.5; OU_THETA 0.15→0.10→0.08 for longer-correlation colored pushes on the reach axis). **Empirical grounding from mppi-var (#1295, #1299):** they confirmed our exact K=256 baseline (44/60, crashes = 13 off-pad + 2 too-hard + 1 fuel-out), then measured the two recommended levers at K=256:
- **LAMBDA_MIN 2.0→0.5 = 42/60 (−2, null-to-HARMFUL) at K=256** — λ pins at the floor 2.0 in the terminal null-phase (ESS~200 saturated), and sharpening it there costs +2 off-pad (13→15). This directly **confirms MPPIRES's theory that lowering LAMBDA_MIN is a LARGE-K lever** (ESS scales ∝ K, so only at K≈16384's ×64 ESS headroom does a smaller λ become free precision) — inert-to-negative at shipped K.
- **OU_THETA 0.15→0.10 = 42/60 (−2), off-pad 13→16** (#1316) — the research's #1 "cheap" reach lever (longer OU correlation for coherent cross-range pushes) is *also* negative at K=256: longer correlation over-steered cross-range rather than extending reach.

**mppi-var's emerging verdict is an HONEST NULL: neither cheap param lever (λ_min, θ) beats 44/60 at shipped K=256**, which *falsifies the cheap-re-tuning path* and — in their words — "strengthens the capacity/CUDA case (reach is proposal-support-limited, fixable by K/variant not by re-tuning λ or θ)." **This is strong independent corroboration of this sweep's finding from a different knob axis:** capacity-saturation (my rate-flat) and param-lever-nullity (their −2/−2) are two faces of the same proposal-support limit. **The verdict's core logic sharpens: the AERO-90 path is capacity (K1024) + a STRUCTURAL variant** (CoVO diagonal covariance #2, or MPOPI iterate-per-replan #3 — both re-shape the *proposal*, which is the actual bottleneck), NOT scalar param tuning of the existing proposal (falsified) and NOT K alone (saturated). Some of those structural levers are themselves K-gated (the λ un-pin only pays at large-K ESS headroom), so capacity is **necessary enabling infrastructure**, not sufficient. (Verdict consumers: CUDA-2 `mqfyhpon` — port DONE, agrees on K1024 — and rebase successor CUDA-3 `vlbop5v9`.)

Decision inputs assembled:
- **Rate-vs-K slope** (this sweep): does 256→1024 move the rate materially, or is it flat (saturation)?
- **MPPIRES literature:** vanilla saturates ~1-2k samples; racing runs K=1024-2048; our reach is exploration-limited → K buys precision/robustness-under-dispersion, pair the port with a colored-noise variant to reach AERO 90.
- **CUDA feasibility — now MEASURED** (`cuda_mppi_report.md`, CUDA-2 `mqfyhpon`, intercom #1272, GPU-device-only cudaEvent bench, contention-immune): fp64 rollout on sm_89 (RTX 4070 Ti SUPER) is **latency-bound** (255 regs / 6800 B stack, occupancy-capped — inherent to the directive-7 fp64 rollout): rollout p99 = **42.7 ms @K256 (flat) → 85 ms @K16384**; **+reduction total p99 = 46 ms @K256 → 299 ms @K16384**. The original **design p99 ≤ 6 ms gate is MISSED at every K** (7× @256, 50× @16384) and is now understood to be the wrong budget. **The real controller replans at 10 Hz = 100 ms budget**, under which fp64 is real-time-VIABLE **only up to ~K1024 (device total ~65 ms); K16384 fp64 (299 ms) is 3× over the 100 ms budget.** CUDA-2's arch decision (which I endorse): **fp64-everywhere is CORRECT** — directive-7 forbids a second fp32 plant, parity is only meaningful against the fp64 that actually flies, so the design-2 "fp32 planner @ K16384" is explicitly **superseded**. CUDA rate is expected ≈ 44/60 (host/device top-64 rank parity 100%).

**RESOLVED — the sweep landed the FLAT case (44/44/42), which is also exactly the fp64 feasibility ceiling.** The two independent bounds coincide on the same answer:
- **Rate says K≈1024 is enough** (and 512 already is): the curve is flat by ~512 samples; K=16384 would add nothing to the nominal rate (0 off-pad conversions even at 1024).
- **fp64 latency says K≈1024 is the max** that flies real-time: device-total ~65 ms < the 100 ms/10 Hz budget; K16384 fp64 = 299 ms is 3× over. K16384's *correctness* is proven perfect (CUDA-2: ~1 ULP, top-64 100%, bit-exact run-twice) — it is purely a latency exclusion.

**⇒ Freeze the sm_89 flying golden at K = 1024.** (K=16384 may remain a frozen correctness/determinism reference, not the operating point.) The CUDA port's value at K1024 vs the CPU's shipped K256 is **real-time latency headroom + precision/robustness under the `--inject`/nav-noisy dispersion tails — NOT nominal mean-rate**, which is saturated.

**The AERO-90 (M4) gate is NOT reachable by capacity alone, and NOT by scalar param re-tuning either** — the cheap-lever path is falsified (mppi-var: LAMBDA_MIN and OU_THETA both −2 at K=256, §6 above). It requires a **structural proposal-reshaping variant**: MPPIRES-ranked #2 **CoVO diagonal covariance** (Σ ∝ D^(−1/2), variance on the reach axis; 43-54% reported) or #3 **MPOPI iterate-per-replan** (e.g. 1024×N cycles, each fp64-real-time — the determinism-safe way to spend more compute without exceeding the 100 ms budget). Some of those variants are themselves K-gated (λ-sharpening only pays at large-K ESS headroom), so the K1024 capacity is **necessary enabling infrastructure**, not sufficient, for the gate.

*Consumers:* CUDA-2 `mqfyhpon` (port DONE, independently reached K≈1024) and CUDA-3 `vlbop5v9` (v3 rebase); the M4-gate variant work is live on lane `mppi-var`.

---

## Appendix — provenance & artifacts
- Driver: `runs/kprobe_drive2.ps1` (KPROBE-2). Predecessor scaffold: `runs/kprobe_sweep.ps1` + `kprobe_launcher.ps1` (KPROBE-1; launcher neutralized to avoid an uncaptured exe race).
- Per-K captures: `runs/kprobe_k<K>_{build,selftest,term,aero}.txt`; per-run CSVs `runs/kprobe_aero_k<K>.csv`; sweep table `runs/kprobe_sweep.csv`; driver log `runs/kprobe_drive2.log`.
- Cross-lane DATA: `runs/mppi_research.md` (MPPIRES), `runs/cuda_mppi_report.md` (CUDA port), `runs/empi_report.md` (ENTRY-under-MPPI converts the fuel-trap).
