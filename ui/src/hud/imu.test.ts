// imu.test.ts — pins the display-only IMU kernel: the mount permutation, the gimbal-angle
// extraction, the lock margin, the align/landing-site references and the rate mapping.
import { describe, it, expect } from "vitest";
import { Quaternion, Vector3 } from "three";
import {
  Q_CASE_IN_BODY,
  Q_SM_IN_WORLD,
  alignRefsmmat,
  computeImu,
  gimbalAnglesFromElements,
  landingSiteRefsmmat,
  makeImuReadout,
} from "./imu";
import { Matrix4 } from "three";

const D2R = Math.PI / 180;
const axis = (x: number, y: number, z: number, deg: number) =>
  new Quaternion().setFromAxisAngle(new Vector3(x, y, z).normalize(), deg * D2R);
const NO_RATE = new Vector3();
const PAD = landingSiteRefsmmat();

function imu(q: Quaternion, w = NO_RATE, ref = PAD) {
  return computeImu(q, w, ref, makeImuReadout());
}

describe("mount permutation (case X = body Z, case Y = body X, case Z = body Y)", () => {
  it("maps the case axes onto the body axes as a proper rotation", () => {
    const cx = new Vector3(1, 0, 0).applyQuaternion(Q_CASE_IN_BODY);
    const cy = new Vector3(0, 1, 0).applyQuaternion(Q_CASE_IN_BODY);
    const cz = new Vector3(0, 0, 1).applyQuaternion(Q_CASE_IN_BODY);
    expect(cx.toArray().map((v) => +v.toFixed(9))).toEqual([0, 0, 1]);
    expect(cy.toArray().map((v) => +v.toFixed(9))).toEqual([1, 0, 0]);
    expect(cz.toArray().map((v) => +v.toFixed(9))).toEqual([0, 1, 0]);
    expect(Q_CASE_IN_BODY.length()).toBeCloseTo(1, 12);
    expect(Q_SM_IN_WORLD.equals(Q_CASE_IN_BODY)).toBe(true); // same permutation, world side
  });
});

describe("gimbal angles on the landing-site REFSMMAT", () => {
  it("upright, heading 0 reads 0/0/0 with 90° margin", () => {
    const r = imu(new Quaternion());
    expect(r.valid).toBe(true);
    expect(r.oga).toBeCloseTo(0, 6);
    expect(r.mga).toBeCloseTo(0, 6);
    expect(r.iga).toBeCloseTo(0, 6);
    expect(r.marginDeg).toBeCloseTo(90, 6);
    expect(r.lock).toBe("OK");
    expect(r.devDeg).toBeCloseTo(0, 6);
    expect(r.tiltDeg).toBeCloseTo(0, 6);
  });

  it("roll about the long axis (body +Z) is OGA, positive for a positive rotation", () => {
    const r = imu(axis(0, 0, 1, 30));
    expect(r.oga).toBeCloseTo(30, 6);
    expect(r.mga).toBeCloseTo(0, 6);
    expect(r.iga).toBeCloseTo(0, 6);
    expect(r.tiltDeg).toBeCloseTo(0, 6); // still vertical
    expect(r.devDeg).toBeCloseTo(30, 6);
  });

  it("tilt about the east axis (world X) is IGA — pitch, no lock exposure", () => {
    const r = imu(axis(1, 0, 0, 30));
    expect(r.iga).toBeCloseTo(30, 6);
    expect(r.mga).toBeCloseTo(0, 6);
    expect(r.oga).toBeCloseTo(0, 6);
    expect(r.marginDeg).toBeCloseTo(90, 6);
    expect(r.tiltDeg).toBeCloseTo(30, 6);
  });

  it("tilt about the north axis (world Y) is MGA — the lock axis; margin shrinks with it", () => {
    const r = imu(axis(0, 1, 0, 30));
    expect(r.mga).toBeCloseTo(30, 6);
    expect(r.iga).toBeCloseTo(0, 6);
    expect(r.oga).toBeCloseTo(0, 6);
    expect(r.marginDeg).toBeCloseTo(60, 6);
    expect(r.lock).toBe("OK");
    expect(r.tiltDeg).toBeCloseTo(30, 6);
  });

  it("annunciates WARN inside 20° of lock and LOCK inside 5°", () => {
    expect(imu(axis(0, 1, 0, 75)).lock).toBe("WARN");
    expect(imu(axis(0, 1, 0, 75)).marginDeg).toBeCloseTo(15, 6);
    expect(imu(axis(0, 1, 0, -88)).lock).toBe("LOCK");
    expect(imu(axis(0, 1, 0, -88)).marginDeg).toBeCloseTo(2, 6);
    expect(imu(axis(0, 1, 0, 90)).marginDeg).toBeCloseTo(0, 5);
  });

  it("recovers arbitrary (OGA, MGA, IGA) triples away from lock (round trip through the factorization)", () => {
    const cases: [number, number, number][] = [
      [10, 20, 30], [-40, 55, -15], [170, -60, 5], [-95, 5, 120], [33, -80, -170],
    ];
    for (const [o, m, i] of cases) {
      // case→SM = Rx(o)·Rz(m)·Ry(i); case→world = REFSMMAT·(case→SM); body→world = (case→world)·(case←body)⁻¹
      const rel = axis(1, 0, 0, o).multiply(axis(0, 0, 1, m)).multiply(axis(0, 1, 0, i));
      const qCase = PAD.clone().multiply(rel);
      const qBody = qCase.multiply(Q_CASE_IN_BODY.clone().invert());
      const r = imu(qBody);
      expect(r.oga).toBeCloseTo(o, 5);
      expect(r.mga).toBeCloseTo(m, 5);
      expect(r.iga).toBeCloseTo(i, 5);
      expect(r.marginDeg).toBeCloseTo(90 - Math.abs(m), 5);
    }
  });

  it("extracts angles from a raw column-major matrix (the formula itself)", () => {
    const rel = axis(1, 0, 0, 12).multiply(axis(0, 0, 1, -34)).multiply(axis(0, 1, 0, 56));
    const out = { oga: 0, mga: 0, iga: 0 };
    gimbalAnglesFromElements(new Matrix4().makeRotationFromQuaternion(rel).elements, out);
    expect(out.oga / D2R).toBeCloseTo(12, 6);
    expect(out.mga / D2R).toBeCloseTo(-34, 6);
    expect(out.iga / D2R).toBeCloseTo(56, 6);
  });
});

