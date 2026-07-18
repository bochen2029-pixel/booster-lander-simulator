# D-010 — Kvel (fins-deployed lateral-damping gain) validation + refine

**Agent:** KVEL-VALIDATE (intercom id 9we13ckv) · **Date:** 2026-07-18 · **Worktree:** `_pb_wt` (isolated)
**Knob:** `core/guidance_hoverslam.c:124` `double Kvel = st->fins_deployed ? <X> : 0.6;` — only the fins-deployed value varied; the 0.6 (TERMINAL, fins-stowed) never touched.
**Baseline (D-009 fin-shield, Kvel=1.2):** ENTRY s42 50% · AERO s42 60.3% · TERMINAL 194/200 · selftest PASS.

All builds/runs confined to `_pb_wt` and its `v07/v08/v10` sub-copies. Turbulence ON (default) for every headless batch. Data logged to `runs/d010_kvel_refine.csv`.

---

## (A) VALIDATE Kvel = 0.9 (the sweep's C02 pick)

### Gates
| Gate | Result | Verdict |
|---|---|---|
| `--selftest` | SELFTEST: PASS | PASS |
| `--headless --scenario terminal --seed 42 --runs 200` | **194/200 = 97.0%** | EXACTLY 194/200 — PASS |

TERMINAL is fins-stowed, so it uses the untouched 0.6 gain — 194/200 reproduces identically at every Kvel (confirms isolation: the fins-deployed knob does not perturb TERMINAL).

### Measurement batches (Kvel = 0.9)
| Scenario | Seed | Runs | LANDED | Rate | off-pad | too-hard | fuel-out | other |
|---|---|---|---|---|---|---|---|---|
| ENTRY | 42 | 100 | 69 | **69.0%** | 5 | 23 | 1 | 2 |
| ENTRY | 7 | 100 | 76 | **76.0%** | 4 | 15 | 2 | 3 |
| ENTRY | 99 | 100 | 60 | **60.0%** | 7 | 28 | 2 | 3 |
| AERO_OFFSET | 42 | 300 | 215 | **71.7%** | 40 | 40 | 1 | 4 |
| AERO_OFFSET | 7 | 300 | 221 | **73.7%** | 38 | 38 | 1 | 2 |
| AERO_OFFSET (MPPI) | 42 | 60 | 39 | **65.0%** | 16 | 4 | 0 | 1 |

**Reproduction of the sweep's C02 finding is exact:** ENTRY s42 = 69.0% (sweep said 69), AERO s42 = 71.7% (sweep said 71.7), TERMINAL = 194/200 (exact), selftest PASS. Combined s42 score = 69 + 71.7 = **140.7** — matches the sweep's 140.7 to the decimal. Cross-seed on s7 and s99 confirms the lift is not a seed artifact (ENTRY 76/60, AERO 73.7).

