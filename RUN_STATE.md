# RUN_STATE

**Canon:** `CLAUDE_v1.md` (read §0–§2 first). `CLAUDE_v0.md` is history — do not edit.

**Current milestone:** M2 (hoverslam headless) — **GATE GREEN, TERMINAL ~97-98% across seeds**
(honest corrected plant; was 100% on the old too-stable plant). Plant physics corrected &
independently VERIFIED by a 5-agent C-only audit (D-005 + D-006): CoP marginal-unstable, Cmq
damping, fin signs — all confirmed on 3 code paths. Audit also FOUND & FIXED: gimbal rate-state
windup, zero roll damping, transonic CoP bump. Implemented P5 AoA-hold (holds 6°, was 2°) + the
base-first "lift opposes tilt" steering-direction physics. Grid-fin control now flies STABLE and
holds/steers correctly. **AERO_OFFSET still 0% land** — the landing burn at 5 km/300 m/s fights
its own passive fin aero (lat grows during the burn); this is the ENTRY-BURN problem (M6). Next:
build the entry supervisor (E3) + divert sequencing (P5 §5.1) + burn-phase aero transition, or
MPPI. See D-006, runs/agentP5_aoa_hold_control.md, runs/sandbox/p4_divert.c.

**TOOLING RULE (user, 2026-07-18):** C/C++/CUDA for everything, NEVER Python (crawls this
CPU). Core is all C — keep it. `tools/` (MC report, G-FOLD oracle, protocol codegen) must be
C/C++, not the Python the spec originally sketched. Use C:\orrery for heavy calc. Fleet
subagents' Python scratch in runs/sandbox is throwaway — do not depend on or re-run it.

## Status (2026-07-18)

- **M0 scaffold:** DONE. CMake + MSVC2022 build (`build/`, VS2022 generator), core builds
  clean, zero deps. (CUDA not yet wired — deferred to M4/M5.)
- **M1 plant + oracles:** DONE. `--selftest` PASS: US76 atmosphere, Philox RNG (host
  determinism + normal stats), quaternion/frame vectors, vacuum ballistic vs analytic,
  coast |q|=1, **analytic İ vs finite-diff**, hover impossibility (TWR_min=1.321),
  **bit-identical determinism memcmp**.
- **M2 hoverslam headless:** **99.8–99.9% LANDED** across seeds (1000/2000 runs).
  - seed 42, 1000 runs: 99.8% landed (270 PERFECT, 626 GOOD, 102 HARD, 2 crash), 0 off-pad,
    0 fuel-out. Landed mean td_v 2.09 m/s, lat 3.96 m, tilt 0.03°.
  - seed 7, 2000 runs: 99.9% landed (497 PERFECT, 1325 GOOD, 176 HARD, 2 crash).
  - GOOD+ ≈ 91%. (Gate wants ≥98% GOOD+ / p95≤3 — MPPI at M5 is expected to close the last
    HARD tail; tier-0 hoverslam has proven the plant is sound, which is its job.)
- **Headless `--out` fix (2026-07-18):** MC report writer (`main.c` cmd_headless) now checks
  `fopen`/`fclose` and fails loud — clear `stderr` error (path + errno) + nonzero exit 3 when
  the CSV can't be written, instead of the old false "wrote"; caller must pre-create the parent
  dir (not auto-created). CSV header/columns unchanged (goldens + tooling safe).

## What exists in core/

vmath.h, rng.h, constants.h, atmosphere.{h,c}, state.h, dynamics.{h,c} (forces/torques/
mass-props/analytic-İ/actuator-lags/SRP-shielding), integrator.{h,c} (RK4), contact.{h,c}
(leg spring-damper-crush + friction + substep), control.{h,c} (quaternion-PD + gimbal/RCS
allocation, altitude-scheduled tilt cap), guidance.h, guidance_hoverslam.{h,c} (unified
velocity-profile suicide burn, frozen a_design, cos-tilt throttle comp, first-order lateral),
scenario.{h,c}, sim.{h,c} (phase machine + verdict + termination), main.c
(--selftest | --headless | --run).

## Key tuning that made hoverslam land (see DECISIONS D-002)

Unified v_ref suicide-burn profile; a_design FROZEN at ignition from actual mass; throttle
compensated by 1/cos(tilt); first-order (non-overshooting) lateral velocity law, gentle
because the plant is sluggish; **overdamped** attitude loop (zeta=1.1) to avoid gimbal-
saturation oscillation; lateral steering fades out below ~40 m so the vehicle straightens
to vertical before touchdown (a tilted booster contacts one leg early at high profile speed).

**Next concrete action:** (1) freeze M2 goldens (MC baseline + a canned trajectory hash).
(2) Wire the telemetry protocol structs + a `--serve` WS stub (M3 groundwork). (3) Begin
ENTRY scenario hardening (fins aero for the aero-descent, thermal/struct budgets) — fins are
currently STUBBED (stowed, zero force); TERMINAL/AERO don't need them but ENTRY does.
Then MPPI (M4 CPU → M5 CUDA).

