# RUN_STATE

**Canon:** `CLAUDE_v2.md` (read §0–§2 first; adopted by **D-019**, 2026-07-19). `CLAUDE_v1.md`
and `CLAUDE_v0.md` are history — do not edit either.
**COLD-START:** point a fresh session at **`HANDOFF_2026-07-18_NIGHT.md`** — the self-contained
bootstrap (state + knob provenance + do-not-retry list + tool contracts + ranked roadmap). It
supersedes `HANDOFF_2026-07-18_EVENING.md`, `HANDOFF_2026-07-18.md` and `NEXT_SESSION.md`.
For N-track work read additionally: `runs/perception_to_policy_stack.md` (map) + the pillar
spec for your lane (`neural_policy_design.md` §H.0 before building anything).

**D-019 — CLAUDE_v2.md ADOPTED AS CANON (2026-07-19, operator-signed, all defaults): the
perception-to-policy integration.** Full v1→v2 supersede, anchor-stable (v1 section numbers
keep their meaning; v2 only ADDS §4.5-4.7 TARGET/ENGINE_OUT/Worlds, §8.4 perception contract,
§9.8 GM_NEURAL + §9.9 frontier metric, §10.9 protocol-v4 plan, §13.6 N-track gates, §14
N-track N0–N4, §18 stack, §19 training pipeline, §20 precompute-artifact registry, App-G the
frozen 28-feature VLM-ready socket; new directive 11 = precompute-in/telemetry-out). M4's
designated vehicle is N3 (the compound engine-out×gust×moving-target showcase + the ≥54/60
attempt; the honest 0.70·D_phys plateau alternative routes M4 to the plant-authority ADR).
Mesh+CFD doctrine: the UE-grade mesh and FluidX3D aero-table regeneration are ONE future ADR
event (§20). Deltas: DECISIONS **D-019**; authoring record runs/D019_proposed_canon_v2.md.
**★★★★ D-029 — E1: the composite operator is a NULL on engine-out (2026-07-20 night).** Built the
expert-iteration composite (student-warm-started MPPI, `--mppi-warm-neural`; byte-clean leak gate — the
GM_MPPI default path is bit-identical). ENTRY `--engine-out random` ×60 s42: **composite 1/60 == student
1/60 == teacher 1/60** — NOT an improvement operator (design §3: must not teach; kept default-off, D-018
pattern). Phase-attribution diagnostic (verbose ph/lat, `runs/e1_phase_attribution.txt`): the ~75% GROSS
cluster (td lat 600–2800 m) is lost at the ENTRY-BURN CUT — the 2-engine divert closes only ~830 m (vs
~2200 m for later failures) and carries **+22.9 m/s OUTBOUND** at the cut (under-driven + mistimed);
MPPI/neural only steer POST-cut ⇒ structurally blind. The ~25% NEAR cluster closes laterally but crashes
on terminal quality (td_v 9.58, off-pad). ⇒ **NEXT: E1.5/D-030 — re-authorize the 2-engine ENTRY DIVERT**
(`entry_divert_step`: open the low-qbar 15° bank cap + re-tune KR/KV/t_go under n_eng<3, byte-clean; D-027
frontier D_phys_2eng ≥12,656 m says the closure is available). Full record: DECISIONS **D-029**.

**★★★★★★ D-028 — ENTRY ROUND-3: THE STUDENT REACHES ITS TEACHER (2026-07-19 night): ENTRY clean
57/56/58 of 60 held-out (171/180 = 95.0%) — DEAD PARITY with MPPI on the identical s42 batch
(57==57), AHEAD on s7/s99 (93.3/96.7% vs 91/93%).** The ladder 0→52→57 in two DAgger rounds.
NP_VERSION 6 (8.98M rows / 8 datasets; sha b4734b48; KAT v6 pinned from the C pass). Determinism
pair identical (td_v 2.80, lat 8.17). No forgetting, 7th time: AERO clean 46, gust-A 45 bit-stable.
All gates green (selftest, TERMINAL byte, run-1 exact, held-out law). ENTRY clean is now a SOLVED
regime at teacher parity — the last competence prereq before engine-out recovery. **Next: the
expert-iteration EO teachers (cbc89fe; D-027 proved the whole random-EO draw is in-frontier =
claimable) → ENTRY engine-out DAgger → pairwise → N3 compound showcase.** Two process nulls logged
(PID-reuse monitor miss; Start-Job-not-detached) → trap rules: detached=Start-Process, watch the
artifact never a PID. Full record: DECISIONS **D-028**.

**★★★★★ D-027 — THE 2-ENGINE FRONTIER ORACLE VERDICT (2026-07-19 evening): MPPI's engine-out
collapse is a CONTROLLER SHORTFALL, not physics.** `runs/sandbox/ceiling_eo.c` (entrydiv.c's
dt-converged 62 km ENTRY machinery + ground-truthed EO physics: side engine, t_fail~U[4,18]s,
profile RE-INTEGRATED, trim debit swept 40/60/80%; self-tests ALL PASS 0.000%): **in-frontier
fraction ≈ 1.000** — lateral D_phys_2eng ≥ 12,656 m worst-case vs 3,000 m offsets; attitude
statically holdable (trim_frac 0.51 of the gimbal cone, ~11 s to LOC vs 3 s dwell); the shipped
reactive divert itself closes 3 km → 12–15 m under 2-engine authority. Clincher: MPPI's D-020
crash at 1,787 m == the burn-cut offset band (closure failure, not tumble); fuel non-binding.
⇒ EO teachers (cbc89fe) UNBLOCKED with the ENTIRE random-draw distribution as claimable
territory; showcase §G.2 framing corrected (honest adjacent failure must be MANUFACTURED —
6–8 km, center-out, or two-out). Queued gilding measurement: one 6-DOF `--engine-out 1@11
--mppi` replay (farm-free window). **ENTRY round-3 farm in flight** (seeds 3300–3311, v5
policy, deadline 21:45) toward NP_VERSION 6 / D-028. Full record: DECISIONS **D-027**,
runs/eo_frontier_report.md.

