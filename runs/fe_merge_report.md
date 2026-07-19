# F2-INTEGRATOR — LZ-COCKPIT F1→F2 merge report

**Agent:** F2-INTEGRATOR · **Date:** 2026-07-18 ~21:20 -05:00 (Saturday)
**Worktree produced:** `C:\Booster_Lander_Simulator\_fe_merge_wt\` (a fresh, gate-green assembly the
main session can copy into the real tree)
**Sources merged:** `_fe_shell_wt` (S0), `_fe_scene_wt` (S1), `_fe_audio_wt` (S3), `_fe_const_wt` (S2B)
**Intercom:** lane `fe-merge`, id `raopljsx` (msgs 1336–1337); frontend-main = `zobqzmwj`.

---

## 0. GATE SCORECARD — ALL GREEN

| # | gate | result |
|---|---|---|
| 1 | `pnpm install --frozen-lockfile` | **OK** — 103 pkgs, "Lockfile is up to date" |
| 2 | `pnpm typecheck` (`tsc --noEmit`) | **CLEAN** — 0 errors |
| 3 | `pnpm test` (vitest run) | **152 / 152** across 13 files |
| 4 | `pnpm build` (`tsc --noEmit && vite build`) | **OK** — both pages (index + constellation), 50 modules |
| 5 | `cargo build` (shell) | **OK** — `booster-shell.exe` 15.1 MB, **0 errors / 0 warnings**, tauri 2.11.5, 3m52s cold |
| 6 | LIVE SMOKE on :8145 | **PASS** — S1 node smoke + S0 verifyHello gate + S3 audio smoke, **zero orphans** |

Toolchain: pnpm 10.33.2, node v24.16.0, cargo/rustc 1.96.0.

---

## 1. What was assembled (base + four waves, territory-clean)

The real tree's `ui/` and `shell/` were confirmed **clean at the F1 branch point** (`git status -- ui shell`
= "nothing to commit"; the parent session's core work landed at `eda595e` D-015 but did not touch
ui/shell). The four `_fe_*_wt` dirs are **plain directory copies** (not registered git worktrees — verified
via `git worktree list`), each a full-repo snapshot; I copied only the ui/shell source each one owns.

Base `ui/src` = `fx/ net/ scene/ main.ts` (M3 stub scene, `onTlm`-only). Base `shell/` =
`build.rs Cargo.toml tauri.conf.json capabilities/default.json src/main.rs`.

Each agent added its own module directory with **zero collisions** except three genuinely shared files
(`main.ts`, `net/client.ts`, and the new `net/events.ts`). Layered in:

- **S1 (scene):** `director/`, `hud/`, `scene/documentaryScene.ts` + `scene/markers.ts` (+test),
  `net/events.ts` (+test), `scripts/smoke.mts`.
- **S3 (audio):** `audio/` (11 files), `smoke/audio_smoke.mts`.
- **S0 (shell):** `ui/src/shell/` (10 files), `net/client.ts` (the `onRawFrame` variant),
  and the Rust `shell/` sources (see §3).
- **S2B (const):** `constellation/` (11 files incl. `__fixtures__/` + `types.ts`), top-level
  `constellation.html`, `scripts/verify_parse.mjs`.

---

## 2. Every merge decision (the reconciliations)

### 2.1 `ui/src/main.ts` — THE three-way additive merge (S0 ⊕ S1 ⊕ S3)
S1 is the new spine (it replaces `uglyScene`→`documentaryScene` and rebuilds the render loop with the
director). S0 and S3 were **additive on the M3 base**; I re-derived their additions onto the S1 spine.
Mount order per mission: **scene → hud → director → audio → shell chrome LAST**. Reconciliations:

| merge point | base | resolution in merged main.ts |
|---|---|---|
| scene | `uglyScene` | S1 `buildDocumentaryScene` (spine) |
| `onTlm` | `interp.push(f)` | `interp.push(f); audio.onTlm(f);` (S1 keep + S3 tee) |
| `onEvtBytes` | (M7) | `evtQueue.push(decodeEvt(buf))` (S1 director/HUD/timeline) **AND** `audio.onEvtBytes(buf)` (S3 trigger bus) — both fire |
| `onOpen`/`onClose` | log only | S1 `chip.set("STREAMING"/"LOST")` **AND** S0 `shell.onSocketOpen()`/`onSocketClose()` |
| `onHelloBytes` | (M7) | S1 `doc.applyHello(h)` + chip info (audio needs no HELLO) |
| `onStatsBytes` | (M7) | S1 `chip.setStats(st.fpsEmit)` |
| `onRawFrame` | — | S0 `shell.onFrame(buf)` (wire log + identity gate + liveness) |
| `TelemetryClient` 2nd arg | default url | S0 `shell.wsUrl` (supervisor-chosen port) |
| audio listener | — | **S3 `audio.setListener(camEyeThree)`** — the director's eye in three-world IS the active camera (canon §B.8). This is the one non-trivial semantic reconcile: base-S3 fed the M3 fixed-offset `camWorld`; on the S1 spine the correct listener is the director eye. |
| loop tail | — | `audio.tick(); audio.updatePanel();` before `renderer.render` |

Result typechecks and builds; every consumed interface verified (`DocumentaryScene.markers.{convergence,
missDistanceM}`, `DirectorRig.{eye,target,fov,onEvt,update,select,setAuto}`, `DirectorContext{phase,
altitudeM}`, `HudHandle.{update,onEvt}`, `TimelineHandle.{onEvt,tick}`, `events.ts` exports).

### 2.2 `ui/src/net/client.ts` — S0's variant (the only modifier)
S1/S3/S2B left `client.ts` byte-unchanged; **S0** added the optional `onRawFrame?` handler (+2 lines:
one interface field, one `this.handlers.onRawFrame?.(ev.data)` before `route()`). Took S0's version
verbatim. Purely additive; unset = no-op; does not disturb the decode hot path.

### 2.3 `ui/package.json` — union
Base + **S1's `"smoke"` script** + **S2B's devDeps** `@types/jsdom ^28.0.3`, `@types/node ^26.1.1`,
`jsdom ^29.1.1`. (S3 added no package.json change; S0 added none.) No runtime `dependencies` changed —
Tauri's API is dynamically imported by S0's chrome, so **no new npm dep** for the shell.

### 2.4 `ui/pnpm-lock.yaml` — used S2B's lockfile (exact match)
The added devDeps mean the base lockfile is stale (`--frozen-lockfile` would fail). **S2B's worktree
already resolved exactly these deps** against the same base; its lockfile's importer block is a
byte-for-byte match for the merged `package.json` devDependencies (verified). I copied S2B's
`pnpm-lock.yaml` in and ran `pnpm install --frozen-lockfile` → **"Lockfile is up to date"**, 103 pkgs,
fully reproducible. No `pnpm install` regeneration was needed. (Benign warning: esbuild postinstall
scripts "ignored" — esbuild works without them; vite/vitest are fine.)

### 2.5 `ui/vite.config.ts` — S2B's multi-page input
Added `import { resolve } from "node:path"` and the additive `build.rollupOptions.input = { main:
index.html, constellation: constellation.html }`. Nothing else changed (server port 5183, three dedupe,
esnext target all preserved).

### 2.6 `ui/tsconfig.json` — unchanged (base == S2B, byte-identical)
Important: `@types/node` is now an installed devDep, but the tsconfig `"types"` array stays
`["vite/client", "vitest/globals"]` — node globals are **not** pulled into the DOM app surface (this is
why typecheck stays clean and S2B deliberately kept it scoped). No change made.

### 2.7 `goldens/protocol/` — copied into the worktree (REAL fix, see §5.1)
S0's `hello.test.ts` imports `../../../goldens/protocol/hello.hex?raw`. In an isolated ui-only copy that
path escapes the tree and vitest fails to load the suite (`ENOENT C:\goldens\...`). I placed
`goldens/protocol/{hello,evt,tlm}.hex` in the merge worktree to mirror the real-tree adjacency (`ui/`
sits next to `goldens/` at repo root). **In the real tree this Just Works** — `goldens/` is already
present — so this is a worktree-faithfulness fix, not a code change. After it, hello.test.ts loads and
its 4 tests pass (148 → 152).

---

## 3. Shell (Rust) merge

Applied S0's sources verbatim from `_fe_shell_wt/shell/`:
`src/main.rs` (replace), `src/supervisor.rs` (new), `src/commands.rs` (new), `src/job.rs` (new),
`Cargo.toml` (replace — adds tauri-plugin-single-instance, tokio, anyhow, serde, serde_json,
tracing(-subscriber), win32job (windows); drops the old `[lib]` + tauri-plugin-shell),
`tauri.conf.json` (replace — removes the illegal `"//"` keys), `capabilities/default.json` (replace —
`core:default` only), plus S0's `Cargo.lock` (for a reproducible build).

- **`icons/icon.ico` — ADDED.** Provenance: byte-identical copy of `C:\Bonsai\src-tauri\icons\icon.ico`
  (2686 bytes, valid ICO header `00 00 01 00`). This is S0's placeholder; **required** — the real
  `shell/` has no icon and `tauri-build` fails the Windows resource compile without one. Replace with a
  real booster icon later (cosmetic; does not affect the build).
- **`binaries/booster-core-x86_64-pc-windows-msvc.exe` — ADDED** as a COPY of
  `build/bin/Release/booster-core.exe` (1.24 MB). Required by `externalBin: binaries/booster-core` for a
  future `cargo tauri build` NSIS bundle. (The real tree's CMake is expected to place this; providing it
  makes the worktree bundle-ready.)

`cargo build` → **`Finished dev profile in 3m52s`**, `booster-shell.exe` 15.1 MB, **0 warnings**. Full
transcript in `_fe_merge_wt/shell/cargo_build.log`.

**Build artifacts NOT part of the integration** (present in the worktree from the build/test, exclude when
copying to the real tree): `shell/target/`, `shell/gen/`, `shell/cargo_build.log`, `_fe_merge_wt/*.log`,
`_fe_merge_wt/booster-core.exe` (the smoke copy), `ui/dist/`, `ui/node_modules/`.

---

## 4. Gate transcripts (verbatim highlights)

**[1] install:** `Lockfile is up to date, resolution step is skipped … Done in 3.3s` (103 pkgs).

**[2] typecheck:** `> tsc --noEmit` — no output (clean).

**[3] vitest — 152/152, 13 files:**
```
✓ src/net/decode.test.ts (4)        ✓ src/net/frame.test.ts (22)        [base 26]
✓ src/shell/hello.test.ts (4)                                            [S0]
✓ src/scene/markers.test.ts (14)    ✓ src/director/director.test.ts (15)
✓ src/net/events.test.ts (5)        ✓ src/hud/timeline.test.ts (4)       [S1 38]
✓ src/audio/propagation.test.ts (18) ✓ src/audio/crackle.test.ts (14)
✓ src/audio/evtDecode.test.ts (4)                                        [S3 36]
✓ src/constellation/runData.test.ts (21) ✓ src/constellation/design.test.ts (18)
✓ src/constellation/dom.test.ts (9)                                      [S2B 48]
Test Files 13 passed (13) | Tests 152 passed (152)
```
(S2B's exact-count assertion — d012 = 84 landed / 6 off-pad / 9 too-hard / 1 fuel-out — is inside the
green runData + dom suites.)

**[4] build — both pages:**
```
✓ 50 modules transformed.
dist/index.html                  0.90 kB
dist/constellation.html          1.31 kB
dist/assets/main-*.js           73.38 kB   (cockpit: scene+hud+director+audio+shell)
dist/assets/constellation-*.js  41.72 kB
dist/assets/three.module-*.js  842.75 kB   (shared three.js — the >500kB warning is pre-existing, benign)
✓ built in 4.92s
```

**[5] cargo:** `Compiling booster-shell v0.1.0 … Finished dev profile [unoptimized + debuginfo] in 3m52s`
(tauri 2.11.5, tokio 1.53, win32job 2.0.3, tauri-plugin-single-instance 2.4.3; no `warning:` lines).

**[6] LIVE SMOKE — core copied to the worktree, served on :8145 (main-tree exe never run):**

S1 node smoke (full descent, `--experimental-transform-types scripts/smoke.mts ws://127.0.0.1:8145 6000`):
```
TLM decoded: 3457  HELLO: 1  EVT: 10  STATS: 288  seqGaps: 0
t: 0.01s -> 27.65s   alt: 2081m -> 13m   phase: 1 -> 7
pred_impact last: [1.0, -0.6]  ignite_h: 60.0m  throttle: 0%  nEng: 0
EVT beats: PHASE IGNITE START TEA-TEB LEGS PHASE SHUTDN TD PHASE VERDICT
SMOKE PASS: 3457 frames (full descent through touchdown/verdict), all consumed fields finite + in range;
HELLO v3 ok; 10 EVT beats decoded.
```
(serve log: `done — verdict=GOOD emitted=3456 frames, t=27.6 s` — one-shot server streamed then exited.)

S0 identity gate — `verifyHello` (byte-verbatim) against the live wire, fresh serve:
```
HELLO frame: magic=0x304c4c48 ver=3 ok=true
=== S0 HELLO IDENTITY GATE: PASS (live v3 verified against the wire) ===
```

S3 audio smoke (`smoke/audio_smoke.mts 8145 500`, terminal s42 pad-cam):
```
=== AUDIO PROPAGATION SMOKE — PASS ===
frames: TLM=500 HELLO=1 EVT=4 STATS=41 · slant 2327–2806 m · delay 6.79–8.18 s
gain & absorption-knee monotone-non-increasing vs range · delay==range/343 · doppler>0
```

**Orphan sweep after all smokes:** port 8145 — NONE; worktree booster-core procs — NONE; background jobs
— NONE. The one-shot server self-exited each time (graceful complete, exactly S0's classifier model). No
orphans left behind.

---

## 5. FLAGS (open items for eyes-on / follow-up)

### 5.1 (resolved, documented) S0 hello.test.ts golden path
`src/shell/hello.test.ts` loads `../../../goldens/protocol/hello.hex?raw`. Works in the real tree
(goldens present at repo root next to ui/). Any *isolated* ui-only checkout must include
`goldens/protocol/hello.hex`. Handled in the merge worktree by copying the goldens tree. **Integration
action: none** (real tree already has it) — just don't move ui/ away from goldens/.

### 5.2 (OPEN — deferred by design) Bloom post-pass NOT wired
Both S1 and S2B flagged this and it remains true: `scene/renderer.ts` (shared, unmodified by all four
agents) sets AgX tone-mapping and calls `renderer.render(scene, camera)` **directly — there is no
WebGPU post-processing chain**. The plume (S1) and constellation glyphs (S2B) emit HDR + additive and
are **bloom-ready**, but nothing blooms until a TSL `PostProcessing`/`mrt({output,emissive})` →
emissive-bloom pass is added scene-wide.
**Decision: LEFT UNWIRED + FLAGGED**, per mission ("wire it if the report gives the recipe, else leave").
The reports give *intent* (mrt → bloom-on-emissive → TRAA), **not a copy-paste TSL recipe**, and wiring
it means editing the shared `renderer.ts` + swapping the `renderer.render()` call in `main.ts` for a
post-processing render — a scene-wide change with real regression risk to the (currently green) build,
and there is **no visual verification available in this environment** (headless WebGPU stalls — the exact
issue all four agents hit; S0's WebView2 GPU init never reached WS connect). Forcing a blind TSL post-pass
could turn a green build red with no way to confirm. Recommend the integration owner add it with eyes-on.

### 5.3 (OPEN — by design) Dual connection chip
Two chrome elements now show connection state, intentionally kept because both are real, tested work with
distinct roles:
- **S1 stream-stats chip** (top-left, `pointer-events:none`): STREAMING/LOST + HELLO seed/run/ver + fps +
  backend — a passive readout driven by the socket callbacks.
- **S0 supervisor top-strip** (`createConnectionChip` + picker + wire-log + stderr toggles): the Tauri
  lifecycle chip (SPAWNING/HEALTHY/RELIGHT), scenario/seed/run picker, panels.
They occupy different regions and don't functionally collide. S1's own report §7 explicitly defers the
on-screen chrome to "the shell/chrome track (frontend-main's territory)", so S0's strip is authoritative
for lifecycle; S1's chip adds live stream stats S0's doesn't. **Eyes-on polish item:** at integration,
decide whether to fold S1's stream stats *into* the S0 strip and drop the separate chip (a small
cosmetic consolidation — not a correctness issue).

### 5.4 (OPEN — carried from S0, unchanged) Mid-stream-crash classifier edge
S0's supervisor classifies "graceful complete" off having streamed at all; a core that streams then dies
mid-run is treated `ExitedComplete` on the **backend**. The **frontend** still handles it correctly for
the user (socket drop → chip LOST + RELIGHT via the 800 ms stall watchdog). Not touched in this merge
(out of frontend-integration scope). One-line `supervisor.rs` follow-up if a stricter backend signal is
wanted (gate `ExitedComplete` on the `serve: done` sentinel).

### 5.5 (carried caveats, unchanged from the F1 reports)
Not re-verified here (they need eyes-on): S1 SRP `ct=min(4,mach·0.6)` proxy, qbar STRUCT reference line
(45 kPa, not a wire field), plume proxy meters-per-unit tuning; S2B synthesized glyph bearings/arcs
(radius=td_lat is real, bearing is golden-angle schematic — true ribbons need `.bltlm`, wave F2); S3
audibility (math proven, the *mix* needs ears in a real browser). All are documented in the respective
F1 reports; none block the gates.

---

## 6. EXACT real-tree integration list (for the main session)

Copy from `_fe_merge_wt/` into the real tree. **`core/` untouched. No protocol changes.** Exclude all
build/test artifacts (§3).

**A. `ui/src/` — new module directories (drop in whole):**
- `audio/` (11 files) · `constellation/` (12 incl. `__fixtures__/` + `types.ts`) · `director/` (2) ·
  `hud/` (3) · `shell/` (10) · `scene/documentaryScene.ts` · `scene/markers.ts` (+`markers.test.ts`) ·
  `net/events.ts` (+`events.test.ts`)

**B. `ui/src/` — replace shared files:**
- `main.ts` (the reconciled three-way merge — §2.1)
- `net/client.ts` (S0's `onRawFrame` variant — §2.2)

**C. `ui/` top-level:**
- `constellation.html` (new) · `scripts/smoke.mts` (S1) · `scripts/verify_parse.mjs` (S2B) ·
  `smoke/audio_smoke.mts` (S3) · **replace** `package.json` (§2.3), `pnpm-lock.yaml` (§2.4),
  `vite.config.ts` (§2.5). `tsconfig.json` unchanged.
- (`scripts/hello_gate_live.mts` is my live-gate harness — optional to keep as a repeatable check;
  self-contained, no imports.)

**D. `shell/` (Rust):**
- replace `src/main.rs`, `Cargo.toml`, `tauri.conf.json`, `capabilities/default.json`
- add `src/supervisor.rs`, `src/commands.rs`, `src/job.rs`, `icons/icon.ico`, `Cargo.lock`
- bundling only: ensure `shell/binaries/booster-core-x86_64-pc-windows-msvc.exe` exists (CMake copies the
  core here; provided in the worktree as a copy)

**E. `goldens/`:** already present in the real tree — **no action** (only needed for isolated checkouts).

**Post-integration verification (from the real tree):**
```
pnpm -C ui install --frozen-lockfile   # 103 pkgs
pnpm -C ui typecheck                    # clean
pnpm -C ui test                         # 152/152
pnpm -C ui build                        # dist/index.html + dist/constellation.html
cargo build --manifest-path shell/Cargo.toml   # booster-shell.exe, 0 warnings
# live: copy build/bin/Release/booster-core.exe somewhere, --serve --port <p>, then
#   pnpm -C ui exec node --experimental-transform-types scripts/smoke.mts ws://127.0.0.1:<p> 6000
```

---

## 7. Bottom line
One merged, gate-green worktree at `_fe_merge_wt/`. All six gates pass; the three-way `main.ts` merge is
coherent (scene→hud→director→audio→shell, every interface typechecked and exercised live); every one of
the four waves' test suites is green in-union (152); the shell builds clean and its identity gate + the
scene + the audio model are all proven against a live one-shot serve with zero orphans. Two design flags
carried forward for eyes-on (**bloom unwired**, **dual chip**) and one backend follow-up (mid-stream
classifier); nothing blocks copying this into the real tree.
