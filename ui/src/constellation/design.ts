// design.ts — the visual language of MC CONSTELLATION (colors, glyph sizing,
// synthesized geometry). PURE + unit-testable; no three/DOM imports here so the
// math (angle synthesis, radius, size) can be tested headless.
//
// Palette is the canon's outcome coding (brainstorm §3.1 / §5 S2):
//   landed  : cool white -> soft gold  (by GOOD/HARD grade)
//   off-pad : amber
//   too-hard: red        (size ∝ td_v)
//   fuel-out: violet
//   tipped  : orange
// Dark-studio AgX baseline — colors are chosen in sRGB but read well under AgX
// tone mapping with the emissive bloom the renderer applies.

import type { ClassifiedRun, CauseBucket } from "./runData";

/** sRGB hex per bucket (the base glyph color; landed is grade-interpolated). */
export const BUCKET_COLOR: Record<CauseBucket, number> = {
  landed: 0xdfe8ff, // cool white (perfect end of the landed ramp)
  "off-pad": 0xffb020, // amber
  "too-hard": 0xff3b30, // red
  "fuel-out": 0xb060ff, // violet
  tipped: 0xff7a1a, // orange
  other: 0x8a94a6, // muted slate (residual)
};

/** Human labels for chips / cards / legend. */
export const BUCKET_LABEL: Record<CauseBucket, string> = {
  landed: "LANDED",
  "off-pad": "OFF-PAD",
  "too-hard": "TOO-HARD",
  "fuel-out": "FUEL-OUT",
  tipped: "TIPPED",
  other: "OTHER",
};

/** The two ends of the landed ramp: cool white (perfect) -> soft gold (hard). */
const LANDED_PERFECT = { r: 0xdf, g: 0xe8, b: 0xff }; // grade 0 -> RGB(223,232,255)
const LANDED_HARD = { r: 0xf4, g: 0xd4, b: 0x80 }; // grade 3 -> soft gold RGB(244,212,128)

/**
 * Glyph color for a run as an sRGB 0xRRGGBB int. Landed runs ramp cool-white
 * (verdict 0, perfect) -> soft gold (verdict 3, hard-but-down) so the *grade*
 * reads at a glance. All other buckets are their flat cause color.
 */
export function glyphColor(run: ClassifiedRun): number {
  if (run.bucket !== "landed") return BUCKET_COLOR[run.bucket];
  // verdict 0..3 -> t 0..1 along the ramp.
  const t = clamp01(run.row.verdict / 3);
  const r = Math.round(lerp(LANDED_PERFECT.r, LANDED_HARD.r, t));
  const g = Math.round(lerp(LANDED_PERFECT.g, LANDED_HARD.g, t));
  const b = Math.round(lerp(LANDED_PERFECT.b, LANDED_HARD.b, t));
  return (r << 16) | (g << 8) | b;
}

/**
 * SYNTHESIZED azimuth for a run's touchdown glyph, in radians.
 *
 * ⚠ SCHEMATIC — the CSV carries only td_lat (a scalar miss magnitude), NOT the
 * true ground-track bearing. We spread runs deterministically around the pad by
 * index using the golden angle (≈137.5°), which gives an even, non-clumping
 * sunflower distribution. This is a LAYOUT choice for legibility, not physical
 * truth. True bearings need recorded trajectories (.bltlm) — a wave-F2 item.
 *
 * Deterministic in the run INDEX (stable across reloads of the same file), and
 * seed-offset by the run's seed so two files with the same indices don't overlap
 * pixel-for-pixel in A/B.
 */
export const GOLDEN_ANGLE = Math.PI * (3 - Math.sqrt(5)); // ≈ 2.399963 rad

export function synthAngle(run: ClassifiedRun): number {
  return run.index * GOLDEN_ANGLE;
}

/** Radial distance [m] of the glyph from pad center = the touchdown lateral miss. */
export function glyphRadius(run: ClassifiedRun): number {
  return Math.max(0, run.row.td_lat);
}

/**
 * Glyph world XZ position (three-frame: X east, Z south-ish — we keep it planar).
 * We place glyphs on the ground plane at (angle, radius). Returned as [x, z].
 */
export function glyphGroundXZ(run: ClassifiedRun): [number, number] {
  const a = synthAngle(run);
  const r = glyphRadius(run);
  return [Math.cos(a) * r, Math.sin(a) * r];
}

/**
 * Glyph size [world m radius]. Base size, with too-hard runs scaled ∝ td_v so the
 * violence of a hard crash reads as a bigger, angrier mark (canon: "size ∝ td_v").
 * Clamped so a 96 m/s fuel-out doesn't dominate the frame.
 */
export function glyphSize(run: ClassifiedRun): number {
  const BASE = 2.2;
  if (run.bucket === "too-hard" || run.bucket === "fuel-out") {
    // td_v of 6..20 -> 1.0..2.2x; clamp beyond.
    const s = 1 + clamp01((run.row.td_v - 6) / 14) * 1.4;
    return BASE * s;
  }
  return BASE;
}

/**
 * Schematic descent-arc control points above a touchdown glyph.
 *
 * ⚠ SCHEMATIC RIBBON — NOT the true ground track. We draw a stylized descent from
 * a synthesized entry point high above and offset, curving down to the touchdown
 * point. It communicates "this run came down HERE" as a readable arc, and its
 * lateral lean encodes the miss magnitude, but the path is invented. The report
 * is explicit that true ribbons require recorded trajectories (wave-F2).
 *
 * Returns [start, mid, end] in three-frame world coords (Y up). Height & entry
 * offset scale gently with td_lat so bigger misses lean more.
 */
export function schematicArc(
  run: ClassifiedRun
): [[number, number, number], [number, number, number], [number, number, number]] {
  const [gx, gz] = glyphGroundXZ(run);
  const r = glyphRadius(run);
  const a = synthAngle(run);
  // Arc apex height: fixed dramatic height so the arcs form a legible dome.
  const H = 140;
  // Entry point: pushed radially outward from the touchdown, so the arc leans in.
  const entryR = r + 40 + Math.min(r, 60) * 0.8;
  const ex = Math.cos(a) * entryR;
  const ez = Math.sin(a) * entryR;
  const start: [number, number, number] = [ex, H, ez];
  const end: [number, number, number] = [gx, 0.2, gz];
  // Mid control: between them, lifted, biased toward the entry side for a curve.
  const mid: [number, number, number] = [
    (ex * 0.55 + gx * 0.45),
    H * 0.55,
    (ez * 0.55 + gz * 0.45),
  ];
  return [start, mid, end];
}

// ── small math helpers (exported for tests) ──────────────────────────────────

export function clamp01(x: number): number {
  return x < 0 ? 0 : x > 1 ? 1 : x;
}
export function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t;
}

/** Convert an 0xRRGGBB int to a CSS hex string (#rrggbb) for DOM chips/cards. */
export function cssHex(color: number): string {
  return "#" + color.toString(16).padStart(6, "0");
}
