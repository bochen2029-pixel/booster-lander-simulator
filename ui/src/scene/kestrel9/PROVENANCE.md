# Kestrel-9 renderer — provenance

`kestrel9.js` and `plume.js` in this directory are **verbatim** copies of
`assets/kestrel9_gfx/booster/{kestrel9,plume}.js` from the operator-supplied archive
`C:\Booster Lander Simulator Graphics.zip` (exported 2026-09-11 16:41, 12.5 MB). Hashes of the
vendored bytes are pinned in `PROVENANCE.sha256` (`sha256sum -c PROVENANCE.sha256` from this
directory must pass). Do not edit the two `.js` files; every adaptation lives beside them:

| file | role |
| --- | --- |
| `kestrel9.d.ts`, `plume.d.ts` | hand-written type surface for the two JS modules |
| `threeShim.ts` | the injected `THREE` namespace. Default = the real `three/webgpu` (canvas maps sample fine for these materials — measured 2026-09-12, D-056); `?k9tex=data` swaps in a `CanvasTexture` backed by `DataTexture` pixels, kept for the day a three bump reopens commit 908bc53's finding |
| `pixels.ts` | pure pixel helper (row flip) the shim uses, unit-tested |
| `kestrelTlm.ts` | the camelCase → snake_case field adapter (`TlmFrame` → the packet's own names) + `bell_alt` |
| `kestrelVehicle.ts` | the scene handle: builds booster + plume, rebuilds wholesale on HELLO, per-frame update |

The models are the asset author's reading of canon `CLAUDE_v1.md` §5 / §10.3 / §10.7 / §11.5–11.6;
the frame contract (`group` authored in the sim body frame already permuted to three, origin at the
base plane) matches `ui/src/net/frame.ts` exactly, so `boosterPivot` drives it with the one canonical
conversion and nothing here knows about sim Z-up.

The asset's `README.md` (integration notes, knobs, the STL export path for FluidX3D) stays at
`assets/kestrel9_gfx/booster/README.md`.
