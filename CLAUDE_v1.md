# BOOSTER LANDER — CLAUDE_v1.md

A 6-DOF propulsive-landing simulator in which the guidance **actually solves the landing
problem in real time**, wrapped in a renderer that aims for **cinematic, physically-grounded
beauty** — and an architecture that makes faking either one structurally impossible.

Native C/CUDA core does physics and guidance. Three.js (WebGPU, TSL) in a Tauri v2 shell
draws and *sounds* it. A one-way binary state stream connects them. The core proves itself
headless with Monte Carlo success rates; the renderer proves itself by reproducing, from
telemetry alone, what real booster landings look and sound like — down to the shock-diamond
spacing, the green TEA-TEB flash, and the triple sonic boom.

This document is the canon. It supersedes `CLAUDE_v0.md` (keep v0 for history; do not edit
it). It is written so that a fresh coding session can be pointed here and build the whole
thing without asking questions. Where a number is not public, it is **[chosen]** and pinned
anyway — a spec with holes breeds improvisation, and improvisation breeds cheating.

Provenance tags used throughout: **[official]** published primary source · **[estimate]**
credible public estimate · **[community]** telemetry reconstruction · **[chosen]** pinned
here for self-consistency. Sources collected in Appendix F.

---

## 0. Prime directives

These are not preferences. Violating any of them defeats the purpose of the project.

1. **State changes only through the integrator.** The guidance layer's entire output is an
   actuator command vector: throttle, two gimbal angles, four fin deflections, RCS mask.
   No code path anywhere writes position, velocity, attitude, angular velocity, mass, or
   slosh/contact states except `integrate_step()`.

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
   the same binary + same GPU architecture (goldens are pinned to sm_89, ORRERY convention);
   the CPU plant path is bit-identical everywhere the same binary runs.

5. **Headless must work.** The same core binary, with no socket and no renderer, runs N
   randomized descents and prints a success rate. This artifact is the proof the simulation
   is real. `ui/` and `shell/` deleted must not break `core/`.

6. **If guidance can't solve it, the vehicle crashes.** No assist term, no nudge, no soft
   clamp toward the pad, no recovery mode that bypasses the controller. A crash — and a
   tip-over, and a fuel-depletion ballistic arc — are valid, fully-simulated outcomes.

7. **One dynamics source.** Plant, hoverslam prediction, and MPPI rollouts call the same
   `__host__ __device__` equations of motion, including actuator lag models and active
   module set. Rollout-model drift from the plant is a cheat with extra steps. (Deliberate
   model mismatch is a legitimate robustness *experiment*, only after parity is established
   and only behind an explicit flag.)

8. **The renderer draws only what it is told.** Every pixel and every sound that represents
   simulation data derives from the telemetry/event stream. Visual-only garnish (dust
   swirls, grain) may use frame time and unseeded randomness; anything presented as data —
   ghost line, tapes, plots, booms — may not. No renderer-side smoothing that masks real
   oscillation (a "raw telemetry" toggle must exist and must be respected by the HUD).

9. **Honest audio and honest cameras.** Sound propagates at 343 m/s from emission events to
   the active camera (2.92 s/km); booms arrive late; distance strips highs. Cameras never
   influence the sim; camera state never crosses the boundary.

10. **Aesthetics last, physics first — but aesthetics are not optional.** The build order
    (§14) gates renderer work behind headless validation gates. Once unlocked, the visual
    milestones are real milestones with acceptance criteria, not decoration. Both maxima —
    max-true simulation and max-cinematic presentation — are the project.

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
  then rumble + crackle (positively-skewed impulse train) ramps; the **triple sonic boom**
  (engine end, fins+legs, top) cracks past ~15 s before the vehicle is down; touchdown
  crunch arrives 5.8 s after you see it. Long-exposure mode has been integrating the whole
  descent into a single streak photograph, which you export as PNG.
- **Replay.** The full run is in the renderer's ring buffer: scrub it, re-cut it from the
  tracker telescope (atmospheric shimmer, focal breathing), the onboard fisheye (soot
  accumulating on the lens), or the droneship deck cam (authentic signal-dropout easter
  egg, ASDS scenarios only). A mission summary card exports the stats.

If a feature does not serve this contract or the headless proof, cut it.

---

## 2. Session protocol (how to work in this repo)

House convention (matches ORRERY/DAVE/FIGUREHEAD):

- **Read order for a cold session:** this file §0–§2 → `RUN_STATE.md` (current milestone,
  next action) → `DECISIONS.md` tail (last 5 ADRs). Then work.
- **`RUN_STATE.md`** — the ledger. Update it every session: current milestone, what passed,
  next concrete action. Write for a stranger.
- **`DECISIONS.md`** — append-only ADRs (`D-001`, `D-002`, …) for any architectural choice
  or deviation from this spec. A deviation without an ADR is a bug.
- **Goldens** (`goldens/`) — frozen reference outputs (§13.5): protocol byte layouts,
  trajectory hashes, MC baseline rates. Hardware-pinned to sm_89. Re-baselining is an
  operator-signed event, recorded in `DECISIONS.md`.
- **Gates are gates.** Do not start a milestone before the previous one's gate is green
  (§14). Do not touch `ui/` aesthetics before M6 is green. Run the test suite before and
  after every work block.
- **No new dependencies without an ADR.** Core: zero external deps. UI: `three@0.185.1`,
  `uPlot`, `vite`, `typescript` — that's the list. Shell: `tauri@2.x` + `tauri-plugin-shell`.
- **Never violate §0.** If a task seems to require it, stop and write the conflict into
  `RUN_STATE.md` instead.

---

## 3. Repository layout

```
CLAUDE_v1.md          this file (canon)   CLAUDE_v0.md   history, do not edit
RUN_STATE.md          session ledger      DECISIONS.md   ADR log
core/                 C11 + CUDA. No graphics, no external deps.
  constants.h         every physical constant, one place (§App-A)
  vehicle.h           full vehicle parameter struct + KESTREL9 preset (§5)
  atmosphere.{h,c}    US76 0–86 km + pressure/temp/density/speed-of-sound (§4.2)
  wind.{h,c}          mean profile + Dryden filters + 1-cosine gust (§4.3)
  sea.{h,c}           [module SEA] deck motion spectrum (§4.4)
  rng.h               Philox4x32-10, __host__ __device__, counter-based (§9.6)
  dynamics.{h,cu}     shared EOM: forces, torques, mass props, actuator lags (§6)
  integrator.{h,c}    RK4 + quaternion handling + contact substepping (§6.7)
  contact.{h,c}       leg spring-damper-crush, friction, settling, verdict (§7)
  nav.{h,c}           measurement layer: truth | noisy (§8.1)
  control.{h,c}       attitude PD + control allocation + RCS PWM (§8.3)
  guidance_hoverslam.{h,c}   tier 0 (§9.1)
  guidance_mppi.{h,cu}       tier 1 (§9.2–9.6)
  protocol.h          packet structs, static_assert sizes, shared with ui via codegen
  ws.{h,c}            embedded minimal RFC6455 server, compiled only with --serve (§10.1)
  journal.{h,c}       command journal record/replay (§10.8)
  log.{h,c}           flight recorder: full-state binary log (§13.4)
  scenario.{h,c}      scenario table + dispersion sampling (§App-D)
  main.c              CLI: --serve | --headless | --replay | --selftest | --golden
  tests/              unit + oracle + parity tests (§13)
shell/                Tauri v2 (Rust). Spawns core as sidecar, supervises. Nothing else.
ui/                   Three.js r185 WebGPU renderer + WebAudio + HUD. Read-only observer.
  src/net/            socket client, packet decode, interp buffer, frame conversion (§11.2)
  src/scene/          world, pad, terrain, ocean/ASDS, procedural booster (§11.4–11.5)
  src/fx/             plume, dust, heat-haze, soot, weather (§11.6–11.8)
  src/cameras/        rigs + director (§11.10)
  src/audio/          synthesis + propagation graph (§12)
  src/hud/            tapes, ladder, ghost line, cloud, plots, cards (§11.11, §12.5)
tools/                Python 3.13. MC reports, G-FOLD oracle, star catalog baker, plots.
data/reference/       public telemetry reconstructions (events.xlsx), aero tables
runs/                 journals, logs, MC outputs (gitignored except goldens)
goldens/              frozen hashes + canonical packet bytes + MC baselines
```

`core/` must build and run with `ui/` and `shell/` deleted.

---

## 4. The world

### 4.1 Frames, units, conventions

- **World frame:** right-handed, **Z up**, origin at the *primary pad center*, X east,
  Y north. SI units, radians internally, degrees only at UI surfaces.
