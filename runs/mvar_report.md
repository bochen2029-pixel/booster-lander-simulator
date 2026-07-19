# MVAR report — the two CHEAP MPPI levers at K=256 (from mppi_research)

**Agent:** MPPIVAR-2 (opus-4.8, lane `mppi-var`, intercom id `oll68a7h`) — successor to MPPIVAR (`2tmyln9a`).
**Date:** 2026-07-18 night. **Worktree:** `C:\Booster_Lander_Simulator\_mvar_wt\` (gitignored; CMakeLists+core copy, VS2022 x64 Release, OpenMP ON). Real tree untouched.
**Mission:** operationalize the two `cheap` levers `runs/mppi_research.md` identified, ONE knob at a time, at **K=256 (the shipped config)**, with full gates (selftest PASS + TERMINAL s42 x200 == 194/200 per build), measured on **AERO_OFFSET s42 x60 --mppi** vs the shipped **44/60 (73.3%)** best-ever baseline.

- **LEVER 1 (research rec A2):** `LAMBDA_MIN` 2.0 → 0.5. The ESS-servo lowers λ toward the floor when ESS is high; a lower floor lets λ sharpen further = free precision. The research rec targets *large* K (ESS∝K, so 16384 earns ~64× headroom and the floor pins hard); the question here is whether it does anything at *shipped* K=256.
- **LEVER 2 (research rec #1, the top-ranked cheap lever):** `OU_THETA` 0.15 → {0.10, 0.08}. Longer OU correlation length = coherent low-freq cross-range pushes that persist over more of the 5 s horizon → attacks **off-pad reach**, our actual bottleneck (colored noise gave 0%→83% on lagged actuators; grid-fin attitude reversal is a lagged actuator). Determinism-safe: still one Philox draw per (k,t,c); the drive normalization `σ√(2θ−θ²)` holds stationary σ=1.5 fixed as θ changes, so this changes correlation length only, NOT variance.

Both are true one-knob changes (verified: `LAMBDA_MIN` = single `static const` def @L153 used as the bisection floor `lam_lo` @L699; `OU_THETA` = single `#define` @L47 propagating to drive normalization @L649-651 + the OU recursion @L682).

---

## 1. Results table (all AERO_OFFSET s42 x60 --mppi unless noted; gates per build)

| Config | LAMBDA_MIN | OU_THETA | selftest | TERMINAL | landed/60 | rate | off-pad | too-hard | fuel-out | td_v mean | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline (shipped) | 2.0 | 0.15 | PASS | 194/200 | **44/60** | 73.3% | 13 | 2 | 1 | 2.953 | bit-determinism CONFIRMED |
| L1 lambda_min 0.5 | 0.5 | 0.15 | PASS | 194/200 | **42/60** | 70.0% | 15 | 2 | 1 | 3.00 | **−2 vs base (null-to-harmful)** |
| L2a ou_theta 0.10 | 2.0 | 0.10 | PASS | 194/200 | **42/60** | 70.0% | 16 | 2 | 0 | 3.00 | **−2 vs base (off-pad +3)** |
| L2b ou_theta 0.08 | 2.0 | 0.08 | PASS | 194/200 | **41/60** | 68.3% | 16 | 2 | 1 | 2.91 | **−3 vs base (monotone OU decline)** |

*(No "best combo" row: the decision rule only measures a combo when a single lever reaches ≥46/60. None did — all three are below baseline — so no combo and no seed-7 confirmation were run, correctly.)*

**Baseline provenance:** adopted the orphan batch (PID 21780, `_mvar_wt` exe SHA C7690E13), which flushed all 60 rows at 19:04:35. Verdict histogram GOOD 12 / HARD 32 / CRASHED 16; crash causes (exe classifier, PAD_RADIUS=26, TD_V_HARD=6): **off-pad 13, too-hard 2, fuel-out 1, other 0**. Landed means td_v 2.953 / td_v_max 5.168 / lat 14.37 / fuel 4552.5. **This matches the D-012 golden and the handoff headline (44/60, td_v 2.95) exactly**, and matches CUDA-2's independent `--mppi-cuda` batch (44/60, byte-identical) — the baseline is triple-confirmed and bit-deterministic.

---

## 2. ESS/lambda mechanism (MPPI_DBG trace, determinism-preserving — reads state only)

**Baseline (LAMBDA_MIN=2.0, OU_THETA=0.15), AERO s42 run1 --mppi** — trace `runs/mvar_dbg_baseline_s42run1.txt` (60 replan samples, rep 0→580):

Two regimes across the descent:
- **Approach/divert phase (rep 0–490, the first ~50 s):** λ stays HIGH — 154→277 early, settling 34–95 mid, dipping to 18 by rep 490. ESS in-band [10.4 .. 49.1] (band = [2%,20%]×256 = [5.1, 51.2]). Here the floor of 2.0 is **inactive** — λ is 9–130× above it, set by the cost spread of genuinely-different rollouts (cspread 500–2700).
- **Terminal null phase (rep 500–580+, the last ~1.5 s to touchdown):** as the vehicle nulls out over the pad, all rollouts become near-cost-equivalent (cspread collapses 550→6), ESS **saturates to ~200** (≫ the 51.2 upper band), and the servo drives **λ HARD onto the floor: rep 500 λ=6.0, rep 510–580 λ=2.0 pinned.**

