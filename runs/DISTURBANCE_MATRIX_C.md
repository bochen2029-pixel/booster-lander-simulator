# Disturbance-Injection Test Matrix & Robustness Definition (Agent C)

Status of the mechanism: **`INJECT_DISTURBANCE` is entirely UNIMPLEMENTED in `core/`.**
Grep of `core/` for INJECT/DISTURB/gust/thrust_deficit/com_offset/sensor_bias => 0 hits.
Canon §10.6 lists it as a closed upstream command "one keystroke away"; it does not exist.
The only stochastic forcing today is (a) seeded IC dispersions (`scenario.c`) and (b) the
mean-wind profile + a first-order horizontal Dryden approx (`sim.c wind_sample`). Measured:
`--no-turb` changes the TERMINAL HARD count 102->105 (noise) => **wind is currently near-inert
in the terminal regime and disturbance robustness is UNTESTED because there is nothing to test.**

Everything below is what to build (coordinator implements the injectors on the plant/nav side,
per canon §10.6 / §8.1 — disturbances act on TRUTH except sensor_bias which acts in `nav.c`)
and how to score it.

## 1. The 7 injectors (canon §10.6 enumeration) — where each acts

| # | Disturbance | Acts on | Plant hook | Notes |
|---|---|---|---|---|
| D1 | **Gust** (1-cosine) | truth aero | `v_wind` add in `wind_sample`; `Vm`, `dm` penetration | canon §4.3.3 already specced, not wired |
| D2 | **Thrust deficit** ±% | truth thrust | scale `engine_thrust` return by `(1+δ)` | steady bias, set at RESET or mid-flight |
| D3 | **Isp error** ±% | truth mass flow | scale `Isp` in `dynamics_deriv` | couples fuel margin |
| D4 | **CoM lateral offset** | truth mass props | add `(δx,δy)` to `mp.com` lateral => thrust makes a torque | the nasty one: constant disturbing torque |
| D5 | **Fin stuck** | truth actuator | freeze one `fins_act[i]` at a value | needs fins implemented first |
| D6 | **RCS pod out** | truth actuator | zero one pod's contribution | halves roll/att authority in vacuum |
| D7 | **Sensor bias** | **nav.c (measurement)** | bias `NavState` pos/vel/att, NOT truth | canon §8.1 — the ONLY one on measurement |

Wire each as `INJECT_DISTURBANCE{type,magnitude,dir}` + a headless flag
(`--inject D2:-0.15` etc.) so the MC harness can sweep them. Emit a `FAULT` EVT so the
renderer shows the re-plan (canon requirement: "a real controller visibly re-plans").

## 2. Test matrix (per scenario, 1000 seeds unless noted)

### Tier A — single-disturbance sweeps (find the cliff for each)
Sweep magnitude to locate the failure knee. Run each level x1000 seeds.

| Disturbance | Levels to sweep | Pass bar (TERMINAL) | Pass bar (AERO_OFFSET) |
|---|---|---|---|
| D1 gust | Vm ∈ {6,12,20,30} m/s, dm ∈ {30,100} m, timed at {landing-burn start, h=200m} | GOOD+ ≥ 95% up to 12 m/s; LAND ≥ 98% to 20 | GOOD+ ≥ 90% to 12 m/s |
| D2 thrust | δ ∈ {−15,−10,−5,+5,+10}% | LAND ≥ 98% for −10..+10; graceful (no LOC) at −15 | ≥ 95% |
| D3 Isp | δ ∈ {−3,−1,+1,+3}% | LAND ≥ 99% (fuel-margin driven) | ≥ 97% |
| D4 CoM | δr ∈ {1,2,3,5} cm, random azimuth | GOOD+ ≥ 95% to 2 cm; upright to 5 cm | GOOD+ ≥ 90% to 2 cm |
| D5 fin-stuck | 1 fin at {0°, +10°, +20°} | (AERO/ENTRY only) LAND ≥ 85% | — |
| D6 RCS-out | 1 pod | LAND ≥ 95% (att still gimbal-controlled in burn) | ≥ 90% |
| D7 sensor bias | pos {0.5,1,2} m, vel {0.1,0.3} m/s, att {0.1,0.3}° | GOOD+ ≥ 95% at nominal σ (canon §8.1) | ≥ 90% |

### Tier B — combined "bad day" (the adversarial corner)
The realistic worst case is simultaneous. One combined config per scenario, x2000 seeds:
`NAV_NOISY + D1(12 m/s gust at h=200) + D2(−8% thrust) + D4(2 cm CoM) + D3(−1% Isp)`.
This is the M6 disturbance-robustness gate. **Robust = LAND ≥ 95%, GOOD+ ≥ 85% under Tier B.**

### Tier C — mid-flight step (does it RE-PLAN, not just tolerate a bias)
Inject at a fixed step mid-descent (not at RESET) and confirm recovery, per canon intent:
- 12 m/s gust at h=200 m during landing burn.
- −15% thrust step at h=1000 m (the "engine underperforms" case).
- CoM offset appears at ignition.
Score: does verdict stay LANDED and does `solver_flags`/plan visibly change? (For MPPI:
cloud scatters then re-converges — this is the demo in canon §1.)

## 3. What "ROBUST" means quantitatively (proposed gate language)

A guidance tier is **ROBUST** for a scenario iff, over the Tier-B combined disturbance x2000
seeds with `NAV_NOISY` on:
1. **LANDED ≥ 95%** (Wilson-95 lower bound ≥ 93%).
2. **GOOD+ ≥ 85%**, and touchdown **|v| p95 ≤ 4 m/s**, **p99 ≤ 6**.
3. **Zero un-graceful failures**: any non-landing must be a *physically forced* outcome
   (fuel-depletion ballistic, struct/thermal from an out-of-envelope IC) — **never LOC from
   controller oscillation** and never an off-pad from a divert the tier structurally cannot do.
4. **Monotone degradation**: success rate must fall smoothly with disturbance magnitude in
   Tier A — a cliff (e.g. 99%→10% between two adjacent levels) signals a brittle single-point
   design, which is itself a fail even if the nominal number is high.
5. **Determinism preserved**: same seed+scenario+injection journal => bit-identical (the
   injection must go through the command journal, canon §10.8, so every disturbed run replays).

Criterion 4 is the specifically **adversarial** bar: the current hoverslam would pass a naive
"99.8% nominal" check but its AERO cliff (98%→2% when lateral exceeds ~600 m) is exactly the
brittleness this criterion catches. A robust controller has no such cliff inside the scenario's
specced dispersion box.

## 4. Priority order for building this
1. **Fins first** (blocks D5 and all AERO/CHAOS robustness). Without fin aero, AERO/CHAOS
   robustness numbers are meaningless (they fail off-pad before any disturbance matters).
2. D2/D3/D4 (thrust/Isp/CoM) — cheap scalar injectors, immediately testable on TERMINAL,
   directly exercise the vertical solver's margins.
3. D1 gust (reuse the specced 1-cosine).
4. D7 sensor bias (nav layer) + NAV_NOISY end-to-end.
5. D5/D6 actuator faults.
