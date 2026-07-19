// scene.ts — the pad-centric 3D CONSTELLATION scene (S2B).
//
// Builds a dark-studio pad view (AgX baseline via the shared renderer) and plants
// every Monte-Carlo run as a touchdown glyph at (synthesized-angle, td_lat radius),
// with schematic descent arcs above. Owns raycast picking for hover/click and the
// A/B paired-glyph + flip-hairline overlay.
//
// Frame: three-frame, Y up, meters. The pad sits at y=0 on the XZ plane. Glyph
// placement is planar (design.glyphGroundXZ). This scene reads ONLY the classified
// run data — it never touches telemetry or the live renderer path.

import {
  Group,
  Mesh,
  CircleGeometry,
  RingGeometry,
  SphereGeometry,
  MeshStandardMaterial,
  MeshBasicMaterial,
  LineBasicMaterial,
  BufferGeometry,
  Line,
  Vector3,
  Vector2,
  Raycaster,
  Color,
  QuadraticBezierCurve3,
  GridHelper,
  DirectionalLight,
  AmbientLight,
  DoubleSide,
  AdditiveBlending,
  type Scene,
  type PerspectiveCamera,
  type Object3D,
} from "three/webgpu";
import type { ClassifiedRun } from "./runData";
import { PAD_RING_M } from "./runData";
import type { Side, Pick } from "./types";
import {
  glyphColor,
  glyphGroundXZ,
  glyphSize,
  schematicArc,
  cssHex,
} from "./design";

export type { Side, Pick } from "./types";

/** Per-glyph scene handle (kept for filter show/hide + hover highlight). */
interface GlyphHandle {
  run: ClassifiedRun;
  side: Side;
  mesh: Mesh;
  arc: Line;
  baseColor: number;
  baseScale: number;
}

export interface ConstellationScene {
  root: Group;
  /** Apply a per-bucket visibility set (filter chips). null = show all. */
  setVisibleBuckets(buckets: Set<string> | null): void;
  /** Raycast at NDC (-1..1) coords; returns the nearest glyph or null. */
  pick(ndcX: number, ndcY: number): Pick | null;
  /** Visually emphasize one glyph (hover); pass null to clear. */
  highlight(pick: Pick | null): void;
  /** Toggle schematic descent arcs. */
  setArcsVisible(v: boolean): void;
  /** Number of glyphs currently in the scene. */
  glyphCount: number;
  dispose(): void;
}

const GROUND_Y = 0;

/**
 * Build the constellation scene into `scene`. Pass B-side runs + a diff to enable
 * the A/B paired view (flip hairlines connect runs whose bucket changed).
 */
