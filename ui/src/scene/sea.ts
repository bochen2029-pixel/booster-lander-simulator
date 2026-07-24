// sea.ts — the SEA environment: a Gerstner-wave ocean + the ASDS droneship deck.
//
// This is the renderer half of the Target Stage-1 moving deck (core D-035..D-037):
// the sim streams the droneship deck's HEAVE (deck_z) + horizontal STATION (target_est_xy)
// + attitude (deck_quat) on the wire, and here the deck mesh is posed EXACTLY from those
// fields — TELEMETRY-HONEST (canon §A.0): the deck moves where the plant put it, never
// on a hand-keyed animation. The surrounding ocean is decorative garnish (canon §0.8 allows
// visual dressing that carries no truth): its Gerstner swell is NOT the sim's P-M spectrum
// (that phase-lock is the §11.9 HELLO-spectrum-transmission follow-up) — it is a plausible
// sea for context. Only the DECK is truth.
//
// SHADING (photoreal pass): the water is a TSL MeshStandardNodeMaterial — CPU Gerstner
// displacement carries the swell shape (pure + unit-tested, sea.test.ts pins it), and the
// GPU node graph carries the LOOK: two octaves of animated procedural normal detail
// (mx_fractal_noise gradients, amplitude faded with view distance so the horizon stays
// mirror-clean), physical deep-water albedo, fresnel sky reflection via the scene IBL, and
// crest foam as a per-vertex MASK broken up by a procedural bubble pattern (no more solid
// white vertex blobs). The far disc shares the material family with the detail faded out,
// and scene fog melts it into the sky at the horizon. All procedural — no textures (the
// r185 WebGPU backend does not sample CanvasTexture color maps; commit 908bc53).

import {
  Group,
  Mesh,
  BufferGeometry,
  BufferAttribute,
  CircleGeometry,
  BoxGeometry,
  CylinderGeometry,
  RingGeometry,
  PlaneGeometry,
  MeshStandardMaterial,
  MeshStandardNodeMaterial,
  DoubleSide,
  Quaternion,
  Sphere,
  Vector3,
} from "three/webgpu";
import {
  attribute,
  clamp,
  float,
  fract,
  mix,
  mx_fractal_noise_float,
  normalLocal,
  positionLocal,
  positionView,
  positionWorld,
  smoothstep,
  step,
  time,
  transformNormalToView,
  vec3,
} from "three/tsl";

// --- Gerstner ocean model (pure; unit-tested) --------------------------------

/** One Gerstner (trochoidal) wave component. dir is a horizontal unit vector (x,z). */
export interface GerstnerWave {
  dirX: number;
  dirZ: number;
  amp: number; // amplitude [m]
  len: number; // wavelength [m]
  speed: number; // phase speed [m/s]
  steep: number; // steepness 0..1 (horizontal pinch; >1/(k*amp*N) self-intersects)
}

/** A plausible moderate sea (a few components, mixed headings) — decorative, tunable. */
export const OCEAN_WAVES: GerstnerWave[] = [
  { dirX: 1.0, dirZ: 0.0, amp: 0.55, len: 120, speed: 9.0, steep: 0.65 },
  { dirX: 0.6, dirZ: 0.8, amp: 0.35, len: 61, speed: 6.4, steep: 0.7 },
  { dirX: -0.7, dirZ: 0.7, amp: 0.22, len: 31, speed: 4.6, steep: 0.75 },
  { dirX: 0.2, dirZ: -0.98, amp: 0.13, len: 17, speed: 3.3, steep: 0.8 },
];

const TWO_PI = Math.PI * 2;

/** Displaced position offset (ox,oy,oz) + unit normal (nx,ny,nz) of the sea surface at the
 * undisplaced horizontal grid point (x,z) at time t. y is up. Pure — the test pins it. */
