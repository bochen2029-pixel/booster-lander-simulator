# PLAN — the integrated program as of 2026-09-11

> **What this file is.** A resume-from-cold spec. If the session context is trimmed, read this
> first, then `SCOREBOARD.md` (every controller × every battery on one page), then the tail of
> `DECISIONS.md`. It states where the work stands, what the next phases are, **what has been
> deliberately dropped and why**, and the methodology laws this arc paid for. Update it in the
> same breath as `ROADMAP.md`.

---

## 1 · WHERE IT STANDS — the ladder, one pool, 180 identical faults

Held-out seeds 42/7/99, ENTRY `--engine-out random` ×60 each. **Every row zero-privilege except
the last.**

| controller | landed | PERFECT | mean lat | LOC | s/flight | ADR |
|---|---|---|---|---|---|---|
| `identity` — the old "reactive baseline" | 28/180 (15.6%) | — | — | — | 0.39 | D-047 |
| constant θ, CEM-optimised (10 numbers) | 121/180 (67.2%) | 0 | — | — | 0.39 | D-047 |
| conditional policy (70 params) | 123/180 (68.3%) | — | — | — | 0.39 | D-050 |
| blind search, full budget, periodic replan | 158/180 (87.8%) | 119 | 0.58 m | 13 | 82.6 | D-046 a.3 |
| blind, 1/8 budget, periodic | 166/180 (92.2%) | 24 | 2.6 m | 3 | 5.0 | D-051 |
| **blind, full budget, EVENT replan** | **177/180 (98.3%)** | **144** | **0.41 m** | **1** | 76 | **D-052** |
| **blind, 1/8 budget, EVENT replan** | **175/180 (97.2%)** | 27 | 2.4 m | 1 | **6.3** | **D-054** |
| *clairvoyant search (privileged)* | *180/180* | *147* | *0.33 m* | *0* | *82.6* | *D-046* |

**THE DEPLOYABLE ROW IS D-054**: 97.2% at **6.3 s/flight ≈ 0.5 s/replan** against a **0.1 Hz**
outer loop — real-time by ~20×, **no privilege, no net, no teacher, no distillation.**

**Residual failures are a tail, not a lever.** Full+event: too-hard 1 · fuel-out 1 · LOC 1.
1/8+event: off-pad 2 · too-hard 2 · LOC 1. One or two of each category, no dominant mechanism.
**The controller arc has reached diminishing returns.**

### The decomposition that reorganised everything

- `identity → constant`: **+51.6 pts** from ten numbers. The shipped baseline was never a baseline.
- `constant → blind search`: **+20.6 pts** — the value of adapting θ in flight.
- **`periodic → event replan`: +19 draws.** A stale plan, not blindness.
- `blind → clairvoyant`: **+3 draws (1.7%).** **Foreknowledge is the smallest term by far.**

The three-arc distillation programme chased the smallest quantity in that ladder, against a
**10 µs** bar inherited from the 500 Hz *inner* loop that a mission-layer setpoint never had.

---

## 2 · THE THROUGH-LINE — why the new assets are not decoration

The project's yardstick is **`P(land | in-frontier)`** (canon §9.9). But `in-frontier` has only
ever been computed on **one of three axes**. `runs/sandbox/ceiling_eo_out.txt` says so in its own
words:

> *"this fraction is the **LATERAL-reach ceiling ONLY**. The TRUE ceiling on landed rate is
> min(lateral-in-frontier, attitude-recoverable, terminal-null-achievable)."*

**So the controller has caught up to the measurement apparatus, and both new assets ARE
measurement apparatus:**

- the **FDAI** makes attitude legible (today a LOC failure is a line in a summary);
- the **Apollo kernel** is the instrument for the attitude-recoverable axis, never computed;
- the **CFD STL** validates `dynamics.c`'s frozen CA/CN against real flow rather than its own
  constants — the aero model the whole frontier rests on.

That is why this is one plan and not three.

### Known plant-honesty gaps (all measured, none repaired)

