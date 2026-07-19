# S1 DOCUMENTARY VIEW — build report (agent S1-SCENE)

**Agent:** S1-SCENE (intercom lane `fe-scene`, id `deog3y08`)
**Date:** 2026-07-18 ~20:30 CST
**Worktree:** `C:\Booster_Lander_Simulator\_fe_scene_wt\` (real tree untouched; core/ + shell/ untouched)
**Scope:** brainstorm §2 (the screen) + §5 S1 (DOCUMENTARY VIEW); wire-contract = `runs/brainstorm_canon.md` §A; interpolation doctrine = `runs/brainstorm_terminal.md`.

---

## Gate results (all green)

| gate | result |
|---|---|
| `pnpm typecheck` (`tsc --noEmit`) | **clean** |
| `pnpm test` (vitest) | **64/64 pass** (baseline was 26; +38 new) |
| node live-wire smoke, 500 frames | **PASS** — 0 seq gaps, all consumed fields finite + in range |
| node live-wire smoke, full descent | **PASS** — 3457 frames through touchdown+verdict, all sane |

New tests added (38): `markers.test.ts` (14), `director.test.ts` (15), `events.test.ts` (5), `timeline.test.ts` (4).

### Smoke output (full descent, seed 42 run 17, cape)
```
TLM decoded: 3457  HELLO: 1  EVT: 10  STATS: 288  seqGaps: 0
t: 0.01s -> 27.65s   alt: 2081m -> 13m   phase: 1 -> 7 (Coast -> Touchdown)
pred_impact last: [1.0, -0.6]  ignite_h: 60.0m  throttle: 0%  nEng: 0
EVT beats: PHASE IGNITE START TEA-TEB LEGS PHASE SHUTDN TD PHASE VERDICT
SMOKE PASS: 3457 frames (full descent through touchdown/verdict), all consumed
fields finite + in range; HELLO v3 ok; 10 EVT beats decoded.
```
The solve lens converged: `pred_impact` slid from `[50.4, -94.7]` at entry to `[1.0, -0.6]` (≈1.2 m off pad center) at touchdown. The full EVT trigger sequence fired, including the `TEA-TEB` GREEN_FLASH that pulses the plume and the terminal `TD`/`VERDICT` beats.

The smoke decodes through the **real** `decode.ts` + `events.ts` — the exact decoders the scene consumes — so it independently cross-checks every offset the renderer reads.

Run the gates yourself:
```
# from _fe_scene_wt\ui, with a serve on 8141:
pnpm typecheck && pnpm test
pnpm smoke ws://127.0.0.1:8141 500          # fast repeatable gate
pnpm smoke ws://127.0.0.1:8141 6000         # full descent (ends on socket close)
# serve:  _fe_scene_wt\booster-core.exe --serve --scenario cape --seed 42 --run 17 --port 8141
```

---

## Features built (S1 checklist)

### 1. Plume mounted (`scene/documentaryScene.ts` mounts `fx/plume.ts`)
- The existing raymarched TSL plume is fitted to a cone-proxy box below the bell and driven **every frame** from the interpolated TLM frame: `throttle_act`, `p_chamber/p_amb` (pressure ratio → Mach-disk spacing + balloon), `mach`, `n_eng`, and an **SRP-envelopment blend**.
- **SRP** (canon §11.6 forward-envelopment): C_T is not on the wire, so the blend uses a mach-gated proxy `ct = min(4, mach·0.6)` while lit — honest to the physics regime (SRP wraps forward only when burning supersonically; the plume's own `smoothstep(0.5,3.0,ct)` turns it into the envelope amount). Verified against the stream: entry burn (mach ~5–8) → full envelopment; landing burn (mach ~1) → trailing plume.
- **Plume-as-light** (canon §B.3): a `PointLight` at the bell whose intensity/color track `throttle_act` (off when unlit). Shadows scoped to the key light only (D-010 #5).
- **TEA-TEB green flash**: `plume.greenFlash` uniform pulsed on the `GREEN_FLASH` EVT, decays ~0.33 s (EVT is the trigger, not an animation clock).
- Bells **never glow** (regen-cooled rule encoded — plain dark metal material).

### 2. Diegetic markers (`scene/markers.ts`)
- **`pred_impact` ground marker** (offsets 220/224): an additive annulus + dot on the ground plane at the predicted impact XY, with a **fading comet-trail** ("the solve lens") of the last 48 predictions. Color ramps off-target **amber → on-pad cool-white** by a `solveConvergence` scalar; the ring tightens as it converges.
- **`ignite_h` altitude ring** (offset 228): a faint horizontal ring centered under the vehicle at altitude = `ignite_h`, that **breathes** (`igniteBreathe`) as the margin recomputes and fades in as the vehicle nears it.
- **Interpolate-never-snap**: both markers read the fields off the **interpolated** frame (`InterpSample.frame`), and the trail is itself a throttled visual decay — no marker ever hard-jumps to a raw packet. All conversion goes through `frame.ts` (the sole sim→three map).
- Pure math (`trailAlpha`, `solveConvergence`, `igniteBreathe`, `padMissDistance`) unit-tested (14 cases).

### 3. HUD strip (`hud/hud.ts`)
- **Phase ladder**: the nominal descent beats (Coast → EntryBurn → AeroDescent → LandingBurn → Touchdown → Landed) driven by the TLM `phase` field; `EVT PHASE_CHANGE` beats update the highlight; terminal-failure phases flip a caution/abort palette.
- **Readouts**: `t_go`, LOX/RP1 fuel, altitude, |v|, mach, throttle.
- **qbar-vs-STRUCT bar**: `qbar` drawn against a structural reference line (nominal/caution/abort fill).
- **Verdict badge**: `verdict` field → PERFECT/GOOD/HARD/TIPPED/CRASHED with colored glow.
- **Frame-time strip**: a 60-sample bar strip with the **8.3 ms budget** line — bars over budget turn red (the budget made visible, canon §B.9).
- **EVT ticker**: the newest beat's label + time.
- DOM/imperative — **telemetry never enters a React render path** (canon §1). CSS-var palette swapped by a `data-state` attr (the TERMINAL theming pattern).

### 4. EVT timeline (`hud/timeline.ts`)
- Bottom glyph scrubber populated **live from EVT frames** as they arrive; each beat is a glyph (◆▲✦▶■≋⋔⊥★…) placed on a time axis with a moving "now" cursor. Display-only in S1 (scrubbing is S2). Pure `timeToFrac` layout math unit-tested.

### 5. Director v0 (`director/director.ts`)
- **Presets**: PAD_LONG_LENS (12° long lens, 2 km tracking-footage grammar), ONBOARD_DOWN (wide, on-vehicle looking down the plume), CHASE (spring-arm behind the velocity vector), FREE_ORBIT (slow auto-orbit). Hotkeys `1–4`; `0`/`a` re-engage AUTO.
- **AUTO-DIRECTOR** cut grammar (§11): entry-burn → wide; aero-descent → chase; landing-burn ignition → onboard; leg-deploy/touchdown/landed → pad long-lens locked. Cuts debounced (0.6 s) so an EVT burst doesn't strobe.
- **Smooth transitions**: eased (`easeInOutCubic`) pose lerp between the frozen previous shot and the live-tracking target pose; settled cams follow the interpolated vehicle continuously.
- All camera state is **renderer-side and never crosses the telemetry boundary** (canon §B.6). Poses are computed in sim-world then converted via `frame.ts` and placed through the floating origin.
- `decideAutoCut`, `easeInOutCubic`, `presetPose` unit-tested (15 cases).

### 6. Dark-studio AgX + bloom baseline (`scene/documentaryScene.ts`)
- Borrowed the aesthetic from `C:\Bonsai\src\lib\bonsaiScene.ts` (which borrowed this project's canon): dark-studio background `0x07090c`, fog, hemisphere fill + warm key + cool rim lighting rig, AgX tone-mapping (already set on the WebGPU renderer). Plume emission stays HDR (>1) so the post bloom pass catches the core.
- **Note:** Bonsai uses the WebGL `EffectComposer`/`UnrealBloomPass`; this project is on the WebGPU `renderer.ts` path (AgX + reversed-z already wired). The scene borrows the *look* (palette, lighting rig, bloom intent, background); the actual TSL bloom **post-pass wiring** is an integration task (see limitations).

### Procedural-lite booster
Replaced the M3 capsule with a procedural booster (tank + interstage + octaweb + gimbaled center bell + **4 grid fins hinged by `fins_act`** + **4 legs deployed by `deploy_frac`**), rebuilt from HELLO geometry (`veh_len/veh_dia/leg_span/pad_radius`). The full canon-grade procedural booster (§B.2: 9 bells, triplanar PBR, soot/frost) is M7 — this is the "credible documentary view, then STOP" target (D-011).

---

## Files

**New:**
| file | lines | what |
|---|---|---|
| `ui/src/net/events.ts` | 156 | EVT/HELLO/STATS decoders (mirror protocol.h) + EVT code enum/labels/glyphs |
| `ui/src/scene/markers.ts` | 222 | diegetic pred_impact comet-trail + ignite_h ring (+ pure math) |
| `ui/src/scene/documentaryScene.ts` | 307 | S1 hero scene: booster + mounted plume + markers + dark-studio rig |
| `ui/src/hud/hud.ts` | 270 | phase ladder / readouts / qbar bar / verdict / frame-time strip / ticker |
| `ui/src/hud/timeline.ts` | 116 | EVT glyph timeline (display-only) |
| `ui/src/director/director.ts` | 218 | camera presets + AUTO-director EVT cut grammar |
| `ui/src/net/events.test.ts` | 119 | EVT/HELLO/STATS decode round-trips |
| `ui/src/scene/markers.test.ts` | 83 | marker math |
| `ui/src/director/director.test.ts` | 109 | cut grammar + easing + preset geometry |
| `ui/src/hud/timeline.test.ts` | 20 | timeline layout math |
| `ui/scripts/smoke.mts` | ~200 | live-wire decode smoke gate (node --experimental-transform-types) |

**Modified (minimal + additive):**
- `ui/src/main.ts` — swapped `uglyScene` → `documentaryScene`; wired `onHelloBytes/onEvtBytes/onStatsBytes`; added director-driven camera through the floating origin; mounted HUD + timeline + connection chip; EVT drain loop feeds director/HUD/timeline/green-flash. Loop shape (stream-clock sampling, 1-packet-past interp, rebase) preserved.
- `ui/package.json` — added `"smoke"` script.

`decode.ts`, `interp.ts`, `frame.ts`, `client.ts`, `renderer.ts`, `floatingOrigin.ts` — **unchanged** (consumed as-is). `uglyScene.ts` left in place (no longer imported).

---

## Canon-checklist coverage map

| canon item | status |
|---|---|
| §A.0 renderer = pure observer, every visual from telemetry | ✅ every moving element keyed to a field or EVT beat |
| §A.4 EVT is the only trigger channel | ✅ green-flash, director cuts, ladder, timeline all EVT/phase-driven |
| §A.6 sole sim→three conversion via `frame.ts` | ✅ markers, booster, director poses all route through it |
| §11.2 interpolate-never-snap | ✅ markers/scene read the interpolated frame; no raw snaps |
| §B.3 plume: pressure-ratio Mach disks, balloon, SRP envelopment, green flash | ✅ mounted + driven (SRP via mach-gated ct proxy) |
| §B.3 plume-as-light | ✅ throttle-driven bell point light |
| §B.2 bells never glow | ✅ encoded |
| §B.2 grid fins hinged by `fins_act`, legs by `deploy_frac` | ✅ |
| §B.6 cameras renderer-side, never cross boundary; director cuts on EVT | ✅ 4 presets + AUTO grammar + smooth transitions |
| §B.7 diegetic pred_impact marker slides onto pad; ignite_h ring | ✅ comet-trail + breathing ring |
| §B.9 8.3 ms frame budget made visible | ✅ frame-time strip with over-budget flag |
| §B.1 dark-studio AgX + bloom | ⚠ AgX + palette/rig done; **TSL bloom post-pass wiring deferred** (see below) |

---

## Limitations + integration notes (honest: what needs eyes-on)

1. **No visual verification.** Per mission, browser preview is not available to this agent — validation is typecheck + vitest + the headless decode smoke. **Everything visual needs eyes-on at integration**: plume appearance/scale, marker legibility, camera framing/cut timing, HUD layout at real resolution. The geometry/uniform *math* is what's verified; the *look* is not.

2. **Bloom post-pass not wired.** The renderer (`renderer.ts`) sets AgX + reversed-z but has **no post-processing chain** yet (canon §B.1 wants `mrt({output,emissive})` → bloom-on-emissive → TRAA → …). The plume emits HDR and is tagged additive so it *will* bloom once the pass exists, but the TSL `RenderPipeline` + bloom pass is a scene-wide integration task (touches `renderer.ts`, which is shared — I did not modify it). **Until then the plume core is HDR-bright but not bloomed.** Recommend the integration owner add the WebGPU post chain.

3. **SRP C_T is a proxy.** `ct = min(4, mach·0.6)` (mach-gated) stands in for the real thrust coefficient, which is not on the wire. It reproduces the *regime* (envelop when supersonic+burning) correctly but is not the physics C_T. If a future protocol adds thrust/C_T, swap `documentaryScene.ts`'s `ctProxy` for the real field (one line).

4. **qbar STRUCT line is a documented reference** (45 kPa), not a wire field — the shipped STATS carries `max_qbar` but no structural limit constant. Tagged in code. If HELLO/STATS gains a structural-q field, wire it in.

5. **Marker proxy scale / plume proxy dimensions** are tuned to KESTREL-9-ish defaults and rebuilt from HELLO. The plume proxy's meters-per-local-unit mapping (`plume.ts` assumes ~2 m near-field/unit) may need a tuning pass against the real vehicle scale at eyes-on.

6. **Single-pad assumption.** Markers use pad center = origin (the shipped HELLO carries a single pad radius, no pad table — canon §A.5 gap). Multi-pad (`SET_TARGET`, PAD_B) needs the HELLO pad-table extension.

7. **Camera hotkeys are keyboard-only** (1–4 / 0 / a). The on-screen director UI (right-rail preset picker) is deferred to the shell/chrome track (frontend-main's territory per §E open questions).

8. **`three/webgpu` in node.** Vitest imports `three/webgpu` for pure classes (Vector3, etc.) — works headless. The renderer itself is never instantiated in tests.

## Coordination
- Posted join + 2 milestones to intercom lane `fe-scene` (`say --project`); bodies are data.
- Did not touch `core/`, `shell/`, `renderer.ts`, or any sibling module another agent owns. `main.ts` diff kept small + additive. Own serve on port 8141 killed; port clear.
