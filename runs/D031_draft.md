# D-031 (DRAFT) — E2: ENTRY engine-out DAgger round on the D-030 entry divert

**Status:** NP_VERSION 7 built + KAT-pinned; gates in flight. Verdict TBD (the EO recovery number).
**Canon:** §9.8, §13.6 (leak + held-out law), §19 (DAgger). Follows D-030 (the EO recovery lever) which
handed this round a real teacher. E2 on the ROADMAP.

## Method (the DAgger round, D-030 regime)
The D-030 entry divert lifted EO recovery mode-independently but the CLEAN-trained policy lagged reactive
(2–8 vs 9–10) — it mishandles the hot 2-engine handoff. This round distills the EO handoff into the policy:
- **Teacher farm** (`data\s0eo2_mppi`, 12 seeds × 15, MPPI+D-030, ENTRY --engine-out random): MPPI-with-D-030
  EO landed 18/180 ≈ 10% — a REAL teacher (18 recoveries as demonstrations, vs the 1/60 that "would teach
  failure", D-025).
- **On-policy shadow farm** (`data\s0eo2_neural`, 12 seeds × 15, v6 policy flies EO+D-030, MPPI shadow-labels
  the states it visits — the covariate-shift fix; policy on-policy landed 7/180, the LABELS are the product).
- **Merged retrain** over 8 datasets (D-028's set with the EO regime swapped to D-030: s0, s0r1, s0g_mppi,
  s0g_neural, **s0eo2_mppi, s0eo2_neural**, s0e_neural, s0e2_neural): 9,009,749 rows / 1,800 runs, 543 s CUDA,
  lateral val-MSE 0.059/0.060 (comparable to v6's 0.078/0.064).

## Ceremony (byte-clean, held-out)
**NP_VERSION 7** (weights_sha256[:16]=**79ae728395cd60d7**). KAT re-pinned FROM THE C PASS (temp-printf
KAT-REGEN, never numpy): EXP0=-2.4109633539133393, EXP1=0.24477453088561507, EXP2=0.40000000229183696;
selftest PASS. Held-out law intact (train seeds 1000s/2000s/3000s/3200s/3300s/**3400s**; eval s42/s7/s99
never trained).

## [TBD] Gates + the EO recovery verdict
| ENTRY EO ×60 | s42 | s7 | s99 |
|---|---|---|---|
| **neural v7 (E2)** | — | — | — |
| neural v6 + D-030 (baseline) | 8/60 | 4/60 | 2/60 |
| reactive + D-030 (the ceiling to chase) | 9/60 | 10/60 | — |

Leak: TERMINAL 194/200 byte, MPPI run-1 2.63/10.48. No-regression floors: AERO clean ≥46, gust-A ≥45,
ENTRY clean ≥57 (must hold — the growing curriculum stays additive). Determinism pair on EO s42.

Verdict (TBD): if v7 EO beats v6+D-030 (8/4/2) — the DAgger taught the handoff (like ENTRY clean's ladder);
if it reaches reactive (9–10), the policy matched the hand law; a second round or a better teacher is the
lever beyond. No-regression must hold or the round is rejected.
