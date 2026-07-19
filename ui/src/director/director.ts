// director.ts — DIRECTOR v0 (brainstorm §5 S1 item 5, canon §B.6).
//
// Camera presets + AUTO-DIRECTOR that cuts on EVT beats. ALL camera state is
// RENDERER-SIDE and NEVER crosses the telemetry boundary (canon §A.0/§B.6). The
// director consumes EVT + phase; it produces a desired camera pose (eye + target
// in SIM-world coordinates) that main.ts converts via the floating origin.
//
// Presets (canon §B.6 rig grammar, S1 subset):
//   PAD_LONG_LENS — 2 km long-lens tracking-footage grammar (the reference target)
//   ONBOARD_DOWN  — mounted on the vehicle looking down the legs + plume
//   CHASE         — spring-arm trailing the vehicle
//   FREE_ORBIT    — slow auto-orbit for a photo-mode feel
//
// AUTO-DIRECTOR cut grammar (brainstorm §5 / §11 camera grammar):
//   entry burn / high altitude  -> PAD_LONG_LENS (wide)
//   ignition (ENGINE_START/IGNITION_CMD in landing regime) -> ONBOARD_DOWN
//   final approach (LandingBurn, low alt) -> PAD_LONG_LENS locked (pad cam)
//   touchdown/settling -> PAD_LONG_LENS held
// Cuts are debounced so a burst of EVTs doesn't strobe the camera.
//
// The pure cut-decision function `decideAutoCut` is unit-tested (director.test.ts):
// given the current preset, a triggering EVT, and phase/altitude context, it
// returns the preset the auto-director should be on. Smoothing/transition timing
// lives in the DirectorRig class (an eased pose lerp), tested for monotonic ease.

import { Vector3 } from "three/webgpu";
import { EvtCode, type EvtFrame } from "../net/events";
import { Phase } from "../net/decode";

export type CameraPreset = "PAD_LONG_LENS" | "ONBOARD_DOWN" | "CHASE" | "FREE_ORBIT";

export const PRESETS: CameraPreset[] = ["PAD_LONG_LENS", "ONBOARD_DOWN", "CHASE", "FREE_ORBIT"];

/** Context the auto-director uses to choose a shot on an EVT beat. */
export interface DirectorContext {
  phase: Phase;
  altitudeM: number; // vehicle altitude above pad [m]
}

/**
 * PURE cut decision (tested). Given the preset we're currently on, an incoming EVT
 * beat, and the phase/altitude context, return the preset the AUTO-DIRECTOR should
 * switch to. Returns the SAME preset when the beat is not a cut trigger (no cut).
 *
 * This encodes the §11 camera grammar: wide long-lens for the fall, cut to onboard
 * at the landing-burn ignition, and lock the pad long-lens for the final approach +
 * touchdown.
 */
export function decideAutoCut(
  current: CameraPreset,
  evt: EvtFrame,
  ctx: DirectorContext
): CameraPreset {
  switch (evt.code) {
    case EvtCode.PhaseChange: {
      // new phase in args[0]
      const newPhase = Math.round(evt.args[0]) as Phase;
      // FIRST-LIGHT FIX: a pad camera at entry burn stares at a 3.7 m booster 40-62 km
      // away — sub-pixel by geometry, "where is the rocket". Ride CHASE until the
      // vehicle is near enough to subtend real pixels; the pad long-lens still owns
      // the final approach + touchdown (where the reference-footage grammar pays).
      if (newPhase === Phase.EntryBurn) return "CHASE";
      if (newPhase === Phase.AeroDescent) return "CHASE"; // follow the fall
      if (newPhase === Phase.LandingBurn) return "ONBOARD_DOWN"; // the leg-and-plume view
      if (
        newPhase === Phase.Touchdown ||
        newPhase === Phase.Settling ||
        newPhase === Phase.Landed
      )
        return "PAD_LONG_LENS"; // lock the pad cam for the money shot
      return current;
    }
    case EvtCode.IgnitionCmd:
    case EvtCode.EngineStart: {
      // ignition in the landing regime -> onboard; entry-burn ignition -> stay close
      // (FIRST-LIGHT FIX: was PAD_LONG_LENS — sub-pixel at entry-burn range)
      if (ctx.phase === Phase.LandingBurn || ctx.altitudeM < 4000) return "ONBOARD_DOWN";
      return "CHASE";
    }
    case EvtCode.LegDeploy:
      // legs come out on final -> cut to the pad long lens for the landing
      return "PAD_LONG_LENS";
    case EvtCode.Touchdown:
      return "PAD_LONG_LENS";
    default:
      return current; // MACH1, RCS, GUST, etc. don't cut the camera in v0
  }
}

