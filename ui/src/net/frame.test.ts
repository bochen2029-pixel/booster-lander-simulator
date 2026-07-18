// frame.test.ts — freezes the sim->three conversion against canon Appendix C.
// This is the M3 gate ("conversion vectors pass in ui (vitest)") and the
// front-line defense for Risk #1 (quaternion conversion bug).
//
// Appendix C (verbatim):
//   v_sim (1,0,0) -> v_three (1,0,0)
//   v_sim (0,1,0) -> (0,0,-1)
//   v_sim (0,0,1) -> (0,1,0)
//   q_sim = (0,0,0.7071068,0.7071068)  [90° about sim-Z]
//        -> q_three = (0,0.7071068,0,0.7071068)
//   q_sim = (0.7071068,0,0,0.7071068)  [90° about sim-X]
//        -> q_three = (0.7071068,0,0,0.7071068)
//   Commutation: convert q, rotate converted (1,0,0) must equal
//                convert( rotate_sim (1,0,0) ) to 1e-6.

import { describe, it, expect } from "vitest";
import { Vector3, Quaternion } from "three";
import {
  simToThreePosition,
  simToThreeQuaternion,
} from "./frame";

const S = Math.SQRT1_2; // 0.70710678...
const EPS = 1e-6;

function expectVecClose(v: Vector3, x: number, y: number, z: number) {
  expect(v.x).toBeCloseTo(x, 6);
  expect(v.y).toBeCloseTo(y, 6);
  expect(v.z).toBeCloseTo(z, 6);
}

describe("basis vectors (App-C)", () => {
  const out = new Vector3();
  it("sim +X (east) -> three +X", () => {
    expectVecClose(simToThreePosition(1, 0, 0, out), 1, 0, 0);
  });
  it("sim +Y (north) -> three -Z", () => {
    expectVecClose(simToThreePosition(0, 1, 0, out), 0, 0, -1);
  });
  it("sim +Z (up) -> three +Y", () => {
    expectVecClose(simToThreePosition(0, 0, 1, out), 0, 1, 0);
  });
});

describe("quaternion permutation (App-C)", () => {
  const out = new Quaternion();
  it("90° about sim-Z -> (0, S, 0, S)", () => {
    simToThreeQuaternion(0, 0, S, S, out);
    expect(out.x).toBeCloseTo(0, 6);
    expect(out.y).toBeCloseTo(S, 6);
    expect(out.z).toBeCloseTo(0, 6);
    expect(out.w).toBeCloseTo(S, 6);
  });
  it("90° about sim-X -> (S, 0, 0, S)", () => {
    simToThreeQuaternion(S, 0, 0, S, out);
    expect(out.x).toBeCloseTo(S, 6);
    expect(out.y).toBeCloseTo(0, 6);
    expect(out.z).toBeCloseTo(0, 6);
    expect(out.w).toBeCloseTo(S, 6);
  });
});

describe("commutation: rotate-then-convert == convert-then-rotate", () => {
  // For every test quaternion q_sim and probe vector p_sim:
  //   LHS = simToThreePos( q_sim * p_sim )      (rotate in sim, then convert)
  //   RHS = q_three * simToThreePos( p_sim )    (convert both, then rotate)
  // must agree to 1e-6. If they don't, the quaternion permutation is wrong.
  const probes: [number, number, number][] = [
    [1, 0, 0],
    [0, 1, 0],
    [0, 0, 1],
    [0.3, -0.7, 0.64807407], // arbitrary unit-ish
  ];
  const qsims: [number, number, number, number][] = [
    [0, 0, S, S], // 90° Z
    [S, 0, 0, S], // 90° X
    [0, S, 0, S], // 90° Y
    // arbitrary normalized quaternion:
    (() => {
      const q = new Quaternion(0.2, -0.5, 0.3, 0.7).normalize();
      return [q.x, q.y, q.z, q.w] as [number, number, number, number];
    })(),
  ];

  for (const qs of qsims) {
    for (const p of probes) {
      it(`q=${qs.map((n) => n.toFixed(3))} p=${p}`, () => {
        const qSim = new Quaternion(qs[0], qs[1], qs[2], qs[3]);

        // LHS: rotate in sim frame, then convert the resulting vector.
        const rotatedSim = new Vector3(p[0], p[1], p[2]).applyQuaternion(qSim);
        const lhs = simToThreePosition(
          rotatedSim.x,
          rotatedSim.y,
          rotatedSim.z,
          new Vector3()
        );

        // RHS: convert q and p independently, then rotate in three frame.
        const qThree = simToThreeQuaternion(qs[0], qs[1], qs[2], qs[3], new Quaternion());
        const pThree = simToThreePosition(p[0], p[1], p[2], new Vector3());
        const rhs = pThree.clone().applyQuaternion(qThree);

        expect(lhs.distanceTo(rhs)).toBeLessThan(EPS);
      });
    }
  }

  it("App-C worked example: (1,0,0) under 90°-Z -> three (0,0,-1)", () => {
    // sim: rotate (1,0,0) by 90° about Z -> (0,1,0); convert -> (0,0,-1).
    const qSim = new Quaternion(0, 0, S, S);
    const rotatedSim = new Vector3(1, 0, 0).applyQuaternion(qSim);
    expectVecClose(rotatedSim, 0, 1, 0); // sanity: sim rotation is correct
    const converted = simToThreePosition(rotatedSim.x, rotatedSim.y, rotatedSim.z, new Vector3());
    expectVecClose(converted, 0, 0, -1);

    // convert-then-rotate must match
    const qThree = simToThreeQuaternion(0, 0, S, S, new Quaternion());
    const pThree = simToThreePosition(1, 0, 0, new Vector3());
    const viaThree = pThree.applyQuaternion(qThree);
    expect(viaThree.distanceTo(converted)).toBeLessThan(EPS);
  });
});
