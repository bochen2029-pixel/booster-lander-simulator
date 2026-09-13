# SCOREBOARD — every controller × every battery, on one page

> **Why this file exists.** The 2026-09-08 post-mortem on D-046 asked how a project this
> disciplined could hold a **~59/60 physical bound** (D-027) and a **~9/60 achieved rate** (D-030)
> for seven weeks without anyone putting them side by side. The answer was structural, not a
> lapse: those two numbers live in different ADRs, hundreds of lines and weeks apart, each
> individually correct and honestly reported. **DECISIONS.md is a ledger — chronological, 240 KB —
> and a ledger is not a scoreboard.** The gap existed only when the numbers were adjacent, and
> nothing in the repo ever made them adjacent.
>
> This file is the fix. One row per controller per battery, **the physical bound as a permanent
> column**, every cell citing its ADR. Update it in the same breath as DECISIONS.md.
>
> **Read the bound column first.** A rate without its frontier is a number without a denominator:
> §9.9's whole point is that `P(land | in-frontier)` separates *how good is the controller* from
> *how hard is the scenario*, and it is the only metric this project treats as a yardstick.

---

## A · ENTRY engine-out — `--scenario entry --engine-out random ×60`, held-out seeds 42/7/99

**Physical bound: in-frontier = 1.000 ⇒ ~59/60 claimable (D-027).** Essentially every draw in this
distribution is physically recoverable, measured with `runs/sandbox/ceiling_eo.c` — and the oracle
reports 1.000 at *every* gimbal-debit level it models (40/60/80%, i.e. `steer_frac` 0.60/0.40/0.20).

> **A caveat the ledger's summary had dropped, and how it closed.** `ceiling_eo_out.txt` says in
> its own words: *"this fraction is the **LATERAL-reach ceiling ONLY**. The TRUE ceiling on landed
> rate is min(lateral-in-frontier, attitude-recoverable, terminal-null-achievable)."* So the ~59/60
> quoted since D-027 was an upper bound on **one of three axes**, and every use of it as "the"
> frontier — including mine, all through 2026-09-08/09 — was quoting a bound looser than the real
> one. **D-046 closes it empirically:** GM_RFLY flew 180/180 on this exact draw distribution, which
> demonstrates the other two axes are satisfiable too. The theoretical caveat is now retired by
> measurement rather than by argument — the right way round.

