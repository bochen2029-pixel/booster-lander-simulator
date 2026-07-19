# The Perception-to-Policy Stack — master synthesis & index
### The next-generation architecture: perceive → localize → learned-control, world-parameterized. (2026-07-19)

> **What this is.** A single north-star architecture assembled from three implementer-grade design
> docs written this session. It takes the Booster Lander Simulator from "a hand-tuned/sampled
> controller lands a booster onto a known point on Earth" to **"tell a vehicle in natural language
> where to land on *any world*, and watch a learned policy solve it through compounding failures —
> engine-out, wind shear, a moving target — recovering whenever physics permits."** Nothing here is
> built yet; it is a build-ready blueprint. The pieces are grounded in the existing plant, the
> shipped play-menu, and the operator's local assets (RTX + rentable H200s; Qwen3.5-9B via
> llama.cpp).

---

## Why now — the empirical case (D-018)

M6 is green: MPPI's closed-loop replanning cleared ENTRY ≥90 (95/91/93). But **M4 (AERO ≥90) hit a
wall, and the fleet proved from four independent angles (D-018) that it is a PLANT-AUTHORITY
ceiling, not a solver problem**: kprobe (capacity), mppi-var (OU noise), covo (anisotropic
covariance), mpopi (iterate) all null, all diagnosing the same aero/thrust crossover dead-zone.
The controller realizes only ~0.70·D_phys ≈ 775 m of the physical 1107 m divert ceiling. The
off-pad seeds are *physically landable* but past what the hand-tuned/sampled controller reaches.

**That wall is exactly where a learned policy earns its keep.** The classical/sampling approach has
reached its honest limit on the hard scenario; closing the 775 → 1107 m gap is a
**frontier-extraction** problem, and a policy trained toward the reachable set is the credible
method. So the neural policy is not a speculative add-on — it is now on the **M4 critical path**.

---

## The three pillars (each an implementer-grade doc)

### 1. The learned guidance policy — `runs/neural_policy_design.md` (the anchor, 80 KB)
π(legal_state) → GuidanceCmd, targeting the **reachability frontier**. The metric is the
contribution: **recovery-rate-vs-frontier** (against the backward-reachable set `ceiling.c` already
slices), which decouples "how good is the controller" from "how hard is the scenario" — the exact
confound behind every landed-rate number in this repo. Path: **DISTILL from MPPI** (DAgger; +G-FOLD
reach labels) → **RL** (SAC, PPO fallback) to exceed the expert → optional **RESIDUAL on
hoverslam** for safety/legibility. A ~38k-param, ~4-layer MLP, **<10 µs inference** (state-based, ~4
orders smaller than Tesla-FSD image nets). The project-defining move: **frozen weights → fp64
fixed-order C forward pass → bit-deterministic, golden-able, a versioned constant = a new
`GM_NEURAL` mode** alongside GM_HOVERSLAM/GM_MPPI. The showcase (§G): recover from a mid-entry-burn
engine-out with a simultaneous gust and a moving target, scored against the reachable set.

### 2. The perception front-end — `runs/perception_design.md` (57 KB)
The honest answer to "how does the rocket know where the (moving) target is?" — **sense it, don't be
told.** A deterministic plant-side **sensor-camera** → **Qwen3.5-9B VLM (via llama.cpp mmproj)**
grounds a natural-language instruction ("land on the flat spot left of the ridge") into a **bearing**
→ **laser rangefinder** gives range → **localize** to `target_3D = veh_pos + range·bearing` using
the vehicle's *own* noisy pose → **Kalman filter** tracks a moving target. It **removes the
truth-origin oracle and replaces it with perception — a purity upgrade to the anti-cheat thesis.**
The VLM runs async (a precompute emitting a target-estimate **trace**); the deterministic guidance
loop replays the trace bit-exact. Baseline honest path: a **beacon** (droneship GPS, how real
SpaceX works). Money-shot machinery (`markers.ts:60 solveConvergence`) already exists in the
renderer.

### 3. World-parameterization & master integration — `runs/interplanetary_integration_design.md` (76 KB)
A clean **`World`** abstraction (gravity + radius, atmosphere density/scale-height/composition/
speed-of-sound, wind) replacing the hardcoded Earth constants — so the **same stack** runs Earth
booster → **Mars reentry** (thin CO₂, 0.38 g, vision-picked hazard-free site — the Perseverance TRN
analog) → generic. **Domain randomization across worlds** becomes another axis of the policy's
joint-disturbance training — one policy family that generalizes. Holds the **master data-flow**
(below) and reconciles the whole thing with the determinism/observer doctrine.

---

## The unified architecture