describe("ALIGN", () => {
  it("re-references the SM so the current attitude reads 0/0/0, and later motion reads relative to it", () => {
    const q0 = axis(0, 1, 0, 40).multiply(axis(0, 0, 1, 25));
    const ref = alignRefsmmat(q0);
    const r0 = computeImu(q0, NO_RATE, ref, makeImuReadout());
    expect(r0.oga).toBeCloseTo(0, 6);
    expect(r0.mga).toBeCloseTo(0, 6);
    expect(r0.iga).toBeCloseTo(0, 6);
    expect(r0.devDeg).toBeCloseTo(0, 6);
    // a further +10° roll about the (now tilted) long axis: OGA alone moves
    const bodyZ = new Vector3(0, 0, 1).applyQuaternion(q0);
    const q1 = new Quaternion().setFromAxisAngle(bodyZ, 10 * D2R).multiply(q0);
    const r1 = computeImu(q1, NO_RATE, ref, makeImuReadout());
    expect(r1.oga).toBeCloseTo(10, 5);
    expect(r1.mga).toBeCloseTo(0, 5);
    expect(r1.iga).toBeCloseTo(0, 5);
  });
});

describe("rates + validity", () => {
  it("maps body rates onto the case axes: roll = ω_z, pitch = ω_x, yaw = ω_y (deg/s)", () => {
    const r = imu(new Quaternion(), new Vector3(0.1, 0.2, 0.3));
    expect(r.rateRoll).toBeCloseTo(0.3 / D2R, 6);
    expect(r.ratePitch).toBeCloseTo(0.1 / D2R, 6);
    expect(r.rateYaw).toBeCloseTo(0.2 / D2R, 6);
  });

  it("flags NO ATT on a non-unit or non-finite quaternion", () => {
    expect(imu(new Quaternion(0, 0, 0, 0)).valid).toBe(false);
    expect(imu(new Quaternion(NaN, 0, 0, 1)).valid).toBe(false);
    expect(imu(new Quaternion(0, 0, 0, 1)).valid).toBe(true);
  });
});
