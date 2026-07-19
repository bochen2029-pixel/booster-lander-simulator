// timeline.test.ts — the pure time->x mapping for the EVT scrubber (S1 display-only).

import { describe, it, expect } from "vitest";
import { timeToFrac } from "./timeline";

describe("timeToFrac", () => {
  it("maps t0 to 0 and tNow to 1", () => {
    expect(timeToFrac(0, 0, 100)).toBe(0);
    expect(timeToFrac(100, 0, 100)).toBe(1);
  });
  it("is linear in between", () => {
    expect(timeToFrac(25, 0, 100)).toBeCloseTo(0.25, 9);
    expect(timeToFrac(60, 20, 120)).toBeCloseTo(0.4, 9);
  });
  it("clamps out-of-window times", () => {
    expect(timeToFrac(-10, 0, 100)).toBe(0);
    expect(timeToFrac(200, 0, 100)).toBe(1);
  });
  it("degrades gracefully for a zero-width window", () => {
    expect(timeToFrac(5, 5, 5)).toBe(0);
  });
});
