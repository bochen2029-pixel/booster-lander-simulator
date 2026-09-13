import { describe, it, expect } from "vitest";
import type { TlmFrame } from "../../net/decode";
import { bellAltitude, makeKestrelTlm, tlmToKestrel } from "./kestrelTlm";

/** The subset of TlmFrame the adapter reads, cast for the test. */
function frame(over: Partial<TlmFrame>): TlmFrame {
  const base: Partial<TlmFrame> = {
    throttleAct: 0.41,
    nEng: 3,
    gimbalAct: [0.01, -0.02],
    finsAct: [0.1, 0.2, 0.3, 0.4],
    deployFrac: 0.75,
    stroke: [0, 0.05, 0.1, 0],
    Qheat: 1.1e8,
    pAmb: 54321,
    mach: 1.7,
    qbar: 12000,
  };
  return { ...base, ...over } as unknown as TlmFrame;
}

describe("tlmToKestrel — camelCase -> the packet's own snake_case names", () => {
  it("maps every field the renderer reads, by name", () => {
    const out = tlmToKestrel(frame({}), 12.5, makeKestrelTlm());
    expect(out.throttle_act).toBe(0.41);
    expect(out.n_eng).toBe(3);
    expect(out.gimbal_act).toEqual([0.01, -0.02]);
    expect(out.fins_act).toEqual([0.1, 0.2, 0.3, 0.4]);
    expect(out.deploy_frac).toBe(0.75);
    expect(out.stroke).toEqual([0, 0.05, 0.1, 0]);
    expect(out.Q_heat).toBe(1.1e8);
    expect(out.p_amb).toBe(54321);
    expect(out.mach).toBe(1.7);
    expect(out.qbar).toBe(12000);
    expect(out.bell_alt).toBe(12.5);
  });

  it("reuses the output object (no per-frame allocation) and overwrites stale values", () => {
    const out = makeKestrelTlm();
    const a = tlmToKestrel(frame({ nEng: 9, throttleAct: 1 }), 3, out);
    const b = tlmToKestrel(frame({ nEng: 1, throttleAct: 0.2 }), 4, out);
    expect(a).toBe(out);
    expect(b).toBe(out);
    expect(out.n_eng).toBe(1);
    expect(out.throttle_act).toBe(0.2);
    expect(out.bell_alt).toBe(4);
  });
});

describe("bellAltitude", () => {
  it("is base Z minus surface minus bell depth, clamped at zero", () => {
    expect(bellAltitude(100, 0, 1.62)).toBeCloseTo(98.38, 6);
    expect(bellAltitude(100, 30, 1.62)).toBeCloseTo(68.38, 6); // deck at 30 m (sea)
    expect(bellAltitude(1.0, 0, 1.62)).toBe(0); // resting: never negative
  });
});
