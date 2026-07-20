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
- **HEAD: 79fa8c0 (D-028).** Canon = `CLAUDE_v2.md`. **NP_VERSION 6** (sha b4734b48).
- Learned policy: AERO clean **46** (teacher+2), gust **45** (teacher+7), **ENTRY clean 57/56/58** (teacher parity). No forgetting ×7.
- **D-027:** engine-out collapse is a **CONTROLLER SHORTFALL** — the whole random-EO draw is in-frontier ⇒ claimable.
- Model: **Opus 4.8** (Fable weekly maxed — stay on Opus). Machine sleeps; detached jobs resume on wake.

## THE MAP — the engine-out arc → N3 showcase (the operator's ORIGINAL wow)
- ⬜ **E0 — v6 EO baseline** (`runs\eo_baseline_v6.txt`, in flight): free-competence ENTRY `--engine-out random` ×60 ×3 + MPPI comparator. On done → log to handoff; it sets the bar E1/E2 must beat.
- ⬜ **E0b — oracle gilding** (cheap, farm-free): one `--engine-out 1@11 --mppi` verbose ENTRY replay logging wmag/gimbal/dist_pad(t) → closes the single axis D-027 *bounded* (the 6-DOF attitude transient). Append to `runs\eo_frontier_report.md`.
- ⬜ **E1 — expert-iteration EO TEACHER build** (`guidance_mppi.c` neural-warm-start mode; default-off byte-clean; design = `runs\expert_iteration_design.md` cbc89fe). Ship its own ADR with **validity tables** (composite-vs-student AND composite-vs-teacher on ENTRY engine-out) BEFORE it labels a single training row. EO is rollout-visible ⇒ the composite operator is valid. **Do while exe is farm-free (LNK1104).**
- ⬜ **E2 — ENTRY engine-out DAgger rounds**: teacher farm (the better teacher) + on-policy shadow → merged retrain → **NP_VERSION 7** → KAT ceremony (C pass) → full gates + no-regression floors (AERO 46 / gust 45 / ENTRY-clean ≥57) → ENTRY `--engine-out random` ×60 ×3 eval, **scored recovery-vs-frontier** (D-027 oracle = the honest denominator). Expect a ladder like ENTRY clean's 0→52→57.
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