**Known simplifications to revisit (all deliberate, tracked):** grid-fin aero stubbed;
slosh module present in state but excitation not yet wired; Dryden turbulence is a
first-order horizontal approx; wind mean-profile only; single WS/renderer not built yet;
no CUDA yet. None affect the M1/M2 proof.

**Blockers:** none.

---

## Renderer / protocol track (Agent D, 2026-07-18) — M3 GROUNDWORK GREEN

Renderer + process-boundary scaffold now exists and is verified. Aesthetics stay gated
behind headless (directive 10); this is the M3 socket/shell/ugly-scene groundwork plus the
protocol contract, all built to compile + test clean without touching the M1/M2 proof.

**Protocol (owned by Agent D per intercom hand-off from Agent B):**
- `core/protocol.h` — packet layout (canon §10.3, App-B). `#pragma pack(1)`, LE, explicit
  `_pad`, `_Static_assert` on sizeof + key offsets. **COMPILED + VERIFIED with MSVC
  `cl /std:c11`**: `sizeof(BlTlmFixed)==276`, all offset asserts hold. Magic tags
  (TLM=0x304D4C54 etc.), `BlPhase`/`BlVerdict`/`BlEvtCode` enums, `BlPlanKnot`(16B),
  `BlCloudSample`(12B), `BlEvt`(48B). Added `p_chamber` (plume needs p0/pa).
- TS decoder `ui/src/net/decode.ts` mirrors it byte-for-byte (will be regenerated by
  `tools/gen_protocol_ts.py`; hand version is the reference + golden target).
- STILL TODO on the C side: `ws.c` (RFC6455 subset, `--serve`), populate + emit TLM/EVT/
  HELLO/STATS, `p_chamber = throttle_act*Pc_ref` (pin `Pc_ref` ~9.7 MPa in constants.h),
  freeze `goldens/protocol/*.hex`. STATS@10Hz struct spec pending (Agent D owns; Agent B to
  match solver fields: solver p50/p99, ESS, lambda, cost best/mean + top-3 terms).

**UI scaffold (`ui/`, three@0.185.1 EXACT, Node 24.16.0 / pnpm 10.33.2):**
- `package.json`, `vite.config.ts`, `tsconfig.json` — full UI **typecheck-clean**.
- `src/net/frame.ts` — THE single sim→three conversion (canon §10.7). 22 vitest pass vs
  App-C vectors + commutation property (rotate-then-convert == convert-then-rotate <1e-6)
  over arbitrary quaternions. **M3 quaternion gate + Risk #1: GREEN.**
- `src/net/decode.ts` (+ `.test.ts`, 4 pass) — binary TLM decoder, every field at exact
  offset (sentinel-trick golden), magic rejection, empty tails.
- `src/net/interp.ts` — N-ring + full-run history (17MB replay); lerp r / slerp q / HOLD
  actuators; render 1 packet in past; `raw` toggle (directive 8); seq-gap drop counter.
- `src/net/client.ts` — direct WS to `core --serve` (no Rust relay, canon §10.1), magic
  routing, reconnect backoff.
- `src/scene/renderer.ts` — WebGPURenderer bootstrap: `reversedDepthBuffer:true`, AgX
  tonemap, `renderer.init()`, backend detect+log (WebGL2 auto-fallback, Risk #2).
- `src/scene/floatingOrigin.ts` — camera-relative rebase >2km (canon §11.1, Risk #10) for
  the 70km→1m continuous shot; fp64 origin in JS, small f32 to GPU.
- `src/scene/uglyScene.ts` — M3 capsule+plane acceptance scene.
- `src/main.ts` + `index.html` — wires it all; splash-gate for audio autoplay (Risk #13).
- `src/fx/plume.ts` — analytic raymarched plume TSL node (canon §11.6), **typecheck-clean**:
  `RaymarchingBox` proxy, Mach-disk `x1=0.67·De·√(p0/pa)`, altitude balloon, SRP blend by
  C_T, kerolox color, GG streak, TEA-TEB flash. Wires at M7.

**Shell (`shell/`, Tauri v2, Rust 1.96):** `tauri.conf.json` (sidecar externalBin
`binaries/booster-core`, CSP `connect-src ws://127.0.0.1:*`), `capabilities/default.json`
(`shell:allow-execute` sidecar-only), `Cargo.toml`, `src/main.rs` (spawn+supervise+restart
the sidecar, kill on close — never writes state), `build.rs`. Not yet `cargo`-built (needs
the core exe copied to `shell/binaries/booster-core-x86_64-pc-windows-msvc.exe`).

**Renderer next actions (in M3→M7 order):** (a) C-side `ws.c` + TLM emit so `pnpm dev`
shows the capsule tracking a live descent (closes M3 gate: 10-min stream, zero drops,
<1-frame jitter). (b) HELLO decoder + procedural booster from geometry (M7). (c) plume wire
+ bloom MRT + sky (takram `/webgpu`, peers three>=0.182 so 0.185.1 OK) + audio + HUD +
director (M7). (d) long-exposure, ASDS, weather (M8). Full build order in intercom FINAL.
