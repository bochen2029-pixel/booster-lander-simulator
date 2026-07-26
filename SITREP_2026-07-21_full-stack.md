# BOOSTER LANDER — FULL-STACK SITREP (snapshot 2026-07-21, ~19:45 CDT)

> A point-in-time state-of-the-project report written from a fresh session after a full
> discovery pass over the flight software AND the frontend/renderer. Trust files over
> recollection. Backend facts verified against disk (selftest, live runs, source diff);
> frontend facts verified against source + the FE reports. Authoritative living plan
> remains `ROADMAP.md`; this is the wider snapshot the operator asked for.

---

## 0. TL;DR — where the whole thing stands

| Layer | State | One-liner |
|---|---|---|
| **Oracle guidance arc** | ✅ concluded (in sandbox) | The optimizer-in-the-loop (`cfly`) sweeps the held-out compound **16/16 GOOD+** in the LODESTAR sandbox — the "no open-loop law can fly the ±5% knife-edge, but a solver-in-the-loop can" result. |
| **Phase 3 — port `cfly` → main tree** | 🟠 **in flight, NOT landing yet** | Isolated `cfly-port` worktree. Builds, selftest PASS, byte-gates intact — but the main-tree compound **fuel-starves (0/4, all FUEL crashes)**. Root-caused (below). |
| **Main-tree flight sw** | ✅ stable | NP_VERSION 6, selftest PASS, HEAD `0ed26eb`. hoverslam / MPPI / neural modes shipped; engine-out + moving-deck + interactive channel all in. |
| **Frontend / renderer** | ✅ mature, cinematic, green on gates | three.js **r185 WebGPU**, physics-driven raymarched plume, Blue-Marble Earth, Gerstner-ocean droneship, camera director, spatial-audio observer, portable Tauri app. "Pixels need eyes-on," but architecturally complete. |

**The single most important new finding this session:** the screenshots celebrate "16/16 GOOD+", but that is the **sandbox** result. The **main-tree port does not reproduce it yet** — it systematically runs out of fuel in the terminal phase. This is a real, precise, fixable divergence (three "D-040 main-tree tuning" edits the port added on top of the proven law), not a mystery.

---

## 1. Repo & worktree topology

One git repo, five live worktrees + eight FE experiment worktrees.

