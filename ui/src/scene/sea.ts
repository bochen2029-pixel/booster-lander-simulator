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
// VERIFICATION NOTE (built without a headless screenshot path — see the frontend reports):
// the ocean uses a CPU Gerstner grid so `gerstnerSample()` is pure + unit-tested
// (sea.test.ts) rather than an unverifiable vertex shader; the deck pose is asserted via
// eval against the streamed deck_z. Colors / foam / specular are sensible defaults flagged
// for eyes-on tuning.

import {
  Group,
  Mesh,
  BufferGeometry,
  BufferAttribute,
  CircleGeometry,
  BoxGeometry,
  RingGeometry,
  PlaneGeometry,
  MeshStandardMaterial,
  MeshBasicMaterial,
  DoubleSide,
  Quaternion,
} from "three/webgpu";

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

// --- palette (sensible defaults; flagged for eyes-on tuning) ------------------
const SEA_DEEP = 0x0b3550; // deep water body (saturated ocean blue)
const SEA_SHALLOW = 0x1f6f8f; // near-surface teal
const DECK_STEEL = 0x3a4048; // ASDS steel landing deck (lighter top plate)
const HULL_STEEL = 0x20242b; // dark hull sides / barge body
const DECK_TRIM = 0x121519; // darker structural trim / superstructure
const BULLSEYE = 0xe6ebf2; // white landing bullseye rings
const FOAM = 0xeaf2f5; // crest / waterline foam

