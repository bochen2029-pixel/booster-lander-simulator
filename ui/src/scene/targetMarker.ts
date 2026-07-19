// targetMarker.ts — the TARGET-ESTIMATE diegetic marker (canon v2 §8.4/§10.9).
//
// Draws WHAT THE ROCKET BELIEVES about its landing target, distinct from the
// truth pad/deck: a diamond reticle at the estimated target position, a 2σ
// covariance ellipse (the uncertainty the guidance is hedging against), and a
// short velocity leader when the estimate is moving. Directive-8 honest: every
// element derives from streamed TLM fields; nothing is animation-keyed.
//
// DORMANT UNTIL PROTOCOL v4 (D-019 §10.9): the estimate fields ship with the N0
// core + decoder. This component reads them OPTIONALLY via `readTargetEst` — on
// a v3 stream it returns null and the marker stays hidden, so this wiring is
// byte-safe today and lights up the moment the v4 decoder lands.
//
// THE FRAME-FIELD CONTRACT (the v4 decoder must surface exactly these names on
// the TLM frame object — integration checklist for the N0 fold-in):
//   targetEstXY:  [number, number]        world XY of the estimated target [m]
//   targetEstVXY: [number, number]        estimated target velocity [m/s]
//   targetCov:    [number, number, number] packed 2x2 covariance (xx, yy, xy) [m²]
//   targetSrc:    number                  0 FIXED | 1 SEEDED | 2 BEACON | 3 PERCEIVED | 4 DRAG
//   targetAge:    number                  seconds since last acquisition/update
//   targetValid:  number                  0 before first acquisition
//
// Pure math (covariance ellipse eigen-decomposition, staleness fade, the safe
// frame reader) is exported for targetMarker.test.ts.

import {
  Group,
  Line,
  LineBasicMaterial,
  BufferGeometry,
  BufferAttribute,
  AdditiveBlending,
  Vector3,
} from "three/webgpu";
import { simToThreePosition } from "../net/frame";

// --- pure math (tested) ------------------------------------------------------

/** Target-estimate source tags (mirrors canon §8.1 TargetEstimate.target_src). */
export enum TargetSrc {
  Fixed = 0,
  Seeded = 1,
  Beacon = 2,
  Perceived = 3,
  Drag = 4,
}

/** Per-source marker color — provenance visible at a glance (directive 8). */
export const SRC_COLORS: Record<TargetSrc, [number, number, number]> = {
  [TargetSrc.Fixed]: [0.75, 0.8, 0.86], // neutral: the classic fixed pad
  [TargetSrc.Seeded]: [0.35, 0.85, 1.0], // cyan: deterministic seeded motion
  [TargetSrc.Beacon]: [0.4, 0.95, 0.55], // green: the honest radio baseline
  [TargetSrc.Perceived]: [0.85, 0.55, 1.0], // violet: the VLM/vision estimate
  [TargetSrc.Drag]: [0.95, 0.7, 0.25], // amber: fenced live operator input
};

export interface CovEllipse {
  /** semi-major axis [m] (k·sqrt(λ+)) */
  a: number;
  /** semi-minor axis [m] (k·sqrt(λ−)) */
  b: number;
  /** rotation of the major axis from +X, radians, in sim world XY */
  thetaRad: number;
}

/**
 * Eigen-decompose a packed 2x2 symmetric covariance [xx, yy, xy] into the kσ
 * uncertainty ellipse. Degenerate/negative inputs clamp to zero axes rather
 * than NaN (a hostile covariance must never break the render loop).
 */
