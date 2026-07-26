# LODESTAR — Solver‑then‑Distill: an offline oracle that lands the "impossible" compound case

| | |
|---|---|
| **Instance / session codename** | **LODESTAR** (Opus 4.8) |
| **Date / time authored** | 2026‑07‑20 12:18 CST (Monday) |
| **Isolation** | git worktree `C:\bl_opus_oracle`, branch `opus/robust-oracle`, off `main @ 29fd724` (D‑039) |
| **Status** | ✅ Proof complete — clean **and** compound landings found (both V_GOOD). Distillation pipeline next. |
| **Non‑collision note** | All work is in the LODESTAR worktree + this uniquely‑named file. Nothing in the shared tree that the parallel instances are editing was touched or overwritten. |

---

## 0. TL;DR

The learned guidance policy has been stuck at ~8–10/60 on engine‑out for months. Three distillation
rounds nulled and the project ledger concluded *"distillation is exhausted → the gap is RL‑class."*

That conclusion was half‑right and misdiagnosed. The real problem was **teacher quality** (the policy was
being taught by ~10%‑success controllers) compounded by three fixable representation faults. The fix the
operator called for: **stop distilling weak closed‑loop controllers — generate near‑optimal solutions with
an expensive offline search, then distill those.** AlphaZero for rocket landings.

This report documents **LODESTAR**, an independent offline solver built to be that teacher. It reuses the
real plant physics and searches a closed‑loop guidance law with the Cross‑Entropy Method (CEM). It answers
the operator's central question — *"is there a physically‑possible way to land the compound almost‑impossible
case (engine‑out + wind + moving target) at zero speed, zero height, on target?"* — with a definitive **yes**:

| Scenario | Verdict | touchdown speed | miss (target‑relative) | tilt | peak dynamic pressure | fuel left |
|---|---|---|---|---|---|---|
| Clean (3 km offset, 62 km entry) | **GOOD** | 1.35 m/s | 2.31 m | 0.04° | 36 kPa | 1686 kg |
| **Compound** (engine‑out + wind + moving deck + heave) | **GOOD** | 2.48 m/s | **0.52 m** | 0.60° | 52 kPa | 1859 kg |

Both are physically valid: within the 80 kPa structural limit, real thrust/gimbal/fuel bounds, no magic.

---

## 1. Backstory — why this was needed

### 1.1 The stuck point (the engine‑out ceiling)
The simulator's learned policy (`GM_NEURAL`, NP_VERSION 6) matches or beats its MPPI teacher on clean flight,
gusts, and entry (95% parity). But on **random engine‑out** it plateaus at ~8–10 landings per 60 — versus a
**proven ~59/60 physically‑reachable frontier** (the D‑027 reachability oracle showed essentially the entire
random‑failure distribution is recoverable). That gap is the whole game: it is the last blocker to the N3
compound showcase.

### 1.2 The misdiagnosis
Three ADRs tried to push engine‑out competence into the policy by distillation and all nulled:
- **E1 (D‑029):** a student‑warm‑started MPPI composite — dead‑even 1/60.
- **E2 (D‑031):** DAgger with an MPPI teacher — regressed (11/180 vs 14/180), reverted.
- **E2′ (D‑032):** DAgger with a *better* reactive teacher — *also* regressed (8→2/60), reverted.

The ledger concluded **"distillation is exhausted for engine‑out; the frontier is RL‑class."**

### 1.3 The real diagnosis (fan‑out investigation)
A parallel swarm of subagents auditing the actual code overturned that headline. The engine‑out failure is
**not** primarily a learning‑capacity wall. It is a stack of concrete, fixable problems:

1. **Authority (the biggest miss).** The neural policy is *never invoked during the entry burn* —
   `entry_supervisor` owns `PH_ENTRY_BURN` and the policy only takes the stick post‑cut (`PH_AERO`). The
   engine‑out fires *inside* the entry burn. ~75% of failures (the "gross cluster") are locked in at the burn
   cut, before the policy can act. You can distill a perfect teacher and still lose them.
2. **The action can't express the maneuver.** Commanded lateral accel is clamped to the 3‑engine authority
   (±3.2 m/s²); the real engine‑out divert needs the ~35° bank / ~5.5 m/s² the reactive law uses. The policy
   physically cannot command it.