1. **The target is FED, not sensed** — `sim.c:411/:433/:445` write truth with `target_valid=1,
   target_age=0.0`. Consequence, measured in D-053: **5 of 39 observation channels are constant**
   (`COVXX/YY/XY`, `TAGE`, `TVALID`); 7 of 39 dead in total. Every net this arc trained had an
   effective input of **32 dimensions, not 39**.
2. **No actuator or sensor lag, either direction** — `control.c:186-195` applies gimbal the same
   tick; `nav.c` has noise and gyro-bias walk but no transport delay.
3. **No attitude-reference failure mode** — `nav.c` can degrade attitude but never *lose* it. A
   real gimbaled platform can. This is the gap the Apollo asset addresses.

---

## 3 · THE ASSETS (copied to `assets/`, unpacked, nothing executed)

### `assets/kestrel9_gfx/` — near drop-in, no design needed
Two dependency-free ES modules + dev viewer, built from this project's own `CLAUDE_v1.md`
§5/§10.3/§10.7/§11.5-11.6. **THREE is injected, not imported** (no fight with the pinned
`three@0.185.1`). Zero binary assets — every texture drawn on canvas at construction.

**Every TLM field matches `BlTlmFixed` by name**, and the protocol was extended *for it*:
`p_amb /* (plume p_a) */`, `p_chamber /* (plume p_0) *ADDED* */`, `Q_heat /* -> soot state */`.
Reads `throttle_act`, `n_eng`, `gimbal_act[2]`, `fins_act[4]`, `deploy_frac`, `stroke[4]`,
`Q_heat`, `p_amb`, `mach`, `qbar`.

Also ships `export-stl.js`: **CFD variant at 1:1 m, Z-up, origin at the gimbal plane, nozzles
hollow to the throat plane (cap at z = −0.06 m) so a velocity inlet sits there**, for FluidX3D
`read_stl()` + `voxelize_mesh_on_device()`. Vehicle box Ø18 m × 51.3 m.

### `assets/apollo_imu/` — an instrument, and a trap to avoid
Working Apollo Block II IMU: OGA/MGA/IGA, live **MGA margin**, gimbal-lock annunciation,
SOLID/X-RAY/CUTAWAY/FRONT, ALIGN/CAGE, 45–120 Hz.

**THE TRAP — "gimbal" is doing double duty and conflating the two would cost a week:**

| | Apollo IMU | Kestrel-9 |
|---|---|---|
| what gimbals | the **platform** — 3 rings holding a stable member | the **engine** — ±5° thrust vectoring |
| kind | **sensor** | **actuator** |
| failure | MGA→±90°, axes align, **attitude reference lost** | torque exceeds authority |
| in this repo | does not exist | `F_LOC`: `wmag > 2 rad/s` sustained > 3 s (`sim.c:565`) |

**The Apollo IMU does NOT model the lander's LOC.** Ours is a control-*authority* failure; theirs
is a reference-*loss* failure. The resemblance is superficial and inviting.

---

## 4 · THE PHASES

### Phase 0 — close the books (hours, no new capability)
- **0.1 Commit D-052/D-054.** Results on disk, untracked. *(done as part of writing this plan)*
- **0.2 SEAL A FRESH HELD-OUT POOL.** Seeds 42/7/99 have informed a dozen decisions and **seed 42
  was contaminated by my own hand probes** (D-047's warm start was chosen because a box-ceiling θ
  scored 13/60 there). Mint ~10 untouched seeds, record that they are sealed, re-verify the
  headline **once**. **Until that runs, 97.2% is a development number.**
- **0.3 The budget sweep.** Two points exist (1/8 and full) with a surprising result between them.
  Run 1/32 · 1/16 · 1/8 · 1/4 · 1/2 · full on one pool **with event replan on**. Note
  `--rfly-budget` scales POP *and* ITERS together and ITERS floors at 2, so below ~1/5 only POP
  moves — **sweep them separately or the curve is confounded.**

### Phase 1 — the drop-in (a day, zero physics risk)
- **1.1** `assets/kestrel9_gfx/booster/{kestrel9,plume}.js` → `ui/src/scene/`, feed
  `setTelemetry(tlm)` from the decoded frame. Nothing to design.
