# TARGET SANDBOX DESIGN — the movable landing target (deterministic deck/drift + fenced live drag)

**Author:** TARGETDESIGN (Opus 4.8) · **Date:** 2026-07-18 (night, post-D-016 / continuity) · **Status:** design + static-analysis of the offset substitution, ready to implement
**Operator ask:** the operator can move the landing target — a pad, or a heaving ASDS deck — and *watch the guidance re-solve* to land as close to it as possible. This is the single best way to *see* the project's "the guidance actually solves it in real time" thesis.
**Builds on:** §4.4 (SEA module — the canonical moving target, designed-not-wired), §8.1 (Deck pose is part of NavState), §11.9 (HELLO-exported spectrum), D-010 item 1 + D-011 §5 (the pure-observer HARD LINE), D-013 (protocol v3 `pred_impact`/`ignite_h`), the D-012 divert law, the D-016 ENTRY-under-MPPI result.
**Canon it must respect:** §4.3 (guidance never reads `v_wind` — the analog: what may guidance legally know about the *target*?), directive 2 (determinism sacred), directive 5 (the renderer is a pure observer; precompute in, telemetry out; NEVER a runtime loop from the pretty half into dynamics), directive 7 (one dynamics source), directive 9 (TERMINAL byte-identical).

> **DESIGN + ANALYSIS ONLY. Nothing in `core/` was edited; no cmake/binary was run against the real tree.** Every code injection below is specified by **file + function + line-neighborhood** so a build agent implements without re-deriving. The offset-substitution claim (§B) is established by a **complete static trace** of the two guidance laws (cited line-by-line); a read-only `_target_wt/` prototype is specified in §E-Stage-0 as the *byte-equality* proof, not needed to establish the trace.

---

## 0. Executive verdict

**Movable target is a ONE-CONCEPT change with TWO cleanly separated delivery modes, and the whole design lives or dies on keeping them on opposite sides of the determinism/observer line.**

1. **The math is a target substitution, and for the reactive law it is literally ONE line.** Every lateral law in the tree — hoverslam's divert AND the MPPI cost — is already written as a function of `r_xy = (y[S_RX], y[S_RY])`, the vehicle's offset *from a pad pinned at the world origin* (`core/scenario.c:55-58`; there is **no explicit pad coordinate** — the pad IS `(0,0)`). Nulling `(r_xy − target_xy)` instead of `r_xy` is, in `guidance_hoverslam.c`, a single substitution at line 84 that the entire downstream law (`r_mag`, `v_rad`, `r_pred`, `vdes`, the D-012 overspeed brake) inherits unchanged. In `guidance_mppi.c` it is **not** one line — the offset is read at ~8 sites woven through the cost — but every site means the identical thing, so the clean implementation threads one `target_xy[2]` into both and subtracts it at each read (§B).

2. **MODE 1 — DETERMINISTIC MOVING TARGET is the canonical version and betrays nothing.** The target moves per a **seeded, closed-form trajectory** — the SEA-module ASDS deck (§4.4) on a seeded Pierson-Moskowitz sea-state, or a scripted/seeded pad drift — computed *inside the plant* exactly the way `wind_sample` computes the wind each step (`sim.c:31-57`, `sim.c:329`). It is fully bit-deterministic (seeded, no runtime input), so it can have goldens, run in `--headless`, and replay byte-identical. Guidance solves for the moving deck exactly as it solves for wind-disturbed descent. **Guidance may legally know the target's CURRENT pose** (canon §8.1: "Deck pose (ASDS) is part of NavState" — like a surveyed/transmitted position). Whether guidance may know the *future* deck motion is a genuine design choice with the §11.9 HELLO-spectrum precedent; **I recommend guidance-blind-to-future by default** (§A.4), with the transmitted-spectrum variant offered as an explicitly-labeled second experiment.

3. **MODE 2 — LIVE INTERACTIVE DRAG is a fenced sandbox with determinism DELIBERATELY WAIVED.** The operator drags the target in the browser; it feeds the plant as a live input. This requires an **inbound channel that `--serve` does not have today** — and this is the single most delicate point in the design. The good news: `core/ws.c` *already receives and unmasks* client→server frames (it must, for PING/PONG/CLOSE — `ws.c:262-285`); it just deliberately *drops the payload* of data frames (`ws.c:282`, with the load-bearing comment "Data frames (which a pure-observer renderer must not send) are consumed and ignored"). So the inbound surface is tiny: parse a fixed 8-byte payload the operator sets, and write it to a target slot. **The fence:** this is a SEPARATE, explicitly-labeled binary mode (`--serve-interactive`) that is NOT the deterministic gate and says *"I am a toy, not the oracle."* It cannot produce goldens; it must be visibly quarantined from every honesty claim. Only a **scalar target position** crosses the line — never renderer *state*, never a loop from the pretty half into dynamics.

4. **The money shot is already 90% wired.** `pred_impact[2]` (protocol offset 220, streaming since D-013) is the guidance's predicted touchdown. When the operator moves the target, `pred_impact` swings off, then re-converges onto the new target as the solve tightens (§C). `dist_pad` (offset 216) already streams the live miss. The renderer draws target + `pred_impact` + the vehicle; the visible beat is the marker chasing the target home. The **one** honest protocol addition both modes want is a `target_xy[2]` field so the renderer draws the target from the wire instead of assuming origin — and `BL_EVT_TARGET_CHANGED` **already exists** in the EVT enum (`protocol.h:226`), reserved for exactly this.

**Recommended build order (each stage independently gated):** Stage 0 = static pad at nonzero offset, prove the substitution is byte-clean vs today when `target=origin` (TERMINAL 194 / ENTRY-mppi 95 / AERO 44|60 must reproduce). Stage 1 = deterministic seeded pad drift + SEA deck in `--headless`, gate-clean, goldens. Stage 2 = the fenced interactive `--serve-interactive` channel, the observer-doctrine-sensitive one, smallest possible inbound surface. **Ship Stage 0+1 into the honest tree; keep Stage 2 quarantined.**

---

## 1. The plant today: where the pad lives, and why "move the target" is a coordinate question

Read directly from `core/scenario.c`, `core/state.h`, `core/sim.c`.

### 1.1 The pad is the world origin — there is no pad coordinate

`state.h:9-23`: the continuous state carries `S_RX,S_RY,S_RZ` = **position in the world frame** (canon §4.1: "origin at the primary pad center"). There is no `pad_x`/`pad_y` anywhere in the tree.

`scenario.c:34-58` initializes the OFFSET scenarios by **displacing the vehicle from the origin**, not by moving a pad:
```c
double lat = d->lateral;                       // AERO_OFFSET mean 500, ENTRY mean 3000 (DEFS[], scenario.c:16-20)
double lx = lat + disp(seed,run_idx,2, lat_sigma);
double ly = disp(seed,run_idx,3, lat_sigma);
st->y[S_RX]=lx; st->y[S_RY]=ly; st->y[S_RZ]=h0 + mp.com;   // scenario.c:56-58
```
So today, **`r_xy` (the vehicle position) IS the pad-relative offset**, because the pad sits at `(0,0)`. `AERO_OFFSET` = mean 500 / σ150 (well-posed: `D_phys≈1107 m`, `runs/sandbox/ceiling.c` — the note at `scenario.c:50-53`); `ENTRY` = mean 3000 / σ250. The two are numerically identical *only because the pad is at the origin*. **This is the fact "move the target" turns on:** the moment the target is not at the origin, "position" and "offset-from-target" diverge, and the guidance cares about the *offset*, the plant cares about *inertial position*, and the contact/verdict care about *position-relative-to-the-moving-deck*.

### 1.2 There is already a vertical target scalar: `deck_z`

`scenario.h:9-14` — `ScenarioEnv` carries `double deck_z; /* ground/deck height (0 RTLS) */`, set from the per-scenario table (`scenario.c:81`, all scenarios currently `0`). It is already threaded into:
- **contact:** `contact_substep(st,&s->act,&s->env,s->se.deck_z,DT)` (`sim.c:340`) → `contact_wrench(st, deck_z, ...)` measures penetration `pen = deck_z - fz` (`contact.c:44`).
- **touchdown capture:** `lo_after<=s->se.deck_z+1e-3 && lo_before>s->se.deck_z` (`sim.c:350`).
- **slant distance:** `dz=st->y[S_RZ]-s->se.deck_z` (`main.c:383`).

