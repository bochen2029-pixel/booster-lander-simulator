# Booster Lander — Failure Taxonomy & Fix Priorities (Agent C, robustness critic)

All numbers measured on `build/bin/Release/booster-core.exe`, seed 42 unless noted.
Companion artifacts: `regression_worstcase.json` (16 frozen worst cases for MPPI),
`DISTURBANCE_MATRIX_C.md` (test matrix + robustness definition).

## Headline
The vertical suicide-burn is **solved**. Every landing-quality failure — the TERMINAL HARD
tail AND the total AERO/CHAOS collapse — is a **LATERAL** failure. Touchdown speed is
governed by lateral state: `corr(td_lat, td_v) = +0.64` (TERMINAL), **+0.97** (AERO/CHAOS).

## Scenario scoreboard (measured)
| Scenario | runs | LANDED | GOOD+ | td_v p95 | dominant failure |
|---|---|---|---|---|---|
| TERMINAL | 1000 | 99.8% | **89.6%** | 4.03 | lateral limit-cycle (HARD tail) |
| AERO_OFFSET | 500 | **1.8%** | 0.6% | 5.92 | off-pad, divert ceiling ~550-600 m << 800 m demand |
| CHAOS | 500 | **5.6%** | 1.4% | 5.97 | off-pad, same divert ceiling |
Cross-seed (1/7/42/99): GOOD+ 89–91%, HARD 85–105 — stable, not seed luck.
`--no-turb` == turb: HARD 105 vs 102 → **the tail is intrinsic, NOT wind-driven**
(and wind is near-inert in the terminal regime).

## Failure classes

### F1 — TERMINAL HARD tail (10.2%): lateral limit-cycle opened mid-swing
- Bucketed 104 bad: **LATERAL-MISS 75%** (td_lat>10 m), **HOT-RESIDUAL-upright 24%**
  (≤10 m, tilt<3°, but v>4 = caught mid-oscillation carrying horizontal velocity),
  TILT-AT-CONTACT **0%**, FUEL **0%**.
- Mechanism (run 729 trace, lat(t) at h<40 m): lateral position oscillates ~3.5 s period,
  amplitude still 10–15 m at the 40 m `lat_scale` fade line. Fade **opens the lateral loop**
  at t=18.5 s exactly when the vehicle crosses lat=0.2 m at MAX inward velocity → residual
  v_xy flings it back out to 17.1 m by touchdown, uncontrolled. `td_v` = mostly horizontal.
- Root: `Kpos=0.12` position-P law + attitude loop `ω_n=1.5` = an underdamped outer loop
  that never settles in the ~20 s descent; the fade-to-blind (guidance_hoverslam.c L84-87)
  then samples it at a random, often worst-case, phase.

### F2 — AERO_OFFSET / CHAOS collapse (98% off-pad): no divert authority
- 100% of failures are OFF-PAD (600–1690 m out at ~12 m/s). Zero fuel/struct/thermal/LOC.
- Cause A: **fins are stubbed** — `control.c:89` sets `act->fins[]=0` (zero force). The
  AERO_DESCENT phase (canon §9.1: fin-α steers lift toward pad) diverts only ~216 m of the
  required 800+ over 20 s of coast (trace: 1373→1157 m from 12→5.5 km — pure ballistic drift).
- Cause B: hoverslam commands **no lateral before ignition** (`a_lat=0` in coast). All divert
  is dumped on landing-burn thrust-vector from ~5 km. Empirical ceiling from the regression
  set: AERO landed from lat0=415 m & 570 m; off-pad crashes start at 1294 m+. → **ceiling
  ~550-600 m** vs 800 m mean demand (1479 m at 3σ).