**★★★★★ D-026 — ENTRY ROUND-2 (2026-07-19 ~19:40): 0/60 → 52/60 IN ONE ON-POLICY ROUND —
IDENTICAL 52/60 ON ALL THREE HELD-OUT SEEDS (156/180 = 86.7%).** NP_VERSION 5 (8.00M rows /
1,800 runs, seven datasets; sha f12edc76…; KAT re-pinned from the C pass). The v4 near-miss
anatomy converted exactly as predicted: reach was learned, round-2 taught terminal finesse.
Student ENTRY landers TIGHTER than the teacher's (lat 7.45 vs 16.32, td_v 2.76, th 0);
5 landings behind MPPI's 57/60 after ONE round on this regime. No forgetting (6th time):
AERO clean 46 (teacher+2), gust-A 45 (teacher+7) bit-stable. Gates: selftest (KAT v5),
TERMINAL byte-clean, run-1 exact, ENTRY pair bit-identical, held-out law intact. Farm-
completion forensics on record: FARM-COMPLETE 12/12 @19:00:31; the session-API "service
busy" hiccup never touched the detached farm. **Next: ENTRY round-3 (→teacher parity) or
the D-025 blocking pair (2-engine frontier oracle + expert-iteration EO teachers) →
pairwise → N3.** Full record: DECISIONS **D-026**.

**★★★★ D-025 — ENGINE-OUT ROUNDS, the honest split verdict (2026-07-19 ~17:30): the wider
curriculum IMPROVED the old skills; ENTRY is round-0; and THE TEACHER ITSELF COLLAPSES under
random engine-out — the axis's real finding.** NP_VERSION 4 (7.03M rows, six datasets; sha
cfa22fbee…): AERO clean **46/60 (teacher+2, best ever)**, gust-A held 45/60 — zero forgetting.
ENTRY (first exposure, one EO round only): clean 0/60 BUT the near-miss anatomy (57 off-pad,
ZERO faults, run-1 reaches 19.6 m from a 62 km start) = the exact AERO round-0 pattern that
DAgger converted 1→4→45. **The bombshell comparator: ENTRY --engine-out random: MPPI 1/60
(from 57/60 clean!) — the classical stack loses 56 of 57 to a random in-burn failure despite
directive-7 rollouts seeing the dead engine.** So the student-vs-teacher engine-out question is
MOOT until two things exist: (1) the 2-ENGINE FRONTIER ORACLE (§A.4 ceiling.c variant — are
random draws even IN the reachable set? 1/60 may be near-physics), and (2) a better-than-MPPI
teacher for this axis = the expert-iteration operators (cbc89fe: verdict-filtered self-imitation
+ student-warm-started refinement — engine-out IS rollout-visible). ENTRY clean DAgger rounds
proceed meanwhile (teacher is excellent clean at 57/60). Gates all green: KAT v4 pinned,
TERMINAL byte-clean, pairs deterministic. Full record: DECISIONS **D-025**.

**★★★★★ D-024 — GUST ROUNDS (2026-07-19 ~15:00): the student is MORE SHEAR-ROBUST than its
teacher — the N2 "recovery ≥ MPPI on ≥1 axis" gate MET by distillation alone.** NP_VERSION 3
(5.15M-row full-curriculum retrain; sha 5fd2b9a7…): s42 ×60 clean 43 vs MPPI 44 (parity);
**gust 12@5000:800 → neural 45 vs MPPI 38 (+7); gust 20@3000:600 → neural 46 vs MPPI 42
(+4)** — the policy's rate is FLAT calm→20 m/s while the teacher degrades. Loop: gusty
teacher farm (15 seeds) + gusty on-policy shadow farm (12 seeds), `-GustFromSeed`
deterministic specs; KAT v3 pinned; TERMINAL byte-clean; pairs bit-identical; the −2 clean
cost recorded honestly. **Next: engine-out rounds (ENTRY farms + 2-engine frontier oracle)
→ pairwise → joint/N3.** Full record: DECISIONS **D-024**.

