// imu.ts — the Apollo Block II IMU kernel, DISPLAY-ONLY (PLAN.md Phase 2.1): body attitude →
// gimbal angles OGA / MGA / IGA, the middle-gimbal margin to lock, and body rates on the case axes.
// Pure functions on the sim-frame quaternion; never touches vehicle truth. This is the ~60-line
// kernel the plan asked for — NOT a port of assets/apollo_imu/gimbal-scene.js (that stays in JS).
//
// GIMBAL ORDER (Apollo Block II, the same factorization gimbal-scene.js uses): the case attitude
// relative to the stable member is R = Rx(OGA)·Rz(MGA)·Ry(IGA) — outer = case X, middle = case Z,
// inner = case Y — with v_SM = R · v_case. Lock at |MGA| → 90°: the outer and inner axes align and
// the resolver gain sec(MGA) → ∞. Positive vehicle rotations read as positive angles.
//
// MOUNT (a display convention, stated once, like the LM's): the outer (roll) axis lies along the
// vehicle long axis. case X = body +Z, case Y = body +X, case Z = body +Y — a cyclic permutation,
// hence a proper rotation. The stable member's reference (REFSMMAT) defaults to the LANDING-SITE
// frame under the same permutation: SM X = world up, SM Y = east, SM Z = north. So an upright
// vehicle at zero heading reads 0/0/0 with 90° of margin; a tilt about the east axis is IGA
// (pitch), a tilt about the north axis is MGA (yaw — the lock axis), a rotation about the long axis
// is OGA (roll). ALIGN re-references the SM to the current attitude (all angles → 0 now).
//
// THE TRAP (PLAN.md §3): this is a platform-REFERENCE instrument. The lander's F_LOC is a control-
// AUTHORITY failure (core/sim.c:565, |ω| > 2 rad/s sustained). Margin → 0 here does not model LOC;
// it makes an attitude departure legible while it happens — which today it is not.
import { Matrix4, Quaternion, Vector3 } from "three";

const R2D = 180 / Math.PI;
export const LOCK_DEG = 5;
export const WARN_DEG = 20;

// case←body and SM←world are the same cyclic permutation (X←Z, Y←X, Z←Y). Matrix4.set is
// row-major; the COLUMNS are the case axes expressed in body coordinates.
function permutationQuat(): Quaternion {
  const m = new Matrix4().set(0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1);
  return new Quaternion().setFromRotationMatrix(m);
}
/** Rotation taking case-frame vectors to body-frame vectors (case X = body Z, ...). */
export const Q_CASE_IN_BODY: Quaternion = permutationQuat();
/** Rotation taking stable-member vectors to world vectors for the landing-site REFSMMAT. */
export const Q_SM_IN_WORLD: Quaternion = permutationQuat();

export type LockState = "OK" | "WARN" | "LOCK";

export interface ImuReadout {
  /** gimbal angles [deg] */
  oga: number;
  mga: number;
  iga: number;
  /** 90° − |MGA| — distance of the middle gimbal from either lock [deg] */
  marginDeg: number;
  lock: LockState;
  /** total rotation between the case and its reference [deg] */
  devDeg: number;
  /** angle between the vehicle long axis (body +Z) and world up [deg] — the plain "how far from vertical" */
  tiltDeg: number;
  /** body rates on the case axes [deg/s]: roll = about the long axis, pitch = body X, yaw = body Y */
  rateRoll: number;
  ratePitch: number;
  rateYaw: number;
  /** false ⇒ NO ATT (quaternion not unit / not finite) */
  valid: boolean;
}

export function makeImuReadout(): ImuReadout {
  return {
    oga: 0, mga: 0, iga: 0, marginDeg: 90, lock: "OK", devDeg: 0, tiltDeg: 0,
    rateRoll: 0, ratePitch: 0, rateYaw: 0, valid: false,
  };
}

const clamp1 = (x: number) => (x < -1 ? -1 : x > 1 ? 1 : x);

/**
 * Extract (OGA, MGA, IGA) [rad] from the column-major elements of R = Rx(o)·Rz(m)·Ry(i):
 * m12 = −sin m; m22 = cos o·cos m, m32 = sin o·cos m; m11 = cos m·cos i, m13 = cos m·sin i.
 */
export function gimbalAnglesFromElements(e: ArrayLike<number>, out: { oga: number; mga: number; iga: number }): void {
  out.mga = Math.asin(clamp1(-e[4]));
  out.oga = Math.atan2(e[6], e[5]);
  out.iga = Math.atan2(e[8], e[0]);
}

export function lockState(marginDeg: number): LockState {
  return marginDeg <= LOCK_DEG ? "LOCK" : marginDeg <= WARN_DEG ? "WARN" : "OK";
}

/** REFSMMAT = the landing-site frame (SM X up, Y east, Z north). */
export function landingSiteRefsmmat(out: Quaternion = new Quaternion()): Quaternion {
  return out.copy(Q_SM_IN_WORLD);
}
/** REFSMMAT := the case's current attitude, so every gimbal angle reads zero right now. */
export function alignRefsmmat(qBody: Quaternion, out: Quaternion = new Quaternion()): Quaternion {
  return out.copy(qBody).multiply(Q_CASE_IN_BODY);
}

const _qCase = new Quaternion();
const _qRel = new Quaternion();
const _m = new Matrix4();
const _ang = { oga: 0, mga: 0, iga: 0 };
const _up = new Vector3();

/**
 * @param qBody sim body→world attitude (xyzw, as streamed)
 * @param wBody body angular rate [rad/s] (body axes, as streamed)
 * @param qRef  REFSMMAT: stable-member→world
 */
export function computeImu(qBody: Quaternion, wBody: Vector3, qRef: Quaternion, out: ImuReadout): ImuReadout {
  const n = qBody.length();
  out.valid = Number.isFinite(n) && Math.abs(n - 1) < 1e-2;
  out.rateRoll = wBody.z * R2D;
  out.ratePitch = wBody.x * R2D;
  out.rateYaw = wBody.y * R2D;
  if (!out.valid) {
    out.oga = out.mga = out.iga = 0;
    out.marginDeg = 90;
    out.lock = "OK";
    out.devDeg = out.tiltDeg = 0;
    return out;
  }
  _qCase.copy(qBody).multiply(Q_CASE_IN_BODY); // case → world
  _qRel.copy(qRef).invert().multiply(_qCase); // case → SM  (= Rx(o)·Rz(m)·Ry(i))
  _m.makeRotationFromQuaternion(_qRel);
  gimbalAnglesFromElements(_m.elements, _ang);
  out.oga = _ang.oga * R2D;
  out.mga = _ang.mga * R2D;
  out.iga = _ang.iga * R2D;
  out.marginDeg = 90 - Math.abs(out.mga);
  out.lock = lockState(out.marginDeg);
  out.devDeg = 2 * Math.acos(Math.min(1, Math.abs(_qRel.w))) * R2D;
  _up.set(0, 0, 1).applyQuaternion(qBody);
  out.tiltDeg = Math.acos(clamp1(_up.z)) * R2D;
  return out;
}