export function buildConstellationScene(
  scene: Scene,
  camera: PerspectiveCamera,
  runsA: ClassifiedRun[],
  opts: {
    runsB?: ClassifiedRun[];
    /** run numbers (A-side) whose bucket flipped in B — draw a hairline. */
    flippedRuns?: Set<number>;
    /** ground extent (m) for the grid + pad backdrop. */
    extent?: number;
  } = {}
): ConstellationScene {
  const root = new Group();
  scene.add(root);

  // Arc visibility is per-scene state, closed over by makeGlyph + the toggle.
  let arcsOn = true;

  const extent = opts.extent ?? autoExtent(runsA, opts.runsB);
  buildStudioLighting(scene);
  buildPadAndGrid(root, extent);

  const glyphs: GlyphHandle[] = [];
  const raycaster = new Raycaster();

  // ── A-side glyphs. In A/B mode, A is nudged slightly so paired glyphs don't
  //    perfectly overlap; solo mode places A exactly on its radius.
  const ab = !!opts.runsB;
  for (const run of runsA) {
    glyphs.push(makeGlyph(root, run, "A", ab ? -1 : 0));
  }
  if (opts.runsB) {
    const bByRun = new Map<number, ClassifiedRun>();
    for (const r of opts.runsB) bByRun.set(r.row.run, r);
    for (const run of opts.runsB) {
      glyphs.push(makeGlyph(root, run, "B", +1));
    }
    // flip hairlines: connect A-glyph -> B-glyph for runs whose bucket changed.
    if (opts.flippedRuns && opts.flippedRuns.size > 0) {
      const aByRun = new Map<number, ClassifiedRun>();
      for (const r of runsA) aByRun.set(r.row.run, r);
      for (const runNo of opts.flippedRuns) {
        const a = aByRun.get(runNo);
        const b = bByRun.get(runNo);
        if (!a || !b) continue;
        root.add(makeHairline(a, b));
      }
    }
  }

  // pickable set = just the glyph meshes (arcs/grid excluded)
  const pickables: Object3D[] = glyphs.map((g) => g.mesh);
  const meshToHandle = new Map<Object3D, GlyphHandle>();
  for (const g of glyphs) meshToHandle.set(g.mesh, g);

  const ndc = new Vector2();
  let highlighted: GlyphHandle | null = null;

  return {
    root,
    glyphCount: glyphs.length,

    setVisibleBuckets(buckets) {
      for (const g of glyphs) {
        const vis = buckets === null || buckets.has(g.run.bucket);
        g.mesh.visible = vis;
        g.arc.visible = vis && arcsOn;
      }
    },

    pick(ndcX, ndcY) {
      ndc.set(ndcX, ndcY);
      raycaster.setFromCamera(ndc, camera);
      const hits = raycaster.intersectObjects(pickables, false);
      for (const h of hits) {
        if (!h.object.visible) continue;
        const handle = meshToHandle.get(h.object);
        if (handle) return { run: handle.run, side: handle.side };
      }
      return null;
    },

    highlight(pick) {
      // clear previous
      if (highlighted) {
        highlighted.mesh.scale.setScalar(highlighted.baseScale);
        setEmissive(highlighted.mesh, highlighted.baseColor, 1.0);
        highlighted = null;
      }
      if (!pick) return;
      const handle = glyphs.find(
        (g) => g.run.row.run === pick.run.row.run && g.side === pick.side
      );
      if (!handle) return;
      handle.mesh.scale.setScalar(handle.baseScale * 1.6);
      setEmissive(handle.mesh, handle.baseColor, 2.6);
      highlighted = handle;
    },

    setArcsVisible(v) {
      arcsOn = v;
      for (const g of glyphs) g.arc.visible = v && g.mesh.visible;
    },

    dispose() {
      root.traverse((o) => {
        const m = o as Mesh;
        if (m.geometry) m.geometry.dispose();
        const mat = (m as Mesh).material;
        if (Array.isArray(mat)) mat.forEach((x) => x.dispose());
        else if (mat) (mat as { dispose(): void }).dispose();
      });
      scene.remove(root);
    },
  };

  // arcsOn is closed-over mutable state for the toggle.
  function makeGlyph(
    parent: Group,
    run: ClassifiedRun,
    side: Side,
    pairNudge: number
  ): GlyphHandle {
    const [gx, gz] = glyphGroundXZ(run);
    const size = glyphSize(run);
    const color = glyphColor(run);

    // A tiny nudge tangential to the radius separates paired A/B glyphs.
    const nudge = pairNudge * size * 0.9;
    const rr = Math.hypot(gx, gz) || 1;
    const tx = -gz / rr; // tangent
    const tz = gx / rr;
    const px = gx + tx * nudge;
    const pz = gz + tz * nudge;

    const geo = new SphereGeometry(1, 16, 12);
    const mat = new MeshStandardMaterial({
      color: new Color(color),
      emissive: new Color(color),
      emissiveIntensity: 1.0,
      roughness: 0.35,
      metalness: 0.0,
      // B side reads slightly translucent so the "candidate vs baseline" grammar
      // holds even without hairlines (baseline A solid, candidate B airy).
      transparent: side === "B",
      opacity: side === "B" ? 0.82 : 1.0,
    });
    const mesh = new Mesh(geo, mat);
    mesh.position.set(px, GROUND_Y + size * 0.5 + 0.15, pz);
    mesh.scale.setScalar(size);
    parent.add(mesh);

    // schematic descent arc above the glyph. We take the synthesized start/mid and
    // re-terminate the curve on the nudged glyph position so it lands on the mark
    // (the schematicArc end is the un-nudged touchdown; px/pz is where the glyph is).
    const [s, m] = schematicArc(run);
    const curve = new QuadraticBezierCurve3(
      new Vector3(s[0] + tx * nudge, s[1], s[2] + tz * nudge),
      new Vector3(m[0] + tx * nudge, m[1], m[2] + tz * nudge),
      new Vector3(px, GROUND_Y + 0.2, pz)
    );
    const pts = curve.getPoints(24);
    const arcGeo = new BufferGeometry().setFromPoints(pts);
    const arcMat = new LineBasicMaterial({
      color: new Color(color),
      transparent: true,
      opacity: side === "B" ? 0.18 : 0.28,
      blending: AdditiveBlending,
      depthWrite: false,
    });
    const arc = new Line(arcGeo, arcMat);
    arc.visible = arcsOn;
    parent.add(arc);

    return { run, side, mesh, arc, baseColor: color, baseScale: size };
  }
}

// ── scene furniture ──────────────────────────────────────────────────────────

function buildStudioLighting(scene: Scene) {
  // Dark studio: a soft key from high, cool ambient fill. The glyphs carry their
  // own emissive (bloom does the rest at integration).
  const key = new DirectionalLight(0xffffff, 1.1);
  key.position.set(60, 140, 40);
  scene.add(key);
  scene.add(new AmbientLight(0x223044, 0.55));
}

