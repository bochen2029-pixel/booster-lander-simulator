# DECKNULL — cross-validation of the (KVEL_NEAR 1.7, KVEL_SPLIT_H 350) AERO lead

**Agents:** opus-4.8-nivxq7ko (started skeleton + Step 1) -> **opus-4.8-gl7gcjfk (DECKNULL-2, completed)** · lane decknull
**Date:** 2026-07-18 night · **Worktree:** `_dn_wt\` (own copy, real tree untouched)

**Mission:** Cross-validate the (KVEL_NEAR 1.7, KVEL_SPLIT_H 350) AERO lead from D-012 sweep2 — a +5/300 s42 AERO gain (225 vs 220) with the lowest too-hard seen (24). Flagged in D-012 as *noise-scale, single-seed, NOT cross-validated, NOT shipped.* DECKNULL-1 died in the idle-wait trap with the baseline MPPI batch (orphan PID 23356) still running; DECKNULL-2 adopted the lane, classified the orphan, finished the baseline check, rebuilt, and ran the full treatment.

## The change (directive-7 pair) — verified EXACTLY two files, nothing else

`fc`/Compare-Object of `_dn_wt\core\*` vs main tree confirmed the diff is PRECISELY:
- `core/guidance_hoverslam.c`: `#define KVEL_SPLIT_H 250.0 -> 350.0`, `#define KVEL_NEAR 1.6 -> 1.7` (lines 48-49; consumed only inside the `st->fins_deployed` block at 185-186 that ramps the -v_xy null gain).
- `core/guidance_mppi.c` (`cmd_from_u_lean` rollout mirror, lines 331-332): `bk=(250.0-h_feet)/250.0 -> 350`, `kvd=kbase + bk*(1.6-kbase) -> 1.7`.
- `core/guidance_hoverslam.h`: **IDENTICAL** (the D-012 KDIV_SEEK/BRAKE/VBLEND schedule is orthogonal and untouched).

TERMINAL is unaffected by construction: fins-stowed => `Kvd==Kvel`, so the split never engages (and TERMINAL reproduced 194 byte-exact under treatment, below).

## Verdict rule (from mission brief) — STRICT

ADOPT only if ALL of:
1. AERO s42+s7 combined improves by **>= 6 runs / 600**, AND
2. ENTRY all three seeds (s42/s7/s99) **within +-1** of baseline, AND
3. MPPI s42 x60 **within +-2** of 44.
Otherwise REJECT with numbers.

Baselines (D-012 shipped, spec winds; from frozen goldens `goldens/mc/*_d012_baseline.txt`):
AERO t0 s42 **220** / s7 **220**; ENTRY s42 **88** / s7 **79** / s99 **78**; MPPI s42 **44/60**.

---

## EXE PROVENANCE (which binary produced which number)

| Exe | Path | Built (LastWrite) | len | KVEL config | Produced |
|---|---|---|---|---|---|
| PRE-EDIT baseline | `_dn_wt\build\bin\Release\booster-core.exe` @ 17:43 | 2026-07-18 17:43:24 | 288768 | 250/1.6 | orphan PID 23356 MPPI 44/60 + predecessor's selftest/TERMINAL/ENTRY/AERO baseline set |
| **TREATMENT (1.7,350)** | same path, rebuilt | **2026-07-18 18:38:02** | 288768 | **350/1.7** | ALL Step-2 numbers below |
| frozen goldens | `goldens/mc/*_d012_baseline.txt` | 17:18-17:24 | — | 250/1.6 (main D-012) | baseline reference column |

**Orphan timeline classification (the stale-exe trap, resolved):** PID 23356 CREATED 17:53:13.122; source edits landed 17:53:41 (hoverslam) / 17:53:58 (mppi) — i.e. AFTER the proc launched from the 17:43 exe. Windows locks the running image, so the orphan ran the UNCHANGED-baseline exe. It exited 18:37:42 and landed **44/60 BYTE-IDENTICAL** to the D-012 golden (see below), confirming (a) the pre-edit baseline and (b) that no rebuild was possible until it released the lock. The treatment exe (18:38:02) was built the instant the lock cleared; its build recompiled exactly `guidance_hoverslam.c` + `guidance_mppi.c`.

---

## Step 1 — unchanged `_dn_wt` reproduces baselines EXACTLY (bit-determinism)

