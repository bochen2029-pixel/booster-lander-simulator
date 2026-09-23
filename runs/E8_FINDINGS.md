# E8 findings — the critic's real baseline is not chance, it is "keep the elite"

*2026-09-23, branch `e-ladder`. Live notes as the farm runs; the committed number is the chain's
`critic_v1` flight on 42/7/99, not anything here. These are diagnostics on the candidate log.*

## v0 preliminary critic (3 of 24 seeds, 144k rows, ranking loss, full 39-ch observation)

Trained the moment seeds 7700/7708/7716 closed, to get an early read rather than wait for noon.
Undertrained on purpose (batch 256 → 17 steps/epoch × 15; the batch default is now 32), so read
these as a **floor**, not a level:

| metric | v0 | chance | E7 (for contrast) |
|---|---|---|---|
| top-1 agreement with the plant's argmin | **0.106** | 0.035 | 3–7% at ~6–12% chance (**at chance**) |
| regret, median | 2.27 | — | ~3.0 |
| P(critic pick lands \| plant best lands) | 0.592 | — | — |

**The roadmap's core bet survives its first contact with data:** full observation + a ranking loss
puts the critic **3× above chance**, where E7's twelve summary magnitudes sat *at* chance. The
signal E7 could not see is in the full observation.

## v0b — 6 seeds, batch 32 (4,005 steps): the critic is learning

| | v0 (3 seeds, 255 steps) | **v0b (6 seeds, 4,005 steps)** | keep-the-elite |
|---|---|---|---|
| top-1 | 0.106 | **0.145** | 0.354 |
| regret median / p90 | 2.27 / 25.6 | **1.49 / 18.7** | — |
| P(pick lands \| best lands) | 0.592 | **0.722** | 0.919 |
| train loss (chance ≈ 4.1) | 3.69 | **3.37** | — |

Every metric improves with data and gradient steps, so v1 (24 seeds, 30 epochs) will improve
again. Two readings: (1) extrapolated, arm B — the critic *alone* — likely still lands short of
keep-the-elite on P(land); (2) **regret p90 = 18.7** says one pick in ten costs 18× the best —
those are the crashers, and they are exactly what confirm-at-events is for. So arm C remains the
deployable candidate. (3) A loss/metric misalignment: listwise CE penalises "picked the second-best
lander" and "picked a crasher" identically; only the second kills a flight. `--pair_weight` (below)
weights pairwise pairs by |Δlog cost| so lander-vs-crasher dominates. **A/B'd (v0c, same six seeds,
same steps): top-1 0.148 vs 0.145, regret median 1.42 vs 1.57, p90 17.6 vs 18.7, P(land\|best lands)
0.736 vs 0.719.** Neutral on top-1, better on both flight-predicting metrics. **Enabled for v1.**

## v0c FLOWN on 42/7/99 (10:10) — the offline metrics do not predict flight, and confirm-at-events does not rescue it

| v0c (6 seeds, P(land\|best lands) 0.736, regret med 1.42) | 42 / 7 / 99 | total | PERFECT |
|---|---|---|---|
| **arm B** — critic alone, blind, event replan, 1/32 | 24 / 26 / 30 | **80/180 = 44.4%** | 0 |
| **arm C** — + `--rfly-critic-confirm 2` at events | 22 / 22 / 33 | **77/180 = 42.8%** | 0 |

Against: the constant 121/180, E4/E5 129/180, the cold 1/32 search 179/180. **The critic-driven
search flies worse than a fixed constant**, with zero PERFECT landings. Pre-registered branch:
**< 100 — the one-shot cost critic cannot capture the rollout.** (Pre-registered for v1; v0c is an
early read on a third of the data, but the gap is not one that 4× data closes.)

**Why the offline metric lied.** "When the plant's best lands, the critic's pick lands 74%" is
measured at states the *plant's* search visited. In flight, the critic's search picks slightly
worse at each of ~13 periodic replans; the state drifts off the training distribution; every later
ranking is made where the critic was never trained. Compounding error — the same mechanism as
π's covariate shift in D-041, one level up.

**Why confirm-at-events did not help (77 vs 80).** Confirm lets the plant choose among the
critic's top-2 plus the carried elite. By the time an event fires, the critic's *population* is
already off the basin — the plant is choosing the best of three bad options. The safety net is at
the wrong place: the damage is done across the periodic replans, not at the event.

**The sharper test this points at: confirm at EVERY replan** (`--rfly-critic-confirm-every`). The
plant rolls out the critic's top-2 + the elite at every replan — 3 rollouts instead of the cold
search's 16. Two readings, both decisive: **≈ 179** ⇒ the critic is a useful *proposer/prefilter*
and has earned a ~5× compute cut over the cold search; **≈ 80** ⇒ its proposals are worthless
even as a prefilter, and the whole "critic replaces rollouts" direction closes on this data.
Floor: with a garbage critic the plant picks the carried elite (keep-the-elite lands 92%/replan),
so arm D should not fall below the constant.

## The baseline that actually matters (measured on all 5,062 groups, no training)

Chance is the wrong yardstick. The search carries an **elite** (the previous solution, `gtheta`)
into every replan as candidate 0. So the trivial controller is *keep the elite and perturb a
little* — which is essentially what the 1/32 search already is. Measured directly:

- **P(plant-best == the elite) = 0.354.** A third of the time the incoming solution *is* the best,
  and just keeping it is a top-1 of 0.354 — **3.3× the v0 critic.**
- **P(the elite lands) = 0.919.** Holding the previous solution lands 92% of replans on its own.
- **P(the plant-best lands) = 0.965.** So the search's entire advantage over keeping the elite is
  **+4.6 percentage points of landing-probability per replan.**
- P(plant-best == the start mean) = 0.000 — the un-refined warm start is never best; refinement is
  doing real work, just a thin sliver of it per replan.

## What this reframes

1. **The critic's job is smaller and harder than "rank like the plant."** It must beat a strong
   incumbent (the elite, 0.354 / 0.919) on a per-replan margin of ~4.6 points. A critic that ranks
   *worse* than keep-the-elite would fly *worse* than just holding the previous solution — so
   "above chance" is necessary and nowhere near sufficient. The number to beat is **0.354**.
2. **Phase 2 (`--rfly-critic-confirm`) is probably load-bearing, not a nicety.** If the critic
   only needs to be right at the events that decide the flight, and the plant confirms its top-K
   there, then a mediocre critic can still land: the elite holds the periodic replans (92%) and
   the plant backs the critic at the events. **Read arm C (1/32 + confirm 2) as the deployable
   candidate; arm B (critic alone) is the clean measure of the critic's own quality.**
3. **A cheap, honest fallback exists if the critic never beats the elite:** the search restricted
   to *keep-the-elite + confirm-at-events* is already a controller worth a ledger row, and it needs
   no net at all. Worth measuring alongside if arm B disappoints.

## Pre-registered, before `critic_v1` flies (unchanged from the roadmap, sharpened by the above)

- **arm B ≥ 160/180** → the critic ranks well enough to fly alone → phase 2 hardening.
- **100–160** → right where it trained, wrong where its own search goes → phase 1.5 (`e8_round.ps1`).
- **< 100** → the one-shot cost critic cannot capture the rollout → terminal-state head (§3).
- **New:** if **arm C ≥ arm B + a few draws**, confirm-at-events is doing the work the diagnostic
  predicts, and the deployable object is critic-proposes / plant-confirms, not the critic alone.
