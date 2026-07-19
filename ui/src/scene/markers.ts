// markers.ts — the DIEGETIC guidance markers (canon §B.7, brainstorm §5 S1 item 2).
// "The interface lives in the world before it lives in glass."
//
// Two markers, both DERIVED FROM TELEMETRY (never animation-keyed):
//
//  1. pred_impact ground marker (TLM offsets 220/224) — "the solve lens." A ring
//     on the ground plane at the predicted impact XY, with a FADING COMET-TRAIL of
//     recent predictions. From 62 km it starts kilometers off the pad and *slides
//     onto the pad as the guidance tightens* (D-010 #1 / D-013). The trail makes
//     the convergence temporal + legible.
//
//  2. ignite_h altitude ring (TLM offset 228) — "the burn lights here." A faint
//     horizontal ring at the landing-burn ignition altitude that the vehicle falls
//     toward; it breathes as the aero-aware margin recomputes.
//
// INTERPOLATE-NEVER-SNAP (the TERMINAL kernel-state doctrine, canon §11.2): the
// marker positions come from the interpolated frame's fields, and the comet-trail
// is itself a smoothing/decay in the visual layer — we never hard-jump a marker to
// a raw packet value. The pure math here (trail decay, ring breathe, pad-distance
// convergence) is unit-tested in markers.test.ts.

import {
  Mesh,
  RingGeometry,
  CircleGeometry,
  MeshBasicMaterial,
  Group,
  Vector3,
  BufferGeometry,
  BufferAttribute,
  Line,
  LineBasicMaterial,
  AdditiveBlending,
  DoubleSide,
} from "three/webgpu";
import { simToThreePosition } from "../net/frame";

// --- pure math (tested) ------------------------------------------------------

/** Number of comet-trail samples retained (the fading tail of past predictions). */
export const TRAIL_LEN = 48;

/**
 * Trail-sample alpha as a function of age index (0 = freshest ... TRAIL_LEN-1 =
 * oldest). Exponential-ish fade so the head is bright and the tail dissolves.
 * Pure + deterministic for the test.
 */
export function trailAlpha(ageIndex: number, len = TRAIL_LEN): number {
  if (len <= 1) return 1;
  const u = ageIndex / (len - 1); // 0..1
  // fade curve: bright near head, quick falloff
  return Math.max(0, (1 - u) * (1 - u));
}

/**
 * The "solve convergence" scalar: how tightly the guidance has locked the impact
 * point onto the pad, in [0,1] (1 = dead on pad center). Drives marker color +
 * ring emphasis. Uses the horizontal miss distance vs a normalizing radius.
 */
export function solveConvergence(
  predXY: readonly [number, number],
  padCenterXY: readonly [number, number],
  normRadiusM = 2000
): number {
  const dx = predXY[0] - padCenterXY[0];
  const dy = predXY[1] - padCenterXY[1];
  const miss = Math.hypot(dx, dy);
  return Math.max(0, 1 - miss / Math.max(1e-3, normRadiusM));
}

/**
 * Ignite-ring "breathe" radius multiplier as the aero-aware margin recomputes.
 * The ring radius is a base geometry; this returns a gentle scale in [~0.9,~1.1]
 * driven by how close the vehicle altitude is to the ignition altitude (it
 * "tightens" as we approach). Pure for the test.
 */
export function igniteBreathe(vehAltM: number, igniteHM: number): number {
  if (igniteHM <= 0) return 1;
  const ratio = vehAltM / igniteHM; // >1 above the ring, ->1 approaching, <1 below
  // breathe amplitude shrinks as we approach (ratio->1): far away it pulses more.
  const proximity = Math.min(1, Math.abs(ratio - 1)); // 0 at the ring
  return 1 + 0.12 * proximity;
}

/** Miss distance [m] pred_impact -> pad center (for the MIND-rail sparkline). */
export function padMissDistance(
  predXY: readonly [number, number],
  padCenterXY: readonly [number, number] = [0, 0]
): number {
  return Math.hypot(predXY[0] - padCenterXY[0], predXY[1] - padCenterXY[1]);
}

// --- color ramp: off-target amber -> on-pad cool-white -----------------------
function convergenceColor(conv: number): [number, number, number] {
  // amber (0.95,0.55,0.15) -> white-cyan (0.75,0.95,1.0)
  const a: [number, number, number] = [0.95, 0.55, 0.15];
  const b: [number, number, number] = [0.75, 0.95, 1.0];
  const t = Math.min(1, Math.max(0, conv));
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}

// --- the visual markers (three objects) --------------------------------------

export interface MarkersHandle {
  root: Group; // add to the rebased world group
  /**
   * Per-frame update from the interpolated frame. Positions are SIM-frame values
   * (world XY [m] for pred_impact; altitude for the ring is the vehicle's sim Z).
   * `padCenter` is sim-frame pad XY (default origin). All conversion via frame.ts.
   */
  update(input: {
    predImpact: readonly [number, number]; // sim world XY [m]
    igniteH: number; // altitude [m]
    vehSimPos: readonly [number, number, number]; // sim r[3] for the ring center + alt
    padCenter?: readonly [number, number];
    dtSec: number;
  }): void;
  /** Latest solve-convergence scalar (for the MIND rail / HUD). */
  convergence: number;
  missDistanceM: number;
}