**So the vertical target level is already a plant input; it is just a static scalar today.** A *heaving* deck makes `deck_z` a time-varying `deck_z(t)`. Crucially, **guidance does NOT read `deck_z`**: `hoverslam_step` uses `h_base = y[S_RZ]-mp.com` (`guidance_hoverslam.c:82`) — height above `z=0`, not above the deck. That is the vertical analog of the horizontal problem, and it is part of the SEA-module wiring (§A.2).

### 1.3 The wind pattern IS the template for a target-as-plant-input

`sim.c:31-57` `wind_sample(Sim* s, double h, double out[3])` computes the world wind each physics step from a **seeded closed form** (mean profile × altitude scaling + Dryden AR(1) from the `RNG_WIND` Philox stream), and `sim.c:329` writes it to `s->env.wind_world` for dynamics to read. **A seeded moving target follows this pattern exactly:** a `target_sample(s, t, out_xy, &out_deck_z)` writing a target slot each step, deterministic and replayable, read by guidance from its nav view. This is the directive-2-clean way to make the target move — the same discipline that makes wind replayable makes the moving target replayable.

---

## 2. The determinism / observer doctrine — the crux this design must respect

This is the line the whole design is organized around. Stated from the source:

- **Directive 5 / D-011 §5 (the HARD LINE, canon-grade):** *"CFD and UE must NEVER close a runtime loop into dynamics. The moment the pretty half feeds state back, determinism, the memcmp oracle, and the anti-cheat thesis die. Precompute in, telemetry out, always."* (`DECISIONS.md:645-648`).
- **D-011 addendum:** *"the protocol is the real coupling point — renderers are disposable, the contract is not."* (`DECISIONS.md:672-674`).
- **The code already encodes the one-way rule:** `ws.c:250-252` — *"Data frames (which a pure-observer renderer must not send) are consumed and ignored."* `main.c:317-322` — `--serve` *"reads sim state, serializes, streams... The sim/integration path is NOT touched — no RNG, no state writes."*
- **What guidance may legally know (the §4.3 analog):** canon §4.3 says guidance never reads `v_wind` — it *feels* it through state. Canon §4.4 + §8.1 say the OPPOSITE about the deck: *"Guidance receives deck pose in its nav state (the real system knows the deck)"* / *"Deck pose (ASDS) is part of NavState."* (`CLAUDE_v1.md:233, 482-483`). **This is the decisive doctrinal fact:** the target's *current position* is a KNOWN quantity (surveyed pad, transmitted deck telemetry), legal for guidance to consume — unlike the wind, which is a hidden disturbance. The target's *future* motion is the genuinely open question (§A.4).

**The design's two modes map cleanly onto the two sides of this line:**

| | MODE 1 (deterministic) | MODE 2 (interactive) |
|---|---|---|
| target source | seeded closed form in the plant | operator drag over the wire |
| determinism | **bit-exact, replayable** | **deliberately waived** |
| goldens | **yes** | **no — quarantined** |
| runs in | `--headless`, `--run`, `--serve` | `--serve-interactive` only |
| what crosses the boundary | nothing (target is internal) | a **scalar target position** (a plant input, like wind) |
| honesty claim | full oracle | "toy, not oracle" |

The interactive mode does NOT violate the hard line **because the target is a PLANT INPUT** (a scalar the operator sets, exactly like an operator-commanded `DISTURB` gust in canon §4.3.3), and **guidance reads only the target's current position** (the §4.3/§8.1-legal quantity). No renderer *state* ever re-enters dynamics — the renderer sends one 2-vector, not its camera, not its particle system, not a physics result. The loop that the hard line forbids is *renderer→dynamics of derived state*; an operator setting a target coordinate is *input→plant*, the same category as a joystick.

---

## A. THE TWO MODES

### MODE 1 — DETERMINISTIC MOVING TARGET (gate-clean, replayable) — THE CANONICAL VERSION

The target moves per a seeded, closed-form trajectory. Two flavors, same plumbing:

- **(1a) the SEA-module ASDS deck** — a heaving/pitching/rolling droneship deck on a seeded sea-state. This is the canon-designated moving target (§4.4), designed-but-never-wired. The horizontal target is the deck's slow station-keeping wander (±3 m, §4.4); the vertical target is `deck_z(t)` heave (±1.5 m); the deck attitude is `deck_quat(t)` (pitch/roll ±2.5°).
- **(1b) a scripted/seeded pad drift** — a purely horizontal target that walks a seeded path (or a scripted ramp / step), no vertical/attitude motion. Simpler than the deck; the cleanest first moving-target (Stage 1). Useful precisely because it isolates the *horizontal* re-solve without the contact-frame complications of a heaving surface.

Both are fully bit-deterministic (seeded, no runtime input) → goldens, `--headless`, byte-identical replay. Guidance solves for the moving deck exactly as it solves for wind-disturbed descent.

#### A.1 The SEA-module interface (deterministic heave/pitch/roll spectrum → deck pose)

Canon Appendix A.3 pins the spectrum: **Pierson-Moskowitz `S(ω)=α g²/ω⁵·exp(−β(ω0/ω)⁴)`, α=8.1e-3, β=0.74, ω0=g/U19.5; 48 components, equal-energy binning, seeded phases; deck heave/pitch/roll RAO gains tuned to §4.4 amplitudes; deterministic sum-of-sines; table exported in HELLO.** (`CLAUDE_v1.md:1053-1057`).

**New module `core/sea.{h,c}` (canon §3 layout already reserves it: `sea.{h,c} [module SEA] deck motion spectrum (§4.4)`, `CLAUDE_v1.md:150`).** Interface:
```c
// sea.h
typedef struct {
    double omega[SEA_N];   // 48 component frequencies [rad/s]  (equal-energy binned from P-M)
    double amp[SEA_N];     // 48 heave amplitudes [m] (RAO-scaled; pitch/roll are fixed fractions)
    double phase[SEA_N];   // 48 seeded phases [rad]
    double x0, y0;         // mean deck station (world) [m]  — the horizontal target center
    double wander_omega[2], wander_amp[2], wander_phase[2];  // slow ±3 m station-keeping drift
} SeaState;
void sea_init(SeaState* S, uint32_t seed, double Hs);   // build the 48-component table from (seed, Hs)
// Pure, stateless-in-time deck pose at absolute sim time t (NO integration — closed-form sum):
void sea_deck_pose(const SeaState* S, double t,
                   double* deck_z,        // heave [m]
                   double  deck_quat[4],  // attitude xyzw (small pitch/roll from the same spectrum)
                   double* target_x, double* target_y);  // horizontal deck station (world) [m]
```
`sea_deck_pose` is a **pure function of `(SeaState, t)`** — `deck_z(t)=Σ amp[k]·cos(omega[k]·t + phase[k])`, pitch/roll analogously with phase offsets, `target_xy` = `(x0,y0)` + the slow wander sum. **No state integrated over time** → identical `(seed, Hs, t)` gives identical pose → replay-safe by construction, exactly like the wind's mean profile (the Dryden AR(1) part is stateful; the *deck* is a pure sum-of-sines, so it is even cleaner than the wind — no filter memory to carry). The table is built once in `sea_init` and exported in HELLO so the renderer's Gerstner/FFT ocean phase-locks to it (§11.9: "one source of truth").

**Where it lives:** `SeaState` in the `Sim` struct (sibling to the wind bookkeeping), built in `sim_init` when `MOD_SEA` is set, evaluated each step. Zero-cost when SEA is off (the `ASDS_NIGHT` scenario or a new `--sea` flag arms it; `TERMINAL`/`AERO`/`ENTRY` never touch it → byte-identical, directive 9).

#### A.2 How the contact surface moves in the PLANT (deck motion lives in dynamics, not a renderer trick)

This is the directive-5 heart of Mode 1: **the deck moves in the DYNAMICS, and the renderer merely draws where the plant already put it.** Concretely:

