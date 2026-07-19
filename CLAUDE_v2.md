# BOOSTER LANDER — CLAUDE_v2.md

> **CANON — adopted by ADR D-019 (2026-07-19, operator-signed, all defaults accepted).**
> Supersedes `CLAUDE_v1.md` (kept for history alongside `CLAUDE_v0.md`; edit neither).
> Authoring record: `runs/D019_proposed_canon_v2.md`.
> **Anchor-stability rule:** every v1 section number (§0–§17, App-A–F) keeps its meaning in v2 —
> existing ADRs and design docs that cite "§4.3", "§8.1", "§9.5", "§10.6" remain valid. v2 only
> ADDS sections (§4.5–4.7, §8.4, §9.8–9.9, §10.9, §13.6, §18–§20, App-G) and amends in place with
> tagged `[v2, D-0xx]` notes. Nothing is renumbered.

A 6-DOF propulsive-landing simulator in which the guidance **actually solves the landing
problem in real time**, wrapped in a renderer that aims for **cinematic, physically-grounded
beauty** — and an architecture that makes faking either one structurally impossible.

**v2 adds the third maximum: max-capable guidance.** A learned neural policy — trained offline,
frozen into a bit-deterministic C artifact — that targets the *reachability frontier*: recover
whenever physics permits, through **compounding failures (engine-out × wind shear × a moving
target) at once**, and crash honestly when physics forbids. A perception front-end (beacon now,
VLM later) lets the vehicle *sense* its target instead of being told, and a world-parameterized
plant points the same stack at Mars. The three maxima — **max-true simulation, max-cinematic
presentation, max-capable guidance** — are all the project, and the compound-recovery showcase
(§1 finale, §14 N3) is the single beat where they intersect.

Native C/CUDA core does physics and guidance. Three.js (WebGPU, TSL) in a Tauri v2 shell
draws and *sounds* it. A one-way binary state stream connects them. The core proves itself
headless with Monte-Carlo success rates; the renderer proves itself by reproducing, from
telemetry alone, what real booster landings look and sound like. The learned policy proves
itself the same way everything here proves itself: **numbers, gates, goldens, held-out seeds.**

This document is the canon. It supersedes `CLAUDE_v1.md` (keep v1 for history; do not edit it).
It is written so that a fresh coding session can be pointed here and build the whole thing
without asking questions. Where a number is not public, it is **[chosen]** and pinned anyway.
Provenance tags: **[official] [estimate] [community] [chosen]** as in v1 (sources: App-F).
Implementer-grade depth for the v2 additions lives in four pillar docs this canon binds to:
`runs/perception_to_policy_stack.md` (the map), `runs/neural_policy_design.md` (the anchor),
`runs/perception_design.md`, `runs/interplanetary_integration_design.md`.

---

## 0. Prime directives

These are not preferences. Violating any of them defeats the purpose of the project.

1. **State changes only through the integrator.** The guidance layer's entire output is an
   actuator command vector: throttle, two gimbal angles, four fin deflections, RCS mask.
   No code path anywhere writes position, velocity, attitude, angular velocity, mass, or
   slosh/contact states except `integrate_step()`. *(v2 note: this binds the learned policy
   identically — `GM_NEURAL` emits a `GuidanceCmd` and nothing else, §9.8.)*

2. **The renderer is a pure observer.** The upstream command channel (§10.6) is a closed
   enumeration. Nothing in it can write vehicle state. If the renderer crashes, the
   simulation is unaffected.

3. **Fixed timestep, always.** `dt = 2 ms` is a compile-time constant, never a frame delta,
   never a wall-clock delta. Real time affects pacing only, never integration.

4. **Deterministic, and provably so.** Seeded counter-based RNG (Philox, §9.6), no
   wall-clock reads inside the sim, no uninitialised memory, no unordered floating-point
   reductions, no atomics in reduction paths. Same seed + same scenario + same command
   journal ⇒ bit-identical trajectory. Every run — including interactive ones — emits a
   **command journal** that replays bit-exactly (§10.8). Determinism scope: bit-identical on
   the same binary + same GPU architecture (goldens pinned sm_89); the CPU plant path is
   bit-identical everywhere the same binary runs. *(v2, D-019: derived constants — policy
   weight headers, regenerated aero tables, acquisition traces, World parameter sets — are
   **versioned precompute artifacts**: generated offline where nondeterminism is allowed,
   then frozen, committed like goldens, and consumed bit-exactly. §20 is the registry.)*

5. **Headless must work.** The same core binary, with no socket and no renderer, runs N
   randomized descents and prints a success rate. This artifact is the proof the simulation
   is real. `ui/` and `shell/` deleted must not break `core/`.

6. **If guidance can't solve it, the vehicle crashes.** No assist term, no nudge, no soft
   clamp toward the pad, no recovery mode that bypasses the controller. A crash — and a
   tip-over, and a fuel-depletion ballistic arc — are valid, fully-simulated outcomes.
   *(v2, D-019: this directive now has a measuring instrument — the reachability-frontier
   metric of §9.9. States outside the backward-reachable set MUST crash; a controller that
   "lands" one is investigated as a leak, never celebrated. The metric is a diagnostic
   overlay; it never softens a landed-rate gate.)*

7. **One dynamics source.** Plant, hoverslam prediction, and MPPI rollouts call the same
   `__host__ __device__` equations of motion, including actuator lag models and active
   module set. Rollout-model drift from the plant is a cheat with extra steps. (Deliberate
   model mismatch is a legitimate robustness *experiment*, only after parity is established
   and only behind an explicit flag.) *(v2 notes: a direct learned policy has NO rollout
   model, so it satisfies this directive for free — but its TRAINING environment must be the
   plant itself, bit-for-bit — "the gym is the plant", §19.2. And MPPI's directive-7 rollouts
   are what make it a valid distillation teacher for any single plant disturbance, §19.5.)*

8. **The renderer draws only what it is told.** Every pixel and every sound that represents
   simulation data derives from the telemetry/event stream. Visual-only garnish may use
   frame time and unseeded randomness; anything presented as data — ghost line, tapes,
   plots, booms, the target-estimate marker — may not. No renderer-side smoothing that masks
   real oscillation (a "raw telemetry" toggle must exist and be respected by the HUD).

9. **Honest audio and honest cameras.** Sound propagates at 343 m/s from emission events to
   the active camera (2.92 s/km); booms arrive late; distance strips highs. Cameras never
   influence the sim; camera state never crosses the boundary.

10. **Aesthetics last, physics first — but aesthetics are not optional.** The build order
    (§14) gates renderer work behind headless validation gates. Once unlocked, the visual
    milestones are real milestones with acceptance criteria, not decoration. All three
    maxima are the project.

11. **Precompute in, telemetry out — always.** *(v2, promoting D-011's hard line to a
    directive.)* Nothing nondeterministic ever closes a runtime loop into dynamics: not the
    cinematic renderer, not a CFD solver, not Unreal Engine, not a vision-language model,
    not a training run. Anything nondeterministic runs OFFLINE as precompute, and only its
    **frozen output** — a weights header, an aero table, an acquisition trace, a World
    parameter set — enters the gated loop as versioned data, exactly like a seed or a wind
    trace. This is the single rule that lets a 9-billion-parameter VLM and a learned policy
    coexist with the `memcmp` oracle.

---

## 1. What "done" looks like (the experience contract)

The `ENTRY` scenario, night preset, director camera. Everything below is generated, not
scripted — every visual/audio beat is driven by telemetry and events:

- **T+0, 62 km, Mach 5.6.** Space-black sky, star field, a limb of atmosphere glowing at
  the horizon (Bruneton sky). The booster is a sliver falling engine-first; grid fins are
  deployed. RCS pods spit crisp white puffs holding attitude trim.
- **Entry burn.** Three engines light — green TEA-TEB flash, then a plume that does not
  trail the vehicle but **wraps forward and envelops it** (supersonic retropropulsion), a
  ragged unsteady sheath, drag falls to ~zero while thrust does the work. 4–5 g of
  deceleration on the HUD accelerometer. The stage emerges sootier: leg-shadow stripes.
- **Aero descent.** Burn ends; the plume snaps off; the world begins to blue as the sky
  LUT thickens. Grid fins articulate continuously — the ghost line (MPPI plan, colored by
  planned throttle) writhes ahead of the vehicle toward the pad; a faint cloud of rollout
  endpoints breathes around the pad like a probability field. Inject a 12 m/s gust: the
  cloud scatters, the ghost line snaps to a new solution, the fins bite, it re-converges.
- **Landing burn, 4.6 km, ~310 m/s.** Green flash, single engine. Diamond shock cells at
  sea-level spacing now — bright orange kerolox soot core with a dark gas-generator streak.
  Legs deploy at ~250 m (drag and inertia change mid-burn; guidance shrugs). The pad
  floodlights catch the booster; the **plume lights the pad** and the vehicle's own base.
- **Touchdown.** Dust wall-jet races radially outward in Görtler streaks, lit from within.
  Four leg spring-damper contacts; crush cores stroke; the stage rocks, settles. Verdict:
  `LANDED (GOOD)` — touchdown 1.4 m/s, 2.1 m off center, tilt 0.6°.
- **The sound arrives on camera time.** Pad camera at 2 km: the burn is silent for 5.8 s,
  then rumble + crackle ramps; the **triple sonic boom** cracks past ~15 s before the
  vehicle is down; touchdown crunch arrives 5.8 s after you see it. Long-exposure mode has
  been integrating the whole descent into a single streak photograph; export as PNG.
- **Replay.** The full run is in the renderer's ring buffer: scrub it, re-cut it from the
  tracker telescope, the onboard fisheye, or the droneship deck cam. A mission summary card
  exports the stats.

