// earth.ts — a NASA "Blue Marble" globe below the pad: the planet you see at entry
// altitude, curving away to a real horizon, with a day/night TERMINATOR (sunlit continents
// on one side, city lights on the other), a drifting cloud shell, and a fresnel atmosphere
// limb. Fades to the local ground on descent (mutually exclusive with the flat land group).
//
// TEXTURES: the real NASA equirectangular maps (public domain), bundled in
// ui/public/textures/planets/ so the portable app needs no network:
//   earth_atmos_2048.jpg  — Blue Marble day color (continents/oceans/ice, relief baked in)
//   earth_lights_2048.png — night city lights (emissive on the dark side)
//   earth_clouds_1024.png — cloud cover (white + alpha)
//
// SCALE NOTE: the true Earth (R=6371 km) is 3× past the render far plane and shows almost
// no curvature from 60 km. We use a STYLIZED radius (600 km) so the globe fits the extended
// far plane AND shows a believable curve at entry altitude — a "vibe-honest" Earth (round
// planet, real horizon, real terminator) rather than a to-scale one.
//
// LIGHTING: the surface is a self-lit MeshBasicNodeMaterial (TSL) — it does NOT depend on
// the scene lights (which the sky dims toward vacuum), so the globe always reads its real
// color. A fixed SUN_DIR (matched to the scene key light) drives the day/night mix in-shader.

import {
  Group,
  Mesh,
  SphereGeometry,
  MeshBasicNodeMaterial,
  TextureLoader,
  AdditiveBlending,
  SRGBColorSpace,
  type Texture,
} from "three/webgpu";
import {
  Fn,
  texture,
  uniform,
  normalWorld,
  positionWorld,
  cameraPosition,
  mix,
  smoothstep,
  vec3,
} from "three/tsl";

export const EARTH_R = 600_000; // stylized planet radius [m]

// World-space sun direction, matched to the scene key light at (60,120,40) so the globe's
// terminator agrees with the rest of the scene's shading. Pre-normalized (avoids a runtime
// node normalize on a constant): normalize(0.45, 1.0, 0.32).
const SUN_DIR = vec3(0.394, 0.875, 0.28);

export interface EarthEnv {
  root: Group;
  /** dayF: 1 at sea level (globe hidden — local ground owns the frame), 0 in space (globe shown). */
  update(dayF: number, dtSec: number): void;
  dispose(): void;
}

function loadTex(url: string, srgb: boolean): Texture {
  const t = new TextureLoader().load(url);
  if (srgb) t.colorSpace = SRGBColorSpace;
  t.anisotropy = 8;
  return t;
}

export function buildEarth(): EarthEnv {
  const root = new Group();
  // THREE.js is Y-UP: "down" is -Y (NOT -Z, which is horizontal/into-screen — the bug that
  // made the globe a vertical wall). Center the planet directly below so its top surface sits
  // ~120 m under the pad and it curves away beneath the rocket like real ground.
  root.position.y = -EARTH_R - 120;
  root.visible = false;

  const dayTex = loadTex("/textures/planets/earth_atmos_2048.jpg", true);
  const nightTex = loadTex("/textures/planets/earth_lights_2048.png", true);
  const cloudTex = loadTex("/textures/planets/earth_clouds_1024.png", true);

  // ---- surface: Blue Marble with an in-shader day/night terminator + city lights ----
  const surfMat = new MeshBasicNodeMaterial();
  surfMat.colorNode = Fn(() => {
    const N = normalWorld.normalize();
    const sd = N.dot(SUN_DIR); // -1 (midnight) .. +1 (noon)
    const dayAmt = smoothstep(-0.12, 0.3, sd); // soft terminator band
    const dayCol = texture(dayTex).rgb.mul(0.92); // the real Blue Marble, held just under bloom
    const nightCol = texture(nightTex).rgb.mul(1.4); // city-light emissive on the dark side
    const base = mix(nightCol, dayCol, dayAmt);
    // a FAINT atmospheric blue rim toward the limb (view-dependent). Kept subtle — the outer
    // halo shell does the heavy lifting; a strong add here washed the globe white.
    const V = cameraPosition.sub(positionWorld).normalize();
    const rim = N.dot(V).oneMinus().clamp(0, 1).pow(3.0);
    return base.add(vec3(0.08, 0.2, 0.42).mul(rim).mul(dayAmt.mul(0.5).add(0.05)));
  })();
  const surf = new Mesh(new SphereGeometry(EARTH_R, 128, 128), surfMat);
  root.add(surf);

  // ---- clouds: a thin translucent shell, faded out on the night side ----
  const cloudMat = new MeshBasicNodeMaterial({ transparent: true, depthWrite: false });
  cloudMat.colorNode = Fn(() => {
    const dayAmt = smoothstep(-0.05, 0.35, normalWorld.normalize().dot(SUN_DIR));
    return vec3(1, 1, 1).mul(dayAmt.max(0.04)); // white by day, near-black at night
  })();
  cloudMat.opacityNode = texture(cloudTex).a.mul(0.9);
  const clouds = new Mesh(new SphereGeometry(EARTH_R * 1.006, 96, 96), cloudMat);
  root.add(clouds);

  // ---- atmosphere: a limb halo (front-side additive; fresnel peaks at the edge, day side) ----
  const atmoStrength = uniform(0.0);
  const atmoMat = new MeshBasicNodeMaterial({
    transparent: true,
    depthWrite: false,
    blending: AdditiveBlending,
  });
  atmoMat.colorNode = Fn(() => {
    const N = normalWorld.normalize();
    const V = cameraPosition.sub(positionWorld).normalize();
    const fres = N.dot(V).oneMinus().clamp(0, 1).pow(3.0); // 0 at center -> 1 at limb
    const lit = smoothstep(-0.5, 0.25, N.dot(SUN_DIR)); // brighter on the day limb
    // fresnel baked into the color (with opacity=1 this is robust to the additive blend factor).
    return vec3(0.3, 0.56, 1.0).mul(fres).mul(lit.mul(0.85).add(0.15)).mul(atmoStrength);
  })();
  const atmo = new Mesh(new SphereGeometry(EARTH_R * 1.03, 96, 96), atmoMat);
  root.add(atmo);

  return {
    root,
    update(dayF, dtSec) {
      const spaceF = 1 - dayF; // 0 at sea level, 1 in space
      // MUTUALLY EXCLUSIVE with the flat local ground (documentaryScene toggles landGroup on
      // the same threshold): the GLOBE owns the frame at altitude, the flat ground below it.
      root.visible = dayF < 0.62;
      atmoStrength.value = 0.3 + 0.4 * spaceF; // gentle limb halo (was 0.6+0.7 — washed white)
      surf.rotation.y += dtSec * 0.004; // slow planetary spin about the polar (Y) axis
      clouds.rotation.y += dtSec * 0.0052; // clouds drift a touch faster than the surface
    },
    dispose() {
      surf.geometry.dispose();
      surfMat.dispose();
      clouds.geometry.dispose();
      cloudMat.dispose();
      atmo.geometry.dispose();
      atmoMat.dispose();
      dayTex.dispose();
      nightTex.dispose();
      cloudTex.dispose();
    },
  };
}
