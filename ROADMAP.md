# ROADMAP — Booster Lander (perpetual, compaction-proof plan)

> **You are reading this because the SessionStart hook forced it in front of you. Good. Follow
> the RESUME PROTOCOL, then work the first unchecked box. This file + the handoff live-log +
> MEMORY.md are the source of truth; trust files over recollection; verify against disk.**

## RESUME PROTOCOL (do this FIRST, every fresh/compacted session)
1. Read `C:\Users\user\.claude\projects\C--Booster-Lander-Simulator\memory\MEMORY.md` — the top ADR line is your state.
2. Read `HANDOFF_2026-07-19_MORNING_AUTORUN.md` §0 **LIVE LOG** (top, newest-first) — the minute-by-minute.
3. Verify disk: `git log --oneline -3` (expect the HEAD the memory names) · `.\build\bin\Release\booster-core.exe --selftest` → PASS.
4. Find the **first ⬜ box** below = your next step. Proceed under **standing autonomy** (Bo Chen: momentum, honest nulls, ledger discipline, byte-gates). Stop only on a hard blocker → write it into this file + ping the operator.
5. **Update this file (check boxes, add notes) AND the handoff live-log at every state change.** Keep the perpetual map current — that is the whole point.

## STATUS (update on every ADR)
- **HEAD: D-030 (2026-07-20 night; E1 null → E1.5 EO-divert lever).** Canon = `CLAUDE_v2.md`. **NP_VERSION 6** (sha b4734b48).
- Learned policy: AERO clean **46** (teacher+2), gust **45** (teacher+7), **ENTRY clean 57/56/58** (teacher parity). No forgetting ×7.
- **D-027 + E0/E0b:** engine-out collapse is a **CONTROLLER SHORTFALL** (gilded on all 4 axes) — whole random-EO draw in-frontier ⇒ ~59/60 claimable. **v6 EO baseline = 1/0/0 (= MPPI); E1 is the real work.**
- **E1 NULL (D-029) → E1.5/D-030 DONE: the 2-engine entry-divert re-authorization LIFTS EO recovery** (reactive 0/1→**9/10**, neural 1/0/0→**8/4/2** of 60; byte-clean). Pivotal: reactive now beats neural on EO. → **E2 DAgger = NULL (D-031): distillation can't exceed a same-level teacher; the EO ceiling for ALL controllers is ~8–10/60 (NP_VERSION 7 rejected, reverted to v6). NEXT: exceed it — reactive-shadow DAgger (marginal) or RL/learned-entry-divert (reserved, →frontier); OR pivot to a parallel-safe showcase axis (Target Stage-1).**
- Model: **Opus 4.8** (Fable weekly maxed — stay on Opus). Machine sleeps; detached jobs resume on wake.