3. **MSE mode‑averaging.** At matched observations, clean vs engine‑out commands differ ~54° on average and
   point into opposite hemispheres 27% of the time. One unimodal MSE head emits their average → mush; adding
   a *better* engine‑out teacher makes the labels more extreme and the regression worse.
4. **A distillation ceiling by construction.** You cannot imitate past a teacher. Every teacher used
   (MPPI ~10%, reactive ~10–15%) was itself bad at engine‑out.

### 1.4 The operator's reframe — the actual key
Two decisive insights from the operator reframed the whole effort:

- **Solver‑then‑distill.** Don't distill a 10%‑quality controller — build "a huge solver that spends a long
  time to find the solution by hand" (search / trajectory‑opt) for even the near‑impossible cases, then
  distill those near‑perfect, physically‑real solutions. If the teacher is ~100% on the in‑frontier cases,
  the distilled floor becomes ~100%.
- **Robustness via inference, not labels.** The policy should *not* be told "engine is out" or "gust now."
  It should be robust enough to handle any contingency and still land, inferring the situation from the
  observable dynamics — the way a good pilot feels the vehicle mis‑respond and compensates. (Consequence:
  the network ultimately needs the *acceleration residual* — commanded vs achieved — and a little memory to
  do online system‑ID, and must drop its reliance on fault flags. The litmus test: hide the flags and it
  still lands.)

LODESTAR is the first half of that program: **the near‑optimal solution generator (the teacher).**

---

## 2. The approach — the LODESTAR oracle

### 2.1 Why an offline search oracle (and why CEM)
The plant is deterministic and fast, and "spend the compute" was explicitly authorized. The Cross‑Entropy
Method is derivative‑free, embarrassingly parallel, and — unlike the deployed 5‑second‑horizon MPPI — it
optimizes the **whole descent globally** with full authority over every phase (it is not benched during the
entry burn). It is deliberately **orthogonal** to the other instances' work (MPPI‑crank / SCvx), so this is a
genuinely independent second opinion on feasibility.

### 2.2 Why closed‑loop (the clairvoyance trap)
The guidance law re‑solves every 50 Hz tick from the *current* state. This matters for distillation: an
*open‑loop* optimizer that plans knowing the future wind produces actions the policy can't justify from its
legal observation (identical observation, different future gust → contradictory labels → un‑learnable). A
closed‑loop law's action is a function of the observable state only — a **legal, distillable feedback
policy**. Wind is never an input; the law reacts to the drift it induces.

### 2.3 Architecture
- **Real plant reuse.** `oracle.c` unity‑includes `core/{atmosphere,dynamics,integrator,contact,control}.c`
  (the same pattern `guidance_mppi_cuda.cu` uses). `rk4_step` is the actual 500 Hz integrator → physics
  parity by construction, not re‑implementation.
- **Phase guidance law:**
  - **Entry / max‑Q burn (3 engines):** retrograde deceleration; fires whenever dynamic pressure approaches
    58 kPa — the real max‑Q limiter — so the descent never breaches the 80 kPa structural line.
  - **Unified ZEM/ZEV lateral (all phases):** a zero‑effort‑miss / zero‑effort‑velocity law that seeks the
    (moving) pad *and* damps lateral velocity, with time‑to‑go from the vertical descent. The 3 km cross‑range
    closes gradually with no terminal overshoot. Thrust‑vectored while burning; grid‑fin steered while
    coasting.
  - **Single‑engine hoverslam landing:** a distance‑triggered suicide burn on one (center) engine — so the
    thrust‑to‑weight ratio actually lands the vehicle instead of forcing it to climb. Terminal straighten for
    an upright touchdown.
- **CEM optimizer:** searches 9 interpretable decision variables (entry ignition altitude, entry throttle,
  cut velocity, fuel reserve, landing ignition altitude, ZEM gains Kr/Kv, time‑to‑go scale) over the real
  plant, OpenMP‑parallel across the population, scoring terminal miss against the moving deck; structural
  failure (>80 kPa) is a hard reject.

Notably, CEM **rediscovered the energy‑optimal ZEM gain** on its own (Kr ≈ 5.7, textbook optimum is 6).

---

