# Booster Lander — Graphics / Presentation Canon (implementation-ready brief)

**Purpose:** one document the 3D-frontend designer can build the GUI/UI from without
re-reading CLAUDE_v1.md, DECISIONS.md, protocol.h, ws.c, or the ui/ scaffold.
**Compiled:** 2026-07-18 (Saturday) from CLAUDE_v1.md §4.4/§10/§11/§12, DECISIONS
D-010/D-011/D-012/D-013, core/protocol.h (v3, shipped), core/ws.c, core/main.c
(`--serve`), the `ui/` scaffold, and `runs/proto_report.md`.
**Ground truth verified this session:** `vitest run` = **26/26 green**; protocol.h and
decode.ts both at **v3, TLM fixed = 288 B**; `--serve` fully wired in main.c.

---

## A. THE WIRE CONTRACT (everything a renderer may consume)

### A.0 Doctrine — pure one-way observer (the hard line, D-011 item 5)

- The renderer is a **pure observer** on a **one-way** binary telemetry stream. It
  connects **directly** to `core --serve` over localhost WebSocket — **no relay hop**
  through Rust (the shell only spawns/supervises). `client.ts` already does this.
- **Nothing the client sends ever writes vehicle state.** The upstream channel is a
  *closed enumeration* (§A.4). Camera state **never crosses the boundary**. Adding a
  command that writes state "defeats the project."
- Every visual must be **derivable from telemetry** (directive 8). Ghost line, impact
  marker, possibility cloud, soot, frost, glow, booms — all keyed to streamed fields or
  EVT beats. Animation-driven triggers are an anti-pattern; **EVT is the only trigger
  channel**. Visual-only garnish (turbulence noise, dust) is allowed *on top of* honest
  state, never *instead of* it.
- Future clients (UE, native audio) are **additional** static-asserted mirrors of the
  same `protocol.h`. Renderers are disposable; the contract is not.

### A.1 Transport & rates

- `ws://127.0.0.1:8787` default (scaffold client) / `--port P` server-side. Binary
  frames, **little-endian**, packed structs, **no JSON on the hot path**.
- `ws.c` is a ~250-line RFC6455 **server subset**: single client, server handshake
  (SHA-1+base64 accept), unmasked binary frames, PING→PONG, CLOSE. Compiled only under
  `--serve`; **Windows-only** (Winsock2). Backpressure = drop-oldest; `seq` gaps tell the
  renderer (interp.ts counts `droppedFrames`).
- Rates: physics **500 Hz** · guidance **50 Hz** · **telemetry 125 Hz** (every 4th
  physics step) · STATS **~10 Hz** (every 12th emitted TLM) · render at display refresh,
  interpolated ~1 packet in the past.

### A.2 Packet kinds (magic = first 4 bytes, LE u32)

| kind | magic | size | cadence |
|---|---|---|---|
| HELLO | `HLL0` 0x304C4C48 | 72 B | once, on connect |
| TLM   | `TLM0` 0x304D4C54 | 288 B fixed **+ tails** | 125 Hz |
| EVT   | `EVT0` 0x30545645 | 48 B | on event (reliable, ordered) |
| STATS | `STT0` 0x30545453 | 48 B | ~10 Hz |

`ver` field on HELLO/TLM/STATS must equal **3** (`BL_PROTO_VERSION`); the decoder
**rejects a mismatch loudly** (old clients fail hard, intended). EVT has no `ver`.

### A.3 TLM fixed head — every field (offsets are the shipped v3 layout)

fp32 unless noted; **world frame, sim Z-up** (convert at draw, §A.6). Semantic in italics.

