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

### Gate: De with a critic that knows nothing IS R3, to the replan (15:35 UTC)

A constant critic (all weights zero, so every candidate scores the same) under De's flags
(`--rfly-critic-confirm 2 --rfly-critic-confirm-every --rfly-critic-confirm-elite`): ties go to the
lowest index, slot 0 of iteration 0 is the elite (skipped as a duplicate), so the proposals are
draws 1 and 2 of iteration 0 — the same normals, from the same per-replan seed, that R3 draws. On
s42 ×10: **per-draw csv byte-identical to R3's first ten rows, and the committed gains identical at
all 143 replans.** So De and R3 are one code path that differs only in the ranking the critic
supplies: **De − R3 is the critic's information and nothing else** — not a budget difference, not
a sampler difference, not the confirm mechanics.

### Where does the width pay? Three decomposition arms, pre-registered 15:37 UTC, before any flies

`--rfly-rollouts 3 --rfly-rollouts-at LIST` puts R3 at the listed replan kinds and the 1/32 search
(16 rollouts, two generations) at the rest. The corners are measured: R3 everywhere 142, 16
everywhere 179 (E6). Per flight there is one t0 solve, one or two periodic replans before the
fault, one event replan, and ~11 periodic replans after it.

| arm | R3 at | the 16-rollout search at | ~rollouts / flight |
|---|---|---|---|
| **rich_event** | t0, periodic | event | ~57 |
| **rich_t0event** | periodic | t0, event | ~70 |
| **rich_periodic** | t0, event | periodic | ~200 |

**Reads, fixed now:**
- **rich_event ≥ 175** ⇒ the search's width is needed only where the fault is handled; t0 and the
  periodic replans can be nearly free. That is the shape §2's propose-rank-confirm assumed, a ~4×
  compute cut with no network — and it would say v0c's arm C failed because its critic-only
  periodic replans were worse than *three random rollouts*, not because periodic replans need width.
- **rich_event ≤ 150** (within ~8 of R3) ⇒ width at the event alone buys little; the damage is done
  at t0 and/or the periodic replans, as E8 read v0c's arm C. rich_t0event vs rich_periodic then says
  which of the two.
- Stated before the numbers: **the event carries most of it** (D-052: the event replan alone was
  worth +19 draws of 180 at full budget; the stderr of s42 run 0 shows the 16-rollout event solve
  committing a plan of cost 1303 where R3 settles for 4564) — **rich_event ≈ 160–175.**

### The farm and c0 (18:34 UTC): the cloud reproduces v0c to the third decimal

**Farm** (`runs/e8_cand_farm.sh`, 6 seeds in parallel, 217 min on 4 cores): the teacher landed
57–60/60 per seed (353/360); **288,420 rows, 10,108 groups, 360 runs** (`runs/e8_cloud/farm/`,
sha256 of every `.cand` recorded). Per replan (`stats_teacher.txt`, 5,048 replans): the carried
elite's rollout lands 0.885, elite + 2 draws 0.908, the teacher's pick 0.960 and equals the elite
0.493 of the time.

**c0** (`e8_train_critic.py --epochs 15 --hidden 256 --batch 32 --pair_weight 4.0 --seed 0`):
**267 steps/epoch × 15 = 4,005 steps — v0b/v0c's exact count** — and the best checkpoint (epoch 10)
scores **VAL top-1 0.148, regret median 1.42, P(pick lands | best lands) 0.736: v0c's three
figures exactly**; the last epoch's regret p90 is 17.66 (v0c: 17.6). Same rows, same split, same
seed, same schedule, a different torch build on a different OS — and the same critic to three
decimals. **c0 stands in for v0c**; arm B is the check that it also flies the same (80/180).
Dead inputs: 9 of 39 observation channels are constant in this corpus (15, 18, 23–29).

**Offline, as a proposer** (`stats_critic.txt`, the trainer's 54 held-out runs, 777 replans): the
critic's pick of 1 draw out of 7 lands **+2.3 points** more often than a random draw (0.931 vs
0.907), its top 2 **+1.5** (0.934 vs 0.919). Small per replan — but R2 → R3 was only +1.2 points
per replan offline and **+31 draws in flight**; the flights decide. Notably, the critic's single
pick (0.931) out-lands two random draws (0.919) per replan.

## RESULT (18:44 UTC): **De = 170/180 — the critic IS a proposer.** Plain arm D = 137/180.