function buildPadAndGrid(root: Group, extent: number) {
  // Pad disc (Ø ~ generous; the pad itself is small, the concrete apron larger).
  const padR = 12;
  const pad = new Mesh(
    new CircleGeometry(padR, 64),
    new MeshStandardMaterial({ color: 0x30343c, roughness: 0.95, metalness: 0.0 })
  );
  pad.rotation.x = -Math.PI / 2;
  pad.position.y = GROUND_Y + 0.01;
  root.add(pad);

  // Pad center cross-hair (a thin '+').
  const cross = new Group();
  const crossMat = new LineBasicMaterial({ color: 0x6f7787, transparent: true, opacity: 0.7 });
  cross.add(lineSeg(-padR * 0.6, 0.02, 0, padR * 0.6, 0.02, 0, crossMat));
  cross.add(lineSeg(0, 0.02, -padR * 0.6, 0, 0.02, padR * 0.6, crossMat));
  root.add(cross);

  // THE 26 m on-pad ring — the load-bearing reference. Bright, cyan-ish.
  const ring = new Mesh(
    new RingGeometry(PAD_RING_M - 0.35, PAD_RING_M + 0.35, 96),
    new MeshBasicMaterial({
      color: 0x39d0ff,
      side: DoubleSide,
      transparent: true,
      opacity: 0.85,
    })
  );
  ring.rotation.x = -Math.PI / 2;
  ring.position.y = GROUND_Y + 0.03;
  root.add(ring);

  // Distance grid (faint), plus labeled reference rings every 25 m.
  const grid = new GridHelper(extent * 2, Math.max(8, Math.round(extent / 12.5)), 0x2a3444, 0x161d28);
  (grid.material as LineBasicMaterial).transparent = true;
  (grid.material as LineBasicMaterial).opacity = 0.5;
  grid.position.y = GROUND_Y;
  root.add(grid);

  // concentric range rings at 25/50/75/... m
  const ringMat = new LineBasicMaterial({ color: 0x33405a, transparent: true, opacity: 0.35 });
  for (let r = 25; r <= extent; r += 25) {
    if (Math.abs(r - PAD_RING_M) < 2) continue; // don't clutter the 26 m ring
    root.add(circleLine(r, GROUND_Y + 0.02, ringMat));
  }
}

// ── A/B flip hairline ────────────────────────────────────────────────────────

function makeHairline(a: ClassifiedRun, b: ClassifiedRun): Line {
  const [ax, az] = glyphGroundXZ(a);
  const [bx, bz] = glyphGroundXZ(b);
  const sizeA = glyphSize(a);
  const sizeB = glyphSize(b);
  const geo = new BufferGeometry().setFromPoints([
    new Vector3(ax, GROUND_Y + sizeA * 0.5 + 0.15, az),
    new Vector3(bx, GROUND_Y + sizeB * 0.5 + 0.15, bz),
  ]);
  // hairline colored by A's cause -> a subtle "it moved from here" cue.
  const mat = new LineBasicMaterial({
    color: new Color(glyphColor(a)),
    transparent: true,
    opacity: 0.6,
  });
  return new Line(geo, mat);
}

// ── low-level line helpers ───────────────────────────────────────────────────

function lineSeg(
  x0: number, y0: number, z0: number,
  x1: number, y1: number, z1: number,
  mat: LineBasicMaterial
): Line {
  const geo = new BufferGeometry().setFromPoints([
    new Vector3(x0, y0, z0),
    new Vector3(x1, y1, z1),
  ]);
  return new Line(geo, mat);
}

function circleLine(r: number, y: number, mat: LineBasicMaterial): Line {
  const pts: Vector3[] = [];
  const N = 96;
  for (let i = 0; i <= N; i++) {
    const a = (i / N) * Math.PI * 2;
    pts.push(new Vector3(Math.cos(a) * r, y, Math.sin(a) * r));
  }
  return new Line(new BufferGeometry().setFromPoints(pts), mat);
}

// ── helpers ──────────────────────────────────────────────────────────────────

function setEmissive(mesh: Mesh, color: number, intensity: number) {
  const mat = mesh.material as MeshStandardMaterial;
  if (mat.emissive) {
    mat.emissive.set(color);
    mat.emissiveIntensity = intensity;
  }
}

/** Auto ground extent: enclose the farthest glyph (+headroom), min 60 m. */
function autoExtent(a: ClassifiedRun[], b?: ClassifiedRun[]): number {
  let maxR = 60;
  const scan = (rs: ClassifiedRun[]) => {
    for (const r of rs) maxR = Math.max(maxR, r.row.td_lat * 1.25);
  };
  scan(a);
  if (b) scan(b);
  return Math.min(Math.ceil(maxR / 25) * 25, 400); // cap so a 158 m outlier doesn't blow the grid
}

/** Exposed for the DOM legend so colors stay in one place. */
export { cssHex };
