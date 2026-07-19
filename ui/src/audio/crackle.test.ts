// crackle.test.ts — the CRACKLE signature (canon §B.8 / D-011).
//
// Canon pins rocket crackle as "Poisson steep-asymmetric shocklets, rate ∝ throttle,
// POSITIVE SKEWNESS 0.1–0.5 — the measured signature". These tests assert that our
// procedural generator reproduces that literally:
//   • the shocklet inter-arrival process is Poisson (mean count ≈ λ·dt, exponential
//     gaps → CV of gaps ≈ 1),
//   • rate scales with thrust (throttle × n_eng) and gates off below the throttle
//     floor,
//   • the rendered pressure trace has POSITIVE SKEWNESS (the falsifiable fingerprint),
//   • it is deterministic given a seed (reproducible → testable, never a "loop").

import { describe, it, expect } from "vitest";
import {
  mulberry32,
  expSample,
  crackleRate,
  shockletArrivals,
  shockletSample,
  skewness,
  renderCrackleTrace,
  CRACKLE,
  SHOCKLET,
} from "./crackle";

describe("seeded RNG + exponential", () => {
  it("mulberry32 is deterministic and in [0,1)", () => {
    const a = mulberry32(12345);
    const b = mulberry32(12345);
    for (let i = 0; i < 100; i++) {
      const x = a();
      expect(x).toBe(b());
      expect(x).toBeGreaterThanOrEqual(0);
      expect(x).toBeLessThan(1);
    }
  });

  it("expSample is non-negative and ~mean 1 over many draws", () => {
    const r = mulberry32(7);
    let sum = 0;
    const N = 20000;
    for (let i = 0; i < N; i++) {
      const e = expSample(r());
      expect(e).toBeGreaterThanOrEqual(0);
      sum += e;
    }
    expect(sum / N).toBeCloseTo(1, 1); // mean of Exp(1) is 1
  });
});

describe("crackle rate ∝ thrust", () => {
  it("gates off with no engines or below the throttle floor", () => {
    expect(crackleRate(1.0, 0)).toBe(0);
    expect(crackleRate(0.3, 3)).toBe(0); // below 0.4 floor
    expect(crackleRate(0.0, 9)).toBe(0);
  });

  it("increases with throttle and engine count", () => {
    const one = crackleRate(1.0, 1);
    const three = crackleRate(1.0, 3);
    expect(three).toBeGreaterThan(one);
    expect(three).toBeCloseTo(one * 3, 5); // linear in n_eng
    expect(crackleRate(1.0, 3)).toBeGreaterThan(crackleRate(0.6, 3)); // rises with throttle
    expect(crackleRate(1.0, 1)).toBeCloseTo(CRACKLE.ratePerEngineFull, 5);
  });
});

describe("Poisson shocklet arrivals", () => {
  it("mean shocklet COUNT over a window ≈ λ·dt", () => {
    const rate = 500; // /s
    const dt = 1.0;
    let total = 0;
    const trials = 200;
    for (let k = 0; k < trials; k++) {
      const rng = mulberry32(1000 + k);
      total += shockletArrivals(rate, dt, rng).length;
    }
    const mean = total / trials;
    expect(mean).toBeGreaterThan(rate * dt * 0.9);
    expect(mean).toBeLessThan(rate * dt * 1.1);
  });

  it("arrivals are sorted, within [0,dt), and exponential (gap CV ≈ 1)", () => {
    const rng = mulberry32(42);
    const rate = 2000;
    const dt = 2.0;
    const arr = shockletArrivals(rate, dt, rng);
    expect(arr.length).toBeGreaterThan(1000);
    // sorted + in-window
    for (let i = 0; i < arr.length; i++) {
      expect(arr[i]).toBeGreaterThanOrEqual(0);
      expect(arr[i]).toBeLessThan(dt);
      if (i > 0) expect(arr[i]).toBeGreaterThanOrEqual(arr[i - 1]);
    }
    // inter-arrival gaps: coefficient of variation ≈ 1 for a Poisson process
    const gaps: number[] = [];
    for (let i = 1; i < arr.length; i++) gaps.push(arr[i] - arr[i - 1]);
    const mean = gaps.reduce((a, b) => a + b, 0) / gaps.length;
    const varr = gaps.reduce((a, b) => a + (b - mean) * (b - mean), 0) / gaps.length;
    const cv = Math.sqrt(varr) / mean;
    expect(cv).toBeGreaterThan(0.8);
    expect(cv).toBeLessThan(1.2);
    // mean gap ≈ 1/rate
    expect(mean).toBeCloseTo(1 / rate, 4);
  });

  it("empty when rate is zero", () => {
    expect(shockletArrivals(0, 1, mulberry32(1))).toEqual([]);
  });
});

