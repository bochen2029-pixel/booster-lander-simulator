# TOTAL SITREP + CONTINUATION PROMPT — Booster Lander, 2026-07-19 ~18:20 (post-D-025)
### Written by the live session at 93% context. THIS FILE reconstructs the entire project to THIS
### moment for any forked/rewound/compacted instance. Self-contained. Trust files over memory; verify.

===============================================================================================
## LIVE LOG (running, newest first — update at EVERY state change; raw material for the next
## rewrite of this file. Operator standing instruction, 2026-07-19 18:35.)
===============================================================================================
- **2026-07-26 ~00:55 [opus] — PHASE 3 IS FULLY SCRIPTED AND THE DAgger MECHANISM IS BUILT + VERIFIED;
  PHASE 2b RUNNING IN PARALLEL.** (1) **`--shadow-rfly` = ORACLE DAgger** (`3a9fbbb`): a GM_NEURAL
  flight where the CEM re-solves θ **at the states the student visits** and the tap logs the ORACLE's
  command there (plant flies the student; only the label is the oracle's). Fixed a latent bug that
  would have made it silent nonsense — `rfly_eval_candidate` never set `c2.guidance_mode`, a no-op
  under GM_RFLY but under a GM_NEURAL shadow it would have flown every candidate with the STUDENT,
  scoring θ against a controller that never reads θ. Shadow keeps its OWN D-009 trim integral (the
  GM_NEURAL flight path never runs the trim, and BC/DAgger rounds must share one label convention);
  trim block DUPLICATED, not refactored — that code is byte-critical and shared with GM_HOVERSLAM.
  **Verified functionally, not just structurally** (a shadow silently logging the student's own
  command would pass every gate identically): θ on 2551/2551 rows, 5 re-solves per flight, and
  **42.3% of labels above the student's own ±3.2 clamp** ⇒ demonstrably the oracle's; `gbest` rises
  37.8→495.5 across the flight = covariate shift measured directly. (2) **Phase 2b** (clean AERO /
  gust / clean ENTRY, `data/s0rf_clean`) launched in parallel — required, because the old clean
  datasets are v1-width and the floors are otherwise unreachable. (3) **`rowformat` hardened** after
  testing the real merge path: the width guard did NOT actually guard (a v1 file with a row count
  divisible by 55 reshapes cleanly into sheared columns) — replaced with a POSITIVE structural check
  on the (t, seed, run) columns; and a live farm's mid-write file was being misdiagnosed as "a v1
  dataset" because its byte count happened to divide by 36. (4) **Scripted the rest of Phase 3:**
  `d041_export_build.ps1` (the single shared freeze ceremony — KAT pinned from the C pass) +
  `d041_dagger.ps1` (the loop; reports the landed rate AND the mean shadow gbest, the covariate-shift
  readout). **Farms at 00:55: compound 5 seeds banked 80/80 landed; clean 4 AERO seeds 63/64.**
- **2026-07-27 [opus] — R2b = NULL: the theta-hat CEM warm-start doesn't help; the real-time win is
  budget-reduction alone.** Built --rfly-warm-net (seed the CEM mean from theta-hat) + --rfly-budget
  (scale POP x ITERS), byte-clean default-off (run-0 PERFECT 0.38 exact). Compound s42 x12 sweep:
  cold GM_RFLY holds 12/12 SURVIVABLE from budget 1.0 down to 0.125 (1642 -> 62 s, 26x faster,
  ~0.4 s/replan = real-time) -- the reactive theta-basin is wide, the search is intrinsically cheap.
  Precision degrades though (10 PERFECT -> 1P/9G/2 HARD), a latency<->precision trade. theta-hat
  warm-starting is NEGATIVE at low budget (12->10->9): re-seeding every replan discards the search's
  accumulated theta. Arc conclusion: compound is search-necessary AND the search is cheap/robust; the
  NN is a broad-envelope gain-schedule (R2), not a compound controller (Phase 3) nor a search
  accelerator (R2b). Real-time GM_RFLY = reduced budget or KESTREL's GPU CEM. Tree clean, gates green.
- **2026-07-27 [opus] — R2 = PARTIAL WIN (D-042, TP_VERSION 1): theta-hat gain-schedule beats the
  analytic baseline held-out (AERO 131->162/180), the FIRST learned artifact of the arc to do so; but
  the engine-out compound stays 2/12 (search-necessary, triply confirmed).** Built the second net
  (guidance_theta.{c,h}, --rfly-theta-net, byte-clean default-off, C==Python to 17 figs, TP KAT in the
  selftest). Cheap ablations first proved constant-theta = identity = 2/12 on compound, and run-0's own
  converged theta held constant CRASHES its own draw (theta must vary by phase => replans load-bearing);
  I nearly mis-concluded from that (constant != theta-hat's schedule) and CORRECTED it in-ledger before
  building. theta-hat then flew: clean AERO 40->53 (s42), held-out 90.0% aggregate (M4-BORDERLINE, s99
  56/60), beating constant mean-theta on 2/3 held-out seeds -> real state-dependence. Compound 2/12 =
  the lookahead wall. Honest reframe: a 10us feedforward controller for the COMPOUND is infeasible;
  what ships is a 10us gain-schedule that lifts the broad envelope. NEXT (recommended): R2b theta-hat
  as a CEM WARM START -> real-time GM_RFLY without an H200. Showcase stays GM_RFLY. Tree clean, gates green.
- **2026-07-26 ~20:45 [fable] — PHASE 3 CLOSED AT THE PROBE'S HARD STOP; TREE RESTORED TO v6 AND
  RE-GATED.** All three pre-declared probe criteria failed: teacher fit not restored (compound rows
  WORSE at weight 0.3, 0.586→0.819), gbest decay REVERSED (21.5k→26.7k), rate 0/12 at every round
  NP8-NP12. The five-round arc's attribution, each mechanism with its own data: covariate shift
  (DAgger fixed it, −71%) · the authority gap (structural, predicted pre-data, the round-4 knee at
  ~2.5× the floor) · label conflict (the LODESTAR mode-averaging wall, resistant to down-weighting
  in BOTH directions). Full tables: D-041 addenda 9-11. Tree restored: NP6 KAT PASS, TERMINAL ×200
  byte-identical, neural AERO exactly 46/60 — nothing unpromoted remains. NEXT: **R2
  (θ̂-as-gain-schedule)** awaits the operator; the showcase flies GM_RFLY regardless.
- **2026-07-26 ~19:15 [fable] — R1 VERDICT LEDGERED (kill criterion fired as registered, 0/12 ×3 with
  gbest −71%); the BOUNDED PROBE is flying (rounds 4-5 global, NP11/12, dagger-weight 0.3, HARD STOP
  after round 5); operator freed disk (C: 21.8 GB — audit H3 CLOSED, v1 datasets stay).** Full data +
  the probe's pre-declared success criteria: D-041 addenda 9-10. Unpromoted NP artifacts still
  quarantined in the working tree (git checkout the two files restores v6).
- **2026-07-25 ~23:30 [opus] — PILOT-TRAINED ON PARTIAL FARM DATA; found a second label defect and
  DRY-RAN THE WHOLE PHASE-3 CEREMONY.** Rather than idle until the farm lands, trained on the first two
  banked seeds (31 runs / 179k rows, 9 s on the RTX). Two payoffs. **(1) The throttle don't-care mask
  (D-041 addendum 4):** the Tier-A throttle head was refusing to learn — val_mse 0.0919→0.0854 over 60
  epochs (7%) while a_lat0 improved 82%. The label is exactly 0 whenever the engine is off (54.6% of
  rows, perfectly correlated), but the policy's output range is [0.4,1.0], so **0 is unemittable** and
  those rows alone contribute an irreducible 0.0873 — i.e. essentially ALL the measured "learning" was
  the floor. They are also DON'T-CARES (engine_cmd==0 ⇒ the value is discarded), so half the gradient
  was chasing an unreachable target on rows the plant throws away. Masked to engine-on rows ⇒
  **val_mse 0.0854 → 0.00101**, moving 30× within the run. Same class as the output-range trap: *a
  label outside what the policy can express.* Both would have been invisible until the compound eval
  failed for unattributable reasons — **pilot-training on partial data is now standard practice.**
  **(2) The Phase-3 ceremony is PROVEN end to end** (in `build_kat/`, never touching the farm's exe):
  export → 39-in/Tier-2 header (1.8 MB) → build → **the KAT fails loudly** (a silent weights swap is
  impossible) → `--np-kat` → the pipeline's automated re-pin (placeholder branch correctly untouched)
  → rebuild → **SELFTEST: PASS**. Tree then restored via git; verified back at NP_VERSION 6 KAT-exact.
  ⇒ when the farm lands, `runs\d041_pipeline.ps1` is a known-good path, not a hopeful one.
- **2026-07-25 ~22:40 [opus] — THE OUTPUT-RANGE TRAP: 45% of the teacher's steering was outside the
  policy's ENTIRE output range (D-041 addendum 1).** Profiled the first oracle rows BEFORE spending the
  farm — worth doing every time. `A_LAT_GAMUT=3.2` is **MPPI's sampler bound, not a plant limit**; MPPI
  clamped its own output to it so every historical label was in range *by construction*. The reactive
  stack (hoverslam ⇒ GM_RFLY) does not clamp there: |a_lat| p50 **2.28**, p90 **19.3**, p99 **36.6**,
  max **43.9** m/s². **45.1% of labels are out of range**, concentrated exactly where the compound
  recovery lives (entry/aero p95 30.9, landing burn p95 28.3) while the terminal phase fits inside it
  (p95 3.56) — **which is v6's own pattern: 46/60 AERO clean, 0/30 compound.** Under the label clip
  those 45% collapse onto the rails (D-009's "railing kills the gradient", at scale). The design doc
  predicted this and sized it at ~5.5; it is 30+. NOT an assist (directive 6): `a_lat` is a DEMAND,
  `control.c` saturates it at the same physical tilt authority it already applies to hoverslam's
  identical demands — the plant limit is untouched, only the policy's ability to EXPRESS it changes.
  Fixed with `--a-lat-gamut auto` (p99.5 from the data, teacher-agnostic: an MPPI dataset auto-ranges
  back to ~3.2 on its own); nothing in C needed changing since `guidance_neural.c` already clamps to
  `NP_OUT_SCALE`. **Also scored the new self-sensed channel honestly:** `sf_z` separates engine-out in
  a SINGLE frame (49.43 vs 34.70, ~1σ) but `|wdot_y|` does NOT separate in aggregate (0.0653 vs
  0.0615) — the induced torque is a transient the inner loop trims within a few ticks. That is measured
  evidence for the deferred frame stack, parked against App-G's own decision rule.
- **2026-07-25 ~22:00 [opus] — D-041 SHIPPED (Phase 1) + the ORACLE TEACHER FARM IS FLYING.** The App-G v2
  re-architecture, done once and wide per the §14-N0 doctrine, and **APPEND-ONLY so it cost the shipped
  policy nothing**: socket 30→39 (achieved specific force + body angular accel + last executed command),
  Tier-A full action gated by a frozen-header `NP_ACTION_TIER` (absent ⇒ compiles out ⇒ NP6 bit-identical),
  and the GM_RFLY **oracle tap** — placed AFTER the D-009 wind-trim, the last thing that can still edit
  `a_lat`, because a row logged before it carries a label the plant never flew. Rows also carry the CEM's
  **θ as privileged teacher context** (own block, never in the obs slice) so the θ̂ prior can be trained
  later without a second farm. **The channel is not theoretical — measured on the first tap:** the
  engine-out at t=4.02 reads `sf_z` 52.37→36.67 m/s² + `wdot_y` +0.021→−0.135 rad/s² in the failure tick.
  Two honest measurements that changed the plan: the oracle costs **~150 s/solve** (not 60 — SEA-armed
  runs stretch to the cap), and **4 concurrent × OMP=4 = 159.6 s/run vs 150 s/run single-process** ⇒ the
  CEM saturates the box, process fan-out buys NOTHING, farm is sequential + deadline-paced. Also caught a
  latent buffer overrun (`neural_policy_step` sized its obs buffer `NP_N_IN` while the builder writes
  `NPOBS_N` — harmless while equal, an overrun the instant they weren't). Gates green at every edit.
  Farm: `data/s0rf`, seeds 5000-5011 ×16, deadline 07:30, monitor armed on the artifact stream (never a
  PID). **NEXT on farm completion: verdict-filtered train → export NP_VERSION 7 (`--action-tier 2`) → KAT
  from the C pass → gate battery → the compound eval vs hoverslam 2/12. KILL: ≤2/12 after 3 DAgger rounds
  ⇒ ledger the null and ship θ̂-prior + RFLY-in-the-loop.** NOTE for whoever resumes: **do not rebuild
  while the farm holds the exe** (LNK1104 stale-exe trap) — Python/ledger/FE work only until FARM-COMPLETE.
- **2026-07-25 [opus] — PHASE 0 (hygiene): the TERMINAL FREEZE was UNCOMMITTED — committed under the full
  battery; the ORACLE-DISTILL arc opened in ROADMAP.** Bootstrap found the load-bearing N3 fix
  (`rfly_async_poll`'s landing-burn freeze — the thing that turned the first live demo's LOC-at-13-m into three
  GOOD landings) living ONLY in the working tree + the linked binary; commit `cb8e093` shipped the ADR text
  describing it but not the code. One `git checkout` would have lost the demo. Gates re-run green before the
  commit: selftest PASS (NP6 KAT) · TERMINAL ×200 **byte-identical** to `runs/n0main_terminal.txt` · MPPI run-1
  AERO s42 **2.63/10.48** exact · RFLY compound run-0 reproduces **PERFECT td_v 1.72 / lat 0.38**. Also committed
  `s0_farm.ps1 -Target` (compound-regime farms — Phase 0a of the distill design). **The strategic read written
  into ROADMAP:** the compound is solved by a SEARCH (~60 s CEM/flight, measured) and canon wants a 10 µs
  POLICY — M4, N4 and a real-time cockpit are all gated behind that conversion. Opened the **ORACLE-DISTILL
  arc** (π the student + θ̂ the CEM prior = the AlphaZero loop the operator framed), Phases 0-5 with
  pre-registered risks and a kill criterion. NEXT: Phase 1, the App-G v2 re-architecture event.
- **2026-07-24 morning [fable] — v0.3.0 PACKAGED + RELEASED (+ the packaging trap found by eyes).** The
  operator's double-click of the freshly repackaged portable exe hit ERR_CONNECTION_REFUSED — the release
  webview navigated to the DEV URL (127.0.0.1:5183) instead of the embedded UI. Diagnosed ON SCREEN (new
  self-verify trick: Win32 SetForegroundWindow + CopyFromScreen of the app window rect → Read the png —
  the desktop app is now inspectable, not just the browser pane). FIX: strip `devUrl` from
  tauri.conf.json — with it absent the embedded path is unconditional (4f313cd). Verified: cockpit boots
  STREAMING in the packaged window, kill-on-close clean. Shipped **GitHub release v0.3.0** (portable zip
  7.3 MB + 3 hero shots): github.com/bochen2029-pixel/booster-lander-simulator/releases/tag/v0.3.0.
  Shell-integrated RFLY (mission-planning progress UI) remains on the N3-garnish list.
- **2026-07-23 FINAL [fable] — D-040 HELD-OUT CONFIRMED: 36/36 ACROSS THE 3-SEED BATTERY (29 PERFECT,
  7 GOOD, zero crashes, every draw sub-meter).** s42 12/12 · s7 12/12 · s99 12/12 on the canonical
  compound. Branch `cfly-port @ f3624c6` (D-040 ADR + held-out appendix + all artifacts) pushed;
  main `8e3dd92` (ROADMAP). **The compound axis is SWEPT on the honest tree — the N3 wow finally has
  its controller.** NEXT SESSION: the N3 showcase — merge cfly-port→main (byte-gates re-run post-
  merge), wire `--serve --rfly --interactive` (measure the live-replan pacing hiccup; interim smaller
  live POP, real fix = KESTREL GPU CEM), film the demo with the FE self-verify loop (photoreal scene
  shipped earlier tonight), including the §G.2 honest manufactured out-of-frontier failure.
- **2026-07-23 latest [fable] — D-040 PIVOT BUILT + FIRST BLOOD: GM_RFLY lands the canonical compound
  PERFECT.** Implemented the pivot the Phase-A null pointed to: 10-D multiplier θ over the NATIVE
  reactive stack's gains (GuidanceCmd.rt carriage — OpenMP-safe candidates, ×1.0 IEEE-exact ⇒ byte-
  clean; incl. the RT_TGTLEAD seek-lead = the D-038 redemption), the GM_HOVERSLAM pipeline verbatim +
  big-t=0/warm-10s CEM. Gates green (selftest · TERMINAL ×200 byte · hoverslam compound leak check
  byte-exact). **Run 0: t=0 gbest 324.7 (identity in-population at ~5583 ⇒ 17× found), replans grind
  324→62.8, RESULT: PERFECT — td_v 1.72, lat 0.38 m, tilt 0.03°, fuel 3296 kg** — vs hoverslam 35 m
  off-pad, cfly FUEL-0, neural 0%. EO@t=4 + 15 m/s gust + drifting deck, landed 38 cm from center
  with 3.3 t margin. The AlphaZero structure works on the main tree when aimed at the native law.
  12+12 rate check + det-pair in flight → `C:\Booster_Lander_CFLY\runs\rfly_rate_s42.txt`.
- **2026-07-23 late [fable] — CFLY PHASE A VERDICT: the direct sandbox-law port is an honest NULL
  (structural, not tuning); pivot designed.** Reverted the 4 unfaithful D-040 edits (maxq 58k, no
  min-throttle trap, no fuel cost term, full-authority coast steering) — byte-gates green — and the
  compound STILL goes 0/4 all-FUEL. The decisive probes: (1) the CLEAN entry starves too (td_v 14.2,
  lat 0.34, fuel 0 — it flies perfectly and runs dry), while hoverslam lands the identical cases with
  2.1-3.3 t margin; (2) the t=0 CEM solve never finds ANY landing candidate (gbest 11018 clean > the
  8000 never-landed floor; elitism means the sandbox's proven NOMINAL_TH itself was evaluated — it
  does not land on this plant); (3) the two plants fundamentally differ — 99-106 s sandbox descents
  vs 130-149 s main-tree (any law), ~40% more integrated drag. ⇒ The sandbox 16/16 certified the
  SEARCH ARCHITECTURE, not this law-on-this-plant; the 3-phase law (deep 3-eng burn → ballistic
  coast → 1-eng suicide) is fuel-infeasible here. **PIVOT (D-040 architecture, drafted in
  `C:\Booster_Lander_CFLY\runs\D040_draft.md`): keep optimizer-in-the-loop, wrap it around the
  NATIVE reactive stack's gains** (D-030 divert KR/KV/bank, seek/damp, ignition margins) — hoverslam
  is already fuel-feasible and its compound miss is LATERAL 35 m soft/upright = exactly what a
  per-scenario gain search closes. The cfly_replan CEM machinery (sampler, Sim-copy candidates,
  cost, byte-clean default-off) is proven and reusable as-is; only the θ→law binding changes.
  Full data: `runs/cfly_rate_s42_faithful.txt`, `runs/cfly_diag2.txt` (worktree).
- **2026-07-23 [fable] — FE PHOTOREAL-V2 SHIPPED: the operator's "graphics are a mess" pass, done fully
  agent-eyes-on.** The new harness browser pane + `?raf` headless-drive (visibility:hidden froze rAF — timer
  shim) made the in-app screenshot loop truly autonomous for the first time: ~12 iterate-verify rounds, zero
  human eyes. Ocean rebuilt as a TSL node material (procedural normal octaves + laced foam + dark physical
  albedo — the vertex-color patch blobs are gone); hull soot now TSL-procedural (bound ONCE via uniform — a
  graph REBIND silently drops the material to unlit white on this backend); real A-frame legs/lathe bells/
  open interstage/Ti fins; deck plating+rust+yellow lines+white bullseye; plume white-hot core + diamond
  pinch + the balloon-curve fix (PR keys on thousands, not tens — it was half-ballooned at SL); shadows
  enabled + sun-follow (shadow.camera.updateProjectionMatrix — the box was silently ±5 m); exposure
  re-balanced photographically (sun 1.9 / exposure 0.34 — the persistent "white vehicle" was sunlit paint at
  ~90% of sky luminance, proven by a raw-pixel probe, not a pipeline bug… though THOSE exist too: WebGPU
  async pipeline compiles render WHITE PLACEHOLDERS in one-shot RT captures — mitigated with RT-bound
  compileAsync + RT cache + double-render settle + `__shotProbe`). Gates green (tsc, vitest 174, build);
  `[INJECT] GUST` re-verified end-to-end. Committing FE + ledgers.
- **07:2x [opus, 8h autorun] — D-039 SHIPPED: Mode 2 LIVE target-drag (the operator drags the pad, guidance
  chases it).** Discovered a PARALLEL session is actively committing FE/interactive/portable-app to main (HEAD
  moved 11+ commits past my D-038: renderer ocean+deck+bloom+Earth-globe, the 3 disturbance buttons, portable
  app). Their ROADMAP line explicitly left "target-drag" as remaining — so I built exactly that. New wire cmd
  `BL_CMD_TARGET=4` (protocol.h append-only, BlCmd still 24 B), `Sim` `live_tgt/live_tgt_on` (sim.h),
  `apply_command` sets+journals it, `sim_step` OVERRIDES `gcmd.target_xy` when on ⇒ guidance nulls `r_xy=y−
  target_xy` against the dragged pad (D-034→D-037 machinery, no guidance edit). Byte-clean (serve-only/
  `live_tgt_on=0` ⇒ gates identical). VERIFIED live: pwsh ClientWebSocket injected `(40,25)` → `target_est_xy`
  flipped `(0,0) FIXED→(40,25) SEEDED` from t=0.14 s (`runs/d039_drag.txt`). Committing core backend
  (protocol.h/sim.h/sim.c/main.c) + ledgers; NOT touching shell/ui (theirs). main.c also carries their
  in-flight N_LANDING engine-out refinement (working, builds). **CONCURRENCY NOTE: two agents on one tree/branch
  — I own core/backend, they own FE/shell/ui. Coordinate; don't clobber their files.**
- **06:5x [opus, 8h autorun] — D-038 = NULL (reverted): the §F.6 target-velocity lead REGRESSES tracking.**
  Tried the design's one-liner for "guidance lags a fast target" (reactive null `−Kvd·v_xy` → `−Kvd·(v_xy−
  target_vxy)`, fed by closed-form deck/target velocity, `--no-vlead` A/B). Byte-clean (target_vxy=0 in gates ⇒
  identical). But hoverslam circle ×60 s42 WITH vs without: slow 55 vs 56, **fast 0 vs 12** — catastrophic. The
  seek+damp law fades `vdes` by `lat_scale` near the deck but not the added `target_vxy` ⇒ overshoot. A correct
  lead augments the SEEK (faded + re-tuned) = a real ADR, not a one-liner. Reverted all code to D-037 (selftest
  PASS, TERMINAL 194/200). Slow SEA wander tracks near-free without it (D-036), so the deployed law is fine for
  the canonical deck. Recorded + committed the null (ledger-only). **Autorun winding down (~06:55, ~1 h to 8am);
  tree pristine at D-037 code + full ledger through D-038.**
- **★★★ NIGHT-RUN SUMMARY (2026-07-20, ~00:00→06:55, opus 8 h autorun) — 10 ADRs (D-029→D-038; 4 honest NULLs),
  2 arcs concluded, tree byte-clean.** (1) **Engine-out recovery arc CONCLUDED (D-029→D-032):** D-030 lifted EO 1→8–10/60 (the
  2-engine entry-divert re-authorization, the shipped win); D-029/031/032 are honest NULLs — the composite
  operator and BOTH distillation teachers (MPPI + reactive) regress the neural EO ⇒ the ~59/60 frontier is
  RL-class (reserved). (2) **Target Stage-1 arc COMPLETE (D-034→D-037):** the full deterministic moving deck —
  1a target-relative verdict (D-034) + 1b heaving deck P-M spectrum + deck-relative leg loads + §A.4 Option-i
  deck-aware guidance (D-035, +9–13 pp) + 1c horizontal station wander (D-036, −2.2 pp, nearly free) + 1d the
  renderer wire (D-037, v4 socket already carries it, live-capture verified) — all byte-clean, replayable,
  streamed to the renderer. (3) **D-033** np_version provenance + nav-noisy honesty. **Every ADR: leak GREEN,
  goldens byte-identical, ledgers cascaded, committed.** HEAD f2e9110. NP_VERSION 6 unchanged (no weights
  touched). **Next levers:** the RL EO lane (multi-session, reserved) · the interactive ws command channel
  (Mode 2, the demo instrument) · SEA polish (tilted-normal contact, heave-phase terminal commit, fast-wander
  target_vxy lead) · N3 compound showcase (needs the RL EO lever).
- **06:3x [opus, 8h autorun] — D-037 SHIPPED: Target Stage-1 COMPLETE — the moving deck reaches the renderer
  wire (verification, ledger-only).** Discovered the last open Target item (§C renderer marker) needs NO code +
  no protocol bump: the v4 WIDE SOCKET already carries `target_est_xy@232` + `deck_z@304`, and `fill_tlm`
  populates them (`main.c:658/686`) from the nav socket the Stage-1b/1c SEA block writes. VERIFIED end-to-end
  by a live WebSocket TLM capture (pwsh ClientWebSocket, served `--sea 1.5 --sea-wander 3`): `target_est_xy=
  (−2.927,2.876)` within ±3 m wander, `deck_z=−0.193→−0.190` CHANGING (live heave), `src=TGT_SEEDED`
  (`runs\d037_wire_capture.txt`). ⇒ **Target Stage-1 COMPLETE (D-034 verdict + D-035 heave/Option-i + D-036
  wander + D-037 wire)** — a deterministic byte-clean replayable moving deck streamed over the unchanged v4
  protocol = the moving-target axis N3 needs. Committing D-037 (ledger + capture, no code). **NEXT: consolidate
  (refresh cold-start doc, final leak sweep) — ~1.3 h to 8am; the EO arc + the Target arc are both concluded.**
- **06:2x [opus, 8h autorun] — D-036 SHIPPED: Target Stage-1c, the SEA horizontal wander — the moving deck is
  DONE (byte-clean).** Added the deck's slow ±wander station-keeping: `sea_init` seeds 2 slow (~40–80 s) wander
  components from an amp arg; the `sim_step` SEA block feeds the deck `target_x/y`→`gcmd.target_xy` (like
  MOD_TARGET) ⇒ the existing `r_xy=y−target_xy` law tracks it, NO guidance edit. `--sea-wander [amp]` (default
  3.0 m). **Byte-clean PROVEN:** wander off ⇒ `target_xy=(0,0)` bit-exact; wander Philox draws use RNG_SEA
  lanes 1000/1001, heave phases untouched — the SEA heave-only aggregate EXACTLY reproduces the D-035 batch
  (43/33/38/43/37); leak GREEN (selftest, TERMINAL 194/200, MPPI 2.63/10.48). **Result:** ±3 m wander costs
  only −2.2 pp (hoverslam Hs1.5 heave-only 129 → heave+wander **125/180**=69.4%) — slow station-keeping nearly
  free (Option-i, horizontal); det-pair 42==42. Committed D-036 (`runs\d036_wander.txt`). **Moving deck DONE:
  heave+drift+deck-relative legs+target verdict, all byte-clean/replayable. NEXT: consolidate (well before
  8am) or Stage-1d protocol marker (§C, protocol bump) — ~1.5 h left.**
- **06:1x [opus, 8h autorun] — D-035 SHIPPED: Target Stage-1b, the SEA heaving-deck (byte-clean).** Built
  `core/sea.{h,c}` — P-M spectrum (48 comp, equal-energy via the closed-form CDF, seeded phases from a new
  `RNG_SEA=5` stream), `sea_deck_pose(t)` → deck_z/deck_vz/quat/station (pure sum-of-sines, replay-safe).
  Wired into the PLANT (§A.2): `sim_step` overwrites `se.deck_z` with the live heave + feeds `deck_vz` into
  `contact_wrench` (deck-relative closing rate `vz−deck_vz` ⇒ leg loads sea-phase-dependent). `--sea [Hs]`
  flag (3 CLI sites), `sim_arm_sea` per-run-seeded. **Leak GREEN on 3/4 fast anchors** (selftest PASS,
  TERMINAL 194/200, MPPI run-1 2.63/10.48; AERO --mppi ×60 running). SEA works: terminal run3 --sea lands
  GOOD td_v 2.35 (vs 1.89 dry — the leg-load coupling). Found + fixed the degenerate mode: deck-BLIND
  hoverslam (targets z=0, not deck_z(t) — the §1.2/§A.4 gap) hovers to fuel depletion on adverse sea phase.
  Added **§A.4 Option-i deck-aware guidance** (GuidanceCmd.deck_z, `h_base = y_z − deck_z − com`, byte-safe
  at 0; all hoverslam callers pass memset-zeroed gcmd ⇒ leak-safe). Built deck-aware, RE-LEAK GREEN (selftest
  PASS, TERMINAL 194/200, MPPI 2.63/10.48 byte-exact). **Option-i lifts the heaving-deck rate ~9–13 pp**
  (identical per-run sea draws = clean before/after): hoverslam Hs3 95→114/180, Hs1.5 113→129, neural Hs1.5
  93→116; nominal run 0 flipped CRASHED(fuel=0,146 m/s)→GOOD(fuel=4201,td_v 2.56). Calm-floor 58/60≈dry,
  det-pair 35==35, AERO --mppi 44/60. Caveat: some deck-aware runs hover-hunt to the t=200 cap (terminal-
  commit refinement = future). Ledgers cascaded (DECISIONS D-035, ROADMAP Stage-1b ✅, RUN_STATE, MEMORY);
  committing. Draft `runs\D035_draft.md`. **NEXT: Target Stage-1c (±3 m wander + target_xy→guidance) or the
  protocol renderer marker (§C) or consolidate — ~1.5 h left in the window.**
- **05:3x [opus, 8h autorun] — D-034 SHIPPED: Target Stage-1a, the target-relative verdict (byte-clean).**
  Pivoted off the concluded EO-distillation arc to the last showcase axis. Closed the D-020 known gap: the
  verdict + touchdown `impact_lat` scored the ORIGIN, so an armed-target run that landed ON the moving pad
  graded off-pad. Now (sim.c + sim.h) the target pose is LATCHED at first contact
  (`impact_target_xy = gcmd.target_xy`); `set_verdict` + the touchdown offset measure from it. FIXED = (0,0)
  ⇒ `sqrt(rx²+ry²)` ⇒ byte-identical. **Leak GREEN** (selftest PASS, TERMINAL 194/200, MPPI run-1
  2.63/10.48, AERO --mppi 44/60 all byte-exact). Proof: `--target line:30:10:0 --mppi` → `td_lat 21.58 m`
  target-relative (on-pad; was ~30 m off-pad); `--target circle:15:60 --neural` ×60 = 6/60 (HONEST moving-
  target rate — clean-trained policy tracks a moving pad weakly = N3 curriculum, not a verdict bug). No
  NP_VERSION bump (no weights). Committing D-034 (sim.c/sim.h + ledgers). **Remaining Target Stage-1: SEA-
  deck core/sea.{h,c}+deck_z(t) (§A.1/A.2); protocol target_xy in TLM (§C, v4→v5). NEXT: Stage-1b/1c or
  consolidate — ~2 h left in the window.**
- **05:2x [opus, 8h autorun] — D-033 SHIPPED: np_version provenance + --neural --nav-noisy honesty spot.**
  (Parallel-safe roadmap items, byte-clean.) np_version now plumbed into fill_hello/fill_tlm (the exact
  flown policy in HELLO@76/TLM@270 under GM_NEURAL; goldens emit under hoverslam ⇒ HELLO/TLM/EVT hex
  byte-identical, verified). Nav-noisy honesty (v6): AERO 46→44, ENTRY 57→55 (graceful −2, robust). Hit
  the LNK1104 trap once (built over the still-running nav-noisy core) — caught, rebuilt clean once free.
  Committed. **NEXT: pivot to Target Stage-1 (moving-target verdict, the last showcase axis) or consolidate.**
- **05:0x [opus, 8h autorun] — E2' = NULL (D-032); distillation EXHAUSTED for engine-out; reverted to v6.**
  Distilled the RIGHT teacher (reactive 9–10/60 via `--shadow-reactive`) — it ALSO regressed: NP_VERSION 7
  (reactive) EO = **2/5/4 = 11/180**, again < v6+D-030's 14/180 (s42 8→2), though no-regression IMPROVED
  (gust 46, ENTRY 58). BOTH teachers fail ⇒ adding fresh EO data regresses the neural EO (the shared policy
  can't absorb the EO a_lat without a clean-vs-EO compromise; DAgger covariate shift doesn't transfer the
  teacher's rate). Reverted header (git checkout) + main.c KAT to v6, KEEPING the `--shadow-reactive` flag;
  rebuilt → KAT v6 PASS, EO 8/60 restored, leak GREEN. Committing D-032 (the `--shadow-reactive` code +
  ledgers, NOT v7 weights). **EO recovery arc CONCLUDED for distillation: D-030 (1→8–10/60) is the shipped
  win; the ~59/60 frontier needs RL (reserved, multi-session). NEXT: pivot to a parallel-safe showcase axis
  (Target Stage-1) or consolidate.** v7 ckpts (s0eo2.pt, s0eo3.pt) + datasets preserved for RL.
- **04:3x [opus, 8h autorun] — E2' REACTIVE-SHADOW round IN FLIGHT (D-032): distill the RIGHT teacher.**
  Re-examining E2's numbers: it nulled because the MPPI teacher (~10%) is WORSE than the student on EO
  (v6 13%), so distilling it REGRESSED the policy. REACTIVE (hoverslam+D-030) is 9–10/60 = 15–17% — the
  BEST EO teacher. Built `--shadow-reactive` (sim.c: skip the MPPI overwrite in the GM_NEURAL DAgger
  shadow ⇒ log hoverslam's own a_lat; `g_shadow_reactive` default off ⇒ byte-clean; leak GREEN; verified
  the reactive tap DIFFERS from the MPPI tap = logging hoverslam labels). Reactive-shadow farm RUNNING
  (`data\s0eo3_neural`, 12 seeds, FAST no-MPPI ~20 min; monitor b3fq3krfc). **NEXT: retrain 7 datasets
  (s0 s0r1 s0g_mppi s0g_neural s0e_neural s0e2_neural s0eo3_neural — reactive-EO only, DROP the
  wrong-teacher s0eo2) → export NP_VERSION 7 → KAT (C pass) → `e2_gates.ps1` → D-032 (WIN if EO > v6
  8/4/2, toward reactive 9–10) or another null.** All prepped.
- **04:3x [opus, 8h autorun] — E2 = NULL (D-031); NP_VERSION 7 REJECTED + reverted to v6.** Full pipeline
  ran clean (both EO farms, 8-dataset retrain 9.01M rows, NP7 sha 79ae7283, KAT v7 pinned from the C pass,
  selftest PASS) — but NP7 EO = **6/0/5** of 60 = 11/180, WORSE than v6+D-030's 8/4/2 = 14/180 (gust-A
  45→44). The MPPI+D-030 teacher (~10%) is NOT better than the student on EO (v6 8/60) ⇒ distillation
  reshuffled without gain (D-025 confirmed directly). Reverted `core/neural_policy_weights.h` + `core/main.c`
  to v6 (git checkout; KAT v6 PASS, EO 8/60 restored; tree back to D-030 code state). v7 ckpt `runs\s0eo2.pt`
  + datasets `data\s0eo2_*` PRESERVED for a better-teacher round. **EO ceiling MAPPED: all controllers
  ~8-10/60 vs the ~59/60 frontier — the gap is RL-class.** Committing D-031 (ledger + artifacts, NOT v7
  weights). **NEXT: exceed the ceiling — reactive/hoverslam-shadow DAgger (marginal) or RL/learned-entry-
  divert (reserved); OR pivot to a parallel-safe showcase axis (Target Stage-1) for the remaining autorun.**
- **03:5x [opus, 8h autorun] — E2 IN FLIGHT: EO DAgger round (retrain running).** Both fresh EO farms
  DONE (with D-030): teacher `data\s0eo2_mppi` (12 seeds, MPPI+D-030 EO ~10% avg = a REAL teacher, 18
  recoveries) + shadow `data\s0eo2_neural` (12 seeds, MPPI shadow labels the v6-policy EO states).
  Merged retrain RUNNING (python pid 27580; 8 datasets: s0 s0r1 s0g_mppi s0g_neural s0eo2_mppi
  s0eo2_neural s0e_neural s0e2_neural; out `runs\s0eo2.pt`; monitor b0erh7zlo watches the artifact).
  **NEXT (all prepped): `export_weights --ckpt runs\s0eo2.pt --np-version 7` → KAT ceremony (temp-printf
  KAT-REGEN from the C pass, NEVER numpy; pin EXP0/1/2 + new sha in main.c test_neural_kat; strip printf;
  rebuild → selftest PASS) → `runs\e2_gates.ps1` detached (leak byte-anchors + no-regression floors
  AERO≥46/gust-A≥45/ENTRY-clean≥57 + EO ×60×3 recovery eval, TARGET: beat D-030 neural 8/4/2 toward the
  reactive 9–10 ceiling) → D-031 ledger + commit. Tasks #9–11 track it.**
- **02:xx [opus, 8h autorun] — D-030 SHIPPED: the 2-engine ENTRY-DIVERT re-authorization is the first
  EO recovery lever.** `entry_divert_step` under n_eng<3: bank 15°→35° + KR×4/KV×2.5 (frozen #defines,
  byte-clean; leak GREEN). Sweep-tuned on neural EO s42 (bank-alone 23° → 2/60 ⇒ GAIN-limited; peak
  35/4/2.5 → 8/60; over-aggression worse: 45/8/4→4, 35/5/2→0, 35/4/3→3). Cross-val (held-out): neural EO
  **8/4/2** of 60 (was 1/0/0), reactive **9/10** of 60 (was 0/1). **REACTIVE now BEATS NEURAL on EO
  (9–10 vs 2–8) — the clean-trained policy mishandles the hot 2-engine handoff ⇒ E2.** Partial fix
  (~8–10×). D-030 written + committing (DECISIONS + ROADMAP + RUN_STATE + MEMORY). MPPI-with-D-030 EO
  measurement in flight (E2 teacher assessment). **NEXT = E2: EO DAgger — teacher = best EO controller
  w/ D-030 (~9–10/60) + on-policy shadow → retrain → NP_VERSION 7 → gates → recovery-vs-frontier eval.**
- **01:0x [opus, 8h autorun] — E1 VERDICT = NULL (D-029); pivoting to the real lever (D-030).**
  Composite ENTRY `--engine-out random` ×60 s42 = **1/60 == student 1/60 == teacher 1/60** — NOT an
  improvement operator (design §3: must not teach; kept default-off). Phase-attribution (verbose ph/lat):
  the ~75% GROSS cluster is lost at the ENTRY-BURN CUT — the 2-engine divert closes only ~830 m (vs ~2200 m
  for later failures) and carries **+22.9 m/s OUTBOUND** at the cut (under-driven + mistimed); MPPI/neural
  only steer POST-cut ⇒ structurally blind. NEAR cluster closes laterally but crashes on td_v/off-pad.
  Farm stopped after s42 (null decisive), exe freed. D-029 written; committing composite code + ledgers +
  artifacts. **NEXT = E1.5/D-030: re-authorize the 2-engine ENTRY DIVERT** (`entry_divert_step`: open the
  low-qbar 15° bank cap + re-tune KR/KV/t_go under n_eng<3, byte-clean; D-027 says the closure is available).
- **23:4x [opus, 8h autorun] — E1 COMPOSITE BUILT + LEAK-CLEAN; de-risk anatomy is DECISIVE.**
  Built the expert-iteration composite operator: `warm_start_neural()` in guidance_mppi.c (mirrors
  warm_start_nominal; seeds ubar with the STUDENT policy's a_lat via neural_policy_step instead of
  the hoverslam recipe), armed by `--mppi-warm-neural` (MppiState.warm_neural; g_mppi_warm_neural in
  sim.c; default 0). Build clean (exit 0, relinked 23:45). **Leak gate green:** selftest PASS (KAT
  v6), TERMINAL 194/200 byte-exact, MPPI run-1 HARD 2.63/10.48 exact (AERO ×60 confirming).
  **DE-RISK ANATOMY (runs\e1_derisk.txt) — the important finding:** (1) plain REACTIVE (GM_HOVERSLAM)
  on `--engine-out random` ×60×3 = **0/1/1 of 60** — the reactive law ALSO collapses to ~1/60, so
  hoverslam/MPPI/neural ALL fail ⇒ no free teacher; the composite is genuinely needed. (2) The
  `--neural` EO miss distribution is **BIMODAL**: a near-miss cluster (lat ≈ 20–85 m, some soft —
  landing-burn-fixable, the composite's target) and a gross cluster (lat ≈ 600–2800 m — the 2-engine
  hotter-handoff closure failures D-027 named, likely beyond the landing-burn warm-start). ⇒ the
  composite's plausible reach is the near-miss fraction (~25%), NOT the whole ~59/60. **Validity farm
  next** (composite vs student vs teacher ENTRY EO ×60×3, recovery-vs-frontier) → D-029 verdict.
- **23:1x [opus] — PERPETUAL-PLAN SETUP + E0/E0b done.** (1) **ROADMAP.md** written+committed
  (engine-out arc E0→N3 + RESUME PROTOCOL). (2) **`.claude/settings.json`** SessionStart +
  PostCompact hook (gitignored) forces every fresh/compacted session to read ROADMAP + re-ingest
  memory — validated. Caveat: auto-arms FUTURE sessions; `/hooks` once to also arm THIS one.
  (3) **E0 baseline:** v6 `--engine-out random` ×60 = 1/0/0 (s42/s7/s99) = dead parity w/ MPPI
  1/60 — EO competence NOT free; ~59/60 claimable. (4) **E0b gilding** (`runs\eo_gild_1at11.txt`,
  report §8): 1@11 --mppi CRASHED lat 118.85 **tilt 0.02°** fault=none → attitude HELD, pure
  closure failure → D-027 gilded on all 4 axes. **NEXT = E1 (expert-iteration EO teacher build;
  guidance_mppi.c neural-warm-start; own ADR w/ validity tables BEFORE labeling). Start fresh-context.**
- **22:5x [opus] — D-028 SHIPPED: ENTRY clean TEACHER PARITY.** ENTRY clean ×60 held-out:
  s42 57/60, s7 56/60, s99 58/60 = 171/180 (95.0%). Dead parity vs MPPI on the identical s42
  batch (57==57); ahead on s7/s99. Ladder 0→52→57 in two rounds. NP_VERSION 6 (sha b4734b48).
  Determinism pair identical. No forgetting 7th (AERO 46, gust-A 45). Gates all green. ENTRY
  clean now SOLVED at parity. Ledgers written (DECISIONS D-028 + RUN_STATE + memory); committing
  + pushing. **NEXT: the expert-iteration EO teachers (task #3; cbc89fe) — D-027 gave them the
  whole random-EO distribution as in-frontier territory. Build needs guidance_mppi.c edits →
  do it in the now farm-free window (exe free). Then EO DAgger → pairwise → N3 showcase.**
  Two process-null traps this arc are §4-logged (PID-reuse; Start-Job≠detached).
- **22:46 [opus] — BUG (mine, §4 trap): Start-Job ≠ detached.** The 21:42 ENTRY eval used
  `Start-Job` — session-scoped; it DIED when the tool's pwsh returned (only the header line
  ever written; ~1h "running" clock was the dead job + machine sleep). RULE: detached work =
  `Start-Process` (real OS process), NEVER Start-Job. Re-launched the eval properly via
  Start-Process runs\d028_entry_eval.ps1 (22:45:52, seed 42 flying, monitor bdsl1q1h6 on the
  DONE-marker + process-death). NP_VERSION 6 + all other gates were already green; only this
  number outstanding. Orphan monitors from earlier PID-watches may false-fire — ignore.
- **21:42 [opus] — D-028 arc in progress:** retrain DONE (8.98M rows/1,980 runs, 659s CUDA,
  val-MSE lat 0.078/0.064). Exported **NP_VERSION 6** (sha b4734b4838c4d1b0). KAT ceremony
  DONE (regen from C pass: EXP0=-2.9905087230062684 EXP1=2.8676126619562528
  EXP2=0.40000000368630478; pinned in main.c; selftest PASS). Leak gates GREEN: TERMINAL
  194/200 byte-exact, MPPI run-1 HARD 2.63/10.48 exact. No-regression (7th time): AERO clean
  46/60, gust-A 45/60 — both bit-match v5. **ENTRY clean ×60 ×3-seed eval RUNNING** (bg job,
  results → runs\d028_entry_eval.txt, monitor br99cw6b3 watches the ENTRY-EVAL-DONE marker).
  On its numbers → D-028 ledger + push. If it matches the 12-15/15 training-seed rates,
  teacher-parity (57) is in reach.
- **21:26 [opus, model switched] — MONITOR TRAP (§4, costly): the round-3 farm COMPLETED
  20:58:11 (12/12, FARM-COMPLETE, all bins intact, land rates 12-15/15!) but the PID-watch
  monitor NEVER FIRED → ~26 min retrain time lost. Root cause: Windows PID REUSE — watching
  `tasklist PID eq 16064` for "No tasks" fails when the OS reassigns 16064 to another process.
  RULE: never watch a PID for death; watch the ARTIFACT (FARM-COMPLETE marker / output file)
  or the process-by-cmdline. Farm data 100% intact, only wall-clock lost. **D-028 eight-dir
  retrain LAUNCHED 21:26** (adds data\s0e2_neural; out runs\s0e2.pt; log d028_train.log; PID
  28504; monitor bnfmu46lp watches the ARTIFACT+process, not a PID). Guard was clean (no
  trainer, header v5, no s0e2.pt). Round-3 farm land rates on training seeds: 14/12/15/14/13
  of 15 — teacher-parity (57) plausibly in reach at the held-out eval.
- **20:1x [fable] — D-027 SHIPPED: the frontier oracle VERDICT = CONTROLLER SHORTFALL.**
  In-frontier fraction ≈ 1.000 (lateral ≥12,656 m vs 3,000 m offsets; attitude trim_frac 0.51,
  11 s to LOC; the deployed law closes to 12–15 m on 2 engines). Self-tests ALL PASS 0.000%;
  D-020's 1,787 m crash == the burn-cut offset band (closure failure). EO teachers (task #3)
  UNBLOCKED with the WHOLE random-draw distribution as territory; §G.2 demo framing corrected
  (adjacent honest failure must be manufactured). Ledger: DECISIONS D-027 + RUN_STATE + memory
  index; committing oracle artifacts + ledgers now. Queued: the 6-DOF 1@11 gilding replay
  (farm-free window). Round-3 farm unaffected, still flying.
- **19:51 [fable] — TRAP for §4: Start-Process PID ≠ driver PID.** The round-3 monitor
  false-fired in <1 min: PID 9840 was a short-lived LAUNCHER pwsh; the real `-File s0_farm`
  driver is **PID 16064** (farm verified alive: seed 3300 flying, tap growing). Rule: after
  Start-Process, re-identify the persistent driver by role/cmdline after a settle delay and
  arm the watch on THAT (or on the artifact stream, e.g. farm.log progression). Monitor
  re-armed: br2h6vy24 on 16064.
- **19:50 [fable] — TWO LANES LIVE (operator "proceed"):** (1) **ENTRY round-3 shadow farm
  FIRED** — `s0_farm.ps1 -Scenario entry -Mode neural -SeedBase 3300 -Seeds 12 -RunsPer 15
  -DeadlineLocal 21:45 -OutDir data\s0e2_neural`, PID 9840, FARM-START 19:50:16, v5 policy
  flying (52/60 competence), monitor bddszrbhb (explicit-death watch). On end: eight-dir
  retrain → NP_VERSION 6 → KAT → gates (+ ENTRY-clean ≥52 no-regression floor joins the
  battery) → ENTRY ×60 ×3 seeds → D-028. (2) **2-engine frontier oracle agent** mid-flight
  (~8 min in): ceiling_eo.c (644 lines) compiled clean, self-tests PASS, dt-converged;
  PRELIMINARY (unconfirmed until its report): lateral in-frontier fraction ≈ 1.000 at
  D_min/mean/max — leaning CONTROLLER SHORTFALL, pending the attitude/LOC + terminal-null
  axes it is computing. Its report → runs/eo_frontier_report.md → D-027 on integration.
- **20:0x [fable, lane returned]** — operator moved the lane back (opus at 96%, compaction
  imminent). Takeover RE-VERIFIED against disk: HEAD `27a70dd`, tracked clean, selftest PASS
  (KAT **NP_VERSION=5**, NP_N_IN=30), ZERO live pipeline processes (first probe self-matched
  its own query — the §4 grep-trap again; excluded ⇒ clean). My 15-min sentinel independently
  returned `header=v5 D-026-PRESENT PIPELINE-IDLE` (healthy, no stall). Session tasks synced
  (#1 D-026 → completed). NEXT per §5: **launching the 2-ENGINE FRONTIER ORACLE build now**
  (delegated to a background agent — self-contained sandbox precompute, no sim-tree writes);
  ENTRY round-3 held pending the oracle/operator word.
- **19:50 [wrap] — D-026 SHIPPED (27a70dd). ENTRY 0→52/60 in ONE round; 52/52/52 across
  s42/s7/s99 (156/180=86.7%), pair bit-identical; landers tighter than the teacher (lat 7.45
  vs 16.32, th 0); AERO clean 46 + gust-A 45 held (no forgetting, 6th time). NP_VERSION 5
  (sha f12edc76…), KAT v5 pinned, all gates green. HEAD=27a70dd; tracked tree clean.**
  §3's D-026 sequence is COMPLETE — next resume point is §5: (A) optional ENTRY round-3
  (52→57 parity), (B) the 2-engine frontier oracle [BLOCKING for any EO claim], (C) the
  expert-iteration EO teachers (cbc89fe), then pairwise → N3. §6 table updates: ENTRY clean
  --neural = 52/52/52 (v5). No live farms, no live monitors, no pending processes.
- **19:12 [opus, now the sole driver]** — Fable takeover session hit API "service busy / model
  overloaded" + low context; operator handed me the lane. DIAGNOSED the farm: **COMPLETED cleanly**
  — farm.log `19:00:31 FARM-COMPLETE seeds_done=12`, all 12 bins (s3200-3211, ~22MB, contiguous),
  driver PID4856 exited post-marker. NOT an early crash; the API error was session-only, farm is
  detached OS process. Anti-double-drive guard CLEAR (no live trainer, no runs\s0e.pt, header still
  NP_VERSION 4). **D-026 SEVEN-DIR RETRAIN LAUNCHED** (detached, log runs\d026_train.log, monitor
  bsuezkoks). On done: export --np-version 5 → KAT ceremony → gates → ENTRY-clean ×60 eval → D-026
  ledger+commit+push+wrap. (Prior instance's monitor bt2f9zce6 is a stale orphan — ignore.)
- **18:40 [fable takeover session]** — running-log habit adopted (this section). Note: the ~18:20
  wholesale rewrite of this file (by the 93% instance) and my first log-insert collided —
  resolved by layering this section on top; everything below §0 is the 18:20 sitrep, intact.
- **18:35** — DOUBLE-MONITOR STATE: the prior instance's farm monitor **bt2f9zce6** AND the
  takeover session's **brz68863c** are BOTH armed on farm PID 4856. ANTI-DOUBLE-DRIVE GUARD
  (standing until the prior instance is parked): before ANY retrain act, check (a) live
  `python.exe` running train_s0/export_weights — match the PROCESS IMAGE, not command-line grep
  (monitor echo text false-positives), and (b) existing NP_VERSION-5 artifacts (runs\s0e.pt /
  weights header sha change). Second-arriver no-ops.
- **18:27** — farm health: 5/12 banked (3200–3204, ~22–24 MB each; 0/15 landed EXPECTED at ENTRY
  round-0 — labels are the product), 3205 in flight, ~4–5.6 min/seed → ETA ~19:00–19:15.
  Discipline: NO heavy compute until farm end (a TERMINAL gate batch slowed seed 3204 3.9→5.6 min).
- **18:26** — monitor trap (add to §4 lore): first takeover monitor FALSE-FIRED — a pwsh launch
  failure inside the git-bash monitor env read as "process dead." Probes must require POSITIVE
  death confirmation (tasklist "No tasks" match), never infer death from probe failure.
- **18:25** — TAKEOVER verified per §1: HEAD `ed7160c`, tracked clean, selftest PASS (KAT
  NP_VERSION=4, NP_N_IN=30), TERMINAL ×200 re-measured BYTE-EXACT. This file + D-025 ADR read in
  full. Session tasks #1–3 mirror §3's D-026 sequence, §5.B (frontier oracle), §5.C (EO teachers).

===============================================================================================
## 0. WHO/WHERE/HOW
===============================================================================================
- Project: **Booster Lander Simulator**, `C:\Booster_Lander_Simulator`. A 6-DOF Falcon-9-class
  propulsive-LANDING (descent-only, no ascent) sim: deterministic C/CUDA plant + real-time
  guidance that actually solves the descent, proven headless via Monte-Carlo, streamed one-way to
  a pure-observer WebGPU/three.js cockpit in a Tauri v2 shell. Public mirror:
  github.com/bochen2029-pixel/booster-lander-simulator (push per completed unit).
- Operator: **Bo Chen**. Standing autonomy granted: proceed at best recommendation; honest numbers
  (record nulls with data, D-013/14/18 tradition); ledger discipline; ask only on true forks or
  hard blockers. Momentum-driven. Values byte-exact determinism above all.
- Platform: Windows 11, **pwsh** (NEVER `powershell` 5.1 — ANSI codepage kills em-dashes). RTX
  4070 Ti SUPER (sm_89), CUDA 13.1, **torch 2.9.1+cu128 / Python 3.13** (the trainer's GPU).
  MSVC 2022, CMake. RunPod H200s available but NOT needed through the distill arcs.
- Tools available: imguard (screenshots), chunker (big files), everywhere/everything (search),
  C:\intercom (subagent swarm), KEEL/Qwen3.5-9B local VLM fallback. Up to 10 Opus subagents OK.

===============================================================================================
## 1. REHYDRATE (do in ORDER, before anything)
===============================================================================================
1. Read this file whole. Then `CLAUDE_v2.md` §0–§2 (CANON, adopted D-019 today) →
   `RUN_STATE.md` top entries (D-025…D-019) → `DECISIONS.md` D-019…D-025 tail.
2. **Verify disk beats memory:**
   - `git log --oneline -12` → HEAD **ed7160c** (D-025) ← cbc89fe (expert-iteration design) ←
     0288403 (D-024) ← acb75b4 (README) ← 8bb5f6a (D-023) ← 33df55d (D-022) ← 350f24e (D-021) ←
     1fe50ae (D-020) ← dd98a11 ← db9e3a5 ← 8d599de (D-019).
   - `.\build\bin\Release\booster-core.exe --selftest` → `SELFTEST: PASS` (incl. NP KAT
     **NP_VERSION=4**).
   - TERMINAL ×200 == `runs\n0main_terminal.txt` byte-exact (194/200) — the standing leak gate.
   - `git status --short` → only untracked scratch (data\, trainer\, _n0_wt\, _n1_wt\, dist\,
     serve*.pid, runs scratch/*.png, *.chunks, transcript export, THIS file). Tracked tree clean.
3. The session `.jsonl` transcript is the grep-able backstop; never re-read whole.

===============================================================================================
## 2. THE FULL ARC (every commit today; verify via git, don't re-derive)
===============================================================================================
The project was M0–M6 done before today (TERMINAL ~97%, ENTRY M6 GREEN via MPPI 95/91/93, AERO
73.3% with the D-018 four-angle proof that AERO≥90 is a PLANT-AUTHORITY ceiling — controller
realizes ~0.70·D_phys≈775m of the 1107m divert limit — which redirected M4 to a learned policy).
Canon was CLAUDE_v1.md. Today's session:

| commit | ADR | essence |
|---|---|---|
| 8d599de | **D-019** | CLAUDE_v2.md adopted CANON (operator all-defaults). Anchor-stable v1 supersede: directive 11 (precompute-in/telemetry-out); §4.5 TARGET/§4.6 ENGINE_OUT modules; §8.1+App-G VLM-ready wide socket; §8.4 perception contract; §9.8 GM_NEURAL; §9.9 frontier metric (OVERLAY-ONLY — landed-rate gates never soften); §13.6 gate battery + HELD-OUT LAW; §14 N-track N0–N4; §19 training pipeline; §20 artifact registry (UE mesh + FluidX3D = ONE future ADR). |
| db9e3a5 | — | Frontend v2: cockpit play menu (gust/engine-out/target inputs), targetMarker.ts, HUD ENG row, fixed a flat-vs-nested get_core_status crash that had killed the Tauri shell chrome. |
| dd98a11 | — | Build pipeline fixed + the packaged-app **CSP kill** (Tauri nonce voids 'unsafe-inline' in style-src → injected <style> blocked in the packaged webview only). Operator confirmed cockpit live. |
| 1fe50ae | **D-020** | **N0 GREEN**: wide socket (TargetEstimate+EngineHealth at nominal), protocol **v4** (TLM 328/HELLO 80, world_hash 0x4EA27408), `null(r−target)` in hoverslam + all MPPI reads + CUDA .cuh, `--engine-out k@t|random`, `--target seeded|circle|line`, cmd_serve play-menu binding. Every golden byte-identical. |
| 350f24e | **D-021** | N1-S0 scaffold: `--policy-log` tap (36-col f64 rows: App-G legal obs + EXECUTED cmd), `trainer/` (torch cu128, held-out law enforced IN CODE), `GM_NEURAL=3` + `--neural` fixed-order fp64 inference + NP KAT. All default-off byte-clean. |
| 33df55d | **D-022** | S0 round-0: farm 36 seeds→2.08M rows→train→**NP_VERSION 1**→first learned landing (1/60). Pipeline proven; parity gate honestly NOT met (pre-registered BC covariate shift). |
| 8bb5f6a | **D-023** | **S0 GREEN**: DAgger shadow teacher + merged retrain (**NP_VERSION 2**) + **Tier-A′** (lateral-only: hoverslam keeps throttle, net owns only a_lat — the D-008 lesson). `--neural` AERO 45/47/43 vs MPPI 44/40/42 = **135/180 vs 126/180 (+9)**, held-out, ~9× faster. |
| acb75b4 | — | README truthed to D-023; gh repo description updated. |
| cbc89fe | — | **expert_iteration_design.md**: the two-operator teacher plan for pairwise/compound (see §5). |
| 0288403 | **D-024** | **Gust rounds** (**NP_VERSION 3**, 5.15M rows): student MORE shear-robust than teacher — s42 clean 43 vs 44; gust-12 **45 vs 38 (+7)**; gust-20 **46 vs 42 (+4)**; FLAT calm→20 m/s while MPPI degrades. |
| **ed7160c** | **D-025** | **Engine-out rounds** (**NP_VERSION 4**, 7.03M rows, six datasets) — the split verdict (§3). |

===============================================================================================
## 3. D-025 — WHERE WE ARE RIGHT NOW (the split verdict + the bombshell)
===============================================================================================
NP_VERSION 4 (sha cfa22fbee79c8aa8) trained on ALL six datasets (clean r0+r1, gusty ×2,
engine-out teacher + engine-out on-policy). KAT pinned in main.c from the C pass
(0.60011940451908519 / 1.0392702394237581 / 0.40001825395469304). Three-way honest result:

1. **Wider curriculum IMPROVED banked skills:** AERO clean **46/60 (teacher +2, best ever)**;
   gust-A held 45/60. Zero forgetting across the growing curriculum.
2. **ENTRY is at round-0:** clean 0/60 BUT the pre-DAgger near-miss anatomy (57 off-pad, ZERO
   faults, run-1 reaches **19.6 m** from a 62 km Mach-5.6 start). AERO showed this exact pattern
   before its 1→4→45 DAgger climb. The ENTRY ladder has begun, not failed.
3. **THE BOMBSHELL:** ENTRY `--engine-out random`: **MPPI 1/60 (from 57/60 clean).** The
   classical stack — whose directive-7 rollouts SEE the dead engine — loses 56 of 57 to a random
   in-burn failure. The H.0 "MPPI is a competent single-disturbance teacher" assumption is
   FALSIFIED for this axis at random draw times; most EO teacher-farm demos are demos of FAILING.

**>>> LIVE PROCESS RIGHT NOW (started 18:03): ENTRY CLEAN DAgger round-2 shadow farm <<<**
- Command: `runs\s0_farm.ps1 -Scenario entry -Mode neural -SeedBase 3200 -Seeds 12 -RunsPer 15
  -DeadlineLocal 20:00 -OutDir data\s0e_neural` (NO engine-out — clean ENTRY competence first;
  teacher is 57/60 clean, an excellent clean teacher). Files: `entry_s32xx.bin`.
- Monitor: **bt2f9zce6** (persistent, end/death only) → fires `ENTRY-R2-FARM-ENDED files=N`.
- **ON ITS COMPLETION, resume here (the D-026 sequence):**
  1. Merged retrain, SEVEN dirs: `python trainer\train_s0.py --data data\s0 data\s0r1
     data\s0g_mppi data\s0g_neural data\s0eo_mppi data\s0eo_neural data\s0e_neural --out
     runs\s0e.pt --epochs 120 --hidden 128 --seed 1234`
  2. `python trainer\export_weights.py --ckpt runs\s0e.pt --out core\neural_policy_weights.h
     --np-version 5`
  3. **KAT ceremony** (§4 rule): temp-replace the three `const double EXP0/1/2 = …;` in
     `core\main.c` test_neural_kat `#else` block with
     `printf("  KAT-REGEN a0=%.17g a1=%.17g a2=%.17g\n",a[0],a[1],a[2]); const double EXP0=a[0];
     const double EXP1=a[1]; const double EXP2=a[2];` → rebuild → `--selftest` → copy the three
     printed %.17g values → paste as the consts, update the `NP_VERSION 4 (engine-out…D-025)`
     comment + the three `NP_VERSION 4 bit-exact` labels to 5/D-026 → rebuild → selftest PASS.
     **NEVER compute the expectation in numpy — fixed-order fp64 accumulation differs.**
  4. Gates: selftest; TERMINAL ×200 byte vs runs\n0main_terminal.txt; MPPI run-1 (AERO s42 r1
     --mppi = HARD 2.63/10.48); no-regression AERO clean ×60 (expect ~46) + gust-A ×60 (~45).
  5. Eval: **ENTRY clean ×60 `--neural`** — the number that matters (did round-2 jump ENTRY off
     0, like AERO's round-1 did 1→4?). Determinism pair. Record honestly whatever it is.
  6. D-026 ledger (DECISIONS + RUN_STATE, D-024 format) + commit + push + refresh THIS file + the
     memory index line.

===============================================================================================
## 4. LAWS + TRAPS (violating these is how quality degrades — non-negotiable)
===============================================================================================
- **HELD-OUT LAW:** seeds 42/7/99 NEVER in training data. Trainer enforces (hard-errors on a
  gate-seed file). Farms use 1000s (clean), 2000s (gust), 3000s (engine-out), 3200s (entry clean).
- **KAT from the C pass, never numpy.** Every export = NP_VERSION bump = ADR + KAT regen + gates.
- **The ceremony per integration:** baselines before edit, byte-equality leak gates, determinism
  pairs, confirm the exe RELINKED (LNK1104 stale-exe trap #22).
- **Batches >10 min run DETACHED** (`Start-Process … -RedirectStandardOutput`) + a Monitor with
  death coverage. **The headless exit code reflects the LANDED RATE — it is NOT a farm-failure
  signal** (early DAgger/EO rounds crash most runs by design; s0_farm classifies success by tap
  file size, not exit code).
- **pwsh only; check `$LASTEXITCODE` after native calls; `--serve` needs explicit `--port 8787`**
  (bare default is 8080); locked desktop fails CopyFromScreen (use imguard + Win32 foreground
  capture when unlocked).
- Byte-gates are tamper-evident: dirty diff ⇒ investigate/re-run, NEVER commit on one.
- **Keep updating THIS file + the memory index** at every state change (operator standing order):
  `C:\Users\user\.claude\projects\C--Booster-Lander-Simulator\memory\MEMORY.md`.
- `s0_farm.ps1` params: `-Scenario aero_offset|entry|terminal`, `-Mode mppi|neural`, `-SeedBase`,
  `-Seeds`, `-RunsPer`, `-DeadlineLocal "HH:MM"`, `-OutDir`, `-GustFromSeed`, `-EngineOutRandom`.

===============================================================================================
## 5. THE ROADMAP FORWARD (priority order; D-025's redirect sharpened it)
===============================================================================================
**A. Finish the ENTRY clean ladder (D-026, IN FLIGHT):** round-2 → maybe round-3, until ENTRY
clean --neural is competitive (target ≥ a useful fraction of MPPI's 57/60). Prereq for any EO
recovery claim.

**B. THE 2-ENGINE FRONTIER ORACLE — now BLOCKING (canon §9.9/§A.4).** Generalize
`runs/sandbox/ceiling.c`: n_eng-scaled a_max in the reachable-set build + a gimbal trim-authority
debit during the burn window, parameterized by failure time. It ALONE distinguishes "MPPI's 1/60
is physics (most random-time failures are OUT of the shrunken reachable set — honest directive-6
crashes)" from "the controller is far from the frontier." No engine-out rate means anything until
this exists. Compiled C, offline (fits the C-only rule; it's precompute like D_phys).

**C. EXPERT-ITERATION teachers for the in-frontier EO subset (design: cbc89fe,
runs/expert_iteration_design.md; the corrected two-operator routing, from the operator's
AlphaZero-loop discussion + my §4.3-wind-blind correction):**
  - **Operator 1 — student-warm-started MPPI refinement**, VALID ONLY on rollout-visible axes
    (engine-out, moving-target, dispersions — the rollout model represents them). Warm-start
    MPPI's mean with the student's own action sequence → the sampler polishes an already-good
    plan instead of rediscovering one → composite (student proposes, sampler refines, plant
    verdict verifies) > either alone, à la network+MCTS. Small build: a neural-warm-start flag on
    GM_MPPI's warm_start_nominal (directive-7-clean, precompute).
  - **Operator 2 — verdict-filtered self-imitation**, for WIND (canon §4.3: MPPI rollouts are
    wind-BLIND, so sampler-refinement on the gust axis pulls the student back toward 38/60 —
    HARMFUL). Fly the student under shear, keep ONLY demonstrations that LANDED well (td_v/lat
    thresholds), distill those. The plant verdict is the oracle that sees everything, free.
  - On mixed rungs operators run in SERIES, verdict arbitrates. Promotion needs the 3-seed ×180
    battery (D-023 pattern — n=60 is inside Wilson noise), a clean-air floor (ratchet), and the
    frontier metric as judge. Echo-chamber defenses: merged replay of all old rounds (never let
    the fixed-teacher fraction hit zero), domain randomization, held-out conditions.
  - Timing: this is the PAIRWISE/COMPOUND-rung teacher (where the fixed teacher was predicted to
    fail — and just DID, D-025). It's a one-ADR upgrade to the loop, not a redesign.

**D. Pairwise → JOINT compound = N3, THE SHOWCASE** (+ the M4 attempt: AERO ≥54/60 ⇒ M4 GREEN via
GM_NEURAL; the 0.70·D_phys plateau alternative routes M4 to the plant-authority ADR — either
decisive). Engine-out × gust × moving target in one descent, scored vs the shrunken BRS, demoed
WITH the honest adjacent out-of-frontier failure (§G). This is the operator's original wow — and
D-025 made it bigger: it's now a demo of something the classical stack measurably CANNOT do.

**Parallel-safe anytime:** target Stage-1 (SEA deck z(t) + §A.3 target-relative verdict +
asds_night — unblocks the moving-target axis, whose verdict currently scores the ORIGIN); the
interactive ws command channel (live inject/drag, journaled per §10.8 so improvised runs replay
bit-exact; then UE 5.8 on the same wires — the operator's declared destination); `--neural
--nav-noisy` honesty spot; np_version plumbing into fill_hello/fill_tlm. RL proper (S1, booster_env
ABI §19.2) ONLY where distillation caps — it hasn't yet; expert-iteration is the nearer lever.

===============================================================================================
## 6. THE NUMBERS TABLE (held-out s42 ×60 unless noted; the whole learned-policy story)
===============================================================================================
                         MPPI (teacher)    --neural            note
AERO clean               44/60             43(v2)→45→46(v4)    student ≥ teacher, improving
AERO gust 12@5000:800    38/60             45/60 (v3)          +7, student flat under shear
AERO gust 20@3000:600    42/60             46/60 (v3)          +4
S0 held-out (42/7/99)    126/180           135/180 (v2)        +9 total, ~9× faster
ENTRY clean              57/60             0/60 (v4, round-0)  near-miss anatomy; DAgger climbing
ENTRY engine-out random  **1/60**          0/60 (moot@r0)      THE BOMBSHELL — teacher collapses
Honest intermediates: BC round-0 1/1/0; full-Tier-A round-1 4/5/3 (Tier-A′ took it to 45/47/43).
Weights lineage: NP_VERSION 1 b4141469 (D-022) → 2 d6249fec (D-023) → 3 5fd2b9a7 (D-024) →
4 cfa22fbe (D-025) → 5 pending (D-026). Datasets: data\s0 (36 seeds clean), s0r1 (on-policy),
s0g_mppi/s0g_neural (gust), s0eo_mppi/s0eo_neural (engine-out), s0e_neural (entry clean, FILLING).

===============================================================================================
## 7. OPERATOR CONTEXT / VERBATIM RECENT INSTRUCTIONS
===============================================================================================
Bo Chen. The true wow he wants to SEE: an engine-out recovery, live, in the cockpit (N3). UE 5.8 +
full interactivity is declared. Star-Trek-"Warhead" framing (agent-vehicles you talk to in goals).
He can fork/branch/rewind the session to an earlier state — hence this file must reconstruct THIS
moment in totality. Recent verbatim: *"agentically and autonomously continue and proceed"*; *"keep
updating your handoff"*; *"if compacted, ingest and rehydrate memory before resuming so no quality
degradation"*; on H200s: local suffices through the distill arcs, rent for the fp64 CUDA teacher +
parallel farms at the joint/RL scale; *"give a memory dump and continuation prompt… so any past
version of you can be fully brought up to speed to THIS moment in all its totality."*

*End of sitrep. Trust the files; verify §1; resume at §3 (the live farm's completion) → §5.*