/** Ease-in-out cubic (tested monotonic 0..1). */
export function easeInOutCubic(t: number): number {
  const x = Math.min(1, Math.max(0, t));
  return x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2;
}

/** A camera pose in SIM-world coordinates (converted by the caller). */
export interface CamPose {
  eye: Vector3; // sim-world position of the camera
  target: Vector3; // sim-world look-at
  fov: number;
}

/**
 * Compute the ideal pose for a preset given the vehicle's sim-world state. Pure
 * geometry (no side effects) so it is trivially testable and deterministic.
 * `vehPos`/`vehVel` are SIM frame; `up` is sim +Z. `orbitT` drives FREE_ORBIT +
 * long-lens seeing shimmer phase.
 */
/** Optional user-camera state for FREE_ORBIT (drag/wheel ownership). */
export interface OrbitOpts {
  halfH?: number; // vehicle half-length [m] (frame the CENTER, not the base)
  az?: number; // user azimuth [rad]
  el?: number; // user elevation [rad]
  r?: number; // user radius [m]
}

export function presetPose(
  preset: CameraPreset,
  vehPos: Vector3,
  vehVel: Vector3,
  orbitT: number,
  out: CamPose,
  opts?: OrbitOpts
): CamPose {
  out.target.copy(vehPos);
  switch (preset) {
    case "PAD_LONG_LENS": {
      // A fixed long-lens position ~2 km from the pad, slightly elevated, framing
      // the vehicle. Tracking-footage grammar: narrow fov (long lens).
      const az = 0.6; // fixed operator azimuth (sim XY)
      const dist = 2000;
      out.eye.set(Math.cos(az) * dist, Math.sin(az) * dist, 30);
      out.fov = 12; // long lens
      // frame slightly ahead so the vehicle isn't dead-center
      out.target.set(vehPos.x * 0.15, vehPos.y * 0.15, vehPos.z * 0.6);
      break;
    }
    case "ONBOARD_DOWN": {
      // Mounted on the vehicle, looking straight down the axis at the pad/plume.
      out.eye.copy(vehPos);
      out.eye.z += 4; // just above the base
      out.target.set(vehPos.x, vehPos.y, 0); // look down toward the ground
      out.fov = 62; // wide fisheye-ish
      break;
    }
    case "CHASE": {
      // Spring-arm trailing behind + above the velocity vector.
      const back = vehVel.clone();
      const speed = back.length();
      if (speed > 1) back.multiplyScalar(-1 / speed);
      else back.set(-0.5, -0.5, 0.3).normalize();
      out.eye.copy(vehPos).addScaledVector(back, 120);
      out.eye.z += 60;
      out.fov = 40;
      break;
    }
    case "FREE_ORBIT": {
      // THE DEFAULT EXTERNAL VIEW (KSP-style, operator-owned): side-on, whole
      // vehicle framed at its CENTER (not its base — centering the base shoved the
      // rocket to the bottom of the frame), user-controlled azimuth/elevation/
      // radius via drag + wheel (the rig only FOLLOWS the vehicle; the user owns
      // the angle — a camera that fights the mouse is worse than no camera).
      const halfH = opts?.halfH ?? 23.5; // ~half a 47 m booster
      const r = opts?.r ?? 150;
      const az = (opts?.az ?? 0) + orbitT * 0.008; // near-static drift under the user's angle
      const el = opts?.el ?? 0.08; // slight upward tilt of the orbit plane
      const cz = vehPos.z + halfH; // vehicle CENTER height
      out.target.set(vehPos.x, vehPos.y, cz);
      out.eye.set(
        vehPos.x + Math.cos(az) * Math.cos(el) * r,
        vehPos.y + Math.sin(az) * Math.cos(el) * r,
        cz + Math.sin(el) * r
      );
      out.fov = 45;
      break;
    }
  }
  return out;
}

/**
 * The stateful rig: holds the current preset, whether AUTO is engaged, an eased
 * transition between the previous and target pose on a cut, and the seeded
 * long-lens shimmer. main.ts calls `onEvt` for each EVT beat and `update` each
 * frame, then reads `.eye/.target/.fov` (sim-world) to drive the camera through
 * the floating origin.
 */
