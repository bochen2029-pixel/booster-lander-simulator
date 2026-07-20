// earth.ts — a stylized 3D Earth globe below the pad: the "planet below" you see at
// entry altitude, curving away to a real horizon, fading to the local ground on descent.
//
// SCALE NOTE: the true Earth (R=6371 km) is 32× past the render far plane and would show
// almost no curvature from 60 km. We use a STYLIZED radius (600 km) so the globe fits the
// extended far plane AND shows a believable curve at entry altitude — a "vibe-honest" Earth
// (round planet, real horizon) rather than a to-scale one. The day texture is generated
// procedurally (FBM continents + ice caps + ocean depth) so the portable app needs no asset;
// drop a real equirectangular earth map into the material's `map` for photoreal.

import {
  Group,
  Mesh,
  SphereGeometry,
  MeshStandardMaterial,
  MeshBasicMaterial,
  BackSide,
  AdditiveBlending,
  CanvasTexture,
  SRGBColorSpace,
} from "three/webgpu";

export const EARTH_R = 600_000; // stylized planet radius [m]

export interface EarthEnv {
  root: Group;
  /** dayF: 1 at sea level (globe hidden — local ground owns the frame), 0 in space (globe shown). */
  update(dayF: number, dtSec: number): void;
  dispose(): void;
}

export function buildEarth(): EarthEnv {
  const root = new Group();
  root.position.z = -EARTH_R - 120; // center far below; top surface ~120 m under the pad (no z-fight)
  root.visible = false;

  const tex = makeEarthTexture();
  // self-lit via emissiveMap so the globe reads its blue/green even where the sun isn't
  // facing it (the hemisphere fill is dimmed in space) — otherwise it renders flat gray.
  const earthMat = new MeshStandardMaterial({
    map: tex,
    emissiveMap: tex,
    emissive: 0xffffff,
    emissiveIntensity: 0.4,
    roughness: 0.96,
    metalness: 0.0,
  });
  const earth = new Mesh(new SphereGeometry(EARTH_R, 96, 96), earthMat);
  root.add(earth);

  // atmosphere: a faint additive back-side shell — a soft blue limb haze. (A true fresnel
  // rim node is a follow-up; this reads as atmosphere at the horizon without a shader.)
  const atmoMat = new MeshBasicMaterial({
    color: 0x5aa0e6,
    transparent: true,
    opacity: 0,
    side: BackSide,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const atmo = new Mesh(new SphereGeometry(EARTH_R * 1.012, 64, 64), atmoMat);
  root.add(atmo);

  return {
    root,
    update(dayF, dtSec) {
      const spaceF = 1 - dayF; // 0 at sea level, 1 in space
      // MUTUALLY EXCLUSIVE with the flat local ground (documentaryScene toggles the land
      // group with the same threshold): the GLOBE owns the frame at altitude (dayF<0.45,
      // ~above 13 km), the flat ground owns it below — so you never see two grounds at once.
      root.visible = dayF < 0.62; // keep the globe (its curvature) down to ~9 km — more of the descent
      atmoMat.opacity = 0.16 * spaceF; // faint limb haze (was 0.35 — it washed the globe gray)
      earth.rotation.z += dtSec * 0.0005; // a slow, subtle spin
    },
    dispose() {
      earth.geometry.dispose();
      earthMat.dispose();
      tex.dispose();
      atmo.geometry.dispose();
      atmoMat.dispose();
    },
  };
}

/** Procedural equirectangular Earth: FBM value-noise continents, polar ice, ocean depth. */
function makeEarthTexture(): CanvasTexture {
  const W = 2048;
  const H = 1024;
  const cv = document.createElement("canvas");
  cv.width = W;
  cv.height = H;
  const ctx = cv.getContext("2d");
  if (!ctx) return new CanvasTexture(cv);
  const img = ctx.createImageData(W, H);
  const d = img.data;

  const hash = (x: number, y: number) => {
    let h = (x * 374761393 + y * 668265263) | 0;
    h = (h ^ (h >> 13)) * 1274126177;
    return ((h ^ (h >> 16)) >>> 0) / 4294967295;
  };
  const vnoise = (x: number, y: number) => {
    const xi = Math.floor(x);
    const yi = Math.floor(y);
    const xf = x - xi;
    const yf = y - yi;
    const u = xf * xf * (3 - 2 * xf);
    const v = yf * yf * (3 - 2 * yf);
    const a = hash(xi, yi);
    const b = hash(xi + 1, yi);
    const c = hash(xi, yi + 1);
    const e = hash(xi + 1, yi + 1);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + e * u) * v;
  };
  const fbm = (x: number, y: number) => {
    let s = 0;
    let amp = 0.5;
    let f = 1;
    for (let o = 0; o < 5; o++) {
      s += amp * vnoise(x * f, y * f);
      f *= 2;
      amp *= 0.5;
    }
    return s;
  };

  for (let j = 0; j < H; j++) {
    const lat = (j / H) * Math.PI - Math.PI / 2; // -π/2..π/2
    const iceEdge = 1.2; // ~69°
    const iceFade = Math.max(0, (Math.abs(lat) - iceEdge) / (Math.PI / 2 - iceEdge));
    for (let i = 0; i < W; i++) {
      const n = fbm(i * 0.02, j * 0.02);
      const idx = (j * W + i) * 4;
      let r: number;
      let g: number;
      let b: number;
      if (n > 0.52) {
        const h = (n - 0.52) / 0.48; // 0..1 inland
        r = 66 + 104 * h;
        g = 130 + 42 * h;
        b = 58 + 34 * h; // vivid green coasts → tan interior
      } else {
        const dep = (0.52 - n) / 0.52; // 0 shallow .. 1 deep
        r = 22 + 44 * (1 - dep);
        g = 72 + 74 * (1 - dep);
        b = 128 + 96 * (1 - dep); // deep blue → bright cyan shallows (was dark navy = gray)
      }
      // ice caps toward the poles
      if (iceFade > 0) {
        const k = Math.min(1, iceFade * 1.4);
        r = r * (1 - k) + 236 * k;
        g = g * (1 - k) + 240 * k;
        b = b * (1 - k) + 246 * k;
      }
      d[idx] = r;
      d[idx + 1] = g;
      d[idx + 2] = b;
      d[idx + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  const tex = new CanvasTexture(cv);
  tex.colorSpace = SRGBColorSpace;
  return tex;
}
