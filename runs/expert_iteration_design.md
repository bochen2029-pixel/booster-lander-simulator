# EXPERT ITERATION — the two-operator teacher loop for pairwise/compound rounds

**Status:** ADOPTED-IN-PRINCIPLE (operator-signed in conversation, 2026-07-19); the introducing
build writes its own ADR with validity tables BEFORE the composite labels a single training row.
**Timing:** does NOT touch D-025 — single-axis engine-out finishes with the fixed MPPI teacher as
planned (a fixed teacher is strictly valid per-axis, §H.0). This loop enters at PAIRWISE.
**Provenance:** synthesized from two independent analyses (the design fork + the implementation
lane), each of which corrected the other; both corrections are canonized here. Written by the
fork lane at the operator's request because the implementation lane is context-constrained —
this file is self-contained; nothing from the conversation is required to execute it.
**Canon it rides on:** §4.3 (planner is wind-blind BY DESIGN), §9.8 (GM_NEURAL/NP_VERSION),
§9.9 (frontier metric), §13.6 (gate battery + held-out law), directive 7 (rollouts share EnvCtx),
directive 11 (all of this is precompute; the shipped artifact stays a frozen NP_VERSION).

---

## 1. The thesis, with the premise stated correctly

The naive claim "imitation can never exceed its teacher" is REFUTED by our own ledger: the
student beat its label source clean (D-023: 135/180 vs 126/180) and beat it badly under shear
(D-024: 45 vs 38 @ 12 m/s) while trained purely on that source's labels. Mechanism: imitation
converges to the teacher's *per-state action distribution*, but closed-loop performance is a
different object — the student executes a variance-reduced, every-tick-consistent rendering of
the teacher's intent, which composes better than the teacher's 10 Hz replan cadence composes
itself (plan staleness, sampling jitter, warm-start rediscovery).

**The honest argument for expert iteration is therefore not impossibility but RATE:** improvement
stalls once per-state label quality becomes the binding constraint — which is exactly what §H.0
predicts at the compound rungs, where MPPI's fixed-budget sampler composes simultaneous
disturbances poorly. The fix is the AlphaZero-shaped loop: an IMPROVEMENT OPERATOR generates
better-than-current-policy behavior, the trainer distills it, a GATE promotes champions, and the
promoted champion powers the next round's generator. All precompute; same ceremony.

## 2. The two operators, routed by DISTURBANCE VISIBILITY

The AlphaZero analogy has one load-bearing precondition: the search must run on a faithful
simulator (MCTS sees the true board). Our sampler deliberately does not — **canon §4.3 zeroes
`wind_world` in MPPI's rollouts** (see D-017: "the MPPI planner zeroes env.wind_world in its
rollouts"). So sampler-refinement is an improvement operator ONLY on axes the rollout model
actually represents. Route accordingly:

### Operator A — SAMPLER-REFINEMENT (student-warm-started MPPI)
*Valid axes: engine-out, moving target, plant dispersions* — all visible to the rollouts
(directive 7: `guidance_mppi.c:~350,555` copy the same `EnvCtx` the plant integrates; `n_eng` +
`thrust_offset` since D-020; `target_xy` threaded through CPU + CUDA rollouts at N0; `--inject`
scales are EnvCtx params — **verify the lean rollout consumes thrust/Isp/CoM scales with one
grep before relying on the dispersions leg**).

- **Mechanism:** warm-start MPPI's mean with the STUDENT's plan instead of the hoverslam recipe —
  roll π through the shared lean model over the horizon to seed `ubar` (a neural-warm-start mode
  in `warm_start_nominal`; deterministic because π and the lean model both are;
  directive-7-clean; default OFF ⇒ byte-clean leak gate). The sampler then spends its entire
  budget POLISHING an already-competent plan; softmax refinement of a good neighborhood is the
  policy-improvement step, exactly network+MCTS. Composite = student proposes, sampler refines.
- **Side benefit:** a strong warm start may cut the K the sampler needs → faster farms. Measure,
  don't assume.

### Operator B — VERDICT-FILTERED SELF-IMITATION
*The wind axis (and anything else the rollouts cannot see).*

Refining a shear-adapted plan against a wind-blind rollout cost would sand the shear adaptations
OFF — pulling the student back toward the teacher's 38/60. **DO NOT run Operator A on the gust
axis.** The improvement operator for wind is the one oracle that sees everything, for free: the
plant verdict.