export function gerstnerSample(
  x: number,
  z: number,
  t: number,
  waves: GerstnerWave[] = OCEAN_WAVES
): { ox: number; oy: number; oz: number; nx: number; ny: number; nz: number } {
  let ox = 0,
    oy = 0,
    oz = 0;
  // normal accumulators (Gerstner analytic normal; nAcc is the up (y) deficit)
  let nX = 0,
    nZ = 0,
    nY = 1;
  for (const w of waves) {
    const invLen = 1 / (w.len || 1);
    const k = TWO_PI * invLen; // wavenumber
    // normalize the heading (guard against unnormalized config)
    const dl = Math.hypot(w.dirX, w.dirZ) || 1;
    const dx = w.dirX / dl;
    const dz = w.dirZ / dl;
    const omega = w.speed * k;
    const phase = k * (dx * x + dz * z) - omega * t;
    const c = Math.cos(phase);
    const s = Math.sin(phase);
    const Q = w.steep; // steepness fraction
    const QA = Q * w.amp;
    ox += dx * QA * c;
    oz += dz * QA * c;
    oy += w.amp * s;
    const kA = k * w.amp;
    nX -= dx * kA * c;
    nZ -= dz * kA * c;
    nY -= Q * kA * s;
  }
  const nl = Math.hypot(nX, nY, nZ) || 1;
  return { ox, oy, oz, nx: nX / nl, ny: nY / nl, nz: nZ / nl };
}

// --- palette ------------------------------------------------------------------
// Real deep-water albedo is DARK (a few percent) — the ocean's brightness comes from the
// sky reflection (fresnel), not the body color. Slight green so it reads Atlantic.
const WATER_DEEP = [0.012, 0.045, 0.062] as const; // body albedo
const WATER_LIFT = [0.028, 0.1, 0.11] as const; // upwelling/scatter lift (subtle patching)
const FOAM_COL = [0.82, 0.87, 0.9] as const;
const HULL_STEEL = 0x232830; // dark hull sides / barge body
const DECK_TRIM = 0x14171c; // darker structural trim
const BULLSEYE = 0xe9edf1; // WHITE landing markings (ASDS grammar)
const SAFETY_YELLOW = 0xc79a2a; // worn deck-perimeter line

// The barge deck (the landing surface, = streamed deck_z) sits this far ABOVE the rendered
// waterline, so the hull shows freeboard and reads as a floating ship rather than a flat plate.
const SEA_FREEBOARD = 5.5;

