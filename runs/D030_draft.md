# D-030 (DRAFT) — 2-engine entry-divert re-authorization: the first engine-out recovery lever

**Status:** built, byte-clean, swept; winner found (bank=35, KR×4, KV×2.5 → s42 8/60); cross-val + freeze pending.
**Canon:** §4.6 (engine-out), §9.9 (frontier metric), §13.6 (leak gate + held-out law), directive 3 (fixed plant).
Follows D-029 (composite null) which located the bottleneck. This is E1.5 on the ROADMAP.

## Motivation (D-029 phase-attribution)
The engine-out failures are bimodal; the ~75% GROSS cluster is lost at the ENTRY-BURN CUT — the 2-engine
`entry_divert_step` (ZEM/ZEV, KR=2.0/KV=3.5, bank cap 15°) closes only ~830 m (vs ~2200 m for later
failures) and carries +22.9 m/s OUTBOUND at the cut: under-driven AND under-damped-for-the-regime. Its
authority `amax = n_eng·thrust·sin(15°)/m` drops to 2/3 under engine-out exactly when the offset must
close. D-027's frontier (D_phys_2eng ≥ 12,656 m) says the closure is physically available.

## Implementation (byte-clean)
`entry_divert_step` (sim.c): under `n_eng<3` only, OPEN the bank cap and stiffen the ZEM/ZEV gains.
The entry burn runs at LOW qbar (~0.2–40 kPa vs the ~80 kPa STRUCT line) ⇒ huge headroom to bank harder.
`n_eng==3` uses 15°/KR=2.0/KV=3.5 EXACTLY ⇒ every clean golden reproduces (leak gate GREEN: selftest PASS,
TERMINAL 194/200, MPPI run-1 2.63/10.48). Tuned via env knobs (EO_BANK_DEG/EO_KR_MUL/EO_KV_MUL, read once)
for the sweep; the winner is FROZEN into the defaults (env dropped) for the shipped state.

## The sweep (neural ENTRY --engine-out random ×60 s42; baseline 1/60)
Two-stage sweep. Clear, narrow peak:
- bank 23, gains ×1 → 2/60 (bank alone barely moves it ⇒ the divert is GAIN-limited, not just authority-limited)
- 30/×3/×2 → 3 ; **35/×4/×2.5 → 8** ; 37/×4/×2.5 → 8 ; 33/×4/×2.5 → 7 ; 40/×6/×3 → 7
- over-aggression HURTS: 45/×8/×4 → 4 ; 35/×5/×2 → 0 (KV too low ⇒ overshoot) ; 35/×4/×3 → 3 (over-damped)

**Winner: bank=35°, KR×4, KV×2.5** = effective KR 8.0 / KV 8.75 (ratio ~1.1, near-critical) vs the baseline
2.0/3.5 (ratio 1.75). Physics: under 2-engine the divert needs FASTER closure (KR 2→8) with LESS
over-damping (ratio 1.75→1.1) to close before the qbar/fuel cut.

## [TBD] Cross-validation (held-out; the D-012 discipline) + frozen result
| ENTRY --engine-out random ×60 | s42 | s7 | s99 |
|---|---|---|---|
| neural, D-030 (35/4/2.5) | 8/60 | — | — |
| neural, baseline (D-029) | 1/60 | 0/60 | 0/60 |
| reactive, D-030 | — | — | — |
| reactive, baseline | 0/60 | 1/60 | 1/60 |

## Honest scope
D-030 is a PARTIAL fix — it lifts EO recovery from ~1/60 to ~8/60 (≈8×) by recovering the gross cluster's
easier draws, but the hardest gross draws (early failure × far offset) and the near-miss cluster's terminal
quality (td_v) remain. The remaining gap needs E2: an EO DAgger round retraining the neural policy on the
improved-entry-divert EO handoff (the policy is clean-trained; its aero+landing isn't tuned for the hot
2-engine handoff). D-030 unblocks E2 by giving it a teacher that actually recovers a fraction (vs the 1/60
that "would teach failure", D-025). Ships with the frozen params + this ADR.
