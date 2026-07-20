# NEXT_SESSION — cold-start / continuation prompt (paste verbatim into a fresh session)

*Point a fresh session here. Rewritten 2026-07-19 after D-028 (ENTRY teacher parity) + E0/E0b
(engine-out baseline + gilding). The `.claude/settings.json` SessionStart/PostCompact hook
auto-fires the short resume protocol; this file is the complete cold-start. Refresh the "NEXT"
block when the next step changes. Trust files over recollection; verify against disk.*

---

You are resuming an autonomous build on the BOOSTER LANDER SIMULATOR at
C:\Booster_Lander_Simulator (Windows 11, pwsh 7; public mirror
github.com/bochen2029-pixel/booster-lander-simulator, push per completed unit).
Operator: Bo Chen — standing autonomy granted: proceed at your best recommendation,
honest numbers always (record nulls with data), ledger discipline always, ask only on
true forks or hard blockers. Values momentum + byte-exact determinism above all.
Model: Opus 4.8 (Fable weekly is maxed — stay on Opus).

== BOOTSTRAP (in order, before anything) ==
1. Read ROADMAP.md — the perpetual map + RESUME PROTOCOL. Your next step = the first ⬜ box.
2. Read HANDOFF_2026-07-19_MORNING_AUTORUN.md §0 LIVE LOG (top, newest-first) — minute-by-minute state.
3. Read MEMORY.md (project memory index) + CLAUDE_v2.md §0–§2 (canon) + DECISIONS.md tail (D-024…D-028).
4. Windows/pwsh discipline: C:\Users\user\.claude\WINDOWS_SHELL_GUIDE.md (detached=Start-Process
   NOT Start-Job; watch an artifact/marker NEVER a PID; pwsh not powershell; check $LASTEXITCODE;
   never build over a live farm = LNK1104).
5. VERIFY DISK BEATS MEMORY:
   - git log --oneline -5  → HEAD is the D-028/E0b commit (ENTRY teacher parity + engine-out gilding).
   - .\build\bin\Release\booster-core.exe --selftest  → SELFTEST: PASS (incl. NP KAT NP_VERSION=6).
   - .\build\bin\Release\booster-core.exe --headless --scenario terminal --seed 42 --runs 200
     → 194/200, byte-identical to runs\n0main_terminal.txt (the standing leak gate).

== STATE (verify, don't re-derive) ==
Canon CLAUDE_v2.md (D-019). Learned policy GM_NEURAL (--neural), Tier-A' lateral-only,
NP_VERSION 6 (sha b4734b48). AERO clean 46 (teacher+2), gust-A 45 (teacher+7), ENTRY clean
57/56/58 = TEACHER PARITY; no forgetting ×7. Engine-out is a proven CONTROLLER SHORTFALL
(D-027 + E0b gilded on all 4 axes: whole random-EO draw is in-frontier, ~59/60 claimable;
the crash is lateral-closure, attitude holds). E0 baseline: v6 on --engine-out random = 1/0/0,
dead parity with MPPI 1/60 — EO competence is NOT free.

== NEXT = E1: build the EXPERT-ITERATION ENGINE-OUT TEACHER ==
Design = runs\expert_iteration_design.md (cbc89fe). Engine-out IS rollout-visible ⇒ the composite
operator (student-warm-started MPPI) is the valid one for this axis. Build a NEURAL-WARM-START
mode in guidance_mppi.c: seed MPPI's mean (warm_start_nominal) with the student policy's action
sequence rolled through the lean model, instead of the hoverslam recipe — so the sampler polishes
an already-competent plan. Default-OFF, byte-clean (leak gate). Ship its OWN ADR (D-029) carrying
the VALIDITY TABLES — composite-vs-student AND composite-vs-teacher on ENTRY --engine-out random —
BEFORE the composite labels a single training row (if it doesn't beat the student head-to-head, it
is not an improvement operator and must not teach). Then E2: EO DAgger rounds → NP_VERSION 7 →
KAT → gates → ENTRY --engine-out random ×60 ×3 eval scored recovery-vs-frontier (the ceiling_eo.c
oracle = the honest denominator). Then E3 pairwise → N3 the compound showcase.

== HARD LAWS (violating = quality loss) ==
Held-out law: s42/s7/s99 NEVER in training data. KAT pinned FROM THE C PASS, never numpy; every
export = NP_VERSION bump = ADR + KAT + re-golden. Gates every build: selftest · TERMINAL ×200
byte · MPPI run-1 AERO s42 = HARD 2.63/10.48 · determinism pairs · leak byte-equality; dirty
byte-gate ⇒ investigate, NEVER commit. Batches >10 min = detached (Start-Process) + a Monitor on
the artifact/marker. Update ROADMAP.md (check boxes) + the handoff LIVE LOG at every state change.

Trust files over recollection; verify against disk; resume at ROADMAP.md's first ⬜ (= E1).
