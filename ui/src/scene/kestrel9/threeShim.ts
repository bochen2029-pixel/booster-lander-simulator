// threeShim.ts — the THREE namespace handed to the vendored kestrel9.js / plume.js.
//
// Both modules take THREE by injection (they import nothing) and build every texture on a 2D canvas
// via `new THREE.CanvasTexture(canvas)`. Commit 908bc53 (2026-07-21) recorded that CanvasTexture
// colour maps did NOT sample on this app's three r185 WebGPU backend (sea.ts:20,
// documentaryScene.ts:240), so this shim exists: a shallow copy of three/webgpu whose `CanvasTexture`
// is a DataTexture built from the canvas pixels at construction. Everything else (wrap, repeat,
// anisotropy, colorSpace) is set by the modules AFTER construction and lands on either class.
//
// MEASURED 2026-09-12 (D-056): the two paths render pixel-identically for these materials — plain
// MeshStandardMaterial with `map`/`roughnessMap` (runs/d056/k9canvas_hero.jpg vs k9data_hero.jpg,
// same camera, same frame). The 07-21 hull was a MeshStandardNodeMaterial whose colorNode overrides
// `map` by design, which is the likeliest reading of the old finding. So the DEFAULT is the real
// class, as the asset ships; `?k9tex=data` selects the shim if a future three bump reopens it.
import * as THREE from "three/webgpu";
import { flipRowsRGBA } from "./pixels";

export type ThreeLike = typeof THREE;
export type KestrelTextureMode = "data" | "canvas";

/** A DataTexture that looks like a CanvasTexture to its callers: same source, same orientation. */
export class DataCanvasTexture extends THREE.DataTexture {
  constructor(canvas: HTMLCanvasElement) {
    const w = canvas.width;
    const h = canvas.height;
    const ctx = canvas.getContext("2d");
    const px = ctx ? ctx.getImageData(0, 0, w, h).data : new Uint8ClampedArray(w * h * 4);
    super(flipRowsRGBA(px, w, h), w, h, THREE.RGBAFormat, THREE.UnsignedByteType);
    // CanvasTexture defaults (DataTexture would be nearest-filtered with no mipmaps).
    this.magFilter = THREE.LinearFilter;
    this.minFilter = THREE.LinearMipmapLinearFilter;
    this.generateMipmaps = true;
    this.flipY = false; // orientation already baked by the row flip
    this.needsUpdate = true;
  }
}

/** The texture path the page asked for: default = the real CanvasTexture; `?k9tex=data` = the shim. */
export function kestrelTextureMode(): KestrelTextureMode {
  try {
    return new URLSearchParams(location.search).get("k9tex") === "data" ? "data" : "canvas";
  } catch {
    return "canvas";
  }
}

/** Build the namespace to inject. `three/webgpu` re-exports the whole core, so the copy is complete. */
export function makeKestrelThree(mode: KestrelTextureMode = kestrelTextureMode()): ThreeLike {
  if (mode === "canvas") return THREE;
  return { ...THREE, CanvasTexture: DataCanvasTexture } as unknown as ThreeLike;
}
