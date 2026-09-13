repo: bochen2029-pixel/booster-lander-simulator
branch: main
path: ui/src/scene

## Last sync
date: 2026-09-11T00:00:00Z
read-only: canon + README (no write-back to the repo from this project)

### Updated in this project
- Added `booster/kestrel9.js` — procedural KESTREL-9 booster model built from CLAUDE_v1 §5 dimensions, zero binary assets.
- Added `booster/plume.js` — analytic plume rooted on the nozzle contour, shock cells from p_amb, SRP wrap by C_T.
- Added `booster-stage.js` + `Kestrel-9 Booster.dc.html` — dev viewer/inspector driven by the §10.3 TLM field names.

## Screen map
| Screen | Built from |
| --- | --- |
| Kestrel-9 Booster.dc.html | CLAUDE_v1.md §5 (vehicle), §10.3 (TLM), §10.7 (frame conversion), §11.3–11.6 (renderer) |
| booster/kestrel9.js | CLAUDE_v1.md §5.1–5.6, §11.5 |
| booster/plume.js | CLAUDE_v1.md §11.6, §6.3 (SRP shielding) |

## Notes
- Repo was read over the public web (GitHub tools not connected). The live tree is
  ahead of the fetched README: canon is now CLAUDE_v2.md with ROADMAP.md/MEMORY.md,
  and the renderer milestone is still gated behind guidance work (E1 expert-iteration
  engine-out teacher). Vehicle geometry was taken from CLAUDE_v1.md §5, which is the
  version published in the tree; re-check §5 in CLAUDE_v2.md before merging and pass
  any changed dimension through `createKestrel9(THREE, { vehicle: ... })`.
- Nothing here touches `core/`. The model is a pure observer: it only reads TLM fields.
