# Oracle-Distillation for the hard frontier (engine-out × gust × moving target)
### The N2-S2 lane, reframed: distill an OFFLINE OPTIMAL oracle, not a real-time teacher. 2026-07-20.

## 0. The thesis (and why it fixes the D-031/D-032 null)

The distillation nulls (D-031 MPPI-teacher, D-032 reactive-teacher) failed for ONE reason: **the teachers were
real-time-suboptimal on engine-out** (MPPI ~10%, reactive ~15%). "Can't distill past the teacher" is a fact
about *those* teachers. It says nothing about distillation itself.

**The fix (operator's proposal, adopted):** build an OFFLINE OPTIMAL ORACLE — a trajectory optimizer with NO
real-time budget that grinds minutes/scenario to trace the genuinely near-optimal recovery — then distill THAT.
D-027 already proved the recoveries physically EXIST (2-engine in-frontier fraction ≈ 1.000; the shortfall is
the controller, not physics). An oracle that finds them is a teacher the student can actually chase to the
frontier. This is the established paradigm: **trajectory optimization → imitation** (guided policy search /
expert iteration with an *optimal* expert), not RL. It is more stable and sample-efficient than RL and is the
right first attempt; RL is the fallback only if the oracle can't cover the state space.

## 1. Is it physically possible? (feasibility — the honest answer)

For a given compound draw (offset at engine-out, remaining fuel, gust profile, target motion): **sometimes.**
The recoverable set is nonempty but bounded — that boundary IS the showcase's "honest adjacent failure."
- **The oracle IS the feasibility test.** It returns a trajectory that hits the terminal manifold (r=deck(tf),
  v=deck_vel(tf), upright, ω≈0) exactly, OR it fails / proves infeasible. Feasible draws → perfect solutions;
  infeasible draws → the honest edge, mapped (not hidden). We keep both (the failures are the §G.2 showcase
  material — a manufactured 6-8 km / center-out / two-out case).
- **"Zero speed exactly at zero height"** is a measure-zero terminal target a REACTIVE law only approaches
  (hence HARD vs PERFECT grades). An optimizer hits it EXACTLY — it's a hard terminal *constraint*, not a
  cost. This is the core reason the oracle beats every reactive/receding controller.
- **The exact recoverable map** is the Backward Reachable Set (BRS): integrate the terminal manifold BACKWARD
  under the (2-engine, gusted) dynamics; the set of initial states from which the terminal set is reachable is
  the feasibility map. "Forwards and backwards" (operator) = forward shooting from the start + backward
  reachability from the terminal — their intersection is the boundary-value solution. The roadmap already
  scores "vs the shrunken BRS"; this computes it.

## 1.5 The policy is a UNIVERSAL robust feedback law — NO contingency labels (operator, load-bearing)

The deployed policy is **NOT told** "engine out" or "gust now" or "target moving." It sees ONLY what a real
flight computer senses onboard, and must be robust enough to land — on the (moving) target, zero velocity at
zero height, upright — across ALL flight regimes and ALL contingencies, *inferring* the situation from the
observable dynamics. One policy, every case, no regime switch. This is the whole point of what the NN must be.

**Asymmetric (privileged) teacher-student — the exact right frame:**
- **The ORACLE is privileged.** Offline, it may know EVERYTHING: the engine-out time, the full gust profile,
  the target's future motion. It plans the optimal recovery with that god's-eye knowledge. That is legal — a
  teacher is allowed to cheat.
- **The STUDENT is onboard-only.** Its input is strictly what the vehicle can measure: position, velocity,
  attitude quaternion, body rates, fuel/mass, the nav estimate of the (moving) deck pose, and the SENSED
  EFFECTS that betray a contingency — measured angular acceleration and the residual between commanded and
  achieved accel (an engine-out shows up as induced torque + a thrust deficit), the nav wind/drift estimate
  (a gust shows up as lateral drift). NO fault flag. It learns to INFER the latent (which engine failed, how
  hard the gust) from these signatures and act optimally.
- This is **privileged learning / learning-by-cheating / RMA (rapid motor adaptation)**: a god's-eye teacher
  distilled into an onboard-observation-only student. Established and reliable.

**This ALSO explains the D-031/D-032 null more completely — there were TWO causes, not one:**
1. the teacher was mediocre (fixed by the oracle, §0), AND
2. **the policy could not EXECUTE or was OFF-DISTRIBUTION for the contingency.** (Corrected after reading
   `policy_obs.h`: the engine-out IS observable — `OBS_EH0/1/2` are the chamber-P health flags, and body rates
   `OBS_WX/WY/WZ` + the target estimate are all present. My earlier "invisible in one frame" was wrong.) The real
   architecture failures are:
   - **Lateral-only action (the dominant limit).** NP_VERSION 6 outputs only `a_lat` and DEFERS throttle +
     ignition-timing to hoverslam. A compound recovery needs COORDINATED throttle + timing + steering; a
     lateral-only head physically cannot command it. Worse, its `a_lat` plugged into hoverslam's vertical makes
     the COMBINED controller worse than pure hoverslam (0% vs 25%) — the net's steering is actively harmful
     off-nominal. ⇒ go **full-action** (throttle + a_lat + ignition/deploy timing).
   - **Off-distribution.** It was distilled on clean + mild-gust + a little EO; the compound regime is far
     outside that, so its `a_lat` there is garbage. ⇒ **train on compound-regime data** (the cheapest single fix
     — likely lifts 0% → ~teacher 25% with NO architecture change, testing the data hypothesis first).
   - **Then** the sysID refinements for the hard tail: a short temporal window (infer disturbance MAGNITUDE from
     ~0.1-0.5 s of rates, beyond the binary health flag — the RMA insight) + more capacity than 37k params.
   Ordering: (a) compound data on the current arch (cheapest, reuses the whole pipeline); (b) full-action head;
   (c) history + capacity; (d) the optimal oracle for the frontier tail.

**Consequence for the pipeline:** the oracle dataset is stored as (privileged-full-state, onboard-observation-
history, oracle-action); training maps **observation-history → oracle-action** with ALL regimes MIXED and
UNLABELED, so the single policy must handle everything. Verdict-filtering (replay in the byte-exact sim) still
guarantees executability. The moving-target lead, engine-out recovery, and gust rejection all emerge as ONE
learned reflex — which is exactly the operator's spec.

## 2. The oracle solver — the RIGHT architecture

The problem is **continuous optimal control with a hard terminal constraint** (6-DOF, thrust ∈ [Tmin,Tmax] per
engine × n_eng, gimbal ≤ max, glide-slope/qbar/attitude limits, minimize fuel). The right tools, best-first:

1. **Gradient-based trajectory optimization** — the production oracle:
   - **Direct collocation** (Hermite-Simpson) + an interior-point NLP (IPOPT/OSQP-SQP): discretize state+control
     at N knots, dynamics as equality constraints, terminal manifold as equality, bounds as inequalities. General,
     exact terminal, handles the aero + engine-out torque + moving target (target(tf) as a terminal equality).
     Multi-start for global coverage. Seconds-to-minutes/solve.
   - **Successive Convexification (SCvx) / lossless convexification** — the Falcon-9/Mars powered-descent method
     (Açıkmeşe et al.): convexify the Tmin>0 nonconvexity losslessly, linearize dynamics about a reference, solve
     a sequence of SOCPs. Fast (~seconds), reliable, near-global. Best for scale IF we accept its formulation cost.
2. **The global/search outer loop** (the operator's "MCTS the hell out of it"): the hard cases have LOCAL minima
   in the strategic decisions — divert direction, ENTRY-burn ignite time, bank-reversal timing, which-way-around a
   moving target. Wrap the local trajopt in a search over those DISCRETE choices: multi-start + a coarse tree
   search / progressive-widening over {ignite time, divert azimuth, reversal count}, trajopt-refine each leaf,
   keep the best. This is where "search forwards and backwards, any other way" lives — as the GLOBAL layer.
3. **Pure MCTS is the wrong INNER tool** for continuous exact-terminal control (action-discretization is crude,
   never hits the terminal manifold exactly). It belongs only in the outer strategic search. Likewise **cranked
   real-time MPPI is not enough**: it's a local sampler with a soft terminal cost — it approximates, it doesn't
   nail zero-velocity-at-zero-height, and it plateaus (the very thing we're escaping). We use MPPI/CEM only as a
   day-1 black-box FEASIBILITY PROBE (§4 Phase A0), then escalate to gradient trajopt for the real oracle.

**Non-negotiable fidelity discipline (the whole thing hinges on this):** the oracle MUST use the sim's EXACT
dynamics, else its "optimal" trajectories don't execute in the real plant (distribution mismatch → garbage
distillation). Two guards, both used:
- The optimizer's model = the sim's `dynamics_deriv` (finite-difference or AD Jacobians off the real C plant).
- **Verdict-filter EVERY oracle trajectory**: replay it open-loop through the byte-exact C sim; keep ONLY those
  that actually land GOOD/PERFECT with the terminal tolerances. This is the project's existing discipline
  (verdict-filtered self-imitation, cbc89fe §4.3) applied to the oracle. A trajectory that doesn't replay is
  discarded — so the dataset is provably sim-executable by construction.

## 3. The distillation pipeline (why it works where D-031/D-032 didn't)

1. **Scenario sampler**: draw hard compound scenarios (engine-out k@t × gust peak/alt × moving target/deck),
   biased toward the FRONTIER (near-infeasible), plus a spread of easier ones for coverage.
2. **Oracle solve** (§2) → optimal (state, action) trajectory; **verdict-filter** through the C sim.
3. **Behavioral cloning**: train the policy on the oracle (state → action) pairs. Because the oracle is optimal,
   the student can be good (the D-031/D-032 blocker — a mediocre teacher — is gone).
4. **DAgger-with-oracle** (the covariate-shift fix, and where the compute goes): roll the student out, collect
   the states it VISITS, RE-SOLVE the oracle from each (or a subsample), add (visited-state → oracle-action) to
   the dataset, retrain. This is the expensive loop the operator is authorizing ("spend the compute"). It
   directly answers D-032's "covariate shift didn't transfer the rate" — because now we re-optimize AT the
   student's states with an optimal expert, not a mediocre one.
5. **Gate battery (unchanged)**: leak byte-equality, held-out seeds s42/s7/s99, no-regression floors (AERO≥46,
   gust-A≥45, ENTRY-clean≥57), KAT from the C pass, NP_VERSION bump + re-golden. The moving-target lead the
   hand-tuned D-038 couldn't get — the NN learns it from optimal examples (the oracle plans to intercept the
   deck's FUTURE pose, so the imitation policy inherits the lead for free).

## 3.5 GROUNDING EVIDENCE (2026-07-20) — the architecture is the proven bottleneck

Compound hard case = ENTRY + engine-out random + gust(15@6000:1000) + moving target(circle:15:40), s42:

| controller | landed | note |
|---|---|---|
| clean ENTRY (no contingency), neural | 29/30 (96.7%) | the net is fine when nothing goes wrong |
| **compound, neural NP_VERSION 6** | **0/30 (0%)** | catastrophic — WORSE than its own teacher |
| compound, MPPI (real-time, wind-blind) | 3/20 (15%) | |
| **compound, hoverslam (reactive)** | **5/20 (25%)** | the distillation *teacher* beats the student 25→0 |

Two conclusions: (1) the **feasible fraction is ≥25%** even under a dumb reactive law ⇒ big headroom for an
optimal oracle. (2) The neural net at **0% while its teacher gets 25%** proves the §1.5 architecture failure is
REAL and DOMINANT — the single-frame lateral-only net can't observe/represent the contingency, so it's actively
bad off-nominal. This means there is a CHEAP first win before any fleet spend:

**Phase 0 (cheap, big, do first): fix the architecture, distill what we ALREADY have.** The reactive+MPPI
controllers already solve ~15-25% of compound draws; those SOLVED trajectories are verdict-filtered oracle-grade
data for their instances. Rebuild the policy per §1.5 (full 6-DOF obs incl. effect-residuals + a short history +
a bigger net), distill the existing controllers' compound successes (all regimes mixed, unlabeled), and the NN
should climb 0% → ~teacher-level (~25%) — proving the architecture was the bottleneck, for near-zero compute.
THEN Phase A (the optimal oracle) attacks the hard 75% tail toward the frontier.

## 4. Phased plan (de-risk cheap, THEN rent the fleet)

- **Phase A0 — black-box feasibility probe (local, ~hours):** pick ONE hard compound draw. Optimize the
  open-loop control over the FULL horizon with CEM/CMA-ES using the exact C sim as a black box (a scripted-control
  injection hook + a terminal-constraint cost). Question answered: can an expensive offline search land this
  "impossible" case, and does it replay GOOD with ~zero terminal velocity on the moving pad? If YES → the whole
  approach is validated on evidence. If the black-box search plateaus → escalate to A1.
- **Phase A1 — the gradient oracle (local, ~days):** direct collocation (sim-dynamics Jacobians) + multi-start;
  solve ~50-200 frontier draws; verify replay + terminal tolerances; measure the true feasible fraction. This is
  the real oracle.
- **Phase B — dataset (fleet):** batch the oracle over 10^4-10^5 frontier scenarios; verdict-filter; build the
  (state, action) corpus. Oracle solve is CPU-heavy (NLP) → many CPU cores; embarrassingly parallel per scenario.
- **Phase C — distill + DAgger (fleet, GPU):** BC then K DAgger-with-oracle rounds; gate; export NP_VERSION N+1.
  Training is GPU (H200); DAgger re-solves are CPU. A mixed CPU+GPU fleet.
- **Phase D — the compound showcase (N3):** the trained policy vs the shrunken BRS, demoed WITH the honest
  manufactured out-of-frontier failure.

**Fleet sizing (rough, to right-size the RunPod rental):** oracle solve ~10 s-2 min/scenario CPU. 10^5 scenarios
× 30 s ≈ 800 CPU-core-hours per DAgger round; ~5 rounds ≈ 4000 core-hours → a ~64-128 vCPU box for a few days, or
a fleet for a day. NN training (37k-param policy, ~10^7 rows) is small — a single H200 trains it in hours; the
H200 fleet is justified only if we scale the policy (bigger net for the compound state) or parallelize DAgger
re-solves on GPU (batched DDP). **Recommendation: CPU-fleet-heavy for the oracle, 1-2 H200 for training — start
with Phase A0/A1 local before renting anything.**

## 5. Honest risks (what could make it harder than "easy")

1. **Model fidelity** (the #1 risk): oracle dynamics ≠ sim dynamics ⇒ non-executable trajectories. Mitigated by
   sim-dynamics Jacobians + verdict-filtering EVERY trajectory (dataset is sim-executable by construction).
2. **Open-loop optimal ≠ closed-loop robust**: the oracle gives an open-loop plan; the gust is deterministic here
   so a plan that accounts for it works open-loop, but the NN must generalize to a FEEDBACK law — that's what
   DAgger-with-oracle buys (re-solve from perturbed/visited states).
3. **Tooling vs the C-over-Python rule**: the sim/validation/policy stay C (byte-exact, determinism). The OFFLINE
   oracle may use a mature solver (IPOPT/OSQP/ECOS, possibly via Python) — sanctioned by the operator ("I don't
   care how you get them"), because it's offline data-gen on a rented fleet, not the sim hot loop. Every output is
   validated through the byte-exact C sim regardless of how it was produced.
4. **Feasibility is partial**: not all draws are recoverable. The oracle maps the true frontier; we distill the
   feasible interior and SHOWCASE the honest edge. We do NOT claim ~59/60 on truly-impossible draws.

## 6. Relation to existing plan
This IS the reserved N2-S2 lane (ROADMAP "E-EO-RL"), reframed from RL to optimal-oracle imitation (a better first
attempt). It composes with the expert-iteration routing (cbc89fe): the oracle is the ultimate "operator" on the
rollout-visible axes (engine-out/target/dispersions), verdict-arbitrated. Promotion discipline unchanged (3-seed
×180 + clean ratchet + frontier judge + KAT + NP_VERSION).
