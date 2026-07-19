# S2B — MC CONSTELLATION (standalone view) — build report

**Agent:** S2B-CONSTELLATION · **Lane:** fe-const (intercom id `as3xjbkh`)
**Date:** 2026-07-18 (Saturday) 20:40 -05:00
**Worktree:** `C:\Booster_Lander_Simulator\_fe_const_wt\` (real tree never edited)
**Mission source:** `C:\Brainstorm\booster-3d-frontend-brainstorm.md` §3 signature 1 (MC
CONSTELLATION) + §5 S2; `runs\brainstorm_canon.md` §A (wire contract, read for conventions).

---

## 1. What this is

A **standalone Vite page** (`constellation.html`) that renders a per-run Monte-Carlo CSV as a
**pad-centric end-state constellation**: the pad disc, the **26 m on-pad ring**, a distance grid,
and **every run as a touchdown glyph** at its `(synthesized-angle, td_lat radius)`. Hover a glyph
for a per-run card; click to pin it; filter by cause with chips; a summary strip rolls up the
population; load a **second CSV for the A/B flip diff** (the `mcdiff` visual — paired glyphs +
connecting hairlines for runs whose outcome changed).

Deliberately its **own entry point** so it parallelized with the S1 cockpit scene (sibling
`fe-scene`). It folds into the main cockpit in **wave F2**. It is a **pure end-state population
view** — it never touches live telemetry.

---

## 2. Design

### 2.1 The value is the END-STATE constellation (honest by construction)
The CSV carries **end-state only** — `td_lat` (touchdown lateral miss magnitude) and `td_v`
(vertical speed), **no trajectory, no ground-track bearing**. So the design puts all the real
signal where the data is real:
- **Radius = `td_lat`** (REAL). Every glyph's distance from pad center is its true miss.
- **The 26 m ring** is the load-bearing reference — on-pad (`td_lat<=26`) vs off-pad reads at a
  glance against it.
- **Glyph color = cause** (canon palette): landed cool-white→soft-gold by GOOD/HARD grade
  (verdict 0→3), off-pad amber, too-hard red, fuel-out violet, tipped orange, residual slate.
- **Glyph size** ∝ `td_v` for too-hard/fuel-out (a harder crash is a bigger, angrier mark),
  clamped so a 96 m/s outlier doesn't dominate.

### 2.2 What is SCHEMATIC (labeled everywhere, honest)
- **Glyph azimuth (bearing)** is **synthesized** deterministically from the run *index* via the
  **golden angle (~137.5°)** — an even sunflower spread for legibility. The CSV has no bearing, so
  this is a **layout choice, not physical truth**.
- **Descent arcs** above each glyph are **stylized** (a quadratic curve from a synthesized entry
  point down to the touchdown mark). They communicate "this run came down HERE"; the path is
  invented.
- Both are called out **in the UI** (a persistent amber caveat bar: *"descent arcs + glyph
  bearings are synthesized from run index … the CSV carries touchdown miss magnitude only, not the
  true ground track. Radius = td_lat (real). True ribbons need recorded trajectories (.bltlm) —
  wave F2."*) and in code comments. **True ribbons require recorded trajectories** — a wave-F2 item
  riding on `.bltlm` recordings.

### 2.3 The cause-bucketing precedence (the load-bearing classification)
A CRASHED run (verdict 5) has a *reason*, and the reasons have a **precedence that matters**:
1. `landed` : verdict 0..3
2. `tipped` : verdict == 4
3. crashed (verdict == 5), sub-classified **in this order**:
   a. **`fuel-out` : `fault == 1`** — checked FIRST. A propellant-depletion crash is a fuel-out
      *even if it also missed the pad* (it ran dry on the way down).
   b. `off-pad` : `td_lat > 26`
   c. `too-hard` : `td_v > 6` (on-pad, no fault)
   d. `other` : residual (on-pad, soft, no fault)

**Why fuel-out before off-pad:** in `d012_entry_v4.csv`, **run 14** is `fault==1` AND
`td_lat=157.8` (way off-pad). The canonical population is **84 landed / 6 off-pad / 9 too-hard /
1 fuel-out**. Naive geometry-first bucketing yields 7 off-pad / 0 fuel-out (WRONG). Fuel-first
yields the canonical **84/6/9/1**. This precedence is asserted exactly in the tests.

### 2.4 Interactions
- **Hover** → per-run card (run #, verdict, td_v, td_lat + on/off-pad, td_tilt, fuel, fault,
  max_qbar, t_total) + a plain-English cause line matching the bucketing rule.
- **Click** → pin the card (survives pointer moves; ✕ to unpin).
- **Filter chips** (one per bucket, with live tallies) → additive show/hide of glyphs.
- **Summary strip** → `X/N landed  P%` + per-cause counts; **matches the known d012 numbers**.
- **A/B mode** → load a 2nd CSV; paired glyphs (A solid / B airy, tangentially nudged so they
  don't overlap), **hairlines connect runs whose bucket flipped**, and a **transition strip** shows
  the flip count + flow counts (`FROM→TO n`) + A-only/B-only tallies.
- **Camera**: OrbitControls, pad-centric (target at origin), ground-clamped polar angle, reset
  button. **Arc toggle** for the schematic descent arcs.
- **Load**: drag-drop (whole window) + file picker (A and B buttons).

### 2.5 Renderer conventions reused (from the repo)
Dark-studio **AgX** baseline via the shared `../scene/renderer.ts` (`createRenderer`, unchanged);
`three/webgpu` imports; meters = three units; XZ ground plane at y=0 — same conventions as
`uglyScene.ts`. Glyphs are **emissive** (bloom-friendly) — see §6 integration note.

---

## 3. Files

All new, all under my territory. **Zero edits** to sibling-owned files.

| file | LOC-ish | role |
|---|---|---|
| `ui/constellation.html` | — | NEW entry point (no audio-gesture splash; pure data view) |
| `ui/src/constellation/runData.ts` | 12.0 KB | CSV parser (header-driven, tolerant) + cause bucketing + summarize + A/B diff. **PURE.** |
| `ui/src/constellation/design.ts` | 6.2 KB | palette, glyph color ramp, synthesized angle/radius/size, schematic arc geometry. **PURE.** |
| `ui/src/constellation/scene.ts` | 12.9 KB | pad+ring+grid+glyphs+arcs+A/B hairlines; raycast pick; filter/highlight. `three` only. |
| `ui/src/constellation/dom.ts` | 19.7 KB | all DOM chrome (strip, chips, card, transition strip, caveat, toast, styles). **NO three** (jsdom-testable). |
| `ui/src/constellation/app.ts` | 6.7 KB | orchestration: renderer + scene + OrbitControls + wires dom callbacks. |
| `ui/src/constellation/types.ts` | 0.5 KB | shared pure types (`Side`, `Pick`) so dom.ts needn't import three. |
| `ui/src/constellation/runData.test.ts` | 21 tests | parser + bucketing; **exact 84/6/9/1 assertion**. |
| `ui/src/constellation/design.test.ts` | 18 tests | angle/radius/size/color math. |
| `ui/src/constellation/dom.test.ts` | 9 tests | **jsdom render check** of the chrome. |
| `ui/src/constellation/__fixtures__/d012_entry_v4.csv` | 8.2 KB | vendored byte-copy of the canonical file (makes the exact-count test self-contained). |
| `ui/src/constellation/__fixtures__/mvar_baseline_s42.csv` | 5.2 KB | vendored A/B fixture (different scenario/population). |
| `ui/scripts/verify_parse.mjs` | — | node-side breadth check across the real `runs/*.csv` corpus. |

**vite.config.ts** — the ONLY edit to an existing file, purely additive:
```diff
+import { resolve } from "node:path";
     rollupOptions: {
       input: {
         main: resolve(__dirname, "index.html"),
         constellation: resolve(__dirname, "constellation.html"),
       },
     },
```
**devDependencies added** (worktree only, for the tests): `@types/node`, `jsdom`, `@types/jsdom`.

**Verified UNCHANGED (byte-identical hash vs main tree):** `src/main.ts`, `src/scene/renderer.ts`,
`src/scene/uglyScene.ts`, `src/scene/floatingOrigin.ts`, `src/net/frame.ts`, `src/net/decode.ts`,
`src/net/interp.ts`, `src/net/client.ts`, `src/fx/plume.ts`, `index.html`.

---

## 4. Test output (gates)

### 4.1 typecheck + vitest — **74/74 green**
```
> tsc --noEmit          (clean, no output)

 ✓ src/net/decode.test.ts             (4)      ← pre-existing, untouched
 ✓ src/net/frame.test.ts              (22)     ← pre-existing, untouched
 ✓ src/constellation/runData.test.ts  (21)
 ✓ src/constellation/design.test.ts   (18)
 ✓ src/constellation/dom.test.ts      (9)   [jsdom]
 Test Files  5 passed (5)
      Tests  74 passed (74)
```
The original **26/26 net tests are untouched and green**; **+48 new**.

### 4.2 The exact-count assertion (the S2B gate) — asserted TWICE, unconditionally
**In `runData.test.ts`** (data spine, against the vendored fixture):
```
d012_entry_v4.csv — the canonical population (EXACT counts)
  ✓ parses all 100 runs with zero skips
  ✓ classifies to EXACTLY 84 landed / 6 off-pad / 9 too-hard / 1 fuel-out
  ✓ the buckets sum to the total (no run unclassified)
  ✓ landedFrac == 0.84
  ✓ the single fuel-out run is run 14 (fault==1, off-pad, would be miscounted without precedence)
```
**In `dom.test.ts`** (the summary strip actually renders it, in jsdom):
```
  ✓ renders 84/100 landed 84% and per-cause tallies from the real file
      strip contains "84/100 landed", "84%", /OFF-PAD 6/, /TOO-HARD 9/, /FUEL-OUT 1/
      chip tallies: landed=84, off-pad=6, too-hard=9, fuel-out=1
```

### 4.3 Vite production build — **green, multi-page**
```
dist/index.html                         0.90 kB
dist/constellation.html                 1.31 kB
dist/assets/constellation-*.js         41.72 kB │ gzip: 13.03 kB   ← the CONSTELLATION module
dist/assets/three.module-*.js         773.32 kB │ gzip: 212.70 kB  ← shared (pre-existing)
✓ built in ~7 s
```
(The >500 kB chunk warning is three.js itself — pre-existing, applies to the main page too.)

### 4.4 Node-side verification — `node scripts/verify_parse.mjs` — **ALL CHECKS PASS**
- `d012_entry_v4.csv` → **total=100 landed=84 off-pad=6 too-hard=9 fuel-out=1** ⇒ PASS
- **44 run-schema CSVs** from `runs/` (48–1000 runs each, across entry/aero/chaos/terminal
  scenarios) **all parse without throwing**, and **every one's buckets sum to its total** (no
  unclassified run).
- `dist/constellation.html` present and **wires the constellation JS chunk** ⇒ YES.

### 4.5 Live dev-server smoke
The worktree dev server (vite on an isolated port) serves the real page
(markers `MC CONSTELLATION` + `id="app"` present) and **transforms every constellation module
cleanly** (`app.ts`/`dom.ts`/`scene.ts`/`runData.ts` all 200, valid compiled ESM with resolved
`OrbitControls` dep).

---

## 5. Honest caveats

- **Schematic ribbons/bearings** (§2.2): the descent arcs and the *angular* placement of glyphs are
  synthesized (golden-angle by run index), **not** the true ground track. **Radius (`td_lat`) is
  real; bearing is not.** Labeled in-UI (persistent caveat bar) and in code. True ribbons need
  recorded trajectories (`.bltlm`) — **wave F2**.
- **Visual check of the 3D WebGPU render is deferred to integration.** Per canon, *"headless
  preview hangs"* for WebGPU, and this session's preview port was held by a sibling's main-tree
  dev server (must not disrupt). What IS verified headlessly: the build emits the page + chunk; the
  node parse test proves the data; the **jsdom test proves the interactive chrome renders** with
  correct counts/card/transition strip. The GPU canvas (glyph meshes, ring, grid, bloom) gets eyes
  at F2 integration — same posture the sibling S1/S3 agents reported.
- **A/B glyph overlap** is handled by a small tangential nudge (A inboard, B outboard) so paired
  glyphs are both visible; the hairline still connects the true radii.

---

## 6. F2 integration notes (folding into the cockpit)

1. **Bloom.** Glyphs emit `emissive` (color = cause), expecting scene-wide bloom. This **matches
   the fe-scene (deog3y08) integration note**: *bloom post-pass is NOT wired in `renderer.ts` yet;
   plume emits HDR+additive and will bloom once a WebGPU `RenderPipeline` + emissive-bloom pass is
   added scene-wide.* The constellation glyphs ride that same pass — no change needed here, they'll
   light up when bloom lands.
2. **Mounting.** The module is a self-contained `boot()` in `app.ts`. To host it inside the cockpit
   instead of as a separate page, call `buildConstellationScene(scene, camera, runs, opts)` against
   the cockpit's existing renderer/scene/camera and drive the `dom.ts` chrome from the cockpit's
   panel system (or keep it as a route/overlay). The scene builder is renderer-agnostic (takes a
   `Scene` + `PerspectiveCamera`).
3. **Real ribbons (the F2 payoff).** When `.bltlm` recordings exist, replace `design.schematicArc`
   + `design.synthAngle` with the recorded ground track: the glyph gets its **true bearing** and the
   arc becomes the **true descent ribbon** (canon signature 1's full form). The end-state
   constellation (pad/ring/glyphs) stays exactly as-is — only the *paths* become real. The
   `ClassifiedRun.index` seam and the `schematicArc`/`synthAngle` functions are the only touch
   points.
4. **The `mcdiff` A/B (D-013 `tools/mcdiff` lifted to 3D)** is already here as the flip-hairline
   view. When trajectories land, the hairline can extend to a full A-vs-B ribbon divergence.
5. **CSV source.** `loadRunCsv(text)` is the single entry; `diffRuns(a,b)` the A/B seam. Both pure,
   fully tested. The shell can also drive `--headless` batches into the same loader.
6. **Shared-file drift.** My worktree is a snapshot taken before the sibling `fe-scene` added
   `net/events.ts` to the main tree. The constellation module imports **only** `scene/renderer.ts`
   and pure files under `constellation/` — it does **not** import `events.ts`/`interp.ts`/`client.ts`,
   so it drops into the current tree without conflict. Re-run `pnpm typecheck && pnpm test && pnpm
   build` after the merge to confirm.

---

## 7. Reproduce

```
# in the worktree (or after F2 merge, in ui/)
pnpm install --frozen-lockfile      # + @types/node, jsdom (added)
pnpm typecheck                      # clean
pnpm test                           # 74/74
pnpm build                          # dist/constellation.html + constellation-*.js
node scripts/verify_parse.mjs       # d012 = 84/6/9/1 + 44-CSV breadth
pnpm exec vite --port 5190          # then open http://127.0.0.1:5190/constellation.html
                                    # drop runs/d012_entry_v4.csv (or Load CSV A);
                                    # Load CSV B = runs/mvar_baseline_s42.csv for the A/B diff
```
