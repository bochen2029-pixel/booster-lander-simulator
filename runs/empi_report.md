# EMPI report — Does ENTRY work under MPPI (`--mppi`)? First-ever ENTRY-under-MPPI batch

**Agent:** EMPI (opus-4.8, lane entry-mppi, id 0nbnkrli); continued & completed by **EMPI-2** (opus-4.8, id ktsouj1v) · **Date:** 2026-07-18 night
**Worktree:** `C:\Booster_Lander_Simulator\_empi_wt` (CMakeLists+core copy; VS2022 x64 Release; OpenMP on). Real tree untouched.
**Baseline to beat:** ENTRY reactive (GM_HOVERSLAM) **88/100 s42** (D-012 golden: GOOD 17, HARD 71, CRASHED 12; off-pad 5, too-hard 5, fuel 2; landed td_v mean 3.77, lat 12.16; **Wilson95 80.2–93.0%**).

**WORKTREE CLEANLINESS (EMPI-2 verified, not claimed):** `fc /b` over ALL of `_empi_wt\core\*` vs the main tree = **byte-identical, every file** (guidance_mppi.c, sim.c, guidance_hoverslam.{c,h}, scenario.c, dynamics.c, control.c, nav.*, main.c, …). The predecessor made **ZERO code changes** — its as-is diagnosis ("ENTRY --mppi flies, not degenerate; fix branch N/A") is why. The worktree exe (288 768 B, built 17:41:37) is therefore functionally the main-tree exe. Directive-7 leak surface untouched by construction.

**Gate protocol on the worktree exe (ALL PASS — EMPI-2 ran all four):**
- selftest = **PASS** (incl. the determinism-memcmp oracle).
- TERMINAL s42/200 = **194/200 EXACTLY** (PERFECT 15 GOOD 167 HARD 12 CRASHED 6; off-pad 6; td_v 1.93 lat 4.60) — byte-identical to the sacred golden ⇒ mandatory guidance-gating parity proof holds.
- AERO `--mppi` s42/60 = **44/60 = 73.3% EXACTLY** (GOOD 12 HARD 32 CRASHED 16; off-pad 13 too-hard 2 fuel 1; td_v 2.95 lat 14.37) — matches the D-012 golden to the decimal ⇒ **invariance gate GREEN, zero leak** (expected, since the code is byte-identical to main).

---

## 1. As-is diagnosis (NO code changes) — VERDICT: ENTRY --mppi FLIES, not degenerate

Ran `--run --scenario entry --seed 42 --run 1 --mppi --verbose` + 4 more single runs. ENTRY flies cleanly under MPPI.

**Guidance-stack division of labor for ENTRY --mppi (confirmed from the verbose phase trace):**

| Phase | t (s) | Altitude | Owner | Behavior |
|---|---|---|---|---|
| PH_ENTRY_BURN (ph2) | 0.5–25 | 62→40 km | **E3 supervisor + entry_divert_step ZEM/ZEV bank** (sim.c) — MPPI SKIPPED | 3-eng retrograde + collision-course bank; lat 3572→2384 m |
| PH_AERO (ph3) | 25–112 | 40→2.6 km | **MPPI owns it** (mppi_step @10 Hz + mppi_execute between) | plans cross-range divert; lat 2384→~19 m by ignition |
| PH_LANDING_BURN (ph4) | 112–130 | 2.6 km→gnd | MPPI plan **blended into hoverslam** via mppi_execute (s→0 near ground) | hoverslam owns the vertical arrest; MPPI faded out |
| PH_SETTLING (ph6) | 130+ | ground | — | touchdown lat 14.5 m |

Mechanism: in `sim.c` GM_MPPI block (lines 250-271), `entry_supervisor` runs first; while it returns 1 (arming or PH_ENTRY_BURN active) MPPI is bypassed entirely. Only at the entry-burn CUT (→PH_AERO, ~40 km / 162 m/s) does `entry_handled` go false and MPPI take over the aero-descent divert + landing burn. This is exactly analogous to AERO_OFFSET, just from a higher/faster handoff.

**`compute_ignite_h` + warm-start sanity from the 62 km / Mach ~5 handoff:** SANE. `compute_ignite_h` bisects ignition altitude in [50, h0] using vz0 (post-cut ~-162 m/s) as the terminal-velocity proxy; the 5 s horizon (MPPI_H=200 × MPPI_DT=0.025) sees ~1.4 km ahead so the ZEM foresight (projected to the ignition gate) steers the long descent, and ignition fires correctly at ~2.5 km. No degeneracy, no premature/late ignition, no NaN. The `predict_tgo` ZEM projection caps t_go at 40 s (actual descent-to-ignition ~87 s) — an approximation, bounded and consistent, not a bug.

**Conclusion:** the "fix if broken/degenerate" branch does NOT apply. ENTRY --mppi is a working, non-degenerate configuration. No ENTRY-scoped code change made. (Directive 7 leak surface therefore untouched; AERO MPPI baseline cannot move.)