```
  [scene geometry] --> SENSOR-CAMERA (plant sensor, deterministic raster)
                            |  image
                            v
                     VLM (Qwen, async, GPU)  --language--> "land there"
                            |  bearing + confidence
                            v
              RANGEFINDER --range--> LOCALIZE --> KALMAN TRACK
                            |                          |
                            |          target_xy + v + covariance (the ESTIMATE / a trace)
                            v                          v
   nav (legal state) -->  [ GM_NEURAL policy  |  or GM_MPPI  |  or GM_HOVERSLAM ]
                            |  GuidanceCmd (a_lat, throttle, n_eng, deploy)
                            v
                     control allocation (control.c) --> PLANT (dynamics.c, World-parameterized)
                            |  telemetry (protocol.h, one-way)
                            v
                     OBSERVERS (renderer, audio) — pure, never loop back

  DETERMINISTIC GATED LOOP: nav -> policy -> control -> plant -> telemetry   (bit-reproducible)
  ASYNC / FENCED (precompute-in): VLM acquisition trace, frozen policy weights, World params, live drag
```

The rule that keeps it honest: **everything in the gated loop is deterministic and golden-able;
everything non-deterministic (the VLM, live operator input) is a precompute whose *output* enters
the gate as frozen data — a trace, a weight-header, a World param — exactly like a wind trace or a
seed.** Directives 2/3/5/7 hold: determinism sacred, if-it-can't-solve-it-crashes (the policy
recovers only inside the reachable set), pure observers, one dynamics source.

---

## The consolidated phased roadmap (value-ranked, each rung independently gated)

| Rung | What | Gate | Unlocks |
|---|---|---|---|
| **P0** | Movable-target socket (`GuidanceCmd.target_xy`, `null(r_xy−target)`) + protocol bump | S0 byte-equality vs today (target=origin) | perception + the whole target story |
| **P1** | Beacon target + deterministic sensor-camera | acquisition→guidance path, gate-clean | perception baseline |
| **P2** | **Distill-from-MPPI policy → `GM_NEURAL`** | match 44/60 at <10 µs, bit-deterministic export | the learned controller, proven |
| **P3** | **RL joint-disturbance policy** (engine-out + gust + moving target) | recovery-rate-vs-frontier; the M4 ≥90 attempt | **the M4 frontier-extractor + the engine-out-recovery showcase** |
| **P4** | VLM bearing + rangefinder + language instruct | logged-trace determinism; fenced live demo | "talk to the rocket" |
| **P5** | World-parameterization (Earth→Mars) | Mars plant + recomputed D_phys | interplanetary |
| **P6** | Cross-world policy + Mars vision-site-selection | the full "describe-a-target-on-Mars-and-watch-it-land-through-engine-out" showcase | the north star |

The dependency spine: **P0→P2→P3 is the M4 critical path** (learned control closing the sampler
wall). P1/P4 (perception) and P5/P6 (worlds) compose on top. The play-menu plant events the policy
trains against are already shipping/designed: `--gust` (D-017, built), engine-out
(`engineout_design.md`), movable target (`target_sandbox_design.md`).

---

## What to build first (my recommendation)

**P0 + P2** — the movable-target socket and the distill-from-MPPI policy. P0 is a one-substitution,
protocol-bumped unit (de-risks the target path and the perception integration point). P2 proves the
entire novel machinery — a policy that *is* the guidance, exported to bit-deterministic C, matching
MPPI at a fraction of the latency — without yet needing RL's reward-shaping art. If P2's distilled
policy reproduces 44/60 as a golden-able `GM_NEURAL` mode, everything after it (RL, perception,
worlds) is a known-shaped extension. **P3 is where the "wow" lives** — the first time you watch a
learned policy catch an engine-out on the way down — and it is also the honest M4 verdict: it
either clears the gate or confirms the 0.70·D_phys wall is truly physical. Decisive either way.

**The canonical build ORDER within the policy track is `neural_policy_design.md` §H.0 — the
build-order doctrine (read it before P2).** The one-line version, because the sequencing is subtle:
**widen the interface once, ramp the difficulty gradually, validate new machinery on the simplest
physics that can exercise it.** Concretely — every plant change splits into an INTERFACE half (what
the policy reads/emits: the target socket, the engine-health flag → build FIRST, wide, so you never
retrain the net) and a BEHAVIOR/DIFFICULTY half (the physics: engine-out torque, target motion, gust
magnitude → a curriculum knob ramped LATER). So the order is: **Step 0** widen the observation
socket (target + health flag, nominal values, byte-equal to today) → **Step 1** build engine-out +
moving-target into the plant but dialed OFF (byte-clean) → **P2/S0** distill MPPI on the EASY setting
to prove the pipeline (engine-out still off — isolate pipeline bugs from physics) → **S1** turn one
disturbance on, distill-then-RL (MPPI is a valid single-disturbance teacher — directive-7 rollouts
see the reduced authority and re-solve) → **S2** RL the JOINT distribution (engine-out + shear +
moving-target at once = the showcase, where MPPI is weak and RL earns its keep). **Do NOT** build the
3-engine plant and distill on it first; build the *capability* early but distill on the *easy* plant
first — strictly lower-risk, zero retraining tax.

**Compute:** P2 distillation + a first P3 policy train on the local RTX in hours-to-days (the C
plant's throughput, not the tiny net, is the bottleneck). The H200 fleet is the *scale* lever for
the full joint-disturbance × multi-world training (P3/P6), not a prerequisite to start.

*Read the three pillar docs for the rigor; this index is the map. The classical guidance work has
reached its honest ceiling on the hard scenario (D-018) — this is the architecture for the next
climb.*