| controller | rate | notes | ADR |
|---|---|---|---|
| **GM_RFLY** (CEM search) | **180/180 = 100%** | 147 PERFECT · 33 GOOD · 0 HARD/TIPPED/CRASHED · 0 faults · 0 crash causes. Mean lat 0.32/0.34/0.33 m. **Privileged: the search flies the true realization** | **D-046** |
| **GM_RFLY BLIND** (`--rfly-blind`) | **158/180 = 87.8%** | 52/52/54 per seed — **119 PERFECT · 34 GOOD · 5 HARD**; lat 0.58 m mean; FUEL 5, LOC 13. **Hiding the future fault costs 22 draws of 180 (12.2%), not 90%.** Costs whole flights, not accuracy: precision falls 0.33→0.58 m but stays sub-metre, while the clairvoyant arm-s ZERO faults become LOC 13 — the attitude loop is surprised by the torque step. Carries NO privilege ⇒ **legal to deploy**; with R2b's 8× budget cut it is ~0.4 s/replan against a 0.1 Hz loop | **D-046 add.3** |
| **GM_RFLY BLIND @ 1/8 BUDGET** | **166/180 = 92.2%** | *(superseded as the deployable row by D-054.)* 56/56/54; 24 P · 121 G · 21 H; FUEL 2, **LOC 3**. **Beats blind at FULL budget (158/180) while running 16× faster — 5.0 s/flight, ~0.38 s/replan against a 0.1 Hz loop.** Less optimization against a misspecified future is more robust: LOC 13 → 3. Cost is precision (119 P → 24, lat 0.58 → ~2.6 m). No privilege, no net, no teacher | **D-051** |
| **GM_RFLY BLIND + EVENT REPLAN, full budget** | **177/180 = 98.3%** | **144 PERFECT** · lat **0.41 m** · LOC **1** · FUEL 1. Seeds 42 and 7 both 60/60 with ZERO faults. `--rfly-event-replan` re-solves the moment the legal sensed `n_eng` changes. **The stale plan — not blindness — was the mechanism: LOC 13 → 1.** Privilege is worth only 3 draws (1.7%), not 22 | **D-052** |
| **GM_RFLY BLIND + EVENT + 1/8 BUDGET — SEALED POOL 9200–9209** | **585/600 = 97.5%** | **THE HEADLINE, flown ONCE on virgin seeds (D-055).** 95 P · 406 G · 84 H · 15 crashed (off-pad 7 · LOC 4 · too-hard 3 · fuel-out 1); lat 2.53 m; per seed 58/60/58/60/58/59/58/59/59/56. Same pool: identity **68/600 = 11.3%**, constant θ **382/600 = 63.7%** — both ~4 pp below their dev-pool figures, the deployable row unchanged ⇒ **ceiling compression, not improvement** (pre-registered read). Quote THIS number | **D-055** |
| **GM_RFLY BLIND + EVENT + 1/8 BUDGET** (dev pool) | **175/180 = 97.2%** | **THE DEPLOYABLE ROW (development number; sealed figure above).** 6.3 s/flight ≈ 0.5 s/replan vs a 0.1 Hz loop — **real-time by ~20×, zero privilege, no net/teacher/distillation.** Budget now buys only PRECISION (27 PERFECT vs 144; 2.4 m vs 0.41 m), not survival | **D-054** |
| GM_MPPI | **4/60** (s42) | 0P · 0G · 4 HARD · 56 CRASHED, **50 off-pad**. Same binary and byte-identical faults as the row above | D-046 control |
| reactive + D-030 | 9–10 / 60 | the 2-engine entry-divert re-authorization | D-030 |
| **constant θ = identity** (`--rfly-fixed 1,…,1,0,1`) | **9/60** (s42) | 1P·3G·5H·51C, off-pad 36. **Validates the rig**: constant-θ GM_RFLY ≡ the reactive stack with D-030. **0.39 s/run** vs the CEM's 76 s | D-047 probe |
| constant θ = D-046 run-0's own converged θ, frozen | 9/60 (s42) | reproduces D-042 — a converged θ held constant is no better than identity | D-047 probe |
| constant θ = divert knobs at the box ceiling | **13/60** (s42) | hand-picked. Headroom exists; and the landscape is **non-monotone** (partway = 5/60, all the way = 13/60) | D-047 probe |
| **constant θ, CEM-optimized** (10 numbers) | **121/180 = 67.2%** | **held-out 42/7/99, the SAME 180 faults as the blind/clairvoyant rows.** identity on this pool = **28/180 = 15.6%** — the shipped baseline was never a baseline. Zero inference, zero privilege, 0.39 s/run | D-047 |
| **conditional policy** (70 params, 6 legal features) | **123/180 = 68.3%** | held-out, same 180 faults. **+2 draws over the constant; per-seed −1/+3/0, mean +0.67 ± 1.2 SE — a NULL.** The earlier +3.1/16% was cross-pool AND in-sample (5000-5005 IS its training set) | D-050 add.1 |
| GM_NEURAL v6 + D-030 | 8/4/2 of 60 = **14/180** | | D-030 |
| *GM_MPPI, pre-D-030* | *1/60* | ⚠ **cross-version — do not quote as a control.** D-030 lifts EO mode-independently | E0 |
| *GM_NEURAL v6, pre-D-030* | *1/0/0 of 60* | ⚠ same caveat | E0 |

**THE GAP, FINAL FORM (2026-09-11, after D-052/D-054).** Three readings were held during this
arc and **two of them were mine and wrong**. The record, in order:

- *"The search's 100% is bought with privilege."* → **Privilege is worth 3 draws, 1.7%** (D-052).
  ①b first measured 12.2%, and even that was mostly something else.
- *"Latency requires distillation."* → **A framing error.** The 10 µs bar belongs to the 500 Hz
  *inner* loop; the mission layer replans at **0.1 Hz**, and the deployable arm runs at
  **0.5 s/replan — real-time by ~20×** (D-054).
- *"Everything deployable sits at 1–9/60."* → That rested on **`identity`, which is 28/180 on this
  pool** and was never a baseline at all — an arbitrary unoptimised point quoted for seven weeks.