**The v2 finale — the compound recovery (gate N3, §14; spec §9.8/§19; experiment
`neural_policy_design.md` §G):**

- Same night ENTRY, `--neural`, a held-out seed. Mid-entry-burn a **side engine dies** — the
  surviving thrust centroid jumps, the induced torque cants the stack, the inner loop
  catches it; a breath later a **1-cosine shear** slams the committed divert while the
  **droneship deck wanders** under seeded swell. The predicted-impact marker (protocol
  offset 220) lurches off the deck… then **walks back on** as the policy re-solves at 50 Hz
  — the burn stretched to cover the lost engine, the divert re-aimed at where the deck *is*.
  Touchdown: soft, upright, centered on a target that never held still, through failures no
  one scripted a recovery for.
- **Then the honesty beat, always shown with it:** the replay of the ADJACENT case — the
  same engine-out a few seconds later, or a few hundred meters farther out, *outside* the
  shrunken reachable set — where the vehicle does **not** make it, because physics says it
  can't (directive 6). The summary card prints the §9.9 confusion table for the batch
  behind the demo. A showcase that includes its own honest failure boundary is the
  anti-cheat thesis made visible.
- **"Talk to the rocket" (N4):** the operator types (or says) *"land on the flat spot left
  of the ridge"*; the perception front-end (§8.4) grounds it, the estimate marker appears
  with its uncertainty ellipse, and the same guidance flies to it — seeded, dragged,
  beacon'd, or seen, the vehicle cannot tell the difference and never needs to.
- **Epilogue (N4/worlds):** the same stack, World-swapped: thin CO₂, 0.38 g, a
  vision-picked hazard-free site on Mars. The reason the whole field exists.

If a feature does not serve this contract or the headless proof, cut it.

---

## 2. Session protocol (how to work in this repo)

House convention (matches ORRERY/DAVE/FIGUREHEAD):

- **Read order for a cold session:** this file §0–§2 → `RUN_STATE.md` (current milestone,
  next action) → `DECISIONS.md` tail (last 5 ADRs). For N-track work (§14), additionally:
  `runs/perception_to_policy_stack.md` (the map) → the pillar spec for your lane
  (`neural_policy_design.md` — read §H.0 before building anything — / `perception_design.md`
  / `interplanetary_integration_design.md`). Then work.
- **`RUN_STATE.md`** — the ledger. Update it every session. Write for a stranger.
- **`DECISIONS.md`** — append-only ADRs (`D-001`, `D-002`, …) for any architectural choice
  or deviation from this spec. A deviation without an ADR is a bug.
- **Goldens** (`goldens/`) — frozen reference outputs (§13.5): protocol byte layouts,
  trajectory hashes, MC baseline rates, and *(v2)* policy-weights KATs. Hardware-pinned to
  sm_89. Re-baselining is an operator-signed event, recorded in `DECISIONS.md`.
- **Gates are gates.** Do not start a milestone before the previous one's gate is green
  (§14). Run the test suite before and after every work block. *(v2: every N-track rung
  additionally passes the leak check — new capability present-but-off must reproduce every
  existing golden byte-exactly, §13.6.)*
- **Tooling rule (operator, 2026-07-18; codified here):** project code — the sim, the
  shipped inference, `tools/` — is **C/C++/CUDA only, never Python**. Offline **precompute**
  that produces versioned data artifacts (the policy trainer, CFD bakes, catalog bakers) is
  language-free (PyTorch on RTX/H200 is fine there); its outputs are data, its code never
  ships, never runs in the sim path (§19.1, §20).
- **No new dependencies without an ADR.** Core: zero external deps. UI: `three@0.185.1`,
  `uPlot`, `vite`, `typescript`. Shell: `tauri@2.x` + plugins. Trainer (offline, unshipped):
  its own environment, outside this rule's blast radius but inside the ADR log.
- **Anchor stability:** v2 never renumbers a v1 section. New material gets new numbers.
- **Never violate §0.** If a task seems to require it, stop and write the conflict into
  `RUN_STATE.md` instead.

---

## 3. Repository layout

```
CLAUDE_v2.md          this file (canon; adopted D-019)   CLAUDE_v1.md / v0  history, do not edit
RUN_STATE.md          session ledger      DECISIONS.md   ADR log (D-001 … )
core/                 C11 + optional CUDA. No graphics, no external deps.
  constants.h         every physical constant, one place (§App-A)
  atmosphere.{h,c}    US76 0–86 km (§4.2)         rng.h   Philox4x32-10 (§9.6)
  dynamics.{h,c}      shared EOM (§6; BL_HD host/device)   integrator.{h,c}  RK4 (§6.7)
  contact.{h,c}       legs/friction/tipover (§7)  nav.{h,c}  measurement layer (§8.1) [BUILT]
  control.{h,c}       attitude PD + allocation (§8.3)
  guidance.h          GuidanceCmd + GM_* modes    guidance_hoverslam.{h,c}  tier 0 (§9.1)
  guidance_mppi.{h,c} tier 1 CPU (§9.2) + mppi_cuda.cu (M5, unity-include, optional)
  guidance_neural.{h,c} + neural_policy_weights.h   tier 3 (§9.8) [N1 — planned]
  sim.{h,c}           phase machine, wind_sample (mean+Dryden+gust), disturbances, verdict
  scenario.{h,c}      scenario table + dispersion sampling (§App-D)
  protocol.h          packet layout v3 (static_asserted; §10)      ws.{h,c}  RFC6455 --serve
  main.c              CLI: --selftest | --headless | --run | --serve | --golden
                      flags: --mppi | --mppi-cuda | --neural[N1] | --gust | --inject
                             --nav-noisy | --engine-out[N0] | --target[N0]
  tests/              oracles + parity (§13)
shell/                Tauri v2 (Rust). Spawns core as sidecar, supervises. Nothing else.
ui/                   Three.js r185 WebGPU renderer + WebAudio + HUD. Read-only observer.
tools/                C/C++ analysis tools (mcdiff, tracestat — toolsmith; NO Python, §2)
trainer/              [N1, optional location — may live outside the repo] offline PyTorch
                      distill/RL precompute; gitignored except exported weight headers
data/reference/       public telemetry reconstructions, aero tables
runs/                 designs, reports, MC outputs (gitignored except committed artifacts)
  perception_to_policy_stack.md + neural_policy_design.md + perception_design.md +
  interplanetary_integration_design.md      the four pillar docs this canon binds to
goldens/              frozen hashes, packet bytes, MC baselines, policy KATs (§13.5)
.github/workflows/    CI: build + 10-oracle selftest + MC gate + leak checks on windows-latest
```

Spec-vs-tree honesty: `wind.{h,c}`/`sea.{h,c}`/`journal.{h,c}`/`log.{h,c}` from v1 remain the
spec'd factoring; as-built, wind lives in `sim.c wind_sample()` (D-017) and SEA/journal/log are
not yet built — refactor/build under their original section contracts when their milestones
arrive; no ADR needed to *match* the spec, only to change it.

`core/` must build and run with `ui/` and `shell/` deleted.

---

## 4. The world

### 4.1 Frames, units, conventions

*(unchanged from v1)* Right-handed world frame, **Z up**, origin at the primary pad center,
X east, Y north. SI units, radians internally. Body frame at the base-plane center, +Z along
the vehicle axis; legs at 0/90/180/270°, fins at 45/135/225/315°. Quaternion body→world,
**scalar-last (x,y,z,w)**. Flat ground, non-rotating Earth; neglect list: Coriolis/centrifugal,
Earth curvature, gravity-gradient torque. `g(h) = g0·(R0/(R0+h))²`, `R0 = 6 356 766 m`,
`g0 = 9.80665`. Record every neglect here; silent physics "improvements" are ADR violations.
*(v2: the world-constant surface is being abstracted into `World` — §4.7; Earth is World #0
and this section is its parameter set.)*

### 4.2 Atmosphere — US Standard 1976, not a bare exponential

*(unchanged from v1)* Piecewise-linear temperature layers 0–86 km (App-A.1) → `T(h), p(h),
ρ(h), a(h)`, exposed as one pure function `atmo_eval(h, AtmoOut*)`. The guidance model uses
the same function (directive 7).

### 4.3 Wind & turbulence (deterministic, seeded)

Three layers, all fed from the `wind` RNG stream of the master seed:

1. **Mean profile.** `(U_ref @ 10 m, azimuth, veer)` per scenario; power law
   `U(h) = U_ref·(h/10)^0.14` to 1 km; ×1.0→2.2 jet ramp 1→11 km; decay to ×0.4 by 25 km;
   veer +20° default across 0→11 km. **[chosen]**
2. **Dryden turbulence, MIL-F-8785C** (scales/intensities and discretization: v1 §4.3 +
   App-A.2, unchanged; stepped at 500 Hz from `|v_rel|`).
3. **Discrete gust.** 1-cosine `V = Vm/2·(1−cos(πx/dm))` — **BUILT (D-017):**
   `--gust <peak_mps>@<alt_m>[:<halfwidth_m>]` + `--gust-dir <deg>`, superposed inside
   `wind_sample()`; pure altitude function, no RNG, replayable; absent ⇒ byte-identical.
   Also triggerable by `DISTURB`/scenario script per v1.

Wind enters the plant **only** through `v_rel = v − v_wind(r, t)`. **Guidance never reads
`v_wind` — it feels wind through state.** This is load-bearing for v2: it is why gusts and
dispersions are pure *curriculum knobs* for the learned policy (zero interface work, §19.5),
and why D-014's attitude-only wind estimator was falsifiable at all. (MPPI's planner zeroes
`env.wind_world` in rollouts; the neural policy's observation simply never contains it.)

