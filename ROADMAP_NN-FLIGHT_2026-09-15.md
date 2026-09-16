# ROADMAP: the network flies the landing

> **AUDIT NOTE (2026-09-15, Claude Opus 5, read-only):** this plan was checked against disk in
> [`REVIEW_2026-09-15_NN-FLIGHT-AUDIT.md`](REVIEW_2026-09-15_NN-FLIGHT-AUDIT.md). The diagnosis
> holds and correctly overturns D-053. **Two numbers in §0's ladder have no receipt on disk** —
> E4/E5's 129/180 and E7's 16/60 and 52/180 were printed to stdout, which was never captured; E6's
> 179/180 does reproduce from its csvs. The E4/E5 plateau is what justifies "critic, not policy",
> so it is the one to re-fly first. The review also promotes §6's sealed 1/32 flight above phase 1
> and pairs it with D-057's unreconciled `iters_0.2` (180/180, 104 PERFECT).

*Written 2026-09-15 by Claude Fable 5.1 at Bo Chen's direction, at the end of a day of measurement in the worktree `C:\bl_e1` (this repo at HEAD 99c7f65). This file is the recommendation and the plan from here to completion. The evidence behind every claim is in `C:\ONE\ONE_BOOSTER_NN-ROOT-CAUSE_2026-09-15_S3.md`; the numbers below are all held-out 42/7/99 ×60 ENTRY engine-out unless stated. Nothing in this file has been committed; the worktree carries the code, this tree is untouched.*

## 0. Where we stand, in one table

| controller | held-out | what it is |
|---|---|---|
| identity | 28/180 = 15.6% | the reactive stack as shipped |
| July net (TP_VERSION 2) | 37/180 = 20.6% | regression onto the clairvoyant search's picks |
| best label-trained net (E3) | 95/180 = 52.8% | regression onto an anchored legal search's picks |
| ten-number constant (D-047) | 121/180 = 67.2% | a constant found by search |
| gain policy optimised on outcomes (E4, 70 params; E5, 498 params) | 129/180 = 71.7% | the feedforward plateau, reached twice |
| blind search, event replan, 1/8 budget | 175/180 = 97.2% | the deployable row (D-054); 585/600 sealed |
| **blind search, event replan, 1/32 budget** | **179/180 = 99.4%** | sixteen plant rollouts per replan, about a second per flight (E6) |
| clairvoyant search | 180/180 | sees the fault's future |

Three facts decide the plan.

1. **The search's decisions cannot be imitated.** At any state the search returns an arbitrary point in a wide, flat basin of gain vectors that all land; the average of such points is identity, the one point that crashes. Measured on the July corpus (t=0 label spread equal to the corpus spread, R² about zero from the state) and again on two fresh corpora. Anchoring the search fixes the average and is worth 32 points; it cannot give a regressor a state-dependent schedule, because the net's held-out error per coordinate equals the constant-mean baseline on every coordinate.
2. **A feedforward map from these features to gains plateaus near 72 percent no matter how it is trained.** Two policy classes, optimised directly on landing outcomes with common random numbers and held-out validation, landed on the same number.
3. **Everything above 72 percent is the plant rollout's judgment of a candidate.** Sixteen candidates scored by rolling out the plant land 99.4 percent. The search is barely searching; it is checking a handful of local perturbations against the plant and keeping the best. That judgment, cost as a function of state and gains, is a deterministic function the search evaluates thousands of times per farm. The first critic built to learn it (E7: twelve summary features, 93 thousand evaluations) ranks candidates at 7 percent agreement with the plant and flies 29 percent. It improves with data and is capped by its features.

So the network that flies this landing is not a policy that outputs gains. It is a critic that scores gains, sitting inside the search the repo already trusts, with the plant rollouts removed. The rest of this file is how to build that, in order, with the number that decides each step.

## 1. The exact next step

**Build the critic properly: full observation, ranking objective, designed candidates. Then fly it.**

### 1.1 Data (C, one session)

In the worktree, extend the candidate log the search already writes (`--rfly-cand-log`, `core/guidance_rfly.c`, gated byte-identical when off):