1. **Vertical (heave):** replace the static `s->se.deck_z` at its three consumers with the live `deck_z(t)`:
   - `sim.c:340` — `contact_substep(st,&s->act,&s->env, deck_z_t, DT)` where `deck_z_t = sea_deck_pose(...).deck_z` for the current step (SEA on) else `s->se.deck_z`.
   - `sim.c:350` — the touchdown-capture threshold uses `deck_z_t`.
   - `contact_wrench` already takes `deck_z` as a parameter (`contact.c:32,44`) — **no change to the contact solver signature**, only the value passed.
2. **Deck-relative closing velocity (leg loads — the honest coupling):** `contact_wrench` computes foot velocity `fv` from the vehicle's *inertial* velocity (`contact.c:47-48`, `vz=fv[2]`). On a *heaving* deck, the physically correct penetration rate and leg damping use the **relative** closing velocity `vz − deck_vz(t)`. `deck_vz(t) = d/dt deck_z = −Σ amp[k]·omega[k]·sin(...)` is a closed-form derivative from the same table (add it to `sea_deck_pose` as an out-param). **Injection:** pass `deck_vz` into `contact_wrench` alongside `deck_z` and subtract it from `fv[2]` before the spring-damper (`contact.c:48,60`). This is the SEA↔contact coupling canon §4.4 names ("the contact solver works in the deck frame") — a real touchdown on a rising deck loads the legs *harder* (closing faster) and on a falling deck *softer*; leg crush and tipover become sea-phase-dependent, which is exactly the physics the demo should show. (Deck pitch/roll also tilt the contact normal; for Stage-1 scope keep the normal vertical and defer the tilted-normal contact to a SEA-polish follow-up — flagged in §F.)
3. **Horizontal (station + wander):** the deck's `target_xy(t)` is NOT a contact-solver input (the legs contact wherever they are in world XY; the pad *rectangle* moves under them). It enters TWO places: (a) **guidance** reads it as the offset target (§B); (b) the **verdict's on-pad test** must become target-relative (§A.3, `sim.c:88,92`).

**Nothing here is a renderer trick:** the deck's heave changes the contact geometry the integrator sees, the deck's motion changes leg loads, and the renderer's job (§11.9) is only to draw the ocean/deck at the pose the plant computed and streamed. The pretty half never tells the plant where the deck is.

#### A.3 What the verdict must know (the on-pad test becomes target-relative)

`sim.c:86-105` `set_verdict` and the touchdown capture (`sim.c:354`) compute `lat = sqrt(y[S_RX]²+y[S_RY]²)` — distance from the origin. With a moving target this must be **distance from the target**:
```c
double dx = st->y[S_RX] - target_x_at_touchdown;   // sim.c:88, 92, 354
double dy = st->y[S_RY] - target_y_at_touchdown;
double lat = sqrt(dx*dx + dy*dy);
int on_pad = (lat <= PAD_RADIUS);                  // sim.c:92
```
**Subtlety (freeze the target at touchdown):** the verdict is graded on the target position *at the moment of contact* — latch `target_xy` when `s->touched` first fires (`sim.c:350-356`), so a deck that drifts on *after* the vehicle is down doesn't retroactively change the grade. This is physically right (you land on the deck where it was when you touched) and keeps the verdict a clean function of the contact event. `res->td_lat` (`sim.c:397`) becomes the target-relative miss — which is exactly the number the demo wants ("landed X m from the pad").

#### A.4 What guidance may legally know: current pose YES, future motion is a design choice

**Current deck pose is legal** — canon §8.1 is explicit ("Deck pose (ASDS) is part of NavState"). Concretely, guidance reads `target_xy(t_now)` and `deck_z(t_now)` from its **nav view** (§B injection). This is like a surveyed pad or a droneship transmitting its instantaneous GPS/IMU — a real flight computer has this.

**Future deck motion — the genuinely open question, argued both ways:**

- **Option (i): guidance blind to the future (RECOMMENDED DEFAULT).** Guidance knows only `target_xy(t_now)`, `deck_z(t_now)`; it re-aims every 50 Hz tick at wherever the deck *is now*. It nulls `(r_xy − target_xy(t_now))`. On a slowly-wandering deck (±3 m, periods 6-10 s) and small heave, chasing the current pose is the honest, robust behavior — the deck moves slowly relative to the divert authority, so "aim at now" converges. **This is the anti-cheat-clean choice:** guidance gets no oracle knowledge of where the deck *will be*, it just tracks, exactly like the D-016 MPPI tracks a wind-disturbed descent by replanning on the fresh state. It is also the strongest demo: the operator sees the marker *hunt* the moving deck, visibly re-solving.
- **Option (ii): guidance gets the transmitted spectrum (the §11.9 HELLO-precedent variant).** Canon §11.9 already exports the SEA sinusoid table in HELLO ("the *rendered* swell phase-locks to the *simulated* deck motion; same sinusoid table from HELLO"). An ASDS can *transmit its predicted heave* — real droneships publish motion forecasts. Under this variant, guidance receives the 48-component table and predicts `deck_z(t_touchdown)`, aiming at where the deck *will be* at contact. This is legal-by-precedent (the spectrum is a transmitted quantity, not a hidden state — the §4.3 line is about *hidden* disturbances) and it improves terminal heave-matching (touch at a heave crest, softer relative velocity). **But** it is a distinct anti-cheat posture: guidance now has *future* information, which is a stronger claim than "it solves the current state." It must be a SEPARATE, explicitly-labeled experiment with its own ADR, never the default.

**RECOMMENDATION: ship Option (i) as the canonical Mode-1 behavior.** It is the cleanest demonstration of the thesis (re-solve on current state, no future oracle) and the safest anti-cheat posture. Offer Option (ii) as a labeled follow-up (`--sea-forecast`) with its own golden and ADR — it is a legitimate, interesting variant (heave-phase-matched touchdown is real F9-on-ASDS physics), but it is a *choice about what the vehicle is allowed to know*, and that choice must be explicit, not smuggled in. **The pred_impact marker (§C) makes the difference visible:** under (i) the marker tracks the current deck and jitters as the deck moves; under (ii) it locks onto the predicted touchdown point and is steadier — the demo can literally show both.

---

### MODE 2 — LIVE INTERACTIVE DRAG (fenced sandbox, determinism DELIBERATELY WAIVED)

The operator drags the target in the browser; it feeds the plant as a live input. This is the mode that makes the thesis *visceral* — a human moves the pad and watches the guidance chase it — and it is the one place the design touches the observer doctrine, so it is fenced hard.

#### M2.0 Why `--serve` cannot do this today, and why the inbound surface is nonetheless tiny

`--serve` is **one-way by construction** (`main.c:317-322`). But the RFC6455 machinery to *receive* a client message already exists: `ws_poll_client` (`ws.c:245-287`) reads client→server frames every telemetry tick (`main.c:538`), and it **already unmasks the payload in place** (`ws.c:275-278`, `pay[]`). The only thing it does with a data frame's payload is drop it:
```c
if(op==0x8){ /* CLOSE */ ... }
else if(op==0x9){ /* PING */ send_pong(...); }
/* op==0xA PONG, 0x1/0x2 data, continuations: ignore (observer sink) */   // ws.c:282
```
**So the inbound channel for Mode 2 is: parse a fixed 8-byte payload from the already-unmasked `pay[]` of a binary data frame, and store it.** The unmask is done; the frame-walk is done; only the *parse + store* is new, and it is ~10 lines. This is the smallest possible inbound surface — which is exactly what an observer-doctrine-sensitive change should be.

#### M2.1 The fence — a SEPARATE, explicitly-labeled mode that is NOT the gate

**Add a distinct CLI mode `--serve-interactive` (a sibling of `cmd_serve`, `main.c:450`), NOT a flag on `--serve`.** Rationale:
- A distinct mode name is a **visible label**: it says "I am a toy, not the oracle." Nobody freezes a golden from `--serve-interactive`; nobody quotes its landings in an honesty table. The name is the quarantine.
- `--serve` stays byte-for-byte the pure observer it is today (its `ws_poll_client` keeps dropping data frames). The deterministic MC gate (`--headless`, `--run`, `--selftest`) is **untouched** — it has no WS server at all, so it *cannot* receive an operator input; it stays seeded/pure/replayable.
- `--serve-interactive` prints a loud banner at startup: `INTERACTIVE SANDBOX — determinism WAIVED, not a gate, no goldens.`

