# KESTREL-9 — procedural booster model for `ui/src/scene/`

Two dependency-free ES modules + a dev viewer. Built from `CLAUDE_v1.md` §5
(vehicle), §10.3 (TLM fields), §10.7 (frame conversion), §11.5–11.6 (renderer).
**No binary assets** — every texture is drawn on a canvas at construction time,
so nothing new lands in the repo but source.

| file | role |
| --- | --- |
| `booster/kestrel9.js` | the vehicle: barrel, interstage, 4 grid fins, octaweb + 9 bells, 4 legs, 2 RCS pods, raceway, soot/frost overlays |
| `booster/plume.js` | analytic plume: bell flame, free jet, shock cells, SRP envelope, TEA-TEB flash, plume light + spot |
| `booster/export-stl.js` | closed-solid rebuild + binary STL writer (print in mm, CFD in metres, sim Z-up) |
| `booster-stage.js` | dev-only viewer shell (sky, pad, camera, bloom, dust, inspector rail). Not part of the flight renderer. |

## Integration

`THREE` is injected rather than imported, so the same file works under
`three/webgpu` and the WebGL2 fallback and never fights your pinned version.

```js
import * as THREE from 'three';
import { createKestrel9 } from './kestrel9.js';
import { createPlume } from './plume.js';

const booster = createKestrel9(THREE, { vehicle: hello.vehicle, quality: 'ultra' });
const plume   = createPlume(THREE, booster);
scene.add(booster.group);

// per decoded TLM packet (§10.3) — pass the packet straight through:
booster.setTelemetry(tlm);
plume.setTelemetry(tlm);
plume.update(dt);

// on EVT GREEN_FLASH:
plume.flash();
```

Fields read (all optional, all named exactly as the packet): `throttle_act`,
`n_eng`, `gimbal_act[2]` (rad), `fins_act[4]` (rad), `deploy_frac`, `stroke[4]`
(m), `Q_heat` (→ soot), `p_amb`, `mach`, `qbar`. Two extras the packet doesn't
carry: `soot`/`frost` (0–1 direct override) and `bell_alt` (bell-exit height over
terrain, m — clamps the free jet into a radial wall jet on impingement).

## Frame

`booster.group` is authored **in the sim body frame already permuted to three**
(§10.7): local `+Y` = body `+Z` (toward the interstage), local `+X` = body `+X`,
local `-Z` = body `+Y`; origin = base-plane centre / gimbal plane. So the
renderer only writes `group.position` and `group.quaternion` from the one
canonical conversion function — nothing here needs to know about sim Z-up.
Legs sit at body azimuth 0/90/180/270°, grid fins at 45/135/225/315°.

## What is driven by what

* `throttle_act` → jet length, core/sheath opacity, plume light intensity, throat glow
* `p_amb` → shock-cell spacing (`∝ √(p₀/p_amb)`), jet flare, sheath fade — cells
  stretch out and thin to 1–2 by ~35 km, exactly as §11.6 asks
* `mach` + `qbar` → SRP envelope blended by thrust coefficient `C_T`, so the plume
  wraps forward during the entry burn and snaps back to a trailing jet after
* `n_eng` → which of the nine bells gimbal and produce a plume (1 = centre,
  3 = centre + opposed pair, 9 = all). Non-firing bells never glow.
* `Q_heat` → soot overlay opacity, with clean leg-shadow stripes at the four leg
  azimuths and a gas-generator streak baked into the alpha
* `deploy_frac` → leg swing (4° → 120°) + telescoping extension; `stroke[i]`
  shortens leg *i* for crush-core travel
* `gimbal_act` → centre-engine (and 3-engine pair) gimbal, body X/Y → three X/−Z

Named meshes and materials throughout (`Barrel`, `GridFin0`, `MerlinCenter`,
`LegSeg2_1`, `K9_BodyPaint`, `K9_Titanium`, …) so a TSL material swap later is a
lookup, not a rewrite.

## Knobs

`createKestrel9(THREE, opts)` — `opts.vehicle` overrides any §5 dimension
(`radius`, `barrel`, `interstage`, `legSpan`, `finPanel`, `engineRing`,
`bellExit`, `bellLength`, `blackBand`, `rcsY`, `finAz`, `legAz`); change a number
once and physics + visuals follow. `opts.quality` = `low` | `medium` | `ultra`
sets radial tessellation.

Cost at `ultra`: ~230 draw calls, ~180 k triangles, three 1024×2048 canvas maps
for the barrel plus five small ones. Shadow casting is on for every structural
mesh; the overlays (soot, frost) are depth-write-off and cast nothing.

## STL export (3D print / FluidX3D)

The render mesh is not printable as-is (open cylinders, zero-thickness bells,
14 mm lattice walls, transparent overlays). `export-stl.js` rebuilds the vehicle
from **closed primitives** and writes binary STL in the **sim world frame, Z-up**
(three Y-up → sim via the §10.7 inverse), legs deployed, fins neutral:

```js
import { exportSTL, buildExportModel, toBinarySTL } from './export-stl.js';
exportSTL(THREE, booster, { variant: 'cfd' });                   // kestrel9_cfd_1-1_m.stl, metres
exportSTL(THREE, booster, { variant: 'print', scale: 1000/200 }); // mm at 1:200 (238.5 mm tall)
```

In the viewer the EXPORT rail does the same (pick 1:100 / 1:144 / 1:200).

**Print variant** — drops parts under ~0.5 mm at scale (RCS nozzles, leg struts,
gas-generator duct, gimbal actuators), grid fins become 18 cm solid plates, bell
walls are 5 cm. Overlapping closed shells are intentional; PrusaSlicer/Cura union
them. Print base-down on the four feet + bell tips, or lay flat with supports.

**CFD variant (FluidX3D)** — 1:1 metres, origin at the gimbal plane, +Z up, legs
deployed. Nozzles are hollow to the throat plane (throat cap at z = −0.06 m) so a
velocity inlet can sit there. Load with `read_stl(path)` and
`lbm.voxelize_mesh_on_device(mesh)`; use `mesh->scale()/translate()/rotate()` to
place it in the box. Note the model is a union of closed shells, not a single
manifold — FluidX3D's surface voxelizer handles that, but if you use a
parity-fill voxelizer or need a single skin, run a boolean union first (Blender
→ Remesh/Voxel, or MeshLib). Vehicle box: Ø18 m footprint × 51.3 m tall
(feet at z = −3.6, interstage top at z = 47.7).

For the plume/ground-effect CFD you want, the interesting inputs are already in
telemetry: bell-exit height, `throttle_act` × `n_eng` for the jet, and
`p_amb`. The renderer's dust/wall-jet are the *placeholder* those results replace.

## Deliberately not done here

Raymarched volumetric plume, heat haze (`viewportSharedTexture`), GPU compute
soot particles, Bruneton sky, ground fluid dome — those are the M7/M8 TSL passes
and want the WebGPU node path. This model is built so they drop *on top of* it:
the plume's cone proxy, the bell anchors (`booster.enginePositions`) and the soot
mask are the hooks.
