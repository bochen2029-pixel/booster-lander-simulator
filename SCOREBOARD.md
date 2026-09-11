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
| GM_MPPI | **4/60** (s42) | 0P · 0G · 4 HARD · 56 CRASHED, **50 off-pad**. Same binary and byte-identical faults as the row above | D-046 control |
| reactive + D-030 | 9–10 / 60 | the 2-engine entry-divert re-authorization | D-030 |
| **constant θ = identity** (`--rfly-fixed 1,…,1,0,1`) | **9/60** (s42) | 1P·3G·5H·51C, off-pad 36. **Validates the rig**: constant-θ GM_RFLY ≡ the reactive stack with D-030. **0.39 s/run** vs the CEM's 76 s | D-047 probe |
| constant θ = D-046 run-0's own converged θ, frozen | 9/60 (s42) | reproduces D-042 — a converged θ held constant is no better than identity | D-047 probe |
| constant θ = divert knobs at the box ceiling | **13/60** (s42) | hand-picked. Headroom exists; and the landscape is **non-monotone** (partway = 5/60, all the way = 13/60) | D-047 probe |
| **constant θ, CEM-optimized** (10 numbers) | **248/360 = 68.9%** | full training pool, paired. **identity on the same pool is 37/360 = 10.3%** — the shipped baseline was never a baseline. Zero inference, zero privilege, 0.39 s/run | D-047 |
| **conditional policy** (70 params, 6 legal features) | **259/360 = 71.9%** | paired, same pool. **+3.1 pts over the constant = 16% of the 18.9-pt adaptation gap.** A finding about the REPRESENTATION, not about learning | D-050 |
| GM_NEURAL v6 + D-030 | 8/4/2 of 60 = **14/180** | | D-030 |
| *GM_MPPI, pre-D-030* | *1/60* | ⚠ **cross-version — do not quote as a control.** D-030 lifts EO mode-independently | E0 |
| *GM_NEURAL v6, pre-D-030* | *1/0/0 of 60* | ⚠ same caveat | E0 |

**THE GAP, AS OF 2026-09-11 — and it is not what it was thought to be.** The standing reading was
that the search's 100% was bought with *privilege and latency*, and that everything deployable was
stuck at 1–9/60. **①b measures both halves and both were wrong.**

- **Privilege was worth 12.2%, not 90%.** Blind GM_RFLY — which never consults the fault's future —
  lands **158/180**, with the cost falling on whole flights (LOC 13, FUEL 5) rather than on
  accuracy (precision 0.33 → 0.58 m, still sub-metre).
- **Latency was a framing error.** R2b measured the budget dropping 8× with no rate loss
  (**~0.4 s/replan**) against an outer loop that replans at **0.1 Hz** — real-time by ~25×. The
  10 µs bar three arcs of distillation chased belongs to the 500 Hz *inner* loop; a mission-layer
  setpoint never had it.
- **And "everything deployable sits at 1–9/60" rested on `identity`**, now known to be an
  arbitrary unoptimized point (5/60 on training seeds, 9/60 on the lucky seed 42) rather than a
  baseline — **37/360 = 10.3% on the full training pool.** An optimized constant reaches
  **248/360 = 68.9%** on that same pool, paired. *(The "~34/60 honest" quoted earlier on 09-11 was
  a two-seed estimate and was pessimistic; the four-fresh-seed figure puts the winner's curse at
  −4%, not −19%.)*

**So a legal, real-time controller that recovers 87.8% of an in-frontier draw distribution exists
on this disk today, and needs no net, no teacher and no distillation.** What remains open is the
22 draws — an attitude/margin problem, not a guidance one.

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

---

*Held-out law: seeds 42/7/99 never appear in training data (enforced in the trainer, twice).
Every rate above is on held-out seeds unless the row says otherwise. Cross-version rows are marked
⚠ and must never be quoted as a control — the D-030 boundary in column A is exactly the trap that
caught this file's author on 2026-09-08.*