- **Body frame:** origin at the base-plane center (engine gimbal plane), +Z along the
  vehicle axis toward the interstage, X/Y through leg pairs. Legs at azimuths 0/90/180/270°,
  grid fins at 45/135/225/315°.
- **Quaternion:** body→world, **scalar-last (x,y,z,w)** to match `THREE.Quaternion`.
- **Flat ground, non-rotating Earth.** Explicit neglect list: Coriolis and centrifugal
  terms (< 0.3 % effect over a 500 s descent), Earth curvature (< 40 km downrange),
  gravity-gradient torque. Gravity magnitude does vary: `g(h) = g0·(R0/(R0+h))²`,
  `R0 = 6 356 766 m`, `g0 = 9.80665`. Record every neglect here; a fresh session must not
  "improve" physics silently — that is an ADR.

### 4.2 Atmosphere — US Standard 1976, not a bare exponential

Piecewise-linear temperature layers 0–86 km (full table: Appendix A.1). From the layer
table compute `T(h)`, `p(h)`, `ρ(h)`, and `a(h) = √(γ R T)` (γ=1.4, R=287.053). Exposed as
one pure function `atmo_eval(h, AtmoOut*)`. The guidance model uses the same function
(directive 7). The old `ρ0·exp(−h/8500)` survives only in comments as a sanity cross-check.

### 4.3 Wind & turbulence (deterministic, seeded)

Three layers, all fed from the `wind` RNG stream of the master seed:

1. **Mean profile.** Params per scenario: `(U_ref @ 10 m, azimuth, veer)`. Power law
   `U(h) = U_ref·(h/10)^0.14` up to 1 km; ×1.0→2.2 linear ramp 1→11 km (jet layer);
   decay to ×0.4 by 25 km; constant above. Direction veers linearly by `veer` (default
   +20°) across 0→11 km. **[chosen]** representative.
2. **Dryden turbulence, MIL-F-8785C.** Below 1000 ft: `L_w = h`,
   `L_u = L_v = h/(0.177+0.000823·h)^1.2`, `σ_w = 0.1·W20`,
   `σ_u = σ_v = σ_w/(0.177+0.000823·h)^0.4` (h in ft; W20 = 15/30/45 kt for
   light/moderate/severe). Above 2000 ft: L = 533.4 m, σ from severity table; blend
   1000–2000 ft. Realized as discrete first/second-order shaping filters stepped at 500 Hz
   with the exact discretization in Appendix A.2. Airspeed-dependent — use current |v_rel|.
3. **Discrete gust.** 1-cosine: `V = Vm/2·(1−cos(πx/dm))` over penetration distance `dm`,
   triggered by `DISTURB` command or scenario script.

Wind enters the plant **only** through relative airspeed `v_rel = v − v_wind(r, t)` used by
aero and fin forces. Guidance never reads `v_wind` directly — it feels it through state.

### 4.4 Sea state — module `SEA` (ASDS scenarios)

Deck pose = superposition of N=48 seeded sinusoids from a Pierson-Moskowitz spectrum,
significant height `Hs` (SS4 default: Hs=2.0 m → heave ±1.5 m, pitch/roll ±2.5°, periods
6–10 s — **[estimate]**, no public ASDS motion data exists; flagged assumption). Deck pose
is part of sim state, streamed in telemetry, and the contact solver works in the deck
frame. Guidance receives deck pose in its nav state (the real system knows the deck).
Station-keeping drift: ±3 m slow wander **[official]**.

---

## 5. The vehicle — `KESTREL-9` (Falcon-9-class, single canonical preset)

All parameters live in one struct in `vehicle.h`; the renderer receives them in the HELLO
packet and builds its model procedurally from the same numbers (§11.5). Change a dimension
once, physics and visuals both follow. A FNV-1a hash of the param struct is the *vehicle
hash* stamped into journals and goldens.

### 5.1 Structure & geometry

| Param | Value | Tag |
|---|---|---|
| Stage cylinder length | 41.2 m | [official] |
| Interstage length (top) | 6.5 m, open cylinder | [estimate] |
| Total length | 47.7 m | [official] |
| Diameter | 3.66 m (A_ref = 10.52 m²) | [official] |
| Dry mass (with legs+fins) | 25 600 kg | [estimate, pinned] |
| Dry CoM above base | 12.4 m | [chosen] |
| Dry inertia | thin-wall cylinder about dry CoM (formula App-A.4) | [chosen] |

### 5.2 Propellant & tanks (two-tank model — CoM and inertia are dynamic)

| Param | Value | Tag |
|---|---|---|
| LOX tank (forward/top) | max 287 400 kg, subcooled ρ=1220 kg/m³, column base 16.0 m above vehicle base | [community]/[chosen] |
| RP-1 tank (aft/bottom) | max 123 500 kg, ρ=833 kg/m³, column base 1.6 m | [community]/[chosen] |
| Mixture ratio (mass) | 2.33 : 1, both tanks drain simultaneously at MR | [community] |
| Tank cross-section | full 3.66 m circle minus wall: A_t = 9.9 m² | [chosen] |

Each tank is a liquid column of shrinking height `h_i(m_i) = m_i/(ρ_i·A_t)`. CoM, inertia
`I(t)`, and analytic `İ` from the two-column + dry-structure composition (App-A.4).
Scenario `prop0` is the *landing reserve*, not full load (Terminal 8 t, AeroOffset 10 t,
Entry 30 t — §App-D).

### 5.3 Engine (per-engine; burns use n_engines ∈ {1,3})

| Param | Value | Tag |
|---|---|---|
| Sea-level thrust (100 %) | 845 kN | [official] |
| Vacuum thrust | 932 kN — from the consistency rule below | [derived] |
| Isp SL / vac | 282 s / 311 s | [official] |
| Mass flow at 100 % | ṁ = 845 000/(g0·282) = 305.6 kg/s | [derived] |
| Min throttle | 40 % (338 kN SL) | [official-ish] |
| Effective exit area | A_e = (932−845) kN / 101 325 Pa = 0.859 m² | [derived] |
| Thrust vs ambient | `T(p) = T_vac − A_e·p(h)`; Isp follows | — |
| Gimbal range | ±5° both axes | [community] |
| Gimbal rate / accel limit | 15 °/s, 60 °/s² (2nd-order actuator) | [chosen] |
| Throttle dynamics | rate limit 200 %/s + first-order lag τ=0.10 s | [chosen] |
| Ignition sequence | cmd → +0.30 s TEA-TEB green-flash event → +0.50 s thrust rises → reaches commanded ≥40 % by +1.50 s (ramp shape App-A.5) | [chosen] |
| Shutdown | exponential decay τ=0.15 s | [chosen] |
| Relight budget | 2 relights (entry, landing) after scenario start | [community] |

**Consistency rule:** the triple (T_SL, Isp_SL, Isp_vac) is the source of truth; T_vac, ṁ,
A_e are derived. Never pin all five independently — public sources conflict.

3-engine burns: symmetric side pair assumed thrust-aligned through CoM (side-engine offset
torque neglected — recorded assumption). Thrust multiplier ×3, same gimbal on all.

### 5.4 Grid fins

| Param | Value | Tag |
|---|---|---|
| Count / placement | 4, hinged at interstage base ring, azimuths 45/135/225/315° | [official] |
| Panel size | 1.2 × 2.0 m, titanium | [community] |
| Mass | 200 kg each (in dry mass) | [estimate] |
| Deflection / rate | ±20°, 20 °/s | [chosen] |
| Force model | per-fin normal force at its mount: `F = q̄·S_f·CNα_f(M)·α_local`, S_f = 2.4 m², CNα_f table with **transonic authority dip** 0.8<M<1.2 (App-A.6); stall |α_local|>25° | [chosen, representative] |

Fins deployed ⇒ net vehicle CoP shifts aft of CoM (marginally stable base-first);
stowed ⇒ CoP forward of CoM (unstable). Both states in the aero model (§6.3). Roll
authority: differential fins in atmosphere, RCS in vacuum — a single gimbaled engine
produces **zero roll torque** about its own thrust axis (§6.9 trap list).

### 5.5 RCS (cold-gas nitrogen)

2 pods at 40.5 m, 4 nozzles each (±X, ±Y thrust directions per pod ⇒ pitch/yaw/roll
couples). 500 N per nozzle **[estimate]**, Isp 68 s, N2 budget 300 kg, min impulse bit
20 ms, PWM at 25 Hz with Schmitt deadband (§8.3). White puff event per pulse (renderer).

### 5.6 Landing legs