// --- TSL water material -------------------------------------------------------
// One builder for both the near Gerstner grid (detail=1, foam attribute present) and the
// far horizon disc (detail low, no foam). The normal is the GEOMETRY normal (the CPU
// Gerstner analytic normal on the near grid) plus two octaves of animated procedural
// gradient detail, faded with view distance (sparkle near, mirror far — also the cheap
// anti-aliasing: distant micro-normals otherwise shimmer).
function waterNodeMaterial(opts: { detail: number; foam: boolean }): MeshStandardNodeMaterial {
  const mat = new MeshStandardNodeMaterial({
    roughness: 0.09,
    metalness: 0.0,
    envMapIntensity: 1.25,
  });

  const wp = positionWorld;
  // view-distance fade for the detail normals: full inside ~150 m, gone past ~1.2 km
  const dist = positionView.length();
  const detailFade = float(1.0).div(dist.mul(dist).div(160_000.0).add(1.0)); // 1/(1+(d/400)^2)

  // octave 1: ~6 m chop; octave 2: ~1.5 m ripple. Finite-difference gradients of
  // mx_fractal_noise, advected along +x/-z like the dominant swell heading.
  const e1 = float(0.9);
  const p1 = vec3(wp.x.mul(0.17).add(time.mul(0.55)), wp.z.mul(0.17).sub(time.mul(0.4)), 0.0);
  const n1x = mx_fractal_noise_float(p1.add(vec3(e1.mul(0.17), 0, 0)), 2, 2.0, 0.55)
    .sub(mx_fractal_noise_float(p1.sub(vec3(e1.mul(0.17), 0, 0)), 2, 2.0, 0.55));
  const n1z = mx_fractal_noise_float(p1.add(vec3(0, e1.mul(0.17), 0)), 2, 2.0, 0.55)
    .sub(mx_fractal_noise_float(p1.sub(vec3(0, e1.mul(0.17), 0)), 2, 2.0, 0.55));

  const p2 = vec3(wp.x.mul(0.71).sub(time.mul(0.9)), wp.z.mul(0.71).add(time.mul(0.7)), 4.7);
  const n2x = mx_fractal_noise_float(p2.add(vec3(0.35, 0, 0)), 2, 2.0, 0.5)
    .sub(mx_fractal_noise_float(p2.sub(vec3(0.35, 0, 0)), 2, 2.0, 0.5));
  const n2z = mx_fractal_noise_float(p2.add(vec3(0, 0.35, 0)), 2, 2.0, 0.5)
    .sub(mx_fractal_noise_float(p2.sub(vec3(0, 0.35, 0)), 2, 2.0, 0.5));

  const amp1 = float(0.55 * opts.detail).mul(detailFade);
  const amp2 = float(0.34 * opts.detail).mul(detailFade);
  const gx = n1x.mul(amp1).add(n2x.mul(amp2));
  const gz = n1z.mul(amp1).add(n2z.mul(amp2));
  const nDetail = normalLocal.add(vec3(gx, 0.0, gz)).normalize();
  mat.normalNode = transformNormalToView(nDetail);

  // color: dark deep-water body + a SUBTLE large-scale upwelling lift (no hard patches)
  const lift = mx_fractal_noise_float(
    vec3(wp.x.mul(0.006), wp.z.mul(0.006), time.mul(0.02)),
    2,
    2.0,
    0.5
  )
    .mul(0.5)
    .add(0.5);
  const deep = vec3(...WATER_DEEP);
  const lifted = vec3(...WATER_LIFT);
  let col = mix(deep, lifted, lift.mul(0.35));

  // roughness: glassy near, duller far (keeps the horizon reflection broad + clean),
  // and much rougher where foam sits.
  let rough = float(0.075).add(smoothstep(200.0, 2200.0, dist).mul(0.22));

  if (opts.foam) {
    // crest foam: the CPU writes a smooth 0..1 crest mask per vertex; break it up with a
    // procedural bubble pattern so it reads as lace, not paint.
    const bubbles = mx_fractal_noise_float(
      vec3(wp.x.mul(1.35).add(time.mul(0.5)), wp.z.mul(1.35), time.mul(0.22)),
      3,
      2.0,
      0.55
    )
      .mul(0.5)
      .add(0.5);
    // (@types/three 0.185 has no usable AttributeNode arithmetic typing — runtime is fine)
    const foamAttr = attribute("foam") as unknown as ReturnType<typeof float>;
    // distance-faded: foam is a NEAR-field read; at range it just reads as soap
    const foamMask = clamp(
      foamAttr.mul(smoothstep(0.32, 0.78, bubbles)).mul(detailFade.mul(0.8).add(0.2)),
      0.0,
      1.0
    );
    col = mix(col, vec3(...FOAM_COL), foamMask);
    rough = rough.add(foamMask.mul(0.5));
  }

  mat.colorNode = col;
  mat.roughnessNode = clamp(rough, 0.05, 0.7);
  return mat;
}

export interface SeaEnv {
  /** Add to the floating-origin `world` group. */
  root: Group;
  /** The droneship deck pivot — its transform is set from the streamed deck pose. */
  deck: Group;
  /** Show/hide the whole sea environment (toggled on the TLM SEA-active flag). */
  setActive(on: boolean): void;
  /** Resize the landing bullseye from HELLO pad_radius. */
  setPadRadius(r: number): void;
  /**
   * Per-frame. All inputs are already THREE-space (frame.ts is the only converter):
   *  - deckPos: the deck origin in three-space (x, y=deck_z, z).
   *  - deckQuat: the deck attitude in three-space (small pitch/roll), or null for level.
   *  - timeSec: wall/stream seconds for the decorative swell animation.
   */
  update(
    deckPos: { x: number; y: number; z: number },
    deckQuat: Quaternion | null,
    timeSec: number
  ): void;
  dispose(): void;
}

