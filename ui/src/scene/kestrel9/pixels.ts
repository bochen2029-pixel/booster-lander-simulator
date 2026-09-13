// pixels.ts — pure pixel helpers for the vendored renderer's texture path. No three import, no DOM,
// so the vitest node environment can pin the behaviour.

/**
 * Reverse the row order of a tightly packed RGBA8 buffer. Canvas `getImageData` rows are top-first
 * and a `CanvasTexture` samples them with `flipY = true` (row 0 at v = 1); a `DataTexture` defaults
 * to `flipY = false` (row 0 at v = 0). Flipping the rows once at construction reproduces the canvas
 * orientation without relying on the backend honouring `flipY` for data uploads.
 */
export function flipRowsRGBA(src: ArrayLike<number>, w: number, h: number): Uint8Array {
  const rowBytes = w * 4;
  const out = new Uint8Array(rowBytes * h);
  for (let y = 0; y < h; y++) {
    const s = (h - 1 - y) * rowBytes;
    const d = y * rowBytes;
    for (let i = 0; i < rowBytes; i++) out[d + i] = src[s + i] as number;
  }
  return out;
}
