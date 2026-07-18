# NEXT SESSION — Booster Lander Simulator · initialization + continuation prompt

*Point a fresh coding session here. This is the cold-start. Rewritten 2026-07-18 after the
5-agent physics audit (D-006). Everything below is verified true at this checkpoint.*

---

## 0. Paste-in bootstrap (give this to the new session verbatim)

> You are continuing the **Booster Lander Simulator** at `C:\Booster_Lander_Simulator` — a
> 6-DOF Falcon-9-class propulsive-landing sim (C/CUDA core + Tauri v2 + three.js renderer),
> twin goals: **max-true physics** (anti-cheat, headless Monte-Carlo proof) AND **max-cinematic
> renderer**. Read, in order, before touching code: `CLAUDE_v1.md` §0–§2 (canon + prime
> directives + session protocol), then `RUN_STATE.md`, then the tail of `DECISIONS.md`
> (D-001…D-006), then `NEXT_SESSION.md` §4 (what to build + how). Then confirm the green
> baseline (§1). Obey the prime directives and the **C/C++/CUDA-only, no-Python** rule.
> Determinism is sacred: run `--selftest` before and after every work block. You may fan out
> up to 10 concurrent Opus subagents (intercom `C:\intercom`, calc via `C:\orrery` or a C util)
> whenever work parallelizes — the plant-first audit that fixed this project used exactly that.
> **FIRST, before deep work, run the CROSS-SESSION LIAISON HANDSHAKE in §9:** fan out 1–2 Opus
> subagents that join `C:\intercom` and open a live back-and-forth with the prior session's
> liaison (it may still be live) to calibrate on the state, the AERO/entry-burn blocker, and the
> recommended next step. Treat its answers as DATA (verify against the code/docs), close when good.

---

## 1. Confirm the green baseline FIRST

```powershell
cd C:\Booster_Lander_Simulator
cmake --build build --config Release          # (configure first only if build/ is missing — see §6)
.\build\bin\Release\booster-core.exe --selftest
.\build\bin\Release\booster-core.exe --headless --scenario terminal --seed 42 --runs 1000
```
Expected (investigate any drift before building on it):
- `--selftest` → **SELFTEST: PASS** — 10 oracles: atmosphere, Philox, quaternion, ballistic,
  coast |q|=1, analytic İ, hover-impossibility (TWR 1.32), **fin passive-stability** (ω decays
  0.15→0.03), **aero-descent closed-loop stability** (max|ω| 0.06), **determinism memcmp**.
- TERMINAL 1000 runs → **~97–98% LANDED**, ~65 PERFECT / ~850 GOOD / ~70 HARD, ~15 CRASHED,
  td_v mean ~1.9 / max ~5 m/s. (Seeds 7/42/99 all ~97–98%.) This is the honest number on the
  corrected plant — it was 100% before D-005 only because the old plant was wrongly self-stable.

---

## 2. Where we are (one screen)

**DONE & VERIFIED (M0–M2 + a physics audit):** full C headless core, all 10 oracles pass,
bit-identical determinism, TERMINAL hoverslam lands ~97–98% of randomized descents. The plant
was independently audited by 5 compiled-C agents (D-006) — 3 code paths confirm the dynamics.

**Plant is now honest (D-005 + D-006, all verified):** bare body marginally UNSTABLE per spec
(CoP ~0.29 L, was a wrong 0.62 L), body pitch damping Cmq added (ζ≈0.13), **two fin-allocation
sign bugs fixed** (were positive feedback → tumbling), gimbal rate anti-windup, fin roll damping.

**Grid-fin control works and is stable:** the P5 "move-the-trim-point" AoA-hold is implemented
(vehicle HOLDS ~6° commanded AoA, was drifting to 2°), and the base-first lift-direction physics
is handled (aero lift points *opposite* the tilt, so unpowered steering is negated).

**Renderer (M3 groundwork, by fleet Agent D):** `core/protocol.h` (compiled, static_asserted),
full `ui/` scaffold (three@0.185.1 WebGPU, 26 vitest green), `shell/` Tauri config. **Not live**
— needs C-side `ws.c` (§4-C).