## 3. What was built (file manifest)

All under `C:\bl_opus_oracle\oracle_cem\`:

| File | Purpose |
|---|---|
| `oracle.c` | the full oracle: plant reuse + phase guidance + CEM + staged `main` (`nominal` / `cem [eo wind target heave\|all]`) |
| `build_oracle.cmd` | isolated MSVC build via `vcvars64` (`/O2 /fp:precise /std:c11 /MT /openmp`) |
| `test_plant.c` | smoke test that the plant unity‑includes, links, and integrates the real entry state |
| `NOTES.md` | working log of the approach + fixes |
| `cem_clean3.txt`, `cem_compound.txt` | the winning runs (per‑iteration convergence + final trajectory + best θ) |

Build & run:
```
cmd /c "C:\bl_opus_oracle\oracle_cem\build_oracle.cmd oracle.c /Fe:oracle.exe"
oracle.exe nominal            # fly the nominal guidance, print the trajectory
oracle.exe cem                # CEM‑optimize the CLEAN case
oracle.exe cem all            # CEM‑optimize the COMPOUND case (eo + wind + target + heave)
```

---

## 4. Physics bugs found & fixed (each surfaced by a real run — the honest record)

1. **Landing law flew back UP and tumbled.** The first ZEM landing, applied only at low altitude with a large
   residual offset, demanded impossible lateral accel, climbed out of the landing‑altitude gate, cut the
   engine, and lost control. → **Unified ZEM from the start + latched landing** (can never exit and fly up).
2. **Structural break (198 kPa).** The entry burn cut too early (kept too much reserve) and the vehicle fell
   supersonic into thick air. → **Max‑Q limiter burn + hard‑reject cost + protection down to a 4.5 t landing
   fuel floor.** Peak dynamic pressure now lands at ~35–52 kPa (matching the real vehicle's ~36 kPa aero peak).
3. **Suicide‑burn trigger never fired.** `a_req ≥ 0.9·a_avail` is never met with 3‑engine thrust vs an
   aero‑limited velocity. → **Distance‑based trigger** (fire when altitude drops to the braking distance).
4. **Could not touch down (hovered/climbed).** Three engines at minimum throttle give TWR ≈ 3.6 — the vehicle
   nulls its velocity *above* the deck and cannot descend the last meters. → **Single‑engine hoverslam** (TWR
   lands, doesn't hover) — exactly why real boosters land on one engine.
5. **Engine‑out could be "revived."** If guidance re‑commanded 3 engines after a failure, the rollout restored
   the dead engine. → **Clamp `n_eng` to healthy engines; zero the induced torque for the on‑axis center‑engine
   landing.**

---

## 5. Results

**Clean case** — 3 km lateral offset, 62 km, −1500 m/s, 30 t propellant:
```
VERDICT=GOOD   td_v=1.346 m/s   td_lat=2.313 m   tilt=0.041°   maxQ=35.7 kPa   fuel=1686 kg   t=98.8 s
```
(Touchdown position and tilt already meet the V_PERFECT bar; only td_v > 2.0 keeps it GOOD.)

**Compound case** — the same entry, **plus**: a side engine fails at t≈8 s (mid entry burn, induced torque),
an 18 m/s crosswind + 12 m/s gust band, a target deck circling at 15 m radius / 60 s period, and a ±1.5 m
sea heave:
```
VERDICT=GOOD   td_v=2.483 m/s   td_lat=0.523 m   tilt=0.600°   maxQ=51.7 kPa   fuel=1859 kg   t=106.1 s
best θ:  h_entry_ign=55521  entry_thr=1.00  fuel_reserve=10342  h_land_ign=9000  Kr=5.735  Kv=1.00  tgo_scale=1.545
```
The trajectory tail shows the vehicle tracking the moving deck to within 0.5 m on a single engine, arriving
upright at 2.5 m/s. CEM found a landing by iteration 2 and refined to GOOD by iteration 27 (of 34).

---

## 6. What this proves

- **Feasibility is settled for the showcase case.** There exists a deterministic, physically‑valid,
  zero‑speed‑at‑zero‑height, on‑the‑moving‑target, upright, structurally‑safe landing for the compound
  engine‑out + wind + moving‑deck scenario — and an offline search finds it in ~30 seconds of CPU time.
- **The engine‑out ceiling is a teacher problem, not a physics wall.** The "distillation is exhausted"
  conclusion was about *weak teachers*. A near‑optimal teacher exists and is cheap to compute.
- **The solutions are distillable.** Because the guidance is closed‑loop and never reads the wind, its
  `(observation → action)` trace is a legal feedback policy — the exact thing a network can learn.

---

## 7. Where this goes from here

### 7.1 Data generation — the "small run" before any rental
Add a `--gen` mode to `oracle.c`: loop over many seeds/scenarios sampling the disturbance product space
(engine‑out timing and which engine, wind speed/gust, target radius/phase, heave state — **seeds kept
disjoint from the gate set {42, 7, 99}**), CEM‑solve each, then re‑fly the winning θ **closed‑loop** and log
`(obs, a*)` rows in the real `policy_tap.h` format. **Verdict‑filter** to keep only V_GOOD/V_PERFECT
trajectories — real, would‑have‑worked solutions. Whole successful trajectories, not cherry‑picked rows.

### 7.2 Distillation — and the robustness litmus
Feed that synthetic‑but‑real dataset to the existing `trainer/train_s0.py`. Then the operator's acceptance
test: **hide the fault flags at evaluation.** If the distilled policy still lands, it is genuinely inferring
the situation from the observable dynamics — not reading a cheat sheet. To make that inference reliable, the
observation should carry the **acceleration residual** (commanded vs achieved specific force — the one
channel that reveals *any* contingency without naming it) and a short memory for online plant estimation.
Drop `eng_health` reliance entirely; it becomes at most redundant.

### 7.3 Scale — the H200 fleet
Generation is embarrassingly parallel and headless (no cross‑scenario dependency; every scenario is a Philox‑
seeded, replayable CEM solve). The plant is clean POSIX C11 + optional CUDA — it builds on a Linux H200 pod
with one architecture flag. Estimated ~$100–200 for full edge coverage; a small local run proves the pipeline
first, exactly as the operator specified.

### 7.4 The bigger arc this unlocks
- **The N3 compound showcase** — engine‑out × gust × moving deck in one descent — gets a policy that can
  actually fly it, plus an honest adjacent out‑of‑frontier failure to demo against.
- **The frontier itself.** A fault‑agnostic, residual‑style network distilled from oracle solutions and then
  RL‑fine‑tuned (warm‑started from this floor) is the credible path to the ~59/60 reachable frontier the
  distillation era could not reach — with the oracle's reachability doubling as a potential‑based reward.
- **A genuinely "handle‑anything" controller.** Because the training distribution is the *product* of all
  contingencies and the teacher is near‑optimal on each, the distilled policy learns one robust adaptive map
  that reads the vehicle and lands — which is the whole point.

---

## 8. Reproduce

```powershell
# from the isolated worktree
cmd /c "C:\bl_opus_oracle\oracle_cem\build_oracle.cmd oracle.c /Fe:oracle.exe"
C:\bl_opus_oracle\oracle_cem\oracle.exe cem all    # ~30 s on 16 cores; prints convergence + best solution
```
The winning runs are preserved verbatim in `cem_clean3.txt` and `cem_compound.txt`.

## 9. Provenance / non‑collision

- Authored by session **LODESTAR** (Opus 4.8) on 2026‑07‑20, in isolated worktree `C:\bl_opus_oracle`
  (branch `opus/robust-oracle`, off `main @ 29fd724`).
- No file in the shared working tree was modified; this report is a new, uniquely‑named artifact. The oracle
  source, build script, and result logs live in the worktree and can be merged or cherry‑picked when desired.
- Trust files over recollection; every number above is from a logged run in the worktree.

---

## 10. Distillation progress — A (DAgger) → B (residual) → the local ">50%" plan (2026-07-20 PM)

The full solver→distill→**fly** loop is built and running end-to-end in the worktree (`oracle.c` modes
`gen` / `dagger` / `fly` / `base`; `train_oracle.py`). The network is 24 self-sensed obs (kinematics +
moving-target + **accelerometer** + last command; **no fault flags**) → 4-channel full command
(throttle, lateral×2, engine-count). The observation is act-independent (verified: `dynamics.c:111`
thrust = `st->y[S_THR]·st->n_eng`), so gen and fly compute it identically — no train/fly skew.

**The improvement progression (touchdown speed on the compound case, lower = softer):**

| stage | reaches ground | touchdown speed | what the fix moved |
|---|---|---|---|
| Pure BC (4 near-optimal trajectories) | 2/8 | 250–290 m/s | — (augers in; breaches qbar) |
| **+ DAgger (fix A)** | **8/8** (monotonic 2→4→5→8 over 3 rounds) | 100–233 m/s | **covariate shift solved** |
| **+ residual policy (fix B)** | ~4/6 | **25–70 m/s** (best 25.9) | **terminal deceleration** |
| target (V_GOOD) | all | <4 m/s | — |

- **Fix A — DAgger** (`dagger` mode): fly the current NN, relabel the states IT visits with the expert
  (saved per-scenario θ), append, retrain. Validated: monotonic 2/8→8/8 to the ground; covariate shift
  solved. This is the load-bearing result.
- **Fix B — residual policy**: ship `command = base_law(obs) + NN(obs)`; train on `oracle − base`. The
  base law (obs-only fixed-gain ZEM + suicide-burn + phase rule) carries the maneuver structure + max-Q
  control; the net learns a small correction. Directionally validated: cut touchdown speed 2–4×
  (100–233 → 25–70 m/s). Caveat: the **stateless base hover-hunts** (min-throttle 1-engine TWR>1 →
  can't hold a slow descent), so it sometimes never touches down, and the residual inherits that. Fixing
  the base's landing commit is "lever 1" below.

**Bottleneck reality:** the local RTX is NOT the constraint — training is seconds. The constraints are
(1) **CEM gen throughput on the CPU** (worsened by ~16 concurrent other-instance Python processes) and
(2) the two algorithm gaps above. The H200 fleet only accelerates data generation; it is **not required
for a >50% compound demo**.

**Local quick-and-dirty plan to >50% (no H200), in leverage order:**
1. **Base-law landing commit** (~30 min): make the base reliably reach the ground (aim a *firm* touchdown
   so it never hovers) → kills the NONEs, and the base does most of the terminal decel so the residual
   shrinks. Highest leverage.
2. **More scenarios** via an overnight local CEM run (30–50 solved ≈ 10× the data; ideally when the box
   is uncontended).
3. **3–5 DAgger rounds** (cheap — no CEM).
4. If terminal precision stalls: a **3-frame stack** into the obs (still trains in seconds locally).

**Estimate:** >50% *survivable* compound landing (on-pad, upright, ~soft) is a realistic **~1–2 session**
local target given the 250→100→25 progression on just 4 scenarios; >50% strict V_GOOD is the stretch
(may want lever 4). H200 is for pushing past that toward the ~59/60 frontier + full-distribution
robustness — a later goal, not a prerequisite.

**Known bug fixed:** scenario-9 wedged an early gen run (a NaN with no guard blocked on a Windows crash
path). Rollout now has finite-state guards + NaN-safe cost, so one bad scenario skips instead of hanging.

*Proceeding with lever 1 (the base-law landing commit).*

---

## 11. The distillation campaign — the honest wall (2026-07-20 evening, FINAL)

After lever 1, I ran the full local program the estimate called for. This section records it honestly: **the
local distillation cannot reach >50% survivable compound landings.** Every architecture and training
variant plateaus at **0/16 survivable**. This is a real, well-characterized wall — not a bug.

### 11.1 Everything tried, and the result

Metric definitions: **landed** = base of vehicle crossed the (moving) deck. **survivable** ≈ V_HARD =
`td_v < 6 m/s` AND on-pad (`td_lat < 26 m`) AND upright (`tilt < 10°`). **best td_v** = softest single
touchdown that run.

| # | approach | landed (of 16) | best td_v | survivable |
|---|---|---|---|---|
| 0 | pure behavior-clone (4 traj) | 2 | 250 m/s | 0 |
| 1 | + DAgger (fix A) | up to **16** | 100 m/s | 0 |
| 2 | + residual over **stateless bang-bang** base (fix B) | ~5 | **9 m/s** | 0 |
| 3 | + terminal-emphasis loss + 16 scenarios + 6 rounds | 12–16 | 40 m/s | 0 |
| 4 | + **frame-stack memory** (lever 4, 72-D input) | 12–14 | 15 m/s | 0 |
| 5 | + residual over **stateful oracle-guidance base** (nominal θ) | 4–8 | **9.9 m/s (on-pad, upright)** | 0 |

- **DAgger (A) is genuinely validated**: it reliably drives the policy from "augers in" to "reaches the
  ground on every scenario" (monotonic 2→16). Covariate shift is solved.
- **Every residual/memory/data/round variant fails to cross 0 survivable.** The softest touchdowns cluster
  at ~9–15 m/s and are *occasionally* on-pad + upright (best: 9.9 m/s @ 8.9 m, 2.8°), but never both-and-<6,
  and never consistently.

### 11.2 Root cause (fully characterized)

The soft hoverslam is a **knife-edge terminal maneuver**. With a single landing engine the minimum-throttle
thrust-to-weight is **>1**, so the vehicle *cannot hold a slow descent* — it must time the burn to arrive at
the deck at ~zero speed *exactly*, or it either hovers (min-throttle climb → limit cycle → no touchdown) or
arrives hot. The oracle achieves this per scenario via **CEM-tuned θ** (ignition altitude, throttle policy,
gains). Distilling that precision into a **6–9k-param net from ~12 scenarios** does not capture it, regardless
of: single-frame vs frame-stack, MSE vs terminal-weighted MSE, stateless vs stateful base, 4 vs 6 DAgger
rounds. It is a **data-volume + capacity wall**, not an architecture bug — and no fixed base helps because
*no single θ generalizes* (that is exactly why the oracle tunes per scenario; a fixed nominal θ lands only
~2/16 alone).

### 11.3 Honest corrections & convergent evidence

- **My earlier estimate was wrong.** The §10 claim that >50% survivable was a realistic ~1–2-session local
  target underestimated the terminal-precision difficulty. The 250→9 m/s *best-case* progression was
  misleading; the *consistent* soft-landing bar is far harder.
- **Independent confirmation:** the sibling oracle instances reached the same conclusion from their own
  distillation runs ("round-0 overfit → needs volume"). Two independent solver→distill efforts converging on
  "needs scale" is strong signal that scale — not a cleverer local trick — is the answer.

### 11.4 What is delivered and reusable (this is real)

- A complete, working **solver→distill→fly pipeline** — `oracle.c` modes `gen`/`dagger`/`fly`/`base`,
  `train_oracle.py`, all CPU-trainable, no fault labels in the observation.
- The **oracle solves the compound case ~100%** (V_GOOD). The *teacher* is proven and is the asset scale
  builds on.
- **Reliable ground-reaching** (16/16) with DAgger.
- A **precisely characterized wall** + root cause, so scale is entered with eyes open.

---

## 12. H200 SCALE-OUT SPEC (the C lever) — on paper

**Thesis.** The terminal-tuning generalization the wall exposed needs (a) **volume** — hundreds–thousands of
solved scenarios so the net sees the θ-tuning across the whole disturbance product — and (b) **capacity** — a
bigger net, and probably an **RL terminal fine-tune** to nail the knife-edge. The local box gave us a proven
teacher and pipeline; scale is about generating enough teacher data and training a big enough student.

### 12.1 What the H200 actually buys (be precise)

The LODESTAR oracle's CEM is **CPU-bound** (the plant rollouts run on CPU cores via OpenMP). So an H200 helps
the *generation* step **only if the oracle rollouts are moved to the GPU**. Two honest paths:

- **Path A — GPU-port the CEM rollouts.** Reuse the project's existing CUDA rollout
  (`core/guidance_mppi_rollout.cuh`) as the template; run the CEM population's rollouts on-device. A sibling
  instance already has a **full-fidelity CUDA oracle port in flight** (multi-arch fat binary, RTX 4070 Ti →
  H100/H200) — the fastest route is to **converge with / reuse that port** rather than re-porting. Then one
  H200 solves scenarios ~10–50× faster than a CPU core, and the fleet fans thousands in parallel.
- **Path B — CPU-scale on many-core nodes.** If the GPU port isn't ready, "scale" = many CPU cores. Rent
  high-core-count nodes and fan the (embarrassingly parallel, Philox-seeded, replayable) scenario CEM solves
  across them. Slower per scenario than GPU but requires zero new code — the current `gen` mode already fans
  by seed.

Training itself is **not** an H200 bottleneck — even a 10× bigger net trains in minutes on one GPU. The H200
fleet is for **generation throughput** (Path A/B) and, later, **RL fine-tuning** (many parallel plant envs).

### 12.2 Dataset target

| | local (done) | H200 target |
|---|---|---|
| scenarios (compound recipes × seeds) | 12 solved | **500–2,000** solved |
| rows (self-sensed obs → full command) | ~0.4 M | **20–100 M** |
| size (280 B/row incl. base) | ~0.1 GB | **6–30 GB** |
| coverage | a thin slice | the EO-time × wind × gust × target × heave product |

Held-out law preserved: gate seeds {42,7,99} never generated; the trainer already hard-refuses them.

### 12.3 Generation parallelism

- Each scenario CEM solve is **independent** (seeded, replayable) → embarrassingly parallel, no cross-node
  comms, no gradient sync.
- Per node: many concurrent solves (process-level fan-out, or a batched-K GPU kernel if Path A).
- Fleet: shard the scenario×seed grid across pods; each writes `.bin` shards to shared/object storage;
  verdict-filter to V_GOOD/PERFECT; concatenate.
- The plant is **clean POSIX C11 + optional CUDA** — the headless path builds on a Linux H200 with one arch
  flag (`-DCMAKE_CUDA_ARCHITECTURES=90`); no Win32 on the `--headless` path (verified earlier).

### 12.4 Compute / cost (order-of-magnitude)

- **Path B (CPU):** ~1–3 CPU-core-min per scenario solve × 1,000 scenarios ≈ 20–50 core-hours → a handful of
  32–64-core nodes for a few hours ≈ **$20–80**.
- **Path A (GPU-ported):** generation collapses to a few H200-hours for the full set → **~$20–60** on the
  fleet plus the one-time port cost.
- **Training / RL:** the distill is minutes; an RL terminal fine-tune (PPO/SAC, parallel envs) is the larger
  GPU consumer if pursued — hours to a day on 1–4 H200s.
- **Total for a >50% attempt at scale: roughly $100–300**, dominated by whichever generation path.

### 12.5 Student architecture at scale

- Bigger net: **128–256 hidden, 3–4 layers** (still tiny by ML standards, byte-deterministic C-exportable),
  frame-stack input retained (memory helps the commit timing once data is sufficient).
- Keep the **self-sensed, no-fault-label observation** (the robustness/"hide-the-flags" property).
- Keep the **residual-over-stateful-base** structure OR move to a **θ-predictor** (see §13) — decide after the
  first scaled distill measures whether the end-to-end residual finally crosses with volume.
- Optional **RL fine-tune** of the terminal on the plant, warm-started from the distilled policy, with the
  oracle's reachability as a potential-based reward — the principled fix for the knife-edge precision if
  imitation alone still falls short at scale.

### 12.6 Pipeline (turnkey)

```
# 1. GENERATE (fleet, Path A or B) — fan scenarios×seeds (seeds 1000+, disjoint from 42/7/99)
oracle.exe gen <N_scen> <POP> <ITERS>        # per pod; writes oracle_data_<shard>.bin (+thetas.bin)
# 2. AGGREGATE shards -> one dataset (concat; verdict already filtered in gen)
# 3. DISTILL (one GPU) — bigger net, frame-stack, residual/theta target
python train_oracle.py <dataset>             # -> oracle_policy.bin
# 4. VALIDATE — held-out gate seeds 42/7/99 x many draws
oracle.exe fly <N>                            # report landed% + survivable% + GOOD%
# 5. (optional) RL FINE-TUNE the terminal, warm-started from oracle_policy.bin
```

### 12.7 Success criteria (honest ladder)

1. **>50% survivable** on held-out compound draws (the immediate goal).
2. **>50% V_GOOD** (soft, on-pad, upright) — the real showcase bar.
3. Toward the **~59/60 frontier** with RL fine-tuning + full-distribution coverage (the original N3 wow).

---

## 13. ALTERNATIVE — the learned-θ-predictor (on paper, not built)

A fundamentally different student that **dodges the terminal-precision wall** by not asking the net to fly the
knife-edge at all.

### 13.1 The idea

The oracle proves that **`guidance()` + the correct per-scenario θ lands softly**. The *hard* thing the
end-to-end net was failing at is reproducing that terminal precision. So: **don't distill the control map —
distill the θ.** The net predicts the 9-D tuning vector θ from the observable state, and the *proven*
`guidance()` controller flies with it. The precision stays in the analytic controller; the net only supplies
the low-dimensional tuning.

```
observation(s)  ->  NN  ->  theta_hat[9]  ->  guidance(state, theta_hat)  ->  command
```

### 13.2 Why it might succeed where end-to-end failed

- **The learning target is 9 smooth numbers, not a knife-edge control law.** Regressing 9 tuning parameters is
  a vastly easier, lower-variance problem than learning the full obs→command map — and θ is *robust* (small θ
  errors still land, because `guidance()`'s feedback absorbs them).
- **The terminal precision is handled by the analytic suicide burn** with the right θ — exactly the thing the
  oracle already does ~100%. The net never has to time the cut.
- The gen pipeline **already saves `thetas.bin`** (scenario → solved θ), so the supervision exists.

### 13.3 The real caveat (and its fix)

θ depends on the disturbances (engine-out time/engine, wind, gust, target motion), which are **only partially
observable, and partly in the future** (at t=0 the engine hasn't failed yet). So a one-shot θ prediction at
t=0 is under-determined. Two resolutions:

- **Rolling θ-estimate (recommended):** the net re-predicts θ every tick from the observation *history*
  (which reveals disturbances as they happen — engine-out shows in the accelerometer, wind in the drift).
  `guidance()` flies with the current θ̂. This is **online system-identification producing the tuning** — and
  it ties directly back to the accel-residual + memory idea that motivated the self-sensed observation. It is
  arguably the "much more intelligent" version: the net *infers the situation* (as a θ) and the controller
  acts on it.
- **Robust-θ:** predict a single θ optimized to land across the *plausible* disturbance set given the early
  observation (a min-max / expected-cost θ), accepting slightly sub-optimal but reliable landings.

### 13.4 Architecture & data

- **Input:** the same self-sensed frame-stacked observation (no fault labels).
- **Output:** θ̂ ∈ ℝ⁹ (de-normalized to the θ bounds), re-evaluated at the guidance rate (or every N ticks).
- **Training data:** log `(observation_history, θ_scenario)` pairs during gen — the observation at each tick
  paired with that scenario's solved θ. Verdict-filter to clean scenarios. `thetas.bin` already has the θ;
  only the per-tick observation pairing needs adding to the tap.
- **Loss:** weighted MSE on θ (or a landing-outcome-weighted loss so θ errors that matter most are penalized).
- **C inference:** identical MLP machinery to the current `nn_forward`, but the output feeds `guidance()`
  instead of being the command. Byte-deterministic, C-exportable, same KAT discipline.

### 13.5 Pros / cons vs the end-to-end goal

- **Pros:** likely lands (dodges the wall); tiny, low-variance learning target; leverages the proven
  controller; small data appetite; still self-sensed / no fault labels; the rolling-θ variant is a clean
  online-system-ID story.
- **Cons:** **not end-to-end NN control** — `guidance()` (a scripted controller) stays in the loop, which is
  a step back from the operator's ultimate "NN flies everything" goal. It's a **hybrid stepping-stone**: a
  reliable compound-landing demo now, with the end-to-end policy pursued later at scale (§12) or via RL.
- **Effort:** small — one new tap field (per-tick obs already logged; add the θ target), a ~30-line trainer
  variant, and a `fly` path that routes NN output → `guidance()`. Buildable in one focused session.

### 13.6 Recommendation

Pursue **§12 (scale)** for the true end-to-end goal. Consider **§13 (θ-predictor)** in parallel as the
fast path to a *working* >50% compound-landing demonstration while scale spins up — it is the most likely
thing to land locally, at the cost of not being fully end-to-end. The two are complementary: the θ-predictor
proves the teacher + observation are sufficient to land; scale then transfers that into the end-to-end policy.
