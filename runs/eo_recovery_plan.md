# EO RECOVERY PLAN — where the engine-out arc really goes (post-de-risk, 2026-07-20)

## The measured situation
- Physics: D-027 says ~59/60 of random EO draws are in-frontier (recoverable). E0b gilded (attitude holds).
- Reality: ALL current controllers collapse — reactive 0/1/1, MPPI 1/60, neural 1/0/0 (de-risk + E0).
- The `--neural` EO miss distribution is BIMODAL: near-miss (lat 20–85 m, landing-burn phase) + gross
  (lat 600–2800 m). Roughly 25% near / 75% gross.

## The structural attribution (from the code)
- Engine-out: side engine, t_fail ∈ [4,18] s, DURING the 3-engine entry burn (arm_engine_out, main.c:342).
- The `entry_supervisor` OWNS PH_ENTRY_BURN and writes gcmd.a_lat via `entry_divert_step` (ZEM/ZEV,
  KR=2.0/KV=3.5, bank cap 15°). Authority `amax = n_eng·thrust·sin(15°)/m` ⇒ an engine-out DROPS lateral
  authority to 2/3 while closing the same ~3 km offset. MPPI/neural do NOT steer until PH_AERO (post-cut).
- ⇒ HYPOTHESIS: the gross cluster is locked in during the entry-divert phase (2/3 authority can't close the
  harder draws); the composite (landing-burn warm-start) is structurally blind to it (reaches near-misses only).

## STEP A — the phase-attribution diagnostic (cheap, decisive; run when exe frees)
`--run <gross> --scenario entry --seed 42 --neural --engine-out random --verbose` for a KNOWN gross run
(de-risk: run 1 → lat 1801 m; run 3 → 2257 m; run 8 → 2757 m). The verbose line logs `lat` and `ph`
every 250 steps (main.c:419). Read `lat` at the ph: ENTRY_BURN→AERO transition (the cut):
- lat already ~1800 m at the cut  ⇒ ENTRY-DIVERT phase is the bottleneck (Step B).
- lat moderate at cut, grows in AERO/LANDING ⇒ the landing/aero phase (composite territory) is the bottleneck.

## STEP B — the entry-divert re-authorization lever (if cut-phase; the high-value fix)
The 2-engine entry divert is a hand-tuned reactive law, mode-independent (helps hoverslam/MPPI/neural alike),
NOT rollout-visible (Operator A does not apply — a different mechanism). D-027's oracle says the reactive
KR/KV divert CAN close to 12–15 m under 2-engine, so the gap is controller-realization, not physics. Levers,
cheapest first, each default-off / byte-clean (n_eng==3 path untouched ⇒ every golden reproduces):
1. **Bank cap for the reduced-authority burn**: raise the 15° cap when n_eng<3 (recover the lost sin-authority;
   entry burn qbar ~39 kPa vs STRUCT ~80 kPa ⇒ headroom to bank harder). One n_eng-gated constant.
2. **KR/KV for 2-engine**: less authority ⇒ start the closure EARLIER/harder (retune the ZEM/ZEV gains gated on
   n_eng<3). A small sweep (the D-012 pattern).
3. **Cut timing / t_go**: D-027 noted 2-engine cuts at higher reserve (hotter handoff v_cut 231 vs 114) — burn
   longer under 2-engine to close more (adjust ENTRY_QBAR_CUT / FUEL_FLOOR gated on n_eng<3).
Gate: leak byte-clean (n_eng==3 untouched), determinism pairs, then ENTRY --engine-out random ×60 ×3 scored
recovery-vs-frontier (ceiling_eo.c). Ship as its own ADR (D-030).

## STEP C — combine + distill
Once the entry divert closes the gross cluster and the composite (or a re-trained student) closes the near-miss
cluster, run the EO DAgger rounds (E2): teacher = the best-closing controller, distill → NP_VERSION 7, gates,
recovery-vs-frontier eval → pairwise (E3) → N3 compound showcase.

## Decision tree
- composite s42 > 1/60 (partial win expected) → D-029 = valid-but-partial operator; the gross cluster is the
  real bottleneck → Step A → Step B (D-030). Composite ×3 completion folds into the post-fix EO eval.
- composite s42 ≈ 1/60 → D-029 honest-null → Step A → Step B directly.
