// markers.test.ts — pure marker math (canon §B.7). The visual objects need eyes-on
// at integration; the MATH (trail decay, convergence, ignite breathe, miss dist) is
// deterministic and tested here.

import { describe, it, expect } from "vitest";
import {
  trailAlpha,
  solveConvergence,
  igniteBreathe,
  padMissDistance,
  TRAIL_LEN,
} from "./markers";

describe("trailAlpha", () => {
  it("is brightest at the head (age 0) and dark at the tail", () => {
    expect(trailAlpha(0)).toBe(1);
    expect(trailAlpha(TRAIL_LEN - 1)).toBe(0);
  });
  it("is monotonically non-increasing with age", () => {
    let prev = Infinity;
    for (let i = 0; i < TRAIL_LEN; i++) {
      const a = trailAlpha(i);
      expect(a).toBeLessThanOrEqual(prev + 1e-9);
      prev = a;
    }
  });
  it("stays in [0,1]", () => {
    for (let i = 0; i < TRAIL_LEN; i++) {
      const a = trailAlpha(i);
      expect(a).toBeGreaterThanOrEqual(0);
      expect(a).toBeLessThanOrEqual(1);
    }
  });
  it("handles a degenerate length of 1", () => {
    expect(trailAlpha(0, 1)).toBe(1);
  });
});

describe("solveConvergence", () => {
  it("is 1 when the prediction is dead on the pad center", () => {
    expect(solveConvergence([0, 0], [0, 0])).toBe(1);
  });
  it("is ~0 at or beyond the normalizing radius", () => {
    expect(solveConvergence([2000, 0], [0, 0], 2000)).toBeCloseTo(0, 6);
    expect(solveConvergence([5000, 0], [0, 0], 2000)).toBe(0); // clamped, not negative
  });
  it("rises monotonically as the prediction slides onto the pad", () => {
    const far = solveConvergence([1500, 0], [0, 0]);
    const mid = solveConvergence([800, 0], [0, 0]);
    const near = solveConvergence([100, 0], [0, 0]);
    expect(mid).toBeGreaterThan(far);
    expect(near).toBeGreaterThan(mid);
  });
  it("uses euclidean miss (offset pad)", () => {
    // pad at (250,-180); prediction there = perfect
    expect(solveConvergence([250, -180], [250, -180])).toBe(1);
  });
});

describe("igniteBreathe", () => {
  it("returns ~1 (minimal breathe) exactly at the ring", () => {
    expect(igniteBreathe(1360, 1360)).toBeCloseTo(1, 6);
  });
  it("breathes more (larger) when far above the ring", () => {
    const atRing = igniteBreathe(1360, 1360);
    const farAbove = igniteBreathe(5000, 1360);
    expect(farAbove).toBeGreaterThan(atRing);
  });
  it("stays within a gentle band [1, 1.12]", () => {
    for (const alt of [0, 500, 1360, 3000, 62000]) {
      const b = igniteBreathe(alt, 1360);
      expect(b).toBeGreaterThanOrEqual(1);
      expect(b).toBeLessThanOrEqual(1.12 + 1e-9);
    }
  });
  it("is a no-op when the ignition altitude is unset (<=0)", () => {
    expect(igniteBreathe(3000, 0)).toBe(1);
  });
});

describe("padMissDistance", () => {
  it("is the euclidean distance to the pad center", () => {
    expect(padMissDistance([3, 4], [0, 0])).toBeCloseTo(5, 6);
  });
  it("defaults the pad to the origin", () => {
    expect(padMissDistance([0, 0])).toBe(0);
  });
});