describe("asymmetric shocklet waveform", () => {
  it("is zero before the front, peaks at the shock, decays after", () => {
    expect(shockletSample(-SHOCKLET.width * 2)).toBe(0); // before front
    expect(shockletSample(0)).toBeCloseTo(1, 6); // peak compressive spike at τ=0
    const early = shockletSample(SHOCKLET.width);
    const late = shockletSample(SHOCKLET.width * 8);
    expect(early).toBeGreaterThan(late); // decaying tail
  });

  it("has a rarefaction UNDERSHOOT (goes below zero in the tail) — the asymmetry", () => {
    let sawNegative = false;
    for (let tau = 0; tau < SHOCKLET.width * 40; tau += SHOCKLET.width / 4) {
      if (shockletSample(tau) < -1e-4) {
        sawNegative = true;
        break;
      }
    }
    expect(sawNegative).toBe(true);
  });
});

describe("THE SIGNATURE — positive skewness of the crackle trace", () => {
  it("skewness() matches a hand values", () => {
    // symmetric set → ~0
    expect(Math.abs(skewness([-2, -1, 0, 1, 2]))).toBeLessThan(1e-9);
    // right-tailed set → positive
    expect(skewness([0, 0, 0, 0, 10])).toBeGreaterThan(0);
    // left-tailed → negative
    expect(skewness([-10, 0, 0, 0, 0])).toBeLessThan(0);
  });

  it("rendered crackle trace is POSITIVELY skewed (canon fingerprint)", () => {
    // render a solid second of dense crackle and measure the population skew.
    const rate = crackleRate(1.0, 3); // full thrust, 3 engines
    const rng = mulberry32(2026);
    const trace = renderCrackleTrace(1.0, 48000, rate, rng);
    const sk = skewness(trace);
    // the DEFINING property: positive skewness (steep compressive spikes, long
    // shallow rarefaction). We assert sign + a sane magnitude band, not an exact
    // value (it depends on density), well clear of zero.
    expect(sk).toBeGreaterThan(0.1);
  });

  it("skew is stable in SIGN across seeds (not a fluke of one realisation)", () => {
    const rate = crackleRate(0.8, 1);
    let positives = 0;
    const trials = 12;
    for (let k = 0; k < trials; k++) {
      const trace = renderCrackleTrace(0.5, 48000, rate, mulberry32(500 + k * 13));
      if (skewness(trace) > 0) positives++;
    }
    expect(positives).toBe(trials); // every realisation is positively skewed
  });

  it("is deterministic given a seed (reproducible, never a loop)", () => {
    const rate = crackleRate(1.0, 1);
    const a = renderCrackleTrace(0.2, 48000, rate, mulberry32(99));
    const b = renderCrackleTrace(0.2, 48000, rate, mulberry32(99));
    expect(a.length).toBe(b.length);
    for (let i = 0; i < a.length; i += 257) expect(a[i]).toBe(b[i]);
  });

  it("empty/silent trace when thrust gates the rate to zero", () => {
    const trace = renderCrackleTrace(0.2, 48000, crackleRate(0.2, 1), mulberry32(1));
    let energy = 0;
    for (let i = 0; i < trace.length; i++) energy += Math.abs(trace[i]);
    expect(energy).toBe(0);
  });
});