**What crosses the line, and why it is safe when fenced:**
1. The operator sends a target position `(target_x, target_y)` — optionally a target *velocity* too for a dragged pad (§C makes the drag smooth). That is **a plant input, exactly like the operator-commanded `DISTURB` gust** in canon §4.3.3 — a human perturbing the world, not the renderer feeding back derived state.
2. Guidance reads **only the target's current position** from its nav view (§B) — the canon §8.1-legal quantity. It does not read the renderer's camera, particles, or any computed graphics state.
3. **No renderer STATE ever re-enters dynamics.** The single 2-vector (or 4-vector with velocity) the operator sets is the *entire* inbound payload. The renderer computes nothing that flows back — it draws telemetry out, and passes an operator *intent* in. The forbidden loop (D-011 §5: "the pretty half feeds state back") does not exist here; there is no derived-state feedback, only human input.
4. **It cannot produce goldens and must be visibly quarantined from the honesty claims.** The mode is non-deterministic *by design* (the trajectory depends on when/where a human dragged), so `--selftest`'s memcmp oracle is meaningless for it and never applied. This is stated in the ADR and the startup banner.

#### M2.2 The inbound message + storage (exact injection)

**Message format (client→server, one binary WS frame):** an 8- or 16-byte little-endian payload:
```c
// interactive target-set message (client -> server). Little-endian, matches protocol.h discipline.
struct BlTargetSet {
    uint32_t magic;     // 'TGT0' — reject anything else (an observer that shouldn't send data is ignored)
    float target_x;     // world X [m]
    float target_y;     // world Y [m]
    // optional (16-byte variant): float target_vx, target_vy for a smooth dragged pad
};
```
**Storage (the target slot):** a new field in the `Sim` struct — `double target_xy[2]; double target_vxy[2]; int target_active;` — mutated ONLY by the interactive poll, read by guidance. In the deterministic modes this slot is set once from the scenario/SEA and never touched by any input path.

**Plumbing:**
- `ws.c`: `ws_poll_client` gains an out-param or a callback so the parsed `BlTargetSet` reaches the caller (keep `ws.c` free of sim types — return the raw 8/16 bytes + a "got target" flag; `main.c` interprets). The `op==0x1/0x2` branch (`ws.c:282`) becomes, *only in the interactive build path*, "copy `pay[]` into the out-buffer, set the flag." Guard it so the plain `--serve` behavior (drop) is preserved — the simplest split is a `ws_set_inbound_sink(buf, &flag)` that `cmd_serve` never calls and `cmd_serve_interactive` does.
- `main.c cmd_serve_interactive`: after `ws_poll_client` (mirroring `main.c:538`), if the inbound flag fired, validate magic and write `s.target_xy` / `s.target_vxy` / `s.target_active=1`, and `emit_evt(&s, BL_EVT_TARGET_CHANGED, target_x, target_y)` so the client (and any recording observer) sees the change on the reliable EVT channel (`protocol.h:226` — the code already exists).
- The target then flows to guidance via the same nav-view injection as Mode 1 (§B) — **the guidance code is identical between the two modes**; only the *source* of `target_xy` differs (seeded closed form vs operator poll). That symmetry is the design's cleanliness: one guidance change serves both modes.

#### M2.3 Determinism boundary — stated precisely

- **The plant is still deterministic *given the input trace*.** If you record the sequence of `(step, target_xy)` operator sets, replaying that trace reproduces the run bit-exact (the plant has no other nondeterminism — wall-clock is pacing-only, `main.c:472,540-549`). This is worth noting: the interactive mode is *not* internally random; it is deterministic-modulo-the-human. A build agent MAY add an optional input-trace recorder (write `(step,x,y)` to a CSV) so an interactive session can be *replayed deterministically in `--headless`* — which promotes an interesting operator run back across the line into a golden-able artifact. **That replay path IS gate-clean** (it reads a recorded trace, no live input), and is the honest bridge between the two modes: play in the sandbox, capture the trace, replay it deterministically. (Recommended as a Stage-2 polish, not required.)
- **No golden is frozen from a live interactive session.** Ever. The ADR says so.

---

## B. THE MATH — the minimal guidance change to null `(r_xy − target_xy)`

**Claim:** in the reactive law it is a single offset substitution at the top of the divert law; in MPPI it is the same concept applied at each read of the offset. Established by a complete static trace below.

### B.1 Hoverslam — ONE substitution (directive-7-clean)

`guidance_hoverslam.c:78-255`. The offset enters at exactly ONE point:
```c
double r_xy[2]={y[S_RX],y[S_RY]};    // hoverslam_step, line 84  <-- THE substitution point
```
**Every downstream use of the offset reads `r_xy`**, never `y[S_RX]`/`y[S_RY]` again:
- `r_mag = sqrt(r_xy[0]²+r_xy[1]²)` (line 132)
- `v_rad_cur = (v_xy·r_xy)/r_mag` (line 147) — radial velocity along the offset
- `r_pred = r_mag + v_rad_cur*T_LEAD` (line 148) → `vdes_mag = sqrt(2·A_DECEL·r_pred)` (line 149)
- the D-012 overspeed brake `os = (vxy_mag − vdes_mag)/KDIV_VBLEND` (line 168) — already a function of `vdes_mag`, hence of `r_xy`
- `vdes[0]=-vdes_mag*r_xy[0]/r_mag` (line 176), and the final `a_lat = Kvel*vdes*lat_scale − Kvd*v_xy` (lines 188-189, 233-234)

**The substitution:**
```c
double r_xy[2]={ y[S_RX] - g->target_xy[0], y[S_RY] - g->target_xy[1] };   // line 84, movable target
```
where `target_xy` arrives via `GuidanceCmd` (see B.3). The entire law — profile, lead, brake, height-split, damp-through-ignition — inherits the shifted offset with **no other edit**. This is directive-7-clean because `hoverslam_step` stays a pure function of `(state, GuidanceCmd)`; the MPPI rollout that calls `hoverslam_step` via `mppi_execute` (`guidance_mppi.c:796`) passes the same `target_xy` through, so the leak-path stays consistent.

**One caveat — the velocity-null term.** `a_lat` subtracts `Kvd*v_xy` (inertial velocity), NOT `Kvd*(v_xy − target_vxy)`. For a **static or slow** target (SEA wander ±3 m over 6-10 s; a slowly-dragged pad) this is correct — you want to arrive with zero *inertial* horizontal velocity (the deck is nearly stationary; matching its ~0 velocity == nulling your own). For a **fast-dragged** interactive target or a fast pad drift, the vehicle should null velocity *relative to the target*: subtract `Kvd*(v_xy − target_vxy)` and lead the aim with `target_vxy`. **Recommendation:** default to nulling inertial velocity (correct for the slow canonical cases and the safest); expose `target_vxy` only in the interactive mode and only if a build agent finds the drag "outruns" the vehicle unnaturally. This is a one-line extension at lines 188-189 (`- Kvd*(v_xy[0] - g->target_vxy[0])`), gated to non-zero `target_vxy`.

### B.2 MPPI — the same concept at each offset read (NOT one line, but mechanical)

`guidance_mppi.c` reads the offset as raw `rst.y[S_RX]/rst.y[S_RY]` at ~8 sites, each meaning "offset from pad-at-origin":
- `converging_vdes(y[S_RX],y[S_RY],...)` — the profile — at **4 call sites**: `cmd_from_u_lean` (line 329), rollout running cost (line 413), rollout terminal (line 482), `warm_start_nominal` (line 564).
- running-cost position terms: `rx=rst.y[S_RX]` → `rxy2=rx*rx+ry*ry` (lines 402, 406, 421 `Q_RXY`).
- the D-009 gate: `gr2=(rst.y[S_RX]²+rst.y[S_RY]²)/R_REF²` (line 376).
- the ZEM foresight: `zx=rx+vx*t_ign` (lines 503, 509) — the projected touchdown offset.
- the touchdown terminal: `phi = TD_RXY*(rxy2/R_REF²) + ...`, `off_pad=(rxy2>PAD_RADIUS²)`, the linear pull `40*sqrt(rxy2)/R_REF` (lines 454, 457, 462).
- the in-air terminal position pull (lines 476, 488, 513-514).