| arm (c0, blind, event replan, 42/7/99 ×60) | s42 | s7 | s99 | total | PERFECT | crashes: off-pad / too-hard / other | LOC |
|---|---|---|---|---|---|---|---|
| **De**: carried elite + the critic's top 2 distinct, plant confirms every replan (3 rollouts) | 60 | 56 | 54 | **170/180** | 1 | 7 / 3 / 0 | 0 |
| **D**: the 09-23 design, {critic best, last iter top 2} (3 slots) | 40 | 49 | 48 | **137/180** | 0 | 24 / 12 / 7 | 3 |
| R3: elite + 2 random draws (3 rollouts) — the matched control | 51 | 43 | 48 | 142/180 | 1 | 19 / 10 / 7 | 6 |
| the 16-rollout search (E6) | 60 | 59 | 60 | 179/180 | 24 | — | — |

**Paired, same 180 faults (`runs/e8_paired.py`):**
- **De vs R3: +28 draws — 36 flips to De, 8 to R3, sign test p = 2.5e-5.** Pre-registered read
  (≥ +5 and flips ≥ 2:1) **met, decisively: the critic's ranking picks better candidates than chance.**
- **De vs D: +33 draws — 38 to 5, p = 2.5e-7.** Pre-registered read (De − D ≥ 5) **met: the missing
  elite floor cost plain arm D a fifth of the battery.** Any arm-D number flown on the Windows box
  is a floor-less number.
- **D vs R3: −5, p = 0.63** — the 09-23 arm D is indistinguishable from random proposals. Had it
  been read alone against its own pre-registration (≈179 useful / ≈80 worthless), 137 would have
  sat between the branches and said nothing; against the matched control it says the design, not
  the critic, was the limit.

**My stated expectation was wrong.** I wrote "De ≈ R3 ± 4" because c0 ranks below keep-the-elite
on top-1 (0.148 vs ~0.35–0.49). Top-1 was the wrong lens: a proposer does not need to name the
single best candidate, only to put a lander among the two the plant checks, and at that it is far
better than chance. The offline proxy said the same thing in miniature (+1.5 points per replan),
and it compounded over ~14 replans into 28 draws, as R2 → R3 had (+1.2 points → +31 draws).