**★★★★★★ D-023 — S0 GREEN (2026-07-19 ~12:45): the LEARNED POLICY MATCHES-OR-BEATS ITS
TEACHER on every held-out seed, at ~9× speed.** `--neural` (Tier-A′ lateral-only,
NP_VERSION 2, sha d6249fece…) AERO ×60: **45/60 s42 · 47/60 s7 · 43/60 s99 = 135/180 vs
MPPI's 44/40/42 = 126/180 (+9)**; landers softer+tighter (td_v 2.49 vs 2.95, lat 9.12);
never trained on any gate seed. The path from D-022's 1/60: the DAgger shadow teacher
(GM_MPPI in shadow at the states the POLICY visits — proven pure: un-tapped flight
byte-identical), round-1 farm (420 policy flights, 1.5M on-policy rows) + merged 3.58M
retrain, then **Tier-A′** — hoverslam keeps the throttle, the net owns only a_lat (the
D-008 lateral-only lesson at the learned tier; zero retrain; full Tier-A stays an ADR
toggle). Honest intermediate recorded: full-Tier-A round-1 = 4/5/3. Gates: selftest PASS
(KAT v2 pinned from the C pass), pair bit-identical, TERMINAL byte-clean, run-1 exact,
shadow-purity battery. Scope per D-016 pattern: a gated CONFIG, GM_HOVERSLAM stays
default. **N1/S0 complete; the student-exceeds-teacher signal (+9/180 held-out) is §B.2's
frontier hypothesis showing EARLY — before any RL. Next: N2/S1 RL, engine-out/gust distill
rounds, or target Stage-1; N3 (the compound showcase + M4 attempt) now has its engine.**
Full record: DECISIONS **D-023**.

**D-022 — S0 ROUND-0 (2026-07-19 ~10:00): the distill pipeline PROVEN end-to-end; the
parity gate honestly NOT MET — and the FIRST LEARNED LANDING is on the books.** Farm 36
train seeds × 20 = 720 teacher runs / 2.08M rows / 570 MB → CUDA train 195 s (val MSE
.068/.079/.101) → export **NP_VERSION 1** (sha b4141469…) → KAT pinned bit-exact FROM THE C
PASS → selftest PASS → leak byte-clean (TERMINAL 194, run-1) → `--neural` AERO ×60 =
**1/60 s42 · 1/60 s7 · 0/60 s99** vs the ≥42/60 gate: the pre-registered round-0
covariate-shift shortfall (th 31 dominant + throttle-MSE plateau ⇒ the vertical channel is
the weak imitation). Pair bit-identical; **52.4 s wall for ×60 vs MPPI's ~540-900 s**; the
one s42 landing = the first learned descent ever (HARD 5.98 / 16.36, deterministic).
**Next lever: DAgger round-1** (label the states GM_NEURAL actually visits with MPPI
replans — needs the small tap-under-neural extension), NOT more BC epochs. Full record:
DECISIONS **D-022**.

**D-021 — N1-S0 SCAFFOLD INTEGRATED (2026-07-19 morning): the teacher tap + trainer +
GM_NEURAL skeleton, all default-off byte-clean.** `--policy-log` logs one 36-col f64 row per
GM_MPPI tick (App-G legal observation + the EXECUTED command; pure observation — sim
byte-identical tap-on; SHA-deterministic, cross-tree bit-identical). `trainer/`
(torch 2.9.1+cu128; held-out law ENFORCED IN CODE — gate seeds 42/7/99 hard-error) trains
the §B.4 MLP and exports the §F.1 fp64 header. `GM_NEURAL=3` + `--neural` fly the §F.2
fixed-order forward pass off `neural_policy_weights.h` (placeholder NP_VERSION 0; NP KAT in
selftest; honest finite crash pre-training, ×5 wall 3.7 s vs MPPI ~45-75 s). Re-gates:
selftest PASS · TERMINAL 194/200 byte · AERO --mppi 44/60 byte · run-1 2.63/10.48 · pairs
bit-identical. ws_probe bumped to v4. **S0 sequence next: farm (runs/s0_farm.ps1, seeds
1000+) → train → export (NP_VERSION 1 = ADR + KAT regen) → first `--neural` parity batch
(gate ≥42/60; round-0 shortfall expected+honest, DAgger is the fix).** Full record:
DECISIONS **D-021**.

**★★★★★ D-020 — N0 GREEN (2026-07-19, integrated this tree): the wide socket + protocol v4
+ engine-out/movable-target BUILT-DIALED-OFF; byte-equality held EVERYWHERE.** NavState
carries TargetEstimate+EngineHealth at nominal (truth pass-through on `nav_measure`'s
memcpy); hoverslam + MPPI (+CUDA threading) null `(r − target)` (directive-7 mirror at
every position read, velocity stays inertial); protocol v4 shipped as ONE unit (TLM 328 /
HELLO 80 with the Earth world-hash pin / EVT 48; TS mirrors + goldens re-frozen; vitest
14 files/163). Engine-out (survivor-centroid torque on the existing arm_thr lever,
multi-engine-burn guard, §4.3-legal eng_health, EVT FAULT) + seeded movable target
(closed-form `target_sample`, seeded/circle/line) both default-off ⇒ TERMINAL 194/200 ·
AERO t0 220/300 · AERO --mppi 44/60 · ENTRY --mppi 95/100 · MPPI run-1 2.63/10.48 ·
selftest ALL BYTE-IDENTICAL (measured in `_n0_wt` AND re-measured in this tree). CUDA
parity 5.0e-12 / rank 100% / bit-stable. Five on-state determinism pairs bit-identical;
chase proven (reactive lands at a 30 m parked target from both bearings; MPPI chases a
100 m divert to 99.75). `cmd_serve` now parses+arms gust/engine-out/target (the play menu
BINDS — the pre-existing serve drop fixed); cockpit `readTargetEst` retargeted to the
decoder's real camelization (`targetEstXy`). Known Stage-1 gap: verdict/td_lat score the
ORIGIN, not the armed target (target_sandbox_design §A.3 — armed runs that land ON the
target grade off-pad until Stage 1). Full record: DECISIONS **D-020**; artifacts
`_n0_wt/runs/n0_*` + `runs/n0main_*`. **N-track next: N1 (distill → GM_NEURAL).**