- *"Less search is better."* → **A symptom, now retired.** The cheap search won only because it
  could not commit hard to a plan about to go stale; fix the staleness and full budget wins
  177 vs 166 (D-052).
- **What it actually was: a STALE PLAN.** `RFLY_REPLAN_DT` is 10 s and purely periodic while the
  fault fires at t ∈ [4,18] s. **LOC 13 → 1** once the vehicle re-solves when `n_eng` changes.

**So a legal, real-time controller recovering 97.2% of an in-frontier draw distribution exists on
this disk today, with no net, no teacher and no distillation** (D-054; 98.3% if you spend the
budget for precision).

**What remains open is the DENOMINATOR, not the controller.** The residual 3–5 draws are a tail —
one or two each of off-pad / too-hard / LOC / fuel, no dominant mechanism. And the bound above is
the **lateral axis only**; `ceiling_eo.c` says the true ceiling is
`min(lateral, attitude-recoverable, terminal-null)` and **the attitude axis has never been
computed**. 177/180 may already be AT the bound. See [PLAN.md](PLAN.md) §2.

## B · AERO held-out — ×60 × 3 seeds = 180. The M4 gate (≥90%, open since D-018)

**Physical bound: D_phys ≈ 1107 m lateral divert; the as-built controller realized ~0.70·D_phys
≈ 775 m (D-018).** The off-pad seeds live in that 775→1107 m gap: physically landable, past reach.

| controller | rate | notes | ADR |
|---|---|---|---|
| **θ̂ gain-schedule, TP_VERSION 2** | **171/180 = 95.0%** | 57/58/56 — **M4 GREEN**, first learned controller to clear it | **D-044** |
| θ̂ gain-schedule, TP_VERSION 1 | 162/180 = 90.0% | M4-BORDERLINE | D-042 |
| hoverslam (analytic baseline) | 131/180 = 72.8% | | D-042 |
| GM_NEURAL v6 | 46/60 (clean AERO) | teacher + 2 | — |
| GM_MPPI | 44/60 = 73.3% | the wall D-018 closed from four angles | D-018 |

**Methodology law, paid for here:** **`val_nrmse` is a poor proxy for landing rate.** TP_VERSION 2
has *worse* val_nrmse than v1 (0.2707 vs 0.2617) and the trainer called it **WEAK** — and it lands
**+9**. Gate on flying the candidate, never on the proxy (D-044).

## C · The compound showcase — engine-out × gust × moving deck, 3 seeds × 12 = 36

| controller | rate | notes | ADR |
|---|---|---|---|
| **GM_RFLY** | **36/36** | + the filmed N3 live demo; reproduces from a clean clone | D-040 |
| θ̂ as controller | 2/12 | the lookahead wall — search-necessary, confirmed three ways | D-042 |
| constant θ (`--rfly-fixed`) | 2/12 | = identity; run-0's own converged θ held constant crashes its own draw | D-042 |
| GM_NEURAL (BC + oracle-DAgger) | **0/12 at every round, NP7–NP12** | 5 rounds + a bounded probe, hard-stopped on its own terms. DAgger itself worked (visited-state cost −71%) | D-041 add. 10–11 |

**Re-read in light of D-046:** Phase 3's 0/12 was long read as *distillation is hard here*. It now
reads as **distillation failed against a teacher sitting at the ceiling** — which is a different
and more interesting result. The teacher was never weak; the channel was never there.

**And re-scoped by D-047 (2026-09-11).** On a *training* seed ×60, the optimized constant θ scores
**31/60 (51.7%)** on this axis against identity's **7/60 (11.7%)** — 4.4×. Apply D-047's measured
winner's-curse deflation (−19%) and the honest expectation is still **~42%**.

> **What that does and does not overturn.** It does **not** refute "the compound is
> search-necessary": the privileged search is at **100%** and the constant at ~half, so the search
> is still buying a great deal. What it retires is the stronger reading the phrase acquired —
> *that a non-search controller cannot fly the compound at all.* One can fly half of it, with ten
> numbers, no inference, no privilege, and 0.39 s/run. Quality is poor (0 PERFECT · 6 GOOD ·
> 25 HARD), one seed, and it is a training seed — so this is a direction, not a held-out result.
> **Every controller baseline on this axis was measured against `identity`, and identity is now
> known to be a badly tuned reference point rather than a neutral one.**