| Param | Value | Tag |
|---|---|---|
| Count / span | 4, deployed footprint Ø 18 m | [official] |
| Deploy | commanded by guidance at h ≤ 250 m AND landing burn active; pneumatic 1.2 s sweep; adds ΔCA=+0.25, shifts CoM −0.15 m | [chosen] |
| Contact | per-foot spring k=3.0 MN/m, damper c=120 kN·s/m, Coulomb μs=0.6/μk=0.45 | [chosen] |
| Crush core | plastic plateau 400 kN/leg over 0.40 m stroke, then hard stop ×10 k (one-way, stroke is state) | [chosen — sized so a 6 m/s full-mass landing strokes ~80 %] |

### 5.7 Structural & thermal budgets (honest failure modes)

- Max dynamic pressure: q̄ ≤ 80 kPa **[estimate]** (descent peaks ~2× ascent max-Q).
  Exceed for >2 s ⇒ `STRUCT_FAIL`.
- Side-load: |α_total| > 15° while q̄ > 30 kPa for >2 s ⇒ `STRUCT_FAIL`.
- Stagnation heating (Sutton-Graves, App-A.7): `q̇ = 1.7415e-4·√(ρ/R_n)·V³`, R_n = 1.83 m.
  q̇ > 300 kW/m² sustained >5 s ⇒ `THERMAL_FAIL` **[chosen]**. Integrated load `Q` drives
  the renderer's soot state. The entry burn exists to keep you under these lines.

---

## 6. The plant

### 6.1 State vector

Core (integrated, fp64 on CPU):

| Block | Size | Frame | Units |
|---|---|---|---|
| `r` | 3 | world | m |
| `v` | 3 | world | m/s |
| `q` | 4 | body→world, xyzw | — |
| `w` | 3 | body | rad/s |
| `m_lox`, `m_rp1` | 2 | — | kg |
| Actuator states: throttle_act, gimbal_act[2], gimbal_rate[2], fins_act[4] | 9 | — | —/rad |
| Engine state: ignition timer, relights left | 2 | — | s/— |
| Wind filter states | 6 | — | m/s |
| [SLOSH] per tank: pendulum angle 2 + rate 2 | 8 | body | rad |
| [LEGS] deploy fraction 1 + crush strokes 4 (plastic, monotonic) | 5 | — | —/m |

**Precision policy:** CPU plant integrates in **fp64**. GPU rollouts run **fp32** (they are
a stochastic planner; fp32 is fine and fast). Telemetry downcasts to fp32 (§10). Rationale
recorded here so nobody "optimizes" the plant to fp32: at h=70 km, fp32 position quantum is
~8 mm — visible jitter at telescope zoom, unacceptable drift over 500 s.

### 6.2 Forces and torques — always force × lever arm

Compute every force with its application point; torque = `(r_app − r_com) × F`. Never
inject a bare torque (exceptions: RCS couples and the fin-drag roll component, which are
genuine couples). Sum in body frame, rotate once.

- **Thrust.** Magnitude `n_eng · throttle_act · T(p_amb)` along the gimbal-deflected axis,
  applied at the gimbal pivot (base center). The pivot-to-CoM offset is the entire source
  of pitch/yaw authority.
- **Gravity.** `m·g(h)` at CoM. No torque by definition.
- **Aero.** At CoP (§6.3), from `v_rel`. Axial + normal decomposition.
- **Grid fins.** Four independent forces at their mounts (§5.4), from local α including
  the `ω × r` velocity contribution (fins damp rotation naturally).
- **RCS.** Pure couples per §5.5.
- **[SLOSH]** Pendulum reaction forces at tank attach points (App-A.8).

### 6.3 Aerodynamics (Mach-dependent, honest about uncertainty)

Base-first descent. Tables in App-A.6, all `[chosen, representative]` — sourced from
generic slender-body data, explicitly tunable, but **frozen per run** (vehicle hash):

- `CA(M)`: 0.85 subsonic → 1.40 transonic peak (M≈1.1) → 0.95 by M3.
- `CNα(M)`: 2.0 → 2.5 → 2.2 /rad. CoP: `x_cp(M, α)` table, forward of CoM bare-body.
- Fins add their own forces (not folded into body tables) — deployed state shifts net
  static margin as §5.4.
- **Supersonic retropropulsion shielding [official research]:** while burning, aero forces
  blend out with thrust coefficient `C_T = T/(q̄·A_ref)`: full aero for C_T<0.5, ~5 % for
  C_T>3, smooth blend between. During entry burn the plume displaces the bow shock and
  drag is negligible — deceleration is thrust-dominated (matches 45–56 m/s² community
  telemetry). This is physics, not garnish: without it the entry burn is mistuned.

### 6.4 Mass properties — recompute every derivative evaluation

`ṁ_prop = −n_eng·throttle_act·T/(Isp(p)·g0)`, split by MR into the two tanks. CoM(t),
I(t), İ(t) from the two-column model (App-A.4). Freezing I or CoM is a listed anti-pattern
— it silently breaks the vehicle exactly when tanks run low and control margins matter.

### 6.5 Rotational dynamics

`I ẇ = τ − ω × (I ω) − İ ω`. Both correction terms mandatory. Quaternion derivative
`q̇ = ½·q⊗[ω,0]` (scalar-last), renormalize after each full RK4 step, not inside stages.

### 6.6 Actuator dynamics are part of the plant

Command → actual passes through the lag models of §5.3–5.5 *inside the integrator*.
Guidance commands intents; the plant decides how fast hardware follows. MPPI rollouts
integrate the same lags (directive 7). One guidance tick of transport delay applies to all
guidance outputs (§8.2) — real GNC has latency; instant reaction is a cheat.

### 6.7 Integration

RK4, `dt = 2 ms`, 500 Hz. Contact phases substep at `dt/8` (§7.2). Ground-contact onset is
bisected inside the step to ≤0.1 ms so touchdown velocity is honest, not grid-quantized.

### 6.8 Termination & phase machine

```
INIT → COAST → ENTRY_BURN → AERO_DESCENT → LANDING_BURN → TOUCHDOWN → SETTLING
                                                              ↓            ↓
                                              LANDED | TIPPED | CRASHED
any phase → FUEL_DEPLETED (ballistic continues → impact) | STRUCT_FAIL | THERMAL_FAIL | LOC
```

Transitions are evaluated in the core only (renderer mirrors via EVT): burns by guidance
command + ignition state; `TOUCHDOWN` on first contact; `SETTLING` until KE < ε for 2 s or
30 s timeout; `LOC` = |ω| > 2 rad/s sustained 3 s. Verdict grading (§7.3).

### 6.9 Known traps (all live; re-read before touching dynamics)

1. Single gimbaled engine ⇒ **zero roll authority**. Roll comes from RCS/differential fins.
2. Gyroscopic term `ω×(Iω)` omitted ⇒ decorated 2D sim.
3. Drag sign under time-reversal — never integrate the plant backward; predictors use
   forward shooting (§9.1).
4. Contact clamping instead of contact dynamics ⇒ fake landings, no tipovers.
5. Fin local-α must include ω×r or fins won't damp — vehicles wobble forever.
6. Thrust must fall with ambient pressure at ignition altitude or hoverslam margins lie.

---

## 7. Contact, touchdown, and verdict

### 7.1 Geometry

Contact set: 4 leg feet (deployed positions from HELLO geometry) + 8 base-rim points +
nose point (crash geometry). Terrain: `z=0` plane (RTLS) or deck plane in motion (ASDS).

### 7.2 Leg contact model

Per foot: penetration spring-damper (`k`, `c` §5.6) along terrain normal, Coulomb friction
tangentially (stick-slip with μs/μk), plus the **crush core**: when leg axial load exceeds
400 kN the plastic stroke advances (one-way state, dissipative), up to 0.40 m then
hard-stop stiffness ×10. Contact forces enter the same integrator — the vehicle can skid,
rock, bounce, and tip. `SETTLING` integrates until rest. Tipover is a real dynamical
outcome (CoM leaves the support polygon), not a threshold check.

### 7.3 Verdict (evaluated after SETTLING, frozen into telemetry + journal)

| Grade | Criteria |
|---|---|
| `LANDED/PERFECT` | touchdown |v| ≤ 2 m/s, lateral ≤ 2 m, settled tilt ≤ 1°, strokes < 10 % |
| `LANDED/GOOD` | |v| ≤ 4 m/s, lateral ≤ 10 m, tilt ≤ 3°, all legs intact |
| `LANDED/HARD` | |v| ≤ 6 m/s, on pad, upright, any crush stroke ≥ 80 % |
| `TIPPED` | touched down survivable, then left support polygon |
| `CRASHED` | body contact, |v| > 6 m/s, or off pad (pad radius 26 m RTLS / deck rect ASDS) |

Touchdown target for guidance: ≤ 2 m/s **[official-ish: "≲2 nominal, 6 max leg design"]**.

---

## 8. Sensing and control architecture

The stack, at fixed rates, all inside `core/`:

