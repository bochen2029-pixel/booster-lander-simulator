// fdai.test.ts — pins the pure half of the attitude ball: the gimbal rotation matrix agrees with
// the kernel's extraction, and the painted features land where an Apollo 8-ball puts them.
import { describe, it, expect } from "vitest";
import { Matrix4 } from "three";
import { BALL_PAINT, ballPixel, gimbalRotation, renderBall } from "./fdai";
import { gimbalAnglesFromElements } from "./imu";

const D2R = Math.PI / 180;

function colorAt(u: number, v: number, o: number, m: number, i: number): [number, number, number] | null {
  const R = gimbalRotation(o * D2R, m * D2R, i * D2R, new Float64Array(9));
  const out = [0, 0, 0];
  return ballPixel(u, v, R, out, 0) ? [out[0]!, out[1]!, out[2]!] : null;
}
/** which paint a shaded pixel came from (shading scales all channels equally) */
function paintOf(rgb: [number, number, number]): string {
  const [r, g, b] = rgb;
  for (const [name, c] of Object.entries(BALL_PAINT)) {
    const s = r / c[0];
    if (s > 0 && Math.abs(g - c[1] * s) < 1e-6 && Math.abs(b - c[2] * s) < 1e-6) return name;
  }
  return "?";
}

describe("gimbalRotation ⇄ gimbalAnglesFromElements", () => {
  it("round-trips a triple through the row-major → column-major matrix", () => {
    for (const [o, m, i] of [[10, 20, 30], [-50, 40, 100], [120, -70, -20]] as const) {
      const R = gimbalRotation(o * D2R, m * D2R, i * D2R, new Float64Array(9));
      // Matrix4.set is row-major
      const M = new Matrix4().set(R[0]!, R[1]!, R[2]!, 0, R[3]!, R[4]!, R[5]!, 0, R[6]!, R[7]!, R[8]!, 0, 0, 0, 0, 1);
      const out = { oga: 0, mga: 0, iga: 0 };
      gimbalAnglesFromElements(M.elements, out);
      expect(out.oga / D2R).toBeCloseTo(o, 6);
      expect(out.mga / D2R).toBeCloseTo(m, 6);
      expect(out.iga / D2R).toBeCloseTo(i, 6);
    }
  });
});

describe("ball paint at zero attitude", () => {
  it("shows the zero marker dead centre and the split line across the middle", () => {
    expect(paintOf(colorAt(0, 0, 0, 0, 0)!)).toBe("zero");
    expect(paintOf(colorAt(0.4, 0, 0, 0, 0)!)).toBe("split"); // on the equator, right of centre
    expect(paintOf(colorAt(-0.4, 0, 0, 0, 0)!)).toBe("split");
  });
  it("puts the upper hemisphere above the line and the lower below it", () => {
    // (0, ±0.45) lies ON the prime meridian through the zero marker (a painted grid line), so
    // sample just off it: lon ≈ 19.6°, lat ≈ ±26.7° — between the 0° and 30° lines of both kinds.
    expect(paintOf(colorAt(0.3, 0.45, 0, 0, 0)!)).toBe("upper");
    expect(paintOf(colorAt(0.3, -0.45, 0, 0, 0)!)).toBe("lower");
    expect(paintOf(colorAt(0, 0.45, 0, 0, 0)!)).toBe("grid"); // the meridian itself
  });
  it("paints the gimbal-lock caps at the left/right limb (±SM Y)", () => {
    expect(paintOf(colorAt(0.999, 0, 0, 0, 0)!)).toBe("lock");
    expect(paintOf(colorAt(-0.999, 0, 0, 0, 0)!)).toBe("lock");
  });
  it("returns nothing outside the disc", () => {
    expect(colorAt(0.8, 0.8, 0, 0, 0)).toBeNull();
  });
});

describe("ball paint follows the gimbals", () => {
  it("at MGA = 90° the lock cap faces the viewer", () => {
    expect(paintOf(colorAt(0, 0, 0, 90, 0)!)).toBe("lock");
    expect(paintOf(colorAt(0, 0, 0, -90, 0)!)).toBe("lock");
  });
  it("a pitch (IGA) slides the split line off centre; a roll (OGA) keeps the centre on it", () => {
    expect(paintOf(colorAt(0, 0, 0, 0, 40)!)).not.toBe("split");
    expect(paintOf(colorAt(0, 0, 0, 0, 40)!)).not.toBe("zero");
    // with pitch +40°, the zero marker (+SM X) moves to where p_case = Rᵀ·x̂: v = sin(-40°)... find it:
    // p_SM = R·p_case, and we want p_SM = x̂ → p_case = Rᵀ x̂ = first ROW of R = (cm·ci, −sm, cm·si) = (cos40, 0, sin40)
    // so (d, u, v) = (cos40, 0, sin40): the zero marker is straight ABOVE centre at v = sin 40°.
    expect(paintOf(colorAt(0, Math.sin(40 * D2R), 0, 0, 40)!)).toBe("zero");
    expect(paintOf(colorAt(0, 0, 30, 0, 0)!)).toBe("zero"); // roll about the viewing axis
  });
  it("a yaw (MGA) slides the zero marker sideways toward the lock cap", () => {
    // p_case = first row of R for (o=0, m=+30, i=0) = (cos30, −sin30, 0): marker at u = −sin 30°
    expect(paintOf(colorAt(-Math.sin(30 * D2R), 0, 0, 30, 0)!)).toBe("zero");
  });
});

describe("renderBall", () => {
  it("fills an RGBA buffer with an opaque disc and transparent corners", () => {
    const size = 32;
    const rgba = new Uint8ClampedArray(size * size * 4);
    renderBall(rgba, size, gimbalRotation(0, 0, 0, new Float64Array(9)));
    const at = (x: number, y: number) => rgba[(y * size + x) * 4 + 3];
    expect(at(0, 0)).toBe(0);
    expect(at(size - 1, size - 1)).toBe(0);
    expect(at(size / 2, size / 2)).toBe(255);
    expect(at(size / 2, 1)).toBe(255);
  });
});
