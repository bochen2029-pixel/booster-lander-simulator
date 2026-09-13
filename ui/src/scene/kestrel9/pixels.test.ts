import { describe, it, expect } from "vitest";
import { flipRowsRGBA } from "./pixels";

describe("flipRowsRGBA", () => {
  it("reverses row order and keeps each row's bytes in place", () => {
    // 2 px wide, 3 rows: row r has bytes r*10 + [0..7]
    const w = 2, h = 3;
    const src = new Uint8ClampedArray(w * h * 4);
    for (let r = 0; r < h; r++) for (let i = 0; i < w * 4; i++) src[r * w * 4 + i] = r * 10 + i;
    const out = flipRowsRGBA(src, w, h);
    expect(out.length).toBe(src.length);
    // new row 0 == old row 2
    for (let i = 0; i < w * 4; i++) expect(out[i]).toBe(20 + i);
    // new row 1 == old row 1 (unchanged middle)
    for (let i = 0; i < w * 4; i++) expect(out[w * 4 + i]).toBe(10 + i);
    // new row 2 == old row 0
    for (let i = 0; i < w * 4; i++) expect(out[2 * w * 4 + i]).toBe(i);
  });

  it("is an involution", () => {
    const w = 3, h = 4;
    const src = new Uint8ClampedArray(w * h * 4).map((_, i) => (i * 37) & 255);
    const twice = flipRowsRGBA(flipRowsRGBA(src, w, h), w, h);
    expect(Array.from(twice)).toEqual(Array.from(src));
  });

  it("handles a single row as identity", () => {
    const src = new Uint8ClampedArray([1, 2, 3, 4, 5, 6, 7, 8]);
    expect(Array.from(flipRowsRGBA(src, 2, 1))).toEqual([1, 2, 3, 4, 5, 6, 7, 8]);
  });
});