| Worktree | Branch | HEAD | Role |
|---|---|---|---|
| `C:\Booster_Lander_Simulator` | `main` | `0ed26eb` | **Main tree** — flight sw + renderer + portable app. selftest PASS, NP6. |
| `C:\Booster_Lander_CFLY` | `cfly-port` | `0ed26eb` (+uncommitted) | **The active work** — porting `cfly` into the main plant. All changes are working-tree (nothing committed — correct: it doesn't land yet). |
| `C:\bl_opus_oracle` | `opus/robust-oracle` | `2e16674` | **LODESTAR sandbox** — where `cfly` was proven 16/16. The reference implementation (`oracle_cem/oracle_t200.c`). |
| `C:\Booster_Lander_Oracle` | `oracle-solver` | `c1c60da` | **KESTREL sandbox** — the residual-distillation NULL + a parked GPU/CUDA CEM port. |
| `…/.claude/worktrees/condescending-mcnulty-00ea83` | detached | `03d8f70` | Ephemeral agent worktree. |
| `C:\Booster_Lander_Simulator\_fe_*_wt` ×8 | `main` | — | Parallel FE experiment worktrees (audio/const/exe/merge/rec/scene/shell/vis) from the earlier multi-agent FE push; each dirty. Not load-bearing. |

---

## 2. BACKEND — flight software & the oracle arc

### 2.1 How we got here (the arc, all committed & ledgered)

The engine-out showcase drove a long arc. Distillation was proven dead **three independent ways** (D-025 teacher-collapse; D-031 MPPI-DAgger NULL; D-032 reactive-DAgger NULL; + KESTREL residual-distill NULL `c1c60da`) — a shared 37k-param policy can't absorb the engine-out lateral without a clean-vs-EO compromise. The operator's reframe (**solve the deterministic plant offline with an expensive search, distill later**) produced the answer:

- The compound landing (engine-out × gust × moving/heaving deck) is a **knife-edge**: the guidance law's 9-D θ-basin is **±5% on 4 of 9 coordinates** → no fixed-θ / lookup / NN-regression can fly it (θ-predictor built + scaled to 66 scenarios = conclusive NO-GO).
- But an **optimizer in the loop** finds the basin ~100%: an oracle-strength CEM solve at t=0 (the mission plan) + small warm-started CEM replans every 10 s (the onboard replan).
- Sandbox proof (`2e16674`, held-out scen 16..31): **`cfly` 16/16 landed · 16/16 GOOD+ · 12 PERFECT** vs nominal-θ 4/16 and θ̂-net 2/16.
- A real **plant touchdown bug** was found & fixed along the way (sandbox missed deck-rises-into-vehicle heave contact; main tree already robust).

The AlphaZero framing the operator set from the start: **search supplies the ±5% precision no regression can; the NN's job becomes warm-start / compute-reduction, not end-to-end control.**

### 2.2 The `cfly`-port (the active work) — built, byte-clean, but fuel-starving

**What's in the port** (`C:\Booster_Lander_CFLY`, all uncommitted working-tree):
- `core/guidance_cfly.{c,h}` (new, 212 lines) — the ported law (`cfly_law`) + warm-CEM replan (`cfly_replan`) + cost (`cfly_cost`).
- `--cfly` flag → `GM_CFLY` wired into `cmd_run` + `cmd_headless` (`core/main.c`).
- `CflyState` embedded in `Sim`; `sim.c`/`sim.h`/`guidance.h`/`CMakeLists.txt` modified.

**What's healthy:**
- Builds clean; **selftest PASS** (NP_VERSION 6 KAT intact).
- **Byte-gate intact:** GM_CFLY is default-off, so the TERMINAL ×200 golden is unchanged — `runs/cfly_gate_terminal.txt` = **194/200 = 97.0%**, matching `runs/n0main_terminal.txt`.
- **Deterministic:** repeated compound run-0 is bit-identical; the CEM sampler is a pure `(seed, step)` xorshift.
- **Lateral + attitude are excellent:** the ZEM/ZEV law nulls cross-range to **≤0.1 m** on the good draws, upright.

**What's broken — the finding:** on the main-tree canonical compound (`--scenario entry --engine-out random --gust 15@6000:1000 --target circle:15:40`, seed 42), a 4-run rate-check returned:

```
run 0 :: CRASHED  FUEL  td_v=12.65  lat=0.07  tilt=0.22   fuel=0  t=123.0
run 1 :: CRASHED  FUEL  td_v=53.69  lat=1.15  tilt=14.39  fuel=0  t=125.3
run 2 :: CRASHED  FUEL  td_v=9.14   lat=8.05  tilt=22.07  fuel=0  t=125.5
run 3 :: CRASHED  FUEL  td_v=12.88  lat=1.54  tilt=4.90   fuel=0  t=123.6
```

**0/4 landed — every one runs the tank dry in the terminal phase.** The `cfly_replan` stderr trace shows why: as the vehicle nears the deck the CEM's best-cost **stalls at ~6700 and never drives toward zero** while entry-throttle decays (1.00→0.49) — the optimizer can't find a soft-landing continuation, so the vehicle **hover-hunts to fuel exhaustion** and drops the last few metres.

> Timing note: **~51 s per compound run** (CEM-in-the-loop, `big=1` t=0 solve = POP 192 × 10 iters, warm replans POP 48 × 4). A full 42/7/99 × N eval is minutes-to-hours — iteration is slow; a warm/quality knob or OpenMP scaling matters for the eval phase.

### 2.3 Root cause — three "D-040" divergences from the proven sandbox law

Diffing `guidance_cfly.c` against the reference `oracle_cem/oracle_t200.c` (the file that produced 16/16):

1. **Added a "min-throttle-trap" coast** (`guidance_cfly.c:93–100`) — not in the sandbox. When the burn wants less than min throttle, it cuts the engine and coasts. Intended to stop a coast-burn sawtooth; in practice it interacts with the coast-relight latch and stretches the descent.
2. **Added a fuel-margin reward to the CEM cost** (`guidance_cfly.c:137`, `c -= 0.022*fuel_margin`) — the sandbox `cost_of` (`oracle_t200.c:572–588`) has **no** fuel term. This biases the optimizer's θ choices in a way the proven law never had.
3. **maxq threshold 58 000 → 40 000 Pa** (`guidance_cfly.c:72`) — changes when the max-Q cap burn engages ("the sandbox's 58 was its own atmosphere").

The **coast-relight law itself is a faithful, byte-identical port** (`guidance_cfly.c:83–84` == `oracle_t200.c:185–186`) — that part is correct. It's the three *additions* (all tagged "D-040 main-tree tuning", made because "the main-tree entry burn is thirstier than the sandbox's") that the previous session was mid-tuning when the session ended.

**Interpretation:** the main-tree plant's fuel/thrust budget genuinely differs from the sandbox's, so a verbatim port needs *re-tuning* — but the current ad-hoc terminal edits over-correct into fuel death. The clean path is to **start from the sandbox-faithful terminal law** (revert the 3 additions) and re-verify, then re-tune *only* what the main-tree plant actually requires, driven by the CEM cost rather than hand-added heuristics.

### 2.4 Main-tree flight software (the stable base the port sits on)
- Modes: hoverslam (reactive), MPPI (hierarchical), GM_NEURAL (NP_VERSION 6, 37k params). Learned policy: AERO 46, ENTRY 57/56/58 at teacher parity.
- Moving deck complete (D-034→D-037): target-relative verdict, P-M-spectrum heave, deck-relative leg loads, ±wander station-keeping, all byte-clean & streamed on the unchanged v4 protocol.
- Interactive channel (Mode 2): `--interactive` inbound `BlCmd` frames — WIND GUST / ENGINE OUT / THRUST LOSS + live target-drag (D-039).
- Gates (house law): selftest · TERMINAL ×200 byte-golden · MPPI run-1 AERO s42 HARD 2.63/10.48 · determinism pairs · leak byte-equality.

---

## 3. FRONTEND — the renderer, GUI & 3D aesthetics (the fresh scan)

`ui/` — **three.js r185 WebGPU**, TypeScript, Vite, Vitest. ~11k LOC, 16 test files (last FE report: 170/170 vitest green, typecheck clean, `vite build` OK). Two HTML entry points: **`index.html`** (the live cockpit) and **`constellation.html`** (the multi-run BRS view). Design doctrine throughout: **telemetry-honest** — every moving element is keyed to a streamed field; visuals are "garnish on honest state," never animation-keyed; telemetry never enters a React render path (DOM-imperative HUD).

### 3.1 Rendering pipeline (`scene/renderer.ts`, `scene/postfx.ts`)
- **WebGPURenderer** with automatic **WebGL2 fallback** (backend detected & logged to the HUD).
- **AgX tone mapping** by default (ACESFilmic behind a toggle) — degrades saturated plume emissives gracefully where ACES hue-skews.
- **Reversed-Z depth** (`reversedDepthBuffer`, depth32float) for the brutal **70 km → 1 m** range; log-depth on the WebGL2 path. Camera far plane **2 000 km** (to hold the stylized Earth globe), near 0.1, 50° FOV.
- **Floating origin** (`scene/floatingOrigin.ts`) keeps rendered coords small regardless of altitude.
- **Selective HDR bloom** post-pass (`postfx.ts`, TSL `BloomNode`: strength 0.8 / radius 0.55 / threshold 0.9) — the >1 plume/flash clears the gate; the LDR daytime scene stays crisp. Live-tunable.

### 3.2 The documentary scene & aesthetics (`scene/documentaryScene.ts`, 504 lines)
- **Operator doctrine: "always sunny & daytime by default."** The old dark-studio look is retired to a future night preset. Warm sun key (3.0) + bright hemisphere sky/ground fill so the white hull always reads; blue sky `0x8fc1ea`, aerial-perspective fog tinted to sky.
- **Atmosphere-by-altitude, physics-driven:** sky + fog lerp from near-black space (`0x03060f`) to full blue day as a function of the **real streamed ambient pressure** (`pAmb/101325 ^ 0.7`, Rayleigh ∝ density). Near-black by ~30 km, full blue at sea level. Stars fade in as the sky goes to space; the sky-fill light fades toward vacuum while the sun key stays. **You read altitude at a glance, and high burns pop against black.**
- **Procedural-lite booster** rebuilt from HELLO dims: tank (78%) + interstage cone + octaweb block + **4 hinged grid fins** (driven by `fins_act`) + **4 telescoping legs** (stowed↔deployed tripod from `deploy_frac`) + a gimbaled center bell (bells **never glow** — encoded rule). Hull `0xbfc3c9` roughness 0.55 metalness 0.15.
- **Plume-as-light:** a point light at the bell whose intensity/color track throttle (40 + 260·throttle) — at night this one system would carry the scene.
- Fog was the first-light bug (a desk-scale 400/6000 m preset in a 70 km scene fogged the whole flight to black); now a scene-scale depth cue (30 km→150 km, pushed to millions of metres at altitude so space reads clear-dark not milky-white).

### 3.3 The plume — "the crown jewel" (`fx/plume.ts`, 223 lines)
An **analytic raymarched TSL node** (WebGPU `RaymarchingBox`, 64 steps), **every parameter physics-driven from telemetry, nothing hand-keyed**:
- **Mach-disk ladder:** first-disk distance `x1 = 0.67·D_e·√(p0/pa)`; cell spacing grows with √(pressure ratio) — tight ladder at sea level, stretches to 1–2 cells by ~35 km.
- **Altitude balloon:** underexpansion widens & smears the plume toward km-scale translucency as ambient→0.
- **SRP forward-envelopment:** when burning supersonically (high C_T) the plume wraps forward around the vehicle, blended exactly like the physics (`smoothstep(0.5,3.0,ct)`).
- **Color:** kerolox soot blackbody ramp (deep orange tip → orange body → yellow-white throat) + a fuel-rich forward sheath + a one-sided sooty gas-generator dark streak.
- **TEA-TEB green flash:** brief boron-green burst at the bell, EVT-pulsed then decayed.
- Emits **HDR (>1)** additively so the bloom pass lights the core; the proxy box scales with throttle so 40% reads as a stub and 100% as a long torch.

### 3.4 Earth globe (`scene/earth.ts`, 145 lines)
A **stylized 600 km Blue-Marble globe** below the pad ("vibe-honest," not to-scale — the real 6371 km Earth shows no curve from 60 km and sits past the far plane). Real **NASA equirectangular textures bundled locally** (day/night-lights/clouds — portable app needs no network). In-shader **day/night terminator** with emissive city lights on the dark side, a drifting translucent **cloud shell**, a **fresnel atmosphere limb** halo, slow planetary spin. Self-lit (MeshBasicNodeMaterial) so it holds its real color as the scene lights dim toward vacuum. Mutually exclusive with the flat local ground (same 0.62 dayF threshold — no "two overlapping Earths").

### 3.5 Sea & ASDS droneship (`scene/sea.ts`, 415 lines)
The renderer half of the moving deck. A **CPU Gerstner-wave ocean** (129×129 grid, 4-component trochoidal swell, analytic normals, depth/foam vertex colors — pure & unit-tested, **decorative garnish, NOT the sim's P-M spectrum**) over a flat blue disc to the fog horizon. The **droneship**: steel barge (deck `0x3a4048`, dark hull, 5.5 m freeboard), blast-wall rim, container silhouette, white concentric-ring **bullseye** sized from HELLO `pad_radius`. **Posed exactly from the wire** — heaves in Y from `deck_z`, drifts in X/Z from `target_est_xy`, as the plant computes. **Deck tilt (`deck_quat`) not yet streamed** — deck stays level (the §F tilted-normal-contact follow-up; the `update()` already accepts a quat).

### 3.6 Camera director (`director/director.ts`, 314 lines)
- **Operator doctrine: default = KSP-style external orbit** (`FREE_ORBIT`) that frames the whole vehicle at its center and **never cuts away** — the user owns azimuth/elevation/radius via drag+wheel (a camera that fights the mouse is worse than none). Camera state never crosses the telemetry boundary.
- **Opt-in cinematic auto-director:** cuts on EVT beats through a rig grammar — `PAD_LONG_LENS` (2 km, 12° long lens tracking-footage) / `ONBOARD_DOWN` (hull-mounted GoPro riding attitude, down the legs+plume) / `CHASE` (spring-arm) / `FREE_ORBIT`. Debounced cuts, eased cubic transitions. Pure `decideAutoCut` is unit-tested.

### 3.7 HUD & GUI chrome
DOM-imperative flat-glass layer, CSS-custom-property theming (nominal/caution/abort):
- **`hud/hud.ts`** — phase ladder (14 phases, EVT-driven highlight), t_go, LOX/RP1 fuel, altitude, speed, mach, throttle, a **qbar-vs-STRUCT bar** (aero pressure vs the ~45 kPa envelope), verdict badge, and a **frame-time strip** (the 8.3 ms budget made visible).
- **`hud/injectPanel.ts`** — the Mode-2 failure buttons (WIND GUST / ENGINE OUT / THRUST LOSS) + target-drag.
- **`hud/cameraBar.ts`** — visible camera-preset selector. **`hud/timeline.ts`** — scrub timeline. **`hud/fidelity.ts`** — HIGH/LOW GFX toggle (LOW drops bloom + 2× DPR for weak GPUs; persisted). **`hud/screenshot.ts`** — in-app capture (P) + DEV self-verify hooks. **`shell/`** — the LZ-COCKPIT chrome (picker, connection chip, stderr panel, wire log, Tauri bridge).

### 3.8 Constellation — the multi-run BRS view (`constellation/`, its own page)
A separate pad-centric 3D scene that plants **every Monte-Carlo run as a touchdown glyph** at (synthesized-angle, `td_lat` radius) with schematic descent arcs above, raycast hover/click picking, and an **A/B paired-glyph + flip-hairline overlay** for comparing runs. Reads only classified run data — never the live path. This is the "shrunken BRS / scored vs the frontier" instrument the N3 showcase needs.

### 3.9 Audio observer (`audio/`)
A **third pure observer** (like the renderer & audio are siblings off the same one-way telemetry): Web-Audio procedural synthesis, **propagation-honest** — a retarded-time event queue so the ignition thump / shutdown pop / touchdown clang / sonic boom **arrive after the sound-wall delay** from vehicle→listener geometry; continuous engine bed + crackle from throttle×n_eng. Listener = active camera. Muted by default, resumes on first gesture. Writes nothing back.

### 3.10 Net / protocol (`net/`)
The v4 WebSocket wire: HELLO/TLM/EVT/STATS frames, decode, **never-snap interpolation** (`interp.ts`), `BlCmd` commands, protocol TS **generated from `core/protocol.h`** (`gen:protocol`). Carries `target_est_xy` + `deck_z` + the SEA-active flag that light up the moving-deck scene.

### 3.11 Portable app (`shell/`, Tauri/Rust)
`booster-shell.exe` — double-click launches the whole thing: spawns `booster-core.exe --serve --interactive` as a **supervised sidecar** (kill-on-close Job Object → no orphans; single-instance), opens a webview with the embedded three.js cockpit → `ws://127.0.0.1:<port>`. Copy-anywhere `dist/BoosterLander-portable/` (~11.7 MB), needs only Windows 11 + WebView2.

### 3.12 Frontend verification status
- ✅ Gates green at last report: typecheck clean, vitest 170/170, `vite build` OK (both pages), runtime-verified via eval (deck posed exactly from the wire; ocean displacement; live WS TLM capture).
- ⚠️ **"Pixels need eyes-on"** — headless WebGPU screenshots stall in the agent env, so the *look* (ocean palette/foam/specular, wave-vs-deck_z coherence, deck proportions, bloom intensity against a live burn, near-grid→far-disc seam) is on sensible-but-unverified defaults. None blocking.
- ⏳ Deck **tilt** awaits the sim streaming `deck_quat`.

---

## 4. The map — DONE vs OPEN

**DONE (shipped, verified, committed):**
- ✅ Oracle arc concluded; `cfly` proven 16/16 in sandbox.
- ✅ Distillation proven dead 3× (honest nulls).
- ✅ Moving deck (D-034→D-037), interactive Mode-2 channel, target-drag.
- ✅ Frontend: WebGPU pipeline, plume, Earth, ocean+droneship, bloom, director, HUD, constellation, audio, portable app.

**OPEN (the live edge):**
- 🟠 **Phase 3.3 — `cfly` port must actually land the main-tree compound** (currently 0/4, fuel-starve). ← *the immediate blocker.*
- ⬜ Phase 3.4 — held-out compound eval 42/7/99 vs reactive (11/90) / neural (0%) baselines → GO/NO-GO.
- ⬜ Phase 3.5 — ceremony (leak gates OFF, determinism pairs ON, no-regression floors AERO≥46/gust≥45/ENTRY≥57) + ADR (D-040) + commit.
- ⬜ N3 compound showcase in the cockpit (the wow) + the honest adjacent out-of-frontier failure.
- ⬜ FE polish: eyes-on tuning pass; `deck_quat` tilt; KESTREL's parked GPU CEM for real-time replan.

---

## 5. Recommended next steps (my read)

1. **Fix the terminal fuel-starve (Phase 3.3).** Start by reverting the port's three non-faithful "D-040" edits (min-throttle-trap `:93–100`, fuel-margin cost `:137`, maxq `58k→40k`) to the **sandbox-faithful** `oracle_t200.c` law, rebuild, and re-run the seed-42 compound. Two outcomes, both informative:
   - Lands → the additions were the bug; keep the faithful law, re-tune *only* via the CEM cost if the main-tree plant needs it.
   - Still starves → the main-tree fuel/thrust budget is genuinely tighter than the sandbox's; quantify the gap (compare fuel-at-terminal for hoverslam/MPPI on the same draw) and re-tune the *entry burn* (θ bounds / warm-start), not the terminal heuristics.
2. Once it lands run-0, do the **42/7/99 rate-check** vs the reactive/neural baselines (Phase 3.4). Mind the ~51 s/run cost — consider a lower warm-CEM budget for the eval sweep.
3. Only then: ceremony + D-040 ADR + commit the port; then wire the N3 cockpit showcase.

*(Autonomy note: this is normal debugging, not a hard blocker. Say the word and I'll take the terminal-law fix and drive it to a GO/NO-GO. I did not change any code this session — only read, ran read-only smokes, and wrote this report.)*

---

## 6. Environment notes
- `C:\Users\user\.claude` was recreated earlier today (old `MEMORY.md` + hooks gone); the memory dir is currently **empty**. Cross-session state survives in `ROADMAP.md` + `HANDOFF_2026-07-19_MORNING_AUTORUN.md` + the git ledgers, all intact. The SessionStart resume hook fired correctly.
- Selftest verified against disk this session: **PASS** (NP_VERSION 6) on both the main tree and the `cfly-port` build.
- Nothing committed or pushed this session; no source modified.
