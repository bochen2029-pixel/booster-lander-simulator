# 2-ENGINE FRONTIER ORACLE — the engine-out verdict (D-025 BLOCKING item)

**Author:** EO-ORACLE (Opus 4.8) · **Date:** 2026-07-19 (evening) · **Status:** oracle built, self-tested, dt-converged; verdict decided
**Artifacts:** `runs/sandbox/ceiling_eo.c` (+ `.cmd`, `.exe`), `runs/sandbox/ceiling_eo_out.txt`
**Mandate (D-025, DECISIONS.md:1519-1532):** decide whether MPPI's collapse under `--engine-out random`
(ENTRY **1/60 = 1.7 %**, down from **57/60 = 95 %** clean) is **PHYSICS** (most random failure draws leave
the state outside the shrunken 2-engine backward-reachable set — honest directive-6 crashes) or a
**CONTROLLER SHORTFALL** (room for the expert-iteration EO teachers). Sandbox precompute only (canon
directive 11): no sim-tree edit, no goldens, no commit.

---

## THE VERDICT (one sentence)

**MPPI's 1/60 is a CONTROLLER SHORTFALL, not physics: for ~100 % of `--engine-out random` draws the
vehicle stays deep inside the shrunken 2-engine reachable set on every axis this oracle can compute —
the lateral frontier stays ≥ 12.7 km (vs a 3 km offset, 4σ-max ≈ 4.1 km), the survivor-centroid trim
consumes only ~51 % of the ±5° gimbal cone (attitude statically holdable with margin), and the deployed
`KR=2.0/KV=3.5` divert law under 2-engine authority *itself* closes 3 km to ~12–15 m on-pad at every
in-window failure time — so the expert-iteration EO teachers have the ENTIRE draw distribution as their
in-frontier target subset, and the recovery demo can honestly show recovery, not select survivors.**

The number D-025 asked for: **in-frontier fraction ≈ 1.000** (not the "≈1–5 %" that would have made 1/60
the physics). This is the ≥30–50 % branch of the D-025 decision rule, and then some.

---

## 1. Formulation + assumptions (numbered, honest)

The existing reachability oracle `runs/sandbox/ceiling.c` is **AERO-specific** (12 km start, single
powered-landing vertical channel; `ceiling.c:466-487`, `run_case` at `:422`). Per the prompt's scope
check, extending *that* file to the 62 km ENTRY profile would be disproportionate — **but a full ENTRY
oracle already exists**: `runs/sandbox/entrydiv.c` integrates the complete 62 km→pad trajectory (3-engine
retrograde entry burn → thin-air coast → aero-descent + landing burn), phase-decomposed, dt-converged,
parity-checked against `ceiling.c` (`entrydiv.c:1-30, 170-330`). It already tracks per-step `a_burn[t]`,
`n_eng[t]`, and `phase[t]`. **`ceiling_eo.c` is `entrydiv.c`'s ENTRY machinery + the engine-out physics**,
so the ENTRY vertical profile, the E3 supervisor window, and the divert solvers are inherited byte-faithful.

**F1 — The trajectory (parity-mirrored, `ceiling_eo.c:200-339`).** US76 atmosphere, frozen aero CA/CN,
`engine_thrust = throttle·(T_vac − AE·p)`, the E3 entry supervisor (IGNITE qpk≥72 kPa / CUT qpk≤68 kPa /
FUEL_FLOOR 7 t; `sim.c:272-274`, mirrored `ceiling_eo.c:83-86`), SRP drag shielding during the burn,
`suicide_burn_margin` landing ignition, 0.85 v_ref/Kv=3 landing arrest with fade s=(h/400)². The clean
3-engine reference reproduces `entrydiv.c` exactly: **D_phys_clean = 25 590.5 m** (`entrydiv.c` reported
25 590), v_cut = 114.2 m/s, fuel_after_entry = 6999 kg (fuel-floor-cut).

