# INTERPLANETARY INTEGRATION DESIGN — the `World` abstraction + the master perception→policy→plant architecture

**Author:** INTERPLANETARY / INTEGRATOR (Opus 4.8, id `rinv3b3k`) · **Date:** 2026-07-19 (post-D-016 / continuity, night) · **Status:** design + complete static trace of the world-constant surface; the master architecture that ties perception + neural policy + plant into one coherent, determinism-clean stack. **NO code changes.** Implementer-grade spec.
**Operator ask (the north star):** generalize the whole *perception-to-policy* stack from Earth-booster-landing to **other worlds** — Mars/other-planet reentry with vision-based landing-site selection (the SpaceX interplanetary raison d'être). "Describe a target on another planet and watch it land through engine-out."
**Role of this doc:** I am the INTEGRATOR. I (A) design a clean `World` parameter set that swaps Earth→Mars→generic while keeping the integrator / contact / guidance-interface invariant and determinism sacred; (B) specify the Mars vision-landing demo; (C) design cross-world domain randomization for one policy that generalizes; (D) draw the master data-flow (sensor-camera → VLM+rangefinder → localization → target estimate → NEURAL POLICY / MPPI → GuidanceCmd → control allocation → PLANT → telemetry → observer) and reconcile it with directives 2/3/5/7; (E) give the phased roadmap; (F) enumerate the honest risks.

**Lanes I integrate (anchors — both LANDED; cited from the delivered docs):**
- **`neuralpolicy` (id `tloczreb`) → `runs/neural_policy_design.md` (landed):** the LEARNED NEURAL GUIDANCE POLICY `π(legal_state)→actuator_cmd` targeting the reachability frontier (`D_phys ≈ 1107 m`; MPPI realizes ~0.70·D_phys ≈ 775 m; `runs/sandbox/ceiling.c`); **distill-from-MPPI first (S0), RL on the joint disturbance (engine-out × shear × moving-target × dispersions) second (S1-S2), multi-world third (S3, explicitly "hand to lane interplanetary")**; observation = `nav_measure`'s perturbed view (r/v/q/ω + fuel + **engine-health flag + target estimate + uncertainty**), action = `a_lat[2]`+throttle(+`engine_cmd`/`n_eng`); **determinism export as a frozen C-header fixed-order MLP = a new `GM_NEURAL` guidance mode.** This is my policy anchor; **my cross-world rung (§E M-Xworld) IS its S3.**
- **`perception` (id `tgnshagk`) → `runs/perception_design.md` (landed):** the honest target-acquisition front-end that REMOVES the truth-origin oracle (the "last cheat"). Two honest sources: **(baseline) BEACON** — the target transmits noisy GPS (the real SpaceX droneship mechanism, §8.1-legal NavState); **(general) VISION/TRN** — headless sensor-camera → Qwen3.5-9B VLM bearing → laser rangefinder range → localize → Kalman track, language-addressable (the Perseverance LVS analog). **Determinism: the VLM runs ASYNC as a PRECOMPUTE emitting a sparse acquisition TRACE the deterministic 50 Hz Kalman+guidance loop replays bit-exact** (precompute-in/telemetry-out); live vibe-instruct = a fenced `--serve-perceive` mode reusing the `target_sandbox` two-mode split, + a gate-clean trace-replay bridge. Its estimate feeds guidance via `GuidanceCmd.target_xy(+cov)`; it explicitly generalizes to my Mars vision-site-selection (§F). This is my perception anchor.

> **DESIGN + STATIC ANALYSIS ONLY. Nothing in `core/` was edited; no cmake/binary was run against the real tree.** Every constant touchpoint below is cited by **file:line** from a complete grep of `core/` + `runs/sandbox/ceiling.c`. The world-parameterization claim (§A) is established by an exhaustive trace of every `G0` / `R_EARTH` / atmosphere / heat / structural-limit read in the plant, the two predictors, and the MPPI rollout. Every injection is specified by **file + function + line-neighborhood** so a build agent implements without re-deriving.

---

## 0. Executive verdict

**The plant is one `World` struct away from Mars. The determinism doctrine that makes Earth honest is exactly the doctrine that makes a *distribution of worlds* trainable and a *learned* policy golden-able — the same "seeded frozen parameter set, precompute-in/telemetry-out" discipline generalizes, three times over, to worlds, to perception, and to learned weights.**

1. **World parameterization is a bounded, *mechanical* change with a clean invariant/variant split.** Every Earth constant the physics reads lives at a **countable set of sites** (§A.1, the full grep): gravity `g(h)` at **7 sites** (`dynamics.c:227`, the two hoverslam predictors, the four MPPI rollout points, `sim.c`'s E3 divert, `control.c`'s `a_vert_ref`), the US76 atmosphere behind **one function** `atmo_eval` (`atmosphere.c:9`) called at **~11 sites**, the heat model at `dynamics.c:275`, the structural `QBAR_MAX` gate at `sim.c:392`. What STAYS invariant is the entire *shape* of the system: the RK4 integrator, the contact spring-damper, the guidance *interface* (`GuidanceCmd`), the actuator lags, the mass model, the RNG/determinism machinery, the protocol. **A `World` is a frozen parameter set seeded like a scenario** — swapping it is `atmo_eval(world, h, &out)` + `g_of(world, h)` + a per-world heat/aero/structural block, and *nothing else moves*.

2. **The single sharpest correctness trap: `G0` wears two hats, and only one is a World property.** `G0` (9.80665) appears as (a) **local surface gravity**, which IS a World property (`dynamics.c:227` `g_h`, every `Tfull/m − G0` authority term), and (b) the **propellant-flow calibration constant** in `mdot = thrust/(Isp·g0)` (`dynamics.c:116`, `guidance_mppi.c:530`, `ceiling.c:225`) and in the derived-engine block `ENG_MDOT_100 = ENG_T_SL/(G0·ENG_ISP_SL)` (`constants.h:24`). **Case (b) is STANDARD gravity — a fixed unit-of-Isp convention (`Isp·g0` defines exhaust velocity `c`), NOT the local world's gravity.** A Merlin on Mars still has the same `c = Isp·9.80665`; its thrust and mdot do not change because Mars pulls weaker. **Conflating them (using `world.g0` in the mdot conversion) is a physics bug that would silently corrupt every rollout's fuel accounting.** The clean split: `world.g_surface` (+ `world.radius`) drives gravity; a fixed `G0_STD` (rename the current `G0`, or keep `G0` meaning *standard gravity* and introduce `world.g0` for *local*) drives Isp. This is the one non-obvious edit and it is called out at every mdot site (§A.3).

3. **Mars is not "Earth with the knobs turned" — it changes the *authority balance*, and the plant already exposes the levers to see it.** Mars: `g ≈ 3.72 m/s²` (0.38 g), surface `ρ ≈ 0.020 kg/m³` (~1.6% of Earth's 1.225), `R_gas ≈ 189 J/kg·K` (CO₂), `γ ≈ 1.29`, scale height `H ≈ 11.1 km`, speed of sound `a ≈ 240 m/s` at surface. The consequence is **structural, not cosmetic**: in ~1.6% density the grid fins and body-lift aero-divert (`dynamics.c:140-220`, the whole `qbar·Aref·CN` machinery) produce **~1.6% of the lateral authority** — the aero-descent divert story that carries Earth AERO_OFFSET **largely evaporates**, and the vehicle is **thrust-authority-dominated** the whole way down (supersonic retropropulsion, SRP). The `D_phys` reachability ceiling (`ceiling.c`) — which bakes in US76 atmosphere, `g_of(h)`, the aero tables, and the `Isp·G0` mdot — **must be recomputed per world**; on Mars the "unpowered aero divert" bound collapses and the "powered thrust-vector" bound (`(thr·T/m)·sin α_cap`, `ceiling.c:19`) dominates. **This is the honest interplanetary physics**, and it falls straight out of the parameterization: change `ρ0`/`H`/`g`, re-run `ceiling.c`, and the ceiling table *tells you* the divert budget shrank.

4. **The SAME trained-policy architecture handles it — because the policy already targets `D_phys`, and `D_phys` is just re-evaluated per world.** `neuralpolicy`'s `π` is trained against the reachability frontier, not against Earth numbers. Feed the World params as **another randomization axis** (gravity × atmosphere × disturbances × worlds), and one policy learns the *invariant* skill ("null the offset within the current authority envelope") across the family — OR you ship a per-world policy family. **Recommendation (§C): ONE cross-world policy, distilled from a per-world MPPI teacher, with the World params (or a low-dim embedding: `g`, `ρ0`, `H`, `TWR_max`) fed to the network as observed context.** The determinism export is unchanged — a frozen C-header MLP is world-agnostic; only its *inputs* carry the world.

5. **The determinism surface grows on THREE new axes, and each is fenced by the SAME doctrine already in the tree.** (i) **Worlds** — a `World` is a seeded frozen parameter set, exactly like a scenario; it is fully golden-able (per-world goldens). (ii) **Perception** — the VLM is nondeterministic and heavy, so per the `perception` lane it runs **ASYNC as a precompute** emitting a target-estimate *trace* the deterministic loop replays byte-exact (the D-011 "precompute in, telemetry out" hard line, applied to the sensor). (iii) **Learned weights** — the policy is a **frozen** C-header MLP evaluated inside the deterministic loop (a pure function, fp-ordered), so `GM_NEURAL` is golden-able exactly like `GM_HOVERSLAM`/`GM_MPPI`; training happens OUT of the gated loop. **The gated deterministic loop stays: plant EOM + control + a frozen policy + a replayed perception trace. Everything expensive/nondeterministic (VLM inference, RL training) is precompute-in/fenced.** Directives 2/3/5/7 survive intact (§D.4).

6. **Scope honesty: this is a multi-milestone north-star, not a weekend.** Ranked by value/effort (§E, §F.4): **(M-W) World parameterization** is the keystone and the cheapest high-value rung (mechanical, gate-clean, unlocks everything) — do it first. **(M-Mars) the Mars plant** is a new `World` + a re-tuned/re-trained descent (the aero story changes) — medium effort, huge narrative value. **Perception** and **neural policy** are the two lanes landing in parallel — large but independently shippable. **Cross-world policy** and **the full showcase** are the summit — highest effort, gated on all the rest. The honest framing: **World parameterization is a real, bounded, this-quarter deliverable; the full "watch it land on Mars through engine-out via a described target" is the north star the rungs climb toward.**

**Recommended build order (each rung independently shippable + gated):** `World` abstraction (Earth-identical golden first) → Mars `World` + re-solve → perception acquisition (Earth) → neural policy (Earth, `GM_NEURAL`) → cross-world policy (the randomization axis) → the full Mars-vision-through-engine-out showcase. Dependencies + consumed artifacts in §E.

---

## 1. Where the plant is Earth today — the complete world-constant surface

Read directly from `core/constants.h`, `core/atmosphere.c`, `core/dynamics.c`, `core/sim.c`, `core/control.c`, `core/guidance_hoverslam.c`, `core/guidance_mppi.c`, and `runs/sandbox/ceiling.c`. This is the exhaustive grep, so a build agent knows *exactly* what abstracts and *exactly* what does not move.

### 1.1 The Earth constants, and their physical role

`constants.h:7-13` — the "World / environment" block:
```c
#define G0            9.80665         /* m/s^2 [official] */          // constants.h:8   DUAL ROLE (gravity + Isp)
#define R_EARTH       6356766.0       /* m, geopotential radius */    // constants.h:9   gravity falloff + geopotential
#define R_AIR         287.053         /* J/(kg K) [official] */       // constants.h:10  gas constant (Earth air)
#define GAMMA_AIR     1.4                                             // constants.h:11  ratio of specific heats
#define RHO0          1.225           /* kg/m^3 sea level */          // constants.h:12  (declared; US76 uses P_b[0] internally)
#define P0_ATM        101325.0        /* Pa [official] */             // constants.h:13  Isp p-correction ref + US76 base
```
Plus the derived engine block (`constants.h:24-26`) and the failure lines (`constants.h:97-100`):
```c
#define ENG_MDOT_100  (ENG_T_SL/(G0*ENG_ISP_SL))     // constants.h:24  <-- G0 as STANDARD gravity (Isp)
#define ENG_T_VAC     (ENG_MDOT_100*G0*ENG_ISP_VAC)  // constants.h:25  <-- G0 as STANDARD gravity (Isp)
#define ENG_AE        ((ENG_T_VAC-ENG_T_SL)/P0_ATM)  // constants.h:26  <-- P0_ATM as Earth-SL calibration
#define QBAR_MAX      80000.0         /* Pa [estimate] */             // constants.h:97  structural (dynamic pressure)
#define HEAT_RN       1.83            /* m nose radius */             // constants.h:98
#define HEAT_K        1.7415e-4       /* Sutton-Graves Earth */       // constants.h:99  <-- Earth-air heat constant
#define HEAT_QDOT_MAX 300000.0        /* W/m^2 sustained */           // constants.h:100 thermal limit
```

**The role taxonomy — this is the design's spine:**

| Constant | Physical role | World-dependent? | Notes |
|---|---|---|---|
| `G0` (as `g_h`) | local surface gravity | **YES → `world.g0`** | Mars 3.72; drives gravity + all `Tfull/m − g` authority |
| `G0` (as `Isp·g0`) | Isp→exhaust-velocity unit | **NO → `G0_STD` fixed** | standard gravity; exhaust `c` is engine physics, not local g |
| `R_EARTH` | gravity falloff `(R/(R+h))²` + geopotential | **YES → `world.radius`** | Mars 3389.5 km; matters for high-entry `g(h)` and atmo geopotential |
| `R_AIR` (287) | specific gas constant | **YES → `world.R_gas`** | Mars CO₂ ≈ 189 J/kg·K |
| `GAMMA_AIR` (1.4) | ratio of specific heats | **YES → `world.gamma`** | Mars CO₂ ≈ 1.29 (affects speed of sound + could affect aero, kept table-frozen §A.5) |
| `P0_ATM` (as Isp p-correction) | ambient-pressure reference for thrust/Isp | **YES → `world.p_ref`** | the `(T_vac − AE·p)` throttle-up and `Isp(p)` blend need the *local* ambient scale |
| `P0_ATM` (as `ENG_AE` calibration) | Earth-SL nozzle-area derivation | **borderline** | `ENG_AE` is a *nozzle geometry* (m²) derived once from Earth-SL vs vac; it is engine hardware → keep fixed (§A.3) |
| atmosphere (US76 tables) | ρ(h), p(h), T(h), a(h) | **YES → `world.atmo`** | the whole `atmo_eval` model swaps (§A.4) |
| `HEAT_K` (Sutton-Graves) | stagnation heat-rate constant | **YES → `world.heat_k`** | gas-composition-dependent (Sutton-Graves `k ∝ 1/√(effective R)`) |
| `QBAR_MAX`, `HEAT_QDOT_MAX` | structural / thermal limits | **vehicle, not world** | but the *regime* that reaches them is world-set (Mars entry heat pulse differs) |
| RK4, contact, actuators, mass, RNG | integrator + vehicle + determinism | **NO — invariant** | the fixed shape of the system |

### 1.2 The gravity surface — every `g(h)` site (7 sites)

`g_h = G0·(R_EARTH/(R_EARTH+h))²` appears **verbatim** at:
- **Plant:** `dynamics.c:227` — `double g_h = G0*(R_EARTH/(R_EARTH+h))*(R_EARTH/(R_EARTH+h));` → `a[2] = Fw[2]/m − g_h` (`:228`); also `diag->twr = thrust/(m*g_h)` (`:284`).
- **Hoverslam predictor (suicide-burn margin):** `guidance_hoverslam.c:28` and `:71` (the ballistic shoot uses `gh = G0·(...)²`).
- **Landing-burn authority:** `guidance_hoverslam.c:115` — `a_max_now = Tfull/m − G0` (surface-g approximation near the pad); `:243` — `T_need = m*(G0 + a_cmd) − D`.
- **MPPI rollout:** `guidance_mppi.c:170`, `:246` (`gh = G0·(...)²` in the two predictor mirrors), `:284` (`a_design = 0.85*(Tfull/m − G0)`), `:291` (`T_need = m*(G0+a_cmd) − D`), `:528` (`aeff = T/m − G0`), `:583` (`amax = Tf/m − G0`).
- **E3 entry supervisor (`sim.c`):** `:154` (`gh = G0·(...)²` for the divert authority `a_burn`), `:279`, `:314` (`amax = Tf/m − G0`).
- **Control:** `control.c:84` — `a_vert_ref = G0 + 2.0` (the tilt-authority reference the aero divert maps against; `ceiling.c:20,265` note this is what `control.c` actually caps to).
- **Verdict/misc:** `main.c:92` (selftest g at 70 km), `main.c:137` (`twr = T/(m*G0)`).

**Every one of these is `world.g0` (local, for the `−g` authority terms) or `g_of(world, h)` (the falloff form).** None uses `G0` as an Isp unit — those are separate (§1.3). This is the clean separation that makes the swap safe.

### 1.3 The propellant-flow / Isp surface — where `G0` is STANDARD gravity (do NOT swap)

- `dynamics.c:115` — `Isp = iscale*(ENG_ISP_VAC − (ENG_ISP_VAC−ENG_ISP_SL)*(atm.p/P0_ATM))` — the **`P0_ATM` here IS world-dependent** (it is the *ambient* pressure scale the Isp blends against; on Mars near-vacuum, `atm.p/p_ref` ≈ 0 so Isp ≈ vac everywhere) → `world.p_ref`.
- `dynamics.c:116` — `mdot_total = thrust/(Isp*G0)` — **`G0` here is STANDARD gravity** (`Isp·g0 = c`, exhaust velocity). **KEEP `G0_STD`.**
- `guidance_mppi.c:530` — `mdot = T/(ENG_ISP_SL*G0)` — same, `G0_STD`.
- `ceiling.c:59-60,225` — `ENG_MDOT_100 = ENG_T_SL/(G0*ENG_ISP_SL)`, `mdot = Tact/(Isp*G0)` — same, `G0_STD`.
- `constants.h:24-26` — the derived-engine consistency triple: `ENG_MDOT_100`, `ENG_T_VAC`, `ENG_AE`. **All three use `G0` as standard gravity and `P0_ATM` as Earth-SL calibration; they are ENGINE HARDWARE properties (the Merlin's exhaust velocity and nozzle area), not world properties. KEEP THEM FIXED.** (A Merlin's `T_vac` and `AE` are the same bell on Mars.)

**The consequence for the pressure-corrected thrust** (`dynamics.c:43-47`, `engine_thrust`): `T = ENG_T_VAC − ENG_AE·p_amb`. `ENG_T_VAC` and `ENG_AE` stay fixed (hardware); `p_amb` comes from `world.atmo`. On Mars `p_amb ≈ 600 Pa` (vs 101325), so `T ≈ T_vac` (the engine is essentially always at vacuum thrust) — **this falls out correctly with zero engine-constant changes**, purely from the atmosphere swap. This is the parameterization working as designed: the engine is hardware, the world is the environment it flies in.

### 1.4 The atmosphere surface — one function, ~11 call sites

`atmo_eval(double h, AtmoOut* o)` (`atmosphere.c:9`) is the **single choke point** for ρ, p, T, a. It is `BL_HD` (host+device, for the CUDA rollout). Called at: `dynamics.c:103`, `guidance_hoverslam.c:25,69,92`, `guidance_mppi.c:168,244,270,526,582`, `control.c:37`, `sim.c:153,183,276,311`, plus the selftest `main.c:41-48`. **Because it is one function, the atmosphere swaps in ONE place** (§A.4) — every consumer inherits it. The US76 internals (`atmosphere.c:10-13`, the `H_b/T_b/L_b/P_b` layer tables) are Earth-specific and use `R_EARTH`, `G0`, `R_AIR`, `GAMMA_AIR` (`:16,27,29,32,33`).

### 1.5 The heat + structural surface

- **Heat:** `dynamics.c:275` — `qdot_heat = HEAT_K*sqrt(atm.rho/HEAT_RN)*speed³` (Sutton-Graves). `HEAT_K` is gas-composition-dependent (Mars CO₂ has a different constant; classic Sutton-Graves `k_earth ≈ 1.7415e-4`, `k_mars ≈ 1.9e-4`-ish depending on formulation) → `world.heat_k`. `HEAT_RN` is the vehicle nose radius (fixed). The integrated load `Q_heat` (`state.h`) and the thermal sink `HEAT_QDOT_MAX` are vehicle limits (fixed), but the *heat pulse the world produces* differs.
- **Structural:** `sim.c:392` — `qbar > QBAR_MAX` sustained > 2 s → `F_STRUCT`. `QBAR_MAX` is a vehicle limit (fixed); but on Mars the vehicle rarely reaches 80 kPa (thin air), so this gate seldom fires — again, correct behavior from the atmosphere swap, no gate change.

### 1.6 The winds surface — already partly abstracted, needs world-scaling

`sim.c:31-77` `wind_sample`: mean profile (`u_ref` power-law `(h/10)^0.14`, altitude scaling) + Dryden AR(1) turbulence (`w20_kt`) + DIAL-A-GUST. Set per-scenario in `scenario.c` (`u_ref`, `w20_kt`, `wind_az`). **The wind is data (a `ScenarioEnv`), not hardcoded Earth physics** — but the *model form* (the `0.14` power-law exponent, the Dryden length scales `Lu` at `sim.c:46`, the kt→m/s conversion) is Earth-atmosphere-shaped. **Mars winds are different in kind:** thinner air means the same wind speed exerts ~1.6% of the force, but Mars has strong thermally-driven winds + **dust** (a density/optical perturbation with no Earth analog). §A.6 handles this: the wind *model* becomes a `world.wind` descriptor (profile exponent, turbulence scale, + an optional Mars dust-density-perturbation term), and the *severity* stays a `ScenarioEnv` (seeded).

---

## 2. The determinism / doctrine spine this design must respect

Stated from the source (identical spine to `target_sandbox_design.md` §2 and `engineout_design.md` §0 — this doc extends it to worlds + perception + learned weights):
- **Directive 2 (determinism sacred):** fixed dt=2 ms, seeded Philox, no unordered FP reductions, bit-identical replay (`--selftest` memcmp oracle). `HANDOFF_2026-07-18_NIGHT.md:50-52`.
- **Directive 3 (if guidance can't solve it, it crashes):** no assist terms, no clamps toward the pad. `HANDOFF:52`. **On Mars this is sharper** — the aero divert is gone, so an offset the thrust budget can't cover is an honest crash (§B.4).
- **Directive 5 (pure observer):** precompute in, telemetry out, NEVER a runtime loop from the pretty half into dynamics (D-011 hard line, `DECISIONS.md:645-648`). **This is the doctrine that fences perception + learned weights** (§D.4).
- **Directive 7 (one dynamics source):** plant, predictors, MPPI rollouts share the same EOM *including behavior changes*. `HANDOFF:54-59`. **A World must reach ALL of them identically** — the `atmo_eval`/`g_of` swap must be visible to the rollout copy or the planner desyncs from the plant (§A.7, the analog of the D-012 leak-catch).
- **Directive 9 (TERMINAL byte-identical):** `HANDOFF:76`. **The keystone gate for World: `World::Earth` must reproduce every current golden byte-exact** (§A.8).

**The three new determinism axes and how each maps onto the existing doctrine:**

| new axis | nondeterministic/heavy? | where it lives | how it's fenced | golden-able? |
|---|---|---|---|---|
| **World** (gravity × atmo × wind) | no — a frozen param set | inside the plant (like a scenario) | seeded, no runtime input | **YES** — per-world goldens |
| **Perception** (VLM bearing) | **yes** — heavy, nondeterministic | ASYNC precompute → target-estimate **trace** | precompute-in/telemetry-out (D-011); the loop replays the trace | **YES** — the trace is the frozen input; live drag = fenced (Mode 2) |
| **Neural policy** (learned π) | training: yes; inference: **no** | **frozen C-header MLP** in the gated loop | train OUT of loop; ship frozen weights; pure fp-ordered eval | **YES** — `GM_NEURAL` golden like `GM_HOVERSLAM` |

**The unifying principle:** *the gated deterministic loop is always {plant EOM + control + a pure-function guidance + replayed exogenous traces}.* Worlds are exogenous params; perception is an exogenous trace; the learned policy is a pure function. Nothing expensive or stochastic runs *inside* the gate. This is the D-011 precompute-in doctrine, generalized.

---

## A. WORLD PARAMETERIZATION — the clean abstraction

### A.1 The `World` struct (the frozen parameter set)

**New `core/world.{h,c}`** (a peer of `scenario.{h,c}`). A `World` is to *physics constants* what a `ScenarioEnv` is to *initial conditions* — a seeded, frozen descriptor. Proposed interface:

```c
// world.h  — the planetary parameter set. A World is FROZEN (const after selection), seeded like a
// scenario, and reaches the plant + BOTH predictors + the MPPI rollout identically (directive 7).
#ifndef BL_WORLD_H
#define BL_WORLD_H
#include "vmath.h"

// Atmosphere descriptor: enough to reproduce ISA-style piecewise layers OR an isothermal
// exponential (Mars is well-approximated by a single-scale-height exponential + a lapse near
// surface). A tagged union keeps Earth's US76 exact while letting Mars be a clean exponential.
typedef enum { ATMO_LAYERED_ISA = 0, ATMO_EXPONENTIAL = 1, ATMO_VACUUM = 2 } AtmoModel;
typedef struct {
    AtmoModel model;
    double R_gas;      // specific gas constant [J/kg/K]  (Earth air 287.053; Mars CO2 189)
    double gamma;      // ratio of specific heats          (Earth 1.4; Mars 1.29)
    // LAYERED_ISA (Earth): the US76 tables (base H', T, lapse, P) — carried so Earth stays EXACT
    int    n_layers;   double H_b[8], T_b[8], L_b[7], P_b[8];   // geopotential-km layer table
    // EXPONENTIAL (Mars/generic): rho(h) = rho0 * exp(-h/H_scale); T from a simple lapse; p = rho*R*T
    double rho0;       // surface density [kg/m^3]         (Earth 1.225; Mars ~0.020)
    double H_scale;    // density scale height [m]          (Earth ~8500 near-surface; Mars ~11100)
    double T0;         // surface temperature [K]           (Earth 288.15; Mars ~210)
    double T_lapse;    // near-surface lapse [K/m]          (small; Mars ~ -0.0025)
    double p_ref;      // ambient-pressure reference for Isp/thrust p-correction [Pa]
                       //   (Earth 101325 sea-level; Mars ~610 surface) — NOT the Isp unit (that's G0_STD)
} WorldAtmo;

// Wind descriptor (the model form; severity stays in ScenarioEnv, seeded).
typedef struct {
    double profile_exp;    // mean-wind power-law exponent  (Earth 0.14; Mars different/near-0 aloft)
    double turb_scale;     // turbulence intensity scale     (multiplies the Dryden severity)
    double dust_tau;       // OPTIONAL Mars dust: fractional density perturbation amplitude (0 = off)
} WorldWind;

typedef struct {
    const char* name;      // "earth" | "mars" | "generic-..."
    double g0;             // LOCAL surface gravity [m/s^2]  (Earth 9.80665; Mars 3.7211)  <-- gravity role
    double radius;         // planetary radius [m] for g(h) falloff + geopotential (Earth 6356766; Mars 3389500)
    WorldAtmo atmo;
    WorldWind wind;
    double heat_k;         // Sutton-Graves stagnation-heat constant (gas-composition dependent)
    // NOTE: G0_STD (standard gravity, the Isp unit) is a FIXED module constant, NOT here — it is
    // engine hardware, world-invariant. Same for ENG_T_VAC / ENG_AE (nozzle geometry).
} World;

// The two built-in worlds (frozen tables). Earth's atmo carries the exact US76 layer arrays so
// World::earth reproduces every current golden byte-for-byte (directive 9).
extern const World WORLD_EARTH;
extern const World WORLD_MARS;

// Pure, BL_HD (host+device for the rollout): local gravity with falloff, and the atmosphere.
BL_HD double world_gravity(const World* w, double h);            // g(h) = g0*(R/(R+h))^2
BL_HD void   world_atmo(const World* w, double h, AtmoOut* o);   // dispatch by w->atmo.model
#endif
```

**`AtmoOut` is unchanged** (`atmosphere.h:7-12`, `{T,p,rho,a}`) — the *interface* every consumer already speaks stays identical; only the *evaluator* becomes world-parameterized. This is why the swap is mechanical.

### A.2 What changes in `atmosphere.c` — `atmo_eval(h)` → `world_atmo(w, h)`

`atmosphere.c:9-34` becomes a dispatcher:
```c
BL_HD void world_atmo(const World* w, double h, AtmoOut* o){
    if(h < 0.0) h = 0.0;
    switch(w->atmo.model){
    case ATMO_LAYERED_ISA: {
        // EXACTLY the current US76 body, but reading w->atmo.{R_gas,gamma,H_b,T_b,L_b,P_b} and
        // w->g0/w->radius instead of the #defines. For WORLD_EARTH these tables == the current
        // literals (atmosphere.c:10-13) and w->g0==9.80665, w->radius==6356766 → BYTE-IDENTICAL.
        double Hp = (w->radius*h)/(w->radius+h)/1000.0; if(Hp>84.852) Hp=84.852;
        /* ...current layer walk, with G0→w->g0, R_AIR→w->atmo.R_gas, GAMMA_AIR→w->atmo.gamma... */
    } break;
    case ATMO_EXPONENTIAL: {
        // Mars/generic: rho = rho0*exp(-h/H); T = T0 + lapse*h (floored); p = rho*R*T; a = sqrt(gamma*R*T)
        double T = w->atmo.T0 + w->atmo.T_lapse*h; if(T<1.0) T=1.0;
        double rho = w->atmo.rho0 * exp(-h / w->atmo.H_scale);
        double p = rho * w->atmo.R_gas * T;
        o->T=T; o->p=p; o->rho=rho; o->a=sqrt(w->atmo.gamma * w->atmo.R_gas * T);
    } break;
    case ATMO_VACUUM: { o->T=w->atmo.T0; o->p=0; o->rho=0; o->a=1.0; } break;
    }
}
```
**The `AtmoOut` contract, the `BL_HD` qualifier, and the CUDA function-local-static discipline (`atmosphere.c:7-8`) are preserved.** Earth path is bit-identical; Mars path is a clean 5-line exponential (Mars's atmosphere is famously well-modeled by a single scale height — this is *more* faithful for Mars than forcing it through ISA layers).

**Determinism note (the exponential + fp):** `exp()` and `pow()` must be the same library calls the rollout and plant both use (they already are — `<math.h>`, `-fmad=false` on CUDA per the M5 port). No new nondeterminism: `world_atmo` is a pure function of `(w, h)`, seeded nowhere, called identically host/device. The Earth ISA path keeps the exact `pow(T/Tb, -g0/(R·Lm))` form.

### A.3 What changes in `dynamics.c` — gravity + the Isp/mdot split (the correctness-critical edit)

Thread a `const World* w` into `dynamics_deriv` (via `EnvCtx`, §A.7). Then:
- **Gravity (`dynamics.c:227`):** `double g_h = world_gravity(w, h);` — replaces the `G0*(R_EARTH/(R_EARTH+h))²` literal. `world_gravity` returns exactly that form with `w->g0`, `w->radius` → Earth byte-identical.
- **Atmosphere (`dynamics.c:103`):** `world_atmo(w, h, &atm);` — replaces `atmo_eval(h,&atm)`.
- **Isp p-correction (`dynamics.c:115`):** `Isp = iscale*(ENG_ISP_VAC − (ENG_ISP_VAC−ENG_ISP_SL)*(atm.p / w->atmo.p_ref));` — `P0_ATM → w->atmo.p_ref`. **This is a world edit** (the ambient scale the Isp interpolates against).
- **mdot (`dynamics.c:116`) — DO NOT TOUCH `G0`:** `mdot_total = thrust/(Isp*G0_STD);` — keep standard gravity (rename `G0`→`G0_STD` at this site to make the intent unmistakable, or add a comment `/* G0 here == STANDARD gravity (Isp unit), NOT world gravity */`). **This is the single sharpest trap in the whole port (§0.2).**
- **Heat (`dynamics.c:275`):** `qdot_heat = w->heat_k*sqrt(atm.rho/HEAT_RN)*speed³;` — `HEAT_K → w->heat_k`.
- **`diag->twr` (`:284`):** `thrust/(m*g_h)` — uses the world gravity (correct; TWR against local g).

**`engine_thrust` (`dynamics.c:43-47`) needs NO change:** `T = ENG_T_VAC − ENG_AE*p_amb` uses fixed hardware constants and the world's `p_amb` (from `atm.p`). Correct on any world.

### A.4 What STAYS invariant (the load-bearing "does not move" list)

This is as important as the change list — it is the promise that the swap is bounded:
- **The integrator** (`integrator.c`, RK4 + the substep gimbal/fin lags) — untouched. Fixed dt=2 ms.
- **The contact model** (`contact.c`, spring-damper legs, crush, friction `LEG_MU_*`) — untouched. Landing on Mars regolith uses the same legs (a future `world.surface_mu` is a *nice-to-have*, not required; §F).
- **The mass model** (`dynamics.c:56-86` `mass_props`, the two-column propellant, inertia, analytic `Idot`) — untouched (vehicle, not world).
- **The actuator dynamics** (gimbal 2nd-order, throttle lag `ENG_THR_TAU`, fin rates, ignition ramp) — untouched (engine hardware).
- **The guidance INTERFACE** (`guidance.h`, `GuidanceCmd`) — untouched. Guidance still outputs `{throttle, a_lat, engine_cmd, ...}`. **The World changes what those commands *achieve*, not the command vocabulary.** This is why a policy trained on one world speaks the same language on another.
- **The RNG / determinism machinery** (`rng.h` Philox, the seeded streams) — untouched.
- **The protocol** (`protocol.h`) — untouched by the World *math*. (An OPTIONAL HELLO field `world_id` + `g0`/`rho0` lets the renderer label the scene "MARS" and scale its atmosphere haze — a pure-observer read, a small v-bump if wanted; §D.3.)
- **The engine consistency triple** `ENG_MDOT_100/T_VAC/AE` (`constants.h:24-26`) — fixed hardware.

### A.5 The aero coefficients — a subtle "mostly invariant" call

`dynamics.c:92-94` — the frozen aero tables `AERO_M/CA/CN` (Mach-indexed axial + normal-force coefficients) and `xcp_frac` / `fin_dip`. **These are DIMENSIONLESS coefficients vs Mach — they are a property of the vehicle SHAPE, not the world.** A cylinder at Mach 2 has ~the same `CA` on Earth or Mars *at the same Mach*. **So the aero tables stay invariant** — and this is physically right. What changes is the *force*: `F = qbar·Aref·C`, and `qbar = ½ρv²` collapses with Mars's density. **Second-order caveat (state it, defer it):** the tables were fit for Earth-air `γ=1.4`; Mars CO₂ `γ=1.29` shifts compressibility (the transonic `CA` peak, the shock structure) slightly. For the honest-but-tractable v1, **keep the tables frozen and note the `γ` caveat in the ADR** (the density collapse dwarfs the `γ` correction — the fins produce 1.6% force either way). A `world.aero_gamma_correction` is a §F polish, not a v1 need.

### A.6 The winds — `WorldWind` + the Mars dust option

`sim.c:31-77` `wind_sample` reads `world.wind`:
- **Mean profile:** the `pow(hh/10.0, 0.14)` exponent (`sim.c:36`) → `w->wind.profile_exp`. Mars boundary layer is different (thermally driven, different profile); a build agent sets a Mars-appropriate exponent (or near-0 with a stronger aloft term).
- **Turbulence:** the Dryden block (`sim.c:42-56`) multiplies severity by `w->wind.turb_scale`. The Dryden *length scales* (`Lu`, `sim.c:46`) are Earth-atmosphere fits; a rigorous Mars turbulence model is out of scope, so **scale the existing Dryden by `turb_scale` and note the model-fidelity caveat** — the point is *a* replayable turbulence, seeded, not a Mars-validated spectrum.
- **DUST (the Mars-only term):** Mars dust is not a wind — it is a **density/optical perturbation**. The honest minimal model: a seeded, altitude-banded fractional density bump `ρ ← ρ·(1 + dust_tau·f(h, seed))` applied *inside* `world_atmo` (or as an `EnvCtx` density scale, mirroring `thrust_scale`). It perturbs qbar/drag/heat (real) and — crucially — the **optical** channel the VLM sees (a dust storm degrades landing-site imaging; §B.3, §D). `dust_tau=0` (Earth, or clear Mars) → byte-identical. **This is the first genuinely-new-physics term the interplanetary work adds, and it is a seeded scalar — gate-clean.**

### A.7 Directive-7: the World must reach the rollout identically (the leak-catch analog)

This is the interplanetary version of the D-012 MPPI-leak discipline (`HANDOFF:54-59`). The MPPI rollout and the two predictors each independently call `atmo_eval`/compute `g_h` (§1.2, §1.4). **All of them must switch to `world_atmo(w,·)` / `world_gravity(w,·)` reading the SAME `World`** — otherwise the planner solves Earth while the plant flies Mars, and the guidance is systematically wrong (the exact failure mode directive 7 exists to prevent).

**The clean plumbing:** add `const World* world;` to `EnvCtx` (`dynamics.h:20-29`), set once in `sim_init` from the selected world, and **the MPPI rollout already copies `EnvCtx` verbatim** (`EnvCtx env = *env0;`, `guidance_mppi.c:350,555` — the same mechanism `engineout_design.md` §C.4 relies on for `thrust_offset`). So the pointer rides into every rollout for free; every `world_atmo`/`world_gravity` inside `dynamics_deriv` (called by the rollout's `rk4_step`) sees the right world automatically. The **hoverslam predictors** (`guidance_hoverslam.c:20-35,65-75`) and the **MPPI's own predictor mirrors** (`guidance_mppi.c:160-180,240-250`) must be passed the `World*` explicitly (they compute `gh`/`atm` locally). **Verification (mandatory):** the single-run MPPI invariance check with `World::Earth` must be **byte-identical** to the current `(1.5,3)` reference line (`td_v 2.63 / lat 10.48`, continuity §0) — proving the World plumbing leaked nothing when it's Earth.

### A.8 The determinism gate — `World::Earth` reproduces every golden

**The keystone acceptance test (directive 9):** with `World::Earth` (the default, and the only world any current CLI selects), **EVERY baseline reproduces byte-exact**, because `world_gravity(EARTH,h)` folds to the identical `G0*(R_EARTH/(R_EARTH+h))²` and the Earth ISA `world_atmo` folds to the identical US76:
- TERMINAL `--headless --seed 42 --runs 200` = **194/200 byte-exact** (the sacred parity gate).
- ENTRY `--headless --seed 42 --runs 100 --mppi` = **95/100** (D-016).
- AERO `--headless --seed 42 --runs 60 --mppi` = **44/60**.
- MPPI single-run invariance = the `(1.5,3)` reference line, byte-identical.
- `--selftest` = PASS (the US76 table-point assertions, `main.c:41-48`, now go through `world_atmo(EARTH,·)` and must still hit the same ρ/p/T/a).
- Determinism pair on each.
**If any moves, the parameterization is not algebraically neutral (a literal became a slightly-different float path, or an Isp site wrongly used world-g) — fix before Mars.** This is the same "prove byte-equality when the new thing is inert" gate `target_sandbox` §Stage-0 and `engineout` §S0 use, applied to worlds. **Do this rung FIRST and ship it before touching Mars** — an Earth-identical `World` refactor is a safe, high-value, independently-gated deliverable.

---

## B. THE MARS DEMO — entry from orbit into thin CO₂, vision-picked site, SRP descent

### B.1 The Mars `World` (the frozen table)

```c
const World WORLD_MARS = {
    .name="mars", .g0=3.7211, .radius=3389500.0,
    .atmo = { .model=ATMO_EXPONENTIAL, .R_gas=188.92, .gamma=1.29,
              .rho0=0.020, .H_scale=11100.0, .T0=210.0, .T_lapse=-0.0025, .p_ref=610.0 },
    .wind = { .profile_exp=0.10, .turb_scale=1.0, .dust_tau=0.0 /* arm per-scenario */ },
    .heat_k=1.90e-4  /* Sutton-Graves CO2 (representative; tag [estimate]) */
};
```
All values tagged `[official]` (g0, radius, R_gas, gamma, H_scale) or `[estimate/chosen]` (heat_k, wind, T_lapse) in the canon-style provenance comments. This is a **frozen parameter set** — seeded, golden-able, replayable, exactly like a scenario.

### B.2 What is genuinely different on Mars (the honest physics, from the parameterization)

Run `ceiling.c` (§B.5) with `WORLD_MARS` and the numbers *tell the story*, but the shape is predictable from the params:

1. **Aero authority collapses (~1.6% of Earth).** `qbar = ½·ρ·v²` with `ρ_mars ≈ 0.020` vs `1.225`. The grid-fin + body-lift divert (`dynamics.c:140-220`) — the mechanism that carries Earth AERO_OFFSET's ~1107 m `D_phys` — produces **~60× less lateral force**. **The unpowered aero-divert phase essentially disappears as a control authority** (it still slows the vehicle over the long descent — drag integrated over altitude is real — but you cannot *steer* meaningfully with fins in 1.6% air). **The fins become primarily stabilization/trim, not divert actuators.** The `AERO_OFFSET`-style "glide to the pad" story is Earth-specific; Mars is **thrust-vector divert or nothing.**

2. **Supersonic retropropulsion (SRP) is the descent.** With no aero brake, the vehicle enters fast and stays fast; it must **light the engine while still supersonic** to decelerate (exactly Perseverance/Starship-Mars SRP). The plant already models SRP shielding (`dynamics.c:155-159`, the `srp_shield` plume-envelopes-aero blend) — **on Mars this is the *dominant* regime, not a landing-burn detail.** The ignition logic (`guidance_hoverslam.c` suicide-burn margin, the E3 supervisor) will ignite *earlier and higher* because the ballistic no-thrust shoot (`suicide_burn_margin`, `ceiling.c:117`) finds the ground arriving fast with no aero help. **This is a re-tune/re-train, not a re-architecture** — the *mechanism* (aero-aware ignition margin) is right; its *numbers* shift because the world shifted.

3. **Lower gravity helps the arrest, hurts the entry.** `g_mars = 3.72` vs `9.81`: the `Tfull/m − g` authority terms (`guidance_hoverslam.c:115`, everywhere in §1.2) get **more decel headroom** (less gravity to fight) — a given engine arrests a given velocity in *less* propellant. But the vehicle also enters from orbit faster relative to the thin atmosphere's braking, so the **total Δv the engine must supply is larger** (the atmosphere does less of the work). Net: **the burn is longer and starts higher, but each second of burn is more effective.** The `ceiling.c` `D_phys` will show the powered-divert bound *grew* relative to Earth's (more thrust headroom) even as the aero bound vanished.

4. **The divert ceiling `D_phys` is world-dependent and MUST be recomputed.** `ceiling.c` bakes in US76 (`:70-88`), `g_of(h)` with `R_EARTH` (`:113`), the aero tables, and `Isp·G0` (`:225`). **Parameterize `ceiling.c` by `World` (it already mirrors `core/` verbatim — mirror the `World` too) and re-run per world.** The Mars ceiling table will show: unpowered-aero bound → ~0; powered-thrust bound → dominant and larger (more `Tfull/m − g` headroom). **This recomputed ceiling is what the neural policy trains against on Mars** (§C) — the policy targets `D_phys(world)`, so it automatically re-aims at the Mars-appropriate frontier.

5. **The heat pulse differs.** Mars entry heating is real (Perseverance's heatshield) but the *pulse shape* differs (thinner air, different gas). `world.heat_k` + the density profile drive `qdot_heat` (`dynamics.c:275`); the vehicle's `HEAT_QDOT_MAX` sink is unchanged, so the question "does the entry cook the vehicle?" gets a per-world answer for free.

### B.3 Vision-based hazard-avoidance landing-site selection (the Perseverance TRN analog)

**This is where the `perception` lane meets the Mars world.** Per `perception`'s two-source design, the honest Mars staging mirrors its Earth staging: **(baseline) a BEACON** — a pre-placed transponder / orbital-relay-transmitted site coordinate with noise (cheap, gate-clean, proves acquisition→guidance on Mars without the VLM); then **(general) VISION/TRN** — headless sensor-camera → Qwen3.5-9B VLM → bearing to a described/selected safe site → laser rangefinder → Kalman track → target estimate + covariance → guidance. On Mars the vision path becomes **Terrain-Relative Navigation (TRN) + hazard avoidance:**
- The operator **describes** a target ("the flat spot left of the big crater," "the smooth plain avoiding the boulder field") OR the policy/perception **selects** a safe flat site from the imaged terrain (the Perseverance "Lander Vision System" analog: match imaged terrain to a hazard map, pick a safe divert target).
- The VLM's output is a **target estimate** in the same `target_xy` slot the `target_sandbox` design threads through `GuidanceCmd` (`target_sandbox_design.md` §B.3). **The Mars vision-site-selection reuses the movable-target plumbing verbatim** — the target is just *chosen by perception* instead of *set by a scenario or an operator drag*.
- **Determinism (critical):** the VLM is heavy + nondeterministic, so per `perception` it runs **ASYNC as a precompute** emitting a **target-estimate trace** (a time series of `(t, target_xy, covariance)`), and **the deterministic guidance loop replays that trace** — precompute-in/telemetry-out (D-011). The Mars descent is then bit-exact given the trace. **Live vibe-instruct** ("actually, land further from that rock") is the FENCED mode (Mode 2, `target_sandbox` §M2) — determinism deliberately waived, quarantined from goldens.
- **Dust couples here (§A.6):** a Mars dust perturbation degrades the *optical* channel — the VLM's target estimate gets noisier/covariance grows. This is the honest Mars imaging challenge, and it lands in the perception trace as increased covariance (which the policy, trained on the joint disturbance, must tolerate). **This is a genuinely-interplanetary honesty touch the Earth demo cannot show.**

### B.4 What the SAME trained-policy architecture must handle (and where it honestly fails)

The `neuralpolicy` architecture is world-agnostic *by construction* (it targets `D_phys(world)`, speaks `GuidanceCmd`, sees world context as observation, §C). On Mars it must:
- Ignite SRP earlier/higher (the margin logic re-solves this from the Mars ballistic shoot — no new code, new numbers).
- Divert with **thrust vectoring, not fins** (the policy learns "in this authority envelope, aero is useless; use the engine" — because it was trained across worlds including low-density ones, §C).
- Tolerate a noisier perception estimate under dust (trained on the joint disturbance including perception covariance).
- **Honestly CRASH (directive 3) when the offset exceeds the Mars thrust-divert budget** — which is *tighter in aero terms but different in thrust terms* than Earth. A far-offset site that Earth's aero could glide to is un-reachable on Mars if the thrust budget can't cover it. **This is not a bug — it is the honest interplanetary physics, and the `D_phys(Mars)` table defines exactly where the line is.** The scenario disperser (§C) must be set so the offset distribution is *well-posed against `D_phys(Mars)`* (the `ceiling.c` recommendation loop, `:633-675`, re-run for Mars), or the landing rate honestly drops — and the ADR reports it.

### B.5 The Mars scenario (initial conditions)

A new `entry_mars` scenario in `scenario.c`'s `DEFS[]` (`scenario.c:14-21`): entry from orbit — high altitude (Mars EDL starts ~125 km but the *powered* problem starts lower; a defensible sim scenario is ~40-60 km at entry-interface-relative velocity, tuned so the thin atmosphere + SRP problem is well-posed), high velocity, an offset dispersion **sized against `D_phys(Mars)`** (§B.4). The scenario carries `world = &WORLD_MARS` (a new `ScenDef` field, or a parallel world-selector). Seeded, replayable, golden-able. **The Mars scenario is a `ScenarioEnv` + a `World` selection — no new machinery.**

---

## C. DOMAIN RANDOMIZATION ACROSS WORLDS — one policy, or a family?

### C.1 The question, stated

`neuralpolicy` trains `π` on the joint disturbance distribution (engine-out × shear × moving-target × dispersions), distilled from an MPPI teacher then RL-refined, targeting `D_phys`. **The interplanetary extension: add the World params as another randomization axis** — train over a *distribution of worlds* (gravity × atmosphere-density × scale-height × wind/dust × the existing disturbances). Two designs:
- **(1) ONE cross-world policy** — a single `π(observation, world_context)` that generalizes across the world family.
- **(2) A policy FAMILY** — one specialized `π_world` per world (Earth-π, Mars-π), selected at deploy.

### C.2 The tradeoff

| axis | ONE cross-world policy | policy family (per world) |
|---|---|---|
| **generalization** | learns the *invariant skill* ("null offset within the current authority envelope"); transfers to unseen worlds by interpolation | zero cross-world transfer; a new world = a new training run |
| **capacity demand** | higher — must represent a family of dynamics; risk of averaging (jack-of-all-worlds) | lower per policy — each specializes cleanly |
| **the interplanetary thesis** | **IS the thesis** — "one brain flies any world" (the SpaceX generalization dream) | contradicts it — you re-solve each planet |
| **determinism export** | one frozen C-header MLP + world context as input | N frozen MLPs, select by world_id |
| **robustness to world mis-estimate** | graceful (interpolates) | brittle (wrong-π = wrong dynamics assumptions) |
| **honest failure visibility** | the policy that can't cover an offset crashes — same on every world (directive 3) | same, but per-policy |

### C.3 Recommendation — ONE cross-world policy, MPPI-teacher-distilled, with world context observed

**Ship (1): a single cross-world `π`, distilled from a PER-WORLD MPPI teacher, with a low-dim world embedding fed as observation.** Rationale:
1. **It is the interplanetary thesis made real** — "describe a target on another planet and watch *the same policy* land it" is the north star; a per-world family quietly abandons it.
2. **The teacher already generalizes for free.** MPPI is model-based — give it the `World` (via `EnvCtx`, §A.7) and its rollouts fly the right dynamics *with zero retraining* (D-016's MPPI re-solves any dynamics you hand it). So the **teacher is per-world at no cost**; the student distills across the teacher's world-conditioned demonstrations. This is the clean pipeline: MPPI(world) generates optimal-ish trajectories on each sampled world → the neural policy distills the *union* → RL-refines against the joint (world × disturbance) distribution.
3. **The world context the policy observes is low-dim and physical:** `{g0, ρ0, H_scale, TWR_max=Tfull/(m·g0)}` — 4 numbers (or an even lower-dim learned embedding). The policy conditions on "how much gravity, how much air, how much thrust headroom" — exactly the quantities that determine the authority envelope. **This is domain randomization + explicit context (the strongest generalization recipe: randomize the world, but *tell* the policy which world it's in).**
4. **The determinism export is unchanged** — `GM_NEURAL` is a frozen MLP; feeding it 4 extra input floats (the world context, constant per run) is trivial and stays a pure function. Golden-able per (world, seed).

**The honest hedge:** if the cross-world policy underperforms a specialist on any world by a meaningful margin (measured — the joint-distribution training may dilute), **fall back to a small family (Earth-π, Mars-π) sharing the same architecture + export path.** Design the pipeline so this is a *config choice* (train on {Earth}, train on {Mars}, or train on {Earth, Mars, sampled-generic}) not a rewrite. **Recommendation stands at ONE policy; the family is the measured fallback** — same posture `neuralpolicy` takes on distill-vs-RL.

### C.4 The randomization ranges (the world distribution)

Sample worlds around the two anchors + a generic spread: `g0 ∈ [3.0, 10.5]` m/s², `ρ0 ∈ [0.01, 1.5]` kg/m³ (log-uniform — spans Mars-thin to Earth-thick), `H_scale ∈ [7, 14]` km, `heat_k`/`R_gas`/`γ` co-varied physically (a thin CO₂ world vs a thick N₂/O₂ world). **Include the anchors (Earth, Mars) as fixed points in the sample** so the policy is *exactly* good on the two worlds that matter, and interpolates between. Each sampled world is a **frozen `World`** — the sampling is a training-time meta-seed (out of the gated loop), and any *specific* trained policy is deployed against *specific* frozen worlds with per-(world,seed) goldens. **Determinism is untouched: training samples worlds; deployment freezes them.**

---

## D. THE MASTER ARCHITECTURE — the full data flow, one coherent stack

### D.1 The block diagram (ASCII)

```
                          ┌─────────────────────── OUT-OF-LOOP / FENCED (nondeterministic, heavy) ───────────────────────┐
                          │                                                                                              │
   operator describes ───►│   ┌──────────────┐   frames    ┌───────────────────┐  bearing  ┌──────────────┐            │
   a target / site        │   │ SENSOR-CAMERA │────────────►│  VLM (Qwen3.5-9B)  │──────────►│ LASER RANGE-  │            │
   ("flat spot L of the   │   │ (headless     │  (rendered  │  landing-site /    │  + range  │ FINDER + KALMAN│            │
    crater")              │   │  render of    │   terrain)  │  target detection  │           │  TRACK        │            │
                          │   │  the terrain) │             └───────────────────┘           └──────┬───────┘            │
                          │   └──────────────┘                                                      │                    │
                          │                                                    emits (ASYNC PRECOMPUTE)                   │
                          │                                            ┌───────────────────────────▼──────────────────┐ │
                          │                                            │  TARGET-ESTIMATE TRACE                        │ │
                          │                                            │  time series: (t, target_xy, covariance)     │ │
                          │                                            │  [+ Mars: covariance grows under dust]       │ │
                          └────────────────────────────────────────────┴───────────────────────────┬──────────────────┘ │
                                                                                                    │ replayed (frozen)  │
  ╔═══════════════════════════════ THE DETERMINISTIC GATED LOOP (fixed dt=2ms, seeded, memcmp-golden) ═══════════════════╗
  ║                                                                                                  ▼                    ║
  ║   ┌──────────┐   NavState    ┌──────────────────────────────────────┐   GuidanceCmd   ┌──────────────────┐          ║
  ║   │  NAV /   │──────────────►│  GUIDANCE  (one of:)                  │────────────────►│ CONTROL          │          ║
  ║   │  §8.1    │  (state +     │   • GM_HOVERSLAM  (reactive)          │  {throttle,     │ ALLOCATION       │          ║
  ║   │  measure │   target est. │   • GM_MPPI  (model-based replan)     │   a_lat[2],     │ (control.c:      │          ║
  ║   │  layer   │◄──┐  from     │   • GM_NEURAL (frozen C-header MLP)   │   engine_cmd,   │  attitude PD →   │          ║
  ║   └────┬─────┘   │  trace)   │        │                              │   n_eng,        │  gimbal/throttle │          ║
  ║        │         │           │        │ reads: legal_state,          │   deploy_cmd}   │  /fins/RCS)      │          ║
  ║        │         │           │        │  target_xy, eng_health,      │                 └────────┬─────────┘          ║
  ║        │         │           │        │  WORLD context (g,ρ,H,TWR)   │                          │ Actuators          ║
  ║        │         │           │        ▼                              │                          ▼                    ║
  ║        │         │           │  ┌─────────────────┐                  │                 ┌──────────────────┐          ║
  ║        │         │           │  │ MPPI ROLLOUTS   │ share the SAME   │                 │  PLANT  (dynamics.c)         ║
  ║        │         │           │  │ (dir.7: copy    │ EOM + WORLD ───────────────────────►│  EOM: world_gravity(w,h),   ║
  ║        │         │           │  │  EnvCtx incl.   │ as the plant     │                 │  world_atmo(w,h), thrust,   ║
  ║        │         │           │  │  World*)        │                  │                 │  aero, fins, contact, mass  ║
  ║        │         │           │  └─────────────────┘                  │                 └────────┬─────────┘          ║
  ║        │         └───────────────────────────────────────────────────────────────────┐          │ state             ║
  ║        └────────────────────────────────────────────────────────────────────────────┐│          ▼                    ║
  ║                                                                              INTEGRATOR (RK4, fixed dt) ── advances ──╬──┐
  ╚═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝  │
                                                                                                    │ telemetry (fp32)      │
                          ┌─────────────────────── PURE OBSERVERS (one-way, precompute-in/telemetry-out) ─────────────────┐ │
                          │   protocol.h packets ──► WebSocket (ws.c) ──► renderer (UE/WebGPU) / audio / recorder         │◄┘
                          │   [HELLO carries world_id + g0/rho0 so the scene labels "MARS" + scales haze — a READ only]   │
                          └────────────────────────────────────────────────────────────────────────────────────────────┘
```

### D.2 The interfaces (how each piece plugs in)

- **Target estimate → guidance:** via `target_xy[2]` (+ covariance) threaded into `GuidanceCmd` (the `target_sandbox` §B.3 plumbing) or `NavState`. In the deterministic loop, `target_xy` is **read from the replayed perception trace** (Mode 1) or set by operator drag (Mode 2, fenced). **Guidance reads only the target's current estimate** — the §8.1-legal "part of NavState" quantity (`target_sandbox` §2). The covariance can modulate how aggressively the policy commits (a wide estimate → hedge), which is a *learned* behavior for `GM_NEURAL`.
- **Engine health → guidance:** the `eng_health[]` chamber-pressure flags (`engineout_design.md` §C.2) — a §4.3-legal *sensed on-board* quantity. `GM_NEURAL`/MPPI read it to re-size authority after an engine-out. Feeds the "burn longer / re-solve" behavior.
- **World context → guidance:** `{g0, ρ0, H_scale, TWR_max}` (a constant-per-run 4-vector) fed to `GM_NEURAL` as observation (§C.3), and the `World*` fed to MPPI via `EnvCtx` (§A.7). **Guidance is world-aware by observation, not by hardcoding** — the same policy, different context.
- **Sensor-camera → the plant's sensor suite:** the camera is an OBSERVER of the *rendered* scene (the renderer already exists as a pure observer). It sits on the **out-of-loop side** — it images what the plant produced (via telemetry → render → camera), the VLM processes ASYNC, and its output re-enters as a *frozen trace* (Mode 1) or a *fenced live input* (Mode 2). **The camera never closes a runtime loop into dynamics** (D-011): image-out is telemetry-derived; target-in is a replayed/fenced scalar. This is the precise line the doctrine draws.
- **Guidance → control → plant:** unchanged. `GuidanceCmd {throttle, a_lat, engine_cmd, n_eng, deploy_cmd, mode}` (`guidance.h:6-16`) → `control.c` allocates to gimbal/throttle/fins/RCS → `dynamics_deriv`. **`GM_NEURAL` is a fourth `mode` value** (`guidance.h:18`, `enum {GM_NONE,GM_HOVERSLAM,GM_MPPI,GM_NEURAL}`) — the policy fills the *same* `GuidanceCmd`. This is why swapping brains (or worlds) needs no control/plant change.

### D.3 What's in the DETERMINISTIC GATED LOOP vs ASYNC/FENCED

**IN the gated loop (fixed dt=2 ms, seeded, memcmp-golden):**
- The plant EOM (`dynamics_deriv` with `world_atmo`/`world_gravity`), the RK4 integrator, contact, actuator lags.
- NAV (§8.1 measurement layer).
- Guidance: `GM_HOVERSLAM` / `GM_MPPI` / **`GM_NEURAL` (a frozen C-header MLP — a pure fp-ordered function)**.
- The MPPI rollouts (they're in-loop but directive-7-consistent: same EOM + same World).
- **The replayed perception trace** (a frozen time series read by index — deterministic input, like a scenario table).
- The World (a frozen const param set).

**ASYNC / FENCED / PRECOMPUTE-IN (out of the gate, D-011 line):**
- **VLM inference** (heavy, nondeterministic) → emits the target-estimate trace (precompute-in).
- **Neural-policy TRAINING** (MPPI-distill + RL) → emits the frozen weights (precompute-in; the weights become a C header compiled into the gated loop).
- **The renderer / audio / camera** (pure observers, telemetry-out).
- **Live operator vibe-instruct / target drag** (Mode 2, `--serve-interactive`) — determinism deliberately waived, **quarantined from every golden and honesty claim** (`target_sandbox` §M2.1).

**The protocol touch (optional, pure-observer):** a HELLO `world_id` + `g0`/`rho0` field so the renderer labels the scene and scales atmospheric haze — a **read-only** enrichment, a protocol v-bump through the D-010/D-011 mirror+golden unit if adopted (not required for the physics).

### D.4 Reconciliation with directives 2 / 3 / 5 / 7

- **Directive 2 (determinism):** every in-loop element is seeded/frozen/pure. Worlds are frozen params (golden-able). Perception is a replayed frozen trace. The policy is a frozen pure function. **The memcmp oracle still holds** — replay a (world, seed, perception-trace, policy-weights) tuple and get bit-identical output. Nothing stochastic runs in the gate.
- **Directive 3 (can't-solve-it-crashes):** **strengthened, not weakened.** The learned policy has NO assist terms — it outputs `GuidanceCmd`, and if the offset exceeds `D_phys(world)` the vehicle honestly crashes (§B.4). Mars makes this *more* visible (tighter aero, honest thrust-budget limits). The anti-cheat thesis survives *because the policy is trained against `D_phys`, not against a landing-guarantee* (§F.2).
- **Directive 5 (pure observer):** the camera/VLM sit on the observer side; their output re-enters only as a **frozen trace** (Mode 1) or a **fenced scalar** (Mode 2). **No renderer STATE ever re-enters dynamics** — the forbidden loop (D-011 §5) does not exist. Perception is precompute-in/telemetry-out — the doctrine applied to the sensor.
- **Directive 7 (one dynamics source):** the `World*` rides `EnvCtx` into the rollout copy (§A.7), so plant + predictors + rollouts share the same EOM *and the same world*. The mandatory Earth-invariance check proves no leak. **This is the D-012 leak-discipline, extended to worlds.**

---

## E. THE PHASED ROADMAP — each rung independently shippable + gated

Ranked by value/effort; dependencies + consumed artifacts noted. **Every rung is gated by: `--selftest` PASS + TERMINAL 194/200 byte-exact + a determinism pair + (if guidance touched) the MPPI single-run invariance check** (`HANDOFF §1.7`).

**Rung M-W — WORLD PARAMETERIZATION (the keystone; do FIRST).**
- Build: `core/world.{h,c}` (§A.1), `atmo_eval→world_atmo` (§A.2), the `dynamics.c` gravity+Isp-split edits (§A.3), thread `World*` through `EnvCtx` + predictors + rollout (§A.7). Ship `WORLD_EARTH` only.
- Gate: **the Earth-identical golden gate (§A.8)** — every current baseline byte-exact. This is the whole acceptance test.
- Consumes: the existing goldens (as the equality target), the D-012 leak-discipline (as the plumbing template).
- Value/effort: **HIGHEST value-per-effort.** Mechanical, bounded, unlocks everything, ships as a safe refactor with zero behavior change. **This is a real this-quarter deliverable.**

**Rung M-Mars — THE MARS PLANT (+ re-solve).**
- Build: `WORLD_MARS` (§B.1), the `entry_mars` scenario (§B.5), parameterize `ceiling.c` by `World` and recompute `D_phys(Mars)` (§B.2.4), re-tune the reactive/MPPI ignition numbers for the SRP regime (the *mechanism* is right, the numbers shift), the Mars dust term (§A.6, `dust_tau`).
- Gate: Earth goldens still byte-exact (Mars is additive, `dust_tau=0`/Earth inert); a NEW per-Mars golden frozen; the Mars landing rate reported *honestly* against `D_phys(Mars)` (directive 3 — it may be lower, and that's the truth).
- Consumes: M-W (the `World` abstraction), `ceiling.c` (re-run per world), the E3 supervisor + hoverslam/MPPI (re-tuned, not rewritten).
- Value/effort: **medium effort, huge narrative value** — "it lands on Mars" is the headline. Depends on M-W.

**Rung M-Percept — PERCEPTION ACQUISITION (Earth first).**
- Build: per `runs/perception_design.md` (landed) — its **S0 BEACON** (noisy-GPS transmit, trivially gate-clean, proves acquisition→guidance) → **S1 VLM bearing + rangefinder headless logged-trace** (the async-precompute/replay determinism split) → **S2 fenced live language demo** (`--serve-perceive`, reuse `target_sandbox` Mode 2).
- Gate: the replayed-trace run is byte-exact (the trace is a frozen input); the truth-oracle is *removed* (the vehicle senses, not told); live mode fenced. Coordinate ONE protocol version bump with `target_sandbox` (perception §E flags this).
- Consumes: `target_sandbox_design.md` (the `target_xy` plumbing + two-mode split), the renderer (as the camera's image source).
- Value/effort: large but independently shippable on Earth; the Mars BEACON+TRN (§B.3) is a re-point of the same S0→S1 pipeline. Parallel to M-Mars.

**Rung M-Policy — NEURAL POLICY (Earth, `GM_NEURAL`).**
- Build: per `runs/neural_policy_design.md` (landing) — distill-from-MPPI, RL on the joint disturbance, the frozen C-header MLP export as `GM_NEURAL`.
- Gate: `GM_NEURAL` is golden-able (frozen weights → pure function → memcmp-clean); it targets `D_phys` honestly (crashes past the frontier); TERMINAL/AERO/ENTRY under `--neural` reported vs the MPPI/reactive baselines.
- Consumes: `ceiling.c` `D_phys` (the training target), MPPI (the teacher), the play-menu disturbances (engine-out, gust, moving-target) as the joint training distribution.
- Value/effort: large; the anchor of the learned-guidance vision. Parallel to M-Mars/M-Percept; the export path is world-agnostic (feeds M-Xworld).

**Rung M-Xworld — CROSS-WORLD POLICY (the randomization axis). = `neuralpolicy`'s S3 ("hand to lane interplanetary").**
- Build: add World params as a randomization axis + world-context observation (§C); train ONE `π` on {Earth, Mars, sampled-generic} via per-world MPPI teachers (§C.3); measure vs per-world specialists. **This is the explicit handoff point the `neuralpolicy` design names as its S3** — its export path is world-agnostic by construction, so this rung adds the world axis, not a new export.
- Gate: the cross-world policy lands both anchors ≥ its per-world-specialist-minus-ε; each (world, seed) golden-able; honest report if generalization dilutes (→ the family fallback, §C.3).
- Consumes: M-W (worlds), M-Mars (the Mars world + `D_phys`), M-Policy (the policy + export), M-Percept (the target estimates as part of the observation).
- Value/effort: high effort, the interplanetary-generalization payoff. Depends on M-W + M-Mars + M-Policy.

**Rung M-Showcase — THE FULL "DESCRIBE-A-TARGET-ON-MARS-AND-WATCH-IT-LAND-THROUGH-ENGINE-OUT".**
- Build: compose it all — Mars world + vision-described target (perception trace) + cross-world neural policy + a seeded mid-descent engine-out (`engineout_design.md`) → the renderer draws the Mars scene, the `pred_impact` marker chasing the vision-picked site, the ghost line writhing on the engine-out, the vehicle re-solving to a Mars landing.
- Gate: the whole run (given the frozen perception trace + weights + world + engine-out seed) is bit-exact and golden-able; the live-drag variant is the fenced showpiece.
- Consumes: **everything** — M-W, M-Mars, M-Percept, M-Policy, M-Xworld, the engine-out + target play-menu, the protocol `pred_impact`/`target_xy` fields, the renderer.
- Value/effort: **highest effort, the summit.** The north star the rungs climb toward.

**Dependency graph:** `M-W → {M-Mars, (M-Policy, M-Percept in parallel, both Earth-first)}`; `{M-W, M-Mars, M-Policy} → M-Xworld`; `all → M-Showcase`.

---

## F. HONEST RISKS

### F.1 The determinism surface area grows on three axes — enumerate golden-able vs fenced

- **Golden-able (stays in the memcmp oracle):** a `World` (frozen params, per-world goldens); the replayed perception **trace** (a frozen input); the frozen `GM_NEURAL` **weights** (a pure function); the Mars scenario; the engine-out event (seeded closed form). **The gate grows by ADDING golden files (per-world, per-policy-version, per-perception-trace), not by weakening the oracle.** Each new world/policy/trace is a new frozen artifact with its own golden — the D-016 "freeze the golden per unit" discipline scales.
- **Fenced (deliberately OUT of the oracle, quarantined):** live VLM inference (nondeterministic — that's why it precomputes to a trace); RL training (a meta-process, out of the loop); the live operator vibe-instruct / target-drag (Mode 2, `--serve-interactive`, determinism waived by design). **The fence is the same one `target_sandbox` §M2.1 draws: a distinct binary mode with a loud banner, no goldens, quarantined from every honesty claim.** The risk is *discipline erosion* — someone quoting a live-mode landing as if it were golden. Mitigation: the mode-name quarantine + the ADR's explicit "never a golden from live mode."
- **The subtle new nondeterminism risks:** (a) `exp()`/`pow()` in `world_atmo` must be the same host/device library path (they are — §A.2); (b) the world-context floats fed to `GM_NEURAL` must be bit-identical host/device (constant-per-run, so trivially so); (c) the perception trace timestamps must align to the fixed-dt grid (replay by step index, not wall-clock). Each is a known pattern with a known fix.

### F.2 The anti-cheat thesis under a learned policy on a randomized world

**The sharpest risk.** A learned policy is a black box — how do we know it isn't *cheating* (exploiting the target-truth, or a sim artifact, instead of solving the physics)?
- **The structural defense:** the policy reads only the **legal_state** (`neuralpolicy`'s framing) — the same NavState the reactive/MPPI laws see — plus the §4.3/§8.1-legal quantities (target *estimate* from perception, engine *health* from chamber-P, world *context*). **It has NO access to the truth target** (perception removed the oracle — that's the whole point of the `perception` lane) — so it *cannot* cheat by reading the answer; it must solve from the sensed estimate.
- **The `D_phys` defense:** the policy is trained/measured against the reachability frontier (`ceiling.c`), and **it is allowed to fail** (directive 3) — a policy that lands 100% of offsets *including un-reachable ones* would be the cheat signature. **The honest metric is "lands the reachable fraction, crashes the rest at the `D_phys` line"** — the same anti-cheat gate the reactive/MPPI laws pass. Cross-world makes this *more* rigorous: the policy must respect `D_phys(world)` on *every* world, so a cheat that worked on Earth's authority envelope would fail Mars's tighter one.
- **The residual risk (state it):** RL can find *sim exploits* (a quirk of the aero table, a contact-solver edge). Mitigation: (i) the joint-disturbance + cross-world training makes single-quirk exploits fragile (they must survive randomization); (ii) **the MPPI-distillation floor** — the policy is anchored to a model-based teacher that provably solves the physics, so it starts honest and RL refines *within* that basin; (iii) the byte-exact golden lets any suspicious behavior be replayed + traced (the `tools/tracestat` forensics). **This is a real risk that needs a dedicated anti-cheat audit rung** (compare `GM_NEURAL` trajectories to MPPI on held-out worlds; flag divergence) — flagged, not hand-waved.

### F.3 The perception + world coupling risks

- **The VLM is the least-deterministic, heaviest element** — the async-precompute/replay split (D-011 applied) is *load-bearing*; if a build agent is tempted to run the VLM live in the loop "for interactivity," the oracle dies. The fence must hold.
- **Mars dust degrades perception in a way Earth doesn't** (§A.6, §B.3) — the policy must tolerate a growing-covariance target estimate. If trained only on clean estimates, it will be brittle under dust. Mitigation: include perception covariance (incl. dust-degraded) in the joint training distribution.
- **The `ceiling.c` `D_phys` recompute is a dependency for BOTH Mars and the policy** — if the Mars ceiling is mis-computed (e.g. the aero-bound not properly collapsed), the scenario disperser is mis-set and the landing rate is misleading. The `ceiling.c` self-test (`:448-464`, the constant-authority closed-form check) must pass per world.

### F.4 Scope realism — the ranking, stated plainly

**This is a multi-milestone north-star, not a weekend.** Honest value/effort ranking:
1. **M-W (World parameterization)** — **DO THIS FIRST.** Highest value/effort: bounded, mechanical, gate-clean, ships as a zero-behavior-change refactor, unlocks everything. A real near-term deliverable.
2. **M-Mars (the Mars plant)** — high value (the headline), medium effort. The aero-story-changes re-tune is real work but not a re-architecture. Depends on M-W.
3. **M-Policy / M-Percept** — the two lanes landing now; large, independently shippable on Earth, high strategic value (learned guidance + honest perception).
4. **M-Xworld (cross-world policy)** — high effort, the generalization payoff; gated on M-W + M-Mars + M-Policy.
5. **M-Showcase (the full Mars-vision-through-engine-out)** — highest effort, the summit; consumes all.

**The honest framing for the operator:** the *World abstraction* is a concrete, bounded, this-quarter engineering deliverable that makes the codebase interplanetary-ready and ships behind an Earth-identical golden. The *full showcase* — describe a target on Mars, watch one learned brain land it through an engine-out, seen through a real camera under a dust storm — is the **north star**: vivid, real, and reachable, but *reached by climbing the rungs*, each of which is itself a shippable, gated, honest artifact. **We do not fake the summit; we build the staircase.**

---

## G. Appendix — the exact edit checklist (for the M-W build agent)

A condensed, file:line-indexed edit list so M-W is implementable without re-deriving (all gated by §A.8):
1. **`core/world.{h,c}`** — new. `World` struct + `WORLD_EARTH` (US76 tables verbatim from `atmosphere.c:10-13`, `g0=9.80665`, `radius=6356766`) + `WORLD_MARS` (§B.1) + `world_gravity` + `world_atmo` (§A.2), both `BL_HD`.
2. **`core/constants.h`** — rename `G0`→`G0_STD` at the **Isp/mdot** sites ONLY (`:24,25`), OR keep `G0` as standard-gravity and add nothing (the world carries local g). Add a load-bearing comment at `:8` distinguishing the two roles. `ENG_MDOT_100/T_VAC/AE` unchanged (hardware).
3. **`core/atmosphere.c`** — `atmo_eval(h)` → `world_atmo(w,h)` dispatcher (§A.2); Earth ISA path byte-identical.
4. **`core/dynamics.h`** — add `const World* world;` to `EnvCtx` (`:20-29`).
5. **`core/dynamics.c`** — `:103` `world_atmo(w,...)`; `:115` `P0_ATM→w->atmo.p_ref`; `:116` `G0→G0_STD` (KEEP standard); `:227` `world_gravity(w,h)`; `:275` `HEAT_K→w->heat_k`. (`engine_thrust` unchanged.)
6. **`core/guidance_hoverslam.c`** — pass `World*` to the predictors; `:25,69,92` `world_atmo`; `:28,71,115,243` `world_gravity`/`w->g0` (surface-g terms).
7. **`core/guidance_mppi.c`** — `:168,244,270,526,582` `world_atmo`; `:170,246,284,291,528,583` `world_gravity`/`w->g0`; `:530` `G0→G0_STD` (mdot, KEEP standard). The rollout inherits `World*` via the `EnvCtx` copy (`:350,555`) — verify.
8. **`core/control.c`** — `:37` `world_atmo`; `:84` `a_vert_ref = w->g0 + 2.0` (world gravity); `:248` `G0→G0_STD` (N2 flow uses Isp·g0 → standard). 
9. **`core/sim.c`** — `sim_init` selects the `World` (default `WORLD_EARTH`), stores it, sets `env.world`; `:153-154` `world_gravity`+`world_atmo`; `:183,276,311` `world_atmo`; `:279,314` `w->g0`; `wind_sample` reads `w->wind` (§A.6). `:392` `QBAR_MAX` unchanged (vehicle limit).
10. **`core/scenario.c`** — add a `World*` selector to `ScenDef`/`ScenarioEnv` (default Earth); the Mars scenario (§B.5) is additive.
11. **`runs/sandbox/ceiling.c`** — mirror the `World` (it already mirrors `core/` verbatim); recompute `D_phys` per world (§B.2.4). Self-test (`:448`) must pass per world.
12. **`core/main.c`** — `--world earth|mars` CLI (default earth → byte-identical); the selftest (`:41-48`) goes through `world_atmo(EARTH,·)` and must still pass.
13. **OPTIONAL `core/protocol.h`** — HELLO `world_id`+`g0`+`rho0` (pure-observer read, D-010/D-011 mirror+golden unit if adopted).

**The gate after every one of these: §A.8 (Earth byte-exact) — the whole thing is a no-op refactor until a non-Earth world is selected.**

---

*The plant has been wrong six times; the method has never been. Worlds, perception, and learned weights each grow the determinism surface — and each is fenced by the one doctrine already proven here: seed it, freeze it, precompute it in, telemetry it out. Build the staircase; the summit is a described target on Mars, one brain, through an engine-out, and it is reachable one gated rung at a time.*