- **1.2 FDAI overlay** from `quat[4]` (offset 56) + `w[3]` (offset 72). Apollo's *look*, not
  Apollo's mechanics. Pure additive UI — **and immediately the best instrument for watching a LOC
  draw depart**, which is currently invisible.

### Phase 2 — the instruments (where the value is)
- **2.1 The ~60-line kernel, display-only first**: quaternion → OGA/MGA/IGA (a specific rotation
  order), `MGA margin = 90° − |MGA|`, lock predicate. **Not a port** — `gimbal-scene.js` is 53 KB
  of Three.js and stays in JS forever.
- **2.2 Attitude-reference failure in `nav.c`** — **a deliberate decision, because it will LOWER
  97.2%.** That is correct and desirable, but it must not be a surprise.
- **2.3 Extend `ceiling_eo.c` to the attitude-recoverable axis.** The one that changes what every
  number in `SCOREBOARD.md` means.
- **2.4 CFD STL → FluidX3D**, validating the frozen CA/CN in `dynamics.c`.

### Phase 3 — the honest denominator
- **3.1** Restate the frontier as the real `min(lateral, attitude, terminal-null)`.
- **3.2** Re-score the whole ladder against it. **The residual 3–5 draws may not be headroom at
  all — they may be physics, and 177/180 may already be AT the bound.**

---

## 5 · DELIBERATELY DROPPED, with reasons

- **The Qwen-Drive generative / multi-sample direction.** D-053 killed its justification on data
  already on disk: at matched observations the teacher's θ dispersion is **0.295 of global**
  (every component 0.22–0.34), so the observation explains ~91% of θ's variance. **The labels are
  not multimodal.** Flow matching answers multimodal targets; these are not. Interesting
  architecture, **not an indicated fix here**, and I should stop citing it as one.
- **Further constant-θ / conditional-policy work.** D-050 is a null (+2 draws of 180, per-seed
  −1/+3/0), and D-052 made the line moot: **one legal event trigger beat every policy searched
  for, by 54 draws.**
- **Porting the gimbal renderer to C.** Category error.
- **D-050's owed interior-bias re-run.** Legitimately owed (its warm start sat on a box bound, so
  half the weight directions clamped to no-ops, and it never got the continuous-margin reward its
  design specified) — but it would now refine a null on an abandoned line. Low value.
- **The "less search is better" finding.** It was the most interesting thing in the arc and it was
  a **symptom**. The cheap search won only because it could not commit hard to a plan about to go
  stale. Remove the staleness and full budget wins: 177 vs 166, 144 PERFECT vs 24.

---

## 6 · LAWS THIS ARC PAID FOR (binding)

- **THE ATTRIBUTABILITY INVARIANT.** *Every liveness or success signal must be uniquely
  attributable to the thing it claims to be about.* Four false greens in two nights, four
  different layers: a DONE marker written unconditionally (**script**); an argv silently skipped
  leaving a 1000× default (**exe**); a wake-lock line killing its own script (**launcher**); a
  liveness check satisfiable by another job (**watcher**). A clause list invites a fifth instance
  to hide in the layer the clauses do not name.
- **Farm scripts**: verify never assume · hold the box awake · be resumable · keep stderr · **the
  monitor checks the DATA, not the marker**, and alarms on the **age of the newest output**, not
  the existence of a process.
- **Never subtract across pools.** The ladder was cross-pool once and its central claim was
  invalid until re-flown on one instrument.
- **A baseline that has not been optimised under the same budget as the candidate is not a
  baseline.** `identity` was quoted for seven weeks and is 28/180.
- **Read the raw output, not the ADR that summarises it.** The frontier's one-of-three-axes caveat
  was in `ceiling_eo_out.txt` the whole time and the summary had dropped it.
- **Two-sided functional gates.** Off must be byte-identical **and** on must demonstrably differ.
  This caught `noreplan=0` leaving the full CEM running underneath a "constant" policy at 195× the
  cost — a result that still landed and would have looked entirely plausible.

---

## 7 · FIRST MOVE

**Phase 0.2 — seal a fresh pool and re-verify once.** Everything downstream is uninterpretable
while the headline sits on a burned pool; that is the same discipline that caught the cross-pool
ladder error. Phase 1 can run in parallel — it touches no physics.
