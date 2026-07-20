// sea.test.ts — the SEA ocean's Gerstner surface math (pure; the visual look needs
// eyes-on, but the displacement + analytic normal are deterministic and pinned here).

import { describe, it, expect } from "vitest";
import { gerstnerSample, OCEAN_WAVES, type GerstnerWave } from "./sea";

const TWO_PI = Math.PI * 2;

describe("gerstnerSample", () => {
  it("a flat sea (no waves) is undisplaced with a straight-up normal", () => {
    const g = gerstnerSample(12, -7, 3.5, []);
    expect(g.ox).toBe(0);
    expect(g.oy).toBe(0);
    expect(g.oz).toBe(0);
    expect(g.nx).toBe(0);
    expect(g.ny).toBe(1);
    expect(g.nz).toBe(0);
  });

  it("always returns a unit normal", () => {
    for (const [x, z, t] of [
      [0, 0, 0],
      [37, -19, 2.2],
      [-123, 88, 9.9],
      [1000, 1000, 41.0],
    ] as const) {
      const g = gerstnerSample(x, z, t);
      const len = Math.hypot(g.nx, g.ny, g.nz);
      expect(len).toBeCloseTo(1, 6);
    }
  });

  it("is deterministic (same inputs -> identical output)", () => {
    const a = gerstnerSample(5, 5, 1.25);
    const b = gerstnerSample(5, 5, 1.25);
    expect(a).toEqual(b);
  });

  it("the surface is never flat and its normal stays in the upper hemisphere", () => {
    let sawDisplacement = false;
    for (let x = 0; x < 200; x += 13) {
      for (let z = 0; z < 200; z += 17) {
        const g = gerstnerSample(x, z, 4.0);
        if (Math.abs(g.oy) > 1e-3) sawDisplacement = true;
        // a plausible sea never overhangs: the up-component of the normal is positive
        expect(g.ny).toBeGreaterThan(0);
      }
    }
    expect(sawDisplacement).toBe(true);
  });

  it("matches the closed form for a single wave at a chosen phase", () => {
    // one +x wave; pick (x,t) so the phase is 0 => sin=0 (no height), cos=1 (max pinch)
    const w: GerstnerWave = { dirX: 1, dirZ: 0, amp: 0.5, len: 40, speed: 0, steep: 0.6 };
    const g = gerstnerSample(0, 0, 0, [w]);
    const k = TWO_PI / 40;
    expect(g.oy).toBeCloseTo(0, 6); // sin(0)=0
    expect(g.ox).toBeCloseTo(0.6 * 0.5 * 1, 6); // steep*amp*cos(0)
    expect(g.oz).toBeCloseTo(0, 6);
    // normal at phase 0: nx = -dx*k*amp*cos(0) = -k*amp; ny = 1 - Q*k*amp*sin(0) = 1
    const nxRaw = -k * 0.5;
    const nl = Math.hypot(nxRaw, 1, 0);
    expect(g.nx).toBeCloseTo(nxRaw / nl, 6);
    expect(g.ny).toBeCloseTo(1 / nl, 6);
  });

  it("advances the crest downstream over time (a +x wave moves +x)", () => {
    const w: GerstnerWave = { dirX: 1, dirZ: 0, amp: 0.5, len: 40, speed: 5, steep: 0.5 };
    // height at a fixed point should change as the wave travels
    const h0 = gerstnerSample(0, 0, 0, [w]).oy;
    const h1 = gerstnerSample(0, 0, 0.5, [w]).oy;
    expect(h0).not.toBeCloseTo(h1, 3);
  });

  it("ships a small, non-degenerate default sea spectrum", () => {
    expect(OCEAN_WAVES.length).toBeGreaterThanOrEqual(3);
    for (const w of OCEAN_WAVES) {
      expect(w.amp).toBeGreaterThan(0);
      expect(w.len).toBeGreaterThan(0);
      expect(Math.hypot(w.dirX, w.dirZ)).toBeGreaterThan(0);
    }
    // amplitudes should descend (energy in the longer swells) — a plausible sea
    expect(OCEAN_WAVES[0].amp).toBeGreaterThan(OCEAN_WAVES[OCEAN_WAVES.length - 1].amp);
  });
});