**★★★★ D-012 STATE-ADAPTIVE DIVERT GAIN (night session; the current tree + goldens): ENTRY
88/100 s42 (79 s7 / 78 s99 — every seed +2-3; op 5, th 5, fuel 2) · AERO tier-0 73.3% s42
(73.3 s7) · MPPI 44/60 = 73.3% new best (was 41; td_v mean 2.95) · TERMINAL 194/200 byte-stable
across ~20 builds · selftest PASS · deterministic · nav-noisy: ENTRY 74, AERO 72.3, TERMINAL
96.5.** The mechanism: a PROFILE-OVERSPEED brake on the fins-deployed POWERED-BURN divert gain
(seek 0.9 → brake 1.5 over 3 m/s of |v_xy| above the vdes profile; guidance_hoverslam.h KDIV_*,
mirrored into the MPPI rollout — directive 7). The v1-v4 structural factorial that found it (and
killed two plausible-but-wrong placements, incl. re-reading D-010's "1.2 over-drove tilt" as
UNPOWERED-phase damage) + the 11-config BRAKE×VBLEND grid + the saturated deck-null grid:
DECISIONS **D-012**, runs/d012_sweep{,2,3}.csv. **M6 (ENTRY ≥90) is 2 points away at s42 — and
88 is the MEASURED PLATEAU of the reactive structure** (D-012 addendum: the trim grid is
null-to-negative — grazes convert only by paying in th+fuel; the engine-cut rule is
relight-blocked at relights_left=2). **Next session: MPPI capacity (K 256→1024 CPU probe → M5
CUDA) — it is the path to BOTH M6 and M4.** Optional plant study: relights 2→3 ADR unblocks the
ENTRY fuel pair (+2 s42 / +5 s7 potential).

**D-014 WIND-ESTIMATOR FALSIFIED at Stage 0 (same night, post-D-012; real tree UNTOUCHED —
settling artifact `_wind2_wt/`):** the NAV-legal OBS-B estimator (runs/windthink_design.md, lane
windbuild) was built exactly per design and killed by measurement: byte-transparency PROVEN
(ENTRY 88/100 · AERO 220/300 · TERMINAL 194/200 · MPPI run-1 all byte-exact vs the D-012
baselines) but the estimate is anti-informative — mean |est−true| at the ignition freeze 21.7 m/s
ENTRY / 18.5 AERO (n=20 each; every run 7–36) vs the <2 m/s bar, because the vehicle NEVER
weathervanes: it holds mean true AoA 9–11° while diverting to null the offset (4.5 m/s error per
degree at |v_rel|~250). Stage 1 (aim_bias feedforward) correctly NOT built (the design's §6
garbage-pre-bias failure mode). DO NOT RETRY attitude-only wind estimation during offset-nulling
descents. Reinforces the plateau verdict above — wind rejection comes from MPPI replanning
(Roadmap A), not open-loop inference. Full record: runs/windbuild_report.md; ADR: DECISIONS D-014.

**★★★★★ D-015 M5 CUDA MPPI INTEGRATED (2026-07-19 small hours; the current tree): `--mppi-cuda`
runs the rollout sweep on the GPU (sm_89/CUDA 13.1, fp64 EVERYWHERE — the .cu unity-includes the
SAME plant sources, directive-7-clean), parity 1 ULP / top-64 rank 100% / run-twice BIT-IDENTICAL
at K=256 AND K=16384, and the 60-batch lands 44/60 LINE-FOR-LINE identical to CPU.** CI-safe
conditional CMake (toolkit-less runners build CPU-only; --mppi-cuda refuses exit-4). Latency
honestly missed the fp32-era 6 ms bar (fp64 p99 ≈46 ms @K256 → 299 ms @K16384) and was RESCOPED
by measurement to the real 100 ms replan budget → fp64 real-time-viable to ~K1024 — exactly where
vanilla saturates: **K=512 measured 44/60 FLAT vs K=256** (kprobe; K=1024 confirming), cheap
variant levers 42/60 interim (mppi-var). Deck-null (1.7,350) cross-validated and REJECTED by the
strict rule (MPPI reach −3 despite AERO t0 +12/600). Fleet record + verdicts: DECISIONS **D-015**,
runs/cuda_mppi_report.md, cuda_rebase_report.md, decknull_report.md, kprobe_sweep.csv. Next
decision pending: EMPI's ENTRY-under-MPPI number (the M6-via-MPPI hinge) + K=1024 + mvar finals.

**D-018 — M4 SAMPLER BRANCH CLOSED (2026-07-19): AERO ≥90 is a PLANT-AUTHORITY ceiling, not a
solver problem.** Four structural-MPPI variants ALL null from four angles — kprobe (K 256→1024
flat 44/44/42), mppi-var (OU-θ/λ-floor −2/−3), covo (anisotropic reach-axis Σ flat-to-harmful),
mpopi (iterate-and-recenter 15/20==baseline + blows the 100 ms budget). Unified diagnosis: off-pad
misses are the aero/thrust crossover dead-zone reach ceiling (~22 kPa); the controller realizes
~0.70·D_phys ≈ 775 m of the 1107 m physical divert ceiling — the off-pad seeds are physically
landable but past the tilt-capped controller's reach. **AERO stays 73.3%; M4 NOT green.** Redirect
(both ADR-grade, operator-steered): a PLANT-AUTHORITY lever (earlier/harder divert, or raise the
15° tilt cap vs the STRUCT envelope) OR the LEARNED NEURAL POLICY as frontier-extractor (now on the
M4 critical path — the perception-to-policy design fleet is speccing it). The guidance work has
reached the honest limit of the classical/sampling approach on the hard scenario. Full record:
DECISIONS **D-018**; reports covo/mpopi/kprobe/mvar.