```
plant state ──► nav.c (measurement layer) ──► guidance (50 Hz) ──► attitude+allocation (500 Hz) ──► actuator commands ──► plant
```

### 8.1 Measurement layer (`nav.c`)

Default `NAV_TRUTH`: pass-through. `NAV_NOISY` mode: position σ = [0.5, 0.5, 0.3] m,
velocity σ = 0.1 m/s, attitude σ = 0.1°, gyro bias random-walk, all seeded **[chosen]**.
`INJECT_DISTURBANCE(sensor_bias)` acts **here** — on measurements, never on truth (v0 left
this undefined; it matters). Guidance sees only NavState. Deck pose (ASDS) is part of
NavState.

### 8.2 Timing discipline

Guidance runs at 50 Hz on the state *as of its tick start*; its output is applied at the
*next* tick boundary (20 ms transport delay, always, both tiers). The attitude inner loop
and allocation run at 500 Hz between guidance ticks. No same-step sense→act.

### 8.3 Inner loop (shared by tier 0 and hierarchical tier 1)

- **Attitude controller:** quaternion-error PD with feed-forward:
  `τ_cmd = −K_q·q_err_vec − K_ω·ω + ω×(Iω)`, gains scheduled from current `I(t)` for
  ω_n = 1.5 rad/s, ζ = 0.8. Tilt command saturated at 8° during burns.
- **Allocation, by dynamic-pressure regime:** q̄ < 150 Pa → RCS only; 150–2000 Pa → RCS +
  fins; > 2000 Pa → fins (+ gimbal when burning). Gimbal: small-angle inverse via thrust ×
  pivot-to-CoM arm. Fins: damped pseudo-inverse of the 4×3 effectiveness matrix (includes
  differential roll), rate/deflection clamped. RCS: per-axis Schmitt trigger (on 0.02,
  off 0.008 rad/s error rates) → PWM 25 Hz, min impulse bit 20 ms.
- Roll is RCS-only in vacuum — by construction, not by tuning.

---

## 9. Guidance

Strict order: tier 0 must land repeatably before tier 1 exists (gate M2). Tier 2 is an
offline oracle, not a flight mode.

### 9.1 Tier 0 — Hoverslam (the plant's correctness test)

Min-throttle TWR at landing mass exceeds 1 (338 kN vs ~26 t ⇒ TWR_min ≈ 1.32): the vehicle
**cannot hover**; ignition timing must be exact.

- **Ignition solver:** forward shooting. From current NavState, propagate the *vertical
  channel* (with drag, mass depletion, pressure-dependent thrust, ignition ramp of §5.3)
  for candidate ignition time `t_ign`; Brent root-solve on "v reaches −v_td exactly at
  h = h_bias" (v_td = 1.5 m/s, h_bias = 2 m). Re-solve every guidance tick; never solve
  once and commit. If no root (too late): full throttle now, flag `SOLVER_DEGRADED`.
  Never integrate backward (trap 6.9.3).
- **During burn, throttle law:** track the constant-deceleration manifold —
  `a_req = (v² − v_td²)/(2·max(h−h_bias, ε)) `, `T_cmd = m·(a_req + g − D_est/m)`,
  throttle = clamp(T_cmd/(n·T(p)), 0.40, 1.0). Terminal 10 m: hold v_td until contact.
- **Lateral channel:** `a_lat_cmd = −Kp·r_xy − Kd·v_xy` (gains gated by time-to-go);
  during burn → tilt command `atan(a_lat/a_vert)` capped 8°; during AERO_DESCENT → fin α
  command `α = clamp(m·a_lat/(q̄·A·CNα), 6°)` steering lift toward the pad.
- Attitude/allocation: §8.3. Legs: command deploy at h ≤ 250 m during burn.

If a well-solved hoverslam lands repeatably, the plant is sound. If not, fix the plant —
do not touch the optimizer.

### 9.2 Tier 1 — MPPI on CUDA (the reason the GPU is here)

Canonical Williams et al. path-integral MPC **[official, T-RO 34(6) 2018]**:

- **Sampling:** K rollouts, control-noise sequences ε ~ correlated Gaussian around the
  warm-started previous solution (shift by one control step). Noise is Ornstein-Uhlenbeck
  (θ = 0.15/step) per channel — white noise chatters, colored noise flies **[community:
  SMPPI/log-MPPI literature]**.
- **Cost with importance-sampling correction:**
  `S_k = φ(x_T) + Σ_t [ q(x_t) + γ·û_tᵀ Σ⁻¹ ε_t ]`, `γ = λ(1−α)`, α = 0.02.
  Baseline: `ρ = min_k S_k`; weights `w_k = exp(−(S_k−ρ)/λ)/η`.
- **Update:** `u_t += Σ_k w_k·ε_t^k`, then Savitzky-Golay smoothing (window 9, order 3),
  clamp to actuator limits, execute first 20 ms, shift, repeat at 50 Hz.
- **Adaptive temperature:** λ servo to effective sample size ESS ∈ [2 %, 10 %]·K.
- **Event-terminated rollouts:** a rollout that crosses the terrain evaluates its terminal
  cost *at the crossing* and freezes (no underground integration — classic silent bug).
- **Horizon vs burn length:** H = 200 × 25 ms = 5 s, shorter than a 15–33 s burn. Rollouts
  that end mid-air pay a **suicide-burn feasibility terminal cost**: the residual of the
  §9.1 vertical-channel solve from the rollout's end state (can this state still be
  landed?). This is what makes a receding 5 s window solve a 30 s problem. Spec'd, not
  optional.

**Control parameterization — two modes, hierarchical is default:**

| Mode | Channels | Notes |
|---|---|---|
| `HIER` (default) | throttle + 2 lateral-accel components | inner loop §8.3 runs *inside the rollout dynamics* (shared code); robust, converges fast |
| `RAW` (advanced) | throttle + gimbal[2] + fins[4] | the purist mode; expect chatter without tuning; RCS stays on the §8.3 rate damper |

### 9.3 Cost function (starting weights — tune ONLY against headless success rates)

Terminal (at ground-crossing or feasibility-weighted at horizon):
`40·|r_xy|² + 60·|v|² + 800·tilt² + 200·|ω|² + 0.02·(m0−m)` + crash indicator 1e6.
Running: glideslope `5·max(0, tan(35°)·|r_xy| − z)²`, q̄ overshoot `1e-3·max(0, q̄−60 kPa)²`,
saturation and fin-rate penalties, plus the γ-term above. Weights in `constants.h`,
logged in STATS, frozen into goldens. Tuning against "the ghost line looks pretty" is a
listed anti-pattern.

### 9.4 GPU budget & layout (sm_89, RTX 4070 Ti SUPER)

K = 8192 default / 16384 quality, one thread per rollout, state in registers, dynamics
`__host__ __device__` from `dynamics.cu`, block 128. ~0.7 GFLOP per replan — microseconds
of math; the gate is end-to-end: **p99 solve ≤ 6 ms at K=16384 including transfers**.
Reductions (η, weighted per-step control sums): fixed-topology pairwise trees, no atomics,
bit-stable on fixed arch. `-fmad=false` on guidance kernels (parity over last-percent
perf); CPU host `/fp:precise`, fp contraction off. Fast-math is banned project-wide
(house rule).

### 9.5 Host/device parity gate

Same rollout (K=64, fixed seeds) on CPU (reference impl, same code via `__host__`) and
GPU: per-step |Δr| < 1e-3 m over 200 steps, identical event terminations. Runs in CI
(`--selftest`). Plant fp64 determinism is a separate memcmp golden. After parity is green,
model-mismatch experiments (deliberately degraded rollout model) are allowed behind
`--mismatch` — as a robustness study, never silently.

### 9.6 RNG

`rng.h`: Philox4x32-10, counter-based, `__host__ __device__`, keyed
`(master_seed, stream_id, step, lane)`. Streams: `wind`, `dispersion`, `mppi`, `nav`.
No state arrays, no curand dependency, bit-identical host/device — this is what makes
parity and journal replay trivial.

### 9.7 Tier 2 — G-FOLD as offline oracle (not a flight mode)

`tools/gfold_oracle.py`: 3-DOF lossless convexification (Açıkmeşe/Blackmore/Carson, IEEE
TCST 2013 — min-thrust annulus via slack Γ, pointing cone, glideslope, z=ln m change of
variables) in cvxpy/Clarabel. Purpose: per-scenario minimum-fuel benchmark. MC reports
grade MPPI fuel usage against the oracle (target: median ≤ 1.15× optimal). Optimality is
*measured*, not claimed — nothing ships as "optimal" without this pairing (house rule).

---

## 10. Process boundary — protocol v2

### 10.1 Transport