| off | field | type | semantic / renderer use |
|---:|---|---|---|
| 0 | magic / ver / flags | u32/u16/u16 | route; flags = SEA_ACTIVE(1) · RAW_MODE(2) · NAV_NOISY(4) |
| 8 | step | u64 | physics step index |
| 16 | **t** | **f64** | sim time [s] — *only f64; drives 2.92 s/km audio propagation* |
| 24 | seq | u32 | telemetry sequence — *gap detect / dropped-frame count* |
| 32 | r[3] | f32 | position [m] — *vehicle world pos* |
| 44 | v[3] | f32 | velocity [m/s] — *velocity vector viz, speed tape* |
| 56 | quat[4] | f32 | body→world, **xyzw scalar-last** — *attitude* |
| 72 | w[3] | f32 | angular velocity [rad/s] body — *spin/tumble* |
| 84 | mass | f32 | total mass [kg] — *HUD* |
| 88 | com_z | f32 | CoM height above base [m] |
| 92 | I_diag[3] | f32 | diag inertia about CoM |
| 104 | prop_lox | f32 | LOX remaining [kg] — ***LOX frost band height*** (D-010 #2) |
| 108 | prop_rp1 | f32 | RP-1 remaining [kg] — *fuel HUD* |
| 112 | throttle_cmd | f32 | commanded throttle [0.40..1] or 0 |
| 116 | throttle_act | f32 | actual (post-lag) throttle — ***plume length/brightness/light*** |
| 120 | gimbal_cmd[2] | f32 | commanded gimbal [rad] (pitch,yaw) |
| 128 | gimbal_act[2] | f32 | actual gimbal [rad] — ***articulate center bell*** |
| 136 | fins_act[4] | f32 | actual fin deflections [rad] — ***hinge the 4 grid fins*** |
| 152 | rcs_mask | u16 | 8 nozzles, 1 bit each — ***which RCS pod fired*** (viz + audio pan) |
| 154 | n_eng | u8 | engines lit {0,1,3} — ***plume core width, count*** |
| 155 | phase | u8 | `BlPhase` enum (§A.7) — *director cuts, HUD* |
| 156 | guidance_mode | u8 | 0 none / 1 hoverslam / 2 mppi |
| 157 | verdict | u8 | `BlVerdict` (NONE until settled) — *endgame card* |
| 158 | solver_flags | u16 | SOLVER_DEGRADED etc. bitmask — *engineering overlay* |
| 160 | mach | f32 | ***diamond sharpness gate, Mach-1 HUD*** |
| 164 | qbar | f32 | dynamic pressure [Pa] — ***aero whistle amplitude, haze*** |
| 168 | alpha_total | f32 | total AoA [rad] |
| 172 | p_amb | f32 | ambient pressure [Pa] — *plume `p_a`* |
| 176 | **p_chamber** | f32 | chamber pressure [Pa] — *plume `p_0`; **pressure ratio = p0/pa**→Mach-disk spacing* |
| 180 | wind_local[3] | f32 | wind at vehicle [m/s] world — ***wind-streamer flags (honest instruments)*** |
| 192 | a_body[3] | f32 | sensed accel [m/s²] body — *HUD g-meter* |
| 204 | qdot_heat | f32 | stagnation heat rate [W/m²] — ***grid-fin entry glow*** (D-010 #3) |
| 208 | Q_heat | f32 | integrated heat load [J/m²] — ***soot accumulation state*** |
| 212 | t_go | f32 | time-to-go [s] — *HUD, impact-marker coast time* |
| 216 | dist_pad | f32 | slant distance to pad [m] |
| 220 | **pred_impact[2]** | f32 | **v3** predicted impact world XY [m] — ***diegetic marker that slides onto the pad*** (D-010 #1 / D-013) |
| 228 | **ignite_h** | f32 | **v3** landing-burn ignition altitude [m] — *horizon line / "burn about to light" cue* |
| 232 | deploy_frac | f32 | 0..1 leg deploy — ***telescope the 4 legs*** |
| 236 | stroke[4] | f32 | per-leg crush stroke [m] — ***touchdown crush + crunch audio scale*** |
| 252 | f_aero[3] | f32 | aero force [N] world — *force viz* |
| 264 | deck_z | f32 | deck heave [m] — *valid iff SEA_ACTIVE* — ***ASDS deck vertical*** |
| 268 | deck_quat[4] | f32 | deck attitude xyzw — ***ASDS deck pitch/roll*** |
| 284 | plan_n / cloud_n | u16/u16 | tail counts (≤64 / ≤128) |

**Tails** (tightly packed after the 288-B head):
- `plan[plan_n]` — `BlPlanKnot { r[3] f32, throttle f32 }` (16 B) → **ghost line**,
  colored by planned throttle. Fresh each guidance tick.
- `cloud[cloud_n]` — `BlCloudSample { xy[2] f32, weight f32 }` (12 B) → **possibility
  cloud** + fitted 2σ dispersion ellipse.
- **⚠ Not yet emitted:** `fill_tlm` currently hard-codes `plan_n=0, cloud_n=0` ("MPPI
  tails not wired yet"). The decoder handles them; the server does not fill them yet.

### A.4 EVT channel — the trigger bus (`BlEvt {step u64, t f64, code u16, args f32[6]}`)

All wired and emitting in `--serve`. Audio schedules **propagation-delayed** sound from
these; the director cuts on them; the HUD ticker prints them.

| code | args | renderer/audio use |
|---|---|---|
| PHASE_CHANGE | new, old phase | director beat |
| IGNITION_CMD | n_eng | ignition overpressure thump |
| GREEN_FLASH | — | **TEA-TEB boron-green burst** at bell (~0.3 s) |
| ENGINE_START / ENGINE_SHUTDOWN | n_eng | plume on/off, shutdown pop |
| **MACH1_CROSS** | **r_emit[3]** (emission XY pos) | ***the triple sonic boom*** — schedule from emit pos + camera distance |
| LEG_DEPLOY | — | pneumatic thunk + latch |
| TOUCHDOWN | v_impact, tilt | crush crunch (scaled by `stroke[]`) + structural groan |
| VERDICT | grade | endgame card |
| GUST | vec[3] | (disturbance feedback for viz) |
| FAULT | type | fault annunciator |
| TARGET_CHANGED | pad | divert re-target |
| RCS_PULSE | mask | hiss-thump, pan by pod |
| SOLVER_DEGRADED | — | overlay warning |

### A.5 HELLO (build the scene once) + STATS (HUD ribbon)

**HELLO** `BlHello` (72 B) — the renderer builds mesh/pads/cameras from this:
`t0, seed, dt, tlm_hz(125), tlm_decim, run_idx`, geometry `veh_len, veh_dia, leg_span,
pad_radius, deck_z, pc_ref`, caps `plan_max(64), cloud_max(128)`, `scenario,
guidance_mode, modules` (module mask MOD_*).
- **⚠ Gap vs canon §10.2:** canon specifies a *richer* HELLO (full §5 geometry struct +
  vehicle hash, a multi-row **pad table** `{id, center xy, radius}` with PAD_A@origin,
  PAD_B@(250,−180), environment/wind/sun/time-of-night). The **shipped** HELLO carries
  only the scalars above — **single pad radius, no pad table, no vehicle hash, no
  sun/wind/time block.** Designer must either derive scene dressing from constants or
  request a HELLO extension (schema-early doctrine, D-011, permits this).

**STATS** `BlStats` (48 B): `max_qbar, peak_qdot, fuel_kg, twr, tlm_seq, fps_emit`.
Feeds the engineering overlay. (Canon §10.5 also wants solver p50/p99/ESS/λ/cost
breakdown/dropped-frame count — **not in the shipped STATS struct**; would need
extension when the MPPI solver stats matter.)

### A.6 The one coordinate conversion (`frame.ts`, already frozen & tested)

Sim = right-handed **Z-up** (X east, Y north). three = **Y-up**.
`(x,y,z)_three = (x, z, −y)_sim`; quaternion is the **same permutation**
`q_three = (qx, qz, −qy, qw)`. **This is the ONLY place conversion happens**, verified to
1e-6 against App-C vectors (frame.test.ts, 22 cases green). Do not hand-derive variants.

### A.7 Enums

- **Phase:** Init, Coast, EntryBurn, AeroDescent, LandingBurn, Touchdown, Settling,
  Landed, Tipped, Crashed, FuelDepleted, StructFail, ThermalFail, LOC.
- **Verdict:** None, LandedPerfect, LandedGood, LandedHard, Tipped, Crashed.

### A.8 Upstream commands (closed enumeration — bytes only, **never writes state**)

`RESET{seed,scenario,modules,[IC]}` · `PAUSE`/`RESUME`/`STEP{n}` ·
`SET_TIMESCALE{0.05–8}` (wall-clock only, never dt) · `SET_GUIDANCE_MODE{0/1/2}` ·
`SET_TARGET{pad_id}` · `INJECT_DISTURBANCE{type,mag,dir}` (gust, thrust deficit ±%, Isp
error, CoM offset, fin stuck, RCS pod out, sensor bias) · `SET_NAV_MODE{truth/noisy}` ·
`PING`. **`INJECT_DISTURBANCE` should be one keystroke away** — it is the "proof it's a
real controller" button (cloud scatters, ghost line snaps, vehicle re-plans).

---

## B. THE CINEMATIC CANON (checklist, with canon refs)

### B.1 Render stack (pinned; §11.1)

- [ ] `three@0.185.1` exact; `uplot` for 2D plots. **WebGPURenderer primary**, auto
  WebGL2 fallback; detect+log actual backend (`renderer.backend.isWebGPUBackend`) into
  HUD. (renderer.ts already does backend detect + AgX.)
- [ ] r183+ renames: `PostProcessing`→**`RenderPipeline`**, `Clock`→`Timer`;
  `AnamorphicNode` removed r185 (use bloom).
- [ ] Post chain (all `three/addons/tsl/display/*`): scene pass `mrt({output, emissive})`
  → **bloom on emissive target** (HDR plume core) → TRAA → optional MotionBlur (velocity
  MRT) + DoF (cinematic cams) → LensflareNode → ChromaticAberration (subtle) → FilmNode
  grain + vignette.
- [ ] **Tone mapping AgX** (`AgXToneMapping`) default; ACESFilmic behind a toggle.
- [ ] Depth: `reversedDepthBuffer:true` on WebGPU; `logarithmicDepthBuffer` on WebGL2
  fallback (re-verify at M7).
- [ ] **Floating origin:** authoritative fp64 in JS; render camera-relative, rebase when
  |camera offset| > 2 km. **Acceptance: 70 km→1 m in one continuous shot, no jitter.**
  (floatingOrigin.ts implements this.)
- [ ] Tauri v2 sidecar packaging: core as `binaries/booster-core`, CSP allows
  `ws://127.0.0.1:*`. Document dual-GPU iGPU-composite override (this box unaffected).

### B.2 Scene, sky, booster (§11.3–11.5, D-010)

- [ ] **Cape-flats RTLS default:** Ø60 m concrete pad w/ circle-X + weathering, scrub
  flats → water horizon, access road, distant hangar, floodlight ring (night).
- [ ] **Scorch persistence:** plume impingement accumulates into a decal RT that
  survives resets within a session — "the pad earns its soot."
- [ ] **Atmosphere:** `@takram/three-atmosphere@0.19.x` `/webgpu` (Bruneton +
  Hillaire) — makes 62 km read as *space* and 2 km as *Florida dusk* in one shot.
  Scenario sets sun elevation + time-of-night. Fallback: `SkyMesh` + altitude fade.
- [ ] **Stars:** `tools/make_stars.py`-baked HYG 9000-brightest B-V points (no binary
  assets — everything procedural/script-generated).
- [ ] **Booster procedurally from HELLO §5 dims:** tank + domes, interstage, octaweb + 9
  bells (center gimbaled by `gimbal_act`), 4 grid fins (alpha-carded lattice, hinged by
  `fins_act`), 4 telescoping legs (`deploy_frac`), RCS pods. TSL triplanar PBR.
- [ ] **Soot** driven by `Q_heat` with clean leg-shadow stripes + GG streak.
- [ ] **Bells NEVER glow** (regen-cooled — *encoded rule so nobody "improves" it*), faint
  red only after sustained burns, decaying post-shutdown.
- [ ] **LOX frost band** = `prop_lox` via tank column geometry (D-010 #2, telemetry-honest).
- [ ] **Grid-fin entry glow** keyed to `qdot_heat` (titanium; real F9) (D-010 #3).

### B.3 The plume — the crown jewel (§11.6; `plume.ts` exists, typecheck-clean)

Analytic raymarched volume in a cone-fitted proxy (custom TSL node), driven by
`(p_chamber/p_amb, throttle, mach, |v_rel|/C_T)`:
- [ ] **Shock cells:** first Mach-disk `x₁ = 0.67·D_e·√(p0/p_amb)`; spacing ∝ √(pressure
  ratio) — tight diamond ladder at SL, cells stretch/fade with altitude to 1–2 by ~35 km,
  then **balloon** to km-scale translucency (underexpansion). Diamond brightness =
  afterburning reignition. *(All implemented in plume.ts.)*
- [ ] **Color:** kerolox soot blackbody ramp (deep-orange core → yellow-white throat) +
  fuel-rich afterburn sheath + **dark sooty gas-generator streak** alongside the bell.
- [ ] **SRP mode** (entry burn, supersonic): plume does **not** trail — it **wraps forward
  and envelops the vehicle** with a bow-shock cap + plume-shedding flicker; blend by C_T
  exactly like the physics. *(Note D-010 #6: this is the same phenomenon as the D-009
  fin-shield — drawing it right visualizes the mechanism that hid the landing.)*
- [ ] **TEA-TEB green flash** on `GREEN_FLASH` EVT (~0.3 s boron-green, EVT-pulsed uniform).
- [ ] **Heat haze:** `viewportSharedTexture` screen-space refraction shroud above bells
  (canonical `webgpu_backdrop`; lives in a separate `heatHaze.ts`).
- [ ] **Plume as light (first among equals):** shadow-casting spotlight down plume axis +
  point light at bell; intensity/color from throttle & mixture; lights pad, vehicle base,
  dust. **At night this one system carries the scene.**
- [ ] Outer GPU-compute particles (`webgpu_compute_particles`) for soot wisps / vortices.
- [ ] *Optional M8 garnish:* small Eulerian fluid dome (`webgpu_volume_fire`, ~100³,
  active <500 m) behind a quality tier. Analytic plume is the deliverable.

### B.4 Ground interaction (§11.7)

- [ ] Impinging-jet from literature, driven by `(thrust, h)`: stagnation dome → **radial
  wall jet** with **Görtler-streak azimuthal banding** (real striated dust) → recirc
  lofting at low h; curl-noise GPU particles lit by plume; intensity ∝ thrust/h².
- [ ] RTLS: dust + grit + scorch-decal accumulation. ASDS: **steam/spray**, wet-deck
  reflectance boost, spray droplets on near cams. Ballistic debris pebbles (visual-only)
  on the first second of wall-jet.

### B.5 Weather & ASDS dressing (§11.8–11.9, §4.4)

- [ ] Presets: clear / haze / broken cloud, **wind-streamer flags reading `wind_local`
  (honest instruments)**, night/dusk/dawn/noon. Rain preset (M8): streaks + steam + lens
  droplets.
- [ ] **ASDS (SEA module):** Gerstner/FFT ocean **phase-locked to the simulated deck**
  (same sinusoid table from HELLO — deck pose is real sim state, streamed as
  `deck_z`/`deck_quat`; PM spectrum, SS4 Hs=2.0 m default). Droneship hull + deck
  markings + generic name plate.
- [ ] **Deck-cam feed-dropout easter egg:** vibration kills uplink at landing-burn peak —
  feed freezes, "SIGNAL LOST", restores after touchdown (default ON for ASDS; clean feed
  always recorded for replay).

### B.6 Cameras & director (§11.10)

- [ ] Rigs (**all renderer-side; never cross the boundary**): `PAD_TRACKER` (2 km
  long-lens, auto-zoom, seeing shimmer + focal breathing + operator lag/overshoot),
  `CHASE` (spring-arm), `ONBOARD_DOWN` (fisheye, soot on lens through entry, ignition
  exposure flicker), `ORBIT`, `FREE` (photo mode), `DECK_CAM` (ASDS), `DIRECTOR` (state
  machine cutting on EVT beats with **seeded per-run variety**).
- [ ] Replay scrubber runs any camera over the telemetry ring buffer; slow-mo via
  `SET_TIMESCALE` live or ring-buffer offline.
- [ ] **Long-exposure mode:** accumulate linear HDR across the descent → the iconic streak
  photo → PNG export. **Photo mode:** pause, free cam, DoF, 4× supersampled still.

### B.7 Data-viz — guidance made visible (§11.11; strictly from telemetry, each toggleable)

- [ ] **Ghost line:** `plan[]` as a fat polyline vehicle→pad, color-mapped by planned
  throttle; it writhes, snaps on disturbance, the vehicle chases it. *(needs plan tail
  wired — see §A.3 ⚠)*
- [ ] **Possibility cloud:** the 128 `cloud[]` endpoints as weight-alpha sprites + a
  breathing 2σ ellipse on the pad. *(needs cloud tail wired)*
- [ ] **Diegetic predicted-impact marker:** ground marker at `pred_impact`, slides onto
  pad as the solve tightens (D-010 #1 / D-013 — **fields live now**).
- [ ] Glideslope cone wireframe, velocity vector, attitude ladder, `ignite_h` horizon line.

### B.8 Audio doctrine (§12, D-011 item 1) — a **third pure observer**, its own client

Not part of the renderer. All WebAudio-synthesized (no samples), all EVT/telemetry-driven
with **propagation honesty**: per-source delay `d/343 s` (2.92 s/km), 1/r gain,
distance-lowpass (air absorption), Doppler via variable-delay slope, ground-reflection
comb. Observer = active camera; camera cuts crossfade propagation state (250 ms).
- [ ] **Engine:** sub-rumble (20–60 Hz) + roar (filtered brown noise) + **crackle**
  (Poisson steep-asymmetric shocklets, rate ∝ throttle, **positive skewness 0.1–0.5** —
  the measured signature). Distance softens crackle tearing→popcorn.
- [ ] **Sonic boom = a TRIPLE** (engine end · fins+legs merged mid shock · stage top),
  total <1 s, spacings ~0.12/0.18 s; N-waves lowpassed by distance; scheduled from
  `MACH1_CROSS` emit pos + camera distance. *"Three artillery cracks before the rumble."*
- [ ] RCS hiss-thump per `rcs_mask` bit; aero wind-shear whistle ∝ `qbar` + grid-fin
  flutter AM; TEA-TEB pop; leg-deploy thunk+latch; touchdown crunch scaled by `stroke[]`
  + groan (+ deck clang ASDS); post-landing vents + cooling ticks.
- [ ] **NEW spectral reveal (D-011):** frequency-dependent absorption from **our own US76
  model** — at 20 km slant range the rocket is pure infrasonic rumble; crackle fades in as
  range closes. The spectral reveal *is* the footage sound.
- [ ] Mix: per-group buses, compressor+limiter, **−16 LUFS**, ambient bed per preset.
  Audio context resumes on first interaction (**splash-screen click** — plan for it).

### B.9 Performance budget (RTX 4070 Ti SUPER, 1440p, **120 fps / 8.0–8.3 ms**, §11.12)

Scene+booster+terrain 1.2 · plume+haze 1.8 · particles(≤250k) 1.0 · sky 0.6 · shadows
0.8 · post 2.2 · HUD 0.4. Quality tiers **Ultra/High/Medium/Fallback(WebGL2)** scale
raymarch steps, particles, shadow res; disable fluid dome / motion blur first. Auto-scaler
drops one tier after 60 frames over budget; hysteresis 2 s. **Shadows scoped to a
near-ground cascade** so floating-origin rebase never fights shadow frusta (D-010 #5).

---

## C. WHAT EXISTS (the `ui/` scaffold — M3 pipeline, all real)

**Toolchain:** `three@0.185.1` + `@types/three@0.185.0`, `uplot`, vite 6, vitest 2.1,
TypeScript 5. `npm run gen:protocol` regenerates the decoder from protocol.h.

| file | status |
|---|---|
| `src/net/decode.ts` | **WORKS** — full v3 decoder, all 288-B fields + tails, magic/ver guards, tail clamps. (Header comment says "sizeof 276" — **stale text only**; the `TLM_FIXED_SIZE=288` const and every offset are correct v3.) |
| `src/net/decode.test.ts` | **GREEN (4)** — offset-sentinel round-trip incl. `predImpact`/`igniteH`, bad-magic, empty-tails |
| `src/net/frame.ts` | **WORKS** — the sole sim→three conversion |
| `src/net/frame.test.ts` | **GREEN (22)** — App-C basis + quaternion + commutation to 1e-6 |
| `src/net/interp.ts` | **WORKS** — ring buffer (600 s ≈17 MB history = the replay system), render-1-packet-in-past lerp/slerp, **hold** actuators, seq-gap `droppedFrames`, `raw` toggle. **No test file yet.** |
| `src/net/client.ts` | **WORKS** — direct WS, magic-routes TLM/HELLO/EVT/STATS, reconnect backoff, `send()` for upstream. `onHelloBytes/onEvtBytes/onStatsBytes` **not yet consumed** (wired at M7). |
| `src/scene/renderer.ts` | **WORKS** — WebGPU bootstrap, backend detect, AgX, reversed-z / log-depth fallback |
| `src/scene/floatingOrigin.ts` | **WORKS** — fp64 authoritative, 2 km rebase, `toRender()` |
| `src/scene/uglyScene.ts` | **PLACEHOLDER** — capsule(=booster) + plane(=pad) + grid + gnomon + north marker + basic sun/ambient. **This is what M7 replaces wholesale.** |
| `src/main.ts` | **WORKS (M3)** — wires renderer+client+interp+ugly scene+floating origin; naive follow camera; **throwaway HUD stub**. |
| `src/fx/plume.ts` | **EXISTS, typecheck-clean, NOT mounted** — full analytic raymarched plume TSL node (Mach disks, balloon, SRP forward-envelopment, kerolox ramp, GG streak, green flash). Ready to drop into the M7 scene. |

**Missing / stub (the frontend's greenfield):** procedural booster, sky/atmosphere,
stars, HUD proper, director/camera rigs, ground-effect, ASDS dressing, weather, data-viz
overlays (ghost/cloud/impact/glideslope), audio client, `heatHaze.ts`, `hello.ts`
(geometry decode), any window-shell/chrome. No `interp.test.ts`.

**`--serve` status (core side):** **fully wired** in `core/main.c` — HELLO once →
per-step EVT (all codes emitting) → TLM@125 Hz via `fill_tlm` → STATS every 12th frame.
Windows-only. **Two known holes:** (1) `plan_n=cloud_n=0` always (ghost/cloud tails not
filled yet); (2) HELLO carries scalar geometry only, **no pad table / vehicle hash /
env block** (canon §10.2 wants more). `--golden` emits HELLO/TLM/EVT hex; `--replay`
re-executes bit-identically. Live wire proven by `runs/ws_probe.mjs` (v3 fields decode,
formula reproduces bytes).

---

## D. THE LADDER (adopted build order + gating)

**Gate rule (directive 10):** **M6 (guidance ENTRY ≥90%) gates M7 (renderer first-light).**
Current standing (D-012): **ENTRY 88** — a measured plateau of the reactive structure;
the path to 90 is MPPI capacity (K→1024 CPU probe → M5 CUDA), which is also the M4 path.

**What D-011's addendum says may proceed EARLY (schema-first doctrine):**
- The **telemetry schema** is the real coupling point → extend `protocol.h` early while
  one cheap client consumes it. **Already done:** D-013 landed `pred_impact`+`ignite_h`
  (v2→v3). The EVT channel already covers ignition/boom timing (audit confirmed sufficient).
- A **renderer-agnostic web validation client** may be built to prove protocol / observer
  model / interpolation / replay / camera logic / basic vehicle+pad+exhaust sprite + HUD +
  a Tier-A audio **sketch** — target "credible documentary view, not AAA polish," then
  **STOP.** Most of this transfer-layer is already vitest-green (decode/frame/interp).

**The rungs (each independently shippable; the core never learns — D-011 items 2/3/6):**
1. Finish `--serve` renderer **first-light** (web, WebGPU/TSL): plume →
   propagation-delayed sound → **ONE volumetric cloud-deck punch-through at 2–5 km**
   (promoted to a first-class beat — strongest altitude cue) → one great long-lens ground
   camera. **The M7 "first light" 80/20 order** (D-010 #4). Reference: indistinguishable
   from LZ-1 tracking footage. (Needs a **real WebGPU browser** — headless preview hangs.)
2. **Tier-A Web Audio** synth (THREE.AudioListener + PositionalAudio/HRTF, ConvolverNode
   IR, AudioWorklet DSP) — the honest ceiling is excellent binaural over headphones.
3. **UE 5.8 spike — early & cheap:** a thin plugin (3rd static-asserted `protocol.h`
   mirror) decoding HELLO/TLM, driving a placeholder mesh at 60 fps with Large World
   Coordinates. Days, not weeks (MCP-assisted). De-risks the engine choice.
4. Blender-MCP model prep + Nanite import.
5. **Niagara plume with FluidX3D bakes.**
6. MetaSounds port (native telemetry-driven synth, OS Atmos free).
7. **Aero-table regeneration** (FluidX3D DNS → CA/CNα/CoP tables) as the final physics
   payoff.

**Audio tiers:** Tier A = browser WebAudio (above). Tier B = native C sidecar —
`ISpatialAudioClient` (object-based Windows Sonic/Atmos/DTS:X) + Steam Audio (HRTF,
occlusion, reflection, ambisonics). FMOD/Wwise are alternates.

**FluidX3D (offline only, both roles):** (a) bake plume + ground-impingement vector
fields/VDB → Niagara; (b) regenerate the plant's aero coefficient tables (ledger-grade
provenance).

**THE HARD LINE (contract, canon-grade):** CFD and UE **must never close a runtime loop
into dynamics.** Precompute in, telemetry out, always. The moment the pretty half feeds
state back, determinism + the memcmp oracle + the anti-cheat thesis die.

**Doctrine reframe:** this is **never a migration — we ADD CLIENTS.** The web renderer
stays forever as the fast, agent-iterable, browser-embeddable debug view; UE becomes the
IMAX theater. Same stream; the core never knows. **The cinematic maximalism (plume,
volumetrics, full audio production) is built ONCE, in the winner (realistically UE) — the
expensive layer is never paid twice.**

---

## E. OPEN DESIGN QUESTIONS (not canonized — the frontend designer decides)

Canon exhaustively specifies the *scene, plume, cameras, audio, and data-viz*, but leaves
the **application shell and interaction design** entirely open. None of the following is
prescribed anywhere:

1. **Window shell / chrome.** Tauri window: title bar, menus, panels, or borderless full
   cinematic canvas? Docked tool panels vs floating vs overlay-only? Multi-monitor?
2. **HUD layout & visual language.** The M3 HUD is a throwaway monospace box. Canon lists
   *what* data exists (phase, verdict, g-meter, alt/vel/mach, throttle, dropped frames,
   solver stats) but **not the layout, typography, skin, or diegetic-vs-flat treatment.**
   NASA-broadcast style? Minimal corner readouts? Full mission-control panel?
3. **Camera UX.** How does the user pick/drive rigs — hotkeys, on-screen dial, gizmo? Is
   DIRECTOR the default with manual override, or manual-first? Free-cam control scheme?
   How is auto-zoom/focal-breathing exposed vs automatic?
4. **Timeline / replay UX.** The 600 s ring buffer *is* the replay system, but the
   **scrubber UI, transport controls, timeline markers (EVT beats?), speed control
   (`SET_TIMESCALE` slider vs offline playback), and PNG/long-exposure export triggers**
   are undesigned.
5. **Multi-view.** Canon mentions deck-cam-with-dropout and long-lens simultaneously in
   spirit; whether the app supports **split-screen / PiP / a wall of cameras** is open.
6. **Control-panel for the closed command set.** `RESET`/scenario/seed picker,
   guidance-mode toggle, `SET_TARGET` (divert), and especially the **one-keystroke
   `INJECT_DISTURBANCE`** — surfaced how? Command palette? Physical-looking console?
7. **Settings surface.** Quality-tier selector, tone-mapping toggle (AgX/ACES), audio
   sliders + LUFS, weather/scenario/preset pickers, backend/build-info display.
8. **Data-viz toggles.** Each overlay (ghost, cloud, impact marker, glideslope, velocity,
   attitude ladder, force) is independently toggleable — the **toggle UI and default
   on/off set** are unspecified.
9. **Connection/lifecycle UX.** Sidecar spawn state, "waiting for core", reconnect
   feedback, seq-gap/dropped-frame surfacing, `SIGNAL LOST` styling.
10. **Onboarding / splash.** Audio needs a first-interaction gesture — is the splash a
    branded title card, a "click to launch" gate, or a scenario chooser?