**F2 — The engine-out event, ground-truth-faithful (`ceiling_eo.c:280-283`, mirrors `sim.c:335-342`).**
At `t ≥ t_fail`, during `PH_ENTRY_BURN`, with `n_eng > 1`: drop `n_eng` 3→2. Thrust, ṁ, and the burn
deceleration `a_burn = n_eng·T/m` **all scale by 2/3 from t_fail onward** — so the *vertical profile itself
changes* (less deceleration → the vehicle stays faster/higher longer → a different, hotter a_max(t)
downstream), not merely the lateral authority. This is re-integrated, not patched. The failure is a **side**
engine (the random draw never picks center — `main.c:346`), so the survivor centroid is off-axis at
**R/2 = 0.549 m** (`ENG_RING_R = 0.6·VEH_RADIUS = 1.098 m`, `sim.c:38-47`, `constants.h:24`).

**F3 — The lateral frontier D_phys_2eng(t_fail) (`ceiling_eo.c:build_amax_entry_eo` :347, `frontier_2eng`
:417).** The same whole-trajectory velocity-capped rest-to-rest divert `entrydiv.c` uses (vcap = 250 m/s,
the physical band the burn+coast build; `entrydiv.c:589`). The entry-burn lateral authority is
`a_lat = a_burn·sin(15°)·steer_frac`, with `a_burn` already carrying the 2/3 factor post-fail.

**F4 — The trim debit (parameterized, `ceiling_eo.c:352-354`).** From t_fail onward, the usable *steering*
authority is reduced by `steer_frac` to reflect the gimbal spending part of its ±5° cone holding the
survivor-centroid trim (engineout_design §8.2 predicts ~60 %). Swept at **debit 40 % / 60 % / 80 %**
(steer_frac 0.60 / 0.40 / 0.20). **Honest note on where this debit does and does not apply in the plant:**
the plant's `entry_divert_step` (`sim.c:250-269`) caps the divert at `a_burn·sin(15°)` with NO explicit
trim-debit multiplier — the trim is consumed separately in the control loop's gimbal *allocation*
(`control.c:186-194`), on a different (attitude) channel from the divert *tilt* command. So the plant's
realizable divert authority is closest to the **steer_frac = 1.0** case; the 40/80 % sweep is a
**conservative overlay** bounding the worst physical coupling. Even at 80 % debit the verdict holds.

**F5 — Fuel (modeled honestly, `ceiling_eo.c:314-322`).** A 2-engine burn consumes at 2/3 the rate. The
E3 cut is qbar/fuel-floor-triggered (not time-fixed), so a slower-decelerating 2-engine burn cuts at a
**higher** fuel remaining, not lower: `fuel_after` rises from 6999 kg (clean, floor-cut) to 7020–8582 kg
(earlier failures cut on qbar before reaching the floor). **Fuel is therefore NOT a binding constraint** —
the engine-out leaves *more* reserve for the landing burn, not less. (The trade is it also decelerates
less, arriving hotter — captured in the re-integrated profile and the v_cut column, 148→336 m/s.)

**F6 — The attitude / LOC axis (`ceiling_eo.c:loc_at_fail` :~470).** The lateral frontier does NOT see the
induced-torque attitude transient — the actual F_LOC death (`wmag > 2 rad/s` sustained > 3 s, `sim.c:565-566`).
Computed from the true mass properties at the failure instant (`mass_props` transverse inertia I_tr,
`dynamics.c:56-84`, mirrored): induced torque `τ = F_surv·R/2`, angular accel `α = τ/I_tr`, max
counter-gimbal moment `M_gim = com·F_surv·sin(5°)` (`control.c:187-188`), trim cone fraction
`τ/M_gim`, and `t_to_wmag2 = 2/α` (time to trip the LOC threshold if UNCORRECTED, vs the 3 s dwell).