**MPPI note (cross-reference):** under the MPPI controller the crash mix INVERTS — off-pad 16 dominates, too-hard collapses to 4 (vs the hoverslam's 40/40 on the same s42 AERO batch). MPPI's proper cross-range reversal kills the lateral-tail too-hard failures that the hoverslam's crude 2 s pre-emptive lead cannot, but it leaves off-pad as the binding limit. (Only 60 runs, so treat as directional.)

---

## (B) REFINE — Kvel ∈ {0.7, 0.8, 0.9, 1.0}, s42 (gates + ENTRY×100 + AERO×300)

Every variant: selftest PASS, TERMINAL s42 = 194/200 exact (fins-stowed, unaffected).

| Kvel | ENTRY s42 | ENTRY too-hard | ENTRY off-pad | AERO s42 | AERO too-hard | AERO off-pad | **Score (E+A)** |
|---|---|---|---|---|---|---|---|
| 0.7 | 64.0% | 30 | 3 | **87.3%** | 9 | 27 | **151.3** |
| 0.8 | 64.0% | 30 | 2 | 78.7% | 25 | 32 | 142.7 |
| 0.9 | **69.0%** | 23 | 5 | 71.7% | 40 | 40 | 140.7 |
| 1.0 | 46.0% | 17 | 30 | 70.0% | 47 | 38 | 116.0 |

### Cross-seed of the best combined score (0.7) on s7
| Scenario | Kvel=0.7 s7 | Kvel=0.9 s7 |
|---|---|---|
| ENTRY | 74.0% | 76.0% |
| AERO_OFFSET | 87.0% | 73.7% |
| **Score (E+A)** | **161.0** | 149.7 |

0.7 wins the combined ENTRY+AERO score on **both** seeds (s42: 151.3 vs 140.7; s7: 161.0 vs 149.7). The advantage is entirely AERO-driven (+15.6 pts on s42, +13.3 on s7) and is consistent across seeds.

---

## The trade-off: Kvel does two opposite jobs

The four-point sweep exposes a clean, **monotonic, opposing** pair of trends in ENTRY s42:

| Kvel | too-hard | off-pad |
|---|---|---|
| 1.0 | 17 | 30 |
| 0.9 | 23 | 5 |
| 0.8 | 30 | 2 |
| 0.7 | 30 | 3 |

- **Lower Kvel → fewer off-pad, more too-hard.** Weaker gain lets the far-field divert settle without over-steering past the pad (off-pad falls 30→3), but the SAME weaker gain under-damps residual cross-range velocity near the ground, so more runs contact with |vxy| in the "hard" tail (too-hard rises 17→30).
- The ENTRY optimum for the single-gain design sits at **0.9** (69%): 1.0 pays too much off-pad, ≤0.8 pays too much too-hard.
- AERO_OFFSET, in contrast, is off-pad-limited across the whole range and just wants the LOWEST Kvel (0.7 = 87.3%): its bigger initial offset makes over-steer/off-pad the dominant failure, so weaker gain helps monotonically.

**This is why the combined-score winner (0.7) and the ENTRY-only winner (0.9) disagree:** the two scenarios weight the off-pad↔too-hard trade oppositely, and a single scalar Kvel cannot sit at both optima at once.

---

## Cross-reference with TOOHARD (rpj3lzls)

TOOHARD instrumented vz/vxy at contact for both cohorts and reported:
- too-hard is **100% LATERAL**: |vz_contact| = 2.4 m/s in every too-hard run (vertical profile lands soft — base_frac=0.85 is fine); |vxy_contact| mean 6.4 (ENTRY)/7.1 (AERO), up to 10.2; 63/63 runs |vxy| > |vz|, so td_v ≈ vxy.
- Ignition state is IDENTICAL for too-hard vs soft runs (h_ign~2950 m, |vz_ign|~290, fuel_ign 7.6/10 t) → **not** an ignite-margin problem.
- Mechanism: the near-ground velocity-null term `Kvel*(-v_xy)` is 25% weaker at 0.9 than 1.2.

**My black-box too-hard-vs-Kvel trend independently confirms TOOHARD's white-box mechanism.** TOOHARD predicted the monotonic sequence "17→23→30"; my measured ENTRY too-hard is exactly 17 (1.0) → 23 (0.9) → 30 (0.8) → 30 (0.7). Two independent methods — contact-state instrumentation and a gain sweep — converge on the same cause: a single Kvel is forced to do both the far-field divert AND the near-ground velocity-null, and those two jobs want opposite gains. The MPPI result corroborates from a third angle: a controller that handles the lateral reversal properly drives too-hard to ~zero.

---

## Recommendation

**Two options, depending on scope for the main session:**

1. **Single-scalar, drop-in (matches the mission's exact-diff ask): keep the sweep's Kvel = 0.9.** Best ENTRY (69%), and the C02 config validated cleanly on all seeds/gates. If ENTRY is the priority scenario, 0.9 is the single-value optimum.
   - Combined-score note: **0.7 out-scores 0.9 on ENTRY+AERO on both seeds** (151.3/161 vs 140.7/149.7) — but the entire gain is AERO, at a −5 pt ENTRY cost. If the objective weights AERO ≥ ENTRY, switch the single value to **0.7**.

2. **Best overall (endorses TOOHARD's fix): split the gain.** Both my sweep and TOOHARD's instrumentation show one scalar cannot be optimal — the far-field divert wants ≤0.9 (fewer off-pad), the near-ground velocity-null wants ~1.2 (fewer too-hard). Splitting them (0.9 divert / 1.2 near-ground damping below ~150 m) should recover ENTRY's too-hard toward the 1.0-level (17) WITHOUT paying 1.0's off-pad penalty (30) — i.e. beat every single-scalar point here. TOOHARD is testing this; I recommend the main session adopt TOOHARD's split rather than any single scalar.

### Exact one-line diff for the main session

Single-scalar, drop-in (choose ONE value):

```diff
- double Kvel = st->fins_deployed ? 1.2 : 0.6;
+ double Kvel = st->fins_deployed ? 0.9 : 0.6;   // D-010: C02 validated (best ENTRY 69%); 0.7 if AERO-weighted
```

(Better, if adopting TOOHARD's split: keep the divert Kvel at 0.9 as above and add a separate near-ground damping gain of ~1.2 below ~150 m — see TOOHARD's patch.)