// The barge deck (the landing surface, = streamed deck_z) sits this far ABOVE the rendered
// waterline, so the hull shows freeboard and reads as a floating ship rather than a flat plate.
const SEA_FREEBOARD = 5.5;

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

  // --- far ocean: a flat blue disc to the fog horizon (fills the distance) -----
  const farMat = new MeshStandardMaterial({
    color: SEA_DEEP,
    roughness: 0.35,
    metalness: 0.0,
    emissive: SEA_DEEP, // a blue floor so the water never washes to the tan ground-bounce
    emissiveIntensity: 0.28,
  });
  const farOcean = new Mesh(new CircleGeometry(60_000, 96), farMat);
  farOcean.rotation.x = -Math.PI / 2;
  farOcean.position.y = -0.15; // just under the near grid so the grid reads on top
  farOcean.receiveShadow = false;
  root.add(farOcean);

  // --- near ocean: a Gerstner grid around the deck (waves near the action) ------
  // 1.6 km span, 160 m resolution is too coarse for the shortest wave; 128 seg over
  // 1.6 km = 12.5 m/quad resolves the 17-120 m components acceptably (crest-tuning
  // is an eyes-on follow-up). Extends past the deck; the far disc covers the rest.
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
  const deckMat = new MeshStandardMaterial({ color: DECK_STEEL, roughness: 0.9, metalness: 0.18 });
  const hullMat = new MeshStandardMaterial({ color: HULL_STEEL, roughness: 0.78, metalness: 0.32 });
  const trimMat = new MeshStandardMaterial({ color: DECK_TRIM, roughness: 0.7, metalness: 0.4 });

  // hull body (dark steel sides; top face is the landing deck at y=0)
  const hull = new Mesh(new BoxGeometry(deckLen, hullH, deckWid), hullMat);
  hull.position.y = -hullH / 2;
  hull.castShadow = true;
  hull.receiveShadow = true;
  deck.add(hull);

  // lighter deck plating slab on top so the landing surface reads distinct from the hull
  const plate = new Mesh(new BoxGeometry(deckLen * 0.99, 0.7, deckWid * 0.99), deckMat);
  plate.position.y = -0.35;
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
    deck.add(rim);
  }

  // station-keeping thruster WINGS jutting out each long side — the signature ASDS look
  for (const side of [1, -1] as const) {
    const wing = new Mesh(new BoxGeometry(deckLen * 0.46, 3.4, 11), trimMat);
    wing.position.set(deckLen * 0.05, -1.7, side * (deckWid / 2 + 5));
    wing.castShadow = true;
    deck.add(wing);
  }

  // stern superstructure: a stack of containers/equipment at ONE end (deck stays clear for
  // the landing bullseye at center). Tall massing so the ship silhouettes against the sky.
  for (let i = 0; i < 5; i++) {
    const h = 6 + (i % 3) * 3.5;
    const box = new Mesh(new BoxGeometry(11, h, 13), trimMat);
    box.position.set(
      -deckLen / 2 + 10 + (i % 2) * 13,
      h / 2,
      -deckWid / 2 + 12 + Math.floor(i / 2) * 15
    );
    box.castShadow = true;
    deck.add(box);
  }

  // landing bullseye: concentric rings + a white pad disc on the deck surface.
  const bull = new Group();
  bull.position.y = 0.05; // just above the deck plate
  deck.add(bull);
  const padDiscMat = new MeshStandardMaterial({ color: 0x3a3f47, roughness: 0.9, metalness: 0.05, side: DoubleSide });
  const padDisc = new Mesh(new CircleGeometry(padRadius, 64), padDiscMat);
  padDisc.rotation.x = -Math.PI / 2;
  bull.add(padDisc);
  const ringMat = new MeshBasicMaterial({ color: BULLSEYE, transparent: true, opacity: 0.9, side: DoubleSide });
  const ringsHandle: Mesh[] = [];
  function rebuildBullseye(r: number): void {
    for (const m of ringsHandle) {
      bull.remove(m);
      m.geometry.dispose();
    }
    ringsHandle.length = 0;
    padDisc.geometry.dispose();
    padDisc.geometry = new CircleGeometry(r, 64);
    // two concentric rings + a center X, LZ-1/ASDS grammar
    for (const frac of [0.95, 0.6]) {
      const ring = new Mesh(new RingGeometry(r * frac * 0.86, r * frac, 64), ringMat);
      ring.rotation.x = -Math.PI / 2;
      ring.position.y = 0.02;
      bull.add(ring);
      ringsHandle.push(ring);
    }
    for (const rot of [Math.PI / 4, -Math.PI / 4]) {
      const bar = new Mesh(new PlaneGeometry(r * 0.12, r * 1.4), ringMat);
      bar.rotation.x = -Math.PI / 2;
      bar.rotation.z = rot;
      bar.position.y = 0.03;
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
  const colors = new Float32Array(count * 3);
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
  const colAttr = new BufferAttribute(colors, 3);
  posAttr.setUsage(0x88e8); // DYNAMIC_DRAW
  normAttr.setUsage(0x88e8);
  geo.setAttribute("position", posAttr);
  geo.setAttribute("normal", normAttr);
  geo.setAttribute("color", colAttr);
  geo.setIndex(idx);

  const mat = new MeshStandardMaterial({
    vertexColors: true,
    roughness: 0.2, // glossy water; the sun key gives a specular sheet
    metalness: 0.0,
    side: DoubleSide,
    emissive: SEA_DEEP, // blue floor: down-facing wave slopes keep the ocean color, not tan
    emissiveIntensity: 0.22,
  });
  const mesh = new Mesh(geo, mat);
  mesh.receiveShadow = true;

  // reusable colors
  const deepR = ((SEA_DEEP >> 16) & 255) / 255,
    deepG = ((SEA_DEEP >> 8) & 255) / 255,
    deepB = (SEA_DEEP & 255) / 255;
  const shR = ((SEA_SHALLOW >> 16) & 255) / 255,
    shG = ((SEA_SHALLOW >> 8) & 255) / 255,
    shB = (SEA_SHALLOW & 255) / 255;
  const foamR = ((FOAM >> 16) & 255) / 255,
    foamG = ((FOAM >> 8) & 255) / 255,
    foamB = (FOAM & 255) / 255;

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
      // depth + crest tint: deep in troughs, teal near flat, foam on steep crests
      const up = g.ny; // ~1 flat, <1 on slopes
      // foam only on the STEEPEST crests (raised thresholds — the sea was over-whitecapped)
      const crest = Math.max(0, (g.oy - 0.62) * 1.1) + Math.max(0, (0.34 - up) * 0.9);
      const cf = Math.min(1, crest);
      const df = Math.min(1, Math.max(0, 0.5 - g.oy * 0.6)); // deeper look in troughs
      const r = deepR * df + shR * (1 - df);
      const gg = deepG * df + shG * (1 - df);
      const bb = deepB * df + shB * (1 - df);
      colors[j * 3] = r * (1 - cf) + foamR * cf;
      colors[j * 3 + 1] = gg * (1 - cf) + foamG * cf;
      colors[j * 3 + 2] = bb * (1 - cf) + foamB * cf;
    }
    posAttr.needsUpdate = true;
    normAttr.needsUpdate = true;
    colAttr.needsUpdate = true;
    geo.computeBoundingSphere();
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