**What De is:** 170/180 at **~42 plant rollouts per flight against the search's ~224 (5.3×
fewer), within 9 draws of it.** It buys survival, not precision: PERFECT 1 (the search: 24) and a
mean lateral miss on the draws both land of 7.6 m (R3 6.3 m). The residual is off-pad 7 /
too-hard 3, with zero LOC. The 09-15 roadmap's deployment bar was "within two draws of the cold
search"; De is not there (−9), and the v1 critic, phase-1.5 correction rounds, and K or budget
changes are the levers that remain. **Arms B (c0 alone — the v0c reproduction check) and De1
(elite + the critic's top 1, vs R2) are flying.**

### B and De1 (18:49 UTC): the reproduction holds, and one critic pick beats two random ones

| arm | s42 | s7 | s99 | total | PERFECT | plant rollouts / replan |
|---|---|---|---|---|---|---|
| **B**: c0 alone, no rollouts | 26 | 26 | 30 | **82/180** | 0 | 0 |
| *v0c alone on Windows (09-23)* | *24* | *26* | *30* | *80/180* | *0* | *0* |
| **De1**: carried elite + the critic's top 1 | 54 | 52 | 53 | **159/180** | 1 | 2 |
| R2: carried elite + 1 random draw | 35 | 38 | 38 | 111/180 | 1 | 2 |

- **B = 82/180** is inside the pre-registered 60–100 band and matches v0c seed for seed on s7 and
  s99 (26, 30), two draws apart on s42 (the C forward pass runs glibc `tanh`, so near-tie rankings
  can flip). **c0 stands in for v0c; the 09-23 critic-alone verdict (< 100 ⇒ the one-shot critic
  cannot replace the rollouts) is confirmed on a second platform.**
- **De1 vs R2: +48 draws, flips 59:11, p = 4.5e-9.** One critic-chosen candidate against one random
  one, same code path, same faults.
- **De1 vs R3: +17, flips 33:16, p = 0.021** — two rollouts with the critic choosing beat three
  rollouts with nobody choosing. **De vs De1: +11, flips 14:3, p = 0.013.**

**The two curves, landed of 180 by plant rollouts per replan:**

| rollouts / replan | 1 | 2 | 3 | 16 (two generations) |
|---|---|---|---|---|
| random proposals (`--rfly-rollouts R`) | 28 (identity) | 111 | 142 | 179 (E6) |
| **c0 proposals (De1, De)** | — | **159** | **170** | — |
| c0 alone (B), 0 rollouts | 82 | | | |

**The read, in one line: the critic cannot replace the plant (82), but it is a strong proposer to
it — the plant keeps the last word at every replan, the critic decides which two candidates the
plant spends its rollouts on, and that lands 170/180 at a fifth of the search's rollouts.** This
is the ROADMAP_NN-FLIGHT §2 shape (propose, rank, confirm) with two corrections the flights forced:
the confirm must carry the carried elite, and it must run at every replan, not only at events.

### rich_event (19:10 UTC): **175/180 with no network** — the width pays at the event

| arm | s42 | s7 | s99 | total | PERFECT | ~plant rollouts / flight | crashes |
|---|---|---|---|---|---|---|---|
| **rich_event**: R3 at t0 + periodic, the 16-rollout search at the events | 60 | 58 | 57 | **175/180** | 3 | ~68 (12.2 × 3 + 2 × 16) | off-pad 3, too-hard 2 |
| R3 everywhere | 51 | 43 | 48 | 142/180 | 1 | ~42 (14.1 × 3) | 38 |
| De (c0 proposes, 3 rollouts everywhere) | 60 | 56 | 54 | 170/180 | 1 | ~42 (14.2 × 3) | 10 |
| the 16-rollout search everywhere (E6) | 60 | 59 | 60 | 179/180 | 24 | ~227 (14.2 × 16) | 1 |

- **rich_event vs R3: +33 draws, flips 33:0, p = 2.3e-10.** Sixteen rollouts at the TWO event replans
  per flight rescue 33 draws and never lose one. Lateral on common landings 6.5 → 4.9 m.
- **rich_event vs De: +5, flips 10:5, p = 0.30** — not separable at n = 180, at ~60 % more rollouts
  (68 vs 42 per flight).
- **Pre-registered read (≥ 175): met, at the threshold.** The search's width is needed where the
  fault is handled; the t0 solve and the periodic replans can run on three random rollouts. **That
  is a ~3.3× compute cut (68 vs 227 rollouts per flight) within 4 draws of the full search, with no
  network at all** — the shape §2 assumed (cheap periodic, expensive event), inverted in one place:
  the event gets the *search*, not a critic's top two.
- **It re-reads v0c's arm C (77/180).** Arm C confirmed at events and ran the critic ALONE at t0 and
  periodic. rich_event says periodic replans need only a little plant judgment — three random
  rollouts suffice — but arm C gave them none: B (critic alone) is 82. The damage was at the
  periodic replans, as E8 read, but the fix is three rollouts there, not a better critic.
- My stated expectation (160–175) held, at its top edge.
- **Correction (19:13 UTC), and it applies to the pre-registration's flight sketch above:** a flight has
  **two** event replans, not one. `--rfly-event-replan` fires on ANY change in the sensed engine
  count, so both the fault (t ∈ [4, 18] s) and the entry-burn cutoff (~t = 31 s) are events. Measured
  from the stderr of all 180 draws of R3, rich_event and De: 1 t0 + ~11.2 periodic + 2.0 events, the
  classifier checked against a flag-on run where the plant-searched replans are the events. The
  landed counts and every read are unchanged; the rollout accounting (first pushed as ~56 per flight,
  "~4×") is corrected above to ~68 and ~3.3×.

**Next, the arm this points at:** the critic proposing at t0 + periodic (De's 3 rollouts) and the
16-rollout search at the event — the critic's +28 over random where it is worth something, the
search's width where it pays. rich_t0event and rich_periodic are flying.

### DeE, pre-registered 19:14 UTC (commit a6e7df2), before it flies: the critic where rollouts are scarce, the search where width pays

`--rfly-critic-event-search` (gated: off byte-identical on the De path; on, the two event replans
per flight run the plant's 1/32 search and every other replan stays critic-proposed): **DeE = De's
flags + `--rfly-critic-event-search`.** Its cost is rich_event's exactly — 3 rollouts at t0 and the
~11 periodic replans, 16 at the two events, ~68 per flight — and its events run the same code as
rich_event's. **DeE − rich_event is the critic's worth at t0 + periodic with the events searched.**

**Reads:**
- **≥ +3 draws, flips ≥ 2:1** ⇒ the critic adds on top of the event search; DeE is the best arm per
  rollout on record.
- **within ±3** ⇒ once the events are searched, the critic's +28 (De vs R3) is absorbed; the
  no-network rich_event is the design at this budget, and the critic's case rests on precision
  (PERFECT, lateral) or on smaller budgets.
- **≤ −3** ⇒ the critic's proposals hurt outside the events.
- Stated before the number: rich_event is 4 draws under the 179 ceiling, so there is little room;
  **DeE ≈ 176–179, within ±3 of rich_event on landed**, and — since De landed wider than R3 (7.6 vs
  6.3 m on common landings) — **no better on lateral.**