**D-017 — DIAL-A-GUST shipped (2026-07-19): `--gust <peak>@<alt>[:<hw>]` + `--gust-dir` — a
deterministic 1-cosine wind-shear injector (plant input, guidance-blind per §4.3, off-by-default
byte-identical: TERMINAL 194 exact, AERO --mppi run-1 invariant).** The first BUILT play-menu
item — throw a shear and watch MPPI re-solve through a disturbance it cannot see (ENTRY reactive
92→72 under 12@5000; ENTRY-MPPI rides a 37 m/s crest, closes 116→18 m, bit-deterministic). The
other two play-menu items are specced-not-built: movable target (runs/target_sandbox_design.md)
and engine-out (runs/engineout_design.md). Also: D-016 nav-noisy honesty spot landed —
**ENTRY --mppi --nav-noisy = 90/100**, MPPI holds the M6 gate exactly at 90 through the estimation
layer (reactive was 74). Full record: DECISIONS **D-017**, runs/gust_report.md.

**★★★★★★ D-016 — M6 GREEN (2026-07-19): ENTRY under `--mppi` clears ≥90 on EVERY seed —
s42 95/100 · s7 91/100 · s99 93/100** (reactive baselines 88/79/78; +7/+12/+15). ZERO code
changes vs the shipped tree — only the flag; GM_MPPI's closed-loop replanning rejects the wind
disturbance implicitly (canon §4.3, never reads wind). Determinism: s42 confirmation batch ==
independent re-capture to the digit; golden `goldens/mc/entry_mppi_s42_d016_baseline.txt` frozen.
Directive-7: ENTRY run-14 CPU `--mppi` == CUDA `--mppi-cuda` bit-identical. The mechanism — run 14
(the RELIGHT study's unsavable fuel-trap seed) LANDS under MPPI (2346 kg left; arrives centered →
short burn → never enters the min-throttle climb trap). SCOPE (honest): GM_HOVERSLAM stays the
ENTRY DEFAULT; `--mppi` is the gate config, not a silent swap — promoting it needs its own ADR +
full Tier-B + perf budget. Capacity/variant verdicts folded in: KPROBE freeze-at-K≈1024 (rate
saturates by 512), MPPIVAR scalar levers null. **Sole remaining guidance gate: M4 (AERO ≥90),
which needs a STRUCTURAL MPPI variant (CoVO diagonal covariance / MPOPI iterate-and-recenter) —
the next build.** M7 renderer unlocked per directive 10 (frontend fleet already has documentary
view + S3 audio live on `--serve`). Full record: DECISIONS **D-016**.

