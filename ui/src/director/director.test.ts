// director.test.ts — the AUTO-DIRECTOR cut grammar (canon §B.6) + easing +
// preset-pose geometry. All pure; the smooth transitions need eyes-on but the
// DECISIONS are deterministic and tested here.

import { describe, it, expect } from "vitest";
import {
  decideAutoCut,
  easeInOutCubic,
  presetPose,
  type CameraPreset,
} from "./director";
import { EvtCode, type EvtFrame } from "../net/events";
import { Phase } from "../net/decode";
import { Vector3 } from "three/webgpu";

function evt(code: EvtCode, args: number[] = []): EvtFrame {
  const a = [0, 0, 0, 0, 0, 0];
  args.forEach((v, i) => (a[i] = v));
  return { code, step: 0, t: 0, args: a as EvtFrame["args"] };
}

describe("decideAutoCut — the §11 camera grammar", () => {
  const ctxHigh = { phase: Phase.Coast, altitudeM: 40000 };
  const ctxLow = { phase: Phase.LandingBurn, altitudeM: 800 };

  it("rides CHASE through the entry burn (a pad cam at 40-62 km sees sub-pixel — first-light fix)", () => {
    const e = evt(EvtCode.PhaseChange, [Phase.EntryBurn]);
    expect(decideAutoCut("PAD_LONG_LENS", e, ctxHigh)).toBe("CHASE");
  });

  it("cuts to CHASE when aero descent begins", () => {
    const e = evt(EvtCode.PhaseChange, [Phase.AeroDescent]);
    expect(decideAutoCut("PAD_LONG_LENS", e, ctxHigh)).toBe("CHASE");
  });

  it("cuts to ONBOARD at landing-burn ignition (low regime)", () => {
    expect(decideAutoCut("CHASE", evt(EvtCode.EngineStart), ctxLow)).toBe("ONBOARD_DOWN");
    expect(decideAutoCut("CHASE", evt(EvtCode.IgnitionCmd), ctxLow)).toBe("ONBOARD_DOWN");
  });

  it("stays CLOSE for an entry-burn ignition high up (not the landing burn; first-light fix)", () => {
    expect(decideAutoCut("PAD_LONG_LENS", evt(EvtCode.EngineStart), ctxHigh)).toBe(
      "CHASE"
    );
  });

  it("locks the pad long lens on leg deploy and touchdown", () => {
    expect(decideAutoCut("ONBOARD_DOWN", evt(EvtCode.LegDeploy), ctxLow)).toBe(
      "PAD_LONG_LENS"
    );
    expect(decideAutoCut("ONBOARD_DOWN", evt(EvtCode.Touchdown), ctxLow)).toBe(
      "PAD_LONG_LENS"
    );
  });

  it("locks the pad cam on the touchdown/landed phase changes", () => {
    for (const ph of [Phase.Touchdown, Phase.Settling, Phase.Landed]) {
      expect(decideAutoCut("ONBOARD_DOWN", evt(EvtCode.PhaseChange, [ph]), ctxLow)).toBe(
        "PAD_LONG_LENS"
      );
    }
  });

  it("does NOT cut on non-beat events (MACH1, RCS, GUST)", () => {
    const cur: CameraPreset = "CHASE";
    expect(decideAutoCut(cur, evt(EvtCode.Mach1Cross), ctxHigh)).toBe(cur);
    expect(decideAutoCut(cur, evt(EvtCode.RcsPulse), ctxLow)).toBe(cur);
    expect(decideAutoCut(cur, evt(EvtCode.Gust), ctxHigh)).toBe(cur);
    expect(decideAutoCut(cur, evt(EvtCode.SolverDegraded), ctxLow)).toBe(cur);
  });
});

describe("easeInOutCubic", () => {
  it("pins the endpoints", () => {
    expect(easeInOutCubic(0)).toBe(0);
    expect(easeInOutCubic(1)).toBe(1);
  });
  it("passes through 0.5 at the midpoint", () => {
    expect(easeInOutCubic(0.5)).toBeCloseTo(0.5, 6);
  });
  it("clamps outside [0,1]", () => {
    expect(easeInOutCubic(-1)).toBe(0);
    expect(easeInOutCubic(2)).toBe(1);
  });
  it("is monotonically increasing", () => {
    let prev = -Infinity;
    for (let i = 0; i <= 20; i++) {
      const y = easeInOutCubic(i / 20);
      expect(y).toBeGreaterThanOrEqual(prev - 1e-9);
      prev = y;
    }
  });
});

describe("presetPose geometry", () => {
  const vehPos = new Vector3(100, -50, 3000); // sim frame
  const vehVel = new Vector3(-10, 5, -200);
  const out = { eye: new Vector3(), target: new Vector3(), fov: 0 };

  it("PAD_LONG_LENS uses a long lens (narrow fov) far from the pad", () => {
    presetPose("PAD_LONG_LENS", vehPos, vehVel, 0, out);
    expect(out.fov).toBeLessThan(20);
    // eye is ~2 km out horizontally
    expect(Math.hypot(out.eye.x, out.eye.y)).toBeGreaterThan(1500);
  });

  it("ONBOARD_DOWN sits on the vehicle and looks down (target below eye in Z)", () => {
    presetPose("ONBOARD_DOWN", vehPos, vehVel, 0, out);
    expect(out.eye.z).toBeGreaterThan(out.target.z); // looking down toward ground
    expect(out.fov).toBeGreaterThan(45); // wide
  });

  it("CHASE trails behind the velocity vector", () => {
    presetPose("CHASE", vehPos, vehVel, 0, out);
    // eye should be opposite the velocity direction from the vehicle
    const toEye = out.eye.clone().sub(vehPos);
    expect(toEye.dot(vehVel)).toBeLessThan(0); // behind
  });

  it("FREE_ORBIT keeps a fixed radius around the vehicle", () => {
    presetPose("FREE_ORBIT", vehPos, vehVel, 0, out);
    const d = out.eye.clone().sub(vehPos).length();
    expect(d).toBeGreaterThan(100); // orbit radius + elevation
  });
});