/** Build the SEA environment (ocean + droneship). `padRadius` sizes the bullseye. */
export function buildSea(padRadius = 30): SeaEnv {
  const root = new Group();
  root.visible = false; // dormant until the SEA flag lights it

  // --- far ocean: a disc to the fog horizon, same water family with the detail faded --
  const farMat = waterNodeMaterial({ detail: 0.35, foam: false });
  // reaches well past the fog horizon so its edge never shows and the Earth globe can't peek
  // out behind it at altitude; scene fog melts it into the sky.
  const farOcean = new Mesh(new CircleGeometry(250_000, 96), farMat);
  farOcean.rotation.x = -Math.PI / 2;
  farOcean.position.y = -0.15; // just under the near grid so the grid reads on top
  farOcean.receiveShadow = false;
  root.add(farOcean);

  // --- near ocean: a Gerstner grid around the deck (waves near the action) ------
  const NEAR_SPAN = 1600;
  const NEAR_SEG = 128;
  const near = buildOceanGrid(NEAR_SPAN, NEAR_SEG);
  root.add(near.mesh);

  // --- the ASDS droneship deck (posed from the wire) ---------------------------
  const deck = new Group();
  root.add(deck);

  // The ASDS droneship: a real barge, not a flat plate. The deck TOP (the landing surface)
  // sits at local y=0 (= streamed deck_z, three-space); a TALL hull drops from there through
  // the rendered waterline (SEA_FREEBOARD below), so the hull sides read above the swell.
  const deckLen = Math.max(110, padRadius * 3.0); // fore-aft
  const deckWid = Math.max(76, padRadius * 2.3); // beam
  const hullH = 11.0; // tall hull: ~5.5 m freeboard above water + ~5.5 m draft below
  const hullMat = new MeshStandardMaterial({ color: HULL_STEEL, roughness: 0.72, metalness: 0.35 });
  const trimMat = new MeshStandardMaterial({ color: DECK_TRIM, roughness: 0.65, metalness: 0.4 });

  // deck PLATING — TSL procedural: per-plate albedo variation + seam grid + rust wash at
  // the edges + roughness variation. Deck-LOCAL coords so the pattern rides the ship.
  const plateMat = new MeshStandardNodeMaterial({ roughness: 0.8, metalness: 0.12 });
  {
    const pl = positionLocal;
    const PLATE_X = 3.6,
      PLATE_Z = 2.8;
    const cellX = pl.x.div(PLATE_X).floor();
    const cellZ = pl.z.div(PLATE_Z).floor();
    // cheap per-plate hash off fractal noise at the cell center (stable, procedural)
    const plateHash = mx_fractal_noise_float(vec3(cellX.mul(7.31), cellZ.mul(11.7), 3.1), 1, 2.0, 0.5)
      .mul(0.5)
      .add(0.5);
    const fx = fract(pl.x.div(PLATE_X));
    const fz = fract(pl.z.div(PLATE_Z));
    const seam = step(fx, 0.015).max(step(float(0.985), fx)).max(step(fz, 0.02).max(step(float(0.98), fz)));
    // rust wash: noise high-passed, weighted toward the deck edges + plate seams
    const rustN = mx_fractal_noise_float(vec3(pl.x.mul(0.3), pl.z.mul(0.3), 9.4), 3, 2.0, 0.55)
      .mul(0.5)
      .add(0.5);
    const edgeW = smoothstep(float(deckLen * 0.5 - 14), float(deckLen * 0.5 - 2), pl.x.abs()).max(
      smoothstep(float(deckWid * 0.5 - 12), float(deckWid * 0.5 - 2), pl.z.abs())
    );
    const rust = smoothstep(0.52, 0.9, rustN).mul(edgeW.mul(0.75).add(0.12));
    const steel = vec3(0.135, 0.15, 0.172).mul(plateHash.mul(0.18).add(0.86)); // ±9% per plate, darker floor
    const rustCol = vec3(0.27, 0.16, 0.1);
    let dcol = mix(steel, rustCol, clamp(rust, 0.0, 0.55));
    dcol = dcol.mul(seam.mul(-0.28).add(1.0)); // darken the seams
    plateMat.colorNode = dcol;
    plateMat.roughnessNode = clamp(float(0.68).add(rust.mul(0.25)).add(plateHash.mul(0.08)), 0.4, 0.95);
  }

  // hull body (dark steel sides; top face is the landing deck at y=0). A lighter deck-plate
  // slab is inset just BELOW the top and the bullseye rides ABOVE it, so no two faces share a
  // plane (coincident faces at y=0 were z-fighting into a crosshatch moiré).
  const hull = new Mesh(new BoxGeometry(deckLen, hullH, deckWid), hullMat);
  hull.position.y = -hullH / 2 - 0.4; // top at y=-0.4, under the deck plate
  hull.castShadow = true;
  hull.receiveShadow = true;
  deck.add(hull);

  // rub strake — a dark chine line around the hull at the waterline (breaks the slab face)
  const strake = new Mesh(new BoxGeometry(deckLen * 1.005, 0.5, deckWid * 1.005), trimMat);
  strake.position.y = -SEA_FREEBOARD + 0.4;
  deck.add(strake);

  // plated landing deck: a slab sitting ON the hull, its top the landing surface (y≈0)
  const plate = new Mesh(new BoxGeometry(deckLen * 0.99, 0.8, deckWid * 0.99), plateMat);
  plate.position.y = -0.4; // top at y=0, bottom on the hull top (y=-0.8)
  plate.receiveShadow = true;
  deck.add(plate);

  // raised blast-wall rim around the deck edge (silhouette + landing lip)
  const rimH = 1.8;
  const rimT = 2.2;
  for (const [sx, sz, w, d] of [
    [0, deckWid / 2, deckLen, rimT] as const,
    [0, -deckWid / 2, deckLen, rimT] as const,
    [deckLen / 2, 0, rimT, deckWid] as const,
    [-deckLen / 2, 0, rimT, deckWid] as const,
  ]) {
    const rim = new Mesh(new BoxGeometry(w, rimH, d), trimMat);
    rim.position.set(sx, rimH / 2, sz);
    rim.castShadow = true;
    rim.receiveShadow = true;
    deck.add(rim);
  }

  // worn yellow perimeter safety line, inset from the rim
  const lineMat = new MeshStandardMaterial({ color: SAFETY_YELLOW, roughness: 0.6, metalness: 0.05 });
  for (const [sx, sz, w, d] of [
    [0, deckWid / 2 - 4.2, deckLen - 12, 0.45] as const,
    [0, -(deckWid / 2 - 4.2), deckLen - 12, 0.45] as const,
    [deckLen / 2 - 4.2, 0, 0.45, deckWid - 12] as const,
    [-(deckLen / 2 - 4.2), 0, 0.45, deckWid - 12] as const,
  ]) {
    const ln = new Mesh(new BoxGeometry(w, 0.02, d), lineMat);
    ln.position.set(sx, 0.012, sz);
    ln.receiveShadow = true;
    deck.add(ln);
  }

  // station-keeping thruster WINGS jutting out each long side — the signature ASDS look
  for (const side of [1, -1] as const) {
    const wing = new Mesh(new BoxGeometry(deckLen * 0.46, 3.4, 11), trimMat);
    wing.position.set(deckLen * 0.05, -1.7, side * (deckWid / 2 + 5));
    wing.castShadow = true;
    wing.receiveShadow = true;
    deck.add(wing);
    // azimuth-thruster pods hanging under each wing (visible at the waterline)
    for (const px of [-deckLen * 0.12, deckLen * 0.22] as const) {
      const pod = new Mesh(new CylinderGeometry(1.9, 2.2, 3.2, 20), hullMat);
      pod.position.set(px, -4.6, side * (deckWid / 2 + 5));
      deck.add(pod);
    }
  }

  // stern superstructure: containers/equipment at ONE end (deck stays clear for the
  // landing bullseye). Mixed cargo colors so it reads as real equipment, not black boxes.
  const contCols = [0x24384f, 0x4c5560, 0x64382a, 0x2e4a41, 0x3f4650];
  for (let i = 0; i < 5; i++) {
    const h = 6 + (i % 3) * 3.5;
    const cmat = new MeshStandardMaterial({ color: contCols[i % contCols.length], roughness: 0.62, metalness: 0.3 });
    const box = new Mesh(new BoxGeometry(11, h, 13), cmat);
    box.position.set(
      -deckLen / 2 + 10 + (i % 2) * 13,
      h / 2,
      -deckWid / 2 + 12 + Math.floor(i / 2) * 15
    );
    box.castShadow = true;
    box.receiveShadow = true;
    deck.add(box);
  }
  // comms mast on the superstructure
  const mast = new Mesh(new CylinderGeometry(0.18, 0.3, 14, 10), trimMat);
  mast.position.set(-deckLen / 2 + 16, 14, -deckWid / 2 + 20);
  mast.castShadow = true;
  deck.add(mast);

  // landing bullseye: WHITE concentric rings + circle-X (ASDS grammar) — painted metal,
  // lit by the sun like everything else (Basic glowed at dusk).
  const bull = new Group();
  bull.position.y = 0.05; // just above the deck plate
  deck.add(bull);
  const padDiscMat = new MeshStandardMaterial({ color: 0x343a43, roughness: 0.9, metalness: 0.04, side: DoubleSide });
  let padDisc = new Mesh(new CircleGeometry(padRadius, 96), padDiscMat);
  padDisc.rotation.x = -Math.PI / 2;
  padDisc.receiveShadow = true;
  bull.add(padDisc);
  const ringMat = new MeshStandardMaterial({ color: BULLSEYE, roughness: 0.55, metalness: 0.05, side: DoubleSide });
  const ringsHandle: Mesh[] = [];
  function rebuildBullseye(r: number): void {
    for (const m of ringsHandle) {
      bull.remove(m);
      m.geometry.dispose();
    }
    ringsHandle.length = 0;
    // REPLACE the disc mesh — swapping .geometry on a live mesh breaks its pipeline on
    // this WebGPU backend (renders unlit white; same family as the hull-rebind bug).
    bull.remove(padDisc);
    padDisc.geometry.dispose();
    padDisc = new Mesh(new CircleGeometry(r, 96), padDiscMat);
    padDisc.rotation.x = -Math.PI / 2;
    padDisc.receiveShadow = true;
    bull.add(padDisc);
    // two concentric rings + a center X, LZ-1/ASDS grammar
    for (const frac of [0.95, 0.6]) {
      const ring = new Mesh(new RingGeometry(r * frac * 0.86, r * frac, 96), ringMat);
      ring.rotation.x = -Math.PI / 2;
      ring.position.y = 0.02;
      ring.receiveShadow = true;
      bull.add(ring);
      ringsHandle.push(ring);
    }
    for (const rot of [Math.PI / 4, -Math.PI / 4]) {
      const bar = new Mesh(new PlaneGeometry(r * 0.12, r * 1.4), ringMat);
      bar.rotation.x = -Math.PI / 2;
      bar.rotation.z = rot;
      bar.position.y = 0.03;
      bar.receiveShadow = true;
      bull.add(bar);
      ringsHandle.push(bar);
    }
  }
  rebuildBullseye(padRadius);

  const _q = new Quaternion();

  return {
    root,
    deck,
    setActive(on: boolean) {
      root.visible = on;
    },
    setPadRadius(r: number) {
      if (r > 0) rebuildBullseye(r);
    },
    update(deckPos, deckQuat, timeSec) {
      if (!root.visible) return;
      // pose the deck EXACTLY from the wire (three-space already)
      deck.position.set(deckPos.x, deckPos.y, deckPos.z);
      if (deckQuat) deck.quaternion.copy(deckQuat);
      else deck.quaternion.identity();
      // the near-ocean grid follows the deck horizontally so the waves stay under the
      // action (the swell is world-anchored via the sampled world x/z, so it does not
      // "slide" with the grid — only the sampling window recenters). Keep it at sea
      // level (y≈0); the deck heaves relative to it, which reads as riding the swell.
      near.mesh.position.set(deckPos.x, -SEA_FREEBOARD, deckPos.z);
      farOcean.position.set(deckPos.x, -SEA_FREEBOARD - 0.15, deckPos.z);
      near.animate(deckPos.x, deckPos.z, timeSec);
      _q.identity();
    },
    dispose() {
      near.dispose();
      farMat.dispose();
      farOcean.geometry.dispose();
    },
  };
}