**Current milestone:** M2 GREEN (TERMINAL ~97-98% across seeds) + **M3 socket path LIVE** + **M6
entry supervisor BUILT**. This session (D-007, ran 4-A + 4-C concurrently per operator "BOTH"):
(1) **E3 entry-burn supervisor** — predictive-peak-qbar 3-engine retrograde burn above hoverslam;
**ENTRY STRUCT_FAIL 100→0** (peak qbar 105→~39 kPa). (2) **Renderer `--serve` integrated** — ws.c
RFC6455 stream (HELLO/TLM@125Hz/EVT/STATS), goldens frozen; `pnpm -C ui dev` vs `--serve` shows a
live descent. (3) **Burn-phase aero steering fix** — smooth aero/thrust-crossover sign in control.c
(the AERO_OFFSET off-pad was a hard-flip tilt-reversal at the ~22 kPa crossover). TERMINAL unchanged
(97.0% s42 / 98.6% s7), determinism + 10 oracles green. **ENTRY & AERO_OFFSET still 0% land but now
AUTHORITY-LIMITED, not broken:** ENTRY is fuel-bound (entry burn decelerates then the vehicle
re-accelerates in thin air; 1-engine landing burn from 17 km runs dry — 92% fuel-out; 45t-prop
diagnostic → 29% fuel + 71% off-pad, so fuel AND lateral both bind), AERO_OFFSET is divert-cap-
limited (well-behaved, lands ~150 m out; can't close the last ~150 m at the 12° AoA tilt cap). Both
= genuine trajectory optimization → **MPPI (4-B) is the next lever** (its HIER inner loop reuses the
AoA-hold + this E3 supervisor). Also: repo pushed to public GitHub (MIT). See **D-007**.

**LATEST (autonomous push, D-007 addenda):** (a) **Aero-aware suicide-burn ignition** (thrust-only
forward-shoot, SRP-drag-shielded) → **ENTRY fuel-out 92%→0**. (b) **Divert recipe**: 15° tilt cap
(gated fins-deployed; TERMINAL stays 12°) + conservative decel profile `vdes=√(2·1.5·r)` + smooth
crossover steer → **FIRST AERO_OFFSET LANDING EVER** (1/300, min td_lat 12 m; median 328→145 m, 78/300
within 100 m). Tier-0 is now VARIANCE-limited (IC sensitivity the reactive law can't beat) — that is
MPPI's job. (c) **INJECT_DISTURBANCE (F4)** implemented (thrust/Isp/CoM, seeded/replayable) →
**TERMINAL passes Tier-B**: LANDED 97.3-98.1%, GOOD+ 88.9-89.4%, td_v p95 3.5/p99 ≤4.5 across 3 seeds.
(d) ENTRY (3 km offset) still off-pad (median ~2 km); banking the entry burn without a null-out
reversal is CATASTROPHIC (→17 km) = needs P5 §5.1 bang-bang = MPPI. **TWO Opus MPPI-HIER CPU builds
in flight** (tournament, intercom lanes mppi-build/mppi-build2); main session coaching them (warm-start
the mean with the hoverslam recipe → search corrections; cap crash cost; weight |v_xy| hard). Integrate
the winner's guidance_mppi.c when it clears the ≥90% gate. TERMINAL 97% / determinism / 10 oracles green.

**MPPI INTEGRATED (autonomous push, ~10am CST):** Both MPPI subagents' agents stalled/died mid-batch
(watchdog), but MPPI-2's 614-line `guidance_mppi.c` (all coaching implemented: lateral-only, ZEM
terminal, TD_VXY null-at-pad, bounded/clipped crash cost, adaptive λ, lean in-rollout vertical, feas
shoot) was PRESERVED. Main session **took over + integrated it into the real tree**: `guidance_mppi.{c,h}`
+ a GM_MPPI block in sim.c (E3 supervisor above MPPI; replan @10 Hz, cheap knot-emit between) + `MppiState`
in Sim + `--mppi` flag + OpenMP + the **flat-15° qcap** (raises the divert ceiling MPPI-2 found limiting).
Build clean, **selftest PASS (GM_HOVERSLAM byte-identical → determinism intact)**, single AERO run under
MPPI diverts run1(843 m)→**88 m** (beats tier-0's 158 m). Rate batch in flight. Run under MPPI:
`booster-core --headless --scenario aero_offset --mppi` (~9 s/run w/ OpenMP). Winner-file provenance:
`_mppi_wt2/core/guidance_mppi.c`. Known open lever if rate < 90: the warm-start A_DECEL=1.3/VLAT=30 is
conservative — raise for the ceiling-limited far seeds (MPPI's TD_VXY nulls the resulting overshoot).

**★★ BREAKTHROUGH (afternoon session, D-009 addenda 2-3): ENTRY 50% + AERO 60% LANDED, spec winds.**
The multi-session 0%-land wall on both hard scenarios was ONE plant bug: the §6.3 SRP shield was
applied to body aero only — the GRID FINS (45 m arm, deepest in the plume wake) passed the full
crosswind side-force through every landing burn, re-trimming the vehicle downwind so the 830 kN
thrust followed it (~140 m systematic miss no guidance could null; guidance commanded −84 m/s of
correction, vehicle realized −8%). Fixed (dynamics.c srp_shield on fin forces) plus: shield-aware
steer_sign, true-a_vert_ref landing-burn mapping, damp-through-ignition, ignition feather, ENTRY
ZEM/ZEV overdamped collision-course bank (3 km divert → med 23 m — design runs/d009_entry_divert_
design.md), MPPI gradient unlock (gamut clamp + ignition-anchored ZEM + fade→blend). **ENTRY
50/100 s42 (41/45% s7/s99; med miss 23 m, 99/100 within 50 m, STRUCT 0) · AERO tier-0 181/300=60.3%
(55% s7; med 19 m, 225/300 on-pad) · AERO --mppi 38/60=63.3% (warm-start tier-0 parity — MPPI now
LEADS) · TERMINAL 97.0% byte-stable · selftest PASS · determinism green (ENTRY+MPPI run-twice).**

**★★★ D-010 COMPOSED STATE (fleet sweep + split-gain + NAV layer; the current tree + goldens):
ENTRY 85/100 s42 (77 s7 / 76 s99; off-pad 5, fuel 3, STRUCT 0) · AERO tier-0 71.7% s42 (75.3 s7) ·
AERO --mppi 68.3% (softest ever: too-hard 2, td_v mean 2.97) · TERMINAL 194/200 byte-exact ·
selftest PASS · deterministic · NAV §8.1 layer LIVE (--nav-noisy: ENTRY 73, AERO 70.3, TERMINAL
96.5; NAV_TRUTH bit-transparent).** Knobs: Kvel(fins)=0.9 + height-split null→1.6 below 250 m
(guidance_hoverslam.c), KI_WIND 0.012 + output-fade trim gated GM_HOVERSLAM-only (sim.c), MPPI
rollout Kvd parity (guidance_mppi.c), nav.{h,c} routed per D-010 addendum. M6 gate (ENTRY ≥90%)
is 5 points away (misses graze 26-33 m); M4 (AERO ≥90%) needs state-adaptive divert or M5-CUDA
MPPI capacity. See DECISIONS D-010 (+addendum) and D-011 (+addendum) for the full record + the
graphics/audio/UE roadmap. Ceiling oracle
(runs/sandbox/ceiling.c): D_phys≈1107 m from 12 km → mean-500 σ150 IS well-posed. Remaining: the
too-hard tail (td_v 6-8 uncentered arrivals), the 26-33 m grazing band, MPPI re-batch, cross-seeds,
M4 ≥90% push. See D-009 (+3 addenda).

**POST-BATCH TRUTH + NEW PUSH (afternoon session, D-009):** the in-flight batch finished **0/60**
(59 off-pad, 1 too-hard) — D-008's "in flight" optimism is corrected in D-009. Failed + reverted:
A_DECEL 2.2/VLAT 48 (reaches 46-103 m, hard td_v 6-8); fade h/150 (worse, td_v 11-12). Root causes
diagnosed (D-009): (1) unmeasured divert ceiling (two estimates disagree 2×) — compiled-C oracle
study `runs/sandbox/ceiling.c` measuring now → scenario-retune ADR; (2) "centered by 400 m" never
implemented in the COST; (3) the execution fade kills velocity damping near the ground (D-003
repeated at the MPPI layer) → near-seeds hit dead-center but crash at td_v 6-8. NOW EXECUTING:
fade→blend (damping persists to contact), gate cost @400 m + altitude-ramped running |v_xy| weight,
warm-start parity with the tier-0 landing recipe (1.5/35). Gates per step: selftest PASS, TERMINAL
97.0% unchanged, MPPI run-twice bit-identical.

**TOOLING RULE (user, 2026-07-18):** C/C++/CUDA for everything, NEVER Python (crawls this
CPU). Core is all C — keep it. `tools/` (MC report, G-FOLD oracle, protocol codegen) must be
C/C++, not the Python the spec originally sketched. Use C:\orrery for heavy calc. Fleet
subagents' Python scratch in runs/sandbox is throwaway — do not depend on or re-run it.

## Status (2026-07-18)

- **M0 scaffold:** DONE. CMake + MSVC2022 build (`build/`, VS2022 generator), core builds
  clean, zero deps. (CUDA not yet wired — deferred to M4/M5.)
- **M1 plant + oracles:** DONE. `--selftest` PASS: US76 atmosphere, Philox RNG (host
  determinism + normal stats), quaternion/frame vectors, vacuum ballistic vs analytic,
  coast |q|=1, **analytic İ vs finite-diff**, hover impossibility (TWR_min=1.321),
  **bit-identical determinism memcmp**.
- **M2 hoverslam headless:** **99.8–99.9% LANDED** across seeds (1000/2000 runs).
  - seed 42, 1000 runs: 99.8% landed (270 PERFECT, 626 GOOD, 102 HARD, 2 crash), 0 off-pad,
    0 fuel-out. Landed mean td_v 2.09 m/s, lat 3.96 m, tilt 0.03°.
  - seed 7, 2000 runs: 99.9% landed (497 PERFECT, 1325 GOOD, 176 HARD, 2 crash).
  - GOOD+ ≈ 91%. (Gate wants ≥98% GOOD+ / p95≤3 — MPPI at M5 is expected to close the last
    HARD tail; tier-0 hoverslam has proven the plant is sound, which is its job.)
- **Headless `--out` fix (2026-07-18):** MC report writer (`main.c` cmd_headless) now checks
  `fopen`/`fclose` and fails loud — clear `stderr` error (path + errno) + nonzero exit 3 when
  the CSV can't be written, instead of the old false "wrote"; caller must pre-create the parent
  dir (not auto-created). CSV header/columns unchanged (goldens + tooling safe).

## What exists in core/

vmath.h, rng.h, constants.h, atmosphere.{h,c}, state.h, dynamics.{h,c} (forces/torques/
mass-props/analytic-İ/actuator-lags/SRP-shielding), integrator.{h,c} (RK4), contact.{h,c}
(leg spring-damper-crush + friction + substep), control.{h,c} (quaternion-PD + gimbal/RCS
allocation, altitude-scheduled tilt cap), guidance.h, guidance_hoverslam.{h,c} (unified
velocity-profile suicide burn, frozen a_design, cos-tilt throttle comp, first-order lateral),
scenario.{h,c}, sim.{h,c} (phase machine + verdict + termination), main.c
(--selftest | --headless | --run).

## Key tuning that made hoverslam land (see DECISIONS D-002)

Unified v_ref suicide-burn profile; a_design FROZEN at ignition from actual mass; throttle
compensated by 1/cos(tilt); first-order (non-overshooting) lateral velocity law, gentle
because the plant is sluggish; **overdamped** attitude loop (zeta=1.1) to avoid gimbal-
saturation oscillation; lateral steering fades out below ~40 m so the vehicle straightens
to vertical before touchdown (a tilted booster contacts one leg early at high profile speed).

**Next concrete action:** (1) **MPPI (M4 CPU → M5 CUDA, track 4-B)** — the real lever for ENTRY-land
+ AERO_OFFSET-land, both now authority/fuel-limited (D-007), not broken. HIER K=256 CPU first; each
rollout reuses the AoA-hold inner loop (control.c) and runs UNDER the E3 entry supervisor (sim.c).
Design: runs/agentB_mppi_design.md. (2) **Renderer visuals** now that `--serve` is live (M7):
procedural booster from HELLO → plume (ui/src/fx/plume.ts) → sky → HUD; run `pnpm -C ui dev` vs
`booster-core --serve`. (3) Optional pre-MPPI tuning: AERO_OFFSET divert authority (aero-descent AoA
aggressiveness + the terminal 12° tilt cap) to close the last ~150 m; ENTRY landing-burn efficiency
(harder/later suicide burn from the ~17 km handoff — currently a 57 s min-throttle fuel-waster).
(4) Consolidate D-007's local entry constants into constants.h; add an ENTRY entry-burn selftest
oracle. (5) INJECT_DISTURBANCE: **DONE** (D-007 addendum 2 — `--inject`, thrust/Isp/CoM seeded,
TERMINAL passes Tier-B); remaining Tier-B extension = NAV_NOISY sensor bias + 12 m/s gust.

**Known simplifications to revisit (all deliberate, tracked):** grid-fin aero stubbed;
slosh module present in state but excitation not yet wired; Dryden turbulence is a
first-order horizontal approx; wind mean-profile only; single WS/renderer not built yet;
no CUDA yet. None affect the M1/M2 proof.

**Blockers:** none.

---

## Renderer / protocol track (Agent D, 2026-07-18) — M3 GROUNDWORK GREEN

Renderer + process-boundary scaffold now exists and is verified. Aesthetics stay gated
behind headless (directive 10); this is the M3 socket/shell/ugly-scene groundwork plus the
protocol contract, all built to compile + test clean without touching the M1/M2 proof.

**Protocol (owned by Agent D per intercom hand-off from Agent B):**
- `core/protocol.h` — packet layout (canon §10.3, App-B). `#pragma pack(1)`, LE, explicit
  `_pad`, `_Static_assert` on sizeof + key offsets. **COMPILED + VERIFIED with MSVC
  `cl /std:c11`**: `sizeof(BlTlmFixed)==276`, all offset asserts hold. Magic tags
  (TLM=0x304D4C54 etc.), `BlPhase`/`BlVerdict`/`BlEvtCode` enums, `BlPlanKnot`(16B),
  `BlCloudSample`(12B), `BlEvt`(48B). Added `p_chamber` (plume needs p0/pa).
- TS decoder `ui/src/net/decode.ts` mirrors it byte-for-byte (will be regenerated by
  `tools/gen_protocol_ts.py`; hand version is the reference + golden target).
- STILL TODO on the C side: `ws.c` (RFC6455 subset, `--serve`), populate + emit TLM/EVT/
  HELLO/STATS, `p_chamber = throttle_act*Pc_ref` (pin `Pc_ref` ~9.7 MPa in constants.h),
  freeze `goldens/protocol/*.hex`. STATS@10Hz struct spec pending (Agent D owns; Agent B to
  match solver fields: solver p50/p99, ESS, lambda, cost best/mean + top-3 terms).

**UI scaffold (`ui/`, three@0.185.1 EXACT, Node 24.16.0 / pnpm 10.33.2):**
- `package.json`, `vite.config.ts`, `tsconfig.json` — full UI **typecheck-clean**.
- `src/net/frame.ts` — THE single sim→three conversion (canon §10.7). 22 vitest pass vs
  App-C vectors + commutation property (rotate-then-convert == convert-then-rotate <1e-6)
  over arbitrary quaternions. **M3 quaternion gate + Risk #1: GREEN.**
- `src/net/decode.ts` (+ `.test.ts`, 4 pass) — binary TLM decoder, every field at exact
  offset (sentinel-trick golden), magic rejection, empty tails.
- `src/net/interp.ts` — N-ring + full-run history (17MB replay); lerp r / slerp q / HOLD
  actuators; render 1 packet in past; `raw` toggle (directive 8); seq-gap drop counter.
- `src/net/client.ts` — direct WS to `core --serve` (no Rust relay, canon §10.1), magic
  routing, reconnect backoff.
- `src/scene/renderer.ts` — WebGPURenderer bootstrap: `reversedDepthBuffer:true`, AgX
  tonemap, `renderer.init()`, backend detect+log (WebGL2 auto-fallback, Risk #2).
- `src/scene/floatingOrigin.ts` — camera-relative rebase >2km (canon §11.1, Risk #10) for
  the 70km→1m continuous shot; fp64 origin in JS, small f32 to GPU.
- `src/scene/uglyScene.ts` — M3 capsule+plane acceptance scene.
- `src/main.ts` + `index.html` — wires it all; splash-gate for audio autoplay (Risk #13).
- `src/fx/plume.ts` — analytic raymarched plume TSL node (canon §11.6), **typecheck-clean**:
  `RaymarchingBox` proxy, Mach-disk `x1=0.67·De·√(p0/pa)`, altitude balloon, SRP blend by
  C_T, kerolox color, GG streak, TEA-TEB flash. Wires at M7.

**Shell (`shell/`, Tauri v2, Rust 1.96):** `tauri.conf.json` (sidecar externalBin
`binaries/booster-core`, CSP `connect-src ws://127.0.0.1:*`), `capabilities/default.json`
(`shell:allow-execute` sidecar-only), `Cargo.toml`, `src/main.rs` (spawn+supervise+restart
the sidecar, kill on close — never writes state), `build.rs`. Not yet `cargo`-built (needs
the core exe copied to `shell/binaries/booster-core-x86_64-pc-windows-msvc.exe`).

**Renderer next actions (in M3→M7 order):** (a) C-side `ws.c` + TLM emit so `pnpm dev`
shows the capsule tracking a live descent (closes M3 gate: 10-min stream, zero drops,
<1-frame jitter). (b) HELLO decoder + procedural booster from geometry (M7). (c) plume wire
+ bloom MRT + sky (takram `/webgpu`, peers three>=0.182 so 0.185.1 OK) + audio + HUD +
director (M7). (d) long-exposure, ASDS, weather (M8). Full build order in intercom FINAL.
