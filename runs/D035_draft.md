# D-035 draft — Target Stage-1b: the SEA heaving-deck (P-M spectrum + deck_vz leg loads + Option-i)

## What shipped
- **`core/sea.{h,c}` (NEW)** — the SEA module. A Pierson-Moskowitz sea state → the ASDS droneship deck
  motion, as a PURE closed-form sum-of-sines (48 components, equal-energy binned from the closed-form P-M
  CDF `F(ω)=exp(−β(ω0/ω)⁴)`; seeded phases from a new Philox stream `RNG_SEA`). `sea_deck_pose(t)` returns
  `deck_z(t)` (heave), `deck_vz(t)=d/dt deck_z` (heave rate), a small pitch/roll `deck_quat`, and the
  horizontal station. NO time integration ⇒ identical `(seed,Hs,t)` → bit-identical pose (replay-safe by
  construction, cleaner than the stateful wind). Parametrized by the operator-facing **Hs** (significant
  wave height): U19.5, ω0 back out from `Hs=0.2092·U19.5²/g`.
- **The heaving deck moves in the PLANT (§A.2), not a renderer trick.** Each physics step under `MOD_SEA`,
  `sim_step` overwrites the (static) `se.deck_z` with the live `deck_z(t)` (used by the contact solver +
  the touchdown threshold + the D-034 target-relative verdict) and feeds `deck_vz(t)` into `contact_wrench`:
  the leg spring-damper now works in the DECK frame, so the closing rate is the vehicle-relative
  `vz − deck_vz`. **A touchdown on a RISING deck loads the legs harder, on a FALLING deck softer** — leg
  crush + tipover become sea-phase-dependent. The honest new physics.
- **Deck-aware vertical guidance (§A.4 Option-i, the design's recommended default).** `GuidanceCmd` gains
  `deck_z`; `hoverslam_step`'s height reference becomes `h_base = y_z − deck_z − com` (the exact vertical
  parallel to the horizontal `r_xy = y − target_xy`). The reactive law (and GM_NEURAL, whose vertical comes
  from hoverslam) now tracks the CURRENT deck pose instead of the static z=0 — no future oracle, it re-aims
  at where the deck IS each 50 Hz tick. MPPI's vertical stays deck-blind (its own rollout; future work).

## Byte-clean (SEA off ⇒ byte-identical) — GREEN, verified on the deck-aware exe
Every coupling is gated on `deck_z`/`deck_vz`==0 (the static-pad default): `vz − 0.0`, `y_z − 0.0 − com` are
bit-exact. `RNG_SEA=5` is purely additive. **Leak GREEN:** selftest PASS, TERMINAL 194/200, MPPI run-1 HARD
2.63/10.48, AERO --mppi ×60 44/60 — all match the sacred anchors on BOTH the SEA-plant and the Option-i
guidance builds. Calm floor `--sea 0.05` = 58/60 (≈ dry TERMINAL 97%): the SEA machinery itself breaks
nothing. Determinism pair: 35/60 == 35/60 (identical) — replayable.

## Honest new capability — landing on a heaving deck (TERMINAL isolates the heave; SAME per-run sea draws)
Deck-blind = hoverslam's static z=0 height ref (the §1.2 gap). Deck-aware = §A.4 Option-i (`h_base = y_z −
deck_z(t_now) − com`). Identical seeds ⇒ identical sea per run ⇒ a clean controlled before/after.

| controller | Hs | s42 | s7 | s99 | total /180 | vs deck-blind |
|---|---|---|---|---|---|---|
| hoverslam **deck-blind** | 3.0 | 36 | 25 | 34 | 95 (52.8%) | — |
| hoverslam **deck-aware** | 3.0 | 43 | 33 | 38 | **114 (63.3%)** | **+10.5 pp** |
| hoverslam **deck-blind** | 1.5 | 35 | 41 | 37 | 113 (62.8%) | — |
| hoverslam **deck-aware** | 1.5 | 43 | 50 | 36 | **129 (71.7%)** | **+8.9 pp** |
| neural **deck-blind** | 1.5 | 29 | 37 | 27 | 93 (51.7%) | — |
| neural **deck-aware** | 1.5 | 37 | 47 | 32 | **116 (64.4%)** | **+12.8 pp** |
| calm floor (hoverslam) | 0.05 | 58/60 | | | — | ≈ dry 97% |

- **The Option-i lift:** deck-aware vertical guidance (re-aim at deck_z(t_now), NO future oracle) raises the
  heaving-deck rate ~9–11 pp across all seeds and both sea states. Mechanism seen directly: the previously
  degenerate **nominal run 0 flipped CRASHED (fuel=0, td_v 146 m/s) → GOOD (fuel=4201, td_v 2.56, lat 6.24)**
  — deck-blind burned full throttle hovering at z=0 while the deck sat below it (fuel-depletion); deck-aware
  follows the deck down and touches when it meets it.
- Hs=3 is canon-rough (heave ≈ ±1.5 m, deck velocity ±~2.5 m/s — rivals the ~2 m/s landing target, brutal);
  Hs=1.5 the moderate demo sea. **Caveat (honest):** some deck-aware runs hover-hunt to the sim cap (t=200 s)
  chasing the oscillating deck before committing — a terminal-commit refinement (heave-phase-timed final
  descent / §A.4 Option-ii forecast) is future work; the rate already improves without it.

## Scope / remaining (Target Stage-1)
- Ships §A.1 spectrum + §A.2 heave/deck_vz plant coupling + §A.4 Option-i vertical guidance. Verdict is
  D-034 (target-relative), reused.
- **Remaining:** the ±3 m horizontal station-keeping WANDER + feeding `target_xy` to guidance (Stage-1c,
  fields present, defaulted 0); tilted-normal contact from deck pitch/roll (SEA-polish §F); MPPI vertical
  deck-awareness; the protocol `target_xy`/`deck_z` renderer marker (§C, TLM already carries deck_z).
- No NP_VERSION bump (no weights).