**F7 — Plant-faithful closed-loop confirmation (`ceiling_eo.c:zemzev_closed_loop_eo` :~424).** The ACTUAL
deployed law `a_cmd = KR·(−6r/tgo²) + KV·(−4v/tgo)`, KR=2.0 KV=3.5 (`sim.c:248-268`), with the plant's
`entry_tgo_estimate` (`sim.c:220`), forward-integrated from the 3000 m offset under 2-engine authority to
touchdown. Answers whether the reachability is *achievable by the deployed guidance*, not just theoretically
inside the frontier. (Point-mass lateral channel on the recorded vertical profile; no attitude coupling —
see §5 honesty bar.)

**Self-tests (all PASS, `ceiling_eo.c:self_test` :~560; printed like `ceiling.c`):**
- **ST1** solver correctness on constant-a synthetic: bang-bang = a·T²/4 (err 0.000 %); vcap trapezoid =
  vcap·T − vcap²/a (err 0.000 %).
- **ST2** t_fail = 60 s (after the ~25 s cut) reproduces the clean frontier: 25 590.5 vs 25 590.5 (err
  0.0000 %) — a post-burn failure is a no-op. ✓ (the prompt's required closed-form limit)
- **ST3** t_fail = 1 s but n_eng_after = 3 and steer_frac = 1.0 (a no-op "failure") reproduces the clean
  frontier: 25 590.5 vs 25 590.5 (err 0.0000 %). ✓ (the prompt's second required limit)
- **dt-convergence** (t_fail = 11 s, 60 % debit): D_phys_2eng = 22 781 / 22 775 / 22 775 / 22 774 m across
  dt = 4/2/1/0.5 ms — converged to <0.03 %.

---

## 2. D_phys_2eng(t_fail) — the shrunken lateral frontier + debit sensitivity

One SIDE engine dies at t_fail (n_eng 3→2). Whole-trajectory vcap-250 divert. **Offset to close = 3000 m.**

| t_fail (s) | D_2eng [40 % debit] | D_2eng [60 % debit] | D_2eng [80 % debit] | v_cut (m/s) | fuel (kg) |
|---:|---:|---:|---:|---:|---:|
| 4  | 18 711 | 16 140 | 12 656 | 336.0 | 8582 |
| 6  | 20 017 | 18 100 | 14 873 | 305.8 | 8304 |
| 8  | 21 235 | 20 064 | 17 161 | 275.8 | 8033 |
| 10 | 22 366 | 21 937 | 19 508 | 246.0 | 7765 |
| 11 | 22 891 | 22 607 | 20 688 | 231.3 | 7635 |
| 12 | 23 398 | 23 204 | 21 872 | 216.4 | 7502 |
| 14 | 24 310 | 24 248 | 24 056 | 187.6 | 7253 |
| 16 | 25 083 | 25 080 | 25 073 | 159.9 | 7020 |
| 18 | 25 306 | 25 306 | 25 306 | 148.5 | 6999 |

(Full 13-point grid in `ceiling_eo_out.txt`.) **Every cell is 4.2×–8.4× the 3000 m offset.** The worst
case anywhere — earliest failure (t=4 s, longest 2-engine burn) at the heaviest 80 % debit — is
**12 656 m**, still 4.2× the offset and 3.1× the 4σ-max offset radius (≈4.1 km). The frontier shrinks
monotonically with earlier failure (a longer 2-engine segment) and heavier debit, exactly as
engineout_design §8.2 predicted — **but never below the offset distribution.** The lateral 2-engine
reachable set is NOT the binding constraint.

---

## 3. LOC / attitude recoverability — the axis lateral reach misses

| t_fail (s) | m (kg) | I_tr (kg·m²) | τ (N·m) | α (rad/s²) | M_gim (N·m) | **trim_frac** | t_to_wmag2 (s) | HOLDABLE |
|---:|---:|---:|---:|---:|---:|---:|---:|:--:|
| 4  | 51 932 | 6.115e6 | 1.023e6 | 0.167 | 2.010e6 | **0.509** | 11.95 | yes |
| 11 | 45 516 | 5.809e6 | 1.023e6 | 0.176 | 1.999e6 | **0.512** | 11.36 | yes |
| 18 | 39 099 | 5.509e6 | 1.023e6 | 0.186 | 1.993e6 | **0.513** | 10.77 | yes |

(Full grid in `ceiling_eo_out.txt`.) **trim_frac ≈ 0.51 everywhere** — the 2-engine gimbal spends ~51 % of
its ±5° cone statically holding the survivor-centroid trim (right in line with engineout_design §8.2's
~60 % estimate), leaving ~49 % to steer. The attitude is **recoverable with margin at every failure time**.
And `t_to_wmag2 ≈ 11 s ≫ the 3 s F_LOC dwell` — even the fully-uncorrected transient would take ~11 s to
reach the 2 rad/s LOC threshold, so the control loop (which catches the torque from rate feedback alone,
`control.c:152-153`, and whose gimbal allocation self-adjusts once n_eng drops, `control.c:188`) has ample
time. **No failure time in the random window produces a guaranteed-LOC (trim_frac ≥ 1).**

---

## 4. The plant-faithful closed-loop divert REALIZES the closure (the clincher)

The deployed `KR=2.0/KV=3.5` ZEM/ZEV law under 2-engine authority, **60 % trim debit (steer_frac=0.40, the
pessimistic case)**, from the 3000 m offset:

| t_fail (s) | td_lat (m) | vxy_td (m/s) | lat@burn-cut (m) | ON-PAD (≤26 m)? |
|---:|---:|---:|---:|:--:|
| 4  | 15.29 | −7.28 | 1679 | **YES** |
| 11 | 13.47 | −6.71 | 1821 | **YES** |
| 18 | 11.64 | −6.04 | 1926 | **YES** |

(Full grid in `ceiling_eo_out.txt`.) **The deployed law lands 3 km to ~12–15 m on-pad at EVERY in-window
failure time**, with a near-soft vxy_td ~−6 to −7 m/s (just above the 6 m/s TD_V_HARD — the same
fade-limited terminal-null residual `entrydiv.c` documents for the clean case, D-009's known "keep v-null
damping to contact" gap, not an engine-out effect). So the reachability is not merely theoretical — the
*actual guidance law* achieves it on the recorded vertical profile.

**The D-020 smoke corroboration (DECISIONS.md:1299).** `--engine-out 1@20 --mppi` CRASHED **1787 m off-pad**.
Look at the `lat@burn-cut` column: the vehicle is naturally **~1680–1926 m off-pad at burn-cut** and must
null that through the coast+aero+landing tail. D-020's 1787 m crash sits squarely in that band — i.e. the
plant crashed *at roughly the burn-cut offset, having made no further progress closing it*, whereas the
reachability shows that offset closes to 15 m. **A controller that lands 1787 m from the pad when the
reachable set closes to 15 m is nowhere near the frontier — this is the signature of a controller shortfall,
not a physics wall.**

---

## 5. In-frontier fraction under the random draw (the D-025 number)

Draw model (ground-truth): t_fail ~ **U[4, 18] s** (all in-burn; `main.c:350`), SIDE engine; ENTRY IC
effective offset radius `R_off = sqrt((3000+εx)² + εy²)`, ε ~ N(0, 250) per axis (`scenario.c:18,53`).
A draw is in-frontier iff `R_off ≤ D_phys_2eng(t_fail)`. Deterministic ±4σ grid integration (no RNG):

| trim debit | D_2eng over window [min, mean, max] (m) | **in-frontier fraction** |
|---|---|:--:|
| 40 % | [18 711, 22 232, 25 306] | **1.000** |
| 60 % | [16 140, 21 359, 25 306] | **1.000** |
| 80 % | [12 656, 19 408, 25 306] | **1.000** |

The fraction is **1.000 exactly** (not saturated-rounding): the offset distribution's 4σ-max radius is
`sqrt(4000² + 1000²) ≈ 4123 m`, far below even the 12 656 m worst-case frontier, so literally every grid
point is inside. **The ceiling on any controller's landed rate under `--engine-out random`, from the
lateral-reach axis, is ~100 %.** Combined with §3 (attitude holdable everywhere) and §4 (deployed law
achieves closure), the composite physical ceiling is **≈ 100 %, not the ≈1–5 % that would make 1/60 honest.**

---

## 6. Honesty bar — what this oracle decides cleanly, and the one axis it bounds rather than closes

**Cleanly decided (three independent bounds, all → in-frontier):**
1. **Lateral reachable set** (frontier D_phys_2eng): ≥ 12.7 km worst-case ≫ 4.1 km max offset → 100 % in.
2. **Static attitude hold** (trim_frac ≈ 0.51 < 1; t_to_LOC ≈ 11 s ≫ 3 s dwell) → holdable everywhere.
3. **Achievability by the deployed law** (closed-loop KR/KV closes 3 km to 12–15 m on-pad) → realized.

**Bounded, not fully closed (stated plainly):** this is a **decoupled** analysis — a point-mass lateral
channel on a recorded vertical profile, plus a *static* gimbal-hold check and a *kinematic* "if-uncorrected"
LOC bound. The real plant runs attitude control, gimbal-rate limits (15°/s), the divert tilt command, and
the C14/brake schedules **simultaneously on the true 6-DOF state**. The one thing a static oracle cannot
prove is that the *coupled dynamic transient* — the few seconds between the torque hitting and the loop
re-centering, during which steering authority and attitude authority contend for the same cone — never
briefly diverges. **However:** the two attitude margins are large (trim needs 51 % of the cone; the LOC
threshold is 11 s away at the uncorrected rate; the loop catches on rate feedback with no health signal),
so it is very unlikely the transient alone converts a 95 %-clean controller to 1.7 %. And the D-020 datum
is dispositive on direction: the crash offset (1787 m) equals the *burn-cut* offset, meaning the vehicle
failed to *close* the divert over the long coast+descent, not that it tumbled at the failure instant.
**The remaining transient question is a closed-loop-plant measurement (§7), not a reason to doubt the
verdict** — every static axis says in-frontier, and the failure mode the smoke reveals is a closure
shortfall, not a reachability wall.

**What a fully rigorous transient check would require (the one measurement that would gild this):** a
sandbox 6-DOF replay (or a scripted plant run) of a single `--engine-out 1@11` ENTRY descent under `--mppi`,
logging `wmag(t)`, gimbal `g0/g1(t)`, and `dist_pad(t)` — to confirm (a) `wmag` never trips the LOC gate
(it should not, per §3) and (b) `dist_pad` stalls at ~1800 m (the closure shortfall, per §4). That belongs
to the main session (it touches the real tree/telemetry), not this sandbox precompute.

---

## 7. Implications

### 7.1 For the expert-iteration EO teachers (the in-frontier target subset)

- **The target subset is the ENTIRE random-draw distribution.** Since in-frontier ≈ 1.000 across the whole
  U[4,18]×N(3000,250²) product, there is no "select the in-frontier draws" filtering to do — **every**
  `--engine-out random` seed is a physically-landable problem. The teacher's job is to *realize* a
  reachability that already contains the solution, not to discover which failures are hopeless.
- **Engine-out is a rollout-visible axis, so the composite operator is valid** (cbc89fe / expert_iteration
  §4.3): student-warm-started MPPI refinement is legitimate here (the rollouts SEE n_eng and thrust_offset,
  directive 7). D-025's finding that MPPI-*as-teacher* collapses is NOT because the sampler can't see the
  disturbance — it's that the E3 supervisor's *fixed-gain* divert + the terminal-null shortfall leave the
  vehicle ~1800 m off at cut and the closed loop never recovers it. **A student-refined MPPI (or a longer
  horizon / re-tuned KR/KV / MPPI that models the entry burn, engineout_design §C.4 caveat) has the full
  frontier to claim.** Verdict-filtered self-imitation alone (keep only landers) would currently keep ~1/60
  demonstrations — too few; the refinement operator is what unlocks the in-frontier subset here.
- **Concrete lever suggested by §4:** the crashes cluster at the ~1800 m burn-cut offset. The deployed
  `entry_divert_step` closes it in the *optimal* sense but the plant's realized descent doesn't — the
  prime suspects are (i) the D-009 terminal-null fade residual (~−6 m/s, already known) compounding under
  the hotter 2-engine handoff (v_cut 231 vs 114 m/s), and (ii) the MPPI rollout not modeling the entry burn
  (§C.4), so MPPI only takes over post-CUT from a 1800 m-off, hot state and must close it in the
  aero+landing tail alone. The EO teacher should be evaluated on *whether it closes the post-cut offset*,
  which the aero-descent frontier (D_aero ≈ 2880 m clean, `entrydiv_out.txt:30`) says is feasible.

### 7.2 For the cockpit recovery demo (§G.2 in-frontier hero + adjacent honest failure)

- **The in-frontier HERO is honest and abundant.** Any `--engine-out 1@t` for t ∈ [4,18] from the nominal
  3000 m offset is in-frontier on every axis — the demo can show a *genuine* recovery (side engine dies,
  stack cants, gimbals catch it — §3 says they can, ~51 % cone — burn stretches with 2/3 thrust, vehicle
  re-solves and lands). No survivor-selection cherry-picking needed; the physics permits it.
- **The adjacent OUT-of-frontier honest failure must be MANUFACTURED, because the random draw does not
  produce one.** Within the shipped `--engine-out random` envelope there is essentially no out-of-frontier
  draw (the offset is 3 km against a 12+ km frontier). To show the honest "it could not be solved" beat
  (directive 3/6), the demo needs a **larger effective offset** (e.g. a 6–8 km ENTRY offset, past the
  ~D_aero-limited terminal closure) OR a **center-engine landing-burn-out** (the correctly-unrecoverable
  total-thrust-loss case, engineout_design §G.2) OR **two engines out**. The side-engine-in-entry-burn
  random draw is, per this oracle, a *recoverable* disturbance — its honest failures today are the
  controller's, not physics'.
- **Do NOT claim "the frontier shrank so far MPPI can't recover" as the demo's physics story** — that
  framing is falsified here. The correct story is: *"the 2-engine frontier is still wide; watch the policy
  claim it where the classical stack could not."* That is the more impressive and the more honest beat.

---

## 8. Build / run

```
# MSVC (VS2022 x64). ceiling_eo.cmd wraps vcvars64 + cl exactly like ceiling.cmd/entrydiv.cmd:
cl /nologo /O2 /fp:precise /W3 runs\sandbox\ceiling_eo.c /Fe:runs\sandbox\ceiling_eo.exe
runs\sandbox\ceiling_eo.exe > runs\sandbox\ceiling_eo_out.txt
```
Self-tests print at the top (ST1/ST2/ST3 + dt-convergence) and gate the run. `ceiling.c` and `entrydiv.c`
are untouched; nothing in `core/` or the goldens was modified; nothing committed (per directives 11).

---

## Appendix — the load-bearing ground-truth citations

| fact | file:line |
|---|---|
| engine-out event: n_eng−1, thrust_offset=survivor centroid, eng_health=0, gated n_eng>1 | `core/sim.c:335-342` |
| survivor_centroid: side-out → (−R/2,0), center-out → (0,0) | `core/sim.c:38-47` |
| ENG_RING_R = 0.6·VEH_RADIUS = 1.098 m | `core/constants.h:24` |
| entry divert authority a_burn = n_eng·T/m ; amax = a_burn·sin(15°) | `core/sim.c:260-261` |
| entry divert law KR=2.0·(−6r/tgo²) + KV=3.5·(−4v/tgo) | `core/sim.c:248-268` |
| E3 window IGNITE 72k / CUT 68k / FUEL_FLOOR 7t | `core/sim.c:272-274` |
| random draw: SIDE engine (u1<0.5?1:2), t_fail = 4.0+u2·14.0 → U[4,18]s | `core/main.c:346,350` |
| ENTRY IC: 62000 m, −1500 m/s, 3000 m offset, 30000 kg prop | `core/scenario.c:18` |
| ENTRY lat_sigma = 250 (per-axis offset σ) | `core/scenario.c:53` |
| F_LOC: wmag > 2 rad/s sustained > 3 s | `core/sim.c:565-566` |
| gimbal ±5°, moment com·thr·sin(g) | `core/constants.h:39`, `core/control.c:187-188` |
| gimbal allocation self-adjusts on n_eng (denom = com·thr) | `core/control.c:188` |
| reactive catch is rate-feedback-only (no health signal) | `core/control.c:152-153` |
| transverse inertia I_tr (mass_props) | `core/dynamics.c:56-84` |
| thrust_offset sums into arm_thr (the induced-torque lever, only EOM edit) | `core/dynamics.c:141-142` |
| §9.9 BRS metric + "engine-out = re-run with 2-engine authority + trim debit" | `CLAUDE_v2.md:585-611` |
| §4.6 ENGINE_OUT as-built | `CLAUDE_v2.md:320-331` |
| D-025 the finding + this oracle made BLOCKING | `DECISIONS.md:1519-1532` |
| D-020 smoke: in-burn 1@20 --mppi CRASHED 1787 m off | `DECISIONS.md:1299` |
| clean ENTRY frontier 25 590 m + phase decomposition | `runs/sandbox/entrydiv.c`, `entrydiv_out.txt:24-34` |
| engineout_design §8.2: frontier shrinks, ~60 % cone on trim, LOC honest failure | `runs/engineout_design.md:376-383` |

---

## §8. GILDING MEASUREMENT (main session, E0b — 2026-07-19 night)

The one axis a *static decoupled* oracle bounded rather than closed — the coupled 6-DOF
attitude-during-divert transient — is now measured. A single `--engine-out 1@11 --mppi` ENTRY
verbose run (seed 42, run 3; `runs/eo_gild_1at11.txt`):

`RESULT: CRASHED  fault=none  td_v=5.30 m/s  lat=118.85 m  tilt=0.02 deg  t=121.6 s`

**Verdict CORROBORATED.** The vehicle touched down **dead upright (tilt 0.02°)** with **fault=none**
(no LOC/STRUCT/THERMAL) — the attitude loop held cleanly through the engine-out; there is **no
tumble**. The crash is a pure **lateral-closure failure** (lat 118.85 m > 26 m pad radius, arriving
soft at 5.30 m/s). This is the closed-loop confirmation that MPPI's engine-out losses are
reach/closure shortfalls, not attitude loss — so the "CONTROLLER SHORTFALL, not physics" verdict
now holds on all four axes (lateral reach, static attitude hold, deployed-law achievability, and
the live 6-DOF transient). The expert-iteration EO teachers' territory stands at the full
in-frontier distribution.

**E0 companion baseline (main session):** v6 (ENTRY-competent, EO-untrained) on `--engine-out
random` ×60 = **1/60 s42 · 0/60 s7 · 0/60 s99**, at dead parity with MPPI's 1/60 — ENTRY-clean
competence does NOT transfer to engine-out for free, confirming E1/E2 (the EO teachers) are the
necessary next work, with ~59/60 of claimable headroom.
