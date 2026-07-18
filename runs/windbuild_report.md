# WINDBUILD REPORT — the NAV-legal wind estimator, built and FALSIFIED at Stage 0

**Author:** WINDBUILD (Opus 4.8) · **Date:** 2026-07-18 (night, post-D-012) · **Lane:** windbuild
**Worktree:** `_wind2_wt/` (CMakeLists + core copy; VS2022 x64; gitignored — the real tree was NEVER touched)
**Blueprint:** `runs/windthink_design.md` (WINDTHINK's design) + intercom windthink #1234/#1247
**Builds on / attacks:** D-012 graze anatomy (the 33 m trim-residual settle; DECISIONS D-012 addendum),
D-009 wind-floor mechanism, D-010 C14 integral, the OBS-B estimator math.

---

## 0. VERDICT (one paragraph)

**Stage 0 FALSIFIED the wind estimator. Stage 1 was NOT built — correctly, per the design's own
decision rule ("if the bar fails: diagnose, fix, or record the falsification and STOP").** The
estimator was implemented exactly per the design (OBS-B AoA-corrected, τ=5 s low-pass,
freeze-at-ignition, in the `Sim` struct, MPPI-exempt) and is **byte-transparent** — with its output
fed into nothing, ENTRY/AERO/TERMINAL/MPPI reproduce the D-012 baselines bit-for-bit (zero hidden
coupling). But the estimator-error bar **failed by an order of magnitude**: the mean absolute wind
error at ignition is **21.7 m/s (ENTRY) / 18.5 m/s (AERO)** against the design's **< 2 m/s** ship bar
(the probe's idealized **< 0.2 m/s**). The error equals or exceeds the true wind magnitude itself
(16–22 m/s) — the estimate is anti-informative. The root cause, measured directly from truth across
40 runs: **the vehicle holds a mean true angle-of-attack of 9–11° (transiently 20–53°) throughout
its fins-deployed aero-descent**, because it is continuously diverting to null the cross-range offset
and its fin-rate-limited attitude loop oscillates. The estimator's foundational premise — the
airframe weathervanes so body −Z tracks the relative wind (AoA ≈ 0) — **never holds in the composed
sim**. At |v_rel| ~ 250 m/s the probe's own measured sensitivity (4.5 m/s per degree of AoA) turns
that 9–11° into ~40 m/s of raw wind error. Injecting an 18–22 m/s estimate whose *direction* is
essentially uncorrelated with truth, as a ≤40 m aim pre-bias, is precisely the design's §6
failure mode ("a garbage estimate injected as a pre-bias moves the aim-point deterministically
wrong… can actively degrade a clean lander"). **Recommendation: shelve the estimator; the path to
M6/M4 is Roadmap A (MPPI capacity), which rejects wind by replanning on the fresh state, not by
open-loop inference. The estimator + debug tap are preserved in `_wind2_wt/` as the artifact that
settles the question.** This is the asymmetry the design predicted: small cost, decisive result.

---

## 1. WHAT WAS BUILT (Stage 0)

Implemented strictly per `windthink_design.md` §2 + §7 checklist:

### 1.1 Estimator state — `_wind2_wt/core/sim.h` (Sim struct, after line 30)
```c
double wind_est[2];        /* filtered horizontal mean-wind estimate (world) [m/s] */
int    wind_est_valid;     /* 0 until the observability gate first fires */
double wind_est_frozen[2]; /* value latched at ignition (coast through the shielded burn) */
int    prev_engine_on;     /* ignition-edge detector for the freeze latch */
```
Sibling to `lat_eint[2]`; zero-initialized by the existing `memset` in `sim_init`. No new init path.

### 1.2 Estimator update — `_wind2_wt/core/sim.c` (new block, lines 311–423, after the C14 integral)
- **Gate** (`is_gtick && GM_HOVERSLAM`): `fins_deployed && !engine_on && qbar>500 && |v|>30 &&
  tilt<20°` — the unpowered aero-descent clean window; freeze in the burn.
- **OBS-B** (§2.1 B1): `zb2w = q_rot(nav.q, +Z)`; de-rotate the KNOWN commanded steering tilt out of
  `zb2w` (mirror control.c's `steer_sign=−1` unpowered + `a_vert_ref=G0+2`, **capped to control.c's
  ≤15° realized tilt**), then `w = v_horiz − |v|·(−zb2w_aero)_horiz`.
- **Filter:** first-order τ=5 s low-pass at the 50 Hz tick.
- **Freeze:** latch `wind_est_frozen` on the ignition edge (`engine_on && !prev_engine_on`).
- **Canon §4.3:** reads ONLY `nav.y[QX..QW]`, `nav.y[VX..VZ]` — never `wind_world`/`wind_filt`.
- Constants: `QBAR_OBS_FLOOR 500`, `V_OBS_FLOOR 30`, `TILT_OBS_MAX 20°`, `TAU_WIND 5.0`.

### 1.3 Debug tap — `_wind2_wt/core/sim.c` (`#ifdef WIND_DBG`, env-var `BL_WIND_DBG` gated, OFF by default)
Emits per observable guidance tick to stderr:
`t h qbar tilt eng | TRUEx TRUEy | obsA_x obsA_y | fobsA_x fobsA_y | estB_x estB_y | trueAoA cmdTilt obs`
where `TRUE*` is the true MEAN wind (**computed inline — NOT via `wind_sample`, which mutates
`wind_filt` and would break determinism**; a bug I hit and fixed, see §4), `trueAoA` is the actual
AoA the body holds off the TRUE relative wind (the observability ceiling, computed from truth,
independent of any estimator), `obsA` is the uncorrected estimate, `fobsA` a τ=5 s filtered OBS-A,
`estB` the shipped estimator. Two build dirs: `build/` (clean, for transparency) and `build_dbg/`
(compiled `/DWIND_DBG`, for the error measurement).

### 1.4 Stage 1 (`GuidanceCmd.aim_bias`) — NOT built.
Correctly gated behind Stage 0 per the design. See §3 for why building it on an 18–22 m/s estimate
would be actively harmful.

---

## 2. STAGE 0 RESULTS

### 2.1 Byte-transparency — PROVEN (the estimator computes but injects nothing)
All captured on the `build/` (non-DBG) exe vs the baselines captured on the UNMODIFIED worktree copy.

| Gate | Baseline (unmodified) | With estimator | Verdict |
|---|---|---|---|
| `--selftest` | PASS | PASS | ✅ |
| TERMINAL s42 ×200 | **194/200** | **194/200** | ✅ byte-exact |
| ENTRY s42 ×100 | **88/100** (op5 th5 fuel2) | **88/100** (op5 th5 fuel2) | ✅ byte-exact |
| AERO_OFFSET s42 ×300 | **220/300** (op47 th30 fuel1) | **220/300** (op47 th30 fuel1) | ✅ byte-exact |
| MPPI run 1 (invariance) | HARD td_v 2.63 lat 10.48 | HARD td_v 2.63 lat 10.48 | ✅ byte-identical |
| ENTRY run 1 (determ.) | HARD td_v 4.61 lat 6.45 | HARD td_v 4.61 lat 6.45 | ✅ |
| ENTRY run 3 (the 31.8 m graze) | CRASHED td_v 5.48 lat 31.81 | CRASHED td_v 5.48 lat 31.81 | ✅ |
| AERO run 1 (determ.) | GOOD td_v 3.60 lat 3.83 | GOOD td_v 3.60 lat 3.83 | ✅ |
| Determinism pair ENTRY run 5 (×2) | — | td_v 5.42 lat 20.96 == td_v 5.42 lat 20.96 | ✅ |
| Determinism pair AERO run 2 (×2) | — | td_v 1.85 lat 28.57 == td_v 1.85 lat 28.57 | ✅ |

**Conclusion:** the estimator has ZERO coupling into guidance/control/plant. The `Sim`-struct
placement + `fins_deployed`/`GM_HOVERSLAM` gating are correct; TERMINAL (fins stowed) never touches
it (byte-identical, directive 9). The transparency half of the Stage-0 bar is fully met.

### 2.2 Estimator error at ignition — FAILED (the frozen value the feedforward would consume)
20 ENTRY + 20 AERO runs (s42), debug tap on. Per-run "freeze value" = the last observable
(pre-ignition) tick's estimate; error = `|estimate − true mean wind|` at that tick. Window stats
over the whole observable aero-descent.

| Scenario (n=20) | mean freeze **\|est−true\|** | mean freeze \|OBS-A−true\| | mean freeze \|τ-OBS-A−true\| | mean window \|OBS-A−true\| | mean true AoA | design bar |
|---|---|---|---|---|---|---|
| **ENTRY** s42 | **21.72 m/s** | 32.30 | 37.30 | 71.88 | 9.12° | **< 2 m/s** |
| **AERO** s42 | **18.52 m/s** | 21.32 | 38.19 | 58.53 | 10.53° | **< 2 m/s** |

- **Every one of the 40 runs** has freeze error 7–36 m/s. Not one approaches 2 m/s.
- The true winds at ignition are 16–22 m/s, so **the error ≈ the signal itself** — the estimate
  carries essentially no usable direction information.
- The OBS-B commanded-AoA correction **does help** (est 18–22 < raw OBS-A 21–32): subtracting the
  *commanded* tilt removes part of the error. But it cannot remove the *uncommanded* attitude
  oscillation, which dominates.
- The **τ=5 s filter makes it WORSE** (37–38 m/s) — proof the error is **not zero-mean** in wind
  space (the sin nonlinearity of the AoA→error map + the sustained divert bias), so no amount of
  low-pass averaging recovers the mean. (The design's τ analysis was for a *weathervaned* body with
  a small Dryden gust; here the "noise" is a large, biased, deterministic divert-attitude signal,
  not a zero-mean gust.)

### 2.3 The decisive physical measurement — the body does NOT weathervane
`trueAoA` is computed entirely from **truth** (true wind, true inertial v, true attitude) — it is a
direct measurement of the plant, independent of the estimator. Representative trajectory, ENTRY
run 3 (the canonical 31.8 m trim-residual graze), coarse-sampled:

```
 t[s]   h[m]   qbar[Pa]  trueAoA[deg]  |OBS-A − true|[m/s]  Wtrue[m/s]
 39.5  33144    500       -26.3           23.1               13.3
 ~65   ~23000   ~23000     -1.8           86.0               31.7   (AoA passes ~0, but error still huge*)
 ~76   ~17000   ~17000    -11.9           74.6               30.3
 ~88   ~10000   ~49000    -15.5           95.5               27.3
 ~100   5179    ~??        -0.8           25.4               17.6
 ~106   3207    41000      -6.2           26.1               13.8  (freeze value)
```
The true AoA **swings between −26° and +7° continuously** through the descent. (*Even where AoA
momentarily crosses ~0, the error is large because the filter carries the recent large-AoA history
and the vehicle's horizontal velocity coupling is non-negligible; a momentary zero-crossing does not
give a clean instantaneous read.)

**No exploitable clean sub-window exists.** Restricting to only the cleanest ticks:
- AERO run 4: **0 / 1399** observable ticks have OBS-A error < 3 m/s (best single tick 5.04 m/s).
- ENTRY run 17: **115 / 3301** ticks < 3 m/s (**3.5 %**) — but these are transient AoA-zero-crossings,
  scattered, and impossible to isolate in-flight (identifying them requires knowing the true wind).
- AERO run 17: **14 / 1418** ticks < 3 m/s (**1.0 %**).

96.5–100 % of the observable window has > 3 m/s error. The estimator cannot be salvaged by tighter
gating.

---

## 3. WHY STAGE 1 WAS NOT BUILT (the decision-rule application)

The design (§4 Stage 0) is explicit: *"Do not proceed to Stage 1 until Stage 0 passes — a
feedforward on a bad estimate is worse than no feedforward,"* and the mission: *"If the bar fails:
diagnose, fix, or record the falsification and STOP — that itself is the deliverable."*

Stage 1's `aim_bias` shifts the divert target upwind by `−k_ff·ŵ_unit·drift_pred`, magnitude-capped
at ~40 m. It consumes `wind_est_frozen`. With a frozen estimate whose error is 18–22 m/s and whose
**direction is essentially uncorrelated with the true wind** (the error exceeds the signal), the
`ŵ_unit` fed to the pre-bias points in a near-random horizontal direction. A ≤40 m aim shift in a
random direction, applied deterministically from ignition, is exactly:

> §6.1: *"A latched gust or a garbage estimate injected as a pre-bias moves the aim-point
> deterministically wrong. … A feedforward that AIMS can actively degrade a clean lander."*

The 33 m trim-residual graze (run 3) is a **flawless landing that settles 31.8 m downwind** — the
one thing a correct upwind pre-bias would fix. But an upwind bias computed from a wrong-by-20 m/s
wind vector would, on the majority of the 194 currently-landing TERMINAL-adjacent / on-pad ENTRY
runs, shove the aim-point in a wrong direction and **convert on-pad landers into off-pad grazes** —
strictly worse than the reactive C14 integral, which (being reactive + faded) can only ever leave a
*residual*, never actively push a good trajectory off-pad. Building and running Stage 1 on this
estimate would not be an honest experiment; it would be knowingly shipping the design's named poison.
**The correct, disciplined action is to stop here and record the falsification.**

(For completeness: one *could* imagine gating the pre-bias to only fire when some in-flight proxy
says the estimate is trustworthy. But §2.3 shows the trustworthy ticks are < 3.5 %, transient, and
unidentifiable without the truth — there is no in-flight proxy that isolates them. The estimator is
not merely mistuned; it is measuring a quantity the plant does not expose during a divert.)

---

## 4. ROOT CAUSE & THE PROBE RECONCILIATION (honest post-mortem)

**The estimator math is correct; the operating point is wrong.** The algebraic inversion
"attitude + inertial v → horizontal wind" is exact *at weathervane trim* — WINDTHINK's probe
(`_wind_wt/windobs.c`) proved this to < 0.2 m/s. But the probe achieved that by **modeling the
realized attitude AS `−vrel_dir` by construction** (`windobs.c:182`: `zb2w = −vrel_true/|vrel|`) —
i.e., it *assumed the body weathervanes*. The live closed loop does not: the vehicle spends its
entire fins-deployed aero-descent in an **active, fin-rate-limited divert** (nulling the 500 m–3 km
cross-range offset), holding a mean true AoA of 9–11° with the attitude loop swinging continuously
(control.c:138 documents exactly this: *"tilt swinging 2..24 deg"*; the fins are rate-limited to
20°/s and heavily damped, so the attitude lags and oscillates around the commanded divert AoA). The
probe's OWN finding (2) predicted the consequence — **4.5 m/s of wind error per degree of AoA** at
|vrel| ~ 260 m/s — and 9–11° × 4.5 ≈ 40 m/s, which is exactly the raw OBS-A error we measure. The
probe measured its own failure mode; it just didn't model the flight condition that triggers it.

This is the **isolated-model-vs-composed-tree lesson** the HANDOFF names (§4: *"isolated-tree optima
do NOT transfer into the composed tree — re-measure every import"*). WINDTHINK re-derived the physics
faithfully but validated it in an idealized single-point trim, not in the composed guidance loop. The
in-sim measurement is what settles it — which is precisely why the design staged an estimator-only
truth-up *before* any feedforward, and why the mission demanded the real number be measured in-sim
rather than trusted from the probe. The staging worked as intended: **the cheap, decisive
measurement was taken, and it falsified the hypothesis.**

**Is the wind observable at all in this sim?** Not from attitude alone during a divert. It would
require either (a) a force-inversion observer (OBS-B2) that solves the AoA from the *realized* lateral
specific force including the uncommanded/oscillation component — but that component is driven by
fin-rate-limit dynamics and slosh, is not cleanly invertible tick-to-tick, and would still be
poisoned by the ~5° residual at ignition; or (b) restricting the estimate to a genuinely feathered,
attitude-settled window that the offset-nulling ENTRY/AERO descents never provide (they are still
actively diverting/oscillating at ignition — mean AoA 4.7° even when the command is feathered to
~0°). Neither is a small fix, and both fight the same wall.

---

## 5. RELATION TO THE D-012 PARETO WARNING

The design (§6 devil's-advocate) flagged: *"if Stage 1 shows the aim-shift converts trim-residual
grazes into too-hards (op→th, no net gain), then the estimator is confirmed to be hitting the same
wall reactively-shaped medicine hit, and it should be shelved in favor of MPPI capacity."* We did not
even reach that test — the estimator failed **upstream** of it, at the observability stage. But the
conclusion converges with the D-012 addendum: **the reactive/open-loop structure is exhausted for the
graze band.** D-012 closed the trim grid (null-to-negative) and the engine-cut rule (relight-blocked).
This work closes the wind-estimator-feedforward lever too — not because the feedforward is Pareto-
saturated, but because its input cannot be observed in the flight condition where it is needed. All
roads point the same way: **ENTRY 88 / AERO 73.3 are the plateau of the open-loop structure; the path
to M6 ≥ 90 and M4 ≥ 90 is MPPI capacity (Roadmap A: K 256→1024 CPU probe → M5 CUDA), which rejects
wind by replanning on the fresh nav state every tick — closed-loop, no open-loop wind inference
required.**

---

## 6. EVERY CHANGE (file + line, all in `_wind2_wt/` — the real tree is untouched)

| File | Location | Change |
|---|---|---|
| `_wind2_wt/core/sim.h` | Sim struct, after `NavState nav;` (~line 31) | +4 fields: `wind_est[2]`, `wind_est_valid`, `wind_est_frozen[2]`, `prev_engine_on` (D-013 estimator state) |
| `_wind2_wt/core/sim.c` | top includes (~line 13) | `#ifdef WIND_DBG` → `#include <stdio.h>`, `<stdlib.h>` (tap only) |
| `_wind2_wt/core/sim.c` | after C14 integral block (lines 311–423) | the D-013 estimator update block (`is_gtick && GM_HOVERSLAM`): observability gate, OBS-B de-rotation (capped), τ=5 s filter, ignition-edge freeze (lines 311–370); `#ifdef WIND_DBG` diagnostic tap (lines 377–421) |
| `_wind2_wt/winderr_batch.ps1` | new | estimator-error batch harness (per-run freeze + window error extraction from the tap) |

**Build provenance:** `_wind2_wt/build/` (clean Release) and `_wind2_wt/build_dbg/` (Release
`/DWIND_DBG`), both VS2022 x64, confirmed rebuilt (build lines observed) before every measurement.
The clean exe proves transparency; the DBG exe (tap off by default → byte-identical) produced the
error stats. **No file in `C:\Booster_Lander_Simulator\core\` was edited or built.**

**A bug caught and fixed mid-work (honesty note):** the first debug tap called `wind_sample()` to
get the true wind — but `wind_sample` mutates `s->st.wind_filt[]` (the plant's Dryden integrator),
which corrupted determinism (ENTRY run 3 td_v shifted 5.48→5.92). Fixed by computing the true MEAN
wind inline in the tap (the mean is the estimator's target; the Dryden gust is quasi-DC ~0.6 m/s).
After the fix, the DBG exe reproduces the baseline byte-exact (5.48/31.81). A second bug — an
off-by-one column index in the *analysis harness* (not the sim) — was caught by dumping and
verifying every token position against a raw tap line before trusting the population means; the
numbers in §2.2 are the corrected, verified values.

---

## 7. WHAT TO DO NEXT (recommendation)

1. **Shelve the estimator feedforward.** Do NOT build Stage 1 on this estimate. Do NOT re-attempt
   the attitude-only estimator during offset-nulling descents (measured: 18–22 m/s error, mean AoA
   9–11°, no clean window).
2. **Roadmap A — MPPI capacity** is the path to both M6 (ENTRY ≥ 90) and M4 (AERO ≥ 90). MPPI rejects
   wind by replanning on the fresh state; it needs no wind estimate. K 256→1024 CPU probe, then the
   M5 CUDA port (design ready: `runs/agentB_mppi_design.md` §5).
3. **Preserve `_wind2_wt/`** as the settling artifact — the estimator, the tap, and this report are
   the decisive measurement that closes the "NAV-legal wind estimator" question that has been open
   since the D-009 addendum. If a future session revisits it, the prerequisite is a flight regime
   where the body genuinely weathervanes (feathered, attitude-settled) at ignition — which the
   current ENTRY/AERO scenarios do not provide.
4. **ADR:** this warrants a `DECISIONS.md` D-013 entry (estimator built + falsified WITH numbers,
   per directive 8 "failed experiments get recorded WITH numbers"). Recommended for the main-session
   integrator to append (this agent did not edit the real tree, including DECISIONS.md).

---

## Appendix — reproduce
```powershell
# transparency (clean exe):
$exe="C:\Booster_Lander_Simulator\_wind2_wt\build\bin\Release\booster-core.exe"
& $exe --selftest ; & $exe --headless --scenario terminal --seed 42 --runs 200
& $exe --headless --scenario entry --seed 42 --runs 100
& $exe --headless --scenario aero_offset --seed 42 --runs 300
& $exe --run --scenario aero_offset --seed 42 --run 1 --mppi
# estimator error (DBG exe, tap on):
& C:\Booster_Lander_Simulator\_wind2_wt\winderr_batch.ps1 -scen entry -n 20 -seed 42
& C:\Booster_Lander_Simulator\_wind2_wt\winderr_batch.ps1 -scen aero_offset -n 20 -seed 42
# single-run trace with geometry:
$env:BL_WIND_DBG="1"; & "C:\Booster_Lander_Simulator\_wind2_wt\build_dbg\bin\Release\booster-core.exe" --run --scenario entry --run 3 2>&1 | Select-String "^WDBG"
```