export function covEllipse(
  cov: readonly [number, number, number],
  kSigma = 2
): CovEllipse {
  const [xx, yy, xy] = cov;
  const tr = xx + yy;
  const det = xx * yy - xy * xy;
  // eigenvalues of a 2x2 symmetric matrix
  const disc = Math.sqrt(Math.max(0, (tr * tr) / 4 - det));
  const l1 = Math.max(0, tr / 2 + disc);
  const l2 = Math.max(0, tr / 2 - disc);
  // eigenvector angle of the MAJOR axis: θ = ½·atan2(2·xy, xx−yy)
  const thetaRad = 0.5 * Math.atan2(2 * xy, xx - yy);
  return { a: kSigma * Math.sqrt(l1), b: kSigma * Math.sqrt(l2), thetaRad };
}

/**
 * Staleness fade: a fresh estimate (age ≤ 1 s) renders at full alpha; by ~10 s
 * without an update it settles to a dim floor (still visible — the guidance is
 * still flying on it — but visibly stale). Pure for the test.
 */
export function stalenessAlpha(ageSec: number): number {
  const FRESH_S = 1.0;
  const STALE_S = 10.0;
  const FLOOR = 0.25;
  if (!(ageSec > FRESH_S)) return 1.0;
  const u = Math.min(1, (ageSec - FRESH_S) / (STALE_S - FRESH_S));
  return 1.0 - (1.0 - FLOOR) * u;
}

/** The validated estimate input the marker consumes. */
export interface TargetEstInput {
  xy: readonly [number, number];
  vxy: readonly [number, number];
  cov: readonly [number, number, number];
  src: number;
  age: number;
  valid: boolean;
}

function isPair(v: unknown): v is readonly [number, number] {
  return (
    Array.isArray(v) && v.length >= 2 && Number.isFinite(v[0]) && Number.isFinite(v[1])
  );
}

/**
 * Safe, optional read of the v4 target-estimate fields off a decoded TLM frame.
 * Returns null on a v3 frame (fields absent) or on malformed values — the
 * marker hides, nothing throws. This is the ONLY coupling point to the decoder,
 * kept structural so this file compiles against the v3 decoder today.
 */
export function readTargetEst(frame: unknown): TargetEstInput | null {
  if (typeof frame !== "object" || frame === null) return null;
  const f = frame as Record<string, unknown>;
  const xy = f["targetEstXY"];
  const cov = f["targetCov"];
  if (!isPair(xy)) return null;
  if (
    !Array.isArray(cov) ||
    cov.length < 3 ||
    !cov.slice(0, 3).every((c) => Number.isFinite(c as number))
  ) {
    return null;
  }
  const vxyRaw = f["targetEstVXY"];
  const vxy: readonly [number, number] = isPair(vxyRaw) ? vxyRaw : [0, 0];
  const src = typeof f["targetSrc"] === "number" ? (f["targetSrc"] as number) : 0;
  const age = typeof f["targetAge"] === "number" && Number.isFinite(f["targetAge"] as number)
    ? (f["targetAge"] as number)
    : 0;
  const valid = f["targetValid"] === undefined ? true : !!f["targetValid"];
  return {
    xy: [xy[0], xy[1]],
    vxy: [vxy[0], vxy[1]],
    cov: [cov[0] as number, cov[1] as number, cov[2] as number],
    src,
    age,
    valid,
  };
}

// --- the visual marker -------------------------------------------------------

const ELLIPSE_SEGS = 64;

export interface TargetMarkerHandle {
  root: Group;
  /** Per-frame update; pass null (or valid=false) to hide. Positions sim-frame. */
  update(est: TargetEstInput | null, groundZ?: number): void;
}

