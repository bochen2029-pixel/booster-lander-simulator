// design.test.ts — the constellation's synthesized-geometry + color math.
// Pure; guards the deterministic layout and the (honestly schematic) glyph rules.

import { describe, it, expect } from "vitest";
import { classifyRuns, parseRunCsv, type ClassifiedRun } from "./runData";
import {
  glyphColor,
  synthAngle,
  glyphRadius,
  glyphGroundXZ,
  glyphSize,
  schematicArc,
  GOLDEN_ANGLE,
  clamp01,
  lerp,
  cssHex,
  BUCKET_COLOR,
} from "./design";

function oneRun(csvBody: string): ClassifiedRun {
  const csv =
    "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush\n" +
    csvBody;
  return classifyRuns(parseRunCsv(csv).rows)[0];
}

describe("synthAngle — deterministic golden-angle layout", () => {
  it("is index * GOLDEN_ANGLE (stable, reproducible)", () => {
    const runs = classifyRuns(
      parseRunCsv(
        "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush\n" +
          "42,e,1,3,0,4,5,0,0,1,0,0,1,0\n" +
          "42,e,2,3,0,4,5,0,0,1,0,0,1,0\n" +
          "42,e,3,3,0,4,5,0,0,1,0,0,1,0"
      ).rows
    );
    expect(runs[0].index).toBe(0);
    expect(synthAngle(runs[0])).toBeCloseTo(0, 10);
    expect(synthAngle(runs[1])).toBeCloseTo(GOLDEN_ANGLE, 10);
    expect(synthAngle(runs[2])).toBeCloseTo(2 * GOLDEN_ANGLE, 10);
  });

  it("GOLDEN_ANGLE is ~2.39996 rad (137.5°)", () => {
    expect(GOLDEN_ANGLE).toBeCloseTo(2.399963, 5);
  });
});

describe("glyphRadius / glyphGroundXZ — radius is the touchdown miss", () => {
  it("radius equals td_lat", () => {
    const r = oneRun("42,e,1,3,0,4,17.3,0,0,1,0,0,1,0");
    expect(glyphRadius(r)).toBeCloseTo(17.3, 6);
  });
  it("ground XZ lies on the circle of radius td_lat", () => {
    const r = oneRun("42,e,1,3,0,4,20,0,0,1,0,0,1,0");
    const [x, z] = glyphGroundXZ(r);
    expect(Math.hypot(x, z)).toBeCloseTo(20, 6);
  });
  it("negative/garbage td_lat clamps to radius 0 (never NaN placement)", () => {
    const r = oneRun("42,e,1,3,0,4,-5,0,0,1,0,0,1,0");
    expect(glyphRadius(r)).toBe(0);
  });
});

describe("glyphColor — landed grade ramp, flat cause colors", () => {
  it("verdict 0 landed is the cool-white end", () => {
    const r = oneRun("42,e,1,0,0,1,3,0,0,1,0,0,1,0");
    expect(glyphColor(r)).toBe(0xdfe8ff);
  });
  it("verdict 3 landed is the soft-gold end", () => {
    const r = oneRun("42,e,1,3,0,1,3,0,0,1,0,0,1,0");
    expect(glyphColor(r)).toBe(0xf4d480);
  });
  it("a mid grade is strictly between the endpoints (monotone ramp)", () => {
    const r1 = oneRun("42,e,1,1,0,1,3,0,0,1,0,0,1,0");
    const c = glyphColor(r1);
    // green channel should be between 0xe8 and 0xd4
    const g = (c >> 8) & 0xff;
    expect(g).toBeLessThanOrEqual(0xe8);
    expect(g).toBeGreaterThanOrEqual(0xd4);
  });
  it("too-hard is the flat red cause color", () => {
    const r = oneRun("42,e,1,5,0,12,4,0,0,1,0,0,1,0");
    expect(glyphColor(r)).toBe(BUCKET_COLOR["too-hard"]);
  });
  it("fuel-out is the flat violet cause color", () => {
    const r = oneRun("42,e,1,5,1,96,157,0,0,0,0,0,1,0");
    expect(glyphColor(r)).toBe(BUCKET_COLOR["fuel-out"]);
  });
});

describe("glyphSize — too-hard/fuel-out scale with td_v", () => {
  it("a landed glyph is the base size", () => {
    const soft = oneRun("42,e,1,0,0,1,3,0,0,1,0,0,1,0");
    expect(glyphSize(soft)).toBeCloseTo(2.2, 6);
  });
  it("a harder crash is a bigger glyph than a marginal one", () => {
    const marginal = oneRun("42,e,1,5,0,6.1,4,0,0,1,0,0,1,0");
    const violent = oneRun("42,e,1,5,0,18,4,0,0,1,0,0,1,0");
    expect(glyphSize(violent)).toBeGreaterThan(glyphSize(marginal));
  });
  it("size is clamped (a 96 m/s fuel-out doesn't run away)", () => {
    const extreme = oneRun("42,e,1,5,1,96,157,0,0,0,0,0,1,0");
    expect(glyphSize(extreme)).toBeLessThanOrEqual(2.2 * 2.4 + 1e-6);
  });
});

describe("schematicArc — a legible dome, honestly invented", () => {
  it("ends at the touchdown point on the ground and starts high", () => {
    const r = oneRun("42,e,1,3,0,4,20,0,0,1,0,0,1,0");
    const [start, mid, end] = schematicArc(r);
    const [gx, gz] = glyphGroundXZ(r);
    expect(end[0]).toBeCloseTo(gx, 6);
    expect(end[2]).toBeCloseTo(gz, 6);
    expect(end[1]).toBeLessThan(1); // on/just above ground
    expect(start[1]).toBeGreaterThan(50); // enters high
    expect(mid[1]).toBeGreaterThan(end[1]);
    expect(mid[1]).toBeLessThan(start[1]);
  });
  it("the entry point is radially outboard of the touchdown (arc leans in)", () => {
    const r = oneRun("42,e,1,3,0,4,20,0,0,1,0,0,1,0");
    const [start] = schematicArc(r);
    const [gx, gz] = glyphGroundXZ(r);
    expect(Math.hypot(start[0], start[2])).toBeGreaterThan(Math.hypot(gx, gz));
  });
});

describe("small helpers", () => {
  it("clamp01 clamps", () => {
    expect(clamp01(-1)).toBe(0);
    expect(clamp01(2)).toBe(1);
    expect(clamp01(0.5)).toBe(0.5);
  });
  it("lerp interpolates", () => {
    expect(lerp(0, 10, 0.25)).toBe(2.5);
  });
  it("cssHex formats with leading zeros", () => {
    expect(cssHex(0x00ff00)).toBe("#00ff00");
    expect(cssHex(0xdfe8ff)).toBe("#dfe8ff");
  });
});