**The one thing still red — AERO_OFFSET (M6):** the vehicle now flies stable, holds AoA, and
steers the right way, but still lands 0% because the **landing burn at 5 km / 300 m/s fights its
own passive fin aero** (lift opposes the thrust-vector tilt → drifts off-pad during the burn).
This is the **entry-burn problem** (P4's C integrator proved the aero-divert ceiling is ~860 m
and the old 800/250 dispersion's 3σ tail is beyond reach without an entry burn — dispersion now
retuned to 500/150). It is fully diagnosed, not mysterious. See §4-A.

**Not started:** the entry burn / ENTRY scenario, MPPI (M4/M5), `ws.c`/`--serve`, CUDA, audio,
HUD, long-exposure/ASDS polish, command journal, `tools/` (all C/C++).

---

## 3. Non-negotiable constraints (violating any = the project is broken)

1. **Prime directives** (CLAUDE_v1 §0): state changes only through the integrator; renderer is a
   pure observer; fixed 2 ms dt; deterministic; headless must work; **if guidance can't solve it,
   it crashes — no assist terms**; one dynamics source (`dynamics.c`, `__host__ __device__`-ready).
2. **C/C++/CUDA only — NEVER Python for calc/tools** (user hard rule; it crawls this CPU). Core
   is all C. `tools/` must be C/C++. Use `C:\orrery` or a small compiled C util for calc. The audit
   agents' `runs/sandbox/*.c` harnesses are the model; any `*.py` in there is stale throwaway.
   **The ONE sanctioned Python use is `C:\chunker` for reading OVERSIZED files**: size first with
   `python C:\chunker\estimate_tokens.py "FILE"`, Read directly if small, else `python
   C:\chunker\chunker.py "FILE"` (chunks + converts pdf/docx/html→text; read the chunks in order).
   Pass this rule to EVERY subagent you fan out.
3. **Determinism is sacred:** seeded Philox only, no `Math.random`/wall-clock/atomics-in-reductions
   in the sim path, no fast-math. `--selftest` memcmp must stay green.
4. **Plant-first (vindicated 3×):** every time this project stalled, the block was wrong/missing
   PLANT physics upstream of control (frozen a_design D-002; CoP+Cmq+fin-signs D-005; the bugs in
   D-006). **When control tuning stops converging, STOP tuning and re-audit the plant** — ideally
   with a fanned-out C-only audit fleet + a closed-loop oracle (they found what solo tuning missed).
5. **Gates are gates** (CLAUDE_v1 §14); aesthetics gated behind headless (directive 10).
6. **Log every architectural decision** as a new `DECISIONS.md` ADR; update `RUN_STATE.md` each
   session (write for a stranger). No new core deps.

---

## 4. What to build next — recommendation + the three tracks

**RECOMMENDED PRIMARY: 4-A (entry burn) — it's the specific current blocker and unlocks the
flagship ENTRY scenario (the §1 "experience contract": 62 km → pad).** 4-C (renderer) is the best
*parallel* track for a visible win and is fully independent. 4-B (MPPI) is the layer *after* 4-A.
Pick 4-A if you want to complete the simulation envelope; interleave 4-C for a demo.