export function buildTargetMarker(): TargetMarkerHandle {
  const root = new Group();
  root.visible = false;

  // diamond reticle (a rotated-square line loop, distinct from the round
  // pred_impact marker so believe-vs-predict never visually alias)
  const DIAMOND_R = 3.0;
  const dPts = new Float32Array(5 * 3);
  const dGeo = new BufferGeometry();
  for (let i = 0; i <= 4; i++) {
    const ang = (i / 4) * Math.PI * 2 + Math.PI / 4;
    dPts[i * 3] = Math.cos(ang) * DIAMOND_R;
    dPts[i * 3 + 1] = 0.12; // lift off the ground plane
    dPts[i * 3 + 2] = Math.sin(ang) * DIAMOND_R;
  }
  dGeo.setAttribute("position", new BufferAttribute(dPts, 3));
  const dMat = new LineBasicMaterial({
    transparent: true,
    opacity: 0.95,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const diamond = new Line(dGeo, dMat);
  root.add(diamond);

  // 2σ covariance ellipse (unit circle scaled/rotated per frame)
  const ePts = new Float32Array((ELLIPSE_SEGS + 1) * 3);
  const eGeo = new BufferGeometry();
  eGeo.setAttribute("position", new BufferAttribute(ePts, 3));
  const eMat = new LineBasicMaterial({
    transparent: true,
    opacity: 0.55,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const ellipse = new Line(eGeo, eMat);
  root.add(ellipse);

  // velocity leader (a short line in the estimate's motion direction)
  const lPts = new Float32Array(2 * 3);
  const lGeo = new BufferGeometry();
  lGeo.setAttribute("position", new BufferAttribute(lPts, 3));
  const lMat = new LineBasicMaterial({
    transparent: true,
    opacity: 0.7,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const leader = new Line(lGeo, lMat);
  root.add(leader);

  const _p = new Vector3();

  return {
    root,
    update(est: TargetEstInput | null, groundZ = 0) {
      if (!est || !est.valid) {
        root.visible = false;
        return;
      }
      root.visible = true;

      // position (sim XY -> three, on the ground plane)
      simToThreePosition(est.xy[0], est.xy[1], groundZ, _p);
      root.position.copy(_p);

      // provenance color + staleness fade
      const src = (est.src in SRC_COLORS ? est.src : TargetSrc.Fixed) as TargetSrc;
      const [r, g, b] = SRC_COLORS[src];
      const fade = stalenessAlpha(est.age);
      dMat.color.setRGB(r, g, b);
      eMat.color.setRGB(r, g, b);
      lMat.color.setRGB(r, g, b);
      dMat.opacity = 0.95 * fade;
      eMat.opacity = 0.55 * fade;
      lMat.opacity = 0.7 * fade;

      // covariance ellipse in the local (marker-centered) frame. Sim world XY
      // maps to three (x, -z) via the canonical conversion; building the ellipse
      // point-by-point through the same mapping keeps the one-conversion rule.
      const ell = covEllipse(est.cov);
      const minVis = 0.5; // floor so a tiny covariance still reads as a ring
      const a = Math.max(minVis, ell.a);
      const bAx = Math.max(minVis, ell.b);
      const ct = Math.cos(ell.thetaRad);
      const st = Math.sin(ell.thetaRad);
      for (let i = 0; i <= ELLIPSE_SEGS; i++) {
        const u = (i / ELLIPSE_SEGS) * Math.PI * 2;
        const ex = a * Math.cos(u);
        const ey = bAx * Math.sin(u);
        const simX = ex * ct - ey * st;
        const simY = ex * st + ey * ct;
        ePts[i * 3] = simX;
        ePts[i * 3 + 1] = 0.12;
        ePts[i * 3 + 2] = -simY; // sim +Y -> three -Z (frame.ts convention)
      }
      (eGeo.getAttribute("position") as BufferAttribute).needsUpdate = true;

      // velocity leader: 2 s of estimated motion, sim -> three mapping as above
      const LEAD_S = 2.0;
      lPts[0] = 0;
      lPts[1] = 0.12;
      lPts[2] = 0;
      lPts[3] = est.vxy[0] * LEAD_S;
      lPts[4] = 0.12;
      lPts[5] = -est.vxy[1] * LEAD_S;
      (lGeo.getAttribute("position") as BufferAttribute).needsUpdate = true;
      leader.visible = Math.hypot(est.vxy[0], est.vxy[1]) > 0.05;
    },
  };
}