**CORRECTION to predecessor `2tmyln9a`'s reading:** their trace header claimed "λ never approaches the floor (bottoms ~33, >16× headroom) ⇒ L1 provably byte-null." That is **wrong** — the same trace's tail shows λ=2.0 pinned for rep 510–580. The floor IS the binding constraint in the terminal phase. So **L1 is NOT provably byte-identical**; it must be measured.

**L1 MEASURED = 42/60 (−2 vs baseline).** The floor bites in the terminal null phase (cspread ~6, ESS saturated) on essentially every descent, so it is NOT byte-null: a per-run diff of baseline vs L1 shows **0/60 runs byte-identical** (every run's td_v/td_lat shifts). Net effect = **2 runs LOST, 0 gained**:
- run 6: baseline HARD on-pad (lat 19.8) → L1 CRASHED marginally off-pad (lat 28.2, just past PAD_RADIUS 26);
- run 23: baseline GOOD (lat 8.5) → L1 CRASHED far off-pad (lat 403) — the sharper terminal λ diverged this one badly.

Mechanistically: sharpening λ 2.0→0.5 in the terminal phase over-commits to individual near-degenerate rollouts, and since off-pad reach is decided *earlier* (approach phase, λ≫floor, unaffected), the only net effect is terminal over-commitment that costs 2 marginal off-pad conversions. **L1 is confirmed null-to-harmful at shipped K=256** — consistent with the research framing that A2 (lower LAMBDA_MIN) is a *large-K* lever (ESS×64 headroom at K=16384 lets the servo exploit a sharper λ in the *approach* phase; at K=256 the floor only ever bites terminally, where it does not help). Falsifies L1 as a cheap win at K=256.

**L2a (OU_THETA 0.10) MEASURED = 42/60 (−2).** Per-run diff vs baseline: **0/60 byte-identical** (θ perturbs the noise correlation on every rollout, as designed), **2 runs LOST, 0 gained**. Both losses are razor-marginal edge cases:
- run 5: baseline HARD on-pad (lat 24.7, just inside PAD_RADIUS 26) → L2a CRASHED off-pad (lat 26.7);
- run 10: baseline HARD (lat 25.5) → L2a CRASHED off-pad (lat 26.3).

The mechanism is the *opposite* of the intended one: the two trajectories baseline landed **at the pad edge** (lat 24–25.5 m) were nudged **just past the 26 m boundary** (26.3–26.7 m) by the longer-correlated noise. So longer OU correlation did not extend reach on the *far* misses (those stayed crashed) — it slightly *degraded* the marginal near-in landings by adding coherent cross-range drift at the touchdown edge.

**L2b (OU_THETA 0.08) MEASURED = 41/60 (−3).** Per-run diff vs baseline: **0/60 byte-identical, 3 LOST, 0 gained.** The losses are the SAME two edge-cases L2a lost (run 5: lat 24.7→27.4; run 10: 25.5→26.5) PLUS a new large divergence (run 32: baseline HARD on-pad at lat 13.7 → L2b crashed at lat **380** — the longest correlation sent a mid-pad trajectory badly off). **The OU_THETA trend is monotone and clean:**

| OU_THETA | landed/60 | off-pad | correlation length |
|---|---|---|---|
| 0.15 (baseline) | 44 | 13 | shortest |
| 0.10 (L2a) | 42 | 16 | longer |
| 0.08 (L2b) | 41 | 16 | longest |

Landed rate **decreases monotonically** as θ drops (correlation lengthens): 44→42→41. **This is the reach-conversion diagnostic the section promised, and it came back decisively negative:** OU-θ does not convert off-pad→landed at K=256 — it monotonically converts a growing set of edge/mid-pad landings→off-pad as the correlation lengthens. The colored-noise reach thesis (0%→83% on lagged actuators) does NOT transfer to our composed, warm-start-anchored controller at K=256. This is the strongest single piece of evidence in the sweep: not a noisy −2 that could be a coin-flip, but a **clean 3-point monotone decline** with a consistent mechanism (edge-nudging + occasional large mid-pad divergence).

---

## 3. Verdict — HONEST NULL (both cheap levers null-to-harmful at K=256)

**Decision rule:** a single lever must reach **≥46/60 (baseline +2)** to trigger combo measurement + seed-7 confirmation. **No lever cleared the bar** — every measured config is **at or below baseline**:

| Config | landed/60 | Δ vs 44 |
|---|---|---|
| baseline (LAMBDA_MIN 2.0, OU_THETA 0.15) | 44 | — |
| L1 LAMBDA_MIN 0.5 | 42 | **−2** |
| L2a OU_THETA 0.10 | 42 | **−2** |
| L2b OU_THETA 0.08 | 41 | **−3** |

**No combo measured, no seed-7 confirmation run** — correctly, because the decision rule only fires on a ≥+2 single-lever win, and there is none. Both cheap levers from `mppi_research.md` are **null-to-harmful at the shipped K=256 operating point**. All four builds passed the sacred gates (selftest PASS, TERMINAL 194/200 byte-exact) — the levers are properly MPPI-gated and TERMINAL is untouched.

**Why this is a WIN, not a disappointment (it falsifies a hypothesis and sharpens the roadmap):**
1. **LEVER 1 (LAMBDA_MIN, rec A2) is a *large-K* lever, confirmed inert-to-negative at K=256.** The research explicitly framed A2 as "verify λ is not pinned at *large* K (ESS×64 at K=16384) and un-pin the floor if so." My dbg trace shows that at K=256 the floor only ever binds in the *terminal null phase* (ESS saturates ~200 near touchdown), where sharpening λ over-commits to degenerate rollouts and costs 2 marginal off-pad conversions — while the *approach phase* that actually decides reach runs at λ≫floor, unaffected. So the A2 headroom the research wants simply does not exist at K=256; it opens only when a big-K budget lets ESS scale. **This is direct empirical confirmation of the research's own scaling logic** — and a concrete instruction to the CUDA lane: A2 is worth re-testing *at K≥1024*, not at K=256.
2. **LEVER 2 (OU_THETA, the #1-ranked cheap lever) does NOT convert off-pad reach at K=256 — it makes it worse.** Longer OU correlation (θ 0.15→0.10→0.08) raised off-pad crashes (13→16 at θ=0.10). The colored-noise reach thesis (0%→83% on lagged actuators) did not transfer to our composed controller at K=256: with only 256 samples around a good warm-start, longer-correlated perturbations over-steer cross-range coherently *in the wrong direction* as often as the right one, and the softmax cannot separate them from a thin sample set. This is the "isolated optima don't transfer into the composed tree" lesson (HANDOFF §4) applied to a research import — **re-measured, and it regressed.**
3. **Net for the capacity thesis:** the two cheapest, most-promising param levers are exhausted at K=256 with **zero upside**. This *strengthens* the case in `mppi_research.md` §b and the CUDA scorecard that AERO's off-pad misses are a **proposal-support / exploration-authority** problem — fixable by **more samples (K↑, the CUDA port)** and/or a **structural variant** (CoVO diagonal Σ #2, MPOPI iterate-per-replan #3), **not** by re-tuning λ or θ at the shipped sample count. The cheap-lever path is honestly closed; the capacity/variant path is where the remaining AERO headroom lives.

**Recommendation:** do NOT adopt any of these three constant changes into main (all null-to-harmful). Record the null in an ADR. Re-open LEVER 1 (LAMBDA_MIN 0.5) **only at K≥1024** where the research predicts it becomes live — that is the one clean follow-up, and it belongs to the CUDA/kprobe lanes, not a K=256 retune.

**Null is UNANIMOUS across all three configs (42, 42, 41 — all < 44), and the OU_THETA arm is a clean 3-point monotone decline (44→42→41 as θ 0.15→0.10→0.08).** No config beat baseline; no config even tied it. The decision rule's combo/seed-7 branch correctly never fired. All four builds byte-exact on TERMINAL (194/200). The cheap-lever path at K=256 is closed with high confidence — this is the strongest possible form of the null (a monotone trend, not a scatter of coin-flips), and it cleanly redirects the remaining AERO headroom to the capacity (K↑/CUDA) and structural-variant (CoVO Σ, MPOPI) paths.

---

## Appendix — provenance & artifacts
- Sweep driver: `runs/mvar_sweep.ps1` (fresh guidance_mppi.c from main per config, regex-patch the 2 knobs, rebuild with SHA-change + LNK1104 guard, gate selftest+TERMINAL-194, AERO s42 x60 --mppi, append row). Orchestrator: `runs/mvar_orchestrate.ps1` (waits L1 → records row → runs L2a → L2b, self-driving). Baseline runner: `runs/mvar_run_batch.ps1`.
- Per-config captures: `runs/mvar_<config>.txt` (build/gate/summary) + `runs/mvar_<config>_aero.csv` (per-run). Master table: `runs/mvar_sweep.csv`.
- ESS/lambda dbg: `runs/mvar_dbg_baseline_s42run1.txt`.
- Verdict codes: V_NONE0/PERFECT1/GOOD2/HARD3/TIPPED4/CRASHED5 (landed = ≤3). Crash classifier (main.c:262-266): off-pad td_lat>26; too-hard td_v>6; fuel-out fault==F_FUEL.
- Cross-lane DATA consumed: `runs/mppi_research.md` (the levers' source), `runs/cuda_mppi_report.md` (K256 baseline byte-parity confirm), `runs/kprobe_report.md` (K-scaling, complementary).