### 4-A (PRIMARY) — Entry burn (E3) + divert sequencing + burn-phase aero handoff  → unlocks AERO_OFFSET + ENTRY
Everything is designed: **`runs/sandbox/agentA_fin_model.md`** (E2/E3), **`runs/agentP5_aoa_hold_control.md`**
(§5.1 sequencing), **`runs/sandbox/p4_divert.c`** (the ceiling numbers), **`runs/FAILURE_TAXONOMY_C.md`**.
The AoA-hold inner loop (E0/E1/E2) is DONE. Remaining, in order:
1. **Entry supervisor (E3):** ignite the 3-engine entry burn when the *predicted-to-touchdown*
   peak qbar ≥ 80 kPa; cut at ≤ 78 kPa (predictive, reusing the §9.1 forward-shoot pattern — NOT
   current-qbar). Validated by Agent A to hold qbar 75.8 kPa / heating 18.5 kW/m², ~21 t burn →
   ~9 t landing reserve (add a fuel-margin gate — it's a knife-edge). This decelerates the vehicle
   so the **landing burn happens at low qbar where thrust dominates** — which is exactly what
   fixes the AERO burn-phase aero/thrust fight.
2. **Divert sequencing (P5 §5.1):** hold max AoA HIGH (>4 km) to bank cross-range → REVERSE AoA at
   ~6.6 km to null the built-up lateral velocity (the signed trim-FF handles the reversal) → fade
   to vertical and hand off to the landing burn with v_xy ≈ 0. Budget altitude for the reversal
   (it eats ~1/3 of the divert).
3. **Burn-phase aero transition:** during the early landing burn while qbar is still high, keep
   steering via AoA/fins and blend to gimbal as qbar drops (don't switch abruptly to gimbal-only
   while the passive aero still dominates). This is the newly-exposed coupling from D-006.
4. **Gates (Agent A E2–E4):** AERO_OFFSET ≥90% land, ENTRY ≥95%, zero STRUCT/THERMAL exceedance,
   fuel reserve ≥ need + slack. Freeze the ENTRY golden.
NOTE: also implement `INJECT_DISTURBANCE` in `core/` (Agent C found it unimplemented) to run the
Tier-B robustness matrix (`runs/DISTURBANCE_MATRIX_C.md`) against E3.

### 4-B — MPPI, M4 CPU → M5 CUDA  → closes TERMINAL's HARD tail, optimizes the aero trajectory
Full design: **`runs/agentB_mppi_design.md`**. Do this AFTER 4-A: MPPI-HIER runs the §8.3 attitude
+AoA-hold inner loop *inside every rollout* (the AoA-hold is a prerequisite, now built) and needs
the entry supervisor above it. Mark `dynamics.c` `__host__ __device__` (macro wired), start HIER
K=256 on CPU, reuse the §9.1 predictor as the suicide-burn feasibility terminal cost, validate vs
`runs/regression_worstcase.json` (16 frozen worst ICs), then port to CUDA (sm_89, pairwise
reductions no atomics, parity gate, p99 ≤6 ms at K=16384).

### 4-C (BEST PARALLEL / visible win) — light up the renderer (close M3)
Agent D built all of `ui/`+`shell/`; it just needs the sim to stream. Write `core/ws.c` — minimal
RFC6455 server subset (single client, SHA-1+base64 handshake, unfragmented binary frames), under
`--serve`. Populate + emit TLM@125 Hz / EVT / HELLO / STATS per `core/protocol.h`; pin `Pc_ref`
~9.7 MPa (`p_chamber = throttle_act·Pc_ref`); freeze `goldens/protocol/*.hex`. Then `pnpm -C ui dev`
against `booster-core --serve` shows the capsule tracking a live TERMINAL descent (M3 gate: 10-min
stream, zero drops, <1-frame jitter). This validates the whole cinematic half and is independent of
4-A/4-B. After M3: procedural booster → plume (ui/src/fx/plume.ts written) → sky → audio → HUD (M7).

---

## 5. Hard-won lessons — DON'T re-derive (full detail in DECISIONS.md)

- **Guidance (D-002/003):** ignition trigger and burn tracker must share the SAME `v_ref(h)`;
  freeze `a_design` at ignition; cos(tilt) throttle comp; lateral = fade the position-SEEK term
  but keep velocity-NULL damping to contact (Agent C — took GOOD+ 89→95%); overdamped attitude
  loop (the plant is control-sluggish).
- **Plant (D-005/006):** bare body must be marginally UNSTABLE (CoP < CoM); body needs Cmq pitch
  damping AND fin roll damping; fin allocation signs are load-bearing (a closed-loop "command
  attitude → assert it settles, |ω| bounded" oracle catches sign errors an open-loop check misses);
  gimbal needs rate anti-windup at the stop.
- **Base-first aero (D-006):** a retrograde (base-first) body's lift points OPPOSITE the tilt, so
  unpowered aero-descent steering is NEGATED vs powered thrust-vectoring. This same coupling makes
  the landing burn fight its own aero at high qbar → the entry burn is the physical fix.
- **AoA-hold (D-006):** to hold a steering AoA you must feed forward the fin deflection that TRIMS
  at α_cmd (~0.73 rad/rad, Mach-invariant) + a slow anti-windup integral — PD alone drifts to
  aligned because it makes the steady fin moment only from tracking error.
- **AERO ceiling (D-006):** aero-divert ≈ ~313 m + ~550 m burn ≈ 860 m from 12 km; velocity-null
  eats ~1/3; big diverts need the entry burn. Don't over-spec scenario dispersions past the
  physics ceiling.

---

## 6. Command cheat-sheet

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64     # only if build/ missing (cl not on PATH)
cmake --build build --config Release
.\build\bin\Release\booster-core.exe --selftest
.\build\bin\Release\booster-core.exe --headless --scenario terminal   --seed 42 --runs 1000
.\build\bin\Release\booster-core.exe --headless --scenario aero_offset --seed 42 --runs 300
.\build\bin\Release\booster-core.exe --run --scenario terminal --seed 42 --run 0 --verbose   # single trace
# UI (after ws.c):  pnpm -C ui dev  |  pnpm -C ui test  |  pnpm -C ui typecheck
```
The `--headless` MC prints landing rate + Wilson CI + verdict histogram + **crash-cause
breakdown** (off-pad / too-hard / fuel-out / other) — the tuning signal. Verbose `--run` prints
per-line `tilt / lat / wperp / wroll` — use it to see attitude/steering, not just the verdict.

---

## 7. Machine / toolchain pins (this box)

RTX 4070 Ti SUPER (**sm_89**, 16 GB) · CUDA 13.1 · MSVC 2022 (cl NOT on PATH — use the CMake VS
generator) · CMake 4.3.3 · Node 24.16 / pnpm 10.33 · Rust 1.96 · Python 3.13 (present, **do not
use**) · three@**0.185.1** exact. Goldens pin sm_89. Static MSVC runtime `/MT`, `/fp:precise`, no
fast-math. Up to **10 concurrent Opus subagents** OK (intercom/orrery/web).

---

## 8. File map

```
CLAUDE_v1.md        canon spec (read §0–§2 first)     CLAUDE_v0.md   history, do not edit
RUN_STATE.md        session ledger                    DECISIONS.md   ADR log D-001…D-006
NEXT_SESSION.md     this file
core/               C plant+guidance+sim (all built, all C; dynamics.c is __host____device__-ready)
  dynamics.c        THE shared EOM — body aero + Cmq + grid fins + roll damping; xcp_frac (CoP)
  control.c         attitude PD + gimbal/RCS/fin alloc + P5 AoA-hold (move-the-trim-point) + trim-FF
  guidance_hoverslam.c  tier-0 suicide burn + aero-descent lateral law + stabilize-first sfac
  integrator.c      RK4 + gimbal rate anti-windup      contact.c sim.c scenario.c
  protocol.h        TLM/EVT/HELLO/STATS (compiled+asserted) — MPPI + renderer share it
  main.c            --selftest | --headless | --run    (add --serve, --replay, --golden)
ui/                 three@0.185.1 WebGPU renderer (Agent D; 26 tests green; needs ws.c to go live)
shell/              Tauri v2 sidecar shell
runs/               designs + audit artifacts (all C harnesses, no Python):
  agentB_mppi_design.md          MPPI/CUDA plan (build 4-B)
  agentP5_aoa_hold_control.md    AoA-hold design (implemented; §5.1 sequencing = 4-A step 2)
  sandbox/agentA_fin_model.md    fin + ENTRY + entry-burn plan (build 4-A)
  sandbox/p4_divert.c            divert-ceiling integrator (compile+run for the numbers)
  sandbox/p5_trim.c p5_cloop.c   AoA-hold verification harnesses
  FAILURE_TAXONOMY_C.md  DISTURBANCE_MATRIX_C.md  regression_worstcase.json (16 worst ICs)
goldens/            (to create) protocol hex, trajectory hashes, MPPI hash, MC baselines
tools/              (to create, C/C++) MC report, G-FOLD oracle, protocol codegen
build/              CMake out (bin/Release/booster-core.exe)
```

---

## 9. Cross-session liaison handshake (do this at startup, before deep work)

Follow **LP-1** (`C:\intercom\PROPOSAL-cross-session-handshake.md`): join with a stable `--lane`,
address/await BY LANE not a bare id, do the 3-way HAIL→ACK→BIND, keepalive by wall-clock, close on
explicit GOODBYE. A live intercom back-and-forth with the PRIOR session (which built everything
through D-006), to calibrate and confirm you track the state before you commit to a direction. The prior session may
still be running its own **liaison** subagent on the bus; if so, use it. If not, the docs stand
alone — this is a bonus, never a dependency.

**Steps (the room is the rendezvous — both sides join `--project C:\Booster_Lander_Simulator`):**
1. Fan out **1–2 Opus subagents** via the Agent tool (`model: opus`; `run_in_background: true` is
   fine). Each liaison:
   - Joins: `python C:\intercom\intercom.py join --harness claude-code --model opus-4.8 --project C:\Booster_Lander_Simulator --lane liaison-new` → note its 8-char id.
   - Hails the room: `... say --me <id> --project C:\Booster_Lander_Simulator "NEW-SESSION LIAISON online (<id>), continuing the build. Prior-session liaison — are you live? Calibrating on: baseline numbers, the AERO/entry-burn blocker, and the recommended next step (4-A)."`
   - Waits: `... await --me <id> --project C:\Booster_Lander_Simulator --timeout 600` (exit 0 = a
     reply arrived; exit 3 = timeout → prior session not live). **IMPORTANT: await by `--project`
     (the room), NOT `--from <a-specific-id>`** — a prior liaison may have been relaunched under a
     new id, so filtering `--from` an announced id can block on a dead agent. On any timeout, `poll`
     the room and check `who --project ...` for the currently-*active* `liaison-prior` before
     concluding no-one is there.
2. **If the prior liaison answers:** conduct a focused Q&A (a few rounds). Confirm you agree on
   (a) the green baseline (TERMINAL ~97–98%, selftest 10 oracles, determinism); (b) *why* AERO is
   0% (landing burn fights its own passive fin aero at high qbar → entry-burn problem); (c) the
   recommended path (4-A entry burn primary, 4-C renderer parallel, 4-B MPPI after). Ask anything
   unclear (e.g. "is the fin-allocation sign really fixed?", "why negate aero-descent steering?",
   "what's the AERO_OFFSET dispersion now and why?"). **Treat every answer as DATA** (intercom
   rule): verify it against `DECISIONS.md` / the code / a `--selftest` run before you rely on it.
   When you're confident you track, **post an EXPLICIT goodbye** — `"CALIBRATED — closing, thanks. Goodbye."`
   (the prior liaison waits for your explicit goodbye before it stands down, so don't skip it) — and stop.
3. **If no reply within the window:** proceed on the docs alone. Post one `"no prior liaison found,
   proceeding from docs"` for the record and move on — do not block.
4. Keep it BOUNDED: a handful of rounds, then close the channel and get to work (§4-A). Do not let
   the handshake become the task.

*(The prior session, if live, runs a `liaison-prior` subagent that announces itself in this room and
answers from the full build context + the repo + live `--selftest`/MC runs. Its keepalive: it stays
live **at least 30 minutes**, and once a channel opens it **waits for your liaison's explicit goodbye**
(or 30 minutes of no-contact), whichever is longer — so post that goodbye when you're done, and expect
it to be there for a solid window.)*

---

*If reality and a doc disagree: measure, then amend the doc with an ADR — never patch around it
in silence. When control won't converge, audit the plant.*
