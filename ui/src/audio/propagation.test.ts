// propagation.test.ts — the PURE acoustic propagation model (canon §B.8 / D-011).
//
// These are the falsifiable invariants of the sketch's physics: retarded-time
// arrival ordering, the absorption-knee spectral reveal (monotone vs range, hits
// the 20 km infrasound anchor), 1/r monotonicity, graceful Doppler clamping, and
// the retarded-time event QUEUE releasing "the silent touchdown then the sound
// wall". No Web Audio, no browser — hand-checkable numbers.

import { describe, it, expect } from "vitest";
import {
  SPEED_OF_SOUND,
  SECONDS_PER_KM,
  slantRange,
  propagationDelay,
  spreadingGain,
  absorptionCutoffHz,
  crackleAudibility,
  radialVelocity,
  dopplerRatio,
  DOPPLER_MAX_RATIO,
  DOPPLER_MIN_RATIO,
  turbulenceDepth,
  turbulenceFlutter,
  computePropagation,
  RetardedEventQueue,
  ABSORPTION,
  type Vec3,
} from "./propagation";

const origin: Vec3 = { x: 0, y: 0, z: 0 };

describe("geometry + retarded time", () => {
  it("slant range is Euclidean", () => {
    expect(slantRange({ x: 3, y: 4, z: 0 }, origin)).toBeCloseTo(5, 9);
    expect(slantRange({ x: 0, y: 0, z: 20000 }, origin)).toBeCloseTo(20000, 6);
  });

  it("propagation delay is distance / c and ~2.92 s per km", () => {
    expect(propagationDelay(343)).toBeCloseTo(1, 9);
    expect(propagationDelay(1000)).toBeCloseTo(SECONDS_PER_KM, 9);
    expect(SECONDS_PER_KM).toBeCloseTo(1000 / SPEED_OF_SOUND, 9);
    // a 20 km slant is a ~58 s wait — the canon "sound wall"
    expect(propagationDelay(20000)).toBeCloseTo(58.3, 1);
  });
});

describe("1/r spreading gain", () => {
  it("is 1.0 at/inside the reference distance and falls as 1/r beyond", () => {
    expect(spreadingGain(50)).toBe(1);
    expect(spreadingGain(100)).toBe(1);
    expect(spreadingGain(200)).toBeCloseTo(0.5, 9);
    expect(spreadingGain(1000)).toBeCloseTo(0.1, 9);
  });

  it("is monotonically non-increasing in distance and always finite in [0,1]", () => {
    let prev = Infinity;
    for (let d = 1; d <= 60000; d += 137) {
      const g = spreadingGain(d);
      expect(g).toBeGreaterThanOrEqual(0);
      expect(g).toBeLessThanOrEqual(1);
      expect(Number.isFinite(g)).toBe(true);
      expect(g).toBeLessThanOrEqual(prev + 1e-12);
      prev = g;
    }
  });
});

describe("frequency-dependent absorption knee (the spectral reveal)", () => {
  it("is wide open up close and infrasonic at 20 km (canon anchors)", () => {
    expect(absorptionCutoffHz(100)).toBe(ABSORPTION.nearCutoffHz); // full bandwidth
    expect(absorptionCutoffHz(20000)).toBeCloseTo(ABSORPTION.farCutoffHz, 6); // ~30 Hz
    expect(absorptionCutoffHz(20000)).toBeLessThan(60); // "pure infrasonic rumble"
  });

  it("is strictly monotone non-increasing across the whole descent range", () => {
    let prev = Infinity;
    for (let d = 50; d <= 40000; d += 97) {
      const c = absorptionCutoffHz(d);
      expect(Number.isFinite(c)).toBe(true);
      expect(c).toBeGreaterThan(0);
      expect(c).toBeLessThanOrEqual(prev + 1e-9);
      prev = c;
    }
  });

  it("crackle FADES IN as range closes: 0 far, 1 near, monotone increasing toward the pad", () => {
    expect(crackleAudibility(20000)).toBe(0); // gone at 20 km
    expect(crackleAudibility(150)).toBe(1); // full up close
    // monotone NON-INCREASING in distance == fades in as range closes
    let prev = -1;
    for (let d = 30000; d >= 100; d -= 111) {
      const a = crackleAudibility(d);
      expect(a).toBeGreaterThanOrEqual(0);
      expect(a).toBeLessThanOrEqual(1);
      expect(a).toBeGreaterThanOrEqual(prev - 1e-9); // increases as d shrinks
      prev = a;
    }
  });
});

describe("Doppler (graceful supersonic clamp)", () => {
  it("radial velocity: positive when receding, negative when closing, 0 tangential", () => {
    // source straight up, moving up (receding) → positive
    expect(radialVelocity(origin, { x: 0, y: 100, z: 0 }, { x: 0, y: 50, z: 0 })).toBeCloseTo(50, 9);
    // moving down (closing) → negative
    expect(radialVelocity(origin, { x: 0, y: 100, z: 0 }, { x: 0, y: -50, z: 0 })).toBeCloseTo(-50, 9);
    // moving sideways (tangential) → ~0
    expect(radialVelocity(origin, { x: 0, y: 100, z: 0 }, { x: 40, y: 0, z: 0 })).toBeCloseTo(0, 9);
  });

  it("receding drops pitch, approaching raises it", () => {
    expect(dopplerRatio(0)).toBeCloseTo(1, 9);
    expect(dopplerRatio(100)).toBeLessThan(1); // receding
    expect(dopplerRatio(-100)).toBeGreaterThan(1); // approaching
  });

  it("clamps supersonic closing to a finite, positive max ratio (never 0/NaN/negative)", () => {
    // closing at exactly Mach 1 and beyond — classical formula would blow up
    for (const vr of [-SPEED_OF_SOUND, -SPEED_OF_SOUND - 500, -5000, -1e6]) {
      const r = dopplerRatio(vr);
      expect(Number.isFinite(r)).toBe(true);
      expect(r).toBeGreaterThan(0);
      expect(r).toBeLessThanOrEqual(DOPPLER_MAX_RATIO);
    }
    // extreme receding stays bounded below
    expect(dopplerRatio(1e6)).toBeGreaterThanOrEqual(DOPPLER_MIN_RATIO);
  });
});

