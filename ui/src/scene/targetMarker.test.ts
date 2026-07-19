// targetMarker.test.ts — pure-math tests for the target-estimate marker
// (canon v2 §8.4/§10.9). Covers the covariance-ellipse eigen-decomposition, the
// staleness fade, and the safe optional frame reader that keeps the marker
// dormant on a v3 stream.

import { describe, it, expect } from "vitest";
import {
  covEllipse,
  stalenessAlpha,
  readTargetEst,
  SRC_COLORS,
  TargetSrc,
} from "./targetMarker";

describe("covEllipse", () => {
  it("isotropic covariance -> circle with radius k·σ", () => {
    // σ² = 4 on both axes, no correlation: 2σ ellipse = circle of radius 2·2 = 4
    const e = covEllipse([4, 4, 0], 2);
    expect(e.a).toBeCloseTo(4, 10);
    expect(e.b).toBeCloseTo(4, 10);
  });

  it("axis-aligned anisotropic covariance -> axes = k·σx, k·σy, θ = 0", () => {
    const e = covEllipse([9, 1, 0], 2);
    expect(e.a).toBeCloseTo(6, 10); // 2·√9
    expect(e.b).toBeCloseTo(2, 10); // 2·√1
    expect(e.thetaRad).toBeCloseTo(0, 10);
  });

  it("correlated covariance rotates the major axis to 45°", () => {
    // xx == yy with positive xy -> principal axis at π/4
    const e = covEllipse([5, 5, 3], 1);
    expect(e.thetaRad).toBeCloseTo(Math.PI / 4, 10);
    expect(e.a).toBeCloseTo(Math.sqrt(8), 10); // λ+ = 5+3
    expect(e.b).toBeCloseTo(Math.sqrt(2), 10); // λ− = 5−3
  });

  it("degenerate / hostile covariance never yields NaN", () => {
    for (const cov of [
      [0, 0, 0],
      [1e-30, 1e-30, 0],
      [1, 1, 5], // xy² > xx·yy — not positive-semidefinite
      [-1, -1, 0], // negative variances (corrupt input)
    ] as const) {
      const e = covEllipse(cov as unknown as [number, number, number]);
      expect(Number.isFinite(e.a)).toBe(true);
      expect(Number.isFinite(e.b)).toBe(true);
      expect(Number.isFinite(e.thetaRad)).toBe(true);
      expect(e.a).toBeGreaterThanOrEqual(0);
      expect(e.b).toBeGreaterThanOrEqual(0);
    }
  });
});

describe("stalenessAlpha", () => {
  it("fresh estimates render at full alpha", () => {
    expect(stalenessAlpha(0)).toBe(1);
    expect(stalenessAlpha(1.0)).toBe(1);
  });
  it("fades monotonically to the dim floor by ~10 s", () => {
    const a2 = stalenessAlpha(2);
    const a5 = stalenessAlpha(5);
    const a10 = stalenessAlpha(10);
    expect(a2).toBeLessThan(1);
    expect(a5).toBeLessThan(a2);
    expect(a10).toBeCloseTo(0.25, 10);
    expect(stalenessAlpha(60)).toBeCloseTo(0.25, 10); // clamps at the floor
  });
});

describe("readTargetEst (the v3-safe optional reader)", () => {
  it("returns null on a v3 frame (fields absent) — marker stays dormant", () => {
    expect(readTargetEst({ mach: 1.2, nEng: 1, predImpact: [0, 0] })).toBeNull();
    expect(readTargetEst(null)).toBeNull();
    expect(readTargetEst(undefined)).toBeNull();
    expect(readTargetEst(42)).toBeNull();
  });

  it("returns null on malformed estimate fields", () => {
    expect(readTargetEst({ targetEstXY: [1], targetCov: [1, 1, 0] })).toBeNull();
    expect(readTargetEst({ targetEstXY: [1, NaN], targetCov: [1, 1, 0] })).toBeNull();
    expect(readTargetEst({ targetEstXY: [1, 2], targetCov: [1, 1] })).toBeNull();
  });

  it("parses a well-formed v4 frame, defaulting the optional fields", () => {
    const est = readTargetEst({
      targetEstXY: [12.5, -3.25],
      targetCov: [4, 1, 0.5],
    });
    expect(est).not.toBeNull();
    expect(est!.xy).toEqual([12.5, -3.25]);
    expect(est!.cov).toEqual([4, 1, 0.5]);
    expect(est!.vxy).toEqual([0, 0]);
    expect(est!.src).toBe(TargetSrc.Fixed);
    expect(est!.age).toBe(0);
    expect(est!.valid).toBe(true);
  });

  it("carries velocity / src / age / valid through when present", () => {
    const est = readTargetEst({
      targetEstXY: [0, 0],
      targetEstVXY: [1.5, -0.5],
      targetCov: [1, 1, 0],
      targetSrc: TargetSrc.Perceived,
      targetAge: 3.5,
      targetValid: 0,
    });
    expect(est).not.toBeNull();
    expect(est!.vxy).toEqual([1.5, -0.5]);
    expect(est!.src).toBe(TargetSrc.Perceived);
    expect(est!.age).toBe(3.5);
    expect(est!.valid).toBe(false); // pre-acquisition: marker hides
  });

  it("every source tag has a provenance color", () => {
    for (const src of [
      TargetSrc.Fixed,
      TargetSrc.Seeded,
      TargetSrc.Beacon,
      TargetSrc.Perceived,
      TargetSrc.Drag,
    ]) {
      expect(SRC_COLORS[src]).toHaveLength(3);
    }
  });
});