export class DirectorRig {
  // OPERATOR DOCTRINE (first light, verbatim: "the whole point is to see the rocket
  // autoland, from third external perspective"): the DEFAULT is a KSP-style EXTERNAL
  // side view that frames the whole vehicle and NEVER cuts away — FREE_ORBIT at
  // framing distance with a near-static drift. The cinematic auto-director (pad
  // long-lens / onboard cuts) is OPT-IN via the AUTO toggle, never the default.
  preset: CameraPreset = "FREE_ORBIT";
  auto = false; // cinematic cuts are opt-in; the external view never abandons the rocket

  // current (output) pose, sim-world
  readonly eye = new Vector3(1200, 800, 30);
  readonly target = new Vector3();
  fov = 12;

  // transition state
  private fromEye = new Vector3();
  private fromTarget = new Vector3();
  private fromFov = 12;
  private toPose: CamPose = { eye: new Vector3(), target: new Vector3(), fov: 12 };
  private transT = 1; // 0..1 (1 = settled)
  private readonly transDur = 1.1; // s
  private orbitT = 0;
  private lastCutT = -1e9; // sim time of last cut (debounce)
  private readonly cutDebounce = 0.6; // s

  private _scratch: CamPose = { eye: new Vector3(), target: new Vector3(), fov: 12 };

  /** Manually select a preset (UI hotkey). Disengages AUTO. */
  select(p: CameraPreset, vehPos: Vector3, vehVel: Vector3): void {
    this.auto = false;
    this.cutTo(p, vehPos, vehVel, 0);
  }

  /** Toggle AUTO-DIRECTOR. */
  setAuto(on: boolean): void {
    this.auto = on;
  }

  /** Feed an EVT beat; may trigger an auto cut (debounced). */
  onEvt(evt: EvtFrame, ctx: DirectorContext, vehPos: Vector3, vehVel: Vector3): void {
    if (!this.auto) return;
    const next = decideAutoCut(this.preset, evt, ctx);
    if (next !== this.preset && evt.t - this.lastCutT >= this.cutDebounce) {
      this.cutTo(next, vehPos, vehVel, evt.t);
    }
  }

  private cutTo(p: CameraPreset, vehPos: Vector3, vehVel: Vector3, simT: number): void {
    this.fromEye.copy(this.eye);
    this.fromTarget.copy(this.target);
    this.fromFov = this.fov;
    this.preset = p;
    presetPose(p, vehPos, vehVel, this.orbitT, this.toPose, this.orbitOpts());
    this.transT = 0;
    this.lastCutT = simT;
  }

  /** Per-frame: advance the transition + resolve the live preset pose. */
  update(vehPos: Vector3, vehVel: Vector3, dtSec: number): void {
    this.orbitT += dtSec;

    // the target pose is recomputed live (so a preset tracking the moving vehicle
    // keeps up); on a settled cam it just is the current preset pose.
    presetPose(this.preset, vehPos, vehVel, this.orbitT, this._scratch, this.orbitOpts());

    if (this.transT < 1) {
      this.transT = Math.min(1, this.transT + dtSec / this.transDur);
      const e = easeInOutCubic(this.transT);
      // lerp from the frozen previous pose to the live target pose
      this.eye.copy(this.fromEye).lerp(this._scratch.eye, e);
      this.target.copy(this.fromTarget).lerp(this._scratch.target, e);
      this.fov = this.fromFov + (this._scratch.fov - this.fromFov) * e;
    } else {
      // settled: track the pose EXACTLY. A follow camera's offset from the vehicle
      // is constant — lerping here chased a 300 m/s target with a ~0.25 s rubber
      // band, which read as vertical JERK at 150 m range (first-light finding).
      // The pose is already continuous because vehPos comes from the interp layer.
      this.eye.copy(this._scratch.eye);
      this.target.copy(this._scratch.target);
      this.fov = this._scratch.fov;
    }
  }

  // ---- user-owned external camera (KSP grammar: the user owns the angle) ----
  orbitAz = 0.6;
  orbitEl = 0.08;
  orbitR = 150;
  vehHalfH = 23.5;

  private orbitOpts(): OrbitOpts {
    return { halfH: this.vehHalfH, az: this.orbitAz, el: this.orbitEl, r: this.orbitR };
  }

  /** Mouse-drag orbit (pixels). Never fights the user: input applies immediately. */
  orbitInput(dxPx: number, dyPx: number): void {
    this.orbitAz -= dxPx * 0.005;
    this.orbitEl = Math.min(1.25, Math.max(-0.45, this.orbitEl + dyPx * 0.003));
  }

  /** Wheel zoom (exponential; clamped so the vehicle can never be lost). */
  zoomInput(wheelDeltaY: number): void {
    this.orbitR = Math.min(2500, Math.max(40, this.orbitR * Math.exp(wheelDeltaY * 0.0012)));
  }
}
