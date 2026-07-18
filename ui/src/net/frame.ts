// frame.ts — THE single sim->render coordinate/quaternion conversion (canon §10.7).
//
// This is the ONLY place the frame change happens (directive 8 / Risk #1). Every
// position and every quaternion from telemetry passes through here before it
// touches the scene graph. Do not hand-derive variants elsewhere.
//
// World (sim): right-handed, Z up, X east, Y north (canon §4.1).
// three.js:    right-handed, Y up.
//
// Basis map (canon §10.7):
//     (x, y, z)_three = (x, z, -y)_sim
// i.e. sim +Z (up) -> three +Y (up); sim +Y (north) -> three -Z (into screen).
//
// The quaternion transforms by the SAME basis rotation (conjugation by Rx(-90°)),
// which for a body->world quaternion reduces to the identical component
// permutation:
//     q_three = (qx, qz, -qy, qw)_sim
//
// Proof obligation: rotate-then-convert MUST equal convert-then-rotate to 1e-6
// against the frozen App-C vectors. See frame.test.ts.

import { Vector3, Quaternion } from "three";

/** In-place: write the three.js-frame position of a sim-frame (x,y,z) into `out`. */
export function simToThreePosition(
  sx: number,
  sy: number,
  sz: number,
  out: Vector3
): Vector3 {
  // (x, z, -y)
  return out.set(sx, sz, -sy);
}

/** In-place: write the three.js-frame quaternion of a sim xyzw quaternion. */
export function simToThreeQuaternion(
  qx: number,
  qy: number,
  qz: number,
  qw: number,
  out: Quaternion
): Quaternion {
  // (qx, qz, -qy, qw). three's Quaternion is (x,y,z,w) = scalar-last, matching
  // the sim's scalar-last convention (canon §4.1) — so w is untouched.
  return out.set(qx, qz, -qy, qw);
}

/** A sim direction vector (velocity, force, wind) uses the same map as position. */
export const simToThreeVector = simToThreePosition;

// --- raw numeric forms (no allocation), for hot paths / tests -----------------

export function simToThreePosArray(
  sx: number,
  sy: number,
  sz: number
): [number, number, number] {
  return [sx, sz, -sy];
}

export function simToThreeQuatArray(
  qx: number,
  qy: number,
  qz: number,
  qw: number
): [number, number, number, number] {
  return [qx, qz, -qy, qw];
}