- **Mechanism:** fly the CURRENT student through domain-randomized shear; score every run with
  the honest outcome; distill from the demonstrations that flew well. Selection discipline:
  - Filter **frontier-relative**, not raw-landed (§9.9): keep in-frontier landings meeting
    td_v/td_lat thresholds; never reward an out-of-frontier "success" (that is the leak cell —
    investigate, don't train on it).
  - Prefer **weighting over hard filtering** (advantage/reward-weighted regression) to blunt
    survivorship bias — a hard keep/drop line selects for lucky wind draws as much as skill.
  - Failures stay OUT of the label set but IN the evaluation record (they are the honest
    denominator).

### The routing rule, including MIXED rungs (this is the part the per-axis statement misses)
Pairwise and compound rounds will MIX visibility — gust+engine-out is literally the next pair.
Rule: **if ANY active axis is rollout-invisible (i.e., wind is in the mix), the two operators run
IN SERIES, and the verdict is the final arbiter of every training row.** Operator A may still
GENERATE (its engine-out re-planning is genuinely valuable mid-compound), but nothing it produces
enters the training set except through Operator B's outcome gate. Only on wind-free rungs may
Operator A's labels flow directly.

## 3. The ceremony (unchanged, plus two floors)

Merged replay across ALL rounds remains the practice (anti-forgetting), with two explicit floors:
1. **The fixed-teacher + clean fractions of the replay never go to zero** (the echo-chamber
   anchor: self-generated data must always be diluted by ground-truth-teacher and clean-flight
   rows).
2. **Promotion gets statistical teeth:** champion promotion (i.e., the new NP_VERSION becomes the
   next generator's warm start / data source) requires the three-seed ×180 battery (the D-023
   pattern) — Wilson intervals at n=60 are too soft to promote on — PLUS the hard clean-air
   floor (no promotion below the current clean ratchet) PLUS the §9.9 frontier metric as judge.
   And the introducing ADR must table **composite-vs-student AND composite-vs-teacher** on the
   target axis BEFORE the composite labels anything: if the composite doesn't beat the student
   head-to-head, it is not an improvement operator and must not teach.

Everything else is the standing NP_VERSION ceremony: freeze → fp64 header → KAT re-pinned from
the C pass → full §13.6 battery (leak check, determinism pairs, held-out law: s42/s7/s99 and
held-out conditions never trained on).

## 4. Do-nots and risks

- **DO NOT sampler-refine the gust axis** (§2 — the wind-blind trap; this is the whole routing).
- **DO NOT let self-generated data become the whole diet** (floor #1).
- **DO NOT promote on a single 60-run cell** (floor #2).
- Sim-overfit accelerates under self-play vs a fixed teacher — the DR + held-out-condition
  defenses (§13.6.3) become MORE binding in this mode, not less.
- Verify-before-relying: the dispersions leg of Operator A assumes the lean rollout consumes the
  `--inject` scales; one grep settles it at build time.

## 5. Sequencing

1. **D-025 (unchanged, in flight):** single-axis engine-out, fixed MPPI teacher.
2. **Introduce the loop at PAIRWISE** with its own ADR: build the neural-warm-start mode
   (default-off, byte-clean), run the operator-validity tables (§3) on the engine-out axis first
   — it is the axis where Operator A is strongest — then let the two-operator loop teach the
   pairs per the §2 routing.
3. **Compound:** both operators in series under the mixed-rung rule; RL (S2/N3) remains reserved
   if the loop plateaus short of the compound gate — this design is the bridge between
   distillation and RL, not a replacement for the reservation.

## Appendix — one-line facts map

| fact | where |
|---|---|
| rollouts zero wind (planner wind-blind by design) | canon §4.3; D-017 ADR ("MPPI planner zeroes env.wind_world in its rollouts") |
| rollouts DO see engine-out (n_eng, thrust_offset) | directive 7; D-020; `EnvCtx` copies at `guidance_mppi.c:~350,555` |
| rollouts DO see the target | N0 threading (D-020): CPU + `guidance_mppi_rollout.cuh` |
| the warm start to replace | `warm_start_nominal` (hoverslam-recipe forward-shoot), `guidance_mppi.c` |
| student>teacher clean / under shear (the premise data) | D-023 (135/180 vs 126/180) · D-024 (43/45/46 vs 44/38/42) |
| promotion battery pattern | D-023 three-seed ×180; clean floor per §13.6.4; frontier judge §9.9 |
| all of this is precompute | directive 11; the shipped artifact is always a frozen NP_VERSION + KAT |