export function buildMarkers(padGroundZ = 0): MarkersHandle {
  const root = new Group();

  // --- pred_impact ground marker: an annulus + inner dot on the ground plane --
  const impactGroup = new Group();
  const impactRingGeo = new RingGeometry(1.6, 2.4, 48);
  const impactRingMat = new MeshBasicMaterial({
    color: 0xffffff,
    transparent: true,
    opacity: 0.9,
    side: DoubleSide,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const impactRing = new Mesh(impactRingGeo, impactRingMat);
  impactRing.rotation.x = -Math.PI / 2; // flat on XZ ground
  const impactDotGeo = new CircleGeometry(0.6, 24);
  const impactDotMat = new MeshBasicMaterial({
    color: 0xffffff,
    transparent: true,
    opacity: 0.7,
    side: DoubleSide,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const impactDot = new Mesh(impactDotGeo, impactDotMat);
  impactDot.rotation.x = -Math.PI / 2;
  impactGroup.add(impactRing, impactDot);
  root.add(impactGroup);

  // --- comet trail: a Line of TRAIL_LEN points with per-vertex fading color ----
  const trailPositions = new Float32Array(TRAIL_LEN * 3);
  const trailColors = new Float32Array(TRAIL_LEN * 3);
  const trailGeo = new BufferGeometry();
  trailGeo.setAttribute("position", new BufferAttribute(trailPositions, 3));
  trailGeo.setAttribute("color", new BufferAttribute(trailColors, 3));
  const trailMat = new LineBasicMaterial({
    vertexColors: true,
    transparent: true,
    opacity: 0.85,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const trail = new Line(trailGeo, trailMat);
  root.add(trail);
  // ring buffer of recent prediction points (three-space), newest at index head
  const trailBuf: Vector3[] = Array.from({ length: TRAIL_LEN }, () => new Vector3());
  let trailFilled = 0;
  let sincePush = 0;

  // --- ignite_h altitude ring: a horizontal ring at the ignition altitude ------
  const igniteGeo = new RingGeometry(6, 7, 64);
  const igniteMat = new MeshBasicMaterial({
    color: 0x66ccff,
    transparent: true,
    opacity: 0.35,
    side: DoubleSide,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const igniteRing = new Mesh(igniteGeo, igniteMat);
  igniteRing.rotation.x = -Math.PI / 2;
  root.add(igniteRing);

  const _p = new Vector3();
  const handle: MarkersHandle = {
    root,
    convergence: 0,
    missDistanceM: 0,
    update({ predImpact, igniteH, vehSimPos, padCenter = [0, 0], dtSec }) {
      // --- pred_impact ground marker (sim XY, ground Z=padGroundZ) -------------
      simToThreePosition(predImpact[0], predImpact[1], padGroundZ, _p);
      impactGroup.position.copy(_p);

      const conv = solveConvergence(predImpact, padCenter);
      handle.convergence = conv;
      handle.missDistanceM = padMissDistance(predImpact, padCenter);
      const [cr, cg, cb] = convergenceColor(conv);
      impactRingMat.color.setRGB(cr, cg, cb);
      impactDotMat.color.setRGB(cr, cg, cb);
      // ring tightens (smaller, brighter) as it converges
      const s = 1.6 - 0.6 * conv;
      impactGroup.scale.setScalar(s);

      // --- comet trail: push the new impact point at a throttled cadence -------
      sincePush += dtSec;
      if (sincePush >= 0.05) {
        // shift ring buffer (index 0 = freshest)
        for (let i = TRAIL_LEN - 1; i > 0; i--) trailBuf[i].copy(trailBuf[i - 1]);
        trailBuf[0].copy(_p);
        trailFilled = Math.min(TRAIL_LEN, trailFilled + 1);
        sincePush = 0;
      }
      for (let i = 0; i < TRAIL_LEN; i++) {
        const src = i < trailFilled ? trailBuf[i] : trailBuf[Math.max(0, trailFilled - 1)];
        trailPositions[i * 3] = src.x;
        trailPositions[i * 3 + 1] = src.y + 0.1; // lift slightly off ground
        trailPositions[i * 3 + 2] = src.z;
        const a = trailAlpha(i);
        trailColors[i * 3] = cr * a;
        trailColors[i * 3 + 1] = cg * a;
        trailColors[i * 3 + 2] = cb * a;
      }
      (trailGeo.getAttribute("position") as BufferAttribute).needsUpdate = true;
      (trailGeo.getAttribute("color") as BufferAttribute).needsUpdate = true;

      // --- ignite_h altitude ring: centered under the vehicle XY, at alt=igniteH
      simToThreePosition(vehSimPos[0], vehSimPos[1], igniteH, _p);
      igniteRing.position.copy(_p);
      const breathe = igniteBreathe(vehSimPos[2], igniteH);
      igniteRing.scale.setScalar(breathe);
      // fade the ring in only when the vehicle is reasonably near it (within ~2x)
      const near = igniteH > 1 ? Math.min(1, (2 * igniteH) / Math.max(igniteH, vehSimPos[2])) : 0;
      igniteMat.opacity = 0.12 + 0.3 * near;
    },
  };
  return handle;
}