- Replace the twelve summary features with the full 39-channel legal observation at the replan, built by `policy_build_obs` exactly as the tap and the nets consume it (stash `obs39` in `RflyState` from the GM_RFLY block in `sim.c`, where `nav` and `phist` are in hand). Add the search's current mean gain vector at the replan (the point the candidates perturb) and the rollout's terminal summary (landed flag, touchdown speed, lateral miss, tilt, fuel margin). Row: t, seed, run, big, obs[39], mean_theta[10], cand_theta[10], cost, landed, td_v, td_lat, tilt, fuel_margin: 68 doubles.
- Add a designed candidate set per replan behind a second flag (`--rfly-cand-design N`): the elite, plus and minus one-coordinate steps on each of the ten gains at two step sizes (40 rollouts), plus N random local perturbations at the sampler's own spread. These extra rollouts are logged only; the search's own candidate loop and its outcome stay byte-identical. About 60 extra rollouts per replan is one second on sixteen threads; a flight costs about twelve seconds.
- Farm the deployable teacher with both flags on training seeds (a new band, 7700 to 7723, 60 draws each, 1,440 flights, about five hours on the box in three processes) under the wake-locked script pattern (`runs/e7_cand_farm.ps1`). Arm the policy tap as well; it is what populates the run index in the rows.

### 1.2 Training (Python, one session)

`runs/e7_train_critic.py` becomes a ranking trainer:

- Input: obs[39], mean_theta[10], cand_theta[10] (59 channels), standardised; drop the seven constant channels D-053 found.
- Loss: within each replan group, a listwise softmax over the candidates with the plant's argmin as the label, plus a pairwise term (RankNet) on all pairs whose costs differ by more than a margin, plus a small auxiliary regression on relative log cost. The critic is consulted only to rank, so it is trained to rank.
- Architecture: a state encoder (39 to 128) and a gain encoder (20 to 64) joined into two hidden layers of 256, tanh, one output. Best-validation checkpoint, split by run.
- Metrics that matter, on held-out runs: top-1 agreement with the plant; regret (cost of the critic's pick divided by the best cost, median and 90th percentile); and the one that predicts flight, the share of replan groups where the critic's pick lands when the plant's pick lands.
- Export in the text format `rfly_load_critic` reads; the C forward pass is fixed-order fp64 like every other law here. `guidance_rfly.c` grows a 59-input variant of `rfly_critic_eval`; the sampler loop in `rfly_replan_critic` is unchanged.

### 1.3 The flight, and the number that decides

Fly `--rfly --rfly-critic critic_v1.w --rfly-event-replan` on 42/7/99 ×60 with the sampler at budget 1.0 (forward passes are free, so use the full population).

| result | meaning | then |
|---|---|---|
| 160/180 or better | the judgment transfers; the network flies the engine-out battery at reflex speed | phase 2 |
| 100 to 160 | the critic is right where the search visited and wrong where its own search goes | phase 1.5 |
| under 100 | the one-shot cost regressor cannot capture the rollout; the alternative in phase 3 | phase 3 |

Expected, stated now: 130 to 170 on the first flight, because the critic has never been trained at the states its own search reaches.

### 1.5 Correct the critic where it is consulted

The expert-iteration loop, for the critic rather than for a policy. Fly the critic-driven search on training seeds; at every replan, evaluate the critic's candidate set with the plant as well and log both the critic's scores and the plant's costs; retrain on the union; repeat three to five rounds. The visited-state cost curve the July DAgger printed is the metric to watch here too, round over round. Each round is one farm of 360 flights and one training run. Decision after five rounds: 170/180 or better proceeds to phase 2; a plateau below that goes to phase 3.

## 2. Deployment design: propose, rank, confirm

Once the critic ranks well, the deployable flight computer is the fast-inverse-square-root shape: a cheap approximation and one exact correction.

- Periodic replans (every 10 s): the critic alone ranks the sampler's population; no rollouts.
- Event replans (the sensed engine count changes, and the entry-burn cut): the critic ranks, and the top two candidates are confirmed by real plant rollouts before commit (two rollouts, under a quarter of a second). This keeps the search's guarantee at the moments that decide the flight.
- Measure against the 1/32 cold search on the same 180 faults: rate, PERFECT count, lateral miss, wall-clock per flight. The bar: within two draws of the cold search and within 1.5 times its lateral miss. Then fly it once on a fresh sealed band (9300 to 9309 ×60) for the number that gets quoted.
- Two honesty checks the ledger already asked for: the flags-hidden litmus (evaluate with the engine-health channels zeroed so the critic must read the accelerometer and angular-acceleration channels) and `--nav-noisy`, which degrades the finite-difference accelerometer (H1 in D-041); if that costs more than a few draws, `nav.c` gets a real specific-force measurement first.

## 3. If the one-shot critic stalls

The critic predicts a rollout's cost from its start state and gains in one shot. If ranking agreement stalls under 50 percent after phase 1.5, the fix is not a bigger net; it is a surrogate that predicts the rollout's terminal state (touchdown speed, miss, tilt, fuel) rather than a scalar, trained on the same rows, with the cost recomputed from the prediction. Terminal quantities are smoother in the inputs than the cost, which folds a threshold at every verdict boundary. Same data, same C loader, one more output head. Beyond that lies a learned coarse-step dynamics model rolled forward by the search, which is a different project and should not be started unless both critic forms fail.

## 4. The compound showcase on the network

The engine-out battery is the yardstick because it has a physical bound; the showcase is engine-out times gust times moving deck. The pipeline transfers by adding those regimes to the candidate farm (`--gust`, `--target`, `--sea`, the D-040 recipes) and retraining; nothing else changes. Order: pass phase 2 on the engine-out battery first, then farm the compound recipes, then the held-out compound 36-draw battery at the 36/36 bar the search holds, then the live cockpit with the asynchronous replan path, which the critic makes trivial because a replan costs microseconds. The sealed pool for the headline is flown once, last.

## 5. The plant epoch

D-060 measured the aero table at about half the CFD's drag and normal force at low Mach, and D-046 found the target fed rather than sensed and no actuator lag. Changing the plant resets every rate in the repo. Recommendation: prove the method on the current plant through phase 2, because the method is the result; then switch plants and rerun the pipeline end to end. The rerun is cheap once the pipeline exists, and how much of the critic transfers across the plant change is itself a finding worth a ledger entry. Do not quote a sealed-pool number for the network on the old plant as anything but old-plant.

## 6. Housekeeping that should happen first, in parallel

- Merge the worktree's seven default-off flags into the main tree as one commit after re-running the gate battery here: `--rfly-det-stream`, `--rfly-anchor`, `--rfly-anchor-w`, `--rfly-mlp`, `--rfly-mlp-warm`, `--rfly-cand-log`, `--rfly-critic`. Every one is byte-identical when off (selftest PASS, TERMINAL ×200 equal to the golden, the deployable arm's manifest identical with the flags absent). Your word, not mine, commits it.
- Fly the 1/32 cold search once on a fresh sealed band. It beat the 1/8 row by four draws on the dev pool at a quarter of the compute, and D-057's budget sweep was already circling this; it deserves its own row and its own sealed number.
- Four ledger entries, written from the report: the label diagnosis and E1 to E3; outcome optimisation E4 and E5 and the 72 percent plateau; E6 and the 1/32 result; the critic E7 and this roadmap. The July nulls (D-031, D-032, D-041, D-042) get their explanation: the students were regressing onto arbitrary picks, and the one that had authority was a noisy constant.
- Retire two ideas from the plan for good: regression onto the search's picks at any scale, and warm-starting every replan from a policy (nulled in July and again today).