**The clean implementation: thread `target_xy` into `MppiState` (set per replan from the nav view) and define the offset once at each read.** Two equivalent tactics:
- **(preferred) a local offset at the top of each cost/profile block:** wherever the code currently forms `rx=rst.y[S_RX], ry=rst.y[S_RY]` for a *position* purpose, write `rx=rst.y[S_RX]-M->target_xy[0], ry=rst.y[S_RY]-M->target_xy[1]`. For the 4 `converging_vdes` calls, pass the shifted values. This is ~8 two-line edits, all mechanical, all meaning the identical thing. The velocity terms (`vx,vy`) are **unchanged** (same slow-target argument as B.1; add `target_vxy` only if the interactive fast-drag needs it).
- **(rejected as unclean) shifting the copied state `rst.y[S_RX] -= target_x`** at the top of each rollout. Tempting (it would make the reads "just work") but it corrupts the state the *plant EOM* integrates inside the rollout (`rk4_step`, line 399) — the dynamics are in the world frame and must see true world position (gravity `g(h)`, atmosphere, the ground-crossing test `lo<=deck_z` at line 447 all need true `S_RZ`, and a shifted `S_RX/RY` would desync the contact/ground logic). **Do NOT shift the integrated state.** Shift only at the *cost/profile reads*. This mirrors exactly why B.1's caveat keeps `v_xy` inertial: the plant is world-frame; only the *guidance objective* is target-relative.

**Directive-7 parity:** the D-012 overspeed brake mirror in `cmd_from_u_lean` (lines 327-334) calls `converging_vdes(y[S_RX],y[S_RY],...)` — it gets the same `target_xy` shift as everywhere else, so the rollout brake and the execution brake stay profile-exact (the parity the D-012 header-share exists to protect). The single-run MPPI invariance check (§E) with `target_xy=0` proves no leak.

### B.3 Plumbing the target into guidance (the nav-view path — canon §8.1)

**`target_xy` (and `deck_z(t)`, `target_vxy`) must reach guidance through the nav view**, because §8.1 says guidance consumes NavState and "Deck pose is part of NavState." Two clean options:

- **(preferred) add `double target_xy[2]` (+ optional `target_vxy[2]`) to `GuidanceCmd`** (`guidance.h:6-16`), mirroring how `a_lat` already flows. `sim.c` fills it on each guidance tick from `s->target_xy` (the seeded-or-operator-set slot), for BOTH GM_HOVERSLAM and GM_MPPI. `hoverslam_step` reads `g->target_xy` (B.1); MPPI copies it into `MppiState` at `mppi_step` entry (B.2). Zeroed by default → TERMINAL/AERO/ENTRY without a target behave byte-identically (directive 9). **Cost: one struct field, filled in one place (`sim.c`), read in the two guidance laws.**
- **(alternative) add it to the `State`/`NavState`** so it rides the nav measurement layer literally. This is more faithful to §8.1 ("part of NavState") and would let `--nav-noisy` perturb the *sensed* deck pose (realistic: a droneship's transmitted position has error). But it enlarges the state vector / nav contract (the NAV_TRUTH bit-transparency gate, `nav.c`) and is more surgery. **Recommendation: `GuidanceCmd` for Stage 0/1 (cheapest, cleanest), promote to NavState only if/when you want nav-noise on the deck pose** (a nice honesty touch for a later stage — a droneship you can't survey perfectly).

**Where the slot is set (`sim.c`, mirroring `wind_sample`):**
```c
// in sim_step, on a guidance tick (or every step for the contact deck_z), before guidance:
if (s->modules & MOD_SEA) {
    double dz, dvz, dq[4], tx, ty;
    sea_deck_pose(&s->sea, s->st.t, &dz, dq, &tx, &ty);   // + deck_vz for contact (§A.2)
    s->target_xy[0]=tx; s->target_xy[1]=ty; s->deck_z_live=dz;   // deck_z_live -> contact/touchdown/verdict
}   // else: static pad drift script, or (interactive) leave whatever the operator poll set
s->gcmd.target_xy[0]=s->target_xy[0]; s->gcmd.target_xy[1]=s->target_xy[1];   // GM_HOVERSLAM
// MPPI: mppi_step copies s->target_xy into M->target_xy at entry
```

---

## C. THE MONEY SHOT — pred_impact re-converging onto the moved target

**This is the demo's emotional core: the operator moves the target, `pred_impact` swings off, then re-converges onto the new target as the solve tightens — the visible "it actually re-solved it" beat.**

### C.1 What is already streaming (D-013, protocol v3)

`main.c:394-398`, `fill_tlm`: `pred_impact[2]` is the guidance's predicted touchdown, computed as the kinematic coast point `r_xy + v_xy·clamp(t_go,0,60)` — one consistent semantic for hoverslam and MPPI (`protocol.h:116` offset 220; `decode.ts:166`). `dist_pad` (offset 216, `main.c:382-385`) is the slant distance to the pad. Both are **pure reads of state** in the telemetry writer — no guidance feedback, directive-5-clean.

### C.2 What the renderer draws (target + pred_impact together)

The renderer already has `predImpact` and `distPad` decoded (`decode.ts:91,90`). The money shot is three glyphs in the ground plane:
1. **The target** — a pad ring / droneship deck, drawn at `target_xy` (see C.3 for how the renderer learns it). On a moving deck it slides; on an operator drag it follows the cursor.
2. **The predicted-impact marker** — a reticle at `pred_impact`, the guidance's own answer to "where will I land?"
3. **The vehicle + its ghost/plan line** (§11.11, the `plan[]` tail; the ghost line "writhes, snaps on disturbances, and the vehicle chases it").

**The beat, frame by frame:** operator drags the pad 200 m east → `target_xy` jumps → the *offset* `(r_xy − target_xy)` the guidance nulls jumps → within a few 50 Hz ticks the divert law (or MPPI replan) re-aims → `pred_impact` (which lags the state) swings toward the new target and **converges onto it as `t_go` and `v_xy` shrink** → `dist_pad` (now target-relative, C.4) counts down. The renderer can draw a fading trail on `pred_impact` so the *swing* is visible as an arc that lands on the target. **This is the entire thesis in one gesture:** a human moves the goalposts, and you watch the guidance re-solve to the new goal, in real time, with no scripted trajectory.