Pre-edit exe (17:43 build), MSVC 19.44, OpenMP ON, VS2022 x64 Release.

| Check | Expected | Got | Pass | Source |
|---|---|---|---|---|
| selftest | PASS | PASS | Y | predecessor (#1228) |
| TERMINAL s42 x200 | 194 | **194/200** | Y | predecessor (#1228) |
| ENTRY s42 x100 | 88 | **88/100** (op5 th5 fuel2) | Y | predecessor (#1228) |
| AERO s42 x300 | 220 | **220/300** (op47 th30 fuel1 oth2) | Y | predecessor (#1228) |
| AERO --mppi s42 x60 | 44 | **44/60 BYTE-IDENTICAL to golden** | Y | orphan PID 23356, DECKNULL-2, `runs/dn_mppi_baseline_unchanged.txt` |

Orphan output vs golden `aero_mppi_s42_d012_baseline.txt` — identical to the field: `44/60; PERFECT 0 GOOD 12 HARD 32 CRASHED 16; off-pad 13 too-hard 2 fuel-out 1; td_v 2.95 lat 14.37`. Pipeline bit-deterministic; worktree == main-tree/golden.

---

## Step 2 — measurements at (KVEL_NEAR 1.7, KVEL_SPLIT_H 350) — TREATMENT exe 18:38:02

### Gates (run first, all GREEN)
- **selftest: PASS**
- **TERMINAL s42 x200 = EXACTLY 194/200** (PERFECT 15, GOOD 167, HARD 12, CRASHED 6, off-pad 6; td_v 1.93). The parity gate holds byte-exact — the deck-null change is correctly fins-gated and does NOT leak into TERMINAL.
- **MPPI determinism pair** (AERO s42 run 1 --mppi, twice): **IDENTICAL** (RESULT: HARD td_v=2.52 lat=9.88 tilt=0.23 fuel=4443 t=58.6 maxq=35438, both runs). The changed rollout mirror is bit-deterministic.

### Config x seed x cause table (treatment vs baseline)

| Config | Seed | Runs | Landed | rate | off-pad | too-hard | fuel | other | Baseline | Delta | Within rule? |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ENTRY | 42 | 100 | **88** | 88.0% | 4 | 6 | 2 | 0 | 88 | **0** | Y (+-1) |
| ENTRY | 7  | 100 | **80** | 80.0% | 6 | 8 | 6 | 0 | 79 | **+1** | Y (+-1) |
| ENTRY | 99 | 100 | **79** | 79.0% | 4 | 15 | 1 | 1 | 78 | **+1** | Y (+-1) |
| AERO t0 | 42 | 300 | **225** | 75.0% | 49 | 24 | 1 | 1 | 220 | **+5** | (reproduces sweep2 225 exactly) |
| AERO t0 | 7  | 300 | **227** | 75.7% | 47 | 21 | 2 | 3 | 220 | **+7** | (decision batch) |
| AERO MPPI | 42 | 60 | **41** | 68.3% | 16 | 1 | 1 | 1 | 44 | **-3** | **N (fails +-2)** |

**ENTRY combined: 247 vs 245 baseline -> +2/300, every seed within +-1.** Cause note: the split shuffles off-pad->too-hard (matches sweep2's op4/th6 at s42) but nets slightly positive.
**AERO combined: 225 + 227 = 452 vs 440 baseline -> +12 / 600.** Both seeds gain; too-hard is the lowest ever at both (24 / 21). s42 reproduces sweep2's 225 byte-for-byte -> pipeline trustworthy, gain is real (not a stale-exe artifact).

### Landed-quality (no regression on the tier-0 side)
td_v: AERO s42 3.31 (base 3.38), s7 3.16; ENTRY ~3.6-3.7 (base 3.77). Tilt/lat unchanged. No fuel/struct/thermal fault inflation on tier-0.

### MPPI regression anatomy (the decisive datum)
AERO --mppi s42 x60: **41/60 (-3 vs 44)**, bit-deterministic (determinism pair IDENTICAL, treatment exe 18:38:02 confirmed in log). The 3 lost landings are ALL **off-pad**: crash causes off-pad **16 (was 13, +3)**, too-hard 1 (was 2), fuel-out 1 (was 1). td_v 2.94 (base 2.95), lat 14.14 (base 14.37) — landed-quality unchanged, the loss is purely reach. Mechanism: the (1.7,350) deck-null strengthens the near-deck -v_xy null gain (kvd ramps to 1.7 over 350 ft vs 1.6 over 250 ft). Mirrored into `cmd_from_u_lean`, it over-damps cross-range velocity late in the MPPI-executed descent on 3 marginal-reach cases, pulling them off-pad. Batch wall 2311s (38 min under 4-9 proc fleet contention).

---

## VERDICT: **REJECT**

The (KVEL_NEAR 1.7, KVEL_SPLIT_H 350) deck-null is **NOT adopted**. Strict rule requires ALL THREE; rule 3 fails.

| Rule | Requirement | Result | Pass |
|---|---|---|---|
| 1 | AERO s42+s7 combined **>= +6 / 600** | **+12/600** (s42 225 +5, s7 227 +7) | **PASS** |
| 2 | ENTRY s42/s7/s99 all **within +-1** | s42 88 (0), s7 80 (+1), s99 79 (+1) | **PASS** |
| 3 | MPPI s42 x60 **within +-2** of 44 | **41/60 = -3** | **FAIL** |

**Rationale for honoring the strict rule (not overriding on the strong AERO win):** the -3 is real (bit-deterministic, mechanistically explained as +3 off-pad from the mirrored over-damping), and it hits the ONE subsystem the roadmap names as the path to BOTH remaining gates (M4 AERO >=90 and M6 ENTRY >=90 are both "MPPI capacity" per HANDOFF §8/§A). Trading -3 MPPI reach for +12 tier-0 AERO is exactly the wrong direction: tier-0 AERO is explicitly NOT the M4 path (D-012: "M4 path = MPPI capacity, not tier-0 tuning"). The mission's >2-MPPI-delta red-flag guard exists to catch precisely this, and it fired. So: REJECT, keep main at (250, 1.6).

**No integration patch** (rejected). For the record, the ADOPT patch WOULD have been (main-tree coordinates), had rule 3 passed:
- `core/guidance_hoverslam.c:48` `#define KVEL_SPLIT_H  250.0` -> `350.0`
- `core/guidance_hoverslam.c:49` `#define KVEL_NEAR     1.6` -> `1.7`
- `core/guidance_mppi.c:335` `double bk=(250.0-h_feet)/250.0; ...` -> `(350.0-h_feet)/350.0`
- `core/guidance_mppi.c:336` `double kvd=kbase + bk*(1.6-kbase);` -> `bk*(1.7-kbase)`

### Follow-up worth an ADR (not done here — out of DECKNULL scope)
The tier-0 gain is genuine and Pareto-interesting (+12/600, too-hard the lowest ever at both seeds). The blocker is ONLY the MPPI mirror. A future study could **decouple** the deck-null between the two consumers: keep hoverslam's tier-0 law at (350,1.7) but leave the MPPI rollout mirror at (250,1.6) — i.e. deliberately break the directive-7 mirror for THIS one gain, IF an MPPI-execution invariance study shows the tier-0 hoverslam path and the MPPI-executed path genuinely want different near-deck damping. That is a directive-7 exception requiring its own ADR + the full leak analysis; flagged, not attempted. (Note: this contradicts directive 7 as written, so it may simply be correct to leave (250,1.6) everywhere and pursue MPPI capacity via K instead, per roadmap A.)

---

## Provenance summary (exe per number)
- **Baselines** (44/60, 88, 79, 78, 220, 220): frozen D-012 goldens `goldens/mc/*_d012_baseline.txt` (main tree, KVEL 250/1.6), corroborated live by the pre-edit `_dn_wt` orphan (44/60 byte-exact) + predecessor's foreground checks (#1228).
- **Treatment** (194 gate, det-pair, ENTRY 88/80/79, AERO 225/227, MPPI 41): `_dn_wt` exe **18:38:02** (KVEL 350/1.7, verified in source + log header). Full log: `runs/dn2_treatment.log`. Orphan baseline log: `runs/dn_mppi_baseline_unchanged.txt`.
- Sweep2 provenance for the 225 target: `runs/d012_sweep2.csv` row (1.7,350) — reproduced byte-exact here (225, too-hard 24), validating the pipeline end-to-end.