### F3 — Contact model unexercised (coverage gap)
- ~2000 runs × 3 scenarios: `max_crush=0.0000` everywhere (cap 0.40), `TIPPED=0`,
  settled_tilt ≤0.04°. The crush core, skid, rock, and **tipover** dynamics (canon §7.2, a
  headline feature) are implemented but **never activated**; the HARD-by-crush≥80% verdict
  path is dead. Guidance always lands slow+upright, so contact stays in the benign
  spring-damper regime. Tipover being unreachable ⇒ also **untested** (latent-bug risk).

### F4 — Disturbance robustness is UNTESTABLE
- `INJECT_DISTURBANCE` (canon §10.6) is **entirely unimplemented** in `core/` (0 grep hits).
  Gust/thrust-deficit/Isp/CoM/fin-stuck/RCS-out/sensor-bias do not exist. Robustness to any
  of them is currently unmeasured because there is no mechanism.

## Anti-cheat review — PASS (no §0/§17 violations)
- `dynamics.c`: gyroscopic `ω×(Iω)` ✓, `İω` ✓, mass props recomputed every eval (not frozen)
  ✓, SRP shield blends by C_T ✓, thrust falls with p_amb ✓, single gimbal ⇒ zero roll torque
  by construction ✓ (trap 6.9.1).
- `guidance_hoverslam.c`: all output flows through the integrator as cmds (directive 1) ✓;
  terminal logic commands MIN throttle, not a velocity clamp (directive 6) ✓; no pad-seeking
  state write, no assist term ✓.
- Two **fragilities** (not cheats): guidance drag model is vertical-only + ignores SRP shield
  (throttle bias when C_T high); tier-0 is a hand-tuned single-point design that cliffs
  off-nominal (see F2). The **plant is trustworthy** — MPPI can build on `dynamics_deriv`.

## Fix priorities (to hit M2: ≥98% GOOD+, p95 ≤3)
1. **[P0, closes most of F1] Do not fade the lateral VELOCITY-null term.** Keep
   `Kvel*(vdes - v_xy)` active all the way to the ground so residual v_xy is always damped;
   below 40 m set `vdes=0` (stop seeking new inward velocity) but keep damping v_xy→0. The
   current fade zeroes the *whole* lateral command, opening the loop. This is the single
   highest-value change.
2. **[P0, F2] Implement grid-fin aero** (Agent A's model, mutually-orthogonal pitch/yaw/roll
   allocation, transonic CNα dip, `FIN_CT_DELTA_FRAC=0.35`). Nothing past TERMINAL works
   without it. Then have hoverslam command fin-α steering during AERO_DESCENT (canon §9.1
   lateral channel — currently missing).
3. **[P1, F1] Damp the outer loop.** `Kpos=0.12` + attitude `ω_n=1.5` is underdamped (~3.5 s
   period). Add lateral-velocity feedback (a real Kd on `r_xy`, not just P-via-vdes) and/or
   schedule higher attitude bandwidth near the ground. Begin the lateral null earlier (h>140 m)
   where authority exists, so the vehicle is centered+slow before the terminal phase.
4. **[P1, F4] Build the 7 injectors** (6 on truth, sensor-bias in `nav.c`), routed through
   the command journal so disturbed runs replay bit-exact. Then run the Tier-A/B/C matrix.
5. **[P2, F3] Add an adversarial contact-validation scenario** (hot + canted + lateral-vel at
   touchdown, on-pad) so tipover and crush-core are actually tested before M6.

## What "ROBUST" should mean (quantitative)
Under Tier-B combined disturbance (`NAV_NOISY + 12 m/s gust + −8% thrust + 2 cm CoM + −1% Isp`)
× 2000 seeds: **LANDED ≥95%** (Wilson-95 LB ≥93%), **GOOD+ ≥85%**, **td_v p95 ≤4 / p99 ≤6**,
**zero controller-LOC failures**, **monotone degradation** in Tier-A single sweeps (a cliff =
brittle even if nominal is high — this is the adversarial bar hoverslam fails on AERO), and
**determinism preserved** (injection in the journal → bit-identical replay).