## 7. What not to spend on

- More tap data for a gain-schedule regressor. It yields a constant.
- A larger feedforward policy on the twelve features. It yields 72 percent.
- A fleet. Everything today ran on this box: a 720-flight farm in seventy minutes, an 80-iteration outcome optimisation in 66, a critic farm in twenty. Phase 1 is a day of this box.

## 8. Timeline

| phase | work | wall time | the number |
|---|---|---|---|
| 6 (housekeeping) | merge, sealed flight of 1/32, four entries | half a day | 1/32 on a sealed band |
| 1 | full-observation candidate log, designed candidates, ranking critic, first flight | two days | held-out with the critic alone |
| 1.5 | three to five correction rounds | two to three days | 170/180 |
| 2 | propose-rank-confirm flight computer, honesty checks, sealed band | one day | within two draws of the cold search |
| 4 | compound regimes, showcase battery, live cockpit | three days | 36/36 held-out compound |
| 5 | plant epoch and rerun | separate | the transfer |

Completion, defined: a network, legal and byte-deterministic, that lands the held-out engine-out battery within two draws of the plant search at reflex speed, then the compound showcase live, then the same on the corrected plant. The path to it goes through the critic, and the critic goes through the full observation and a ranking loss. That is the next step, and it is a single session of work before the first number comes back.
