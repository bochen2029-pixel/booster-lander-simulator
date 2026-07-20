# FE-SEA — the ocean + ASDS droneship deck scene (the renderer half of the moving deck)

**Date:** 2026-07-20 · **Scope:** `ui/src/scene/sea.ts` (new) + `documentaryScene.ts` wiring + a DEV
inspection hook. **Gates:** typecheck CLEAN · vitest **170/170** (7 new) · `vite build` OK (both pages).

## What this is
The renderer counterpart to Target Stage-1 (core D-035→D-037). The sim now streams the droneship deck's
**heave** (`deck_z`), **horizontal station** (`target_est_xy`), and the SEA-active flag on the v4 wire —
but the scene was a daytime LAND landing (LZ-1 scrubland + concrete pad), so the moving deck was invisible.
This adds a SEA environment that lights up when the deck is active and poses it **exactly from the wire**.

## What was built
- **`scene/sea.ts` (new)** — `buildSea(padRadius)` → a `SeaEnv`:
  - **Ocean:** a CPU **Gerstner** wave grid (129×129, 1.6 km, analytic normals + depth/foam vertex colors)
    around the action, over a flat blue disc to the fog horizon. The swell is a small default spectrum
    (4 components) — **decorative garnish** (canon §0.8), NOT the sim's P-M spectrum (that phase-lock is the
    §11.9 HELLO-spectrum-transmission follow-up).
  - **Droneship deck:** a steel barge plate (top face at local y=0), a blast-wall rim, an equipment/container
    silhouette, and a white landing bullseye (concentric rings + LZ-X bars) sized from HELLO `pad_radius`.
  - `update(deckPos, deckQuat, t)` poses the deck **exactly** from the wire (three-space, via frame.ts) and
    animates the swell. `setActive()` toggles the whole env; `setPadRadius()` resizes the bullseye.
- **`documentaryScene.ts` wiring** — the LAND elements moved into a `landGroup`; each frame, when
  `flags & TLM_FLAG_SEA_ACTIVE`, the land hides and the SEA env shows with the deck at
  `simToThreePosition(target_est_xy[0], target_est_xy[1], deck_z)` — i.e. it **heaves in Y and drifts in
  X/Z exactly as the plant computes**. TELEMETRY-HONEST: only the DECK is truth; the ocean is dressing.

## Verification (no pixel verification — see below)
- **Gerstner math** — `sea.test.ts` (7 tests): flat sea is undisplaced with a straight-up normal; the normal
  is always unit + upper-hemisphere; determinism; closed-form match for a single wave; the crest travels
  downstream over time; a non-degenerate default spectrum.
- **Runtime (eval, against the live scene)** — `setActive(true)` shows the env; `update({x:5,y:1.23,z:-3})`
  posed the deck to **exactly [5, 1.23, -3]** (heave y = deck_z, drift x/z); the ocean grid is **16,641
  verts** displacing to a max wave height **1.242 m** (= Σ amplitudes 1.25 m). Structure: sea has 3 children
  (far disc + wave grid + deck), deck has 9 parts.
- **Gates:** typecheck clean, 170/170 tests, build OK (main bundle 73→84 kB; DEV hook tree-shaken out).

## ⚠ NOT verified — needs your eyes (headless WebGPU screenshots stall in this env, per the FE reports)
Run it: `pnpm -C ui dev` + `.\build\bin\Release\booster-core.exe --serve --scenario aero_offset --seed 42
--sea 1.5 --sea-wander 3 --port 8787`, then watch the deck heave/drift under the booster.
Tuning items (all sensible defaults, none blocking):
1. **Ocean palette / foam / specular** — colors (`SEA_DEEP/SHALLOW/FOAM`), roughness 0.18, crest-foam
   thresholds are guesses; the water look wants eyes.
2. **Wave amplitude vs deck_z coherence** — the decorative swell (~±1.25 m) vs the true deck heave: if they
   read oddly, scale `OCEAN_WAVES` amps or blend the near-grid toward `deck_z` at the deck.
3. **Deck proportions / detail** — barge size (`deckLen/Wid`), rim, containers, bullseye are schematic ASDS.
4. **Deck TILT** — `deck_quat` (pitch/roll) is NOT streamed yet (the sim passes NULL to `sea_deck_pose`), so
   the deck stays level; wiring is ready (`sea.update` accepts a quat) — enable when the sim streams it (the
   §F tilted-normal-contact work).
5. **near-grid → far-disc seam** at ~800 m, and fog interaction; the near grid follows the deck horizontally.

## Files
`ui/src/scene/sea.ts` (+`sea.test.ts`), `ui/src/scene/documentaryScene.ts` (wiring), `ui/src/main.ts`
(DEV `__doc` inspection hook, DEV-guarded). `core/` untouched; no protocol change.