describe("turbulence flutter (garnish atop honest gain)", () => {
  it("depth is zero near, grows with range, clamps to max", () => {
    expect(turbulenceDepth(100)).toBe(0);
    expect(turbulenceDepth(20000)).toBeCloseTo(0.28, 9);
    expect(turbulenceDepth(2000)).toBeGreaterThan(0);
    expect(turbulenceDepth(2000)).toBeLessThan(0.28);
  });

  it("flutter multiplier stays within [1-depth, 1+depth] and is 1.0 up close", () => {
    for (let t = 0; t < 10; t += 0.05) {
      expect(turbulenceFlutter(t, 80)).toBe(1); // steady near source
      const m = turbulenceFlutter(t, 15000);
      expect(m).toBeGreaterThanOrEqual(1 - 0.28 - 1e-9);
      expect(m).toBeLessThanOrEqual(1 + 0.28 + 1e-9);
    }
  });

  it("is deterministic in (t, range)", () => {
    expect(turbulenceFlutter(3.14, 9000)).toBe(turbulenceFlutter(3.14, 9000));
  });
});

describe("computePropagation snapshot", () => {
  it("bundles finite outputs and tracks range", () => {
    const near = computePropagation(origin, { x: 0, y: 200, z: 0 }, { x: 0, y: -30, z: 0 }, 1.0);
    const far = computePropagation(origin, { x: 0, y: 25000, z: 0 }, { x: 0, y: -300, z: 0 }, 1.0);
    for (const p of [near, far]) {
      for (const v of Object.values(p)) expect(Number.isFinite(v)).toBe(true);
    }
    // far away: quieter, more muffled, less crackle, longer delay
    expect(far.gain).toBeLessThan(near.gain);
    expect(far.cutoffHz).toBeLessThan(near.cutoffHz);
    expect(far.crackle).toBeLessThanOrEqual(near.crackle);
    expect(far.delay).toBeGreaterThan(near.delay);
  });
});

describe("RetardedEventQueue — the silent touchdown, then the sound wall", () => {
  it("releases an event only after emitTime + distance/c, exactly once", () => {
    const q = new RetardedEventQueue<string>();
    // touchdown emitted at sim t=10 from 3430 m away → 10 s delay → arrives at t=20
    const listener: Vec3 = { x: 3430, y: 0, z: 0 };
    q.enqueue({ emitTime: 10, emitPos: origin, payload: "clang" }, listener);

    expect(q.inFlight).toBe(1);
    // just before arrival: still silent
    expect(q.advance(19.9)).toEqual([]);
    expect(q.inFlight).toBe(1);
    // at/after arrival: the sound wall hits
    const rel = q.advance(20.0);
    expect(rel.length).toBe(1);
    expect(rel[0].payload).toBe("clang");
    expect(rel[0].arrivalTime).toBeCloseTo(20.0, 6);
    expect(rel[0].delay).toBeCloseTo(10.0, 6);
    // consumed exactly once — not re-released
    expect(q.advance(30)).toEqual([]);
    expect(q.inFlight).toBe(0);
  });

  it("releases in ARRIVAL order even when enqueued out of order", () => {
    const q = new RetardedEventQueue<number>();
    const listener: Vec3 = { x: 0, y: 0, z: 0 };
    // three events, different emit distances → different delays
    q.enqueue({ emitTime: 0, emitPos: { x: 10000, y: 0, z: 0 }, payload: 1 }, listener); // ~29.2s
    q.enqueue({ emitTime: 0, emitPos: { x: 343, y: 0, z: 0 }, payload: 2 }, listener); //   1.0s
    q.enqueue({ emitTime: 0, emitPos: { x: 3430, y: 0, z: 0 }, payload: 3 }, listener); //  10.0s
    const rel = q.advance(1000);
    expect(rel.map((r) => r.payload)).toEqual([2, 3, 1]); // nearest arrives first
  });

  it("timeToNext + inFlight report the pending sound wall", () => {
    const q = new RetardedEventQueue<string>();
    q.enqueue({ emitTime: 5, emitPos: origin, payload: "boom" }, { x: 6860, y: 0, z: 0 }); // 20 s delay
    q.advance(5); // listener clock = 5
    expect(q.inFlight).toBe(1);
    expect(q.timeToNext()).toBeCloseTo(20, 3); // arrives at t=25, clock=5
    q.clear();
    expect(q.inFlight).toBe(0);
    expect(q.timeToNext()).toBe(Infinity);
  });

  it("gain carried on release matches 1/r at the emission geometry", () => {
    const q = new RetardedEventQueue<string>();
    q.enqueue({ emitTime: 0, emitPos: origin, payload: "x" }, { x: 1000, y: 0, z: 0 });
    const [rel] = q.advance(100);
    expect(rel.gain).toBeCloseTo(spreadingGain(1000), 9);
    expect(rel.range).toBeCloseTo(1000, 6);
  });
});