### 4.4 Sea state — module `SEA` (ASDS scenarios)

*(unchanged from v1)* Deck pose = 48 seeded Pierson-Moskowitz sinusoids, Hs=2.0 m SS4 default
(heave ±1.5 m, pitch/roll ±2.5°, periods 6–10 s **[estimate]**); deck pose is sim state,
streamed in telemetry; contact solves in the deck frame; **guidance receives deck pose in its
nav state** (the real system knows the deck); station-keeping wander ±3 m **[official]**.
*(v2: the SEA deck is the canonical *moving target source* — §4.5 generalizes it.)*

### 4.5 Movable target — module `TARGET` *(v2, D-019; design: `runs/target_sandbox_design.md`)*

The landing target becomes a first-class, time-varying plant object `target_xy(t)` (+
`deck_z(t)` when SEA is active), decoupled from the world origin. Guidance's job everywhere
becomes **null the offset to the target**: the one-line substitution
`r_rel = r_xy − target_xy` at the guidance layer (`guidance_hoverslam.c:84` and the MPPI /
neural equivalents), height above deck `h = r_z − com − deck_z`.

**Target sources (one schema, §8.1/App-G — guidance cannot tell them apart):**

| source | determinism | mechanism |
|---|---|---|
| `FIXED` | deterministic | origin default — byte-compatible with v1 (the N0 gate) |
| `SEEDED` | deterministic (Philox `target` stream) | scripted drift / SEA deck wander — the training source |
| `BEACON` | deterministic (seeded noise on true pose) | the pad broadcasts its own noisy GPS (§8.4 honest baseline) |
| `PERCEIVED` | deterministic **replay** of an async acquisition trace (§8.4) | VLM + rangefinder + Kalman |
| `DRAG` | **FENCED** — determinism deliberately waived, journal-recorded | operator live-drag in `--serve` |

Default `FIXED` at origin ⇒ every existing golden reproduces byte-exactly. CLI `--target …`
arms the seeded modes. `SET_TARGET` (§10.6) remains the legal mid-flight goal change;
`BL_EVT_TARGET_CHANGED` (already reserved, `protocol.h:226`) announces it.

### 4.6 Engine-out — module `ENGINE_OUT` *(v2, D-019; design: `runs/engineout_design.md`)*

A seeded, time-triggered engine failure in multi-engine regimes (the 3-engine entry burn is
the money regime): CLI `--engine-out <k>@<t>` (side or center engine, seeded window).
Plant effects, all through existing machinery: decrement `n_eng` (thrust, ṁ, and the
allocation denominator scale), and the surviving-thrust centroid offset `thrust_offset[2]`
produces the induced torque through the existing `arm_thr × F_thr` lever
(`dynamics.c:137-138`); the entry-burn authority literal `3.0 → st->n_eng` (`sim.c:154`).
Directive 7 is free: MPPI's rollouts copy the same `EnvCtx` and re-solve with the reduced
authority on the next replan. **Engine health is a legal measurement** (§8.1): real engines
telemeter chamber pressure; `eng_health[]`/`n_eng` join NavState. Default OFF ⇒
byte-identical (the N0 leak gate). This module **rescinds a v1 neglect** — see §5.3.

### 4.7 Worlds *(v2 forward-pointer; owner: `runs/interplanetary_integration_design.md`)*

§4.1–4.3's Earth constants (g0, R0, the US76 table, the wind model) are one frozen parameter
set — **World #0**. The `World` abstraction (gravity + radius; atmosphere surface
density/scale-height/composition/speed-of-sound; wind model) replaces hardcoded constants so
the same plant flies Earth → Mars (0.38 g, ~1% density CO₂) → generic. A World is seeded,
frozen, hashed data (directive 11). **Canonized trap (caught by the pillar doc): `G0` wears
two hats** — local surface gravity AND the standard-gravity unit inside Isp→exhaust-velocity.
Only the first varies per world; conflating them silently corrupts fuel accounting on every
non-Earth rollout. Per-world frontier oracles (D_phys is world-dependent) come with N4 (§14).

---

## 5. The vehicle — `KESTREL-9` (Falcon-9-class, single canonical preset)

*(§5.1, 5.2, 5.4, 5.5, 5.6, 5.7 unchanged from v1 — geometry, two-tank model, grid fins,
RCS, legs, structural/thermal budgets, all with their v1 tables and provenance tags. The
vehicle param struct in `vehicle.h`/HELLO and its FNV-1a vehicle hash remain the single
source both physics and visuals build from.)*

### 5.3 Engine (per-engine; burns use n_engines ∈ {1,3}) — v1 table stands, one amendment

All v1 engine parameters stand (845 kN SL / 932 kN vac, Isp 282/311, min throttle 40%,
gimbal ±5° @ 15°/s, throttle lag τ=0.10 s, ignition ramp, relight budget 2, the consistency
rule). **Amended [v2, D-019]:** v1 recorded the neglect *"3-engine burns: symmetric side pair
assumed thrust-aligned through CoM (side-engine offset torque neglected)."* That neglect now
holds **only while all engines are healthy**. With module `ENGINE_OUT` armed (§4.6), the
failed-engine thrust-centroid offset and its induced torque are modeled explicitly, and
`n_eng` becomes dynamic state. (Nominal all-healthy flight is bit-identical to v1 — the
neglect is rescinded conditionally, which is why every golden survives.)

---

## 6. The plant

*(§6.1–6.9 unchanged from v1: state vector; force×lever-arm discipline; Mach-dependent aero
tables with SRP shielding — including the D-009 lesson that the shield applies to fins too;
mass properties recomputed every derivative; `I ẇ = τ − ω×(Iω) − İω`; actuator dynamics
inside the integrator; RK4 dt=2 ms with contact substepping and bisected touchdown; the phase
machine; and all six known traps.)* Two v2 annotations:

- **§6.1 state additions (N0):** `eng_health[3]` / dynamic `n_eng` (§4.6) and the target
  trajectory state (§4.5) join sim state; both default-nominal ⇒ byte-identical.
- **§6.3 aero tables:** remain **[chosen, representative]** and frozen per vehicle hash —
  until the CFD regeneration event of §20 replaces them with FluidX3D-derived tables from
  the actual vehicle mesh (an ADR + vehicle-hash bump + full re-golden, pre-scoped there).

---

## 7. Contact, touchdown, and verdict

*(unchanged from v1: contact set, leg spring-damper-crush model, and the verdict table
PERFECT/GOOD/HARD/TIPPED/CRASHED with its thresholds. Verdicts are the ground truth the §9.9
frontier metric classifies against — `S_land = V_PERFECT ∪ V_GOOD`.)*

---

## 8. Sensing and control architecture

The stack, at fixed rates, all inside `core/`:

```
plant state ──► nav.c (measurement layer) ──► guidance (50 Hz) ──► attitude+allocation (500 Hz) ──► actuator commands ──► plant
                     ▲
   target sources ───┘  (§4.5/§8.4: fixed | seeded | beacon | perceived-trace | drag)
```

### 8.1 Measurement layer (`nav.c`) — THE WIDE SOCKET *(v1 core + v2 widening, D-019)*

**[BUILT — D-010]** `NAV_TRUTH` pass-through (proven bit-transparent) and `NAV_NOISY`
(pos σ [0.5,0.5,0.3] m, vel σ 0.1 m/s, att σ 0.1°, gyro bias random-walk, seeded;
`--nav-noisy`). `INJECT_DISTURBANCE(sensor_bias)` acts here — on measurements, never truth.
Guidance sees only NavState. Deck pose (ASDS) is part of NavState *(v1 already said so —
v2 generalizes it)*.

**v2 widening — add to NavState, at constant nominal values from day one (N0):**

```c
TargetEstimate {                      // ONE schema for every §4.5 source
  double target_xy[2];                // estimated target position (world XY)
  double target_vxy[2];               // estimated target velocity (0 for FIXED)
  double target_cov[3];               // 2x2 covariance packed (xx, yy, xy) — σ_target
  double deck_z;                      // estimated deck height (0 flat pad)
  uint8  target_src;                  // FIXED|SEEDED|BEACON|PERCEIVED|DRAG (provenance tag)
  double target_age;                  // s since last acquisition/update (staleness)
  uint8  target_valid;                // 0 before first acquisition — handle gracefully
}
EngineHealth { uint8 eng_health[3]; uint8 n_eng; uint8 relights_left; }   // §4.6, LEGAL
```

**Why wide-now:** the socket is designed for the *eventual* full stack — including the VLM —
even though every field sits at nominal today. Interface changes mid-training re-architect
the policy net (§19.5, the H.0 doctrine); schema changes re-bump the protocol. Pay the width
once. Nominal defaults (`target = origin, cov = tiny, valid = 1, src = FIXED, all healthy`)
reproduce v1 byte-exactly — that byte-equality IS the N0 gate.

**The provenance rule (binding for every guidance tier and the policy's
`build_observation`):** a quantity is legal iff computable from `nav_measure`'s output +
`atmo_eval` + the §8.1 TargetEstimate + the §8.1 EngineHealth. `wind_world`, `wind_filt`,
truth-target, and any hidden disturbance schedule are ILLEGAL reads — canon §4.3 — and the
ban is flagged loudly here because `wind_filt` sits in the truth pass-through where it is
easy to reach and forbidden to touch.

### 8.2 Timing discipline

*(unchanged from v1: guidance 50 Hz on tick-start state, output applied next tick — 20 ms
transport delay, both tiers and tier 3 alike; inner loop 500 Hz; no same-step sense→act.)*