// --- near-ocean Gerstner grid (CPU displacement + analytic normals) -----------

interface OceanGrid {
  mesh: Mesh;
  /** Re-displace the grid for a sampling window centered at world (cx,cz) at time t. */
  animate(cx: number, cz: number, t: number): void;
  dispose(): void;
}

function buildOceanGrid(span: number, seg: number): OceanGrid {
  const n = seg + 1;
  const count = n * n;
  const positions = new Float32Array(count * 3);
  const normals = new Float32Array(count * 3);
  const foam = new Float32Array(count); // smooth 0..1 crest mask — the TSL material laces it
  // base grid in local XZ centered on the mesh (the mesh is moved to follow the deck)
  const base = new Float32Array(count * 2); // (bx, bz) undisplaced local horizontal
  const step = span / seg;
  let i = 0;
  for (let iz = 0; iz < n; iz++) {
    for (let ix = 0; ix < n; ix++) {
      const bx = -span / 2 + ix * step;
      const bz = -span / 2 + iz * step;
      base[i * 2] = bx;
      base[i * 2 + 1] = bz;
      positions[i * 3] = bx;
      positions[i * 3 + 1] = 0;
      positions[i * 3 + 2] = bz;
      normals[i * 3 + 1] = 1;
      i++;
    }
  }
  // indices (two triangles per quad)
  const idx: number[] = [];
  for (let iz = 0; iz < seg; iz++) {
    for (let ix = 0; ix < seg; ix++) {
      const a = iz * n + ix;
      const b = a + 1;
      const c = a + n;
      const d = c + 1;
      idx.push(a, c, b, b, c, d);
    }
  }
  const geo = new BufferGeometry();
  const posAttr = new BufferAttribute(positions, 3);
  const normAttr = new BufferAttribute(normals, 3);
  const foamAttr = new BufferAttribute(foam, 1);
  posAttr.setUsage(0x88e8); // DYNAMIC_DRAW
  normAttr.setUsage(0x88e8);
  foamAttr.setUsage(0x88e8);
  geo.setAttribute("position", posAttr);
  geo.setAttribute("normal", normAttr);
  geo.setAttribute("foam", foamAttr);
  geo.setIndex(idx);
  // static bound (displacement is ±~1 m — skip the per-frame recompute)
  geo.boundingSphere = new Sphere(new Vector3(0, 0, 0), span);

  const mat = waterNodeMaterial({ detail: 1.0, foam: true });
  const mesh = new Mesh(geo, mat);
  mesh.receiveShadow = true;

  function animate(cx: number, cz: number, t: number): void {
    for (let j = 0; j < count; j++) {
      const bx = base[j * 2];
      const bz = base[j * 2 + 1];
      // sample in WORLD space so the swell is world-anchored (does not slide with the
      // grid recenter); the mesh is translated to (cx,0,cz), so world = base + center.
      const g = gerstnerSample(bx + cx, bz + cz, t);
      positions[j * 3] = bx + g.ox;
      positions[j * 3 + 1] = g.oy;
      positions[j * 3 + 2] = bz + g.oz;
      normals[j * 3] = g.nx;
      normals[j * 3 + 1] = g.ny;
      normals[j * 3 + 2] = g.nz;
      // smooth crest mask: high crests + steep slopes → foam candidates (the material
      // multiplies by a bubble pattern + distance fade, so lace — not paint)
      const crest = Math.max(0, (g.oy - 0.56) * 1.05) + Math.max(0, (0.36 - g.ny) * 1.15);
      foam[j] = Math.min(1, crest * 0.9);
    }
    posAttr.needsUpdate = true;
    normAttr.needsUpdate = true;
    foamAttr.needsUpdate = true;
  }
  animate(0, 0, 0);

  return {
    mesh,
    animate,
    dispose() {
      geo.dispose();
      mat.dispose();
    },
  };
}
