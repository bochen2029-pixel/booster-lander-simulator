# NEURAL POLICY DESIGN — a learned neural guidance policy π(legal_state) → actuator_cmd, the REACHABILITY-FRONTIER extractor

**Author:** NEURALPOLICY (Opus 4.8) · **Date:** 2026-07-19 (post-D-016 / continuity) · **Status:** ANCHOR design doc — implementer-grade spec, the novel core of a next-gen perception-to-policy stack. No code changed.
**Operator ask:** the most-wanted, never-attempted piece — a LEARNED guidance policy whose "wow factor" is watching the vehicle recover, live, from a mid-descent ENGINE-OUT with compounding wind shear and a moving target. Be maximally rigorous and ambitious.
**Builds on:** the D-016 ENTRY-under-MPPI 95/100 result; the M4 finding that AERO ≥90 is blocked by a PLANT-AUTHORITY ceiling — proven null from FOUR angles (`runs/kprobe_report.md` K-capacity 44/44/42, `runs/mvar_report.md` OU-θ −2/−3, `runs/covo_report.md` anisotropic-Σ flat/harmful, `runs/mpopi_report.md` iterate); the reachability oracle `runs/sandbox/ceiling.c` (D_phys ≈ 1107 m, MPPI realizes ~0.70·D_phys ≈ 775 m); the MPPI expert (`core/guidance_mppi.c`) and the convex alternative (`runs/gfold_research.md`); the three disturbance designs — gust SHIPPED (`--gust`, D-017), engine-out DESIGNED (`runs/engineout_design.md`), moving-target DESIGNED (`runs/target_sandbox_design.md`); the windthink design's rigor template (`runs/windthink_design.md`).
**Canon it must respect** (the NON-NEGOTIABLES, HANDOFF §0-1): directive 1 (state changes only through the integrator; guidance outputs actuator commands, nothing else); directive 2 (determinism sacred — seeded Philox, no unordered FP reductions, bit-identical replay, `--selftest` memcmp oracle); directive 3 (if guidance can't solve it, the vehicle crashes — no assist toward the pad); directive 5 (renderer is a pure observer); directive 7 (ONE dynamics source: any law the plant sees, the rollouts/predictors see — *a direct policy has NO rollout model, which makes this directive nearly free*); directive 9 (TERMINAL byte-identical); §4.3 (guidance reads the LEGAL nav state, never truth-wind / truth-target / hidden disturbance).

> **DESIGN + ANALYSIS ONLY. Nothing in `core/` was edited; no cmake/binary was run against the real tree.** Every code injection is specified by **file + function + line-neighborhood** so a build agent implements without re-deriving. This doc is the anchor for two peer lanes: `perception` (a VLM front-end that produces a legal state estimate) and `interplanetary` (world-param + master integration). Their work references the interfaces defined here (§C observation/action, §F determinism export, §H roadmap).

---

## 0. Executive verdict — the thesis, stated once

**A small state-based MLP, trained offline and frozen into a bit-deterministic C-header forward pass, is the credible path to close the gap between what the controller REALIZES (~0.70·D_phys) and what physics ALLOWS (the backward-reachable set). It is safe to build because the FIRST rung distills the MPPI expert to MPPI quality at NN speed with a bit-exact determinism export — a rung that pays for the whole pipeline even if RL never exceeds the expert. It is ambitious because the LATER rungs target the reachability frontier under the JOINT disturbance distribution (engine-out × shear × moving target × plant dispersions), which no method on this project has attempted, and which is exactly the operator's dream showcase.**

Seven load-bearing claims, each defended below:

1. **The yardstick is the reachability frontier, not a fixed success rate** (§A). The right metric is *recovery-rate-vs-frontier*: recover whenever the state is inside the backward-reachable set (BRS) of a safe landing; crash when it is outside (directive 3). `ceiling.c` already computes the frontier's *lateral* slice (D_phys); §A generalizes it to a per-scenario reachability oracle and defines the metric precisely. **A policy that recovers 100% of in-frontier states and 0% of out-of-frontier states is optimal — even if its raw landed-rate is below 100%, because the out-of-frontier states are *physically unlandable*.**

2. **The plant IS the training environment.** The fast deterministic C plant does millions of headless steps/sec; the bottleneck of RL here is *sim throughput*, not the tiny net (§E). The plant already composes disturbances (wind SHIPPED, engine-out + moving-target DESIGNED and reduced to two-scalar/one-substitution plant writes) — the joint distribution is a domain-randomization config, not new physics.

3. **DISTILL first, RL second, RESIDUAL optionally** (§B). Supervised regression of the MPPI (or convex G-FOLD) expert action across millions of scenarios gives MPPI-quality at NN speed, is safe/validatable/determinism-clean, and PROVES the determinism export. Then PPO/SAC on the joint distribution to EXCEED the expert toward the frontier. The residual-on-hoverslam variant is the safety/legibility fallback.

4. **The observation is the LEGAL nav vector; the action is the GuidanceCmd** (§C), both mapped line-by-line to the code. The policy reads `nav_measure`'s perturbed view (r/v/q/ω + fuel + engine-health flag + target estimate + uncertainty) — NEVER `wind_world`/`wind_filt`/truth-target. It emits `a_lat[2]` + throttle (+ optionally `engine_cmd`/`n_eng`), the exact fields `control_step` consumes.

5. **State-based guidance is tiny and fast** (§B.4). ~4-layer MLP, ~64-128 units, <10k-40k params → <10 µs inference, orders of magnitude inside the 500 Hz (2 ms) control budget and the 50 Hz (20 ms) guidance budget. This is why it can be a *direct* policy (directive-7-free: no rollout model) and why it contrasts sharply with Tesla-FSD-class image nets (§B.4) — perception is expensive, state→policy is nearly free.

6. **The determinism export is the project-defining move** (§F). Trained → FREEZE weights → export as a C header of `float`/`double` arrays → a hand-rolled fixed-order MLP forward pass in C (no atomics, no cuDNN nondeterminism) → BIT-DETERMINISTIC inference, golden-able, a versioned constant like any tuned coefficient (KDIV_SEEK, Kvel). It slots in as a new `GM_NEURAL` mode beside `GM_HOVERSLAM`/`GM_MPPI`. The anti-cheat reconciliation (§F.4): reads legal state + emits actuators + frozen weights + offline training = "precompute in, telemetry out"; it re-derives every tick, so it is NOT a scripted trajectory.

7. **The one real honesty risk is sim-overfitting** (§F.5). The offline trainer optimizes the policy against *this plant*; a policy that exploits a plant quirk is a cheat by another name. Mitigations: domain randomization over disturbances + plant params, held-out seeds/conditions, and — decisively — **the same MC gates every other guidance change passes** (selftest, TERMINAL 194, cross-seed, nav-noisy, the frontier metric). A policy that beats MPPI on held-out seeds under domain randomization is not overfit; it is better.

**Recommended path (each rung independently gated, §H):** **S0** distill-from-MPPI → match 44/60 AERO at NN speed (proves pipeline + determinism export). **S1** RL single-disturbance → beat MPPI on one axis. **S2** RL joint distribution → the compounding-recovery frontier + the M4-gap attempt (can it exceed 0.70·D_phys?). **S3** multi-world (hand to lane `interplanetary`). Build S0 into the honest tree behind `GM_NEURAL`; gate every rung on the frontier metric and the determinism export.

---

## A. PROBLEM FRAMING — a policy targeting the reachability frontier

### A.1 The object

Define a policy
```
π_θ : O → A ,     a_t = π_θ(o_t)
```
where `O` = the LEGAL observation space (the nav view + legal auxiliaries, §C.1), `A` = the actuator-command space (a subset of `GuidanceCmd`, §C.2), and `θ` = frozen network parameters (§F). It runs at the 50 Hz guidance tick (the `is_gtick` cadence, `sim.c:257`), producing a `GuidanceCmd` that `control_step` (500 Hz, `control.c:30`) allocates to the physical effectors. Between guidance ticks the last command is held (exactly as MPPI's `mppi_execute` holds a knot). **The policy is a direct feedback map — no rollout, no online optimization, no plan tail.** That is the whole point: it is the *compiled* answer to the trajectory-optimization problem MPPI solves online, evaluated in <10 µs.

### A.2 The disturbance distribution it must be robust to

The policy must land across the JOINT distribution
```
D = D_scenario  ×  D_wind  ×  D_engineout  ×  D_target  ×  D_plant
```
- **D_scenario** — the IC dispersion: AERO_OFFSET mean 500 / σ150 (`scenario.c`, D_phys ≈ 1107 m), ENTRY mean 3000 / σ250, TERMINAL 2 km. Lateral offset + altitude + descent-speed spread.
- **D_wind** — mean profile (`u_ref` 6 AERO / 8 ENTRY, power-law × jet amplification) in a per-run azimuth + Dryden AR(1) turbulence (`sim.c wind_sample`). SHIPPED as the default, plus `--gust` 1-cosine shear (D-017). The wind is HIDDEN (§4.3): the policy feels it through state, never reads it.
- **D_engineout** — a seeded, time-triggered side/center engine failure in the 3-engine entry burn: `n_eng` decrement (3→2, thrust + mdot + allocation) + `thrust_offset[2]` induced torque (`runs/engineout_design.md` §B; the plant edits are `arm_thr` at `dynamics.c:137` + a time-triggered `sim.c` event + the `a_burn` `3.0→st->n_eng` authority edit — see that design's §9 line map). Engine health IS legal (chamber-pressure flag, §4.3-legal analog).
- **D_target** — a seeded moving target: `target_xy(t)` (SEA deck wander / scripted drift) + `deck_z(t)` heave (`runs/target_sandbox_design.md` §A). The current pose is legal (§8.1 "Deck pose is part of NavState"); future motion is withheld by default.
- **D_plant** — thrust/Isp/CoM dispersions on the `--inject` channel (`sim.c:65-76`: up to −8% thrust, −1% Isp, 2 cm lateral CoM) + sensor bias (nav.c inject) + gyro-bias walk under `--nav-noisy`.

The compounding showcase (§G) is a *single draw* from the tail of `D` where all four fire at once: engine-out mid-entry-burn, a simultaneous gust, a moving target, on dispersed plant params.

### A.3 The reachability yardstick — the precise metric (THE contribution of this section)

The naive metric ("landed rate") is WRONG for a frontier-targeting policy, because some ICs are *physically unlandable* (directive 3 says they must crash). The correct metric is defined against the **backward-reachable set (BRS)**.

**Definition (BRS of a safe landing).** For a scenario and a disturbance realization `d ∈ D`, let `S_land` be the set of terminal states graded `V_PERFECT ∪ V_GOOD` (on-pad, `td_v ≤ TD_V_HARD`, upright — `set_verdict`, `sim.c:86-105`). The BRS at time `t` is
```
BRS_t(d) = { x : ∃ admissible control u(·) s.t. the plant flown from x under d reaches S_land }
```
i.e. the set of states from which SOME admissible controller lands. Its boundary is the **reachability frontier**. A state on the wrong side of the frontier cannot be landed by *any* controller — the honest crash of directive 3.

**The oracle that computes it (we already have the lateral slice).** `runs/sandbox/ceiling.c` computes the frontier's lateral projection D_phys by solving the time-optimal, velocity-capped, rest-to-rest bang-bang under the true time-varying lateral authority `a_max(t)` along the integrated vertical trajectory — the maximum landable initial offset. This IS a BRS boundary computation (the largest `|r_xy(0)|` in BRS_0). Generalize it:
- **Lateral frontier** (built): `D_phys(h0, vz0) ≈ 1107 m` nominal AERO; the 3×3 grid in `ceiling.c:552-579` gives its dispersion.
- **Engine-out frontier** (new, §A.4): the same oracle re-run with reduced authority (`a_burn` at `n_eng=2` instead of 3, plus the divert-time lost to trim) → a SMALLER D_phys. This is the *temporal/authority* ceiling the engine-out adds on top of the lateral one (`runs/engineout_design.md` §8.2).
- **Moving-target frontier**: the target displacement `|target_xy(t_td)|` shifts the effective offset the divert must null; the frontier is `D_phys` measured relative to the *touchdown* target position.

**The metric — recovery-rate-vs-frontier.** For a held-out batch of `(seed, d)` draws, label each IC by the oracle as IN-frontier (`|effective_offset| ≤ D_phys(d)`) or OUT-of-frontier. Then report the 2×2 confusion table:

| | policy LANDS | policy CRASHES |
|---|---|---|
| **IN-frontier** (landable) | ✅ **recovered** (the number that matters) | ✗ missed a landable state (the policy's shortfall) |
| **OUT-of-frontier** (unlandable) | ⚠ "landed" an unlandable state — a plant/oracle bug or an assist leak (investigate!) | ✅ correct crash (directive 3) |

**THE metric is the top-left cell: the recovery rate = P(land | in-frontier).** A perfect policy scores 1.0 there and 0.0 in the bottom-left-to-top-right off-diagonal. **This decouples "how good is the controller" from "how hard did we make the scenario"** — the exact confound that has dogged every landed-rate number in this project (`ceiling.c` itself flags it: mean-500 is "well-posed ONLY if MPPI is improved toward D_phys"). It also gives a crisp M4 restatement: **AERO ≥90 is achievable iff the recovery rate against D_phys can be pushed past ~0.70, because at mean-500/σ150 the p90 offset (692 m) sits at ~0.63·D_phys, so the seeds that miss today are the ones between 0.70·D_phys (what MPPI realizes) and D_phys (what physics allows).** Closing that band IS the frontier-extraction thesis.

**Two sub-metrics** (report alongside): (i) **realization fraction** = median landed `|effective_offset|` / D_phys (MPPI ≈ 0.70; the policy's target is → 1.0⁻); (ii) **arrival quality** on the landers (td_v, td_lat) — a policy that reaches the frontier but arrives hot has traded the wrong thing (the D-012 op→th conversion trap, §D.4).

### A.4 The engine-out frontier (the showcase's yardstick, made concrete)

An engine-out is an *authority* change that shrinks the BRS in two ways (`runs/engineout_design.md` §8.2, §B.4): (a) 33% thrust loss → `a_burn` drops from ~50 to ~32 m/s² at 2 engines → the ZEM/ZEV divert reaches less; (b) the gimbal spends ~60% of its ±5° cone holding the induced-torque trim, leaving less for steering. **So the engine-out frontier is a strict subset of the nominal frontier.** The oracle computes it by re-running `ceiling.c`'s `a_max(t)` build with the post-failure authority (a two-line edit: `n_eng`-scaled thrust in `build_amax`, and a trim-authority debit during the burn window). A seed that lands at 3 engines but sits *outside* the 2-engine frontier is a *correct* crash under engine-out — and the showcase must show exactly this boundary (§G): the policy recovers the in-frontier engine-outs and honestly loses the out-of-frontier ones, scored against the shrunken BRS.

---

## B. ARCHITECTURE — recommend and justify

Three training approaches, in the order they should be built. **Recommendation up front: DISTILL (B.1) → RL (B.2) → RESIDUAL (B.3) as an optional safety wrapper. Distill first is non-negotiable — it de-risks the entire pipeline and delivers the determinism export before any RL instability can bite.**

### B.1 Approach 1 — DISTILLATION (supervised regression of the expert) — BUILD FIRST

**Idea.** The MPPI controller (or the convex G-FOLD oracle, §B.1.3) is a *slow, correct* teacher. Run it across millions of scenarios drawn from `D`, log `(o_t, a_t*)` pairs where `a_t*` is the expert's executed `GuidanceCmd`, and train `π_θ` by supervised regression to imitate it: `min_θ E_{(o,a*)∼D}[ ‖π_θ(o) − a*‖²_W ]` with a per-channel weight `W` (throttle vs a_lat scaled to comparable units, §C.3).

**Why first, and why it is safe:**
- **It cannot be worse than the teacher by construction** (up to approximation error). MPPI already lands 44/60 AERO, 95/100 ENTRY, 194/200 TERMINAL. A faithful distillation inherits those rates — a KNOWN-GOOD floor, unlike RL which can diverge to a policy that crashes 100%.
- **It is the determinism-export proof carrier** (§F). The distilled net is small, frozen, and exported to a C-header MLP; S0's gate is "does the C-header `GM_NEURAL` reproduce the MPPI landed-rate within tolerance, bit-deterministically?" — a hard equality check that validates the ENTIRE freeze/export/inference-in-C machinery before any RL exists.
- **It is a massive speedup.** MPPI is ~9 s/run (256 rollouts × 200-step lean rollouts × 10 Hz replan). The distilled policy is a single <10 µs forward pass per tick. **This alone is a headline: "MPPI-quality guidance at 1000× the speed"** — and it makes the CUDA MPPI port (K=16384, ~65 ms/replan) unnecessary for *deployment* (the port stays valuable as a *teacher* and an oracle, §B.1.3).
- **DAgger for the covariate-shift fix.** Pure behavior cloning suffers covariate shift (the policy visits states the expert never showed it). The standard fix is **DAgger** (Dataset Aggregation): roll out the *current* policy, query the *expert* on the states the policy actually visits, add those `(o, a*)` pairs, retrain, iterate. Because the expert (MPPI) is available *online* in the same C plant, DAgger is cheap here — run the policy in the loop, and at each tick also run one MPPI replan to get the label. **This is the recommended distillation protocol, not vanilla BC.** (Cost note: the DAgger label pass re-introduces MPPI's 9 s/run *during data collection only*; inference stays fast. Budget the teacher cost into the data-generation phase, §E.)

**B.1.3 — the teacher choice (MPPI vs convex G-FOLD).** Two candidate experts:
- **MPPI** (`core/guidance_mppi.c`) — the shipped, gated, determinism-clean expert. It targets *landing probability under dispersion* (its terminal cost is exactly the frontier objective: TD_RXY off-pad + TD_VXY carry-through + CRASH_COST). **Recommended primary teacher** — it is already tuned for our objective, already handles the wind via replanning, and is available in-process for DAgger.
- **Convex G-FOLD oracle** (`runs/gfold_research.md`, Rank-1: build offline) — a *fuel-optimal* SCvx-with-aero solve. `gfold_research.md` §C already recommends it as an offline oracle + **superior warm-start generator** ("a convex-optimal warm-start is strictly better than the hand-tuned sqrt-profile warm-start"). **Recommended as a SECONDARY teacher for the in-frontier reach cases** — the convex solve finds the true optimum of its model, so distilling it teaches the policy the *optimal reach allocation* MPPI only approximates. Caveat (`gfold_research.md` obs. 2, Ridderhof/Tsiotras): the min-fuel convex optimum rails the throttle with zero disturbance margin, so distilling *only* G-FOLD gives a fragile policy — blend it with MPPI labels (MPPI supplies the robustness, G-FOLD supplies the reach). **Verdict: MPPI-primary DAgger, with G-FOLD labels mixed in on the far-offset seeds where reach is the binding constraint.**

**Distillation's honest ceiling.** A distilled policy is bounded by its teacher: if MPPI realizes 0.70·D_phys, so does its clone. **Distillation ALONE does not close the M4 gap** — it delivers MPPI-quality fast, which is a huge win, but exceeding the expert is RL's job (B.2). State this plainly so S0 is not oversold: S0's deliverable is *the pipeline + the speedup + the determinism export at MPPI parity*, not the frontier.

### B.2 Approach 2 — REINFORCEMENT LEARNING (exceed the expert, approach the frontier)

**Idea.** Train `π_θ` by RL (PPO or SAC) directly on the joint distribution `D`, with the reward of §D, to MAXIMIZE landed-rate-vs-frontier — including on states MPPI mishandles. RL can exceed the teacher because it is not imitating a suboptimal expert; it is optimizing the true objective against the true plant.

**Why RL is the frontier lever (and distillation is not):** the M4 finding is that MPPI is *proposal-support-limited* in the aero/thrust crossover dead-zone (`covo_report.md` §4: "the sampled set lacks a trajectory that reaches the far seed AND arrives nulled"). A trained policy has NO sampling — it has *memorized* (in its weights) a direct map from state to the reach-optimal action, learned by trial-and-error across millions of episodes that *did* find the reaching maneuver. **Where MPPI must rediscover the bang-bang reversal by sampling every replan (and misses it in the dead-zone), the policy has it compiled.** This is the precise mechanism by which a policy can exceed 0.70·D_phys: it is not sampling the dead-zone, it is *reacting through it* with a pre-learned law. (Honesty check, §H-S2: this is a *hypothesis* — the dead-zone might be a genuine plant-authority wall no controller crosses, in which case the policy matches the frontier D_phys and the M4 gap was always dispersion-retune, per `ceiling.c`'s own recommendation. The frontier metric §A.3 settles which: if the policy hits ~1.0·D_phys realization it found the authority; if it plateaus at 0.70 the wall is physical.)

**Algorithm choice — SAC recommended, PPO acceptable:**
- **SAC (Soft Actor-Critic)** — off-policy, sample-efficient (reuses a replay buffer), continuous-action-native, entropy-regularized (good exploration through the dead-zone). **Recommended** because sim throughput, not sample efficiency, is our bottleneck — but SAC's replay reuse means fewer *expensive* teacher-free episodes, and its stochastic policy explores the reach maneuver better. The continuous `a_lat`/throttle action space is SAC's home turf.
- **PPO (Proximal Policy Optimization)** — on-policy, extremely robust/stable, trivially parallel (matches the plant's millions-of-steps/sec throughput — you can run thousands of parallel envs and PPO eats the on-policy data). **Acceptable and simpler to stabilize**; the standard choice for massively-parallel sim (Isaac-Gym-style). If SAC's off-policy instability bites, fall back to PPO with a large parallel-env count.
- **Warm-start RL from the distilled policy** (B.1): initialize the RL policy's weights from the S0 distilled net. This starts RL at MPPI-quality (not from noise), so RL only has to *improve* from a good baseline — dramatically faster and safer than tabula-rasa RL, and it means S1/S2 never regress below the distilled floor if early-stopped. **This is the recommended coupling of B.1→B.2: distill to bootstrap, RL to exceed.**

**RL's risks (managed in §F.5, §H):** reward hacking (§D.5), sim-overfitting (§F.5), training instability (mitigated by distill-warm-start + PPO fallback), and the determinism export applying to the *frozen* final policy only (RL training may be nondeterministic; the *shipped* artifact is frozen and re-golden'd, §F).

### B.3 Approach 3 — RESIDUAL (NN correction on the hoverslam baseline) — the safety/legibility variant

**Idea.** Instead of the net emitting the full command, it emits a *correction* on a known-safe analytic baseline:
```
a_lat = a_lat_hoverslam(o)  +  clamp(π_θ(o), ±Δ_max)
```
where `a_lat_hoverslam` is the tier-0 divert law (`hoverslam_step`) and `π_θ` outputs a bounded residual. **This is the residual-RL / residual-policy-learning pattern.**

**Why it is attractive (and why it is the fallback, not the default):**
- **Safety-by-construction.** With `Δ_max` bounded, the policy *cannot* command more than `Δ_max` away from the proven hoverslam law — the worst case is degraded, never catastrophic. This directly addresses the directive-3 anxiety: an out-of-distribution observation produces a bounded deviation, not a wild command.
- **Legibility.** The baseline is human-readable (the sqrt-decel divert); the residual is a small learned correction whose magnitude is a live diagnostic ("the policy is fighting the baseline by X m/s²"). This maps onto the existing MPPI blend structure — `mppi_execute` already does `a_lat = s·MPPI + (1−s)·hoverslam` (`guidance_mppi.c:812`), so a residual policy is a *drop-in for the MPPI half of that blend*.
- **Determinism/parity inheritance.** Because the baseline is `hoverslam_step` (already directive-7-mirrored), the residual only needs the net's forward pass to be deterministic (§F) — the analytic half is already golden.

**Why NOT the default:** the residual is *capped*, so it cannot reach the frontier if the frontier requires a command far from the hoverslam baseline (which, in the dead-zone, it does — the whole point is that hoverslam *under-diverts* there). A capped correction on an under-diverting baseline is still under-diverting. **Verdict: build the residual as the S1 SAFETY variant** (a policy you can ship with confidence because it degrades gracefully) **and as a legibility demo, but use the FULL policy (B.2) for the frontier-extraction S2.** Offer both; recommend residual for the conservative "ship it now" path and full-policy for the ambitious "beat the frontier" path.

### B.4 Network topology — small MLP, sized for <10 µs, and WHY state-based is tiny

**Recommended topology:**
```
input:   o ∈ ℝ^{~20-28}   (the normalized legal observation, §C.1)
hidden:  3-4 fully-connected layers, 64-128 units each, tanh or ReLU activation
output:  a ∈ ℝ^{2-4}      (a_lat[2], throttle, [optional engine_cmd logit], §C.2)
params:  ~10k-40k weights (e.g. 24→128→128→128→3 ≈ 4.4k+16.5k+16.5k+0.4k ≈ 38k)
```

**Why this size:**
- **Expressivity.** The map from a 20-D kinematic state to a 2-D reach-optimal lateral accel is a smooth, low-dimensional function (MPPI computes a continuous approximation of it every replan). A 3-4 layer MLP with 64-128 units is comfortably over-parameterized for this — the aerospace RL literature (and the MPPI-distillation literature) lands landing/hovering policies at exactly this scale.
- **Speed — the decisive constraint.** 38k params, all dense fp32 (or fp64, §F.3) matrix-vector products: ~38k multiply-adds per forward pass. At even a conservative 1 GFLOP/s scalar C throughput that is **~38 µs worst-case, realistically <10 µs** with any vectorization — and it runs at 50 Hz (20 ms budget), so it uses **<0.05% of the guidance tick**. Even if promoted to the 500 Hz control tick (2 ms budget) it is <0.5%. **Inference cost is a non-issue by three orders of magnitude.** (Contrast MPPI's ~9 ms CPU replan or ~65 ms fp64 CUDA replan — the policy is 1000× cheaper.)
- **Determinism-friendliness.** A fixed-topology dense MLP is trivially expressible as a fixed-order loop of multiply-accumulates in C (§F.2) — no dynamic graph, no data-dependent branching, no reduction-order ambiguity. Small nets are *easy* to make bit-exact.

**Why state-based guidance is tiny/fast — the contrast with Tesla FSD image nets (the operator's framing):**
- **Tesla FSD** processes multi-camera *images* at ~30 Hz: the input is millions of pixels, the network is a deep convolutional/transformer backbone with **tens-to-hundreds of MILLIONS of parameters**, and inference needs a dedicated NPU/GPU. The expense is *perception* — extracting a scene representation from raw pixels.
- **This policy** processes a ~20-float *state vector* (position, velocity, attitude, rates, fuel, health, target) that the NAV solution already produced. There is no perception to do — the state IS the representation. So the network is ~4 orders of magnitude smaller (38k vs ~100M params) and runs on a single CPU core in µs.
- **The architectural lesson for the perception-to-policy stack** (this is where lane `perception` connects): **separate perception from policy.** A VLM/image front-end (lane `perception`) produces a *legal state estimate* (the ~20-float nav-like vector, possibly with an uncertainty); THIS policy consumes that estimate. Tesla fuses them into one giant image→control net at 30 Hz because their perception is the hard part and their control is comparatively simple. Here the split is cleaner and better: expensive perception at whatever rate the front-end runs, cheap state→policy at 50-500 Hz. **The policy this doc specifies is the "to-policy" half; it is deliberately tiny because the "perception-to" half carries the representational load.** (Concretely: lane `perception`'s output schema should MATCH §C.1's observation vector so its estimate drops into `π_θ` with no glue.)

---

## C. STATE + ACTION SPACES — precisely mapped to the code

### C.1 Observation = the LEGAL nav vector (+ legal auxiliaries)

The policy reads the output of `nav_measure` (`core/nav.c`) — the perturbed `State` view whose kinematic fields (indices `S_RX..S_WZ` = 0..12) are the sensed estimate and whose other fields are truth pass-through (`nav.h` documents the split exactly). **The policy NEVER reads `wind_world`, `wind_filt`, or the truth target** (§4.3 — even though `wind_filt` is in the pass-through set, it is the *plant's* Dryden integrator, truth bookkeeping; reading it is a canon violation, flagged loudly in `windthink_design.md` §6 and here).

**The observation vector `o` (normalized, §C.3), field-by-field from `nav.y` and legal auxiliaries:**

| # | quantity | source (nav view / legal) | frame | notes |
|---|---|---|---|---|
| 1-2 | horizontal offset from target `(r_xy − target_xy)` | `nav.y[S_RX,S_RY] − g_target` | world | target-relative (§C.4); target from §8.1-legal pose |
| 3 | height above deck `h = r_z − com − deck_z` | `nav.y[S_RZ]`, `mass_props`, `deck_z_live` | world | the vertical state guidance actually uses (`control.c:37`) |
| 4-5 | horizontal velocity `v_xy` | `nav.y[S_VX,S_VY]` | world | (optionally target-relative for a fast target, §C.4) |
| 6 | vertical velocity `v_z` | `nav.y[S_VZ]` | world | |
| 7-9 | attitude as tilt vector `(sinθ·axis)` or the body +Z-in-world `zb2w` | `q_rot(nav.y[S_QX..S_QW], [0,0,1])` | world | 3 numbers; avoids quaternion double-cover (feed `zb2w`, not raw q) |
| 10-12 | body angular rate `ω` | `nav.y[S_WX,S_WY,S_WZ]` | body | gyro output = truth + bias walk under nav-noisy |
| 13 | total propellant mass | `nav.y[S_MLOX] + nav.y[S_MRP1]` (pass-through truth) | — | fuel-awareness (the "burn longer" signal, §D) |
| 14 | Mach or speed/`a(h)` | derived from `v` + `atmo_eval(r_z)` | — | tells the policy the aero/thrust regime (crossover ~22 kPa) |
| 15 | dynamic pressure `qbar` | `0.5·ρ·|v|²` | — | the authority regime (aero vs thrust dominance) |
| 16 | `fins_deployed` flag | pass-through | — | 0/1; TERMINAL=0 gates the policy off it (directive 9) |
| 17 | `engine_on` + `ign_timer` (or a phase one-hot) | pass-through | — | ignition state (the damp-through-ignition regime) |
| 18-20 | **engine-health flags** `eng_health[3]` (equiv. `n_eng`) | pass-through (§4.3-LEGAL) | — | THE engine-out signal (`engineout_design.md` §C.2: chamber-P is legal) |
| 21-22 | target estimate uncertainty `σ_target` (optional) | from the nav/perception layer | — | so the policy can hedge on a poorly-surveyed deck (§C.4, lane `perception`) |
| 23 | `relights_left` | pass-through | — | can it relight? (feasibility) |
| 24 | ignition-altitude margin `h − ignite_h` | `bl_predict_ignite_h(nav)` (read-only, legal) | — | the aero-aware ignition foresight MPPI precomputes; a strong feature |

**~20-24 features.** All are either the perturbed nav kinematics or legal pass-through / derived-from-legal quantities. **Provenance rule for the build agent:** a feature is legal iff it is computable from `nav_measure`'s output + `atmo_eval` + the §8.1-legal target pose + the §4.3-legal engine-health flag. If a candidate feature needs `wind_world`/`wind_filt`/truth-target, it is ILLEGAL — drop it (the policy must *feel* the wind through the kinematic state, exactly as every other guidance layer does).

**Optional history / recurrence (deferred).** A pure feedforward MLP on the instantaneous state is Markov-optimal IF the nav view were the full state — but the wind is an unobserved disturbance with memory (Dryden AR(1)). A short observation history (stack the last k nav views) or a tiny GRU could let the policy *infer* the wind from the state sequence (the observability the `windthink` estimator exploited: attitude + inertial v encode the relative wind). **Recommendation: start FEEDFORWARD (Markov on the instantaneous nav view) — it matches MPPI, which is also memoryless-per-replan. Add a k=3-5 frame stack in S2 ONLY if the frontier metric shows wind-rejection is the binding shortfall.** A stacked-frame MLP stays tiny (input grows to ~60-100 floats, still <50k params) and stays feedforward (determinism-trivial); a GRU adds recurrent state that must be carried and reset per run (determinism-manageable but more surgery — defer).

### C.2 Action = the GuidanceCmd fields the plant consumes

The policy emits a subset of `GuidanceCmd` (`guidance.h:6-16`). **Trace of what the plant actually consumes** (`control_step`, `control.c:41-44` + the a_lat mapping `:82-125`):
```c
act->throttle   = g->throttle;      // control.c:41  -> engine thrust
act->engine_cmd = g->engine_cmd;    // control.c:42  -> ignition latch (sim.c:269)
act->n_eng      = g->n_eng;         // control.c:43  -> thrust×n_eng + gimbal allocation denom
act->deploy_cmd = g->deploy_cmd;    // control.c:44  -> legs
// g->a_lat[2] -> steer_sign-corrected -> zdes tilt -> attitude PD -> gimbal/fins (control.c:82-125)
```

**Recommended action space (two tiers):**
- **Tier A (RECOMMENDED primary) — `a = (a_lat[0], a_lat[1], throttle)` ∈ ℝ³**, matching MPPI's channels EXACTLY (MPPI is lateral-only + throttle; `guidance_mppi.h:32` `MPPI_NCH 3`). The policy owns the world-lateral steering accel and the throttle; ignition timing (`engine_cmd`) and legs (`deploy_cmd`) are handled by the same analytic triggers MPPI uses (the aero-aware `compute_ignite_h`, the `LEG_DEPLOY_H` height gate) OR learned as Tier B. **This is the cleanest first action space** — it drops into the exact interface MPPI already validated, and `control.c`'s allocation + tilt-cap + steer-sign crossover machinery is inherited unchanged.
- **Tier B (ambitious, S2) — add `engine_cmd` (a 0/1 gate, via a sigmoid logit thresholded) and/or `n_eng`** so the policy learns the IGNITION TIMING and (for engine-out) the *authority re-sizing* itself. This is the "re-solve the burn" beat (§G): a policy that learned to ignite later / burn longer under engine-out. **Add Tier B only after Tier A works** — a learned ignition gate is a discrete action that complicates the continuous-control RL and risks the min-throttle climb trap (HANDOFF §3 latent item); start with the analytic ignition trigger and let the policy own only the continuous channels.

**What the policy does NOT emit:** gimbal/fin/RCS commands (those are `control.c`'s allocation job — the policy stays at the guidance layer, directive 1: "guidance outputs actuator *commands*" = the `GuidanceCmd`, and the inner loop maps them to effectors). This preserves the HIER abstraction MPPI uses (guidance solves 3-DoF translation; the inner loop owns attitude — `gfold_research.md` notes this is the aerospace-standard split).

### C.3 Normalization and frames

- **Normalization (critical for NN training AND determinism).** Each observation feature is scaled to O(1) by a FIXED, frozen affine transform `õ_i = (o_i − μ_i)/s_i` with `(μ_i, s_i)` chosen from the scenario's natural scales (reuse MPPI's reference scales: `R_REF=40` m for position, `V_REF=8` m/s for velocity, `W_REF=0.30` rad/s for rate, `TILT_REF=10°` — `guidance_mppi.c:86-89`; height by ignition altitude ~3 km; fuel by initial prop ~10 t). **These normalization constants are FROZEN with the weights and exported to the C header** (§F) — they are part of the deterministic artifact, not runtime-computed. Actions are de-normalized symmetrically: the net outputs `ã ∈ [−1,1]` (tanh), mapped to `a_lat ∈ ±A_LAT_GAMUT` (3.2 m/s², the physical gamut `guidance_mppi.c:40`) and `throttle ∈ [ENG_THR_MIN, 1]`. Clamping to the physical gamut is done in C after the forward pass (exactly as MPPI clamps `ubar` to `±A_LAT_GAMUT` — the D-009 lesson that railing beyond the gamut kills the gradient applies to the policy's output range too).
- **Frames.** `a_lat` is **world** lateral accel (`guidance.h:8` "desired world lateral accel") — the policy outputs world-frame steering, matching MPPI and the `control.c` steer-sign machinery. Attitude is fed as `zb2w` (body +Z in world) to avoid quaternion double-cover discontinuities that hurt NN training. Angular rate `ω` stays body-frame (its natural sensor frame). **The target-relative offset `(r_xy − target_xy)` makes the policy translation-invariant in the pad frame** — the same trick that makes the moving-target substitution one line (`target_sandbox_design.md` §B.1): the policy learns "null the offset," and the offset is measured to wherever the target is.

### C.4 The target and its uncertainty (the moving-target hook, lane `perception` connection)

For the moving-target showcase, the observation's offset is target-relative (features 1-2, 4-5). The target pose arrives via the §8.1-legal path: `sim.c` fills `g->target_xy` from the seeded `target_sample`/SEA or the operator drag (`target_sandbox_design.md` §B.3), and the policy subtracts it. **This is where lane `perception` plugs in**: a VLM that *sees* the deck produces the target estimate (position + an uncertainty `σ_target`, features 21-22); the policy consumes it exactly like the seeded target, and the uncertainty lets the policy hedge (aim conservatively at a poorly-localized deck). **The policy code is identical whether the target is seeded, dragged, or vision-estimated** — only the *source* of `target_xy` differs, exactly the symmetry `target_sandbox_design.md` §M2.1 flags as the design's cleanliness. For a *fast* target, feed velocity target-relative too (`v_xy − target_vxy`) and lead the aim (§C.1 note; `target_sandbox_design.md` §B.1 caveat).

---

## D. REWARD FUNCTION — the RL objective (and the distillation loss)

The distillation loss (B.1) is just the imitation error `‖π_θ(o) − a*‖²_W`. The RL reward (B.2) must encode "land on the moving target, softly, upright, fuel-aware, within authority, without cheating." **Recommendation: a sparse-terminal + dense-shaped reward, with the shaping designed to be potential-based (no perverse incentive) and the terminal carrying the true objective.**

### D.1 Terminal reward (at touchdown / episode end) — the TRUE objective

Fired once, at the ground-crossing event (`sim.c` touchdown capture) or timeout:
```
R_terminal =
   + W_land  · 1[landed]                          // the binary success (V_PERFECT | V_GOOD)
   − W_rxy   · (td_lat / R_REF)²                  // target-relative miss distance (§A.3 metric)
   − W_vxy   · (v_xy_td / V_REF)²                 // horizontal carry-through (soft-touchdown)
   − W_vz    · ((v_z_td + VTD_TARGET) / V_REF)²   // vertical speed error (VTD_TARGET=1.5 m/s)
   − W_tilt  · (tilt_td / TILT_REF)²              // upright at contact
   − W_omega · (|ω_td| / W_REF)²                  // not tumbling at contact
   − W_crash · 1[off_pad ∨ too_hard ∨ tipped]     // the catastrophic-failure indicator (bounded)
```
This MIRRORS the MPPI touchdown terminal (`guidance_mppi.c:454-462`) deliberately — MPPI's terminal cost is *already* a well-tuned landing objective, so reusing its structure (with sign flipped to a reward) inherits its tuning wisdom. Rough weights (from MPPI's `TD_*`, normalized): `W_land ≈ 100`, `W_rxy ≈ 120`, `W_vxy ≈ 90`, `W_vz ≈ 30`, `W_tilt ≈ 120`, `W_omega ≈ 60`, `W_crash ≈ 200` (bounded so a crash ranks worst but stays in the reward range — MPPI's `CRASH_COST=800` convention).

### D.2 Dense shaping (per step) — for RL sample efficiency, potential-based

Sparse terminal reward alone is hard to learn (credit assignment over a ~25 s / ~1250-tick episode). Add a dense shaping term — **but potential-based** so it cannot change the optimal policy (Ng-Harada-Russell: `F = γΦ(s') − Φ(s)` for any potential `Φ` leaves the optimal policy invariant, which structurally *prevents* reward-hacking on the shaping):
```
Φ(s) = − k_r · |r_xy − target_xy|/R_REF        // closer to target is better
       − k_v · |v_xy − vdes_converging|/V_REF   // on the converging profile is better (MPPI's vdes)
       − k_t · tilt/TILT_REF                     // more upright is better
```
`vdes_converging` is MPPI's own converging-velocity reference (`converging_vdes`, `guidance_mppi.c:225`) — shaping toward "be on the profile MPPI targets" gives the policy a dense signal that agrees with the expert without over-constraining it. **Potential-based shaping is the recommended form precisely because it is hacking-proof** (§D.5): the agent cannot accumulate shaping reward by looping (the telescoping sum cancels), so it cannot trade landing for shaping-farming.

### D.3 Fuel-awareness and within-authority terms

- **Fuel** (dense, small): `− k_fuel · (fuel_used_this_step)/1000` — mirrors MPPI's `Q_FUEL=0.5`. Makes the policy prefer efficient burns (the "burn longer" engine-out beat costs fuel; the policy should spend it only when needed). A larger *terminal* fuel bonus `+ W_fuel_left · (fuel_remaining)/1000` rewards landing with margin (relight-feasibility).
- **Within-authority** (dense, small): `− k_auth · relu(|a_lat_commanded| − amax(state))²` — penalize commanding lateral accel beyond the tilt-capped `amax` (the physical gamut). This teaches the policy the authority envelope directly instead of relying only on the C clamp, and it discourages the D-009 pathology of railing the command (which the clamp catches but which wastes the softmax/gradient). Keep it small — the clamp is the hard backstop; this is a soft nudge to stay flyable.
- **Control effort / smoothness** (dense, small): `− k_smooth · |a_lat_t − a_lat_{t−1}|²` — discourage chatter (MPPI gets this from Savitzky-Golay + OU; the policy gets it from a smoothness penalty). Keeps the actuator commands physical and the attitude loop happy.

### D.4 Phase-shaping (the divert-then-arrest structure)

The descent has two regimes (aero-divert then powered-arrest, `gfold_research.md` §0). Optionally scale the shaping by phase: emphasize *reach* (closing the offset) high up where aero authority is cheap, and *arrival quality* (v_xy→0, upright) low down where the crossover dead-zone bites. This mirrors MPPI's altitude-ramped `Q_VLOW`/gate structure (`guidance_mppi.c:132-146`, "kill v_xy HIGH where aero authority is cheap"). **Recommendation: start WITHOUT phase-shaping (the potential-based Φ already encodes the converging profile, which is phase-aware); add it in S1 only if the policy over-diverts low or under-diverts high.** Over-shaping is a reward-hacking surface (§D.5) — prefer the minimal reward that works.

### D.5 The perverse-incentive traps and how to avoid them (reward hacking — be concrete)

Every trap below is a way the agent gets reward WITHOUT the behavior we want. The design defends against each:

1. **The "hover forever" trap.** If landing is rewarded and crashing is only mildly penalized, and there is no time cost, the agent can hover to farm dense shaping and never commit to touchdown. **Defense:** (a) potential-based shaping (D.2) cannot be farmed by hovering (it telescopes); (b) a small per-step time penalty `− k_time` and the fuel penalty (D.3) make hovering strictly costly; (c) the episode has a hard timeout that scores a non-landing as a crash. Combined, the agent's only path to high reward is to land.
2. **The "graze the pad edge" trap.** If off-pad is binary and on-pad is uniformly rewarded, the agent lands at the pad *edge* (max miss that still counts). **Defense:** the continuous `− W_rxy·(td_lat/R_REF)²` term rewards *center* landings, not edge landings — the gradient always pulls toward the target. (MPPI has exactly this: `TD_RXY` continuous + the linear pull `40·sqrt(rxy2)/R_REF`, `guidance_mppi.c:462`.)
3. **The "arrive hot but on-pad" trap (the D-012 op→th conversion, the SUBTLE one).** A policy that reaches the far seed but arrives with residual velocity trades an off-pad miss for a too-hard crash — net zero, and it looks like "reaching more" on the position metric. `covo_report.md` and the D-012 addendum both warn this is the real Pareto wall. **Defense:** the terminal MUST weight `v_xy_td` (W_vxy) AND `td_lat` (W_rxy) comparably, so reaching-hot is penalized as hard as missing. The frontier metric's *arrival-quality sub-metric* (§A.3.ii) is the honesty check — report td_v on the landers, not just landed-rate, so a policy that games reach-at-the-cost-of-arrival is caught.
4. **The "exploit a plant quirk" trap (sim-overfitting, the DEEPEST one).** The agent finds a numerical/dynamical artifact of THIS plant (a fin cross-coupling, an ignition-transient edge case) that lands but wouldn't generalize / is physically absurd. **Defense:** domain randomization (§E) over disturbances AND plant params (the agent can't exploit a quirk that varies run-to-run), held-out seeds/conditions (§F.5), and the same MC gates (a quirk-exploiter fails the held-out frontier metric). **This is the anti-cheat crux and it is handled by the same discipline that makes every other guidance change honest** (§F.4-F.5).
5. **The "assist toward the pad" trap (directive-3 violation by another name).** If the reward is shaped so strongly toward the pad that the agent is effectively *pulled* there regardless of feasibility, it violates directive 3 (unlandable states must crash). **Defense:** the reward NEVER adds a force/command toward the pad (it only *scores* the outcome); the policy's command comes solely from `π_θ(o)`, which the frontier metric verifies crashes out-of-frontier states. A policy that "lands" out-of-frontier states (§A.3 bottom-left cell) is a red flag investigated as a leak.

**Overarching reward principle:** keep the reward MINIMAL and terminal-dominated, with only potential-based dense shaping. Every additional dense term is a hacking surface. The MPPI terminal cost is the proven template — start there.

---

## E. TRAINING PIPELINE — the C plant as a gym-style env

### E.1 The house-rule reconciliation (state this clearly, up front)

The canon C-only rule (directive 6, HANDOFF §1.6: "C/C++/CUDA ONLY for project code, NEVER Python") governs **the SIM and the shipped INFERENCE**. The **offline TRAINER is PRECOMPUTE** — it produces a C-consumable artifact (a frozen weights header, §F), exactly as one might use any offline tool to compute a tuned coefficient. **The offline training MAY be Python/PyTorch** (the RL/autodiff ecosystem lives there and the training runs on the GPU fleet, not this CPU), because:
- Its output is DATA (float arrays in a `.h`), not project code — like a golden file or a swept constant.
- It never runs in the sim path, never touches determinism, never ships. The *inference* that ships is hand-rolled C (§F).
- It is the direct analog of the existing offline oracles: `ceiling.c` and the proposed G-FOLD oracle (`gfold_research.md` Tier-1) are offline compute that informs the sim; the trainer is offline compute that *produces a policy constant*. `windthink`'s estimator was designed in a probe; this is the same "design/compute offline, ship a clean artifact" pattern at larger scale.

**Explicitly:** Python/PyTorch for the trainer (on RTX/H200); C-header float arrays as the interface; hand-rolled fixed-order C MLP forward pass as the shipped inference (`GM_NEURAL`). The C-only rule is honored where it matters — the deterministic sim and the shipped controller are pure C. The trainer is precompute. (If the operator prefers zero Python even offline, a C/CUDA RL trainer is possible but is a large build for no benefit; the pragmatic recommendation is PyTorch offline → C artifact.)

### E.2 The env wrapper — a thin RL harness driving booster-core headless

Two options for exposing the C plant as a gym-style `env.step(action) → (obs, reward, done)`:

- **(RECOMMENDED) In-process env via a thin C ABI + ctypes/pybind binding.** Expose a minimal C API over the existing `Sim`:
  ```c
  // booster_env.h — a thin RL-facing wrapper over sim.{h,c}. No new physics; reuses sim_step.
  void* env_reset(uint32_t seed, uint32_t run, int scenario, int module_mask, DisturbConfig* dc);
  //    -> initializes Sim (scenario.c IC + disturbance config §E.3), runs to the first guidance tick,
  //       returns an opaque Sim* and writes the initial legal observation (nav_measure) to a buffer.
  int  env_step(void* sim, const double action[3], double* obs_out, double* reward_out);
  //    -> writes `action` into s->gcmd (a_lat[2]+throttle) with mode=GM_NEURAL, advances ONE guidance
  //       tick (the 500 Hz control/dynamics substeps between ticks run exactly as sim_step does),
  //       computes the reward (§D) from the resulting state, writes the next legal obs. Returns `done`.
  void env_free(void* sim);
  ```
  The Python trainer calls these via ctypes; the C side reuses `sim_step`'s inner loop VERBATIM (same integrator, same nav layer, same control allocation) — so the training env IS the sim, bit-for-bit. **This is the honest, directive-7-aligned choice: one dynamics source, driven by an RL loop instead of the batch loop.** Throughput: the C plant does the physics (millions of steps/sec); the Python overhead is one ctypes call per *guidance tick* (50 Hz, not 500 Hz — the control/dynamics substeps stay in C), so the Python cost is ~1/25 of the step count and negligible against the C physics. Vectorize by running N `Sim` instances (N parallel envs) in C threads, stepping them in a batch per Python call (the SubprocVecEnv/batched pattern).
- **(alternative) Out-of-process via the existing headless batch + a pipe/file protocol.** Slower (IPC per tick), but zero new C ABI. Only if the in-process binding is deemed too invasive. **Rejected as the primary** — the in-process env is faster and cleaner.

**The env is worktree-only training scaffolding** (`_neural_wt/`), gitignored — the shipped tree gets only the frozen weights header + the `GM_NEURAL` inference (§F). The env wrapper is not shipped (it is the trainer's harness).

### E.3 Domain randomization over disturbances + plant params

Each `env_reset` draws a disturbance config `DisturbConfig` from `D` (§A.2), so every episode is a different world:
- **Wind:** randomize `wind_az` (already per-run), `u_ref` scale, Dryden intensity; optionally a `--gust` 1-cosine shear at a random time/altitude.
- **Engine-out:** with some probability, schedule `--engine-out k@t` (side/center, seeded time in the entry-burn window) — the `engineout_design.md` §E injector, reduced to a `DisturbConfig` field.
- **Moving target:** with some probability, a seeded `target_xy(t)` drift / SEA deck (`target_sandbox_design.md` §A) — a `DisturbConfig` field.
- **Plant dispersions:** `--inject` thrust/Isp/CoM (`sim.c:65-76`), sensor bias, `--nav-noisy` (so the policy trains on the SAME noisy observations it will face — critical, else it overfits to clean state).

**All draws are seeded** (the existing Philox streams) → each training episode is itself replayable, and the domain-randomization distribution is a config, not new code. This is the defense against sim-overfitting (§D.5.4, §F.5): a policy can't exploit a quirk that varies every episode.

### E.4 The curriculum — single-disturbance → joint

RL from scratch on the full joint distribution is hard (the compounding failures are rare and catastrophic — sparse learning signal). **Recommended curriculum (this maps directly onto the S1→S2 roadmap):**
1. **Nominal** (no disturbance beyond the base wind): learn the basic divert-and-land. (Bootstrapped from the S0 distilled policy, so this starts near MPPI-quality.)
2. **Single disturbance, one axis at a time:** wind-only (stronger gusts) → engine-out-only → moving-target-only → plant-dispersion-only. Each rung isolates one skill.
3. **Pairwise:** engine-out + wind; moving-target + wind; etc.
4. **The full joint** — engine-out + shear + moving target + dispersions SIMULTANEOUSLY (the operator's dream scenario, §G). By now the policy has each skill; the joint rung teaches it to *compose* them under compounding stress.

Curriculum sequencing: anneal the disturbance probability/intensity upward as the policy's frontier-metric on the current rung passes threshold (e.g. recovery-rate ≥ 0.9 on rung k → advance to k+1). This is standard curriculum RL and it is the difference between "learns the showcase" and "never sees a successful compound-recovery episode to learn from."

### E.5 Compute — local RTX for a first policy, H200-fleet for the joint

- **The bottleneck is SIM THROUGHPUT, not the net.** The 38k-param net trains in milliseconds per batch; the cost is generating experience (rolling out the C plant). A ~25 s episode at 500 Hz is ~12,500 physics steps; millions of steps/sec headless means ~hundreds of episodes/sec/core, ×N parallel envs.
- **Local RTX (this PC, RTX 4070 Ti SUPER sm_89) for the FIRST policy:** S0 distillation + S1 single-disturbance RL is feasible in **hours-to-days** — the net is tiny (GPU barely used for the net), and the C plant on the CPU cores generates experience fast. The DAgger label pass (MPPI teacher, 9 s/run) is the expensive part of S0's data-gen; budget it as a one-time dataset build (parallelize the teacher across cores).
- **H200-fleet (RunPod) for S2/S3:** the full joint distribution + multi-world (S3) needs many more environment steps (rare compounding events, wider randomization, multiple world-params). Scale to the fleet: many parallel C-plant envs feeding a central PPO/SAC learner. The net stays tiny; the fleet buys *environment throughput* (more parallel worlds) and wall-clock for the harder curriculum rungs. (Note: the fleet-capacity memory allows up to 10 concurrent Opus subagents for *orchestration*; the RL fleet here is compute nodes running the C env, a different resource — coordinate the RunPod spin-up with lane `interplanetary` who owns world-param/master-integration.)

### E.6 Determinism during training vs at ship

**Training may be nondeterministic** (parallel envs, GPU RL, replay shuffling) — that is FINE, because training is precompute and its output is a *frozen* artifact. **The SHIPPED policy is frozen and re-golden'd** (§F): once training ends, the weights are fixed, exported to C, and the `GM_NEURAL` inference is bit-deterministic and memcmp-gated exactly like the rest of the sim. The line is: *training is offline/nondeterministic-OK; inference is in-sim/bit-deterministic-mandatory.* This is the same line the whole project draws between "compute the constant offline" and "the sim is sacred."

---

## F. THE DETERMINISM EXPORT — the project-defining move

This is the piece that makes a neural policy *belong* in this project instead of being an anti-cheat liability. **Trained → FREEZE → export as a C header → hand-rolled fixed-order MLP forward pass in C → bit-deterministic, golden-able, a versioned constant.**

### F.1 Freeze and export

After training, the policy weights are FROZEN. Export them to a C header:
```c
// neural_policy_weights.h — FROZEN policy θ. GENERATED offline (PyTorch). A versioned constant,
// like KDIV_SEEK or the aero tables. Re-generating = an ADR event (a new policy version + re-golden).
#define NP_VERSION       1
#define NP_N_IN          24
#define NP_N_HID         128
#define NP_N_LAYERS      3
#define NP_N_OUT         3
// normalization (frozen with the weights — part of the deterministic artifact, §C.3):
static const double NP_IN_MU[NP_N_IN]  = { ... };
static const double NP_IN_SD[NP_N_IN]  = { ... };
static const double NP_OUT_SCALE[NP_N_OUT] = { A_LAT_GAMUT, A_LAT_GAMUT, 1.0 };  // de-norm ranges
// weights, layer by layer (row-major, fixed layout):
static const double NP_W0[NP_N_HID][NP_N_IN]  = { ... };
static const double NP_B0[NP_N_HID]           = { ... };
static const double NP_W1[NP_N_HID][NP_N_HID] = { ... };
// ... W2, B2, W_out, B_out
```
**fp64, not fp32** (§F.3 justifies): the whole plant is fp64 (`BL_HD` fp64 confirmed by the CUDA lane); the policy joins it in fp64 so there is one precision regime and the memcmp oracle is clean. The export script (Python) writes the trained `torch` tensors as fp64 C arrays. **The header is a data artifact** — committed like a golden, versioned by `NP_VERSION`, re-generated only as a deliberate ADR event.

### F.2 The hand-rolled fixed-order forward pass in C

```c
// neural_policy.c — GM_NEURAL inference. Pure C, fixed-order, no atomics, no library.
// BIT-DETERMINISTIC: every operation is a fixed sequence of scalar fp64 multiply-adds in a
// fixed loop order. No cuDNN, no BLAS with runtime-variable reduction, no threading.
BL_HD void neural_policy_step(const State* nav, const GuidanceCmd_ctx* ctx, GuidanceCmd* g){
    double o[NP_N_IN];
    build_observation(nav, ctx, o);              // §C.1 feature extraction (nav view + legal aux)
    for(int i=0;i<NP_N_IN;i++) o[i]=(o[i]-NP_IN_MU[i])/NP_IN_SD[i];   // normalize (frozen)
    double h0[NP_N_HID];
    for(int j=0;j<NP_N_HID;j++){                  // layer 0: FIXED accumulation order over i
        double acc=NP_B0[j];
        for(int i=0;i<NP_N_IN;i++) acc += NP_W0[j][i]*o[i];   // no reordering, no fma-fusion ambiguity
        h0[j]=activation(acc);                    // tanh/relu — a deterministic scalar fn
    }
    // ... layers 1,2 identically (fixed j-then-i loop order) ...
    double a[NP_N_OUT];
    for(int k=0;k<NP_N_OUT;k++){ double acc=NP_B_OUT[k]; for(int j=0;j<NP_N_HID;j++) acc+=NP_W_OUT[k][j]*hL[j]; a[k]=acc; }
    // de-normalize + clamp to the PHYSICAL gamut (parity with MPPI's A_LAT_GAMUT clamp):
    g->a_lat[0]=clampd(NP_OUT_SCALE[0]*tanh_out(a[0]), -A_LAT_GAMUT, A_LAT_GAMUT);
    g->a_lat[1]=clampd(NP_OUT_SCALE[1]*tanh_out(a[1]), -A_LAT_GAMUT, A_LAT_GAMUT);
    g->throttle=clampd(0.5*(tanh_out(a[2])+1.0), ENG_THR_MIN, 1.0);
    g->mode=GM_NEURAL; g->n_eng=1; /* engine_cmd/deploy from analytic triggers (Tier A) */
}
```
**The determinism-critical rules** (mirroring the MPPI determinism discipline, `guidance_mppi.c` header):
- **Fixed loop order** (`j` outer, `i` inner) for every matrix-vector product — the accumulation `acc += W[j][i]*o[i]` runs in a fixed sequence, so the fp64 rounding is bit-identical every run (no unordered reduction — the exact constraint directive 2 imposes).
- **No fma fusion ambiguity:** compile with `-fmad=false` (CUDA) / consistent fp contraction flags (the same `-fmad=false` the CUDA MPPI port uses for 1-ULP parity, `agentB_mppi_design.md` §5) so `a*b+c` is not sometimes-fused-sometimes-not.
- **No atomics, no threading, no BLAS/cuDNN** — a single-threaded scalar loop. The net is so cheap (<10 µs) that vectorization is unnecessary; even if vectorized later, the reduction tree must be fixed-topology (like MPPI's pairwise fold).
- **`activation` is a deterministic scalar function** — tanh/relu from libm (fixed) or a polynomial approximation frozen into the header (even more portable/deterministic across libm versions — recommended for cross-machine parity, matching the CUDA lane's host/device tolerance concern).

### F.3 Why fp64 (and the precision decision)

fp32 would be faster and smaller, but the plant is fp64 and the memcmp oracle demands one precision regime. **Recommendation: fp64 weights + fp64 forward pass.** The net is so cheap that fp64's cost is irrelevant (<10 µs either way), and fp64 sidesteps the fp32 cross-platform rounding divergence that would fight the `--selftest` memcmp. (If a future embedded target forces fp32, treat it as a separate golden with its own tolerance, exactly as the CUDA lane treats host/device parity — but for the desktop sim, fp64 is the clean choice.)

### F.4 Slotting into the guidance interface — the new GM_NEURAL mode

Add `GM_NEURAL=3` to the mode enum (`guidance.h:18`) and a `sim.c` guidance block mirroring the GM_MPPI block (`sim.c:291-321`):
```c
if(is_gtick && s->guidance_mode==GM_NEURAL){
    // E3 entry_supervisor stays ABOVE (like MPPI) for the ENTRY 3-engine burn; the policy owns
    // the aero-descent divert + landing burn. (Or a policy trained end-to-end owns it all — S2.)
    if(!entry_handled){ nav_resync(st,&nav); neural_policy_step(&nav, &ctx, &s->gcmd); }
    // SAME ignition latch + ada freeze as GM_HOVERSLAM/GM_MPPI (sim.c:269-283 / 306-318):
    if(s->gcmd.engine_cmd && !st->engine_on && st->relights_left>0){ st->engine_on=1; ... }
}
```
Selected by a `--neural` CLI flag (mirroring `--mppi`, `main.c` parse pattern). **Default OFF → the whole thing is dead code the compiler folds away → TERMINAL 194/200, ENTRY 95/100, AERO 44/60 reproduce byte-exact** (the hard determinism gate — a `GM_NEURAL` never selected cannot perturb the shipped rates). Between guidance ticks, the last command holds (like `mppi_execute`); the 500 Hz control/dynamics run unchanged. **Directive 7 is nearly FREE:** a direct policy has NO rollout model, so there is no "one dynamics source" mirror to maintain (contrast MPPI, whose `cmd_from_u_lean` rollout mirror must track every hoverslam law change — the policy has no such twin because it doesn't simulate the future, it *reacts*). This is a genuine architectural advantage the operator's directive-7 note calls out.

### F.5 The anti-cheat reconciliation (the honesty argument — decisive)

The concern: "a neural net is a black box; is it a cheat?" The reconciliation, point by point:
- **It reads only the LEGAL state.** `build_observation` (§C.1) consumes `nav_measure`'s output + legal auxiliaries — never `wind_world`/truth-target/hidden disturbance (§4.3). It has NO privileged information; it feels the wind through state exactly as hoverslam and MPPI do.
- **It emits only ACTUATOR COMMANDS.** The output is a `GuidanceCmd` (`a_lat`+throttle) that `control_step` allocates — directive 1 satisfied. It cannot write state, cannot teleport the vehicle, cannot add an assist force (the reward *scores* outcomes but never *commands* toward the pad).
- **It re-derives EVERY TICK.** The forward pass runs fresh on the current legal state at 50 Hz — it is NOT a replayed/scripted trajectory (the D-011 hard line). Two different states → two different commands; the same state → the same command (deterministic). **It is a feedback law, expressed as a weight matrix instead of `KDIV_SEEK` — a learned constant, not a canned path.** The distinction from a scripted trajectory is exactly the distinction between MPPI (re-solves every tick) and a pre-recorded run: the policy, like MPPI, is a *function of the live state*.
- **The frozen weights + offline training = "precompute in, telemetry out."** The weights are computed offline (like `ceiling.c`'s D_phys, like a swept `Kvel`), frozen, versioned, and golden'd. The sim consumes a constant; the sim itself is untouched-pure-C-deterministic. This is the SAME contract as every tuned coefficient in the tree — the policy is just a *bigger* constant, derived by optimization instead of hand-tuning.
- **The ONE real honesty risk is sim-overfitting** — a policy that lands by exploiting a plant artifact rather than by solving the physics. This is a genuine risk (unlike the others, which are structural non-issues). It is handled by:
  1. **Domain randomization** (§E.3): a quirk that varies run-to-run can't be exploited.
  2. **Held-out seeds/conditions** (§F.6): train on one seed set, GATE on a disjoint one — an overfit policy fails the held-out set.
  3. **The same MC gates every guidance change passes** (selftest, TERMINAL 194, cross-seed s7/s99, `--nav-noisy`, `--inject`) — a plant-quirk-exploiter fails these.
  4. **The frontier metric** (§A.3): a policy that "lands" out-of-frontier states (bottom-left cell) is exploiting something un-physical — investigated as a leak, not celebrated.
  **A policy that beats MPPI on HELD-OUT seeds, under domain randomization, passing every MC gate, at ~1.0·D_phys realization, is not overfit — it is a better controller.** That is the honesty bar, and it is the same bar D-012/D-016 met with numbers.

### F.6 The train/held-out split (make it explicit)

- **Train seeds:** a large set (e.g. seeds 1000-9999) × the domain-randomization distribution.
- **Held-out / gate seeds:** the CANONICAL gate seeds the whole project uses — **s42, s7, s99** — kept OUT of training entirely. The policy is GATED on s42/s7/s99 exactly as every prior result (ENTRY 88/79/78 reactive, 95/91/93 MPPI). A policy that scores well on s42/s7/s99 having never trained on them is genuinely generalizing. Plus held-out *conditions*: train on nominal + moderate disturbances, gate on the tail (severe compound engine-out+shear+target) to prove the frontier claim isn't memorized.

---

## G. THE ENGINE-OUT-RECOVERY SHOWCASE — the exact experiment the operator wants to SEE

**The demo:** a policy trained on the joint distribution recovers, live, from a mid-entry-burn ENGINE-OUT with a simultaneous GUST and a MOVING TARGET, scored against the reachable set. This is the "wow factor" — no scripted recovery, the policy re-solves the compounding problem in real time.

### G.1 The exact experiment spec

**Setup (a single, reproducible, seeded run):**
```
scenario  = entry            (62 km, mean 3000 m offset — the 3-engine entry burn exists here)
seed      = a HELD-OUT seed (e.g. s42) not in the training set
guidance  = --neural         (the S2 joint-distribution policy, GM_NEURAL)
disturbances (all fire, seeded, in one run):
  --engine-out 1@t_eo        // a SIDE engine fails mid-entry-burn (the money shot, engineout_design.md §8.1)
  --gust <A@t_g@h_g>         // a 1-cosine wind shear during the aero-descent/burn (D-017)
  --target <seeded drift>    // a moving deck target_xy(t) (target_sandbox_design.md §A.1b)
  --inject                   // thrust/Isp/CoM dispersion (sim.c:65-76)
  [--nav-noisy]              // optional: the policy runs on noisy state (the honest sensor case)
```

**The frame-by-frame beat (mirroring engineout_design.md §8.1, now policy-driven):**
1. **Nominal 3-engine entry burn** — decelerating retrograde, the policy (or E3 supervisor above it) steering the coarse divert.
2. **`--engine-out 1@t_eo` fires** — one side engine dies; the surviving centroid jumps to `−R_eng/2`; the induced torque `Tthr` (`dynamics.c:138`) cants the stack; `n_eng`→2; the policy's engine-health features (§C.1 #18-20) flip.
3. **The control loop catches the torque** (state-feedback gimbal, `control.c:152` — no policy action needed for the *attitude* catch) WHILE **the policy re-solves the reduced-authority DIVERT**: it reads the reduced `n_eng`, the disturbed state, the moving target, and the gust-perturbed velocity, and commands the `a_lat`/throttle that re-aims the now-harder trajectory. **The ghost/pred_impact marker (protocol offset 220) lurches off on the disturbance, then re-converges** onto the moving target as the policy tightens the solve.
4. **The burn stretches** — the policy "burns longer" (33% less thrust; if it owns `engine_cmd` in Tier B, it learned to re-time ignition) and the divert reaches for the moving deck under the gust.
5. **Handoff + land** — at CUT the entry burn hands to the aero-descent; the policy flies the 1-engine landing burn to the moving target, arriving soft and upright — **having recovered from the compound disturbance with no scripted recovery, the entire thesis in one gesture.**

### G.2 Scoring against the reachable set (the rigor)

The demo is not just "it landed" — it is scored against the ENGINE-OUT frontier (§A.4):
- **Compute the shrunken BRS** for this exact disturbance realization: re-run the `ceiling.c` oracle with the 2-engine authority + the gust + the target displacement → the reduced D_phys(engine-out, gust, target).
- **Classify the IC:** is this seed's effective offset IN or OUT of the shrunken frontier?
- **The honest claim:** the policy recovers this run IFF it is in-frontier (and the demo picks a seed that IS in-frontier, so the recovery is *possible* — directive 3). **Then show the boundary:** a slightly-larger-offset or slightly-later-engine-out seed that is OUT of the shrunken frontier, where the policy *correctly fails* (LOC or off-pad) — proving the policy respects physics, not that it magically always lands. **A demo that shows both the recovery AND the honest adjacent failure is far more convincing than one that only shows success** — it proves the policy targets the frontier, not that the scenario was rigged easy.

### G.3 The batch validation behind the single demo

The single hero-run is backed by a batch (the actual gate):
```
--headless --scenario entry --seed {42,7,99} --runs 100 --neural --engine-out 1@t_eo --gust ... --target ...
```
Report, per seed: the recovery-rate-vs-frontier (§A.3), the LOC/off-pad tail (the honest out-of-frontier failures), td_v/td_lat of the landers, and a comparison against **MPPI** run on the identical disturbed batch (the policy should MATCH or BEAT MPPI's compound-recovery rate — MPPI re-solves via replanning, the policy via its compiled reactions). **The headline: "the learned policy recovers X% of in-frontier compound-engine-out scenarios, vs MPPI's Y%, at Z% of the compute."** If X ≥ Y at 1000× less compute, the policy is the deployment winner; if X > Y, it also extended the frontier.

---

## H. STAGED ROADMAP — each rung independently gated

**All in a `_neural_wt/` worktree** (CMakeLists + core + the env wrapper; VS2022 x64 configure), gitignored. **Never edit/build the real tree until a rung's gate is green.** Gates after every build (HANDOFF §1.7): `--selftest` PASS; TERMINAL s42 ×200 == **194/200 byte-exact**; a determinism pair on the changed path; and — since `GM_NEURAL` is default-off — proof that the MPPI/hoverslam rates (ENTRY 95, AERO 44/60, TERMINAL 194) reproduce byte-exact with the neural code present-but-unselected (the leak check: a new mode must not perturb existing modes).

### H.0 — THE BUILD-ORDER DOCTRINE (read before S0 — the canonical sequencing)

The rungs S0–S3 below are the training arc. But the operator's sequencing question — *"do I carve
out the 3-engine asymmetric plant and get Monte-Carlo data first, then distill? or distill first,
then add the disturbances?"* — is answered by two prerequisite steps (**Step 0, Step 1**) that must
precede S0, and by one governing principle that dictates their timing. This subsection is the
explicit, canonical order. **It is neither "all-plant-first" nor "distill-first-blindly" — it is
interface-first, capability-early-but-off, difficulty-ramped.**

**THE GOVERNING PRINCIPLE — THE PLANT IS THE CURRICULUM.** A policy can only learn to handle a
disturbance the plant can *produce*, and the MPPI expert can only *demonstrate* recovery from a
disturbance the plant can *simulate*. So plant-capability and policy-training advance in lockstep —
but NOT uniformly. Every plant change splits into two classes with **opposite optimal timing:**

- **INTERFACE changes** — anything that changes what the policy READS (its observation vector) or
  EMITS (its action). Adding one mid-training changes the network's input/output dimensions →
  forces a re-architecture + retrain. **→ BUILD ALL OF THESE FIRST, WIDE**, before any training.
  Design the socket for the full *eventual* observation/action even if fields sit at constant
  nominal values at first. Pay the interface cost once; then adding a disturbance is free.
- **BEHAVIOR / DIFFICULTY changes** — anything that changes the PHYSICS (harder dynamics, larger
  disturbances) *without* changing the interface. **→ these are a CURRICULUM KNOB**, ramped AFTER
  the pipeline is proven, via fine-tuning. The interface is stable across them → zero
  re-architecture, and the easy-setting policy is never wasted (nominal flight is unchanged; a
  disturbance is an *added condition*, not a replacement).

**THE TRAP: engine-out and moving-target are BOTH.** Classify each plant change into its halves:

| plant change | INTERFACE half (build FIRST, wide) | BEHAVIOR/DIFFICULTY half (curriculum knob, later) |
|---|---|---|
| **Movable target** | `target_xy`, `target_vxy`, `target_cov` added to the observation (§C.4); the `null(r_xy − target)` guidance substitution + `GuidanceCmd.target_xy` socket (P0; `guidance_hoverslam.c:84`; protocol bump w/ lane perception) | the target actually MOVING (a seeded trajectory in training; VLM-acquired much later) |
| **Engine-out** | the engine-health flag(s) added to the observation (`n_eng` is already in State) | the asymmetric torque + reduced authority on failure (`thrust_offset[2]` + `sim.c:154` literal; `engineout_design.md`) |
| **Wind shear (gust)** | **NONE** — guidance is wind-blind (canon §4.3); the policy only ever feels wind as *state drift* | the gust magnitude/timing (`--gust`, D-017, already built) |
| **Dispersions (`--inject`)** | **NONE** — same; felt only as state drift | the dispersion magnitude (already built) |

**Consequence you can exploit immediately:** gust and dispersions need **zero** interface work
(guidance never reads them) — they are pure curriculum knobs, available *today*. Target and
engine-out each need a *small* interface add (a health flag; the target socket) done **up front**,
after which their difficulty is also just a knob.

**THE EASY-PLANT-FIRST RULE (why not build all the hard physics before training).** A **pipeline
bug** (bad C export, broken determinism, a reward mistake, covariate shift) and a
**physics-too-hard failure** (the disturbance exceeds the policy's or the plant's authority)
produce *different symptoms* and need *different fixes* — but if you switch on hard physics before
the pipeline is validated, a failure is **ambiguous** and you burn cycles bisecting "is my export
broken, or is engine-out-plus-shear just unrecoverable here?" So: **validate the novel machinery
(distill → freeze → C-export → determinism → golden) on the SIMPLEST physics that exercises it
(nominal + mild gust), THEN ramp difficulty.** This is *why* S0 distills on the easy setting even
though the ultimate target is the compound showcase — it is deliberate risk isolation, not timidity.

**MPPI IS A VALID DISTILLATION TEACHER FOR EVERY *SINGLE* DISTURBANCE — the teacher question,
answered.** Because of directive 7, MPPI's rollouts consume the SAME `EnvCtx` the plant integrates
(`guidance_mppi.c:350,555` copy it), so the instant the plant produces an engine-out (reduced
`n_eng` + `thrust_offset`), MPPI's rollouts **see the reduced authority and re-solve the recovery
on the next 100 ms replan.** Therefore MPPI is a competent teacher for engine-out — *and* gust,
*and* moving-target, *and* dispersions — taken **individually**: run it across Monte-Carlo
scenarios with that one disturbance ON and regress its actions, and you inherit its recovery for
free. Where MPPI is a **weak** teacher — and where distillation caps out and RL must take over — is
the **JOINT / COMPOUNDING** distribution (engine-out + shear + moving-target *at once*): a
fixed-horizon sampler composes multiple simultaneous disturbances poorly (the coupling), which is
exactly the frontier RL exists to reach. **So: distill single/mild disturbances from MPPI (cheap,
inherits competence); RL the joint/compound distribution (where the policy earns its keep and the
showcase lives).**

**THE CANONICAL BUILD SEQUENCE (explicit; Steps 0–1 are the S0 prerequisites the operator asked
about):**

- **STEP 0 — WIDEN THE SOCKET ONCE (interface, before any training).** Add to the observation
  (§C.1): `target_xy`, `target_vxy`, `target_cov` (§C.4) and the engine-health flag(s) — at
  constant nominal values for now. Add the movable-target guidance substitution `null(r_xy −
  target)` + the `GuidanceCmd.target_xy` socket (= P0 of the stack roadmap; protocol bump
  coordinated with lanes perception/targetdesign). **Gate:** byte-equality vs today when
  `target = origin` AND all engines healthy — the wide socket at nominal inputs reproduces the
  current plant *exactly* (TERMINAL 194 / ENTRY 95 / AERO 44/60). This *is* the "design the socket
  wide, pay zero retraining tax" rule made concrete.
- **STEP 1 — PUT THE DISTURBANCES IN THE PLANT, DIALED OFF (capability, cheap, build early).** Build
  engine-out (`engineout_design.md`: decrement `n_eng` + `thrust_offset[2]` + the `sim.c:154`
  literal + the seeded `--engine-out k@t` injector) and the seeded moving-target trajectory (the
  target moves on a deterministic path *in training*; VLM acquisition is layered on much later — for
  CONTROL training you feed the *true* moving target as the estimate; perception is a separate
  concern, lane perception). Gust + dispersions already exist. **Gate:** each default-OFF is
  byte-identical (the leak check). Now the plant is **capability-complete**; every disturbance's
  difficulty is a dial.
- **STEP 2 = S0 — PROVE THE MACHINERY ON EASY PHYSICS.** Distill MPPI → `GM_NEURAL` on the SIMPLE
  setting (nominal + dispersions + mild gust + static-target-as-input; **engine-out OFF**).
  Validates the whole pipeline + determinism export. **Do NOT turn engine-out on yet** — isolate
  pipeline bugs from physics. (Full detail in S0 below.)
- **STEP 3 = S1 — ONE DISTURBANCE, DISTILL-THEN-RL.** Turn engine-out ON; run MPPI across MC *with*
  engine-out (the valid teacher, above); distill THAT; then RL fine-tune (warm-started from the
  distilled weights) on engine-out-ALONE to beat MPPI on that axis. Repeat per single axis
  (moving-target-alone, stronger-gust-alone). (S1 below.)
- **STEP 4 = S2 — THE JOINT DISTRIBUTION, RL.** Sample engine-out + gust + moving-target +
  dispersions TOGETHER; RL to the frontier. **This is the showcase AND the M4-gap attempt** — and
  the step your dual-failure ("shear + engine-out at once") scenario lives in. MPPI is a weak
  teacher here → RL earns its keep. (S2 below.)
- **STEP 5 = S3 — WORLDS.** Add world-params as a randomization axis (lane interplanetary). (S3
  below.)

**WHY THIS ORDER — THE DECISIVE SUMMARY (answering "which is better," with the reasons):**
1. **Interface-first → zero retraining tax.** You never re-architect the net to add a disturbance;
   the socket was wide from Step 0.
2. **Cheap capabilities early but OFF → the env is capability-complete before training**, so
   expanding the distribution is a *dial*, not a rebuild — and default-off keeps every step
   byte-clean against the existing goldens.
3. **Easy-physics-first for the pipeline → clean diagnosis.** A pipeline bug and a
   physics-too-hard failure look different; don't conflate them by turning on hard physics before
   the machinery is proven.
4. **Distill-single / RL-joint → spend effort where it's needed.** Inherit MPPI's per-axis
   competence for nearly free; reserve RL (the expensive, delicate part) for the compounding
   frontier MPPI can't reach.
5. **Curriculum-ramp difficulty → stable and non-wasteful.** Standard RL practice; the easy-setting
   policy from S0 is a valid initialization for every harder rung (nominal flight is unchanged).

**So, directly: do NOT carve out the 3-engine plant and distill on it first.** Build the engine-out
*capability* early (Step 1, it's small) with its health-flag *interface* wired from the start
(Step 0), but distill on the *easy* plant first (Step 2 / S0) to prove the machinery, THEN turn
engine-out on and distill *with* it (Step 3 / S1), THEN RL the dual-failure joint case (Step 4 /
S2). Both orders "work"; this one is strictly lower-risk because it isolates pipeline bugs from
physics difficulty and pays zero retraining tax.

**RULE OF THUMB:** *widen the interface once, ramp the difficulty gradually, and always validate new
machinery on the simplest physics that can exercise it.*

---

### S0 — DISTILL-FROM-MPPI → match 44/60 AERO at NN speed (the pipeline + determinism-export proof)
*(Prerequisites: Step 0 — the wide observation/action socket; and Step 1 — engine-out + moving-target built into the plant but dialed OFF. See §H.0.)*

**Goal:** prove the ENTIRE machinery — env wrapper, data-gen, training, freeze, C-header export, `GM_NEURAL` bit-deterministic inference — by distilling the EXISTING MPPI expert to MPPI parity. No frontier claim; just "MPPI-quality guidance, 1000× faster, bit-deterministic."
- **Build:** the `booster_env` C ABI (§E.2); the MPPI-teacher data-gen (DAgger, §B.1) across AERO seeds; the PyTorch supervised trainer (§B.1); the freeze/export (§F.1); the `neural_policy.c` C forward pass (§F.2); the `GM_NEURAL` mode (§F.4).
- **Gate:** (a) leak check — neural-present-but-off reproduces TERMINAL 194 / ENTRY 95 / AERO 44/60 byte-exact; (b) determinism — `--neural` run twice = bit-identical RESULT lines (the memcmp oracle on the new path); (c) **parity — `--headless --scenario aero_offset --seed 42 --runs 60 --neural` lands within tolerance of MPPI's 44/60** (target: ≥ 42/60, i.e. within ~2 of the teacher — distillation approximation error is expected small); (d) speed — the neural run is ~1000× faster per tick than `--mppi` (measure wall-time/run). Cross-seed s7/s99. **Decision rule:** proceed to S1 iff the pipeline is byte-clean when off, bit-deterministic when on, and matches MPPI within tolerance. **This rung's deliverable is the pipeline + determinism export, and it ships into the honest tree behind `GM_NEURAL`** (a validated, fast, deterministic MPPI-clone is worth shipping regardless of what RL does next).
- **Honest risk:** distillation approximation error might leave the policy a few points below MPPI (e.g. 41/60) — acceptable if within tolerance; if worse, add DAgger iterations / capacity. Covariate shift is the usual culprit — DAgger fixes it.

### S1 — RL single-disturbance → beat MPPI on one axis

**Goal:** prove RL can EXCEED the expert on at least one disturbance axis (the first evidence the policy is a frontier lever, not just a fast clone).
- **Build:** the RL trainer (SAC or PPO, §B.2), warm-started from the S0 distilled weights; domain randomization on ONE axis at a time (§E.3-E.4 curriculum rungs 1-2); the reward (§D). Also build the RESIDUAL variant (§B.3) as the safety/legibility policy.
- **Gate:** on a single-disturbance batch (e.g. stronger gusts, or engine-out-only, or moving-target-only), the RL policy's **recovery-rate-vs-frontier (§A.3) ≥ MPPI's**, on held-out s42/s7/s99, passing all MC gates + `--nav-noisy`. **Decision rule:** S1 "works" iff RL matches-or-beats MPPI on ≥1 axis AND respects the frontier (correct crashes out-of-frontier, §A.3) AND the determinism export still passes (re-freeze the RL policy, re-golden). Record the number either way (a null — "RL matched but didn't beat MPPI on axis X" — is a real result).
- **Honest risk:** RL instability (mitigated by distill-warm-start + PPO fallback); reward hacking (§D.5 — watch the arrival-quality sub-metric for the op→th trap); the axis might be authority-limited (engine-out reach) where even RL can't beat the frontier — then the null is informative (the wall is physical).

### S2 — RL joint distribution → the compounding-recovery frontier + the M4-gap attempt

**Goal:** the operator's dream — a policy that recovers from the JOINT distribution (engine-out + shear + moving target + dispersions) AND the direct attempt on the M4 gap (can it exceed 0.70·D_phys on AERO?).
- **Build:** the full-curriculum RL (§E.4 rungs 3-4) to the joint distribution; the compound-recovery showcase (§G); the frontier oracle for the shrunken BRS (§A.4).
- **Gate:** (a) the showcase batch (§G.3) — recovery-rate-vs-frontier on compound engine-out+gust+target, ≥ MPPI on the identical batch, held-out seeds; (b) **the M4-gap test — `--neural` AERO realization fraction vs 0.70·D_phys**: does the policy land seeds MPPI misses (the 0.70·D_phys → D_phys band, §A.3)? If AERO lands ≥ 54/60 (≥90%, the M4 gate) it is a HISTORIC result (the first method to clear M4 — every sampling variant was null: kprobe/mvar/covo/mpopi). **Decision rule:** S2 is a win iff the policy recovers the joint distribution at-or-above MPPI AND (the ambitious stretch) clears or approaches M4. **The frontier metric settles the M4 question honestly:** if the policy hits ~1.0·D_phys realization, it found authority MPPI's sampling couldn't (the thesis vindicated); if it plateaus at 0.70·D_phys, the dead-zone is a genuine plant-authority wall and the M4 answer is dispersion-retune (per `ceiling.c` — also a valuable, honest finding).
- **Honest risk:** THE deepest — sim-overfitting on the joint distribution (§F.5), reward hacking on the compound scenario (§D.5), and the real possibility that the 0.70·D_phys wall is physical (in which case the policy matches but doesn't exceed the frontier — the M4 gap was never a controller problem). All are handled by the frontier metric + held-out gates + the same MC discipline. **Report the null loudly if it comes** — "the learned policy also plateaus at 0.70·D_phys, confirming the dead-zone is a plant-authority ceiling" would be as important a finding as clearing M4.

### S3 — multi-world (hand to lane interplanetary)

**Goal:** generalize the policy across world-params (different gravity/atmosphere/vehicle — Mars, Moon, a different booster), the perception-to-policy stack's full ambition.
- **Build:** domain-randomize over WORLD parameters too (gravity, atmosphere model, vehicle mass/thrust) — a superset of §E.3. This is where lane `interplanetary` owns the world-param axis and the master integration; THIS doc's policy is the guidance core they parameterize.
- **Gate:** recovery-rate-vs-frontier across the world-param distribution, per-world frontier oracles. **Hand-off:** lane `interplanetary` drives S3; this doc provides the policy architecture, observation/action interface (§C — world-params enter as additional observation features or as a conditioning input), and the determinism export (§F) they must preserve per world.
- **Honest risk:** a single small MLP may not span radically different worlds (a Mars policy and an Earth policy might need different weights) — the fallback is per-world policies (a policy *family*, each frozen/golden'd) or a world-conditioned net (world-params as input features). Lane `interplanetary`'s call; flagged here as the known scaling question.

### H.1 Roadmap summary + honest overall risk

| Rung | Deliverable | Gate (the number) | Ships? | Deepest risk |
|---|---|---|---|---|
| **S0** | pipeline + determinism export + fast MPPI-clone | AERO ≥42/60 `--neural`, bit-deterministic, byte-clean off | **YES** (behind GM_NEURAL) | distillation approx error |
| **S1** | RL beats MPPI on one axis | recovery-vs-frontier ≥ MPPI on ≥1 axis, held-out | maybe | RL instability / axis is authority-limited |
| **S2** | joint compound-recovery + M4 attempt | showcase ≥ MPPI; AERO → M4 (≥54/60) or the honest 0.70 plateau | if it clears gates | sim-overfitting; the 0.70 wall may be physical |
| **S3** | multi-world (→ interplanetary) | recovery-vs-frontier across worlds | interplanetary's call | one MLP may not span worlds |

**The overall honest framing:** S0 is high-confidence and pays for itself (fast deterministic MPPI-clone). S1 is likely (RL beating a suboptimal expert on one axis is standard). S2 is the ambitious frontier bet — it MIGHT clear M4 (the first method to do so) or it MIGHT confirm the 0.70·D_phys wall is physical (equally valuable). Either S2 outcome is a decisive measurement, and that asymmetry — a de-risked pipeline (S0) carrying an ambitious-but-falsifiable frontier bet (S2) — is why this is worth building as the centerpiece. **The determinism export (§F) is what makes it belong here; the frontier metric (§A.3) is what keeps it honest; the plant-as-env (§E) is what makes it feasible on the hardware we have.**

---

## Appendix A — canon compliance restated

- **directive 1 (guidance outputs actuator commands only):** SATISFIED — `π_θ` emits a `GuidanceCmd` (`a_lat`+throttle), consumed by `control_step`; it never writes state (§C.2, §F.5).
- **directive 2 (determinism sacred):** SATISFIED — the shipped inference is a fixed-order fp64 scalar C forward pass, no atomics/BLAS/cuDNN, memcmp-golden'd; training may be nondeterministic (precompute), the frozen artifact is bit-exact (§F). Default-off ⇒ byte-identical to today.
- **directive 3 (unsolvable → crash):** SATISFIED — the reward scores outcomes, never commands toward the pad; the frontier metric (§A.3) verifies the policy crashes out-of-frontier states; the showcase shows the honest adjacent failure (§G.2).
- **directive 5 (renderer pure observer):** SATISFIED — the policy is a guidance layer; the renderer draws the resulting state/pred_impact from telemetry (§G), no feedback into dynamics.
- **directive 7 (one dynamics source):** SATISFIED and NEARLY FREE — a direct policy has NO rollout model, so there is no dynamics-twin to mirror (contrast MPPI's `cmd_from_u_lean`); the policy re-derives from live state each tick (§F.4).
- **directive 9 (TERMINAL byte-identical):** SATISFIED — `GM_NEURAL` is `fins_deployed`-aware and default-off; TERMINAL (fins stowed, no `--neural`) never touches it (§F.4).
- **§4.3 (guidance reads legal state, never hidden disturbance):** SATISFIED — `build_observation` consumes only `nav_measure`'s perturbed view + §8.1-legal target pose + §4.3-legal engine-health flag; never `wind_world`/`wind_filt`/truth-target (§C.1, flagged loudly).
- **§8.1 (guidance consumes the nav view):** SATISFIED — the observation IS the nav view; under `--nav-noisy` the policy correctly degrades on noisy inputs and is trained on them (§E.3).

## Appendix B — the one-line-per-fact code map (verify before building)

| fact | file:line |
|---|---|
| the LEGAL observation (nav view: perturbed r/v/q/ω, pass-through rest) | `nav.c:71-139`, `nav.h:32-45` |
| perturbed kinematic indices `S_RX..S_WZ` = 0..12 | `nav.c:65-68`, `state.h:9-23` |
| engine-health is §4.3-LEGAL (chamber-P analog) | `engineout_design.md` §C.2 |
| the ACTION interface (`GuidanceCmd`: throttle, a_lat[2], engine_cmd, n_eng, deploy_cmd, mode) | `guidance.h:6-16` |
| what the plant consumes from the cmd | `control.c:41-44`, `:82-125` |
| the mode enum (add GM_NEURAL=3) | `guidance.h:18` |
| the guidance-tick cadence + GM dispatch | `sim.c:257,262,291` |
| the ignition latch + ada freeze (mirror for GM_NEURAL) | `sim.c:269-283`, `:306-318` |
| the MPPI expert (distillation teacher) | `guidance_mppi.c` (whole) |
| MPPI channels = throttle + 2 lateral (matches action space) | `guidance_mppi.h:32` |
| MPPI blend `a_lat = s·MPPI + (1−s)·hoverslam` (residual hook) | `guidance_mppi.c:812` |
| the physical gamut clamp `±A_LAT_GAMUT` (policy output range) | `guidance_mppi.c:40` |
| reference scales (normalization constants) R_REF/V_REF/W_REF/TILT_REF | `guidance_mppi.c:86-89` |
| the converging-velocity reference (shaping target) | `guidance_mppi.c:225` |
| the MPPI terminal cost (reward template) | `guidance_mppi.c:454-462` |
| the reachability oracle D_phys ≈ 1107 m | `ceiling.c` (whole), esp. `:528-539` |
| MPPI realizes ~0.70·D_phys | `ceiling.c:645-674`, `gfold_research.md` §C |
| the M4 plant-authority ceiling (null from 4 angles) | `covo_report.md` §4, `kprobe_report.md`, `mvar_report.md`, `mpopi_report.md` |
| the aero/thrust crossover dead-zone ~22 kPa | `control.c:44-81`, `covo_report.md` §4 |
| the engine-out injector (disturbance) | `engineout_design.md` §E + §9 (its own line map: `dynamics.c:137` arm_thr, `sim.c` a_burn `3.0→n_eng`) |
| the moving-target substitution (disturbance) | `target_sandbox_design.md` §B, `target_sample`/SEA |
| gust shipped | `--gust` (D-017) |
| the CUDA determinism discipline (fixed reduction, `-fmad=false`) to mirror | `agentB_mppi_design.md` §5, `guidance_mppi.c` header |
| the convex G-FOLD teacher/warm-start alternative | `gfold_research.md` §C (Rank 1) |
| fp64 everywhere (`BL_HD`) | confirmed by the CUDA lane (continuity §1) |
