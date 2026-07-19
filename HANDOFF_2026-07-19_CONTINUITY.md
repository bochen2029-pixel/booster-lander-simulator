# CONTINUITY — Booster Lander Simulator · live-state resume prompt (2026-07-19, ~21:55)
### Layered ON TOP of `HANDOFF_2026-07-18_NIGHT.md` (the constitution — read it FIRST, in full).
### This file is the delta: what happened tonight after that handoff, what is in flight RIGHT NOW, and the exact pending procedure.

> **HOW TO USE (crash/reboot/new session):** *"bootstrap and awaken: read
> C:\Booster_Lander_Simulator\HANDOFF_2026-07-18_NIGHT.md in full, then
> C:\Booster_Lander_Simulator\HANDOFF_2026-07-19_CONTINUITY.md, then proceed autonomously."*

---

## 0. WHERE THE NIGHT STANDS (all pushed to GitHub main unless marked)

Six pushes tonight, in order:
| commit | content |
|---|---|
| b3399b0 | **D-012** state-adaptive divert gain (powered-burn overspeed brake): ENTRY 88/79/78, AERO 73.3, MPPI 44/60 best |
| 0e33411 | D-012 addendum: reactive M6 levers closed (trim grid null, engine-cut relight-blocked); ENTRY 88 = reactive plateau |
| 03d8f70 | **D-013** telemetry protocol v3 (`pred_impact[2]` + `ignite_h`, BL_PROTO_VERSION 3, TS mirror, goldens) + research trilogy (mppi/gfold/windthink) + toolsmith report |
| d16f8ea | D-013 addendum: RELIGHT null (cut works, seeds fuel-starved anyway) + WINDBUILD falsified (AoA 9-11° kills weathervane premise); `tools/` mcdiff+tracestat |
| eda595e | **D-015** M5 CUDA MPPI integrated (`--mppi-cuda`, fp64-everywhere unity-include, 1-ULP parity, bit-identical run-twice at K=256 AND 16384, 44/60 line-for-line vs CPU; CI-safe conditional CMake) + **D-014** wind-estimator ADR (ledger session's) |
| (pending) | **D-016** — see §2. THE HEADLINE IS ALREADY MEASURED. |

**The current tree** = D-015: protocol v3 + CUDA path + all D-012 guidance. Gates on it (verified
post-integration): selftest PASS · TERMINAL 194/200 · protocol goldens MATCH · MPPI run-1
invariance HARD td_v 2.63 / lat 10.48 (CPU AND CUDA, identical) — that run-1 line is the current
(1.5,3)-config reference.

## 1. TONIGHT'S DECISIVE RESULTS (all in DECISIONS.md D-012…D-015 with full numbers)

1. **ENTRY 88 is the reactive plateau** — every reactive/estimation lever measured-closed:
   brake/deck-null/trim grids saturated (D-012+addendum), engine-cut null (560 runs, the
   fuel-marginal seeds lack propellant for ANY strategy), wind estimator falsified at
   observability (the vehicle never weathervanes: mean AoA 9-11° while diverting).
2. **Capacity is NOT the rate lever**: K 256→512→1024 = 44/44/42 (kprobe; 0 of 14 off-pads
   converted, 2 landings lost). Cheap param levers null-to-harmful (mvar: λ-floor 0.5 = −2,
   OU θ 0.10/0.08 = −2/−3). **Freeze the CUDA golden at K≈1024** (joint optimum: rate saturates
   by 512; fp64 real-time ceiling ~65 ms < the 100 ms/10 Hz budget). AERO-90 (M4) needs a
   STRUCTURAL variant: CoVO covariance or MPOPI iterate-per-replan (runs/mppi_research.md).
3. **★ ENTRY-UNDER-MPPI = 95/100 s42 ★** (first-ever ENTRY --mppi batch; lane entry-mppi;
   per-run CSV `_empi_wt\entry_mppi_s42_x100_v2.csv`, 100 rows): op 2, th 1, fuel 1, other 1;
   landed td_v mean 3.31. **ZERO code changes — stock shipped config** (byte-verified worktree;
   AERO invariance 44/60 identical). Run 14 — the relight-study's unsavable fuel-trap seed —
   LANDS under MPPI with 2346 kg remaining (replanning arrives centered → short cheap burn →
   never enters the min-throttle trap). **The M6 gate is ≥90. This clears it by 5, pending §2.**

## 2. IN FLIGHT RIGHT NOW → THE PENDING D-016 / M6-GREEN PUSH

**Two background batches on the MAIN exe** (started ~21:02, ~50-80 min expected):
- ENTRY s42 ×100 `--mppi` on main — the confirmation (expect ≈95; D-013/D-015 are
  behavior-neutral for this path per the invariance pairs, so likely EXACTLY 95).
- ENTRY s7 ×100 + s99 ×100 `--mppi` — the cross-seed honesty numbers (reactive were 79/78).

**If those batches were lost (reboot/crash), re-run them:**
```powershell
$exe="C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"   # rebuild first if needed; gates per NIGHT §1.7
& $exe --headless --scenario entry --seed 42 --runs 100 --mppi    # expect ~95/100
& $exe --headless --scenario entry --seed 7  --runs 100 --mppi
& $exe --headless --scenario entry --seed 99 --runs 100 --mppi
```

**THE PROCEDURE WHEN s42 CONFIRMS ≥90 (write it exactly like this):**
1. Full-capture golden: re-run ENTRY s42 ×100 --mppi piping FULL output to a file; re-run again
   → bit-identical; freeze as `goldens/mc/entry_mppi_s42_d016_baseline.txt` (header comment in
   the style of the d012 goldens, noting cross-seeds + stock-config provenance).
2. Directive-7 spot: ENTRY single-run pair CPU `--mppi` vs `--mppi-cuda` (e.g. --run 14) —
   expect identical RESULT lines (CUDA parity was proven on AERO; this is the ENTRY spot).
3. Honesty spot: ENTRY s42 ×100 `--mppi --nav-noisy` (reactive nav-noisy was 74).
4. **D-016 ADR** in DECISIONS.md: ENTRY-under-MPPI 95/100 stock-config (the M6 gate), the
   run-14 trap-escape mechanism, the cross-seed table, main-tree confirmation numbers, the
   KPROBE freeze-at-K≈1024 verdict adoption, the MPPIVAR null adoption, gate criterion stated
   honestly (s42 ≥90 with cross-seeds reported, like every prior gate claim).
5. RUN_STATE.md: ★★★★★★ M6 block. HANDOFF_2026-07-18_NIGHT.md: update the headline table +
   mark Roadmap A DONE (M6) — or write HANDOFF_2026-07-19_*.md fresh if context allows.
6. Commit (leak check first — NIGHT §6 GitHub rules): the goldens + ledger files + any
   remaining final artifacts. Push. **M6 GREEN unlocks M7 per directive 10** (and note: the
   operator's SEPARATE frontend fleet already has the documentary view + S3 audio sketch live
   against `--serve` — coordinate via intercom, do NOT touch ui/ from this session).

**Already-final artifacts on disk, committed in the continuity push alongside this file:**
kprobe_report.md + kprobe_sweep.csv + kprobe_aero_k{512,1024}.csv + kprobe scripts;
mvar_report.md + mvar_sweep.csv + mvar_*.csv + scripts; empi_report.md (harvest-finalized).
NOT ours to commit: runs/fe_*.md (frontend fleet's), runs/brainstorm_*.md (another session's).

## 3. THE FLEET (all concluded — nothing running except the two batches above)

~17 Opus agents across 3 waves tonight. All lanes CONCLUDED with reports in runs/:
proto ✓ · mppi-research ✓ · gfold-research ✓ · windthink ✓ · toolsmith ✓ (tools/ landed) ·
relight ✓(null) · windbuild ✓(falsified) · decknull ✓(reject) · cuda-mppi ✓(integrated) ·
kprobe ✓(saturation verdict) · mppi-var ✓(null) · entry-mppi ✓(THE 95/100).
Worktrees on disk (`_*_wt/`, all gitignored): preserved evidence; safe to delete EXCEPT keep
`_empi_wt` (the 95/100 CSV) and `_wind2_wt` (D-014 settling artifact) until D-016 is pushed.

**Fleet lessons (hard-won, reuse them):** (a) THE IDLE-WAIT TRAP killed 8 agents — any
subagent that "waits for a notification" dies; mandate the repeated foreground sleep-loop poll
pattern (~8 min per call, reissued) in EVERY batch-running agent prompt. (b) Successor agents
adopt orphan work — worktree batches SURVIVE agent death; inspect processes
(`Get-CimInstance Win32_Process -Filter "Name='booster-core.exe'"`) + output files before
re-running anything. (c) The stale-exe trap is real and bit determinism makes it detectable:
summary-identical means = the changes were not in the binary. (d) Intercom lanes + the main
session as sole integrator with full gates per unit — zero work lost across 8 agent deaths.

## 4. AFTER M6 — THE RANKED ROAD (supersedes NIGHT §8 ordering)

**A. M4 (AERO ≥90) = the structural-variant build**: MPOPI iterate-per-replan (4096×4 at fixed
budget) or CoVO diagonal covariance, at K≈1024 on the CUDA path, per runs/mppi_research.md +
kprobe_report.md. Pre-registered follow-up: retest LAMBDA_MIN 0.5 at K≥1024. This is the one
remaining guidance gate. AERO misses are proposal-support-limited — this is the measured lever.
**B. Consider ENTRY-MPPI as the new ENTRY default?** NO — not without an ADR + full Tier-B
(nav-noisy/inject) + cross-seed ≥90 evidence; GM_HOVERSLAM remains default; `--mppi` is the
M6-gate configuration. Record the distinction honestly in D-016.
**C. M7 renderer**: unlocked at M6 GREEN; the frontend fleet is ahead of us — coordinate on the
bus (lane frontend-main exists), protocol evolution (pred_impact v2 = planner's own terminal
projection) goes through the D-010/D-011 mirror+golden unit process.
**D. Tier-B matrix + housekeeping**: nav-noisy+inject combined (measured tonight: TERMINAL 98.0,
ENTRY 75.0, AERO 50.3 — record in the next ADR/README robustness table); constants
consolidation; CHAOS/ASDS unattempted.

## 5. TOOLING (new since NIGHT)

- `tools/mcdiff.c` + `tools/tracestat.c` (committed; build: cl /O2 /W4 from vcvars64) — per-run
  CSV A/B + verbose-trace forensics. USE THEM instead of eyeballing batches.
- `--mppi-cuda` / `--mppi-cuda-verify` / `--mppi-cuda-bench` on the main exe (CUDA build).
- Intercom: rejoin per NIGHT §6 (`--lane main`); the room's tonight-history IS the fleet record.

*The plant has been wrong six times; the method has never been. Tonight the method closed five
levers with numbers, ported the planner to the GPU at 1 ULP, and measured the M6 gate falling.
Finish the validation, freeze the golden, write D-016, push.*