Localhost WebSocket, default `ws://127.0.0.1:8787`, binary frames, little-endian, packed
structs. `ws.c` is a minimal embedded RFC6455 **server subset**: single client, server
handshake (SHA-1+base64 of the accept key), unfragmented binary frames, ping/pong, close.
~250 lines, zero deps, compiled only under `--serve` (headless never links it). The
webview connects directly — no relay hop through Rust (v0's relay was dead latency; the
shell only spawns and supervises). No JSON anywhere on the hot path; no frames streamed.

Rates: physics 500 Hz · guidance 50 Hz · **telemetry 125 Hz** (every 4th physics step —
v0's 120 Hz did not divide 500) · STATS/CLOUD 10 Hz · render at display refresh,
interpolated. Backpressure: drop-oldest, never block the sim thread; `seq` gaps tell the
renderer.

### 10.2 HELLO (once, on connect; also written at the head of every journal/log)

Protocol version, build id, master seed, scenario id, module mask
(SLOSH|SEA|LEGS|NAV_NOISY…), tick rates, **the full vehicle geometry block** (§5 struct)
+ vehicle hash, pad table `{id, center xy, radius}` (RTLS: PAD_A at origin, PAD_B at
(250, −180) for divert demos), environment config (wind params, sea state, sun elevation,
time-of-night). The renderer builds its booster mesh, pads, and cameras *from this packet*
(§11.5). One source of truth; visuals cannot drift from physics.

### 10.3 TLM @125 Hz (fixed ~230 B + tails)

All fields fp32 unless noted. State: `r[3] v[3] q[4] w[3]`, mass block
(`m, com_z, I_diag[3], prop_lox, prop_rp1`), actuators (`throttle_cmd/act,
gimbal_cmd[2]/act[2], fins_act[4], rcs_mask u16, n_eng u8`), environment & derived
(`mach, qbar, alpha_total, p_amb, wind_local[3], a_body[3], qdot_heat, Q_heat`), guidance
(`phase u8, guidance_mode u8, verdict u8, t_go, dist_pad, solver_flags`), legs
(`deploy_frac, stroke[4]`), `f_aero[3]`, ASDS deck pose when SEA active
(`deck_z, deck_quat[4]`), `seq u32, step u64, t f64`.
Tails: `plan_n ≤ 64` knots `{r[3], throttle}` (fresh each guidance tick — ghost line,
colored by planned throttle); `cloud_n ≤ 128` rollout terminal samples `{xy[2], weight}`
@10 Hz (the "possibility cloud" + dispersion ellipse). Layout frozen by golden
(§13.5); `static_assert(sizeof)` in `protocol.h`; a codegen script emits the matching
TypeScript decoder (`tools/gen_protocol_ts.py`) so the two sides cannot skew.

### 10.4 EVT (reliable, ordered)

`{step u64, t f64, code u16, args f32[6]}`. Codes: `PHASE_CHANGE, IGNITION_CMD,
GREEN_FLASH, ENGINE_START, ENGINE_SHUTDOWN, MACH1_CROSS(r_emit[3]) — the boom emission,
LEG_DEPLOY, TOUCHDOWN(v_impact, tilt), VERDICT(grade), GUST(vec), FAULT(type),
TARGET_CHANGED(pad), RCS_PULSE(mask), SOLVER_DEGRADED`. Audio schedules propagation-delayed
sound from these (§12); the director cuts on them; the HUD ticker prints them. Events are
the *only* trigger channel — animation-driven triggers are an anti-pattern.

### 10.5 STATS @10 Hz

Solver `{last, p50, p99}` ms, ESS, λ, best/mean rollout cost + top-3 cost-term breakdown,
sim-rate actual vs target, journal byte count, dropped-frame count. Feeds the engineering
overlay; also how you catch a dying GPU before it lies to you.

### 10.6 Upstream commands (closed enumeration — THE anti-cheat contract)

| Command | Payload | Notes |
|---|---|---|
| `RESET` | seed, scenario, module mask, [IC struct] | full re-init; IC only valid here |
| `PAUSE` / `RESUME` / `STEP{n}` | — | pacing only |
| `SET_TIMESCALE` | f32 0.05–8 | wall-clock pacing; **never touches dt** |
| `SET_GUIDANCE_MODE` | 0 none · 1 hoverslam · 2 mppi | |
| `SET_TARGET` | pad_id | goal change, legal mid-flight (divert demo) |
| `INJECT_DISTURBANCE` | type, magnitude, dir | gust, thrust deficit ±%, Isp error, CoM offset, fin stuck, RCS pod out, sensor bias |
| `SET_NAV_MODE` | truth / noisy | |
| `PING` | echo | RTT display |

Nothing in this table writes vehicle state. Camera state never crosses the boundary.
Adding a command that writes state defeats the project — the enumeration is closed.

`INJECT_DISTURBANCE` earns its keep: a scripted animation shatters under a mid-descent
gust or a 15 % thrust deficit; a real controller visibly re-plans (cloud scatters, ghost
line snaps) and still lands. Keep it one keystroke away in the UI.

### 10.7 Coordinate & quaternion conversion (exactly one function, ui-side)

Sim Z-up → three Y-up: `(x,y,z)ᵗʰʳᵉᵉ = (x, z, −y)ˢⁱᵐ`. Quaternion by the same basis
rotation (conjugation by Rx(−90°)) reduces to the **same component permutation**:
`qᵗʰʳᵉᵉ = (qx, qz, −qy, qw)`. Do not hand-derive variants; implement, then verify with the
frozen test vectors in App-C (rotate-then-convert must equal convert-then-rotate to 1e-6).
This function is the only place conversion happens.

### 10.8 Command journal — interactive runs are reproducible too

Every run (served or headless) appends `{step, command}` records after a header
`{seed, scenario, modules, vehicle hash, build id}` → `runs/*.rjrn` (typically <1 KB).
`core --replay run.rjrn` re-executes bit-identically and prints the terminal state hash;
`--verify <hash>` gates it. This turns *any* interesting interactive moment — including
disturbances you injected by hand — into a regression test and a renderer-replayable
artifact. The flight recorder (§13.4) is the heavyweight sibling for offline analysis.

---

## 11. Renderer — cinematic, physically grounded, telemetry-honest

### 11.1 Stack (pinned; verified July 2026)

- `three@0.185.1` exact. **WebGPURenderer** primary (production since ~r171), automatic
  WebGL2 fallback (same scene graph; TSL transpiles). Detect and log the actual backend
  (`renderer.backend.isWebGPUBackend`) into the HUD build info.
- Imports: `three/webgpu`, `three/tsl`, `three/addons/*`. **r183 renames that stale
  tutorials miss:** `PostProcessing` → **`RenderPipeline`**; `Clock` → `Timer`.
  `AnamorphicNode` was removed in r185 (use bloom patterns).
- Post chain (all `three/addons/tsl/display/*`): scene pass with
  `mrt({ output, emissive })` → **bloom on the emissive target** (the HDR plume core; the
  canonical r185 `webgpu_postprocessing_bloom_emissive` pattern) → TRAA → optional
  MotionBlur (velocity MRT) + DepthOfField (cinematic cams) → LensflareNode →
  ChromaticAberrationNode (subtle) → FilmNode grain + vignette.
- **Tone mapping: AgX** (`THREE.AgXToneMapping`, r160+) — degrades saturated emissives
  gracefully where ACES hue-skews; ACESFilmic behind a toggle.
- Depth: `WebGPURenderer({ reversedDepthBuffer: true })` (r183+) on WebGPU;
  `logarithmicDepthBuffer` on the WebGL2 fallback. Re-verify fallback behavior at M7.
- **Floating origin:** authoritative positions in JS doubles; world is rendered
  camera-relative (rebase when |camera offset| > 2 km). 70 km→1 m in one continuous shot
  with no jitter is an acceptance test, not a hope.
- Tauri v2: core is a sidecar (`bundle.externalBin: ["binaries/booster-core"]`, on-disk
  name `booster-core-x86_64-pc-windows-msvc.exe`), capability
  `shell:allow-execute {sidecar:true}`. CSP: `connect-src ipc: http://ipc.localhost
  ws://127.0.0.1:*`. WebGPU is default-on in current WebView2 (no flags since 113); if
  `additionalBrowserArgs` is ever set, re-add wry's default `--disable-features=…` string.
  Dual-GPU laptops: WebView2 may composite on the iGPU (known issue) — document the
  Windows graphics-settings override; this box (RTX 4070 Ti SUPER only) is unaffected.

### 11.2 Ingest & interpolation

Decode packets with the generated TS decoder; ring-buffer the last N=2 telemetry frames +
full-run history (125 Hz × 600 s ≈ 17 MB — keep it; it *is* the replay system). Render
~one packet interval in the past: lerp position, slerp attitude, hold actuators. This
absorbs 125 Hz→display-Hz beating without touching the sim. "Raw" toggle disables
interpolation for debugging (directive 8).

### 11.3 Scene: Cape-flats RTLS default

Concrete pad (Ø 60 m, generic circle-X markings, weathering), scrub flats to a water
horizon, access road, distant hangar block, floodlight ring (night). **Scorch
persistence:** each landing's plume impingement accumulates into a decal render-target
that survives resets within a session — the pad earns its soot. ASDS scenario swaps in
ocean + droneship (§11.9).

### 11.4 Sky, sun, stars

`@takram/three-atmosphere@0.19.x` (`/webgpu` entry): Bruneton precomputed scattering +
Hillaire multi-scatter LUT — correct from ground to space, aerial perspective included.
This is what makes 62 km look like *space* and 2 km look like *Florida dusk* in the same
continuous shot. Scenario sets sun elevation + time-of-night. Stars from a
`tools/make_stars.py`-baked HYG subset (9 000 brightest, B-V colorized points) — no binary
assets in the repo; everything is procedural or script-generated. Fallback if the addon
fights the pinned three version (it peers `>=0.170`): `SkyMesh` + altitude fade to black.

### 11.5 The booster, procedurally, from HELLO

Build geometry in code from the §5 dimensions: tank cylinder with dome joints, interstage,
octaweb + 9 bells (center bell articulated by `gimbal_act`), 4 grid fins (lattice via
alpha-carded instancing, hinged, driven by `fins_act`), 4 legs (telescoping pistons driven
by `deploy_frac`), RCS pods. Materials: TSL triplanar PBR — painted white body, panel
lines, weathered aluminum octaweb, titanium fins. **Soot state:** blackening driven by
telemetry `Q_heat` with clean leg-shadow stripes (the real post-entry pattern), plus
gas-generator streak. Bells: regeneratively cooled ⇒ **no glow** on the booster (glow is
an MVac-only effect — encoded rule so nobody "improves" it), faint red only after
sustained burns, decaying post-shutdown.

### 11.6 The plume (the crown jewel — every parameter physics-driven)

Analytic raymarched volume in a cone-fitted proxy (custom TSL node), parameterized by
`(p_chamber/p_amb, throttle, mach, |v_rel|)` from telemetry:

- **Shock cells:** first Mach-disk distance `x₁ = 0.67·D_e·√(p0/p_amb)` (Apogee/Ashkenas-
  Sherman form), cell spacing scaling √(pressure ratio): at sea level a tight diamond
  ladder; cells **stretch and fade with altitude** until 1–2 remain by ~35 km; above that
  the plume *balloons* toward km-scale translucency (underexpansion). Diamond brightness =
  afterburning reignition at each disk.
- **Color:** kerolox soot blackbody ramp (deep orange core → yellow-white throat), fuel-
  rich afterburn sheath, plus the **dark sooty gas-generator streak** alongside the bell.
- **SRP mode** (entry burn: burning while supersonic): plume does not trail — it
  **wraps forward and envelops the vehicle** in an unsteady sheath with a bow-shock cap
  (NASA WB-57 imaging), with plume-shedding flicker. Blend by C_T like the physics does.
- **TEA-TEB green flash** on `GREEN_FLASH` events: brief boron-green burst at the bell,
  ~0.3 s, before throttle rises.
- **Heat haze:** `viewportSharedTexture` backdrop distortion shroud above the bell(s) —
  screen-space refraction, the canonical TSL pattern (`webgpu_backdrop` family).
- **Plume as light:** shadow-casting spotlight down the plume axis + point light at the
  bell, intensity/color from throttle & mixture; it lights the pad, the vehicle's own
  base, and the dust. At night this one system carries the scene (v0 was right; keep it
  first among equals).
- Outer particles: GPU compute particles (`webgpu_compute_particles` pattern) for soot
  wisps and detached vortices, lit by the plume light.
- Optional M8 showcase: a small Eulerian fluid dome (adapted `webgpu_volume_fire`,
  ~100³ grid, active only below 500 m) for ground-interaction rollup. The analytic plume
  is the deliverable; the fluid is garnish behind a quality tier.

### 11.7 Ground interaction

Impinging-jet structure from the literature, driven by `(thrust, h)`: stagnation dome →
**radial wall jet** with Görtler-streak azimuthal banding (the real striated dust pattern),
recirculation lofting at low h, all as curl-noise-advected GPU particles lit by the plume;
intensity ∝ thrust/h². RTLS: dust + grit + pad scorch decal accumulation. ASDS: steam/spray
instead, wet-deck reflectance boost, spray droplets on near cameras. Debris pebbles
(instanced, ballistic, visual-only) on the first second of wall-jet.

### 11.8 Weather & presets

Scenario-driven: clear/haze/broken cloud (billboard + volumetric-lite), wind-streamer flags
on the pad (reading true wind from telemetry — they are instruments, honest ones), night /
dusk / dawn / noon sun presets. Rain preset (M8): streaks, plume steam interaction, lens
droplets.

### 11.9 ASDS scenario dressing

Gerstner/FFT ocean (three webgpu ocean example lineage) matched to the SEA module's
spectrum seed — the *rendered* swell phase-locks to the *simulated* deck motion (same
sinusoid table from HELLO; one source of truth). Droneship hull + deck markings + name
plate (`OF COURSE I STILL LOVE YOU` homage is fine as `STILL I LOVE YOU` generic), thruster
wash, deck camera with the **authentic feed-dropout easter egg** (vibration kills the
uplink at landing-burn peak; feed freezes, "SIGNAL LOST", restores after touchdown —
default ON for ASDS, clean feed always recorded for replay; exactly like reality).

### 11.10 Cameras & director

Rigs (all renderer-side; never cross the boundary): `PAD_TRACKER` (2 km, long-lens with
auto-zoom, atmospheric-seeing shimmer + focal breathing + operator lag/overshoot),
`CHASE` (spring-arm), `ONBOARD_DOWN` (fisheye, soot accumulating on the lens through entry,
exposure flicker at ignition), `ORBIT`, `FREE` (photo mode), `DECK_CAM` (ASDS), `DIRECTOR`
— a state machine cutting on EVT beats (ignitions, Mach 1, leg deploy, touchdown) with
seeded variety per run. Replay scrubber runs any camera over the telemetry ring buffer;
slow-mo is `SET_TIMESCALE` live or ring-buffer playback offline.

**Long-exposure mode:** accumulate linear HDR (`accum += frame·dt`) across the whole
descent → tonemap → the iconic streak photograph; PNG export. **Photo mode:** pause via
command, free cam, DoF, 4× supersampled still export.

### 11.11 Data visualization (the guidance made visible)

- **Ghost line:** the `plan[]` tail as a fat polyline from vehicle to pad, color-mapped by
  planned throttle. It writhes, snaps on disturbances, and the vehicle chases it — the
  optimizer thinking, on screen. No faked implementation can produce it.
- **Possibility cloud:** the 128 rollout endpoints as weight-alpha'd sprites + a fitted 2σ
  dispersion ellipse breathing on the pad.
- **Glideslope cone** wireframe, **velocity vector**, **attitude ladder**.
- All of it strictly from telemetry (directive 8), each toggleable.

### 11.12 Performance budget (RTX 4070 Ti SUPER, 1440p, target 120 fps sustained)

| Pass | Budget |
|---|---|
| Scene + booster + terrain | 1.2 ms |
| Plume raymarch + haze | 1.8 ms |
| Particles (≤250 k) | 1.0 ms |
| Atmosphere/sky | 0.6 ms |
| Shadows (sun cascade + plume spot) | 0.8 ms |
| Post (TRAA/bloom/etc.) | 2.2 ms |
| HUD | 0.4 ms |
| **Total** | **8.0 / 8.3 ms** |

Quality tiers Ultra/High/Medium/Fallback(WebGL2) scale raymarch steps, particle count,
shadow resolution, and disable fluid dome / motion blur first. Auto-scaler drops one tier
after 60 consecutive frames over budget; never oscillates (hysteresis 2 s). Frame stats in
the engineering overlay.

---

## 12. Audio — the other half of realism

All WebAudio, all synthesized (no samples in repo), all triggered by telemetry/EVT with
**propagation honesty**: per-source delay `d/343 s` (2.92 s/km), 1/r gain, distance
lowpass (air absorption), doppler via variable-delay slope, subtle ground-reflection comb.
The observer is the active camera. Cutting cameras crossfades propagation states (250 ms).

- **Engine:** three layers — sub-rumble (20–60 Hz), roar (filtered brown noise, low-
  frequency-dominant per launch-acoustics spectra), and **crackle**: a Poisson impulse
  train of sharp-compression/slow-rarefaction transients (rate ∝ throttle, sharpened when
  close), targeting positive waveform skewness 0.1–0.5 — the measured signature of rocket
  crackle **[official: Ffowcs Williams; McInerny; Gee JASA]**. Distance softens crackle
  from "tearing" to popcorn-over-rumble.
- **Sonic boom:** the landing boom is a **TRIPLE** (engine end · grid-fins+legs merged
  middle shock · top of stage — Anderson & Gee, JASA-EL 2025), total < 1 s, spacings
  ~0.12/0.18 s scaled by geometry, N-wave transients lowpassed by distance; scheduled from
  the `MACH1_CROSS` event's emission position + camera distance. At the 2 km pad camera it
  arrives as three artillery cracks before the landing rumble — get this right and people
  who've stood at a real RTLS will grin.
- **RCS:** short hiss-thump pulses on `RCS_PULSE` (mask → which pod pans where).
- **Aero:** wind-shear whistle ∝ q̄ with grid-fin flutter AM when fins articulate fast.
- **Events:** TEA-TEB pop with the green flash; leg-deploy pneumatic thunk + latch;
  touchdown: crush crunch scaled by actual stroke telemetry + structural groan (+ deck
  clang on ASDS); post-landing: venting hisses, cooling ticks.
- **Mix:** per-group buses (engine/aero/rcs/foley/ambient), compressor+limiter on master,
  −16 LUFS target, sliders in settings. Ambient bed per preset (sea, night insects, wind).
- Browser autoplay: audio context resumes on first interaction — splash screen click.

---

## 13. Validation

### 13.1 Unit & property tests (run with `--selftest`, and in CI before every milestone)

- Frame/quaternion conversion vs frozen vectors (App-C), both directions.
- Atmosphere vs US76 table points (11 km: 22 632 Pa, 216.65 K; 47 km: 110.9 Pa; ±0.1 %).
- Two-tank CoM/inertia vs closed-form at full/half/empty; İ vs finite difference.
- RK4 order check on a known ODE (error ∝ dt⁴).
- Philox KAT vectors host==device (ORRERY-style 42-check).
- Determinism: two 60 s runs, same seed → `memcmp == 0` on state history.
- Journal: record 60 s with scripted commands → replay → identical terminal hash.
- Protocol: struct sizes/offsets static_asserts + canonical packet bytes vs golden hex.

### 13.2 Physics oracles (tolerances are gates)

- Vacuum ballistic arc vs closed form: |Δr| < 1 mm over 60 s.
- Coast energy drift < 1e-6 relative over 60 s; |q|−1 < 1e-9 always.
- Terminal velocity vs analytic ρ(h)-corrected solution < 0.5 %.
- Torque-free symmetric-top precession rate vs analytic < 0.1 %.
- Hover impossibility: min-throttle TWR at dry+0.5 t > 1.25 (config sanity gate).
- Sutton-Graves & Dryden σ spot checks vs App-A formulas.

### 13.3 Monte Carlo harness

`core --headless --runs 10000 --seed 42 --scenario terminal --out runs/mc.csv`
Dispersions per scenario (App-D): IC scatter, mass ±5 %, thrust bias ±2 %, Isp ±1 %,
CoM lateral offset ±2 cm, wind severity draw, sensor mode. CSV columns fixed (App-D.3):
seed, scenario, verdict, grade, touchdown |v|/lateral/tilt, fuel margin, max q̄, peak q̇,
solver p99, failure code. `tools/mc_report.py` (polars+matplotlib): success rate with
Wilson CI, CEP, violins, failure taxonomy (fuel exhaustion / tipover / hard / off-pad /
LOC / thermal / struct), **auto-saves the 5 worst journals** for renderer replay (the
blooper reel is a debugging tool), G-FOLD fuel-optimality ratio distribution.

### 13.4 Flight recorder

`--log-full` writes every-step state+commands binary log for tools; the renderer can
replay a log through the same socket format (`core --playback log`) — the renderer cannot
tell live from replay, which is exactly the point.

### 13.5 Goldens (house convention)

`goldens/`: protocol packet hex, trajectory terminal hashes for 3 canned journals, Philox
KATs, MC baseline success rates ±CI per scenario, sm_89-pinned MPPI regression (fixed-seed
solve → control sequence hash). Golden failures block merges; re-baselining is an ADR.

---

## 14. Milestones — gates are quantitative, artifacts are mandatory

Do not start M(n+1) before M(n)'s gate is green and recorded in `RUN_STATE.md`.

| M | Scope | Gate (all must hold) | Artifacts |
|---|---|---|---|
| **M0** | Scaffold: repo layout, CMake (core), pnpm+vite (ui), tauri dev shell, test runner, ledgers, move `_events.xlsx`→`data/reference/` | `core.exe --selftest` runs (trivial pass); `pnpm dev` shows a cube; one-command builds | RUN_STATE, DECISIONS D-001 |
| **M1** | Plant + §13.1/13.2 suite | every oracle within tolerance; determinism memcmp; journal replay | test log |
| **M2** | Hoverslam headless | Terminal scenario, 1000 seeds: ≥98 % LANDED(GOOD+), touchdown |v| p95 ≤ 3 m/s; solver < 30 Brent iters p99 | mc.csv + report |
| **M3** | Socket + shell + ugly scene (capsule + plane) | 10 min stream, zero drops; interp jitter < 1 frame; conversion vectors pass in ui (vitest) | screenshot |
| **M4** | MPPI CPU (K=256) | AeroOffset ≥90 %; cost curves monotone-ish; CPU reference frozen | report |
| **M5** | MPPI CUDA | K=16384 p99 ≤ 6 ms; parity §9.5; Terminal ≥99 %, AeroOffset ≥97 % with moderate wind+gusts | goldens: MPPI hash |
| **M6** | Full envelope: entry burn, thermal/struct budgets, legs, contact, tipover, disturbances, MC taxonomy | Entry ≥90 %; 10 k MC report generated; G-FOLD median ratio ≤ 1.2; worst-5 journals replay | 10k report |
| **M7** | Cinematic core: procedural booster, plume (analytic + SRP + diamonds + green flash), dust, sky, plume-light, audio v1, HUD v1, director, replay, floating origin | 120 fps p99 @1440p Ultra on this GPU; 70 km→pad continuous shot no jitter/z-fight; boom timing ±0.1 s vs 2.92 s/km; diamonds fade by ~35 km; screenshots match §1 beats | demo captures |
| **M8** | Polish: long-exposure, photo mode, ASDS+SEA+ocean+dropout, weather, webcast layout, summary cards, soot persistence, fluid dome (optional), engine-out demos | 12-shot demo-reel checklist captured; ASDS SS4 ≥85 %; all goldens green | reel + cards |

Aesthetics deliberately land at M7/M8 — but with real gates, because both maxima are the
project (directive 10).

---

## 15. Toolchain & build (pinned to this machine, 2026-07)

| Tool | Pin | Notes |
|---|---|---|
| GPU | RTX 4070 Ti SUPER, 16 GB, **sm_89** | goldens pinned here; fat binary sm_89 + sm_90 + compute_120 PTX (house pattern) |
| CUDA | 13.1 (V13.1.80) | MSVC 2022 host; `-fmad=false` guidance kernels; no fast-math anywhere |
| MSVC | 2022 (present) | `/std:c11` core, `/fp:precise`, static runtime `/MT` (self-contained exe) |
| CMake | 4.3.3 | single preset; FetchContent nothing (core has zero deps) |
| Node / pnpm | 24.16.0 / 10.33.2 | ui + shell frontend |
| Rust | 1.96.0 | Tauri v2 shell |
| three | **0.185.1 exact** | r183+ API names (§11.1) |
| Python | 3.13 | tools/: polars, matplotlib, cvxpy+Clarabel (venv in tools/) |

Cookbook: `cmake --preset release && ctest` · `core --headless …` · `pnpm -C ui dev`
(browser dev against `core --serve`) · `pnpm tauri dev` (full shell) ·
`pnpm tauri build` (bundles sidecar). Windows pacing: `timeBeginPeriod(1)` + QPC hybrid
sleep-spin for the 500 Hz loop; never trust default 15.6 ms timer granularity.

---

## 16. Risk register (what will actually bite, and the pre-planned answer)

1. **Quaternion conversion bug** → frozen test vectors (App-C) before any visuals; M3 gate.
2. **WebGPU unavailable** (driver/RDP/VM) → automatic WebGL2 tier; backend logged in HUD.
3. **Stale three.js tutorials** → §11.1 rename table; pin 0.185.1; consult migration guide
   on any upgrade (ADR required).
4. **`@takram/three-atmosphere` vs r185 friction** → SkyMesh fallback pre-approved (§11.4).
5. **WS backpressure stalls sim** → drop-oldest + seq gaps; sim thread never blocks on IO.
6. **MSVC/nvcc flag skew breaks determinism** → flags live in one CMake preset; golden
   memcmp catches drift; `/MT` everywhere.
7. **GPU reduction nondeterminism** → fixed-topology trees, no atomics; goldens sm_89.
8. **MPPI chatter in RAW mode** → HIER default; OU noise + SGF are not optional.
9. **Rollouts integrating underground** → event-terminated rollouts (§9.2) + test.
10. **fp32 position jitter at altitude** → fp64 plant, camera-relative rendering; M7 gate.
11. **Timer granularity ruins pacing** → §15 hybrid wait; STATS sim-rate telltale.
12. **Contact stiffness explodes RK4** → dt/8 substep + bisected onset; stiffness caps in
    `constants.h` with the stability bound documented.
13. **Audio autoplay policy** → context resume on splash interaction.
14. **Scope creep in ui/** → every visual feature maps to a §1 beat or a §11/§12 spec
    line; anything else is an ADR first.

---

## 17. Anti-patterns (each silently makes it fake — check in every review)

The v0 list, kept and extended. Watch for:

- Lerping/easing position toward the pad; PD on position with no mass/inertia/torque.
- Integrating on `requestAnimationFrame` delta; frame-dt anywhere in `core/`.
- Clamping above ground instead of contact dynamics; freezing I or CoM.
- Assist/stabilization terms that engage when the optimizer struggles.
- Rollout dynamics drifting from plant dynamics (incl. actuator lags and module mask).
- Any write to vehicle state originating in `ui/` or `shell/`; any new upstream command
  that carries state.
- Unordered FP reductions or atomics in reduction paths; `Math.random`/wall-clock in core.
- Tuning cost weights until the ghost line "looks right" instead of against MC rates.
- Renderer smoothing that hides real oscillation; data pixels not derived from telemetry;
  audio/VFX triggered by animation timelines instead of EVT.
- Binary assets committed to the repo (everything procedural or tool-generated).
- Claiming optimality/speedup without the paired oracle measurement (G-FOLD, budgets).
- "Improving" physics silently — neglect list changes are ADRs.

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
by Tustin at 500 Hz (state-space form in `wind.c`; verify PSD vs σ² in a test).
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
write it out in `dynamics.cu` comments and test vs finite difference).

### A.5 Ignition ramp

`T(t_since_start)/T_cmd = smoothstep(0, 1, (t−0.5)/1.0)` for t∈[0.5, 1.5] s, 0 before
**[chosen]**; shutdown `exp(−t/0.15)`. Green-flash EVT at t=0.30 s.

### A.6 Aero tables (representative; frozen per vehicle hash)

```
M:      0.0   0.6   0.9   1.1   1.5   2.0   3.0   5.0   8.0
CA:     0.85  0.88  1.10  1.40  1.25  1.10  0.95  0.92  0.90
CNα/rad:2.0   2.1   2.4   2.5   2.4   2.3   2.2   2.1   2.0
x_cp/L (from base, bare body): 0.62 + 0.04·exp(−((M−1.05)/0.3)²) − 0.02·min(α/15°,1)
Fin CNα_f/rad: 3.0 subsonic; ×0.55 dip for 0.8<M<1.2; ×0.8 for M>2   (per fin, S_f=2.4 m²)
```

### A.7 Heating

`q̇ = 1.7415e-4 · √(ρ/R_n) · V³  [W/m², SI]`, R_n = 1.83 m; `Q = ∫q̇ dt` drives soot.

### A.8 Slosh (module SLOSH, default ON)

Per tank: planar pendulum, mass `m_s = 0.20·m_tank`, length from first-mode fit
`ω_s² = (g_eff·1.84/R)·tanh(1.84·h_i/R)`, damping ζ=0.05 (baffled) **[chosen]**.
Pendulum reaction force applied at tank centroid; excitation from body accel. States in
§6.1. Rollouts include it when the module mask says so (directive 7).

## Appendix B — Struct discipline

Every protocol struct: `#pragma pack(push,1)`, explicit `_pad` fields, little-endian,
`static_assert(sizeof(TLM) == …)`. Canonical byte layouts frozen as hex goldens. The
TypeScript decoder is generated by `tools/gen_protocol_ts.py` from `protocol.h` — never
hand-edit the TS mirror.

## Appendix C — Frame-conversion test vectors (freeze these; ui and core both test them)

```
v_sim (1,0,0) → v_three (1,0,0)      v_sim (0,1,0) → (0,0,−1)      v_sim (0,0,1) → (0,1,0)
q_sim = (0,0,0.7071068,0.7071068)  [90° about sim-Z]  → q_three = (0,0.7071068,0,0.7071068)
Check: rotate (1,0,0) by q in sim → (0,1,0); convert → (0,0,−1);
       convert q, rotate converted (1,0,0) → must equal (0,0,−1) to 1e-6.
q_sim = (0.7071068,0,0,0.7071068)  [90° about sim-X] → q_three = (0.7071068,0,0,0.7071068)
```

## Appendix D — Scenario library (IC means; dispersions in parentheses; all seeded)

| Scenario | h₀ m | v₀ m/s | lateral m | prop₀ t | attitude | extras |
|---|---|---|---|---|---|---|
| `TERMINAL` | 2 000 (±150) | −180 (±20) vert | 0 (±120) | 8 | ≤4° tilt (±) | wind light |
| `AERO_OFFSET` | 12 000 (±800) | −330 (±30) | 800 (±250) | 10 | ≤6° | wind moderate, gusts armed |
| `ENTRY` | 62 000 (±3000) | −1 500 (±150), fpa ~ −70° | 3 000 (±800) | 30 | ≤8° | 3-eng entry burn, thermal live |
| `ASDS_NIGHT` | as AERO_OFFSET | | | 10 | | SEA SS4, deck target, night |
| `CHAOS` | 9 000 | −250 | 600 | 9 | tumbling ω≤0.4 rad/s | severe wind |
| `CUSTOM` | via RESET IC struct | | | | | |

Common dispersions (all scenarios): m_dry ±2 %, thrust bias ±2 %, Isp ±1 %, CoM lateral
±2 cm, per-run wind seed. D.3 CSV columns listed in §13.3.

## Appendix E — Starting cost weights (constants.h; tune only vs MC)

`w_pos=40, w_vel=60, w_tilt=800, w_rate=200, w_fuel=0.02, crash=1e6, w_glide=5,
w_qbar=1e-3 over 60 kPa, λ0=1.0, α=0.02, OU θ=0.15, σ_throttle=0.08, σ_acc=0.6 m/s²
(HIER) / σ_gimbal=0.8°, σ_fin=3° (RAW)`.

## Appendix F — Provenance (key sources; fetch before arguing with the spec)

Merlin/F9: SpaceX Falcon User's Guide 2025; Wikipedia Merlin/Falcon 9 FT; NASA F9 data
sheet. Landing profiles: shahar603 Telemetry-Data (`data/reference/events.xlsx`, saved
locally); SpaceX press kits; Blackmore, *NAE Bridge* 2016 (accuracy goals). SRP: NASA NTRS
20170008725; AIAA 2017-5294; Marwege JS&R 2023. MPPI: Williams et al. T-RO 34(6) 2018;
RSS 2018 Tube-MPPI; SMPPI arXiv:2112.09988; log-MPPI arXiv:2203.16599; MPPI-Generic
arXiv:2409.07563. G-FOLD: Açıkmeşe & Ploen JGCD 2007; Blackmore JGCD 2010; Açıkmeşe TCST
2013; jonnyhyman/G-FOLD-Python. Atmosphere/wind: US76 NASA-TM-X-74335; MIL-F-8785C via
MathWorks Dryden pages. Heating: Sutton & Graves NASA TR R-376. Plume: Apogee #441
(Mach-disk 0.67·D·√(p0/pa)); Ashkenas & Sherman 1966; Frontiers Mech. Eng. 2023 (rarefied
smearing). Acoustics: Gee et al. JASA 150 (2021) F9 noise; Lubert/Gee/Tsutsumi JASA 151
(2022); Ffowcs Williams JFM 1975 (crackle skewness); Anderson & Gee JASA-EL 5 (2025)
**triple** boom; BYU "Boom buh-boom" (8 km SEL numbers); AIAA 2024-3188 (overpressure vs
distance). three.js: r185 releases + migration guide; webgpu examples index;
takram/three-geospatial. Tauri: v2 sidecar + CSP docs; WebView2Feedback #5072 (dual-GPU).

---

*End of canon. If reality and this document disagree, measure, then amend this document
with an ADR — never patch around it in silence.*