### 8.3 Inner loop (shared by all guidance tiers)

*(unchanged from v1: quaternion-error PD with feed-forward and I(t)-scheduled gains;
q̄-regime allocation RCS/fins/gimbal; Schmitt+PWM RCS; roll RCS-only in vacuum. v2 note: the
learned policy sits ABOVE this loop exactly as MPPI does — it emits `a_lat`+throttle intents;
the inner loop owns attitude and effector mapping. The policy never emits gimbal/fin/RCS.)*

### 8.4 Perception front-end — the target-acquisition contract *(v2, D-019; implementation: `runs/perception_design.md`)*

**The honesty thesis.** v1 handed guidance the target as the world origin — an *oracle*. A
real vehicle has no voice announcing "the pad moved." It knows one of two honest ways, and
both are **sensors**, not oracles: (a) a **BEACON** — the pad broadcasts its own (noisy) GPS
pose, as the real droneship does; (b) **VISION** — the vehicle sees it (Perseverance's
Lander Vision System lineage), which is also what makes the target *language-addressable*.
Perception therefore **removes** a cheat rather than adding one.

**The contract (canon-level; details in the pillar doc):**
- **The sensor-camera is a plant sensor** — a deterministic, headless raster of scene
  geometry from the vehicle's camera pose, part of the sensor suite. It is NOT the cinematic
  renderer and no observer pixel ever feeds back (directives 2/5/11 all bind).
- **The VLM is an async precompute.** Image + instruction → grounded bearing + confidence
  (Qwen3.5-9B via llama.cpp mmproj on the operator's box — or any successor; the model is
  outside the gate). A simulated **laser rangefinder** supplies range along the bearing;
  localization `target_3D = veh_pos + range·bearing` uses the vehicle's OWN noisy pose; a
  small **Kalman filter inside the deterministic loop** fuses sparse fixes into the §8.1
  TargetEstimate (position, velocity, covariance, age).
- **The determinism fence:** the async VLM emits a sparse **acquisition trace**
  `{t, fix, conf}`; deterministic runs REPLAY the logged trace bit-exactly (a trace is
  frozen input data, like a wind seed — directive 11). The live "vibe-instruct" demo runs in
  the FENCED interactive mode (determinism waived, journal-recorded), exactly the §4.5
  `DRAG` split.
- **Mis-grounding gates (a wrong fix must not quietly fly the vehicle into empty ground):**
  confidence gate + multi-frame/Kalman-innovation (Mahalanobis) consistency + the
  rangefinder as an independent geometric witness. A rejected acquisition leaves the last
  valid estimate aging (`target_age` grows — the policy can hedge); a *bad* accepted one
  produces an honest MISS, never a clamp to truth.
- **Source-blindness (the wide-socket payoff):** guidance and policy code are identical for
  every `target_src`. Swapping seeded → beacon → VLM changes the *source*, never the
  consumer — no retraining of the net's architecture, no guidance edits. (Honest nuance for
  training: the *fields* exist from N0, but the policy only learns to *use* covariance and
  staleness if the curriculum randomizes them — an explicit N2/N3 curriculum axis, §19.6.)

---

## 9. Guidance

Strict order preserved: tier 0 must land repeatably before tier 1 exists (M2 ✓); tier 2 is
an offline oracle; **tier 3 (v2) is the learned policy, distilled from tier 1 and gated like
everything else.** Each tier feeds the next: tier 0 warm-starts tier 1; tiers 1+2 teach
tier 3 (§19).

### 9.1 Tier 0 — Hoverslam (the plant's correctness test)

*(unchanged from v1: forward-shooting Brent ignition solver re-solved every tick; the
constant-deceleration throttle law; first-order lateral; §8.3 attitude; leg deploy ≤250 m.
As-built additions recorded in D-002/D-007/D-009/D-012 — aero-aware thrust-only ignition,
smooth crossover steer_sign, the KDIV overspeed brake — live in `guidance_hoverslam.{h,c}`
and are directive-7-mirrored into the MPPI rollout.)*

### 9.2 Tier 1 — MPPI (CPU reference + CUDA port)