## D · Reach boundary — `--target line:D:80:0`, clean, GM_RFLY at full budget

| demand | result | miss | fuel left | ADR |
|---|---|---|---|---|
| 4 km | GOOD | **0.19 m** | 2345 kg | D-045 |
| 5 km | CRASHED | 84.5 m short | 2088 kg | D-045 |
| 6 km | CRASHED | 1070 m short | 2075 kg | D-045 |

Arrives **upright (2.9°) with 2.1 t aboard** and simply lands short — the honest signature of a
reach limit, not a control failure. **Scoping (D-045's own):** this is the measured *controller*
reach boundary, **not** a certified out-of-frontier claim. Certifying it needs `ceiling.c` re-run
for the ENTRY state; at a D-018-like 0.70 factor the true frontier sits near ~6 km, which would
make the 5 km case marginal rather than forbidden.

---

## The plant honesty column — what every number above is resting on

Two findings from 2026-09-08 (D-046), both verified, **neither repaired**:

- **The moving target is FED, not sensed.** `sim.c:411/:433/:445` write the deck's exact truth
  position with `target_valid=1, target_age=0.0`; `nav.c:78` confirms `--nav-noisy` adds **no**
  target noise. **`target_age` is a structural zero** — assigned `0.0` at three sites and nowhere
  else, while occupying protocol offset 264 and feeding the policy as `OBS_TAGE`: **one of the 39
  observation channels is a constant in every net trained this arc.**
- **No actuator or sensor lag, either direction.** `control.c:186-195` inverts torque into gimbal
  angles and applies them the **same tick**; `nav.c` has noise and gyro-bias random walk but **no
  transport delay**.

**Consequence for column A's bound:** `ceiling_eo.c` computes D_phys against *this* plant, so **a
BRS with actuator lag is strictly smaller** and `in-frontier ≈ 1.000` is an upper bound resting on
an optimistic plant. It does not rescue a ~50-draw gap — but part of that gap is a gift, and the
frontier shrinks when either finding is repaired.

## D · The plant with an attitude reference that can be LOST — the deployable controller × `--imu-platform` (D-058, 2026-09-12)

Column A's deployable row (blind + event replan + 1/8 budget, dev pool 42/7/99, the identical 180
faults) re-flown with a Block II gimbaled platform as the attitude source (`core/imu.c`), servo
rate limit swept. **The first measurement of the attitude axis of the ceiling.**

| servo limit | landed | PERFECT | HARD | lost (crashed) | lat | td_v | ADR |
|---|---|---|---|---|---|---|---|
| none (truth) | 175/180 | 27 | 21 | — | 2.39 m | 2.58 | D-054 |
| 180°/s | 174/180 | 25 | 16 | 0 (0) | 2.17 m | 2.61 | D-058 |
| 90°/s | 173/180 | 19 | 23 | 135 (3) | 2.32 m | 2.84 | D-058 |
| **45°/s** | **162/180 = 90.0%** | **2** | **84** | 73 (15) | **7.06 m** | **3.83** | D-058 |

The "reference LOST" latch is not what binds (135 losses at 90°/s cost two draws); the belief BIAS a
rate-limited servo carries through the flare is (at 45°/s the flights that never tripped the
floats still arrived 7 m off at 4 m/s). Peak gimbal demand on a nominal flight: 151°/s.

## E · The aero table vs a CFD body at low Mach (D-060, 2026-09-12)

| point | table (`dynamics.c`) | FluidX3D LBM/LES (23 cells/D, ±20–30 %) |
|---|---|---|
| CA, α = 0, M → 0 | 0.85 (body only; fins have no drag term) | **1.61 ± 0.20** |
| CN, α = 8°, M → 0 | 0.470 (2.0·α body + passive fins) | **1.01 ± 0.07** |

**Every rate in this file was flown on a plant with roughly half the low-Mach drag and half the
normal force at angle of attack that its own body produces.** Not yet a plant change (a new
ledger epoch); the operator's decision.

---

*Held-out law: seeds 42/7/99 never appear in training data (enforced in the trainer, twice).
Every rate above is on held-out seeds unless the row says otherwise. Cross-version rows are marked
⚠ and must never be quoted as a control — the D-030 boundary in column A is exactly the trap that
caught this file's author on 2026-09-08.*