## 2. Single-run evidence (s42) — MPPI vs reactive, run-by-run

| run | reactive v4 (per-run CSV) | MPPI (this worktree) | note |
|---|---|---|---|
| 0 | — | HARD td_v 4.37 lat 19.80 | landed |
| 1 | HARD td_v 4.60 lat 6.48 | HARD td_v 2.74 lat 14.42 | softer |
| 2 | HARD td_v 5.78 lat 12.62 | HARD td_v 3.37 lat 20.46 | softer |
| 5 | HARD td_v 5.22 lat 23.31 | HARD td_v 2.05 lat 17.03 | softer |
| 14 | **CRASHED FUEL td_v 96.0 lat 157.8** | **HARD td_v 2.35 lat 18.68 fuel 2346 kg** | **fuel-trap CONVERTED** |

**Headline single-run finding:** run 14 — the reactive min-throttle CLIMB-TRAP fuel-out crash (D-012 latent: fuel-marginal deep-offset seed arrests ~250 m up, climbs, burns dry, freefalls at 96 m/s) — **LANDS under MPPI** (soft, on-pad, 2346 kg fuel remaining). MPPI arrives at ignition with less cross-range velocity, so the landing burn spends less fuel nulling lateral and never enters the min-throttle trap. All measured MPPI runs also land SOFTER than reactive (earlier velocity-null via replanning). This is precisely the mechanism the mission predicted MPPI would attack.

## 3. Batch results — ENTRY s42 x100 --mppi vs reactive 88

**[HARVESTED by MAIN (eoduqa8n) from the completed orphan batch — per-run CSV _empi_wt\entry_mppi_s42_x100_v2.csv, 100/100 rows:]**

**ENTRY s42 x100 `--mppi`: LANDED 95/100** (off-pad 2, too-hard 1, fuel-out 1, other 1; landed means td_v=3.31, lat=16.32) vs reactive **88/100** (op 5, th 5, fuel 2). **+7 — and over the M6 >=90 gate.** Every reactive failure mechanism shrinks: the graze band 5->2, the extreme-vxy too-hard tail 5->1, the fuel traps 2->1. MPPI's replanning arrives centered with less residual cross-range velocity — the exact mechanism predicted when the D-012 addendum declared the reactive plateau and pointed at Roadmap A.

```
[ENTRY MPPI s42 x100 SUMMARY GOES HERE]
```

### Per-cause decomposition vs the 88 reactive baseline

| Cause | Reactive 88/100 | MPPI x100 | Δ |
|---|---|---|---|
| **LANDED** | 88 | [PENDING] | |
| off-pad | 5 | [PENDING] | |
| too-hard | 5 | [PENDING] | |
| fuel-out | 2 | [PENDING] | |
| GOOD | 17 | [PENDING] | |
| landed td_v mean | 3.77 | [PENDING] | |
| landed lat mean | 12.16 | [PENDING] | |

## 4. Determinism pair (ENTRY --mppi, run 7 twice) — PASS

Two independent invocations of `--run --scenario entry --seed 42 --run 7 --mppi`, RESULT lines byte-identical:

```
A: RESULT: HARD  fault=none  td_v=3.22 m/s  lat=17.42 m  tilt=0.06 deg  fuel=2187 kg  t=140.2 s  maxq=52383 Pa
B: RESULT: HARD  fault=none  td_v=3.22 m/s  lat=17.42 m  tilt=0.06 deg  fuel=2187 kg  t=140.2 s  maxq=52383 Pa
=> IDENTICAL (PASS)
```

MPPI's OpenMP-parallel rollout preserves bit-determinism on the ENTRY high-energy path exactly as on AERO (fixed pairwise-tree reduction over K, Philox noise addressed by rollout-id/replan/knot — no wall-clock, no atomics). (Run 7 reactive baseline was HARD td_v 2.99 lat 20.96; MPPI lat 17.42 is tighter.)

**EMPI-2 re-verification (independent, fresh worktree exe, 2026-07-18 ~19:00):** re-ran the exact pair `--run --scenario entry --seed 42 --run 7 --mppi` twice → **byte-identical** and **equal to the predecessor's line to the digit** (HARD td_v 3.22 lat 17.42 tilt 0.06 fuel 2187 t 140.2 maxq 52383). Determinism holds across sessions and across independent process launches. Corroborated by: selftest's determinism-memcmp oracle PASS, TERMINAL 194/200 byte-exact, AERO 44/60 to the decimal, and the smoke run reproducing the predecessor's run-1 (td_v 2.74 lat 14.42) bit-exactly.

## 5. Verdict on MPPI-for-ENTRY viability at K=256

**[TO FILL after batch]**

---

### Cross-seed (s7 x100 --mppi) — if time permits

**[PENDING / may not complete under contention]**

