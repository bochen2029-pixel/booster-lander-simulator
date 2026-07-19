# PERCEPTION DESIGN — the honest target-acquisition front-end (VLM + laser rangefinder)
### The "perception-to-policy" sensing layer: the vehicle SENSES where to land instead of being told.

**Author:** PERCEPTION (Opus 4.8, id `tgnshagk`) · **Date:** 2026-07-19 (night, post-D-016) · **Status:** implementer-grade design + static analysis. NO code changed; no cmake/binary run against the real tree.
**Peer lanes:** `neuralpolicy` (tloczreb) — the guidance policy my target estimate feeds; `interplanetary` (rinv3b3k) — Mars vision-site-selection, the generalization of §F. Sibling doc: **`runs/target_sandbox_design.md`** (TARGETDESIGN) — the movable-target substitution + two-mode determinism fence this design PLUGS INTO (I inherit its `target_xy` slot; I replace its *source*).
**Operator context:** the operator is fluent with VLMs (Qwen 3.5 9B native-vision proven locally at `C:\models` via `C:\llama.cpp`). This is the *easier* half for them — so this doc is rigorous but concise; the hard rigor (the closed-loop guidance that consumes my estimate) lives in the `neuralpolicy` lane.

> **DESIGN + ANALYSIS ONLY.** Every injection is specified by file+function+line-neighborhood so a build agent implements without re-deriving. Canon it must respect (cited from source): directive 2 (determinism sacred, `HANDOFF_2026-07-18_NIGHT.md:50-52`), directive 3 (guidance can't cheat — no oracle, `:52`), directive 5 / D-011 §5 (renderer is a PURE OBSERVER, precompute in / telemetry out, the HARD LINE, `:60-61`), directive 7 (one dynamics source), §4.3 (guidance reads LEGAL SENSORS not truth) vs §8.1 (deck pose is legal NavState). The determinism fence is TARGETDESIGN's two-mode split (`runs/target_sandbox_design.md:69-80`) reused verbatim.

---

## A. THE HONESTY THESIS (lead with this — it is the whole point)

### A.1 What the sim does TODAY is a cheat, and it is the LAST cheat in the anti-cheat project

The project's founding thesis is that **the guidance actually solves the landing in real time — no scripted trajectory, no assist terms, no clamps toward the pad** (directive 3, `HANDOFF_2026-07-18_NIGHT.md:52`; "if guidance can't solve it, the vehicle crashes"). Every disturbance the vehicle fights — wind, gusts, aero, mass dispersion — is a HIDDEN quantity guidance never reads directly; it *feels* it through state (canon §4.3; the MPPI planner even zeroes `env.wind_world` in its rollouts so it re-solves against the induced drift alone, `sim.c:66-68`). That discipline is the soul of the project.

**There is one place this discipline is silently violated: the target itself.** Today the pad is the world origin `(0,0)` and guidance nulls `r_xy = (y[S_RX], y[S_RY])` — the vehicle's own surveyed position (`guidance_hoverslam.c:84`; `scenario.c:56-58` — there is no pad coordinate anywhere in the tree). For a fixed pad at the origin this is fine (a surveyed landing site IS known). But the moment the target **moves** — TARGETDESIGN's whole feature — the sim's answer is to hand guidance the target's TRUE coordinate: either the seeded closed-form `sea_deck_pose(t)` (Mode 1) or the operator's drag vector (Mode 2). **That true target coordinate is an ORACLE.** A real rocket has no voice in its ear saying "the pad just moved to (x, y)." A droneship 2 km downrange does not telepathically inform the descending booster of its GPS-truth. **Handing guidance the target's true position is exactly the class of cheat the entire project exists to forbid — it is `v_wind`-in-the-guidance for the *target* instead of the *wind*.**

### A.2 The honest ways to know where the target is: SENSORS, not oracles

A real flight computer learns the target's position through **sensors**, in exactly two flight-proven ways:

1. **BEACON / transponder (the baseline honest path).** The pad *broadcasts* its position — GPS + a differential correction, or an RF transponder the vehicle ranges against. **This is literally how the SpaceX droneship works:** the ASDS holds station on GPS/thrusters and transmits its state; the booster's flight computer receives a *telemetered* pad position. This is honest because it is a **received message with sensor error**, not truth — the transponder has noise, latency, dropout. It is the simplest honest mechanism and it maps *perfectly* onto the existing plant: it is the wind estimator's opposite (the wind is un-transmitted, so §4.3 forbids reading it; the deck pose IS transmitted, so §8.1 explicitly permits it: *"Deck pose (ASDS) is part of NavState... the real system knows the deck,"* cited at `runs/target_sandbox_design.md:67`). **Spec it as the baseline (S0):** it turns TARGETDESIGN's truth-oracle into a *noisy sensed beacon*, and it is trivially gate-clean.

2. **VISION / TRN (the general, language-addressable path).** The vehicle **sees** the target and localizes it from its own camera + a rangefinder. **This is flight-proven:** Mars 2020 Perseverance's **Lander Vision System (LVS)** did exactly this — a downward camera matched terrain against an onboard map (Terrain-Relative Navigation) to pick and steer to a safe touchdown, autonomously, in the final seconds of EDL. Vision is harder than a beacon (it needs a camera, a perception model, geometry, gating) but it is **general** (works on an un-instrumented pad, a natural clearing, a Mars crater floor — anything with no transponder) and, uniquely, it is **language-addressable**: a VLM can take a natural-language instruction ("land on the flat area left of the ridge") and ground it into an image region, then a bearing. That is the operator-facing feature.

### A.3 The thesis, crisp: VLM perception is a PURITY UPGRADE, not a compromise

The naive reading is "the VLM is slow and fuzzy, so bolting it on *weakens* the clean deterministic sim." **The opposite is true.** Today's clean sim contains a hidden oracle (the true target coordinate). The perception layer **REMOVES that oracle** and replaces it with a sensor that has honest, bounded error:

> **truth-origin target (cheat) → seeded/scripted true target (still a cheat: an oracle voice) → SENSED target (honest: a beacon with noise, or a camera + rangefinder + a VLM that can be WRONG).**

The vehicle now earns the target the same way it earns everything else — through imperfect sensors it must fuse and gate. When the VLM mis-grounds, the vehicle can aim at empty ground and MISS — and *that honest failure is the proof the loop is real* (directive 3: if it can't solve it — including "can't correctly perceive it" — it crashes). This closes the anti-cheat thesis on its last open front. **The perception layer is the final removal of an oracle, and it is the one that lets a human speak to the vehicle in words.** That is the frame for everything below.

**One-line positioning vs the sibling doc:** TARGETDESIGN answers *"how does a moving target enter guidance and get drawn"* (the `target_xy` substitution + the determinism fence). PERCEPTION answers *"where does `target_xy` HONESTLY come from"* — it replaces the oracle source with a sensor. The two compose: TARGETDESIGN is the socket; PERCEPTION is the honest plug.

---

## B. THE PIPELINE — sensor-camera → VLM bearing → rangefinder range → localize → track → estimate

The full perception chain, as a plant-side SENSOR SUITE that produces a target estimate `(target_xy, covariance)` for guidance. Five stages; the crux (async determinism) is §C.

```
   ┌─────────────────────────── THE PLANT (deterministic, 500 Hz) ─────────────────────────┐
   │                                                                                        │
   │   vehicle pose (truth) ──► [1] HEADLESS SENSOR-CAMERA ──► image I_k  (offscreen raster) │
   │   scene geometry (pad/deck/terrain)                        + camera pose C_k            │
   │                                                                │                        │
   │   operator language instruction L ────────────────────────────┤                        │
   └────────────────────────────────────────────────────────────────┼───────────────────────┘
                                                                     │  (crosses to async)
              ┌──────────────── ASYNC PERCEPTION (slow, GPU, non-deterministic) ─────────────┐
              │  [2] VLM ACQUISITION:  (I_k, L) ──► image region ──► BEARING b_k + confidence  │
              │       (Qwen3.5-9B + mmproj vision tower, via llama.cpp)                        │
              │  [3] LASER RANGEFINDER (sim sensor): range r_k ALONG bearing b_k (geometric)   │
              │  [4] LOCALIZE:  target_3D_k = veh_pos_k + r_k · bhat_k   (vehicle's OWN pose)  │
              └───────────────────────────────────┬───────────────────────────────────────────┘
                                                  │  emits a per-acquisition RECORD ──►  TRACE (logged)
   ┌──────────────────────────── THE PLANT again (deterministic) ────────────────────────────┐
   │   [5] TRACKING FILTER (Kalman): replay the TRACE at guidance rate; predict between        │
   │       acquisitions ──► smooth target estimate  x̂ = (target_xy, target_vxy) + covariance P  │
   │                                          │                                                 │
   │                                          ▼                                                 │
   │       fills  s->target_xy / s->target_vxy  ──► GuidanceCmd ──► @neuralpolicy GUIDANCE      │
   │       (the TARGETDESIGN socket, guidance_hoverslam.c:84 / MPPI shifts)                     │
   └──────────────────────────────────────────────────────────────────────────────────────────┘
```

### B.1 [1] The headless SENSOR-CAMERA — a plant sensor, NOT the cinematic observer

**This is the directive-5 subtlety, and it must be stated precisely.** The `ui/` renderer (three.js/WebGPU; `uglyScene.ts`, `documentaryScene.ts`, `markers.ts`) is the **pure OBSERVER**: it consumes the one-way telemetry stream and draws, and it must NEVER feed anything back into dynamics (D-011 hard line, `HANDOFF_2026-07-18_NIGHT.md:60-61`). **The sensor-camera is a DIFFERENT thing that happens to share scene geometry.** It is a stripped, offscreen raster of the world **from the vehicle's downward camera pose**, computed **inside the plant** as part of the SENSOR suite — the visual analog of the laser rangefinder or the IMU. It produces a pixel buffer that STAYS on the plant side and feeds perception. It is NOT the renderer, does NOT read the renderer's state, and does NOT close a loop from the pretty half. The direction of dependence is the opposite of the forbidden one: **plant → sensor image → perception → target estimate → guidance**, all one-way, exactly like `plant state → nav.c measurement → guidance` (`nav.h:3-4`).

Why "reuses scene geometry" is safe and cheap: the *geometry* (pad plane at `deck_z`, the KESTREL-9 booster proxy 47.7 m × 3.66 m dia, terrain, deck rectangle) is **data** — the same mesh the observer draws, but the sensor-camera rasters it from a plant-owned camera matrix. Sharing a mesh definition is not sharing renderer *state*; it is two consumers of one geometry table (the HELLO packet already exports the invariant geometry constants for exactly this "one source of truth" reason: `pad_radius`, `deck_z`, `veh_len`, `veh_dia`, `leg_span`, `protocol.h:172-177`).

**Concrete raster (three tiers, pick by fidelity vs speed):**
- **Tier-0 (S0/S1 default): a tiny CPU rasterizer in C.** The scene at target-acquisition altitude is almost trivial: a textured ground plane + a pad ring / deck rectangle + optional terrain heightfield, seen from above. A ~256×256 (or 320×240) offscreen framebuffer, painter's-algorithm or z-buffer, projecting the handful of scene primitives through the camera matrix `C_k = K · [R|t]` (intrinsics `K` from a chosen FOV; extrinsics from the vehicle pose `y[S_RX..S_RZ]`, `y[S_QX..S_QW]` + a fixed camera mount transform). This is a few hundred lines of C, deterministic, fast (<1 ms). It is deliberately austere — but it is enough for the VLM to ground "the pad" / "the ship" / "the H" against a textured deck vs surrounding water/terrain.
- **Tier-1 (S1 polish, optional): the CUDA raster via `C:\buddhabrot_cuda`.** The operator has a full CUDA rendering project (`C:\buddhabrot_cuda`: `src/`, `build/`, CMakeLists, a GPU tile-accumulation raster pipeline). Its device-side framebuffer + camera-projection machinery is directly repurposable for a higher-fidelity sensor-camera (textured deck, sea specular, terrain shading) at high frame rate — useful if the sim-to-real domain gap (§E.2) needs a richer image than the CPU tier can draw. sm_89, fp-controlled — the same GPU-determinism discipline the M5 MPPI CUDA port already established (`-fmad=false`, fixed reductions; `runs/agentB_mppi_design.md` §5; D-015 got 1-ULP parity).
- **Tier-2 (S2 / demo): render-to-texture off the WebGPU observer.** For the fenced live demo ONLY, the browser can grab the observer's own framebuffer as the "camera image." This is convenient for a live vibe-instruct session — but it is FENCED (§C.4): it is not gate-clean (it depends on the renderer), so it lives strictly in the interactive mode, never in a golden path.

**Camera model (spec):** a downward-canted body-fixed camera, FOV ~40°×30° (a nav-cam-like frustum), mounted along the vehicle's -Z-ish look with a fixed pitch so the target is in-frame during descent. Intrinsics `K` fixed per session (exported in HELLO alongside geometry). The camera pose is a pure function of the vehicle pose — **no new state**, no integration.

### B.2 [2] VLM ACQUISITION — image + instruction → target BEARING (referring-expression grounding)

The VLM's job is **referring-expression grounding**: given the sensor image `I_k` and a natural-language instruction `L` ("the droneship", "the H pad", "the flat area left of the ridge"), return the **image region** (a point or bbox) that `L` refers to, which the camera model converts to a **bearing** `b_k` (a unit ray in the world frame from the camera through that pixel). Plus a **confidence** and enough for **multi-frame consistency** (§E.1).

**Model + serving (the operator's proven local path, referenced concretely):**
- **Qwen 3.5 9B native-vision** (early-fusion multimodal), weights at `C:\models\Qwen3.5-9B-Q5_K_M.gguf` (6.13 GB) + the vision tower `C:\models\mmproj-F16.gguf` (0.86 GB). Served via `C:\llama.cpp\llama-server.exe` (OpenAI-compatible HTTP endpoint, image + text in, JSON out) or `C:\llama.cpp\llama-mtmd-cli.exe` (one-shot multimodal CLI). Both accept the mmproj vision projector — this is the operator's established VLM path. (`llama-qwen2vl-cli.exe` is also present as a Qwen-VL-specific runner if the generic mtmd path needs a fallback.)
- **The perception service is a separate process** (a llama-server instance the plant talks to over localhost HTTP, OR an mtmd-cli invocation per acquisition). It is emphatically NOT linked into the deterministic sim binary — it is the async precompute (§C). The plant writes an image + prompt; the service returns a grounding record.

**The prompt/response contract is §D.** The output the plant consumes per acquisition is a small **acquisition record**:
```
{ pixel_uv: [u, v],        // grounded target center in image coords (or bbox center)
  bbox: [u0,v0,u1,v1],     // optional, for angular-size cross-checks (§E)
  confidence: 0..1,        // model's calibrated grounding confidence
  label_echo: "droneship", // what it thinks it locked (for the operator + consistency)
  frame_id: k,             // which sensor frame this grounds
  cam_pose: C_k }          // the vehicle pose at capture (so localization uses the RIGHT pose)
```
The bearing is then `bhat_k = normalize(R_k · K^{-1} · [u, v, 1])` — the world-frame ray through the grounded pixel, using the camera pose `C_k` **at capture time**.

### B.3 [3] The laser RANGEFINDER — range along the bearing (a sim sensor)

A VLM gives a *direction*, not a *distance*. To place the target in 3D we need **range along the bearing**. A **laser rangefinder** (lidar altimeter / spot ranger — flight-standard on landers, e.g. the descent lidar on lunar/Mars landers) provides it: point the beam along `bhat_k`, get the range `r_k` to first surface intersection.

**As a SIM sensor it is a geometric ray-cast into the plant scene** (the same geometry the sensor-camera rasters): intersect the ray `veh_pos_k + t·bhat_k` with the ground plane at `deck_z` (and the deck rectangle / terrain heightfield if present), return `t*` at the nearest hit, plus a small sensor noise term drawn from the Philox stream (a dedicated RNG lane, like `nav.c`'s `NAV_LANE_*`). This is **deterministic and cheap** (closed-form ray-plane intersection; ~microseconds) and it runs **on the plant side at the acquisition instant** — it does NOT need the VLM; it only needs the bearing the VLM produced. Range noise ~0.5–2 m σ (realistic lidar), a max-range gate, and a "no-return" flag (beam misses all geometry → the acquisition is rejected, §E).

**Why the rangefinder is also the geometric SANITY CHECK (key):** the VLM can hallucinate a target on empty ground. But the rangefinder shoots the ACTUAL bearing into the ACTUAL scene. If the VLM points at open water/terrain with no deck, the range still returns *a* surface, but the localized point will be inconsistent (wrong height, or far from any known landable feature, or wildly inconsistent frame-to-frame). The range gives an independent geometric witness that catches "confident nonsense" — see §E.

### B.4 [4] LOCALIZE — target_3D from the vehicle's OWN pose (this is the honesty)

```
target_3D_k = veh_pos_k + r_k · bhat_k
```
where `veh_pos_k` is the vehicle's own position estimate (the **nav view**, `nav.measure` — noisy under `--nav-noisy`, truth otherwise), `bhat_k` is the VLM bearing, `r_k` is the rangefinder range. **This is the crux of the honesty:** the target position is *derived from the vehicle's own sensors and its own (imperfect) knowledge of where IT is* — never read from a truth oracle. Error in the vehicle's pose, the bearing, and the range all propagate into the target estimate, exactly as they would on a real vehicle. We project onto the horizontal to get `target_xy_k` (the quantity guidance nulls), keeping `target_z_k` for the vertical/deck-height channel.

**Covariance propagation:** the localization Jacobian maps `(σ_pose, σ_bearing, σ_range)` into a target-position covariance `R_k` (a 2×2 for `target_xy`, or 3×3 with height). This `R_k` is what the tracking filter weights the measurement by, and it is what flows to guidance so the policy knows *how well it knows the target* (a wide `R_k` early, tightening as the vehicle nears and bearing/range sharpen). This is the perception analog of `nav.c` telling guidance the state is noisy.

### B.5 [5] The TRACKING FILTER — a Kalman filter tracking a MOVING target across re-acquisitions

Acquisitions arrive sparsely (every few seconds, §C) and noisily. Guidance runs at 50 Hz and wants a *smooth, current* target estimate — including for a **moving** target (a drifting deck, a dragged pad). A small **linear Kalman filter** bridges the gap:

- **State:** `x = [target_x, target_y, target_vx, target_vy]` (constant-velocity model; add `target_z, target_vz` for a heaving deck). This directly produces the `target_xy` AND the `target_vxy` that TARGETDESIGN's guidance socket wants (`target_sandbox_design.md:233` — the velocity-null extension for a moving target).
- **Predict** every guidance tick (50 Hz): `x⁻ = F x`, `P⁻ = F P Fᵀ + Q` (constant-velocity `F`, small process noise `Q` sized to the expected target maneuver — deck wander ±3 m over 6–10 s, or a drag rate).
- **Update** only when a fresh acquisition record lands: `x⁺ = x⁻ + K(z − H x⁻)`, `K = P⁻ Hᵀ(H P⁻ Hᵀ + R_k)⁻¹`, where `z = target_xy_k` from §B.4 and `R_k` is the localization covariance. Between updates the filter COASTS on the constant-velocity prediction — so a moving target is *tracked*, not just snapshotted.
- **Output to guidance:** `x̂ = (target_xy, target_vxy)` + covariance `P`. Fills `s->target_xy` / `s->target_vxy` (the TARGETDESIGN slot); `P` optionally rides to guidance (and to the wire for the renderer to draw an uncertainty ellipse — §C's money shot).

**Why a Kalman filter is the right tool and stays deterministic:** the filter is pure linear algebra on the (logged, deterministic) acquisition records — it has NO nondeterminism of its own. It is the exact analog of `nav.c`'s gyro-bias integrator: a small piece of stateful math, seeded/replayable, that lives on the plant side. It absorbs the VLM's slowness (predicts through the gaps), its noise (weights by `R_k`), and its occasional dropouts (a rejected acquisition is simply a skipped update — the filter coasts, and if too many are rejected, covariance grows and guidance knows the estimate is stale). It also gives the multi-frame consistency machinery a natural home (a gated update = an innovation test, §E.1).

---

## C. THE DETERMINISM FENCE (the crux — precompute-in / telemetry-out applied to PERCEPTION)

This is the section the whole design turns on, and it is where perception is HARDER than TARGETDESIGN's target-sandbox: TARGETDESIGN's seeded target is closed-form and instant (a `sea_deck_pose(t)` sum-of-sines evaluated inline). **The VLM is slow (hundreds of ms), runs on the GPU, and is NOT bit-deterministic** (llama.cpp CUDA sampling, batching, and floating-point reductions are not reproducible tick-to-tick at the 2 ms plant rate). Dropping a 300 ms non-deterministic GPU call into the 500 Hz `sim_step` loop would **destroy determinism, the memcmp oracle, and real-time pacing in one stroke** — a direct violation of directive 2. So the perception layer is architected around a strict fence.

### C.1 The insight: a VLM run is a PRECOMPUTE; its output trace is a deterministic INPUT

The project already has the exact pattern for this, and it is canon: **"precompute in, telemetry out"** (directive 5, D-011 §5). Wind is the template — `wind_sample` is a seeded closed form the plant reads as a deterministic INPUT (`sim.c:31-56`); a DIAL-A-GUST is an operator-set pulse that is a pure function of altitude, "no RNG, so it neither perturbs the seeded stream nor breaks replay" (`sim.h:11-19`). The gust is an *external input* the deterministic plant consumes without becoming non-deterministic.

**A VLM acquisition is the same category of thing: it is a PRECOMPUTE that produces a data trace, and the deterministic guidance loop REPLAYS against that trace.** Concretely:

- **The VLM runs ASYNC**, off the plant clock: an **initial acquisition** at the start of the approach, then a **re-acquisition every few seconds** (NOT per-tick). Each run consumes an image + instruction and emits an **acquisition record** (§B.2) stamped with the sim step `k` it corresponds to.
- **The sequence of records is a TRACE:** `[(step k₀, target_xy₀, R₀), (step k₁, target_xy₁, R₁), ...]` — a sparse, timestamped list of target measurements. This trace is **exactly analogous to a wind trace, a seed, or a recorded gust schedule**: it is a fixed, external input to the deterministic gate.
- **The deterministic guidance loop replays against the logged trace at full rate:** the Kalman filter (§B.5) runs *inside* the deterministic `sim_step` at 50 Hz, PREDICTING between records and UPDATING when a record's step is reached. Given the same trace, the same seed, and the same plant, the run is **bit-identical** — because the only thing the VLM contributed (the sparse records) is now a frozen input, and everything downstream (filter, guidance, dynamics) is deterministic. **The VLM's non-determinism is quarantined into the trace-generation precompute; the gate never sees it.**

This is the precise generalization of the wind-trace/seed discipline to perception: **the VLM output is a deterministic input to the gate, produced by a non-deterministic precompute that runs outside the gate.**

### C.2 The data flow, with the determinism line marked EXACTLY

```
  ══════════════════════ PRECOMPUTE (non-deterministic, off-gate, async) ══════════════════════
    plant emits (image I_k, instruction L, veh_pose_k)  ──►  VLM service (llama.cpp, GPU)
                                                              ──► grounding (pixel, conf, label)
    plant rangefinder ray-casts bearing ──► range r_k  (deterministic itself, but bundled here)
    localize ──► acquisition RECORD (step k, target_xy_k, R_k, conf)  ──► APPEND to TRACE file
  ──────────────────────────────────────────────────────────────────────────────────────────────
                                    │  the TRACE crosses the line  ▼
  ║═══════════════ THE DETERMINISM LINE ═══════════════════════════════════════════════════║
                                    │  (a logged trace: frozen data, like a wind trace / seed)
  ══════════════════════ THE GATE (deterministic, seeded, replayable, 500 Hz) ═══════════════════
    sim_step:  Kalman PREDICT @50 Hz  ──►  when step == a record's step: Kalman UPDATE with it
               ──► x̂ = (target_xy, target_vxy), P  ──► s->target_xy/vxy  ──► GuidanceCmd
               ──► guidance re-solves (hoverslam:84 / MPPI shifts)  ──► actuators  ──► plant
    fill_tlm:  target_xy, P, pred_impact, dist_pad  ──►  TELEMETRY OUT  ──►  observer (draws)
  ──────────────────────────────────────────────────────────────────────────────────────────────
```

**What crosses the determinism line, precisely:** ONLY the **acquisition trace** — a sparse list of `(step, target_xy, covariance, confidence)` records. That is frozen data (a file, or a pre-generated array), identical in category to a wind trace or an RNG seed. **Nothing else crosses.** The image never enters the gate; the VLM process never enters the gate; the GPU never touches the gate. The Kalman filter, guidance, and dynamics are all inside the gate and all deterministic. **Given the trace + seed, `--headless` replays byte-identical** — the same acceptance bar as every existing golden (`target_sandbox_design.md:344`).

### C.3 The two operating modes (TARGETDESIGN's fence, reused verbatim)

Perception maps onto TARGETDESIGN's two-mode split (`target_sandbox_design.md:69-80`) exactly, because the fence is the same fence:

| | **MODE 1 — REPLAY (deterministic, gate-clean)** | **MODE 2 — LIVE VIBE-INSTRUCT (fenced, determinism WAIVED)** |
|---|---|---|
| target source | a **pre-generated acquisition trace** (VLM run offline, once) | the **VLM running live** against the live sensor-camera + operator's typed instruction |
| when the VLM runs | ahead of time (precompute) → trace logged | in the loop, async, hundreds of ms, non-deterministic |
| determinism | **bit-exact replay** (trace + seed → identical) | **deliberately waived** (depends on live GPU timing + when the human typed) |
| goldens | **yes** — freeze a trace + its replay golden | **no — quarantined**, loud banner, no honesty claim |
| runs in | `--headless`, `--run`, `--serve` (replaying a trace) | a separate `--serve-perceive` mode ONLY |
| what crosses the line | the logged trace (frozen data) | the live target estimate (a plant input, like a drag) |
| honesty claim | full — "the vehicle sensed the target, replayable" | "toy / live demo, not the oracle, not a gate" |

**Mode 1 is the canonical, honest, golden-able capability** — it proves "the vehicle SENSED the target and re-solved" as a deterministic, freezable artifact. The offline VLM run generates a trace; `--headless` replays it bit-exact; the land rate on a sensed (not oracle) moving target is measured and reported with cross-seeds — a NEW golden-able capability, same as TARGETDESIGN Stage 1.

### C.4 Mode 2 — the fenced live "vibe-instruct" demo (the visceral one)

The visceral demo — operator *types* "land on the ship" and *watches the booster sense it and go* — is Mode 2, and it reuses TARGETDESIGN's **exact fence** (`target_sandbox_design.md:157-206`):
- A **separate CLI mode `--serve-perceive`** (sibling of `--serve` / TARGETDESIGN's `--serve-interactive`), NOT a flag — the mode name IS the quarantine label. It prints a loud banner: `LIVE PERCEPTION SANDBOX — VLM in the loop, determinism WAIVED, not a gate, no goldens.`
- **The inbound operator input is tiny and already-half-wired:** the operator sends a **text instruction** (and optionally clicks to seed a region). TARGETDESIGN established that `ws.c`'s `ws_poll_client` already receives + unmasks client→server frames and merely drops the payload (`target_sandbox_design.md:163-169`); the instruction rides that same minimal inbound surface (parse a small `BlInstruct` frame — a magic + a short UTF-8 string — from the already-unmasked `pay[]`). What crosses is an **operator INTENT (a text string)**, never renderer state — the same category as a joystick or a `DISTURB` command (`target_sandbox_design.md:80`).
- **The VLM runs live async** against the live sensor-camera (Tier-2 render-to-texture is permitted HERE only, §B.1), producing target estimates that feed guidance in real time. The run is non-deterministic BY DESIGN (live GPU timing) — so `--selftest`'s memcmp oracle is meaningless for it and never applied.
- **The honest bridge (recommended, like TARGETDESIGN's input-trace recorder):** record the live acquisition trace `(step, target_xy, R, conf)` to a file during the session. Then `--headless --replay-perception-trace FILE` replays it **deterministically and gate-clean** — promoting a good live "watch it sense the ship" moment into a golden-able Mode-1 artifact. This is the perception analog of TARGETDESIGN's `--replay-target-trace` (`target_sandbox_design.md:205`) and it is the clean path from sandbox to golden: **run the VLM live once, capture the trace, replay it forever deterministically.**

**The fence in one sentence:** the VLM (slow, GPU, non-deterministic) NEVER runs inside the deterministic gate; it runs as an off-gate precompute (Mode 1) or in an explicitly-fenced non-gate mode (Mode 2), and only its output TRACE — frozen data — ever crosses the determinism line.

### C.5 The money shot (perception version): the marker AND the sensed target both converging

TARGETDESIGN's money shot is `pred_impact` chasing a moved target (`target_sandbox_design.md:272-289`). Perception ADDS a second convergence that makes the sensing visible: **the target-estimate itself converging as the vehicle nears.** Early in the approach the vehicle is far, the bearing is coarse, the range is long → the localization covariance `R_k` is WIDE (draw it as a big uncertainty ellipse around the estimated target). As the vehicle descends, bearing + range sharpen → the ellipse SHRINKS onto the true target, and `pred_impact` (which the guidance drives) converges onto the tightening estimate. The renderer draws THREE things now: (1) the sensed-target estimate + its shrinking covariance ellipse (the "the vehicle is figuring out where the ship is" beat), (2) `pred_impact` re-solving onto it (the existing beat), (3) the vehicle. **The visible story is no longer just "it re-solves" — it is "it PERCEIVES the target, uncertain at first, then locks on, and re-solves onto what it perceived."** That is the perception thesis rendered. `markers.ts` already has the `pred_impact` convergence machinery (`ui/src/scene/markers.ts:60-69` `solveConvergence`); the covariance ellipse is one added glyph fed by the `P` field on the wire (§C.6).

### C.6 The one honest protocol addition (through the D-013 mirror+golden unit)

Perception wants the renderer to draw the *sensed* target + its uncertainty. It reuses TARGETDESIGN's proposed `target_xy[2]` field (so the two designs share ONE protocol addition, not two) and adds a compact uncertainty scalar/ellipse:
- **`float target_xy[2]`** (shared with TARGETDESIGN) — the estimated target the renderer draws.
- **`float target_cov[3]`** — the 2×2 covariance upper triangle `(Pxx, Pxy, Pyy)` → the uncertainty ellipse. (Cheap; 12 bytes.)
- **`uint8 perceive_state`** — an enum {NO_LOCK, ACQUIRING, TRACKING, COASTING, REJECTED} so the HUD shows the perception state honestly (e.g. "COASTING — no VLM return for 4 s").
- **Reuse `BL_EVT_TARGET_CHANGED`** (`protocol.h:226`, already reserved) for the discrete "new acquisition landed / operator re-instructed" beat.

**This goes through the exact D-013 protocol playbook (non-negotiable, `HANDOFF_2026-07-18_NIGHT.md:60-61` + `target_sandbox_design.md:295-299`):** append near the tail (after `deck_quat`@268, so existing v3 offsets 220/228/264 don't move), bump `BL_PROTO_VERSION` (coordinate the single bump with TARGETDESIGN so it's ONE version step, not two), update the `static_assert` offset pins (`protocol.h:238-252`), regenerate the TS mirror via `tools/gen_protocol_ts.py`, re-freeze `goldens/protocol/*.hex`, mirror in `decode.ts`. One validated unit. `fill_tlm` writes all three as pure reads of `s->target_xy` / the filter's `P` / `perceive_state` (`main.c:365` — directive-5-clean, no guidance feedback).

---

## D. THE LANGUAGE INTERFACE — natural language → image region → bearing (the operator-facing feature)

This is what makes the vehicle **tellable**: the operator TYPES where to land, in words, and the VLM grounds it. It is the natural-language front-end to the existing guidance — the operator speaks, the VLM turns speech into a bearing, the pipeline (§B) turns the bearing into a tracked target, and `neuralpolicy`'s guidance flies to it.

### D.1 The referring expressions the operator uses

Three classes, in rising difficulty for the grounder:
- **Named object:** "the ship", "the droneship", "the barge", "the H pad", "the landing pad" → ground the salient man-made target in the frame. Easiest (one obvious object on empty water/terrain).
- **Spatial-relational:** "the flat area left of the ridge", "the clearing past the crater", "the pad on the right" → ground a region defined *relative to* other scene features. Harder (needs scene parsing + spatial reasoning — a VLM strength).
- **Attribute/affordance:** "the flattest ground", "somewhere safe to land", "away from the rocks" → ground a *landability* judgment, not a named object. Hardest, and the most powerful (it is site-*selection*, the §F generalization — the operator delegates the choice).

### D.2 The prompt / response contract (spec)

**System prompt (fixed per session):** frames the VLM as a landing-site grounder and PINS the output format so the plant can parse it deterministically. Sketch:
```
You are the target-acquisition vision system of a landing rocket. You see a
downward camera image of the landing zone. The operator will name or describe
where to land. Return ONLY a compact JSON object grounding that instruction to
a point in THIS image. If you cannot confidently find it, say so.

Respond with EXACTLY this JSON, no prose:
{ "found": true|false,
  "point": [u, v],           // pixel of the target center, image coords (0..W, 0..H)
  "bbox":  [u0,v0,u1,v1],    // tight box around the target (or null)
  "label": "<what you locked onto>",
  "confidence": 0.0-1.0 }    // your grounding confidence
```
**User turn (per acquisition):** the image `I_k` + the instruction `L` (e.g. `"land on the droneship"`).
**Assistant turn (parsed):** the JSON → the acquisition record (§B.2). The plant validates: JSON parses, `found==true`, `point` in bounds, `confidence ≥ τ_conf`; else the acquisition is REJECTED (the filter coasts, §E).

**Contract notes:**
- **Format-pinning is load-bearing.** llama.cpp supports **grammar-constrained / JSON-schema-constrained sampling** (GBNF); use it so the VLM CANNOT emit unparseable output — the response is guaranteed-valid JSON, which removes a whole failure class and is essential for an automated pipeline. (This is the operator's proven llama.cpp path — grammar files are a standard llama-server/mtmd-cli feature.)
- **Confidence must be USED, not decorative** — it is the primary gate (§E.1). If the model is poorly calibrated, replace/augment the self-reported confidence with a computed proxy (bbox stability across frames, grounding-vs-rangefinder geometric agreement) — the pipeline treats "confidence" as a fused quantity, not a raw model token.
- **Instruction persists across re-acquisitions** — the operator types once ("the ship"); every subsequent async acquisition re-uses `L` against the fresh frame. Re-instructing (typing a new `L`) is the discrete `BL_EVT_TARGET_CHANGED` beat (Mode 2), or a scripted instruction change in a Mode-1 trace.
- **Bearing conversion, not pixel-as-target:** the plant NEVER treats the pixel as the target directly — it converts `(u,v)` → a world bearing via the camera pose (§B.2), then ranges + localizes (§B.3-4). The VLM owns *direction from the image*; geometry owns *distance + world position*. This split is what keeps the VLM's job small (ground a region) and the honesty intact (the 3D fix comes from the vehicle's own geometry).

---

## E. FAILURE MODES + GATING (the rigor — a wrong acquisition must NOT quietly fly the vehicle into empty ground)

The perception layer's whole risk is **confident wrongness**: the VLM grounds "the ship" onto empty water, the pipeline localizes a target there, and guidance faithfully flies to nothing and crashes off-target. The defense is layered gating so a bad acquisition is CAUGHT, not obeyed.

### E.1 VLM mis-grounding / hallucination → confidence + multi-frame consistency + the rangefinder as geometric witness

Three independent filters, any of which can reject an acquisition (a rejected acquisition is a skipped Kalman update — the filter coasts on prediction, and covariance grows, so guidance *knows* the estimate is stale rather than being fed a lie):

1. **Confidence gate.** Reject if `confidence < τ_conf` (and the format-constrained `found==false` path). First and cheapest line.
2. **Multi-frame consistency (the Kalman innovation test).** A true target is *geometrically consistent frame-to-frame*: consecutive localized points cluster (and drift smoothly for a moving target). A hallucination jumps around. The tracking filter's **innovation** `ν = z − H x⁻` and its covariance `S = H P⁻ Hᵀ + R_k` give a **gated update**: reject the measurement if the Mahalanobis distance `νᵀ S⁻¹ ν > χ²_gate` (a standard track-gating test). One confident-but-wrong grounding that disagrees with the established track by many sigma is rejected outright. This is why the Kalman filter (§B.5) is not just smoothing — it is the consistency arbiter. Requires ≥2-3 acquisitions to establish a track; the initial acquisition is gated by confidence + the geometric check (below) alone.
3. **The rangefinder geometric sanity check (the independent witness — §B.3).** The bearing is shot into the ACTUAL scene. Reject if: the beam gets no return (points at sky/off-scene), OR the returned range implies a target *height* inconsistent with a landable surface (a deck/ground at the expected `deck_z` band), OR the localized point is implausibly far from any known-landable geometry. The rangefinder can't be fooled by pixels — it measures real geometry along the claimed direction — so it catches "the VLM is confident about open water." This is the geometric backstop behind the semantic (confidence) and temporal (consistency) gates.

**Fail-safe behavior when perception is untrustworthy (directive-3-honest):** if acquisitions are persistently rejected (covariance grows past a threshold, `perceive_state → COASTING/NO_LOCK`), the vehicle does NOT get a fabricated target. Options, in honesty order: (a) coast on the last good track (fine for a brief VLM gap); (b) if a **beacon** (S0) is also available, fall back to it (the honest degraded sensor); (c) if NOTHING is trustworthy, guidance flies the last estimate and may MISS — and that miss is the honest, correct outcome (directive 3: it couldn't perceive it, so it doesn't magically land). We NEVER clamp to a truth oracle to "rescue" a bad perception — that would re-introduce the exact cheat this design removes.

### E.2 The sim-to-real DOMAIN GAP (VLM trained on real photos vs the sim's austere look)

Qwen 3.5 9B was trained on real-world photographs; the Tier-0 sensor-camera draws an austere textured plane + pad ring. A grounder can under-perform on out-of-distribution synthetic imagery ("that flat gray quad doesn't look like a droneship to a model that knows real ASDS photos"). Mitigations, in effort order:
1. **Render realistic-ENOUGH (cheapest, do first).** The bar is not photorealism — it is "recognizable as the named object." A deck drawn as a labeled barge silhouette with an obvious landing bullseye / painted "H", on textured water, is groundable. The Tier-1 CUDA raster (`C:\buddhabrot_cuda`, §B.1) buys texture/shading/specular cheaply if Tier-0 is too abstract. Add the visual affordances real pads have (the SpaceX "X"/bullseye, an "H" helipad glyph) so the referring expressions ("the H pad", "the ship") have something literal to latch onto.
2. **Prompt the model toward the synthetic domain.** The system prompt can say "this is a stylized/simulated downward view; the landing target is the [barge/pad/marked area]" — telling the VLM the domain is synthetic measurably helps grounding.
3. **A light fine-tune (only if 1-2 are insufficient).** A small LoRA on a few hundred rendered sensor-camera images with grounded targets (auto-labeled — the sim KNOWS the true target pixel, so labels are free) adapts the vision tower to the sim's look. This is a last resort (it is real ML work), and the auto-labeling makes it cheap if needed. The operator's local Qwen + llama.cpp stack supports LoRA adapters.
4. **The rangefinder + consistency gates (§E.1) are the safety net regardless** — even a domain-gap-degraded VLM that occasionally mis-grounds is caught by the geometric + temporal filters, so the gap degrades *acquisition RATE* (more rejected frames → more coasting), not *safety* (it doesn't fly to garbage). This is why the gating architecture matters more than closing the gap perfectly.

### E.3 LATENCY BUDGET (why async, not per-tick — the quantitative justification for §C)

The numbers that force the fence:
- **Plant tick:** 2 ms (500 Hz), `HANDOFF_2026-07-18_NIGHT.md:50`. Guidance tick: 20 ms (50 Hz). MPPI replan budget: <100 ms (10 Hz), and the M5 CUDA path hits it (~65 ms fp64, D-016 / continuity §1.2).
- **VLM acquisition:** a 9B multimodal forward pass with a vision tower, image-encode + prefill + a short constrained JSON decode, on a single local GPU, is realistically **~200 ms to low seconds** per acquisition (image resolution, GPU, quant-dependent). That is **100–1000× the plant tick and ≥2-10× even the MPPI replan budget.**
- **Conclusion:** a synchronous per-tick (or even per-guidance-tick) VLM call is impossible — it would stall the plant by orders of magnitude and, being non-deterministic, would break replay even if it were fast. So the VLM MUST run **async, sparsely** (initial + every few seconds), and the fast deterministic loop must bridge the gap. That bridge is the Kalman filter predicting between sparse acquisitions (§B.5), and the determinism is preserved by replaying the logged trace (§C). **The latency budget is the reason the entire architecture is "slow async precompute → sparse trace → fast deterministic replay."** A moving target that drifts slowly (deck wander ±3 m over 6–10 s) is well-matched to a several-second re-acquisition cadence + constant-velocity coasting; a fast operator drag in Mode 2 is where re-acquisition latency is most visible, and the recommended answer is that Mode 2 is the *fenced* mode where such lag is acceptable (and the reachable-set framing of `target_sandbox_design.md:369` applies).

---

## F. PHASED BUILD (each stage independently gated)

**All in a `_perceive_wt\` worktree copy (CMakeLists + core; VS2022 x64 configure), gitignored. Never edit/build the real tree until a stage's gate is green.** Gates after EVERY build (HANDOFF §1.7, `:64-72`): `--selftest` = PASS; `--headless --scenario terminal --seed 42 --runs 200` = **exactly 194/200**; a determinism pair on the changed scenario; and — since this feeds the lateral law — the **MPPI single-run invariance check** (`--run --scenario aero_offset --seed 42 --run 1 --mppi` vs the `(1.5,3)` reference `td_v 2.63 / lat 10.48`, continuity §0). If TERMINAL moves, the change leaked — stop, fix. **This build depends on TARGETDESIGN's `target_xy` socket** (`GuidanceCmd.target_xy`, the hoverslam:84 substitution, the MPPI shifts) — sequence PERCEPTION after (or jointly with) TARGETDESIGN Stage 0, and coordinate the single protocol version bump.

### S0 — The deterministic sensor-camera + a BEACON target (honest, trivial, gate-clean) — proves the acquisition→guidance path

**Build:** (1) the headless Tier-0 CPU sensor-camera (§B.1) — offscreen raster of the scene from the vehicle camera pose (initially it need only produce a framebuffer + the camera matrix; the VLM is NOT wired yet); (2) the laser rangefinder ray-cast (§B.3); (3) a **BEACON target source** — the pad/deck TRANSMITS its position as a noisy sensed message (a `beacon_sample(s)` that returns `true_target_xy + Philox_noise`, deterministic, keyed like `nav.c`'s draws), fed through the SAME nav-view path into `s->target_xy`. NO VLM, NO language yet.

- **What S0 proves:** the ENTIRE acquisition→estimate→guidance→re-solve path works end-to-end with an HONEST (noisy, sensed — not oracle) target, deterministically. It replaces TARGETDESIGN's truth-oracle target source with a *sensed beacon* at zero risk. The Kalman filter (§B.5) is exercised on beacon measurements (a degenerate VLM-less acquisition record). The sensor-camera + rangefinder are built and validated geometrically (the range matches closed-form ray-plane truth).
- **Gate:** with beacon noise σ=0, EVERY baseline reproduces byte-exact (TERMINAL 194, ENTRY-mppi 95, AERO 44|60 — subtracting a zero-noise sensed target == the oracle target == TARGETDESIGN Stage 0). With beacon noise on, `asds_night` (the moving-deck scenario, `scenario.c:19`) lands on the SENSED moving target at a measured rate, deterministic (byte-exact replay), reported with cross-seeds. TERMINAL/AERO/ENTRY untouched (module-gated).
- **Why first:** it de-risks everything downstream — if guidance can't fly a NOISY beacon target, no amount of VLM sophistication helps. It also delivers real honest value on its own (the SpaceX-droneship-accurate baseline: a transmitted, noisy pad position) and is the fallback sensor for §E.1.

### S1 — VLM bearing + rangefinder localization (headless, logged-trace determinism)

**Build:** (1) the VLM acquisition service (§B.2) — `llama-server`/`llama-mtmd-cli` with Qwen3.5-9B + mmproj, grammar-constrained JSON output (§D.2); (2) the offline trace generator — run the VLM against sensor-camera frames from a recorded/seeded descent, localize each (§B.4), write the acquisition TRACE; (3) the deterministic replay path — `--headless --replay-perception-trace FILE` that runs the Kalman filter against the logged trace inside the gate (§C.1); (4) the full gating (confidence + innovation + rangefinder witness, §E.1). A fixed instruction (e.g. `"land on the droneship"`) for S1 — full free-form language is S2's demo.

- **What S1 proves:** the vehicle **SEES** the target (VLM bearing) + **RANGES** it (rangefinder) + **LOCALIZES** it from its own pose + **TRACKS** it (Kalman) + **lands on it** — and the whole thing is **deterministically replayable** from the logged trace (the crux, §C). This is the honest oracle-removal delivered as a golden-able artifact.
- **Gate:** the trace replay is **bit-identical** run-to-run (`--headless` twice, memcmp — the standard bar, `target_sandbox_design.md:344`); freeze a `goldens/mc/perceive_<scenario>_s42_baseline.txt` + a single-run full-capture golden of the sense-and-land trajectory. Measure + report (cross-seeds) the land rate on a VISION-sensed target vs the S0 beacon rate vs the oracle rate — the honest cost of perception. The VLM's non-determinism is OUTSIDE the gate (trace generation), so the gate stays clean.
- **Note:** S1's trace generation is a PRECOMPUTE (may run slowly, offline, in background — the idle-wait discipline of the fleet applies: run it as a self-driving background job, not an awaited call). The *gate* (replay) is fast + deterministic.

### S2 — The fenced live language-instruct demo (`--serve-perceive`, determinism WAIVED)

**Build:** `cmd_serve_perceive` (§C.4) — the live VLM in the loop against the live sensor-camera (Tier-2 render-to-texture permitted here), the `ws.c` inbound instruction sink (the minimal `BlInstruct` frame, TARGETDESIGN's surface), the WAIVED-determinism banner, `emit_evt(BL_EVT_TARGET_CHANGED)` on each re-instruction/acquisition, and the input-trace recorder + gate-clean replay bridge (§C.4). `--mppi` allowed on the serve path so the operator watches the REPLANNER chase the sensed target.

- **What S2 proves (a DOCTRINE gate, not a determinism gate):** the visceral feature — operator types "land on the ship" / "the flat area left of the ridge", watches the booster SENSE it (covariance ellipse shrinking, §C.5) and re-solve onto it, live. The proofs are TARGETDESIGN's doctrine proofs (`target_sandbox_design.md:355-361`): `--serve` byte-clean (inbound sink only in the perceive path), the deterministic gate untouched (separate mode, `--selftest`/TERMINAL/all goldens still match), the inbound surface minimal + audited (one validated `BlInstruct` text frame; the ONLY writers of `s->target_xy` are the seeded/beacon source, the trace-replay filter, and — in this mode only — the live perception poll), and the quarantine banner + a SEPARATE ADR stating it produces NO goldens and is outside every honesty claim.
- **Decision rule:** ship S2 iff the doctrine proofs hold. If ANY deterministic gate moves, the live perception path leaked into the pure tree — the #1 risk — stop and re-fence.

### Composition + generalization

- **With `neuralpolicy` (the guidance that consumes my estimate):** my output is `(target_xy, target_vxy, P)` filling `s->target_xy`/`s->target_vxy` — the TARGETDESIGN socket that `neuralpolicy`'s guidance (hoverslam:84 / MPPI shifts) nulls. The covariance `P` is available to a policy that wants to weight its aggressiveness by target certainty (fly conservatively while the estimate is wide, commit as it tightens) — a natural coupling to discuss on the `neuralpolicy` lane. The two lanes meet at the `GuidanceCmd.target_xy` interface: PERCEPTION produces it honestly, `neuralpolicy` consumes it to solve. **Coordinate: one protocol version bump covering both designs' fields.**
- **Generalization to `interplanetary` (Mars vision-site-selection):** this pipeline IS Mars EDL landing-site selection. Perseverance's LVS (§A.2) is the flight-proven precedent; the attribute/affordance language class (§D.1, "the flattest ground", "somewhere safe") is exactly autonomous hazard-avoidance site-selection. The generalization for `interplanetary`: swap the pad/deck target for terrain hazard-mapping (the VLM grounds "a safe flat spot avoiding the boulders/slopes" against a Mars-surface sensor image), keep the identical bearing→rangefinder→localize→track→guidance pipeline and the identical async-trace determinism fence. The rangefinder becomes the descent lidar (real Mars landers carry one); the "beacon" S0 baseline has no Mars analog (no transponder on unexplored terrain), which is precisely WHY vision matters there — it is the only honest site-knowledge on an un-instrumented world. **The perception layer is the Earth-ASDS proving ground for the Mars site-selection capability.** Flagged for the `interplanetary` lane.

---

## Appendix — canon / doctrine compliance restated

- **directive 2 (determinism):** the VLM never runs in the gate; only its sparse acquisition TRACE (frozen data, like a wind trace/seed) crosses the line; Mode-1 replay is byte-exact + golden-able; Mode-2 live is non-deterministic BY DESIGN, quarantined, no golden, with a gate-clean trace-replay bridge. ✓ (§C)
- **directive 3 (no cheat / no oracle):** the entire point — the truth-origin/oracle target is REMOVED and replaced by a sensed target (beacon or vision) with honest bounded error; a mis-perception can MISS, and that honest failure is preserved (never clamped to truth). ✓ (§A, §E.1)
- **directive 5 / D-011 §5 (pure observer, HARD LINE):** the sensor-camera is a PLANT sensor (plant→image→perception→guidance, one-way, like `nav.c`), NOT the cinematic observer; nothing flows from the renderer into dynamics; Mode-2's only inbound is an operator text INTENT (a plant input like a `DISTURB`), never renderer-derived state; Tier-2 render-to-texture is fenced to the non-gate mode. ✓ (§B.1, §C.4)
- **§4.3 vs §8.1 (what guidance may legally know):** the target's CURRENT position is a SENSED quantity (beacon = transmitted deck pose, §8.1-legal; vision = the vehicle's own camera+rangefinder fix) — never the un-transmitted truth the §4.3 wind-line forbids; the vehicle localizes from its OWN (noisy) pose, so nothing is read from an oracle. ✓ (§A.2, §B.4)
- **directive 7 (one dynamics source):** `target_xy` threads through the SAME `GuidanceCmd`/nav-view socket TARGETDESIGN specified; the MPPI rollout mirror gets the same estimate → profile-exact parity; the `target=origin` (zero-noise) invariance check proves no leak. ✓ (§B.5, §F-S0)
- **directive 9 (TERMINAL byte-identical):** all perception is module-gated (a `MOD_PERCEIVE`-style arm + the beacon/trace source off by default → `s->target_xy` zero → subtracting zero is a no-op); TERMINAL never touches any of it. ✓ (§F-S0 gate)
- **D-013 protocol process:** the `target_xy`/`target_cov`/`perceive_state` fields go through the full mirror+golden unit (version bump coordinated with TARGETDESIGN, static asserts, decode.ts, hex goldens, gen_protocol_ts.py — the D-013 playbook). ✓ (§C.6)

## Appendix — local-asset grounding (verified on disk, 2026-07-19)

- **VLM (the acquisition model, §B.2 / §D):** `C:\models\Qwen3.5-9B-Q5_K_M.gguf` (6.13 GB) + vision tower `C:\models\mmproj-F16.gguf` (0.86 GB) [also `k0cQwen3.5-9B.Q5_K_M.gguf` + a BF16 mmproj]. Served via `C:\llama.cpp\llama-server.exe` (HTTP, image+text) or `C:\llama.cpp\llama-mtmd-cli.exe` (one-shot multimodal); `llama-qwen2vl-cli.exe` present as a Qwen-VL-specific fallback. Grammar-constrained JSON is a standard llama.cpp feature (§D.2). — the operator's proven local VLM path.
- **Sensor-camera raster (§B.1 Tier-1):** `C:\buddhabrot_cuda` — a full CUDA rendering project (`src/`, `build/`, CMakeLists, GPU tile-accumulation raster) repurposable for a higher-fidelity device-side sensor-camera at sm_89, with the same GPU-determinism discipline as the D-015 M5 MPPI CUDA port (`-fmad=false`, fixed reductions).
- **Determinism template (§C):** `core/nav.{h,c}` — the measurement-layer contract my estimate follows (per-guidance-tick State view, Philox `RNG_NAV` keyed by (seed,step,run), NAV_TRUTH bit-transparent, gyro-bias as the one stateful-but-replayable term = the model for the Kalman state). `core/sim.c:31-56` `wind_sample` + `sim.h:11-26` DIAL-A-GUST = the "external input the deterministic plant consumes without becoming non-deterministic" template for the acquisition trace.
- **Guidance socket (§B.5 / §F):** `core/guidance_hoverslam.c:84` (`r_xy` substitution point), the MPPI shift sites, `GuidanceCmd.target_xy` (TARGETDESIGN's `guidance.h` field) — where my estimate lands.
- **Wire (§C.6):** `core/protocol.h` — `pred_impact[2]`@220, `dist_pad`@216, `deck_z`@264, `BL_EVT_TARGET_CHANGED`@226 (reserved), HELLO geometry constants @172-177; the D-013 mirror+golden process for the new fields.
- **Observer / scene geometry the sensor-camera reuses (§B.1):** `ui/src/scene/uglyScene.ts` (pad plane + KESTREL-9 booster proxy), `ui/src/scene/markers.ts` (the `pred_impact` convergence money-shot, `solveConvergence` @60-69) — the OBSERVER; the sensor-camera is a separate plant-side raster of the same geometry data.

---
*The plant has been wrong six times; the method has never been. Today's sim contains one last hidden oracle — the true target coordinate. This design removes it: the vehicle earns the target through a camera, a laser, and a language it can be told in — imperfect sensors it must fuse and gate — and when it mis-perceives, it misses, honestly. That is the anti-cheat thesis closed on its final front, and the door to telling a rocket where to land in words.*