## THE MAP — the engine-out arc → N3 showcase (the operator's ORIGINAL wow)
- ✅ **E0 — v6 EO baseline** (`runs\eo_baseline_v6.txt`): v6 on `--engine-out random` ×60 = **1/60 · 0/60 · 0/60** (s42/s7/s99) = dead parity with MPPI 1/60. ENTRY competence does NOT transfer to EO for free; ~59/60 claimable headroom. E1/E2 confirmed necessary.
- ✅ **E0b — oracle gilding** (`runs\eo_gild_1at11.txt`, report §8): `1@11 --mppi` = CRASHED lat 118.85 m **tilt 0.02°** fault=none → attitude held, NO tumble; pure lateral-CLOSURE failure. D-027 verdict now gilded on all 4 axes.
- ✅ **E1 — composite operator (student-warm-started MPPI): BUILT, byte-clean, NULL (D-029).** ENTRY `--engine-out random` ×60 s42: composite **1/60 == student 1/60 == teacher 1/60** — NOT an improvement operator (design §3: must not teach). Phase-attribution (`runs\e1_phase_attribution.txt`): the gross cluster (~75%) is lost at the ENTRY-BURN CUT (2-engine divert closes only ~830 m); MPPI/neural only steer POST-cut ⇒ the composite is structurally blind to it. `--mppi-warm-neural` kept default-off (D-018 pattern). ⇒ redirect to E1.5.
- ✅ **E1.5 — 2-ENGINE ENTRY-DIVERT re-authorization (D-030): DONE.** `entry_divert_step` under n_eng<3: bank cap **15°→35°** + **KR×4/KV×2.5** (byte-clean, frozen #defines; leak GREEN). EO recovery lifts mode-independently (ENTRY EO ×60): reactive **9/10** of 60 (was 0/1), neural **8/4/2** of 60 (was 1/0/0), held-out generalizes. Effective KR8/KV8.75 near-critical (baseline 2/3.5 overdamped). **Pivotal: REACTIVE now beats NEURAL (2–8) — the clean-trained policy mishandles the hot 2-engine handoff ⇒ E2.** Partial fix (~8–10×); hardest draws + terminal quality remain.
- ❌ **E2 — ENTRY engine-out DAgger round (D-031): NULL, reverted.** MPPI+D-030 teacher (~10%) distilled → NP_VERSION 7 EO **6/0/5** of 60 = 11/180, WORSE than v6+D-030's 14/180 (+ gust-A 45→44). A teacher no better than the student (v6 8/60) can't lift it (D-025 confirmed directly). Rejected → reverted to v6 (selftest KAT v6 PASS, EO 8/60 restored). **The distillation-era EO ceiling is mapped: all controllers ~8–10/60.** Datasets `s0eo2_*` + ckpt `s0eo2.pt` preserved.
- ⬜ **E2' / N3-S2 — EXCEED the EO ceiling** (the real remaining EO work): (a) distill the BEST teacher — a reactive/hoverslam-shadow DAgger (a shadow-tap change) to reach ~9–10 in one policy (marginal, clean); and/or (b) the reserved **RL lane** (SAC/PPO warm-started, §19) or a learned/MPPI-planned entry divert, to exceed ~10/60 toward the D-027 frontier (~59/60). D-030's 1→8–10 is the shipped distillation-era win; the frontier gap is an RL-class problem.
- ⬜ **E3 — pairwise**: gust + engine-out together. Mixed-rung ⇒ operators in SERIES, verdict arbitrates (cbc89fe routing).
- ⬜ **N3 — THE COMPOUND SHOWCASE**: engine-out × gust × moving-deck in one descent, scored vs the shrunken BRS, demoed **WITH** the honest adjacent out-of-frontier failure (§G.2 — must be MANUFACTURED at 6–8 km / center-engine-out / two-out, per D-027). + the **M4 attempt** (AERO ≥54/60 ⇒ M4 GREEN via GM_NEURAL; else the 0.70·D_phys plateau routes M4 to the plant-authority ADR). **This is the wow.**

## PARALLEL-SAFE (any farm-free or frontend window)
- ⬜ **Target Stage-1** (SEA deck z(t) + §A.3 target-relative verdict + asds_night) — unblocks the moving-target axis N3 needs (today the verdict scores the ORIGIN under an armed target).
- ⬜ **Interactive ws command channel** (live inject/drag over the closed upstream enum, journaled §10.8 so improvised runs replay bit-exact) — the demo instrument; then UE 5.8 on the same wires.
- ⬜ **`--neural --nav-noisy` honesty spot**; np_version plumbed into fill_hello/fill_tlm.

## HARD LAWS (violating = quality loss; full text: handoff §4 · CLAUDE_v2 · `C:\Users\user\.claude\WINDOWS_SHELL_GUIDE.md`)
- **Held-out law:** s42/s7/s99 NEVER in training data (trainer enforces). **KAT from the C pass, never numpy.** Every export = NP_VERSION bump = ADR + KAT + re-golden.
- **Windows/pwsh:** detached = `Start-Process` (never `Start-Job`); watch an artifact/marker (never a bare PID — Windows recycles them); `pwsh` never `powershell`; check `$LASTEXITCODE`; never build over a live farm (LNK1104 stale-exe); `--serve` needs explicit `--port 8787`.
- **Gates:** selftest · TERMINAL ×200 byte vs `runs\n0main_terminal.txt` · MPPI run-1 AERO s42 = HARD 2.63/10.48 · determinism pairs · leak byte-equality. Dirty byte-gate ⇒ investigate, NEVER commit.

*Ledger arc so far: D-019 canon → N0/N1 pipeline → D-023 student beats teacher clean → D-024 beats under shear → D-025 teacher collapses on EO → D-027 collapse is recoverable ground → D-028 ENTRY solved at parity. One clean arc (E1→N3) from the showcase.*