*(v1 spec unchanged: Williams path-integral MPC; OU noise; IS-correction; SGF smoothing;
ESS-adaptive λ; event-terminated rollouts; the suicide-burn-feasibility terminal; HIER
default / RAW advanced.)* **As-built [D-008…D-016]:** lateral-only HIER — the proven
hoverslam owns the vertical channel; MPPI owns `a_lat[2]` (+ throttle channel per
`MPPI_NCH 3`); K=256 CPU bit-deterministic under OpenMP; replan at 10 Hz with knot-hold
between; ~9 s/run CPU. `--mppi` is a selected configuration, not the silent default
(D-016's honest scope).

### 9.3 Cost function

*(unchanged from v1 + the as-built terminal structure of D-008/D-009: converging-velocity
tracking, ZEM foresight re-anchored at the ignition gate, bounded crash cost, TD_RXY/TD_VXY
touchdown terms — tuned ONLY against headless MC rates. v2 note: this terminal is the
template the policy's reward mirrors, §19.4.)*

### 9.4 GPU budget & layout — **rescoped by measurement [v2, D-015]**

v1's `p99 ≤ 6 ms at K=16384` was an fp32-era bar. The shipped port is **fp64 EVERYWHERE**
(the `.cu` unity-includes the same plant sources — a second fp32 plant would violate
directive 7), parity ≤1 ULP with 100% top-64 rank agreement, run-twice bit-identical at
K=256 and K=16384. Measured fp64 latency (sm_89): p99 ≈46 ms @K256 → ≈299 ms @K16384. The
honest budget is the controller's **100 ms replan period (10 Hz)** → fp64 real-time-viable
to **K≈1024 — exactly where the rate saturates** (kprobe 44/44/42). Goldens freeze at
K≈1024, sm_89. Reduction discipline unchanged: fixed-topology pairwise trees, no atomics,
`-fmad=false`, no fast-math anywhere.

### 9.5 Host/device parity gate — *(unchanged from v1; as-built it passes at ≤1 ULP, and a CPU `--mppi` vs CUDA `--mppi-cuda` ENTRY spot-run is bit-identical, D-016.)*

### 9.6 RNG — *(unchanged: Philox4x32-10, counter-based, keyed (master_seed, stream_id, step, lane); streams `wind`, `dispersion`, `mppi`, `nav`, + v2 `target` (§4.5).)*

### 9.7 Tier 2 — G-FOLD: offline oracle **+ teacher [v2]**

*(v1 role unchanged: per-scenario minimum-fuel benchmark; optimality is measured, never
claimed.)* Implementation per the §2 tooling rule: C/C++ (or an offline precompute outside
the repo) — not the Python sketch of v1. **Role extended [D-019]:** the convex solve is also
(a) a **superior warm-start generator** for MPPI and (b) a **secondary distillation label
source** for tier 3 on far-offset reach cases — blended with MPPI labels, never alone (the
min-fuel optimum rails throttle with zero margin; MPPI supplies robustness, G-FOLD supplies
reach — `runs/gfold_research.md`).

### 9.8 Tier 3 — `GM_NEURAL`: the learned guidance policy *(v2, D-019; anchor spec: `runs/neural_policy_design.md`)*

**The object.** `π_θ : O → A` — a small MLP (~3-4 dense layers, 64-128 units, ~10k-40k
params) mapping the LEGAL observation (§8.1 wide socket, normalized per frozen constants) to
the actuator command, evaluated fresh every 50 Hz guidance tick in **<10 µs** (three orders
inside budget; state-based guidance is ~4 orders smaller than image-based driving nets —
perception carries the representational load elsewhere, §8.4).

- **Observation:** the App-G socket — nav kinematics (target-relative offset, velocities,
  `zb2w` attitude, body rates), fuel, Mach/q̄ regime, fins/ignition state, `eng_health`,
  `relights_left`, ignition-altitude margin, TargetEstimate incl. covariance + age. NEVER
  `wind_world`/`wind_filt`/truth-target (§8.1 provenance rule).
- **Action (Tier A, primary):** `(a_lat[2], throttle)` — MPPI's exact channels; ignition
  (`engine_cmd`) and legs stay on the analytic triggers; the E3 entry supervisor sits above,
  as for GM_MPPI. **(Tier B, later):** learned `engine_cmd`/`n_eng` — the "re-time the burn"
  ambition, only after Tier A is gated. Outputs tanh-bounded, de-normalized, and C-clamped
  to the physical gamut (`±A_LAT_GAMUT`, `[ENG_THR_MIN, 1]`).
- **The determinism export (the move that makes a net belong here):** train offline →
  **FREEZE** → export weights + normalization as fp64 C arrays in `neural_policy_weights.h`
  (`NP_VERSION`d, committed like a golden; regenerating = ADR) → a hand-rolled
  **fixed-loop-order scalar fp64 forward pass** in C — no BLAS, no cuDNN, no threading, no
  atomics, fp-contraction pinned — bit-deterministic, memcmp-golden'd. Training may be
  nondeterministic; the shipped artifact never is (directive 11).
- **Mode plumbing:** `GM_NEURAL` joins the mode enum; a sim.c guidance block mirrors
  GM_MPPI's (same ignition latch + ada freeze); selected by `--neural`;
  `SET_GUIDANCE_MODE 3` (§10.6). **Default OFF ⇒ dead code ⇒ every existing golden
  byte-exact** (the standing leak gate, §13.6).
- **Training path (§19): DISTILL → RL → optional RESIDUAL.** Distill the MPPI expert first
  (DAgger; G-FOLD labels on reach cases) — MPPI-quality at ~1000× speed, the pipeline +
  export proven before any RL exists. Then RL (SAC; PPO fallback), warm-started from the
  distilled weights, on the widening disturbance curriculum, to EXCEED the expert toward
  the frontier. The RESIDUAL variant (bounded correction on hoverslam) is the
  safety/legibility fallback — shippable, but capped away from the frontier by
  construction.
- **The anti-cheat reconciliation, point by point:** reads only legal state; emits only a
  `GuidanceCmd`; **re-derives every tick from live state** (a feedback law expressed as a
  weight matrix — a learned constant, not a canned path; the same distinction that
  separates MPPI from a replay); frozen weights = precompute-in like every tuned
  coefficient. **The one real honesty risk is sim-overfitting** — defended by domain
  randomization (§19.3), the held-out law (§13.6: gate seeds s42/s7/s99 are NEVER trained
  on, plus held-out tail conditions), the full MC gate battery, and the §9.9 frontier
  check (out-of-frontier "landings" = leak investigation).

### 9.9 The reachability-frontier metric *(v2, D-019 — the official yardstick for disturbance batches)*

**Definition.** For scenario + disturbance realization `d`, `S_land` = terminal states graded
`V_PERFECT ∪ V_GOOD` (§7.3). The backward-reachable set
`BRS_t(d) = { x : ∃ admissible u(·) → S_land }`; its boundary is the **frontier**. States
outside it are *physically unlandable* — directive 6 requires they crash.

**The oracle.** `runs/sandbox/ceiling.c` already computes the lateral slice (D_phys ≈ 1107 m
nominal AERO, dt-converged, self-tested). Generalize per disturbance: engine-out = re-run
with 2-engine authority + a trim-authority debit (a two-line `build_amax` edit) → a strictly
smaller frontier; moving target = frontier measured to the *touchdown* target position; gust
= the perturbed profile. Per-world oracles at N4.

**The metric.** Label each IC in a batch IN- or OUT-of-frontier; report the confusion table.
**THE number is `P(land | in-frontier)`** — recovery rate. Sub-metrics: realization fraction
(median landed offset / D_phys; MPPI ≈ 0.70, the policy's target → 1.0⁻) and arrival quality
(td_v/td_lat on landers — catches reach-bought-with-hot-arrival). The two red cells:
IN-frontier crashes = the controller's shortfall; **OUT-of-frontier "landings" = a plant/
oracle bug or an assist leak — investigate, never celebrate.**

**Usage law (anti-dilution):** the metric is a diagnostic OVERLAY. It never softens a
landed-rate gate. M4 stays "AERO ≥90%" — the metric explains it (the p90 offset sits at
~0.63·D_phys, so ≥90 is achievable within physics iff realization pushes past ~0.70) and
arbitrates the N3 outcome honestly: ~1.0·D_phys realization = the policy found authority the
samplers couldn't; a 0.70 plateau = the wall is physical and M4's remaining lever is the
plant-authority ADR (D-018 path 1). **Either N3 outcome is a decisive, publishable
measurement.**

---

## 10. Process boundary — protocol (v3 as-built; v4 pre-authorized)

### 10.1 Transport

*(unchanged from v1)* Localhost WebSocket `ws://127.0.0.1:8787`, binary LE packed structs;
`ws.c` is a minimal embedded RFC6455 server subset (single client, ~250 lines, zero deps,
compiled only under `--serve`) — **[BUILT, D-007]**. Rates: physics 500 Hz · guidance 50 Hz ·
telemetry 125 Hz · STATS/CLOUD 10 Hz. Backpressure: drop-oldest, never block the sim thread.

### 10.2 HELLO

*(v1 contents unchanged: protocol version, build id, master seed, scenario id, module mask,
tick rates, the full vehicle geometry block + vehicle hash, pad table, environment config —
the renderer builds everything from this packet.)* v4 additions (§10.9): module-mask bits for
`TARGET`/`ENGINE_OUT`/`NEURAL`, the World id+hash (§4.7), and `NP_VERSION` when GM_NEURAL is
selected — so a replay is attributable to the exact frozen policy that flew it.

### 10.3 TLM @125 Hz — **as-built v3 [D-013]**

v1's layout + the D-013 extension: `pred_impact[2]` (f32, offsets 220/224) and `ignite_h`
(f32, offset 228) in the guidance-derived group; `sizeof(BlTlmFixed) == 288`;
`BL_PROTO_VERSION == 3`; TS decoder rejects mismatched versions loudly. Tails unchanged
(plan knots ≤64 — the ghost line; cloud ≤128 @10 Hz). Layout frozen by golden;
`static_assert`s compile-time-prove it; the TS mirror is regenerated/validated with it.

### 10.4 EVT — *(unchanged from v1: the reliable ordered event channel — PHASE_CHANGE, IGNITION_CMD, GREEN_FLASH, MACH1_CROSS(r_emit), LEG_DEPLOY, TOUCHDOWN, VERDICT, GUST, FAULT(type), TARGET_CHANGED(pad), RCS_PULSE, SOLVER_DEGRADED. v2: engine-out announces via `FAULT(type=ENGINE_OUT, args=k,t)`; target motion via the already-reserved `TARGET_CHANGED`.)*

### 10.5 STATS @10 Hz — *(unchanged; v4 adds the GM_NEURAL diagnostics: np_version, forward-pass time, and — for the residual variant — the live residual magnitude |Δ| as the legibility readout.)*

### 10.6 Upstream commands (closed enumeration — THE anti-cheat contract)

| Command | Payload | Notes |
|---|---|---|
| `RESET` | seed, scenario, module mask, [IC struct] | full re-init; IC only valid here |
| `PAUSE` / `RESUME` / `STEP{n}` | — | pacing only |
| `SET_TIMESCALE` | f32 0.05–8 | wall-clock pacing; never touches dt |
| `SET_GUIDANCE_MODE` | 0 none · 1 hoverslam · 2 mppi · **3 neural [v2]** | |
| `SET_TARGET` | pad_id **[v2: or target source/params per §4.5 — DRAG mode is the fenced path]** | goal change, legal mid-flight |
| `INJECT_DISTURBANCE` | type, magnitude, dir | gust **[BUILT]**, thrust deficit, Isp error, CoM offset, fin stuck, RCS pod out, sensor bias, **engine_out k@t [v2]** |
| `SET_NAV_MODE` | truth / noisy | |
| `PING` | echo | RTT display |

Nothing in this table writes vehicle state. Camera state never crosses the boundary. Adding
a command that carries state defeats the project — the enumeration is closed.

### 10.7 Coordinate & quaternion conversion — *(unchanged from v1: the single ui-side conversion `(x,y,z)ᵗʰʳᵉᵉ = (x, z, −y)ˢⁱᵐ`, `qᵗʰʳᵉᵉ = (qx, qz, −qy, qw)`, frozen App-C vectors.)*

### 10.8 Command journal — *(unchanged from v1: every run appends `{step, command}` after the seeded header; `--replay` re-executes bit-identically. v2: journals additionally record target-source config and, for PERCEIVED runs, reference the acquisition-trace artifact (§8.4) — a trace-replayed run is a first-class deterministic run; a DRAG/live-VLM run is journal-recorded but determinism-fenced.)*

### 10.9 Protocol v4 — the pre-authorized N0 schema extension *(v2, D-019)*

Executed as ONE validated unit (the D-010/D-011/D-013 precedent: field adds + version bump +
TS mirror + goldens re-frozen together), as the **opening act of N0** — the schema is the
real coupling point; widen it while ONE cheap client consumes it:

- **TLM +=** the TargetEstimate view (`target_est_xy[2]`, `target_est_vxy[2]`,
  `target_cov[3]`, `target_src u8`, `target_age f32`, `target_valid u8`) + `eng_health u8`
  (bitmask) + `n_eng u8` + `guidance_np_ver u16`. The renderer draws the ESTIMATE marker
  (with uncertainty ellipse) distinct from the truth deck — "what the rocket believes" made
  visible, directive-8-honest, and the whole §1 estimate-marker beat.
- **HELLO +=** module-mask bits (TARGET, ENGINE_OUT, NEURAL), World id+hash, NP_VERSION.
- **EVT:** none needed — `FAULT` + `TARGET_CHANGED` already cover it (audit confirms, not
  assumes, per D-011).
- `BL_PROTO_VERSION 3→4`; `sizeof` re-asserted; `goldens/protocol/*.hex` re-frozen
  (re-baseline pre-authorized by D-019).

---

## 11. Renderer — cinematic, physically grounded, telemetry-honest

*(§11.1–11.12 unchanged from v1: the pinned three@0.185.1 WebGPU/TSL stack, ingest/interp,
Cape-flats scene, Bruneton sky, procedural booster from HELLO, the physics-driven plume with
SRP envelopment + Mach-diamond spacing + TEA-TEB flash, ground interaction, weather, ASDS
dressing, cameras/director/long-exposure, the data-visualization suite, and the 8.3 ms @1440p
budget. D-010's adopted deltas stand: the pred_impact diegetic marker [fields BUILT, D-013],
LOX frost band, fin entry glow, the cloud-deck punch-through beat, near-ground shadow
cascade.)*

### 11.13 Multi-client doctrine, the UE endgame, and the mesh-fidelity rule *(v2, folding D-011 into canon)*

- **We ADD clients, never migrate.** The protocol is the contract; renderers are disposable.
  The WebGPU cockpit stays forever as the fast, agent-iterable, always-works observer; a UE
  5.x client (thin plugin: decode HELLO/TLM — a third static-asserted mirror — drive actor
  transforms; Nanite hull, Lumen/MegaLights plume-as-light, Niagara pyro, MetaSounds audio,
  LWC for 70 km→1 m) is the IMAX theater on the SAME one-way stream. Audio may be a third
  pure observer (its own client). The cinematic maximalism is built ONCE, in the winner.
- **The mesh-fidelity rule (the operator's planned upgrade, canonized):** the §5 param
  struct + HELLO geometry remain the **physics truth**. A high-fidelity artist mesh
  (TurboSquid/Blender-grade, for UE) is observer-side garnish — UNTIL the §20 CFD event,
  when the SAME mesh becomes the FluidX3D input that regenerates the plant's aero tables.
  From that moment mesh and tables version together under the vehicle hash: **one geometry
  source, by construction.** A visual mesh never feeds physics directly — only through the
  frozen CFD-precompute artifact (directive 11).
- The hard line, restated from D-011 verbatim: **CFD and UE must NEVER close a runtime loop
  into dynamics.** Precompute in, telemetry out, always.

---

## 12. Audio — the other half of realism

*(unchanged from v1: all-synthesized WebAudio with propagation honesty — per-source retarded
time, 1/r, distance lowpass/atmospheric absorption, doppler; the three-layer engine sound
with measured-skewness crackle; the TRIPLE sonic boom from MACH1_CROSS emission geometry;
RCS/aero/event foley; the mix bus. D-011's Tier-A (Web Audio in ui/) vs Tier-B (native
ISpatialAudioClient + Steam Audio observer client) ladder stands, gated behind M7 order.)*

---

## 13. Validation

### 13.1–13.4 — *(unchanged from v1: the unit/property suite incl. the determinism memcmp and journal replay; the physics oracles with tolerances; the MC harness + CSV contract + report tooling [tools/ now C/C++ per §2 — mcdiff/tracestat]; the flight recorder.)*

### 13.5 Goldens (house convention) — *(v1 + v2 categories)*

`goldens/`: protocol packet hex (v3 → v4 at N0), trajectory terminal hashes, Philox KATs, MC
baseline rates ±CI per scenario (current: TERMINAL s42 194/200; ENTRY --mppi s42 95/100;
AERO --mppi s42 44/60), sm_89 MPPI regression @K≈1024, **[v2] the policy KAT** (frozen
observation vector → bit-exact action vector, per NP_VERSION — the Philox-KAT pattern
applied to the net) and the frozen weights-header hash. Golden failures block merges;
re-baselining is an ADR.

### 13.6 N-track gates *(v2, D-019 — the standing battery for every learned-policy rung)*

1. **The leak check (named, CI-grade):** every new capability present-but-OFF (target module,
   engine-out, GM_NEURAL) reproduces every existing golden **byte-exactly**. A new mode must
   not perturb existing modes.
2. **Determinism-on:** the new path run twice = bit-identical (RESULT lines / CSV hash), incl.
   with disturbances armed (the D-017 pattern: gust-on pair identical to the digit).
3. **The held-out law:** gate seeds **s42/s7/s99 are NEVER in any training set** — they are
   the generalization proof. Train seeds are a disjoint range (e.g. 1000–9999). Additionally
   hold out *conditions*: train on nominal-to-moderate, gate on the severe compound tail.
4. **Parity/quality gates per rung:** N1 distill ≥42/60 AERO (within ~2 of the 44/60
   teacher); N2/N3 recovery-rate-vs-frontier ≥ MPPI's on the identical batch (§9.9), plus
   arrival-quality reporting (td_v on landers) so reach is never bought with hot arrivals.
5. **The full existing battery always:** `--selftest` PASS, TERMINAL 194/200 byte-exact,
   cross-seed, `--nav-noisy`, `--inject`, protocol goldens, CI on the toolkit-less runner.

---

## 14. Milestones — gates are quantitative, artifacts are mandatory

**M-track status at v2 adoption** (spec preserved; statuses per the ADR log):

| M | Scope | Gate | Status |
|---|---|---|---|
| M0 | Scaffold | builds + trivial selftest | ✅ D-001 |
| M1 | Plant + oracle suite | all oracles + determinism memcmp | ✅ (10 oracles) |
| M2 | Hoverslam headless | Terminal ≥98% LANDED | ✅ 97–99.9% across seeds (GOOD+ sub-bar ~91% remains tracked; the HARD tail is lateral-coupled, not quantization) |
| M3 | Socket + shell + scene | 10-min stream, conversion vectors | ✅ D-007 (`--serve` live; vitest green) |
| **M4** | **AERO_OFFSET ≥90%** | ≥90% landed | **OPEN — redirected (D-018):** sampler branch closed 4-angle-null; designated vehicles: **N3 policy** or the plant-authority ADR |
| M5 | MPPI CUDA | parity §9.5 + determinism | ✅ **rescoped D-015**: fp64 parity ≤1 ULP, bit-identical, K≈1024 @100 ms budget (the 6 ms fp32-era bar superseded; rate sub-gates fold into M4) |
| M6 | Full envelope: ENTRY ≥90% | ≥90% | ✅ **D-016**: `--mppi` 95/91/93 (s42/s7/s99); holds 90 under `--nav-noisy`; hoverslam stays default config |
| M7 | Cinematic core | 120 fps, §1 beats | **UNLOCKED** (directive 10; documentary view + audio sketch live on `--serve`) |
| M8 | Polish + ASDS + demos | reel; ASDS SS4 ≥85% | pending — **its "engine-out demos" line item = the N3 showcase, rendered** |

**The N-track (v2, D-019) — the perception-to-policy ladder.** Runs parallel to M7/M8
(aesthetics were gated on M6, which is green). Each rung lands in a worktree first, ships
only through §13.6, and maps onto the pillar docs' P/S numbering:

| N | Scope | Gate (all §13.6 + these) | Maps to |
|---|---|---|---|
| **N0** | **Widen once:** the §8.1 wide socket (TargetEstimate + EngineHealth at nominal) + protocol v4 (§10.9) + plant capabilities BUILT-BUT-OFF (target module §4.5, engine-out §4.6) | byte-equality vs EVERY golden at nominal/off (TERMINAL 194, ENTRY 95, AERO 44/60); v4 goldens re-frozen as one unit; each flag's off-state byte-clean | Step 0+1 (§H.0), P0 (+P1 beacon, optional) |
| **N1** | **Distill → `GM_NEURAL` ships:** env ABI, DAgger data-gen from the MPPI teacher, offline trainer, freeze/export, fixed-order C inference | AERO `--neural` ≥42/60 (s42; cross-seed s7/s99); bit-deterministic pair; policy KAT golden; ~1000× per-tick speedup measured | S0, P2 |
| **N2** | **RL beats the teacher on ≥1 axis:** SAC/PPO warm-started from N1; single-disturbance curriculum; the RESIDUAL safety variant | recovery-vs-frontier ≥ MPPI on ≥1 axis, held-out seeds, `--nav-noisy` incl.; re-frozen weights re-golden'd; correct out-of-frontier crashes | S1 |
| **N3** | **The joint distribution — THE SHOWCASE + the M4 attempt:** engine-out × gust × moving target × dispersions; the compound-recovery demo scored vs the shrunken BRS with the honest adjacent failure | showcase batch ≥ MPPI on the identical compound batch (held-out); **AERO ≥54/60 ⇒ M4 GREEN via GM_NEURAL** — else the honest 0.70·D_phys plateau verdict routes M4 to the plant-authority ADR. Either outcome is decisive (§9.9) | S2, P3 |
| **N4** | **Perception live + worlds:** beacon → VLM acquisition trace + language instruct (fenced live demo); World abstraction, Mars plant, per-world frontier oracles, cross-world policy | logged-trace determinism gate; fenced demo demonstrably honest (bad fix → honest miss); per-world D_phys oracles; recovery-vs-frontier across worlds | P4–P6, S3 |

**The build-order doctrine (canonical; full text `neural_policy_design.md` §H.0):** *widen
the interface once, ramp the difficulty gradually, and always validate new machinery on the
simplest physics that can exercise it.* Interface halves (what the policy reads/emits) build
FIRST and WIDE; behavior/difficulty halves are curriculum knobs ramped LATER; gust +
dispersions need zero interface work; distill single disturbances from MPPI (a valid
teacher, directive 7); reserve RL for the joint/compounding frontier. Do NOT build the
asymmetric plant and distill on it first.

Aesthetics land at M7/M8 with real gates, as always. The N-track's showcase is also M8's
centerpiece demo — the three maxima converge there.

---

## 15. Toolchain & build (pinned to this machine, 2026-07)

| Tool | Pin | Notes |
|---|---|---|
| GPU | RTX 4070 Ti SUPER, 16 GB, **sm_89** | goldens pinned here |
| CUDA | 13.1 | **OPTIONAL + auto-detected** (CI-safe: `check_language(CUDA)`, never in `project()`; toolkit-less runners build CPU-only; `--mppi-cuda` refuses exit-4) — as-built CMakeLists |
| MSVC | 2022 | `/std:c11`, `/fp:precise`, `/MT` static runtime |
| CMake | ≥3.24 | zero FetchContent in core |
| Node / pnpm | 24.x / 10.x | ui + shell |
| Rust | 1.96 | Tauri v2 shell |
| three | **0.185.1 exact** | r183+ API names (§11.1) |
| Trainer (offline precompute ONLY) | PyTorch on local RTX → RunPod H200 fleet | never in the sim path; outputs = fp64 C weight headers (§19); the sole sanctioned non-C surface, per §2 |

`tools/` is C/C++ (the v1 Python line is superseded per the §2 tooling rule). CI
(`.github/workflows/ci.yml`): build + 10-oracle selftest (incl. determinism memcmp) + the
TERMINAL MC gate on a clean `windows-latest` runner per push; §13.6 leak checks join it at
N0. Windows pacing: `timeBeginPeriod(1)` + QPC hybrid sleep-spin; never trust the 15.6 ms
timer.

---

## 16. Risk register (what will actually bite, and the pre-planned answer)

*(v1 risks 1–14 all stand: quaternion conversion, WebGPU fallback, stale tutorials, sky-addon
friction, WS backpressure, flag-skew determinism, GPU reduction nondeterminism, MPPI chatter,
underground rollouts, fp32 jitter, timer granularity, contact stiffness, audio autoplay,
ui scope creep.)* v2 additions:

15. **Sim-overfitting (the deep one, §9.8)** → domain randomization over disturbances AND
    plant params; the held-out law (§13.6.3); the full MC battery; the §9.9 out-of-frontier
    leak check. A policy that beats MPPI on held-out seeds under randomization, passing
    every gate, is not overfit — it is better. That is the bar.
16. **Reward hacking** → terminal-dominated reward mirroring the proven MPPI terminal;
    potential-based-only dense shaping (telescoping ⇒ unfarmable); time+fuel costs kill
    hover-farming; comparable W_vxy/W_rxy weights kill the reach-hot trade; the
    arrival-quality sub-metric catches what slips through (§19.4, neural doc §D.5).
17. **Gate dilution via the frontier metric** → the §9.9 usage law: overlay only; landed-rate
    gates never soften; a "but the frontier says" argument is an anti-pattern (§17).
18. **Interface churn re-architecting the net** → the wide-socket doctrine (§8.1/§14 N0):
    all interface halves built first at nominal; adding a disturbance is then a knob.
19. **VLM mis-grounding flying into empty ground** → the three-layer gate (confidence +
    innovation consistency + rangefinder witness, §8.4); rejected fix ⇒ estimate ages;
    accepted-bad fix ⇒ honest miss; never a clamp to truth.
20. **The G0 double-hat (worlds)** → §4.7: surface gravity varies per world; the Isp
    standard-gravity constant does not. Separate symbols, or Mars fuel accounting silently
    corrupts.
21. **Teacher cost in data-gen** → DAgger's label pass re-runs MPPI (~9 s/run CPU) during
    collection only; parallelize across cores and/or use the CUDA port as the label
    accelerator (its post-rescope deployment role); budget it as a one-time dataset build.
22. **Stale-exe gates (LNK1104 trap)** → a failed relink silently gates on the OLD binary
    (bit-D-017 nearly did); always confirm the link succeeded and the changed TU recompiled
    before trusting a gate run.

---

## 17. Anti-patterns (each silently makes it fake — check in every review)

*(the full v1 list stands: position-lerp, frame-dt, ground clamping, assist terms,
rollout-model drift, state-writing commands, unordered reductions, cost-tuning-by-ghost-line,
renderer smoothing, committed binaries, unpaired optimality claims, silent physics edits.)*
v2 additions:

- Training on (or "just peeking at") gate seeds s42/s7/s99 — the held-out law is absolute.
- Softening any landed-rate gate because the frontier metric "excuses" the misses.
- `build_observation` (or any guidance tier) reading `wind_world`/`wind_filt`/truth-target
  — the §8.1 provenance rule is the checklist.
- A policy emitting anything besides a `GuidanceCmd` — no state writes, no inner-loop
  bypass, no per-effector commands from the guidance layer.
- Regenerating `neural_policy_weights.h` (or the aero tables, or a World) without an
  ADR + version bump + re-golden — precompute artifacts are constants, not conveniences.
- Any nondeterministic component (VLM, CFD, UE, trainer) closing a runtime loop into
  dynamics — directive 11 is absolute; only frozen artifacts cross.
- Celebrating an out-of-frontier landing instead of investigating it as a leak (§9.9).
- Narrowing the socket "temporarily" or adding an observation field mid-training without
  treating it as the re-architecture event it is (§14 N0 doctrine).

---

## 18. The perception-to-policy stack (the v2 north star) *(v2, D-019)*

The unified architecture the N-track builds — assembled from the four pillar docs
(`runs/perception_to_policy_stack.md` is the map):

```
  [scene geometry] --> SENSOR-CAMERA (plant sensor, deterministic raster)          §8.4
                            |  image
                            v
                     VLM (Qwen, async, GPU)  --language--> "land there"            §8.4
                            |  bearing + confidence        (acquisition TRACE out)
                            v
              RANGEFINDER --range--> LOCALIZE --> KALMAN TRACK                     §8.4
                            |                          |
                            |     TargetEstimate: xy + v + cov + age (§8.1 socket)
                            v                          v
   nav (legal state) -->  [ GM_NEURAL π_θ | GM_MPPI | GM_HOVERSLAM ]              §9
                            |  GuidanceCmd (a_lat, throttle, n_eng, deploy)
                            v
                     control allocation (control.c) --> PLANT (dynamics.c, World-parameterized §4.7)
                            |  telemetry (protocol.h, one-way)
                            v
                     OBSERVERS (WebGPU cockpit, UE, audio) — pure, never loop back §11.13

  DETERMINISTIC GATED LOOP: nav -> policy -> control -> plant -> telemetry  (bit-reproducible)
  ASYNC / FENCED (precompute-in, directive 11): VLM traces, frozen weights, CFD tables,
                                                World params, live operator drag
```

The rule that keeps the whole tower honest is directive 11: everything in the gated loop is
deterministic and golden-able; everything nondeterministic enters only as frozen data. The
end state this architecture exists for: *tell a vehicle, in natural language, where to land
on any world — and watch a learned policy solve it through compounding failures, recovering
whenever physics permits, crashing honestly when it doesn't, bit-reproducibly.* (And the
same one-way stream that proves it headless renders it in an IMAX theater — the three maxima
in one artifact.)

---

## 19. Training & precompute pipeline *(v2, D-019; implementer spec: `neural_policy_design.md` §E/§H)*

### 19.1 The language reconciliation
The sim and every shipped byte are C/C++/CUDA (§2). The **trainer is precompute**: PyTorch
offline (local RTX for N1/N2 — hours-to-days; the H200 fleet for N3+ scale), producing DATA
(the fp64 weights header). It never runs in the sim path, never ships, never touches
determinism. The exact analog of `ceiling.c`'s D_phys or a swept gain — a bigger constant,
derived by optimization.

### 19.2 The gym is the plant
A thin C ABI over the existing `Sim` (`env_reset(seed, run, scenario, DisturbConfig*)` /
`env_step(sim, action[3], obs*, reward*)` / `env_free`) — reusing `sim_step`'s inner loop
VERBATIM: same integrator, same nav layer, same allocation, bit-for-bit the honest plant
(directive 7 extended to training). Python drives it via ctypes at the 50 Hz guidance tick
(the 500 Hz substeps stay in C); N parallel `Sim` instances batch across cores. The env
wrapper is worktree/trainer scaffolding — gitignored, unshipped.

### 19.3 Domain randomization + the curriculum
Every `env_reset` draws a seeded `DisturbConfig` from the joint distribution: wind
(azimuth/u_ref/Dryden/gust), engine-out (probability, engine, time-in-burn-window), target
(motion mode/speed/`target_cov`+staleness randomization — so the policy *learns to use* the
uncertainty fields, §8.4), plant dispersions (`--inject`), `--nav-noisy` (train on the noise
you fly). Curriculum: nominal → single axis (gust / engine-out / target / dispersion) →
pairwise → **the full joint** (the N3 showcase distribution), advancing on frontier-metric
thresholds. Every episode is seeded and replayable.

### 19.4 Objectives
Distillation: per-channel-weighted regression on the expert's executed `GuidanceCmd`, via
**DAgger** (label the states the *policy* visits — the covariate-shift fix; MPPI is available
in-process as the online expert). RL: terminal-dominated reward MIRRORING the proven MPPI
touchdown terminal (land bonus; continuous td_lat/v_xy/v_z/tilt/ω penalties; bounded crash),
plus potential-based-only dense shaping (hacking-proof by telescoping), small fuel/time/
smoothness/authority terms. The five named traps and defenses: neural doc §D.5.

### 19.5 The teacher doctrine (why the order works)
Because of directive 7, MPPI's rollouts see any armed plant disturbance and re-solve — so
**MPPI is a valid teacher for every SINGLE disturbance** (distill it per-axis, inherit its
recovery for free). It is a **weak** teacher for the JOINT/compounding tail — exactly where
RL, warm-started from the distilled floor, earns its keep. G-FOLD labels blend in on
reach-limited cases (§9.7). This is the §H.0 doctrine in one paragraph.

### 19.6 Freeze, export, version
Training ends → weights + normalization constants FREEZE → exported as fp64 C arrays
(`neural_policy_weights.h`, `NP_VERSION`) → the §9.8 fixed-order inference → the §13.5
policy KAT + full gate battery → committed. Re-training = a new NP_VERSION = an ADR event
with the same ceremony as any golden re-baseline. Held-out law: §13.6.3, absolute.

---

## 20. Offline precompute artifact registry *(v2, D-019 — directive 11's ledger)*

Everything nondeterministic contributes to the sim ONLY through this table. All artifacts:
generated offline (any language/tool), frozen, versioned, committed (or journal-referenced),
consumed bit-exactly. Regeneration = a deliberate, ADR-recorded event.

| artifact | generator (offline) | consumed by | version key | regen ceremony |
|---|---|---|---|---|
| **Policy weights** `neural_policy_weights.h` | PyTorch trainer (RTX/H200) | `GM_NEURAL` (§9.8) | `NP_VERSION` | ADR + policy KAT + full §13.6 re-golden |
| **Aero tables** (App-A.6) | today: hand-modeled **[chosen]**; future: **FluidX3D CFD** (OpenCL LBM, LES) run on the vehicle STL | plant + all rollouts (§6.3) | vehicle hash | ADR + vehicle-hash bump + FULL re-golden (rates will move — that is the point; it also retires the D-006 transonic-CoP and stage-length caveats) |
| **Visual-FX bakes** (plume/impingement fields, VDB/flipbooks) | FluidX3D / EmberGen / Blender pyro | observers only (Niagara/TSL) | asset hash | none (garnish; directive 8 still binds data pixels) |
| **VLM acquisition traces** | Qwen/llama.cpp async front-end (§8.4) | the Kalman → TargetEstimate | per-run trace hash (journal-referenced) | per-run artifact — replaying it IS the deterministic run |
| **World parameter sets** (§4.7) | literature + choice, pinned | plant | World hash (in HELLO) | ADR per world; per-world frontier oracle + goldens |
| **Star catalog** | baker script from HYG | renderer | file hash | cosmetic |
| **G-FOLD label sets / warm-starts** | offline convex solver (§9.7) | trainer + MPPI warm-start experiments | dataset hash | dataset-build note in the ADR that consumes it |

**The mesh+CFD coupling (the operator's planned upgrade, scheduled):** when the UE-grade
high-fidelity mesh arrives (§11.13), the SAME geometry feeds FluidX3D → regenerated
CA/CNα/CoP/fin-effectiveness tables → one ADR covering mesh + tables + vehicle-hash +
re-golden as a single event. Until then the **[chosen]** tables stand and the visual mesh
stays observer-side. One geometry source, achieved through the precompute gate rather than a
runtime link.

---

## Appendix A — Constants & formulas (implement exactly; unit-test against the numbers)

### A.1 US Standard Atmosphere 1976 (geopotential base H' km / T K / lapse K·km⁻¹ / p Pa)

```
0.0    288.15   −6.5   101325.0
11.0   216.65    0.0    22632.1
20.0   216.65   +1.0     5474.89
32.0   228.65   +2.8      868.019
47.0   270.65    0.0      110.906
51.0   270.65   −2.8       66.9389
71.0   214.65   −2.0        3.95642      top: 84.852 km' (86 km geometric), 186.946 K
```
`g0=9.80665, R*=8.31432 J/(mol·K), M=28.9644 kg/kmol, R_air=287.053, ρ0=1.225 kg/m³`.
Geometric↔geopotential via r0=6 356 766 m. Within a layer: gradient ⇒
`p = p_b·(T/T_b)^(−g0/(R·L))`; isothermal ⇒ `p = p_b·exp(−g0·(H−H_b)/(R·T))`.

### A.2 Dryden discrete filters (per axis, stepped at 500 Hz, V = |v_rel|)

First-order (u): `x⁺ = (1 − V·dt/L_u)·x + σ_u·√(2·V·dt/L_u)·η` (η ~ N(0,1) from `wind`
stream). Second-order (v,w): discretize `H(s) = σ·√(L/(πV))·(1+√3·(L/V)s)/(1+(L/V)s)²`
by Tustin at 500 Hz (state-space form lives with `wind_sample`; verify PSD vs σ² in a test).
Sub-1000 ft scales/intensities in §4.3; above 2000 ft L=533.4 m, σ per severity
(light 1.5 / moderate 3.0 / severe 6.0 m/s) **[MIL-F-8785C via standard realizations]**.

### A.3 Sea spectrum (SEA)

Pierson-Moskowitz `S(ω) = α g²/ω⁵ · exp(−β(ω0/ω)⁴)`, α=8.1e-3, β=0.74, ω0=g/U19.5.
48 components, equal-energy binning, seeded phases; deck heave/pitch/roll RAO gains
tuned to §4.4 amplitudes. Deterministic sum-of-sines; table exported in HELLO.

### A.4 Mass properties (two-column + dry)

Column i: height `h_i = m_i/(ρ_i A_t)`, centroid `z_i = z_base,i + h_i/2`,
`I_axial = ½ m_i R²`, `I_trans = m_i(3R² + h_i²)/12` + parallel-axis to vehicle CoM.
Dry structure: thin-wall cylinder `I_axial = m R²`, `I_trans = m(6R² + L²)/12` about its
CoM **[chosen]**. `İ` analytic: differentiate w.r.t. m_i at fixed geometry (closed form —
write it out in the dynamics source comments and test vs finite difference).

### A.5 Ignition ramp

`T(t_since_start)/T_cmd = smoothstep(0, 1, (t−0.5)/1.0)` for t∈[0.5, 1.5] s, 0 before
**[chosen]**; shutdown `exp(−t/0.15)`. Green-flash EVT at t=0.30 s.

### A.6 Aero tables (representative; frozen per vehicle hash)

```
M:      0.0   0.6   0.9   1.1   1.5   2.0   3.0   5.0   8.0
CA:     0.85  0.88  1.10  1.40  1.25  1.10  0.95  0.92  0.90
CNα/rad:2.0   2.1   2.4   2.5   2.4   2.3   2.2   2.1   2.0
Fin CNα_f/rad: 3.0 subsonic; ×0.55 dip for 0.8<M<1.2; ×0.8 for M>2   (per fin, S_f=2.4 m²)
```
**x_cp (bare body) — [amended D-005/D-006]:** v1 printed
`x_cp/L = 0.62 + 0.04·exp(−((M−1.05)/0.3)²) − 0.02·min(α/15°,1)`, which put the CoP
backwards (a wrongly self-stable bare body). As-built after the audits: **base ≈ 0.28 with
a +0.01 transonic bump — marginally unstable at all Mach**, per canon §5.4/§6.3; the code
(`dynamics.c xcp_frac`) is authoritative, and the VEH_STAGE_LEN-vs-VEH_LEN labeling caveat
(D-006) remains open. All of A.6 is **[chosen]** until the §20 CFD regeneration event
replaces it with mesh-derived tables (which also retires these caveats).

### A.7 Heating

`q̇ = 1.7415e-4 · √(ρ/R_n) · V³  [W/m², SI]`, R_n = 1.83 m; `Q = ∫q̇ dt` drives soot.

### A.8 Slosh (module SLOSH, default ON)

Per tank: planar pendulum, mass `m_s = 0.20·m_tank`, length from first-mode fit
`ω_s² = (g_eff·1.84/R)·tanh(1.84·h_i/R)`, damping ζ=0.05 (baffled) **[chosen]**.
Pendulum reaction force applied at tank centroid; excitation from body accel. States in
§6.1. Rollouts include it when the module mask says so (directive 7).

## Appendix B — Struct discipline *(unchanged: pack(1), explicit `_pad`, LE, static_asserts, hex goldens, generated TS mirror — never hand-skewed.)*

## Appendix C — Frame-conversion test vectors *(unchanged from v1 — frozen; ui and core both test them.)*

## Appendix D — Scenario library *(v1 table unchanged: TERMINAL / AERO_OFFSET [mean 500 σ150 per D-006/D-009] / ENTRY / ASDS_NIGHT / CHAOS / CUSTOM + common dispersions + CSV columns.)* v2 note: the compound showcase is NOT a new scenario row — it is ENTRY (or AERO) **composed with flags** (`--engine-out … --gust … --target … --inject [--nav-noisy]`), which compose deterministically (D-017 proved the pattern). Scenario rows define ICs; disturbances are orthogonal modules.

## Appendix E — Starting cost weights *(unchanged from v1; the policy-reward starting weights live in `neural_policy_design.md` §D and freeze with NP_VERSION.)*

## Appendix F — Provenance *(v1 list stands; add: Williams et al. T-RO 2018 lineage already present; DAgger — Ross/Gordon/Bagnell AISTATS 2011; SAC — Haarnoja 2018; PPO — Schulman 2017; potential-based shaping — Ng/Harada/Russell ICML 1999; RL-for-planetary-landing — Gaudet & Furfaro; TRN — JPL Lander Vision System / Perseverance 2021; FluidX3D — Lehmann; G-FOLD — Açıkmeşe/Blackmore/Carson as in v1.)*

## Appendix G — The frozen observation/action socket, v1 *(v2, D-019 — the interface the net is architected against; change = re-architecture event)*

**Observation `o` (input dim frozen at NP_N_IN; nominal-constant fields reserved from N0):**
target-relative offset `r_xy − target_xy` (2) · height above deck `h` (1) · `v_xy` (2,
optionally target-relative) · `v_z` (1) · attitude as `zb2w` (3) · body rates `ω` (3) ·
propellant mass (1) · Mach (1) · q̄ (1) · `fins_deployed` (1) · `engine_on`+`ign_timer` (2) ·
`eng_health[3]` (3) · `relights_left` (1) · ignition-altitude margin `h − ignite_h` (1) ·
`target_vxy` (2) · `target_cov` σ-features (2) · `target_age` (1) · `target_valid` (1) —
**≈28 features**, all §8.1-provenance-legal, normalized by the frozen `(μ, s)` constants
exported with the weights. History/frame-stacking: deferred; add only if N3's frontier
metric shows wind-inference is the binding shortfall (then k=3–5 stack, still feedforward).

**Action (Tier A):** `(a_lat[0], a_lat[1], throttle)` — world-frame lateral accel + throttle,
tanh-bounded, de-normalized, C-clamped to `±A_LAT_GAMUT` / `[ENG_THR_MIN, 1]`; ignition +
legs analytic. **(Tier B, gated later):** + `engine_cmd` logit (and/or `n_eng`).

---

*End of canon (v2 DRAFT). If reality and this document disagree, measure, then amend this
document with an ADR — never patch around it in silence.*