**Enhancement (make the convergence legible):** color `pred_impact` by `dist_pad` (red when far, green when it's about to nail it) and draw a line from `pred_impact` to `target_xy` whose length IS the predicted miss. When that line collapses to a point, the solve has re-converged — the visual "it solved it."

### C.3 The one honest protocol addition: `target_xy` on the wire

Today `dist_pad` and `pred_impact` assume the pad is at origin, and the renderer has no `target_xy` field — it would have to *guess* the target is at `(0,0)`, which is wrong the moment the target moves. **The clean fix is a new `target_xy[2]` field in `BlTlmFixed`** so the renderer draws the target from the wire (single source of truth), plus reusing `BL_EVT_TARGET_CHANGED` (already in the enum) for the discrete "operator moved it" beat.

**Protocol change — goes through the D-010/D-011 mirror+golden unit process (this is non-negotiable, `DECISIONS.md:672-680`):**
- Add `float target_xy[2]` to `BlTlmFixed`. Placement: after `deck_quat` (offset 284) is cleanest (append near the tail so the existing v3 offsets 220/228/264/268 don't move), OR grouped with the guidance-derived block — either way, **every downstream offset after the insertion point shifts**, `sizeof` grows, `BL_PROTO_VERSION 3→4`, the static asserts update (`protocol.h:231-252`), `decode.ts` mirrors it, `goldens/protocol/*.hex` re-freeze, `tools/gen_protocol_ts.py` regenerates the TS. **One validated unit**, exactly like D-013 did for `pred_impact`. (Reuse the D-013 playbook verbatim: `runs/proto_report.md`, `runs/ws_probe.mjs`.)
- Set a new flag bit `BL_TLM_FLAG_TARGET_MOVABLE` (next free bit after `NAV_NOISY`, `protocol.h:54-57`) so a client knows the target is not pinned at origin and should read `target_xy`.
- `fill_tlm` writes `p->target_xy[0/1] = (float)s->target_xy[0/1]` (pure read).
- `HELLO` optionally gains the initial `target_xy` and the SEA spectrum table (§11.9) so the renderer builds the ocean/deck phase-locked from frame 0.

**Note on `pred_impact` v2 (optional, deferred):** the current `pred_impact` is kinematic (`r+v·t_go`, no wind/steering/aero — the D-013 limitation). For the moving-target demo it is *good enough* (it converges onto the target as the solve tightens, which is the beat). A v2 that streams the *planner's own terminal projection* (MPPI's ZEM touchdown, `guidance_mppi.c:503`) would be a steadier, truer marker — but that is the pred_impact-v2 work already noted in the continuity handoff (§4C), a separate protocol-mirror unit, not required for this feature.

### C.4 `dist_pad` becomes target-relative

`main.c:382-385` computes `dist_pad` from `dx=st->y[S_RX]` (origin). With a moving target, subtract it: `dx=st->y[S_RX]-s->target_xy[0]`, `dy=st->y[S_RY]-s->target_xy[1]`, `dz=st->y[S_RZ]-deck_z_live`. Now `dist_pad` IS the live miss to the *current* target — the number the HUD shows counting down as the guidance re-solves. (Pure read, directive-5-clean.)

---

## D. THE PLAY MENU — the same plumbing generalizes

The movable-target machinery (a `target_xy` slot, threaded into guidance via `GuidanceCmd`/nav-view, drawn via `pred_impact`+`target_xy`) is a small, general primitive. Four "watch it re-solve" demos compose from it; **movable target is the centerpiece, these are the "same mechanism enables" coda.**

- **Abort-to-alternate (two targets, reassign mid-descent).** Keep two target slots `target_xy[0..1]` and an active index; the operator (or a scripted EVT) flips the index mid-descent. `BL_EVT_TARGET_CHANGED /*args[0]=pad*/` (`protocol.h:226`) was authored for *exactly this* — `args[0]` is the pad index. The guidance re-aims at the new active target with zero new code (it just reads a different `target_xy`), and `pred_impact` leaps between pads — a dramatic "diverting to the backup droneship" beat. The only addition over movable-target is the index+flip.

- **Engine-out (a separate but related "watch it re-solve" demo).** Not a target change — an *authority* change — but it composes on the same "re-solve visibly" stage. Drop `n_eng` or apply a thrust-deficit mid-burn (the `INJECT_DISTURBANCE` thrust_scale already exists, `sim.c:68-76`, `EnvCtx.thrust_scale` `dynamics.h:26`); the operator triggers it live in the interactive mode via a second inbound message code. MPPI's replan (or hoverslam's feedback) re-solves with less authority; `pred_impact` shows whether it can still make the target (and honestly, if the deficit is severe, the marker *doesn't* reach it — the honest failure, §F). Reuses the interactive inbound channel (§M2.2) with a new message tag; no target plumbing needed.

- **IC perturbation.** In the interactive mode, let the operator nudge the initial offset (or a live position/velocity kick — again an inbound message, again `INJECT`-flavored). The guidance re-solves from the perturbed state. This is the "flick the vehicle and watch it recover" demo; it is the same inbound-channel + same pure-observer readout, and it stress-tests the divert authority visibly.

Each is one small increment on the movable-target primitive: a second slot + index (abort), a thrust input (engine-out), a state kick (IC). The centerpiece — a draggable pad/deck the guidance chases — is the one that most directly *shows the thesis*; the rest are the same lever pointed at different variables.

---

## E. STAGED BUILD PLAN (each stage independently gated)

**All in a `_target_wt\` worktree copy (CMakeLists + core; VS2022 x64 configure), gitignored. Never edit/build the real tree until a stage's gate is green.** Gates after EVERY build (HANDOFF §1.7): `--selftest` = PASS; `--headless --scenario terminal --seed 42 --runs 200` = **exactly 194/200**; a determinism pair on the changed scenario (same `--run` twice, RESULT lines byte-match); and — since this touches the lateral law — the **MPPI single-run invariance check** (`--run --scenario aero_offset --seed 42 --run 1 --mppi` vs the current `(1.5,3)`-config reference RESULT line, `td_v 2.63 / lat 10.48` per the continuity handoff §0). If TERMINAL moves, the change leaked past its gate — stop, fix.

### Stage 0 — Static pad at nonzero offset (prove the substitution is byte-clean when target=origin)

**Build:** the `target_xy` plumbing (§B.3, `GuidanceCmd` field), the hoverslam substitution (§B.1), the MPPI shifts (§B.2). **Set `target_xy=(0,0)` everywhere** (no target motion, no new input path). Add a *worktree-only* `--target X Y` debug flag on `--run`/`--headless` to place a STATIC target at a nonzero point for the second half of the test.

- **The byte-equality proof (target=origin):** with `target_xy=(0,0)`, EVERY baseline must reproduce EXACTLY, because subtracting zero is a no-op the compiler must fold identically:
  - TERMINAL `--headless --seed 42 --runs 200` = **194/200 byte-exact**.
  - ENTRY `--headless --seed 42 --runs 100 --mppi` = **95/100** (the D-016 number).
  - AERO `--headless --seed 42 --runs 60 --mppi` = **44/60**.
  - MPPI single-run invariance = the `(1.5,3)` reference line, byte-identical.
  - **Determinism pair** on each. If any moves, the substitution is not algebraically neutral (a sign error, a missed `v_xy` that should have stayed inertial, or an MPPI read left un-shifted) — fix before proceeding.
- **The substitution-works proof (target≠origin):** place a static target at, e.g., `(300,0)` on AERO and RE-RUN with the vehicle's *initial offset reduced by the same 300 m* (i.e. start the vehicle at `lat=800` aiming at target `300`, vs the baseline `lat=500` aiming at `0`). The two must land **identically** (same td_v/lat/verdict) — because the *relative* geometry `(r_xy − target_xy)` is identical. This is the decisive test that the substitution is a true coordinate shift, not an approximation. (A cheaper variant: target `(500,0)` with the vehicle at the baseline `500` offset → the vehicle is AT the target → it should behave like a `lat=0` near-terminal case.)
- **Decision rule:** proceed to Stage 1 iff (a) target=origin reproduces all baselines byte-exact AND (b) the relative-geometry equivalence holds. Both are hard equalities; no tuning.

### Stage 1 — Deterministic seeded pad drift / SEA deck in `--headless` (gate-clean, goldens)

**Build:** `core/sea.{h,c}` (§A.1) + the pad-drift script; the contact `deck_z(t)`/`deck_vz(t)` coupling (§A.2); the target-relative verdict (§A.3); the seeded `target_sample` in `sim.c` (§B.3). Arm via a `--sea` flag or the `ASDS_NIGHT` scenario (`scenario.c:19`).

- **Determinism (the whole point of Mode 1):** the moving target is seeded and closed-form, so a `--headless --scenario asds_night --seed 42 --runs N` batch **replays byte-identical** (run it twice, memcmp the CSVs — like every existing golden). Freeze `goldens/mc/asds_night_s42_baseline.txt` once it's stable. Also freeze a **single-run full-capture golden** (à la the D-016 procedure) so the deck-motion + re-solve trajectory is pinned bit-exact.
- **What the gate proves:** guidance lands on a *moving* target at a rate that's honest (measure it — this is a NEW capability, not a regression of an existing gate; report the number with cross-seeds s7/s99 like every rate claim). TERMINAL/AERO/ENTRY (no SEA) stay byte-identical (directive 9 — SEA is `MOD_SEA`-gated).
- **Two sub-stages (isolate the horizontal re-solve from the contact complications):**
  - **1a — pad drift (horizontal only, `deck_z=0`):** the cleanest moving target. Proves the guidance re-solves for a moving *horizontal* target with no contact-frame changes. Gate: lands on the drifting pad; deterministic; goldens.
  - **1b — SEA deck (heave + station + contact coupling):** adds `deck_z(t)`, `deck_vz(t)` leg loads (§A.2). Gate: lands on the heaving deck; leg-crush is sea-phase-dependent (the honest new physics); deterministic; goldens. Defer tilted-normal contact (deck pitch/roll) to a polish follow-up (§F).
- **Decision rule:** ship Stage 1 into the honest tree iff both sub-stages are deterministic (byte-exact replay) AND the moving-target land rate is measured + reported honestly with cross-seeds. This is a *new golden-able capability*, so the bar is "deterministic + honestly measured," not "beats an existing gate."

### Stage 2 — The fenced interactive `--serve-interactive` channel (observer-doctrine-sensitive)

**Build:** `cmd_serve_interactive` in `main.c` (§M2.1); the `ws.c` inbound-sink (§M2.2, the smallest possible surface — parse `BlTargetSet` from the already-unmasked `pay[]`); write `s.target_xy` from the poll; `emit_evt(BL_EVT_TARGET_CHANGED)`; the `target_xy` protocol field + `BL_TLM_FLAG_TARGET_MOVABLE` (§C.3, through the full mirror+golden unit). The `--mppi` option on the serve path (trivial — `cmd_serve` hardcodes `GM_HOVERSLAM` at `main.c:463`; the interactive mode should allow `--mppi` so the operator can watch the *replanner* chase the target, which is the strongest demo).

- **What the gate proves:** it is NOT a determinism gate — it is a *doctrine* gate. The proofs are:
  - **`--serve` is untouched:** its `ws_poll_client` still drops data frames (the inbound sink is only wired in the interactive path). Re-run a `--serve` capture vs a pre-change one — byte-identical wire frames (the D-013 `ws_probe.mjs` reproduces the golden).
  - **The deterministic gate is untouched:** `--selftest` PASS, TERMINAL 194/200, all Stage-0/1 goldens still match. The interactive code is in a *separate mode* that the gate never invokes.
  - **The inbound surface is minimal and audited:** the only thing that crosses is a validated `BlTargetSet` (magic-checked; anything else ignored). Read the diff of `ws.c` — the `op==0x1/0x2` branch must ONLY copy `pay[]` to a sink, never touch sim state directly. No renderer-derived state, no camera, no physics-result, flows back.
  - **The banner + ADR quarantine it:** startup prints "INTERACTIVE SANDBOX — determinism WAIVED, not a gate, no goldens"; the ADR states no golden is ever frozen from a live session.
- **Optional polish (the honest bridge):** the input-trace recorder (§M2.3) — record `(step,x,y)` operator sets to a CSV, add a `--replay-target-trace FILE` path in `--headless` that reads the trace (no live input) and reproduces the run deterministically. **That replay IS gate-clean** and turns a good sandbox moment into a golden-able artifact. Recommended, not required.
- **Decision rule:** ship Stage 2 iff the doctrine proofs all hold (--serve byte-clean, gate byte-clean, inbound surface minimal + audited, quarantine banner + ADR present). If ANY deterministic gate moves, the interactive path leaked into the pure tree — that is the #1 risk (§F) — stop and re-fence.

---

## F. RISKS / TENSIONS (honest)

1. **The determinism line is the #1 risk — a sloppy interactive path silently pollutes the oracle.** If the inbound-sink wiring bleeds into `--serve` (or, worse, into the sim path used by `--headless`), the memcmp oracle and the anti-cheat thesis die (D-011 §5). **Mitigations, layered:** (a) a *separate mode* (`--serve-interactive`), not a flag — the interactive code path is never reachable from the gate; (b) the inbound sink is opt-in (`ws_set_inbound_sink`, which `cmd_serve` never calls); (c) the target slot is mutated ONLY by the operator poll in the interactive mode — in every deterministic mode it is set once from the seeded source and read-only thereafter; (d) the Stage-0 byte-equality proof (target=origin reproduces every baseline) is the tripwire that catches any accidental coupling. The audit is: `grep` for every writer of `s->target_xy` and confirm exactly two — the seeded `sea_deck_pose`/drift-script (deterministic modes) and the interactive poll (Mode 2 only).

2. **Divert authority is finite — a target that moves faster/further than the vehicle can chase WILL miss, and that honest failure is a great demo.** The `D_phys≈1107 m` ceiling (`runs/sandbox/ceiling.c`, the authoritative optimal-divert study, "PHYSICAL D_phys (vcap=30, fade/15)" — the AUTHORITATIVE ceiling for the AERO 12km/-330 profile) bounds how far the vehicle can re-aim from a given altitude. If the operator drags the pad 1500 m at 3 km altitude, the divert is **physically infeasible** and `pred_impact` will *not* reach the target — it will asymptote to the divert-authority frontier and the vehicle lands short. **This is not a bug; it is the thesis's honest edge:** the guidance solves *as far as physics allows*, and when you ask for more than the vehicle has authority for, it misses — visibly. The renderer can draw the authority frontier (a reachable-set ring around the vehicle, radius ≈ `D_phys(h)`) so the operator *sees* when they've dragged the target out of reach. This turns the limitation into a teaching moment and pre-empts "why didn't it land on my pad" — because you moved it beyond the physics. **Recommendation: draw the reachable-set ring in the interactive mode; it makes the failure legible and it is the most honest possible framing.**

3. **Moving-deck contact dynamics (touchdown on a heaving surface).** The leg loads on a rising deck are higher (closing velocity `vz − deck_vz` is larger) and on a falling deck lower — this is the SEA↔`contact.c` coupling (§A.2). Two honest caveats: (a) **Stage-1b must handle `deck_vz` in the contact wrench** (`contact.c:48,60`), or leg-crush and tipover will be wrong (too soft on a rising deck — you'd under-count hard landings). (b) **Deck pitch/roll tilt the contact normal** — a fully faithful heaving deck contacts on a tilted plane (`contact.c:44` assumes a vertical `deck_z`). Stage-1b keeps the normal vertical (heave-only contact) for scope; the tilted-normal contact is a SEA-polish follow-up with its own ADR (it changes the contact geometry and wants its own goldens). Flag it in the Stage-1 ADR as a known simplification. The verdict's target-freeze-at-touchdown (§A.3) is essential here — grade on where the deck *was* at contact.

4. **What guidance-knows-about-the-future does to the anti-cheat thesis.** Option (ii) (§A.4, transmitted spectrum → predict `deck_z(t_touchdown)`) gives guidance *future* information. This is legal-by-§11.9-precedent (the spectrum is a transmitted quantity), but it is a **stronger** claim than "solves the current state" — the vehicle now knows where the deck *will be*, not just where it *is*. If shipped as the default, a skeptic could (fairly) say "it's not solving the moving target, it's solving a known future." **Mitigation: default to Option (i) (blind to future, track current pose), which is unimpeachable — it re-solves on the fresh state exactly like the D-016 MPPI re-solves a wind-disturbed descent.** Option (ii) is a *labeled, ADR'd variant* (`--sea-forecast`), never the headline. The honesty is in the labeling: "with transmitted heave forecast" is a different capability than "tracking the current deck," and the two must not be conflated in any rate claim.

5. **Minor: `pred_impact` is kinematic (D-013 limitation).** On a moving target the current `r+v·t_go` marker converges but with more jitter than a true planner-terminal projection would (it doesn't account for the burn steering). It is *good enough* for the demo (the convergence beat is clear), but the pred_impact-v2 work (planner's own ZEM touchdown, continuity §4C) would make the marker steadier. Not required; noted so the renderer team doesn't over-interpret marker jitter as a guidance defect.

6. **Minor: fast interactive drag + inertial-velocity-null.** If a build agent wires a fast-dragged target without `target_vxy` (§B.1 caveat), the vehicle nulls *inertial* velocity and may appear to "lag" a fast drag unnaturally (it aims at the target but doesn't match its velocity). Fix is the one-line `- Kvd*(v_xy − target_vxy)` extension, gated to the interactive mode. For the slow canonical cases (SEA wander, slow drift) this never bites.

---

## G. Implementation checklist (for the build agent, no re-derivation needed)

1. **`GuidanceCmd` (guidance.h):** add `double target_xy[2];` (+ optional `double target_vxy[2];`). Zeroed by default → TERMINAL/AERO/ENTRY byte-identical.
2. **`Sim` struct (sim.h):** add `double target_xy[2]; double target_vxy[2]; double deck_z_live; int target_active;` + a `SeaState sea;`. Zero-init by the existing `memset` in `sim_init` (`sim.c:60`).
3. **Hoverslam substitution (guidance_hoverslam.c:84):** `double r_xy[2]={y[S_RX]-g->target_xy[0], y[S_RY]-g->target_xy[1]};`. Nothing else in the law changes (the trace, §B.1). Optional `v_xy` → `v_xy − target_vxy` at lines 188-189, gated to nonzero `target_vxy`.
4. **MPPI shifts (guidance_mppi.c):** thread `target_xy` into `MppiState` (set at `mppi_step` entry from the nav view); at each of the ~8 position reads (the 4 `converging_vdes` calls: lines 329/413/482/564; the cost `rx/ry`: lines 402/406/376/454/462/476/513; the ZEM: 503/509) subtract `M->target_xy`. **Do NOT shift the integrated `rst.y`** (§B.2). Velocity terms unchanged.
5. **`core/sea.{h,c}` (NEW, §A.1):** P-M spectrum (App-A.3), 48 seeded components, `sea_init(seed,Hs)`, `sea_deck_pose(t)` pure closed-form → `deck_z, deck_vz, deck_quat, target_xy`. Table exported in HELLO (§11.9).
6. **`sim.c` target source (§B.3):** on each step (deck_z/contact) + guidance tick (target_xy → gcmd), if `MOD_SEA` call `sea_deck_pose`; else static pad-drift script. Fill `s->gcmd.target_xy`.
7. **Contact coupling (§A.2):** pass `deck_z_live` (not `s->se.deck_z`) to `contact_substep` (`sim.c:340`) and the touchdown test (`sim.c:350`); pass `deck_vz` into `contact_wrench` and subtract from `fv[2]` (`contact.c:48,60`).
8. **Verdict (§A.3):** `set_verdict` `lat` (`sim.c:88,92`) and the touchdown capture `impact_lat` (`sim.c:354`) become target-relative; latch `target_xy` at first contact.
9. **Telemetry (§C):** `dist_pad` target-relative (`main.c:382-385`); add `float target_xy[2]` + `BL_TLM_FLAG_TARGET_MOVABLE` to `BlTlmFixed` **through the full protocol mirror+golden unit** (BL_PROTO_VERSION 3→4, static asserts, decode.ts, `goldens/protocol/*.hex`, gen_protocol_ts.py — the D-013 playbook); `fill_tlm` writes it (pure read).
10. **Interactive mode (§M2 / MODE 2, Stage 2 only):** `cmd_serve_interactive` (main.c), `ws` inbound sink (parse `BlTargetSet` from `pay[]`, the smallest surface), `emit_evt(BL_EVT_TARGET_CHANGED)`, `--mppi` on the serve path, the WAIVED-determinism banner. Optional input-trace recorder + `--replay-target-trace` (gate-clean replay).
11. **Gates (every build):** selftest PASS, TERMINAL 194/200, determinism pair, **MPPI single-run invariance** (the leak check — non-negotiable, HANDOFF §1.4). Stage 0: target=origin reproduces ENTRY-mppi 95 / AERO 44|60 / all goldens byte-exact.
12. **ADRs:** Stage-0/1 → a `DECISIONS.md` entry (the offset substitution + SEA module + moving-target land rate WITH numbers + cross-seeds; freeze `asds_night_*_baseline.txt` goldens + the protocol re-baseline). Stage-2 → a *separate* ADR that explicitly records: interactive mode is non-deterministic BY DESIGN, produces NO goldens, is quarantined from every honesty claim, and the exact inbound surface (one validated `BlTargetSet`). The two ADRs keep the two modes' honesty postures on the record, separately.

---

## Appendix A — the offset-substitution trace (evidence for §B, cited line-by-line)

**Hoverslam (`guidance_hoverslam.c`), the offset flows from ONE assignment:**
`r_xy` @84 → `r_mag` @132 → `v_rad_cur` @147 → `r_pred` @148 → `vdes_mag` @149 → D-012 brake `os` @168 → `vdes[]` @176 → `a_lat` @188-189 (and the damp-through-ignition `Kvd*(-v_xy)` @233-234 uses only `v_xy`, correctly inertial). **Substitute at @84; the law inherits it.** ✓ single line.

**MPPI (`guidance_mppi.c`), the offset is read (as pad-relative-to-origin) at:**
`converging_vdes(y[S_RX],y[S_RY],…)` @329 (cmd_from_u_lean), @413 (running cost), @482 (terminal), @564 (warm-start); `rx=rst.y[S_RX]` @402/@472; `rxy2` @406/@476; gate `gr2` @376; `Q_RXY` @421; ZEM `zx=rx+vx*t_ign` @503/@509; touchdown `TD_RXY`/`off_pad`/pull @454/@457/@462; in-air pull @488/@513-514. **Subtract `target_xy` at each; do NOT shift the integrated state** (the plant EOM @399 + ground test @447 need true world position). ✓ mechanical, ~8 edits, one concept.

**Plant/verdict/telemetry offset reads that also become target-relative:**
verdict `lat` `sim.c:88,92`; touchdown `impact_lat` `sim.c:354`; `dist_pad` `main.c:383`; `pred_impact` (already `r_xy+v_xy·t_go`, converges onto the target once the renderer knows `target_xy`) `main.c:396-397`.

**Already-present hooks that make this cheap:** `ScenarioEnv.deck_z` (`scenario.h:12`) + its three consumers (`sim.c:340,350`, `main.c:383`); `contact_wrench(…, deck_z, …)` parameterized (`contact.c:32`); `BL_EVT_TARGET_CHANGED /*args[0]=pad*/` (`protocol.h:226`); `deck_z`/`deck_quat` TLM fields (`protocol.h:124-125`) + their `decode.ts` mirror (`decode.ts:174-175`); the SEA module slot reserved in canon (`CLAUDE_v1.md:150`) + spectrum pinned (App-A.3, `CLAUDE_v1.md:1053-1057`); `ws_poll_client`'s already-unmasked `pay[]` (`ws.c:275-278`).

## Appendix B — canon/doctrine compliance restated
- **directive 5 / D-011 §5 (pure observer, HARD LINE):** Mode 1 — the target is internal to the plant; nothing crosses. Mode 2 — the ONLY inbound is a scalar target position (a plant input like a `DISTURB` gust); no renderer-derived state feeds back; fenced in a separate non-gate mode. ✓
- **directive 2 (determinism):** Mode 1 is seeded closed-form → byte-exact replay + goldens. Mode 2 is non-deterministic BY DESIGN, quarantined, no golden ever; optionally replay-able from a recorded input trace (which IS gate-clean). ✓
- **§4.3 analog / §8.1 (what guidance may know):** guidance reads the target's CURRENT pose (§8.1-legal: "Deck pose is part of NavState"); future motion is Option-(ii)-only, labeled + ADR'd, never default. ✓
- **directive 7 (one dynamics source):** `target_xy` threads through `GuidanceCmd`; the MPPI rollout mirror (`cmd_from_u_lean`, `converging_vdes`) gets the same shift → profile-exact parity; the single-run invariance check with `target=0` proves no leak. ✓
- **directive 9 (TERMINAL byte-identical):** `target_xy=0` by default; SEA is `MOD_SEA`-gated; TERMINAL never touches any of it → byte-identical. ✓
- **D-010/D-011 protocol process:** the `target_xy` TLM field goes through the full mirror+golden unit (version bump, static asserts, decode.ts, hex goldens, gen_protocol_ts.py) — the D-013 playbook. ✓
