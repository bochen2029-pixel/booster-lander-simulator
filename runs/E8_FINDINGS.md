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

## 2026-09-24 — the matched-budget control, pre-registered before its first number (cloud session)

*Written at 15:03 UTC on 2026-09-24, while `--rfly-rollouts 3` is in flight on 42/7/99 and before
any of its seeds has finished. Linux build of `681acb9` (gcc -O2 -ffp-contract=off), gated:
TERMINAL ×200 byte-identical to `runs/n0main_terminal.txt`; the 1/32 teacher on s42 ×60 lands
60/60 here, matching E6's receipt for that seed.*

**What arm D's pre-registration left out.** "≈179 ⇒ the critic earned a ~5× compute cut; ≈80 ⇒ its
proposals are worthless" has no row for *what three rollouts buy with a proposer that knows
nothing*. Every budget point on record is ≥16 rollouts per replan (the `--rfly-budget` floors POP 8
× ITERS 2). `--rfly-rollouts R` flies exactly R per replan: the carried elite plus R−1 sampler
draws, one generation, the plant keeps the best.

**And the floor arm D assumes is not in its code.** Its confirm set is {critic's global best, the
last iteration's top 2}; the carried elite is in it only when the critic ranks it first, and slot 1
duplicates slot 0 whenever the last iteration's best is the global best. `--rfly-critic-confirm-elite`
keeps the incoming solution in slot 0 unconditionally and fills 1..K with the critic's best
*distinct* candidates. Smoke (random critic, s42 ×2): plain confirm-every crashed 2/2 and never
picked slot 1 in 26 confirms; with the elite kept, 2/2 landed.

**Reads, fixed now:**
- **R3 ≥ 175/180** — three rollouts with random proposals already fly within ~4 draws of the
  16-rollout search. Arm D's "≈179" branch then cannot distinguish a useful critic from a useless
  one, the "~5× compute cut" needs no network, and the critic-as-proposer question moves to R = 2
  (the elite + one proposal), where the control has room below it.
- **150 ≤ R3 < 175** — the room exists. The critic's value as a proposer is **De − R3** on the same
  180 faults (De = confirm-every + confirm-elite, K = 2, three rollouts); ≤ +2 draws is nothing.
- **R3 < 150** — proposals matter at this budget; De − R3 is the measurement, and plain D vs De
  separates the critic from the missing floor.
- R2, R5, R9 map the rest of the curve; no read is attached to them in advance.

### The critic arms, pre-registered 15:08 UTC — before the critic exists and before R3 has landed

The Windows farm and `critic_v0c.w` never reached git, so the critic is rebuilt here from the v0c
recipe: the same six farm seeds (7700/7701/7708/7709/7716/7717 ×60, 1/32 teacher + the 41-step
designed set, `runs/e8_cand_farm.sh`), `e8_train_critic.py --epochs 15 --hidden 256 --batch 32
--pair_weight 4.0 --seed 0` (v0b's 4,005 steps = 267 steps/epoch × 15). Call it **c0**: v0c-class,
not v0c — same rows if the plant is bit-identical across platforms (TERMINAL and the s42 teacher
say it is so far), a different torch build. Each arm flown ONCE on 42/7/99 ×60, blind + event
replan, the critic's CEM at 1/32, per-draw csv receipts for paired reads:

| arm | flags | plant rollouts per replan | its control |
|---|---|---|---|
| **B** | critic alone | 0 | — (v0c flew 80/180) |
| **D** | `--rfly-critic-confirm 2 --rfly-critic-confirm-every` | 3 slots, ~2 distinct, no elite floor | R3 |
| **De** | D + `--rfly-critic-confirm-elite` | 3: the carried elite + the critic's top 2 distinct | **R3** |
| **De1** | `--rfly-critic-confirm 1 --rfly-critic-confirm-every --rfly-critic-confirm-elite` | 2: the elite + the critic's top 1 | **R2** |

**Reads:**
- **B** is the reproduction check: **60–100/180** ⇒ c0 stands in for v0c; outside that, the other
  arms are c0's numbers, not a verdict on v0c.
- **The critic's worth as a proposer is De − R3 and De1 − R2**, on the same 180 faults, read with
  the per-draw flips: **≥ +5 draws, flips ≥ 2:1 in the critic's favour** ⇒ its proposals beat random
  ones at that budget; **within ±4** ⇒ no measurable proposer value; **≤ −5** ⇒ it steers the
  population somewhere worse than chance does.
- **De − D ≥ 5** ⇒ the missing elite floor was costing plain arm D, and any Windows arm-D number is
  a floor-less number and must be read that way.
- Stated before the number: **De ≈ R3 ± 4, and D < De.** Offline, v0c ranked below keep-the-elite
  (top-1 0.148 vs 0.354), so its preference among seven sampler draws should carry little the
  plant's own pick of three does not already get.

### R3 LANDED (15:11 UTC): **142/180 = 78.9 %**, 1 PERFECT — the read is the "R3 < 150" branch

| `--rfly-rollouts 3` | s42 | s7 | s99 | total | PERFECT | crashes (off-pad / too-hard / fuel-out / other) | LOC |
|---|---|---|---|---|---|---|---|
| elite + 2 sampler draws, one generation | 51 | 43 | 48 | **142/180** | 1 | 19 / 10 / 2 / 7 | 6 |

~11.4 s/flight on one core. **Three rollouts with nothing choosing them already beat every
network-driven arm on record** (v0c B 80, C 77; E4/E5 129; the constant 121) — and sit **37 draws
under** the 16-rollout, two-generation search (E6: 179). So the budget between 3 and 16 rollouts is
worth most of the battery, and a proposer has real room to show itself: **De − R3 is a live
measurement**, not a ceiling artefact. The PERFECT count (1 vs E6's 24) says precision is bought by
search width, the same finding D-054 made at the top of the budget curve.

### R2 LANDED (15:22 UTC): **111/180**, and R3 re-flown with per-draw csv reproduces to the replan

| control | s42 | s7 | s99 | total | PERFECT | plant rollouts / flight |
|---|---|---|---|---|---|---|
| R1 = identity (the elite alone never moves off the warm start) | — | — | — | 28/180 (D-047) | — | 14 |
| **R2** elite + 1 draw | 35 | 38 | 38 | **111/180** | 1 | ~28 |
| **R3** elite + 2 draws | 51 | 43 | 48 | **142/180** | 1 | ~42 |
| 1/32 search, two generations of 8 (E6) | 60 | 59 | 60 | 179/180 | 24 | ~224 |

Paired R2 → R3 on the same draws: **40 flips up, 9 down, +31, sign test p = 9e-6**; on the 102 draws
both land, mean lateral 9.7 → 6.5 m. The budget curve is steep at the bottom — every plant-judged
candidate is worth ~30 draws here — so a critic that chooses *which* two candidates the plant sees
has a large target, and De1 − R2 is measured on the steepest part of the curve.

**Determinism receipt (R3b = R3 re-flown with `--out`):** stdout identical but for the `wrote <csv>`
line `--out` adds; **stderr — every replan's pick and all ten gains, ~2,540 lines — byte-identical**
on all three seeds.
