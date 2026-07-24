// documentaryScene.ts — the S1 DOCUMENTARY VIEW scene (brainstorm §2, §5 S1).
//
// Replaces the M3 uglyScene wholesale for S1: a dark-studio pad, a procedural-lite
// booster (tank + interstage + octaweb block + 4 hinged grid fins + 4 telescoping
// legs — driven by streamed fins_act / deploy_frac), the raymarched plume mounted
// under the bell (fx/plume.ts), and the diegetic markers (scene/markers.ts).
//
// AESTHETIC: dark-studio AgX + bloom baseline borrowed from Bonsai's bonsaiScene.ts
// (which itself borrowed this project's canon — canon §B.1/§B.3). AgX tone-mapping
// is already set on the WebGPU renderer (renderer.ts); here we set the dark studio
// background + the lighting rig (hemisphere fill + key + rim), and tag the plume
// emissive so the post bloom pass catches it. The full procedural booster, sky/
// atmosphere, and ground-effect dust are M7/S4 — this is the "credible documentary
// view, then STOP" target (D-011).
//
// TELEMETRY-HONEST: every moving element is keyed to a streamed field — attitude
// from quat, gimbal from gimbal_act (center bell only — canon §B.2), grid fins from
// fins_act, legs from deploy_frac, plume from throttle_act/p_chamber/p_amb/mach/
// n_eng. Nothing is animation-keyed. The booster geometry is procedural garnish on
// honest state (canon §A.0 allows visual dressing ON TOP of honest state).

import {
  Mesh,
  CylinderGeometry,
  BoxGeometry,
  PlaneGeometry,
  CircleGeometry,
  SphereGeometry,
  TorusGeometry,
  LatheGeometry,
  MeshStandardMaterial,
  MeshStandardNodeMaterial,
  MeshBasicMaterial,
  HemisphereLight,
  DirectionalLight,
  PointLight,
  Group,
  Vector3,
  Vector2,
  Quaternion,
  Color,
  Fog,
  GridHelper,
  LineBasicMaterial,
  DoubleSide,
  BackSide,
  Points,
  PointsMaterial,
  BufferGeometry,
  BufferAttribute,
} from "three/webgpu";
import {
  clamp as tslClamp,
  float as tslFloat,
  mix as tslMix,
  mx_fractal_noise_float,
  positionLocal,
  smoothstep as tslSmoothstep,
  uniform as tslUniform,
  vec3 as tslVec3,
} from "three/tsl";
import type { Scene, WebGPURenderer } from "three/webgpu";
import { installSkyAndIBL, type SkyEnv } from "./environment";
import { simToThreePosition, simToThreeQuaternion } from "../net/frame";
import type { InterpSample } from "../net/interp";
import type { HelloFrame } from "../net/events";
import {
  makePlumeUniforms,
  buildPlumeMaterial,
  updatePlumeUniforms,
  type PlumeUniforms,
} from "../fx/plume";
import { buildMarkers, type MarkersHandle } from "./markers";
import { buildTargetMarker, readTargetEst } from "./targetMarker";
import { buildSea, type SeaEnv } from "./sea";
import { buildEarth, type EarthEnv } from "./earth";
import { TLM_FLAG_SEA_ACTIVE } from "../net/decode";

// OPERATOR DOCTRINE (first light, verbatim): "it MUST always sunny and daytime by
// default." The dark studio is retired as the default (it survives only as a future
// night PRESET). Daytime palette: bright sky, fog tinted TO THE SKY (aerial
// perspective), warm sun + strong sky/ground fill so the white hull always reads.
const DAY_SKY = 0x8fc1ea;
// fog tinted to the SKY'S HORIZON tone (pale, near-white blue): distant sea then melts
// into the same band the Preetham sky shows at the horizon instead of meeting it at a seam
const DAY_FOG = 0xbdd3e2;
// FIRST-LIGHT FIX: the original 400/6000 m fog was Bonsai's DESK-SCALE preset pasted
// into a 70 km scene — everything beyond 6 km fogged to background, so the entire
// descent rendered pure black ("where is the freaking rocket"). Fog must be a subtle
// depth cue at SCENE scale, never an occluder of the flight.
const STUDIO_FOG_NEAR = 30_000;
const STUDIO_FOG_FAR = 150_000;

export interface DocumentaryScene {
  /** Rebased-by-floating-origin root (camera-relative content). */
  world: Group;
  /** The booster pivot (its .position is the sim base point, three-space). */
  boosterPivot: Group;
  markers: MarkersHandle;
  plume: PlumeUniforms;
  /** The SEA environment (ocean + droneship deck); dormant until the SEA flag. */
  sea: SeaEnv;
  /** Rebuild geometry from HELLO once it arrives (veh_len/dia/leg_span/pad_radius). */
  applyHello(h: HelloFrame): void;
  /** Fire the TEA-TEB green flash (called by the EVT scheduler). */
  triggerGreenFlash(): void;
  /** Show/hide ALL diegetic guidance markers (DEV beauty-shot switch). */
  setMarkersVisible(on: boolean): void;
  /** Per-frame update from the interpolated sample. */
  update(s: InterpSample, dtSec: number): void;
}

export function buildDocumentaryScene(scene: Scene, renderer: WebGPURenderer): DocumentaryScene {
  scene.background = new Color(DAY_SKY);
  scene.fog = new Fog(DAY_FOG, STUDIO_FOG_NEAR, STUDIO_FOG_FAR);

  const world = new Group();
  scene.add(world);

  // The LAND environment (ground + pad + markings + grid) lives in one group so the
  // SEA environment can replace it wholesale when the droneship deck is active (an
  // identity group — transform-transparent, so the land renders exactly as before).
  const landGroup = new Group();
  world.add(landGroup);

  // --- ground: DAYTIME scrubland to the horizon + concrete pad (LZ-1 grammar) --
  // (was near-black night-studio; and the 4 km disc ended mid-view — extend to the
  // fog so ground meets sky at the horizon like the real coastline footage)
  const groundMat = new MeshStandardMaterial({ color: 0x8a8577, roughness: 1.0, metalness: 0.0 });
  const ground = new Mesh(new CircleGeometry(60000, 96), groundMat);
  ground.rotation.x = -Math.PI / 2;
  ground.receiveShadow = true;
  landGroup.add(ground);

  // the landing pad disc (Ø from HELLO pad_radius; default 30 m radius circle-X)
  const padMat = new MeshStandardMaterial({ color: 0x4a4f55, roughness: 0.85, metalness: 0.05 });
  let pad = new Mesh(new CircleGeometry(30, 64), padMat);
  pad.rotation.x = -Math.PI / 2;
  pad.position.y = 0.02;
  pad.receiveShadow = true;
  landGroup.add(pad);
  // circle-X pad marking (two crossed bars) — WHITE on concrete, per LZ-1 footage
  const markMat = new MeshBasicMaterial({ color: 0xe8ecef, transparent: true, opacity: 0.9, side: DoubleSide });
  for (const rot of [Math.PI / 4, -Math.PI / 4]) {
    const bar = new Mesh(new PlaneGeometry(3, 42), markMat);
    bar.rotation.x = -Math.PI / 2;
    bar.rotation.z = rot;
    bar.position.y = 0.04;
    landGroup.add(bar);
  }

  const grid = new GridHelper(3000, 60, 0x6e6a5e, 0x7c776a);
  grid.position.y = 0.01;
  const gridMat = grid.material as LineBasicMaterial;
  gridMat.transparent = true;
  gridMat.opacity = 0.4;
  landGroup.add(grid);

  // --- lighting rig: SUNNY DAYTIME (operator doctrine — the default, not a mode).
  // Hemisphere = bright sky bounce + warm ground bounce; sun = strong warm key.
  const hemi = new HemisphereLight(0xcfe4f7, 0x8a7f6e, 1.35);
  scene.add(hemi);
  const key = new DirectionalLight(0xfff4e0, 3.0);
  key.position.set(60, 120, 40);
  key.castShadow = true;
  key.shadow.camera.near = 1;
  // The shadow frustum must hold the WHOLE droneship (110×76 m) + the booster beside it,
  // and the light FOLLOWS the vehicle in update() so the contact shadow exists at any
  // altitude (a fixed 80 m box at the origin lost the vehicle the moment it drifted).
  key.shadow.camera.far = 1200;
  key.shadow.camera.left = -150;
  key.shadow.camera.right = 150;
  key.shadow.camera.top = 150;
  key.shadow.camera.bottom = -150;
  key.shadow.camera.updateProjectionMatrix(); // the box above is dead without this
  key.shadow.mapSize.set(4096, 4096);
  key.shadow.bias = -0.0002;
  key.shadow.normalBias = 0.05;
  scene.add(key);
  scene.add(key.target);
  // sun offset for the follow-light (matches the sky's sun azimuth/elevation direction)
  const SUN_OFFSET = new Vector3(0.5, 0.55, 0.42).normalize().multiplyScalar(600);
  const rim = new DirectionalLight(0x6f9bff, 0.7);
  rim.position.set(-70, 50, -60);
  scene.add(rim);

  // --- PHOTOREAL sky + image-based lighting (real reflections on the PBR metal) ---
  const skyEnv: SkyEnv = installSkyAndIBL(scene, renderer, new Vector3(0.5, 0.55, 0.42));
  hemi.intensity = 0.4; // IBL carries ambient now
  // EXPOSE FOR THE SUBJECT: at 2.4 the sun put sunlit white paint at ~1.5 linear — ~90%
  // of sky brightness = blown white-on-white with no tonal detail (probe-verified). 1.9
  // lands the hull at ~0.4 post-exposure where AgX gives it body; the sky rolls off.
  key.intensity = 1.9;
  if (import.meta.env.DEV) {
    (globalThis as Record<string, unknown>).__sky = skyEnv;
    (globalThis as Record<string, unknown>).__lights = { key, hemi, rim };
  }

  // --- STARS: a faint field that fades IN with the darkening sky at altitude (the space
  // backdrop for the entry / high-altitude regime; invisible in bright day). Upper
  // hemisphere only (above the horizon). In the rebased `world` so it shares the
  // floating-origin frame; radius within the 200 km far plane. Opacity driven in update().
  const STAR_N = 2600;
  const STAR_R = 120000;
  const starPos = new Float32Array(STAR_N * 3);
  for (let i = 0; i < STAR_N; i++) {
    const z = Math.random(); // cos(theta) in [0,1] => above-horizon hemisphere
    const s = Math.sqrt(1 - z * z);
    const ph = Math.random() * Math.PI * 2;
    starPos[i * 3] = STAR_R * s * Math.cos(ph); // x (horizontal)
    starPos[i * 3 + 1] = STAR_R * z + 400; // y = UP in three.js (above the horizon), not z
    starPos[i * 3 + 2] = STAR_R * s * Math.sin(ph); // z (horizontal)
  }
  const starGeo = new BufferGeometry();
  starGeo.setAttribute("position", new BufferAttribute(starPos, 3));
  const starMat = new PointsMaterial({
    color: 0xdfe8ff,
    size: 1.6,
    sizeAttenuation: false, // screen-space pixels => crisp star points at any range
    transparent: true,
    opacity: 0,
    depthWrite: false,
  });
  const stars = new Points(starGeo, starMat);
  world.add(stars);

  // --- plume light (canon §B.3 "plume as light, first among equals") ----------
  // A point light at the bell whose intensity/color track throttle. At night this
  // one system carries the scene. Parented to the booster so it moves with it.
  const plumeLight = new PointLight(0xff7a2a, 0.0, 200, 2.0);
  plumeLight.castShadow = false; // scoped: shadows on the key only (perf, D-010 #5)

  // --- the booster (procedural-lite; rebuilt from HELLO dims in applyHello) ----
  const boosterPivot = new Group();
  world.add(boosterPivot);

  // materials — PBR + IBL, with the HULL as a TSL node material: white airframe paint with a
  // PROCEDURAL soot job (heavy at the octaweb, streaking up the body — the signature of a
  // flown, landed booster) computed in the shader from stretched fractal noise. Procedural
  // beats textures here twice over: no CanvasTexture sampling bug (commit 908bc53), and the
  // streaks live in tank-local space so they ride the airframe at any scale.
  // envMapIntensity LOW: the physical-sky IBL is bright in absolute terms and a white
  // hull at 0.9 reflected it into a sky-mirror blowout (probe: linear 1.4-1.9, blue).
  const hullMat = new MeshStandardNodeMaterial({ roughness: 0.45, metalness: 0.12, envMapIntensity: 0.3 });
  // HULL SOOT (TSL) — bound ONCE here. The tank length arrives later (HELLO), so it is a
  // UNIFORM the rebuild updates, never a graph rebind: mutating a compiled node graph on
  // this WebGPU backend silently drops the material to an unlit white fallback (the
  // "white column" bug). Streaky soot: bottom-heavy gradient × vertically-stretched
  // fractal noise (streaks run along the airflow), with a solid floor at the base.
  const uTankLen = tslUniform(34.8);
  {
    const pl = positionLocal;
    const y01 = pl.y.div(uTankLen).add(0.5); // 0 at tank bottom, 1 at top
    const streaks = mx_fractal_noise_float(
      tslVec3(pl.x.mul(2.1), pl.y.mul(0.11), pl.z.mul(2.1)),
      3, 2.0, 0.55
    ).mul(0.5).add(0.5);
    const grad = tslSmoothstep(0.62, 0.02, y01); // heavy at the octaweb, fading by ~60% up
    const soot = tslClamp(grad.mul(0.8).add(grad.mul(streaks).mul(0.75)), 0.0, 1.0);
    const film = tslSmoothstep(0.72, 0.99, y01).mul(streaks).mul(0.3); // re-entry scorch wash
    const paint = tslVec3(0.76, 0.775, 0.79);
    const sootCol = tslVec3(0.075, 0.078, 0.085);
    const s = tslClamp(soot.add(film), 0.0, 1.0);
    hullMat.colorNode = tslMix(paint, sootCol, s);
    hullMat.roughnessNode = tslMix(tslFloat(0.4), tslFloat(0.88), s);
    hullMat.metalnessNode = tslMix(tslFloat(0.08), tslFloat(0.03), s);
  }
  const darkMat = new MeshStandardMaterial({ color: 0x25282e, roughness: 0.5, metalness: 0.82, envMapIntensity: 1.1 }); // raw structure
  const engMat = new MeshStandardMaterial({ color: 0x35383e, roughness: 0.45, metalness: 0.9, envMapIntensity: 1.1 }); // engine plumbing/manifold
  const heatShieldMat = new MeshStandardMaterial({ color: 0x1b1d21, roughness: 0.92, metalness: 0.25 }); // sooted base plate
  // black composite interstage — carbon with a faint woven striation (TSL, low contrast)
  const interMat = new MeshStandardNodeMaterial({ roughness: 0.48, metalness: 0.3, envMapIntensity: 0.9 });
  {
    const pl = positionLocal;
    const weave = mx_fractal_noise_float(tslVec3(pl.x.mul(6.0), pl.y.mul(0.8), pl.z.mul(6.0)), 2, 2.0, 0.5)
      .mul(0.5).add(0.5);
    interMat.colorNode = tslMix(tslVec3(0.045, 0.048, 0.053), tslVec3(0.075, 0.078, 0.085), weave);
    interMat.roughnessNode = tslClamp(tslFloat(0.42).add(weave.mul(0.18)), 0.3, 0.7);
  }
  const interInnerMat = new MeshStandardMaterial({ color: 0x0a0b0e, roughness: 0.9, metalness: 0.1, side: BackSide });
  const domeMat = new MeshStandardMaterial({ color: 0x1a1c20, roughness: 0.85, metalness: 0.1 }); // insulated LOX dome (dark, inside the open interstage)
  // titanium grid fins (uncoated Ti — neutral gray with the faintest warmth, metallic)
  const finMat = new MeshStandardMaterial({ color: 0x87847d, roughness: 0.36, metalness: 0.9, side: DoubleSide, envMapIntensity: 1.1 });
  const legMat = new MeshStandardMaterial({ color: 0x15171b, roughness: 0.48, metalness: 0.35, envMapIntensity: 0.9 }); // carbon leg fairings
  const pistonMat = new MeshStandardMaterial({ color: 0x8e939b, roughness: 0.32, metalness: 0.9, envMapIntensity: 1.1 }); // telescoping boom
  // bells NEVER glow (regen-cooled, canon §B.2 encoded rule) — dark polished nozzle metal
  const bellMat = new MeshStandardMaterial({ color: 0x14161a, roughness: 0.34, metalness: 0.92, envMapIntensity: 1.2 });
  const bellInnerMat = new MeshStandardMaterial({ color: 0x050607, roughness: 0.85, metalness: 0.3, side: BackSide });

  // geometry containers we rebuild on HELLO
  const bodyGroup = new Group();
  boosterPivot.add(bodyGroup);
  const finPivots: Group[] = [];
  const legPivots: Group[] = [];
  let bellY = 0; // three-space Y of the bell exit (where the plume attaches)
  let vehLen = 47.7;
  let vehDia = 3.66;
  let legSpan = 18;
  let legStowed = Math.PI * 0.965; // leg pivot angles, set from geometry in rebuildBooster
  let legDeployed = 0.9;
  let plumeProxyLen = 30; // base raymarch-box length/width (set in rebuildBooster); the
  let plumeProxyWide = 6; // update() scales these by throttle so the plume grows/shrinks visibly

  // plume proxy (built once; re-scaled on HELLO). It is a box the RaymarchingBox
  // marches; local +Y is toward the bell, plume flows -Y (canon fx/plume.ts).
  const plume = makePlumeUniforms();
  const plumeMat = buildPlumeMaterial(plume);
  const plumeProxy = new Mesh(new BoxGeometry(1, 1, 1), plumeMat);
  boosterPivot.add(plumeProxy);
  boosterPivot.add(plumeLight);

  function rebuildBooster(): void {
    // clear
    bodyGroup.clear();
    for (const p of finPivots) boosterPivot.remove(p);
    for (const p of legPivots) boosterPivot.remove(p);
    finPivots.length = 0;
    legPivots.length = 0;

    const R = vehDia / 2;
    const octaLen = vehLen * 0.09; // engine bay
    const tankLen = vehLen * 0.73; // main tank
    const isLen = vehLen * 0.1; // interstage
    const _UP = new Vector3(0, 1, 0);
    // Built base-at-y=0, growing +Y (sim +Z up -> three +Y up: a vertical booster stands).

    // aligned-primitive helpers (A-frame legs, plumbing runs)
    const beamBetween = (a: Vector3, b: Vector3, w: number, t: number, m: MeshStandardMaterial): Mesh => {
      const dir = new Vector3().subVectors(b, a);
      const len = dir.length();
      const mesh = new Mesh(new BoxGeometry(w, len, t), m);
      mesh.position.copy(a).lerp(b, 0.5);
      mesh.quaternion.setFromUnitVectors(_UP, dir.normalize());
      mesh.castShadow = true;
      return mesh;
    };
    const tubeBetween = (a: Vector3, b: Vector3, r: number, m: MeshStandardMaterial): Mesh => {
      const dir = new Vector3().subVectors(b, a);
      const len = dir.length();
      const mesh = new Mesh(new CylinderGeometry(r, r, len, 14), m);
      mesh.position.copy(a).lerp(b, 0.5);
      mesh.quaternion.setFromUnitVectors(_UP, dir.normalize());
      mesh.castShadow = true;
      return mesh;
    };

    // the hull soot graph is bound ONCE at material construction — only feed it the size
    uTankLen.value = tankLen;

    // ===== engine bay (octaweb) + base ring + vertical ribs =====
    const octa = new Mesh(new CylinderGeometry(R * 0.99, R * 0.94, octaLen, 96, 1), darkMat);
    octa.position.y = octaLen / 2;
    octa.castShadow = true;
    bodyGroup.add(octa);
    const baseRing = new Mesh(new CylinderGeometry(R * 0.97, R * 0.97, octaLen * 0.18, 96), engMat);
    baseRing.position.y = octaLen * 0.09;
    bodyGroup.add(baseRing);
    for (let i = 0; i < 8; i++) {
      const a = (i / 8) * Math.PI * 2;
      const rib = new Mesh(new BoxGeometry(0.12, octaLen * 0.88, 0.2), engMat);
      rib.position.set(Math.cos(a) * R * 0.975, octaLen * 0.5, Math.sin(a) * R * 0.975);
      rib.rotation.y = -a;
      bodyGroup.add(rib);
    }
    // sooted heat-shield base plate (the bells hang through it)
    const shield = new Mesh(new CircleGeometry(R * 0.985, 96), heatShieldMat);
    shield.rotation.x = Math.PI / 2; // faces down
    shield.position.y = -0.01;
    bodyGroup.add(shield);

    // ===== 9 Merlin engines (1 center + 8 ring): lathe-profile bells =====
    const bellProfile = (scale: number): LatheGeometry => {
      const pts: Vector2[] = [];
      const prof: [number, number][] = [
        [0.082, 0.02], [0.076, -0.05], [0.086, -0.17], [0.105, -0.3],
        [0.128, -0.44], [0.148, -0.55], [0.157, -0.6], [0.16, -0.62],
      ];
      for (const [r, y] of prof) pts.push(new Vector2(r * R * scale, y * R * scale));
      return new LatheGeometry(pts, 40);
    };
    const engBell = (scale: number): Group => {
      const g = new Group();
      const outer = new Mesh(bellProfile(scale), bellMat);
      outer.castShadow = true;
      g.add(outer);
      const inner = new Mesh(bellProfile(scale * 0.965), bellInnerMat);
      inner.position.y = -0.005;
      g.add(inner);
      const lip = new Mesh(new TorusGeometry(R * 0.159 * scale, R * 0.011 * scale, 10, 40), bellMat);
      lip.rotation.x = Math.PI / 2;
      lip.position.y = -R * 0.62 * scale;
      g.add(lip);
      // powerhead: chamber + turbopump block + feed elbow
      const cham = new Mesh(new CylinderGeometry(R * 0.085, R * 0.115, R * 0.2, 20), engMat);
      cham.position.y = R * 0.1 * scale;
      g.add(cham);
      const tp = new Mesh(new BoxGeometry(R * 0.12, R * 0.14, R * 0.1), engMat);
      tp.position.set(R * 0.08, R * 0.16 * scale, 0);
      g.add(tp);
      return g;
    };
    const engY = -R * 0.05;
    const center = engBell(1.0);
    center.position.set(0, engY, 0);
    bodyGroup.add(center);
    for (let i = 0; i < 8; i++) {
      const a = (i / 8) * Math.PI * 2 + Math.PI / 8;
      const e = engBell(0.82);
      e.position.set(Math.cos(a) * R * 0.6, engY, Math.sin(a) * R * 0.6);
      // ring engines cant slightly outboard, like the real octaweb
      e.rotation.set(Math.sin(a) * 0.06, 0, -Math.cos(a) * 0.06);
      bodyGroup.add(e);
    }
    bellY = -R * 0.72; // center engine exit — the plume attaches here

    // ===== main tank: ONE cylinder, the TSL soot job carries the story =====
    const tank = new Mesh(new CylinderGeometry(R, R, tankLen, 96, 1), hullMat);
    tank.position.y = octaLen + tankLen / 2;
    tank.castShadow = true;
    bodyGroup.add(tank);
    // three subtle weld seams (thin, hull-toned — catch light without reading as stripes)
    for (const fy of [0.31, 0.62, 0.86]) {
      const seam = new Mesh(new CylinderGeometry(R * 1.004, R * 1.004, 0.05, 96, 1, true), engMat);
      seam.position.y = octaLen + tankLen * fy;
      bodyGroup.add(seam);
    }

    // ===== raceway conduit + cable runs =====
    const race = new Mesh(new BoxGeometry(R * 0.1, tankLen * 0.97, R * 0.18), engMat);
    race.position.set(R * 1.01, octaLen + tankLen / 2, 0);
    race.castShadow = true;
    bodyGroup.add(race);
    for (const off of [-1, 1]) {
      const line = new Mesh(new CylinderGeometry(R * 0.024, R * 0.024, tankLen * 0.93, 10), engMat);
      line.position.set(R * 1.005, octaLen + tankLen / 2, off * R * 0.14);
      bodyGroup.add(line);
    }

    // ===== interstage: OPEN black composite ring (a booster has no nose cone) =====
    const inter = new Mesh(new CylinderGeometry(R, R, isLen, 96, 1, true), interMat);
    inter.position.y = octaLen + tankLen + isLen / 2;
    inter.castShadow = true;
    bodyGroup.add(inter);
    const interInner = new Mesh(new CylinderGeometry(R * 0.985, R * 0.985, isLen * 0.98, 96, 1, true), interInnerMat);
    interInner.position.y = octaLen + tankLen + isLen / 2;
    bodyGroup.add(interInner);
    const rimTorus = new Mesh(new TorusGeometry(R * 0.993, 0.06, 10, 96), interMat);
    rimTorus.rotation.x = Math.PI / 2;
    rimTorus.position.y = octaLen + tankLen + isLen;
    bodyGroup.add(rimTorus);
    // LOX dome bulkhead visible down inside the open interstage (dark insulation)
    const bulkhead = new Mesh(new SphereGeometry(R * 0.97, 48, 20, 0, Math.PI * 2, 0, Math.PI * 0.45), domeMat);
    bulkhead.position.y = octaLen + tankLen + isLen * 0.12;
    bulkhead.scale.y = 0.5;
    bodyGroup.add(bulkhead);
    // 4 RCS pods just below the interstage joint
    for (let i = 0; i < 4; i++) {
      const a = (i / 4) * Math.PI * 2 + Math.PI / 4;
      const pod = new Group();
      pod.position.set(Math.cos(a) * R * 0.995, octaLen + tankLen * 0.965, Math.sin(a) * R * 0.995);
      pod.rotation.y = -a;
      const body = new Mesh(new BoxGeometry(0.34, 0.3, 0.62), engMat);
      pod.add(body);
      for (const nz of [-0.18, 0.18]) {
        const noz = new Mesh(new CylinderGeometry(0.055, 0.075, 0.12, 10), darkMat);
        noz.position.set(0.12, 0.2, nz);
        pod.add(noz);
      }
      bodyGroup.add(pod);
    }

    // ===== 4 titanium LATTICE grid fins (mounted on the interstage) =====
    const finTopY = octaLen + tankLen + isLen * 0.35;
    for (let i = 0; i < 4; i++) {
      const ang = (i * Math.PI) / 2;
      const pivot = new Group();
      pivot.position.set(Math.cos(ang) * R, finTopY, Math.sin(ang) * R);
      pivot.rotation.y = -ang; // face outward
      // hinge boss + actuator arm at the root
      const boss = new Mesh(new CylinderGeometry(0.22, 0.26, 0.5, 18), finMat);
      boss.rotation.z = Math.PI / 2;
      boss.position.x = R * 0.12;
      pivot.add(boss);
      const fin = new Group();
      fin.position.x = R * 0.62;
      const FW = 2.6, FH = 1.9, depth = 0.4;
      // frame (slightly thicker) + fine 7×5 lattice + corner chamfers
      const FRAME = 0.09, BAR = 0.05;
      for (const [px, py, w, h] of [
        [0, FH / 2, FW, FRAME], [0, -FH / 2, FW, FRAME], [-FW / 2, 0, FRAME, FH], [FW / 2, 0, FRAME, FH],
      ] as const) {
        const bar = new Mesh(new BoxGeometry(w, h, depth), finMat);
        bar.position.set(px, py, 0);
        bar.castShadow = true;
        fin.add(bar);
      }
      for (let k = -3; k <= 3; k++) {
        const v = new Mesh(new BoxGeometry(BAR, FH, depth * 0.92), finMat);
        v.position.x = (k / 7) * FW;
        v.castShadow = true;
        fin.add(v);
      }
      for (let k = -2; k <= 2; k++) {
        const hb = new Mesh(new BoxGeometry(FW, BAR, depth * 0.92), finMat);
        hb.position.y = (k / 5) * FH;
        hb.castShadow = true;
        fin.add(hb);
      }
      pivot.add(fin);
      boosterPivot.add(pivot);
      finPivots.push(pivot);
    }

    // ===== 4 landing legs — REAL A-frame: twin carbon blades + telescoping boom + pad ====
    // STOWED (deploy_frac 0) = folded flush up the hull; DEPLOYED (1) = the wide tripod.
    const legLen = legSpan * 0.55;
    legDeployed = Math.asin(Math.min(0.98, Math.max(0.25, (legSpan / 2 - R * 0.9) / legLen)));
    legStowed = Math.PI * 0.965;
    for (let i = 0; i < 4; i++) {
      const ang = (i * Math.PI) / 2 + Math.PI / 4;
      const pivot = new Group();
      pivot.position.set(Math.cos(ang) * R * 0.92, octaLen * 0.85, Math.sin(ang) * R * 0.92);
      pivot.rotation.y = -ang; // local +X points radially outward
      const foot = new Vector3(0.14, -legLen, 0);
      // twin A-frame blades from spread hull attachments converging on the foot
      pivot.add(beamBetween(new Vector3(0.08, 0, 0.66), foot, 0.4, 0.12, legMat));
      pivot.add(beamBetween(new Vector3(0.08, 0, -0.66), foot, 0.4, 0.12, legMat));
      // wide front fairing blade (the face you see — tapers into the pad)
      pivot.add(beamBetween(new Vector3(0.2, -legLen * 0.06, 0), new Vector3(0.16, -legLen * 0.985, 0), 0.55, 0.16, legMat));
      // telescoping boom: three nested silver stages
      pivot.add(tubeBetween(new Vector3(0.34, -legLen * 0.05, 0), new Vector3(0.28, -legLen * 0.42, 0), 0.16, pistonMat));
      pivot.add(tubeBetween(new Vector3(0.28, -legLen * 0.42, 0), new Vector3(0.2, -legLen * 0.74, 0), 0.115, pistonMat));
      pivot.add(tubeBetween(new Vector3(0.2, -legLen * 0.74, 0), new Vector3(0.15, -legLen * 0.97, 0), 0.08, pistonMat));
      // broad footpad, counter-rotated so it lands flat at full deploy
      const padG = new Group();
      padG.position.copy(foot);
      padG.rotation.z = -legDeployed;
      const pad = new Mesh(new CylinderGeometry(0.92, 1.05, 0.2, 28), legMat);
      pad.castShadow = true;
      padG.add(pad);
      const padRim = new Mesh(new TorusGeometry(0.98, 0.05, 10, 28), pistonMat);
      padRim.rotation.x = Math.PI / 2;
      padRim.position.y = 0.06;
      padG.add(padRim);
      pivot.add(padG);
      boosterPivot.add(pivot);
      legPivots.push(pivot);
    }

    // fit the plume proxy: a box ~ (2*exit) wide and ~ proxyLength tall below the
    // bell. plume.ts maps ~2 m near-field per local unit; we scale the box so the
    // marching space covers the near plume. Extend upward a bit for SRP envelope.
    const exitDia = R * 0.56;
    plume.exitDia.value = exitDia;
    plumeProxyLen = Math.max(30, vehLen * 0.8);
    plumeProxyWide = Math.max(exitDia * 8, 6);
    plumeProxy.scale.set(plumeProxyWide, plumeProxyLen, plumeProxyWide);
    // center the proxy so +Y (top of box) sits ~at the bell exit, box hangs down
    plumeProxy.position.set(0, bellY - plumeProxyLen * 0.5 + plumeProxyLen * 0.12, 0);
    plumeLight.position.set(0, bellY - 2, 0);
  }

  rebuildBooster();

  // markers (add to the rebased world so they share the floating-origin frame)
  const markers = buildMarkers(0);
  world.add(markers.root);

  // target-estimate marker (v2 §8.4 "what the rocket believes") — dormant until
  // the protocol-v4 decoder surfaces the estimate fields (readTargetEst -> null
  // on a v3 stream), then lights up with covariance ellipse + provenance color.
  const targetMarker = buildTargetMarker();
  world.add(targetMarker.root);

  // SEA environment (Target Stage-1 moving deck — core D-035..D-037): a Gerstner ocean
  // + the ASDS droneship deck, posed EXACTLY from the streamed deck_z (heave) /
  // target_est_xy (station drift) / (deck_quat later). Dormant until a TLM frame sets
  // the SEA-active flag, then it REPLACES the land group (only one environment shows).
  const sea: SeaEnv = buildSea(30);
  world.add(sea.root);

  // EARTH GLOBE: a stylized planet below (the "planet at altitude" curving to a real
  // horizon). Shown in space (high altitude), hidden at sea level where the local ground
  // owns the frame. Rebased with the world (floating-origin). Fades via the sky dayF.
  const earth: EarthEnv = buildEarth();
  world.add(earth.root);

  // green-flash decay state (EVT-pulsed uniform, decays on its own — canon §B.3)
  let greenFlash = 0;

  const _p = new Vector3();
  const _q = new Quaternion();
  const _deckP = new Vector3();
  const _lightAnchor = new Vector3();
  // ATMOSPHERE-BY-ALTITUDE endpoints: at entry (~60 km) the sky is near-black space; it
  // brightens into full day as the vehicle descends into the thick lower atmosphere.
  const _skySpace = new Color(0x03060f);
  const _skyDay = new Color(DAY_SKY);
  const _fogSpace = new Color(0x05070e);
  const _fogDay = new Color(DAY_FOG);

  return {
    world,
    boosterPivot,
    markers,
    plume,
    sea,

    applyHello(h: HelloFrame) {
      if (h.vehLen > 0) vehLen = h.vehLen;
      if (h.vehDia > 0) vehDia = h.vehDia;
      if (h.legSpan > 0) legSpan = h.legSpan;
      if (h.pcRef > 0) {
        // pc_ref is the chamber-pressure reference; plume pressureRatio uses live
        // p_chamber/p_amb from TLM, so nothing to store here beyond geometry.
      }
      // rebuild pad radius (land pad + the droneship deck bullseye). REPLACE the mesh —
      // swapping .geometry on a live mesh breaks its pipeline on this WebGPU backend
      // (renders unlit white; same family as the hull-rebind bug).
      if (h.padRadius > 0) {
        const fresh = new Mesh(new CircleGeometry(h.padRadius, 64), padMat);
        fresh.rotation.x = -Math.PI / 2;
        fresh.position.y = 0.02;
        fresh.receiveShadow = true;
        landGroup.add(fresh);
        landGroup.remove(pad);
        pad.geometry.dispose();
        pad = fresh;
        sea.setPadRadius(h.padRadius);
      }
      rebuildBooster();
    },

    triggerGreenFlash() {
      greenFlash = 1.0;
    },

    setMarkersVisible(on: boolean) {
      markers.root.visible = on;
      targetMarker.root.visible = on;
    },

    update(s: InterpSample, dtSec: number) {
      const f = s.frame;

      // --- SEA vs LAND: when the droneship deck is active (TLM flag), swap the land
      // environment for the ocean + droneship and pose the deck EXACTLY from the wire.
      // The deck's horizontal station is target_est_xy (sim world XY) and its height is
      // deck_z (sim world Z); frame.ts is the ONLY converter. deck_quat (tilt) is a
      // follow-up — the sim does not stream deck attitude yet, so the deck stays level.
      const seaOn = (f.flags & TLM_FLAG_SEA_ACTIVE) !== 0;
      landGroup.visible = !seaOn;
      sea.setActive(seaOn);
      if (seaOn) {
        simToThreePosition(f.targetEstXy[0], f.targetEstXy[1], f.deckZ, _deckP);
        sea.update({ x: _deckP.x, y: _deckP.y, z: _deckP.z }, null, f.t);
      }

      // --- ATMOSPHERE BY ALTITUDE: near-black SPACE at entry (~60 km) brightening to full
      // DAY as the vehicle descends into the thick lower atmosphere. Sky + fog colour and
      // the sky-fill light are driven by the vehicle altitude (world Z), so the operator
      // reads the height at a glance and high-altitude burns pop against a black sky.
      // PHYSICS: the sky's "day-ness" tracks REAL air density — the streamed ambient
      // pressure pAmb (Rayleigh scattering ∝ density). Full blue day at sea level
      // (pAmb ≈ 101325 Pa), thinning through the teens of km, near-black space by ~40 km.
      // Driven by the sim's own exponential atmosphere, so it's altitude-honest. The 0.4
      // power keeps the sky blue a bit longer on the way up (perceived brightness saturates).
      // 0.7 power: full blue at sea level, noticeably deeper by ~10 km, near-black by ~30 km
      // (0.4 kept it washed-bright far too high — the "milky white at altitude" bug).
      const dayF = Math.min(1, Math.pow(Math.max(0, f.pAmb) / 101325, 0.7));
      (scene.background as Color).lerpColors(_skySpace, _skyDay, dayF);
      if (scene.fog) {
        (scene.fog as Fog).color.lerpColors(_fogSpace, _fogDay, dayF);
        // Atmospheric haze belongs ONLY near the ground. At altitude the air is thin, so push
        // the WHOLE fog band far past the vehicle+globe — otherwise the 600 km globe (and
        // everything past 30 km) gets fogged to the light day-color = a white-out. Near scales
        // up hard with altitude so space reads CLEAR + DARK, not milky.
        (scene.fog as Fog).near = STUDIO_FOG_NEAR + (1 - dayF) * 1_500_000;
        (scene.fog as Fog).far = STUDIO_FOG_FAR + (1 - dayF) * 3_000_000;
      }
      // sky bounce fades toward vacuum; the sun key stays. SCALED DOWN since the IBL
      // carries the ambient now — the old 1.35 day value stomped the environment rebalance
      // every frame and blew the sunlit hull to white.
      hemi.intensity = (0.12 + 1.23 * dayF) * 0.3;
      starMat.opacity = (1 - dayF) * 0.9; // stars fade in as the sky goes to space
      // The stylized Earth globe is for the LAND/RTLS entry view. In SEA mode the ocean disc
      // is the surface, so HIDE the globe (passing dayF=1 keeps it hidden) — otherwise the
      // globe + its atmosphere peek out behind the ocean at altitude as a white haze band.
      earth.update(seaOn ? 1.0 : dayF, dtSec); // globe owns the frame at altitude (land only)
      // ...and the flat local ground owns it below — MUTUALLY EXCLUSIVE with the globe
      // (same 0.45 threshold), so the "two overlapping earths" can never happen. (SEA
      // hides the land group entirely; the globe is fine over the ocean.)
      landGroup.visible = !seaOn && dayF >= 0.62;

      // --- pose the booster: sim r/q -> three (frame.ts is the ONLY conversion) -
      simToThreePosition(s.r.x, s.r.y, s.r.z, _p);
      simToThreeQuaternion(s.q.x, s.q.y, s.q.z, s.q.w, _q);
      boosterPivot.position.copy(_p);
      boosterPivot.quaternion.copy(_q);

      // --- sun-shadow FOLLOW: keep the directional shadow frustum centered on the
      // vehicle in RENDER space (the world group carries the floating-origin offset;
      // lights live at scene root, so add it). One frame of rebase lag is invisible.
      _lightAnchor.copy(_p).add(world.position);
      key.target.position.copy(_lightAnchor);
      key.position.copy(_lightAnchor).add(SUN_OFFSET);

      // --- grid fins: hinge each by fins_act[i] (rad) --------------------------
      for (let i = 0; i < finPivots.length; i++) {
        const defl = f.finsAct[i] ?? 0;
        finPivots[i].rotation.z = defl; // deflect about the hinge
      }

      // --- legs: swing from stowed (up along hull) to deployed (out+down) ------
      // deploy_frac 0 => STOWED (~173° = strut folded up along the body, high altitude),
      // 1 => DEPLOYED (~30° = strut swung out+down into the landing tripod). +θ about the
      // pivot's local Z rotates the hanging (-Y) strut toward +X (radially outward).
      const deploy = f.deployFrac;
      const legAngle = legStowed + (legDeployed - legStowed) * deploy;
      for (const p of legPivots) p.rotation.z = legAngle;

      // --- plume: drive uniforms from throttle_act / p_chamber / p_amb / mach ---
      // C_T (thrust coefficient) is not on the wire; approximate the SRP-envelope
      // blend from mach (SRP is the entry-burn supersonic regime). Canon §B.3: SRP
      // wraps forward when burning supersonically. Use a mach-gated ct proxy that
      // rises with mach while the engine is lit — honest to the physics regime, and
      // the plume's own smoothstep(0.5,3.0,ct) turns it into the envelope amount.
      const lit = f.throttleAct > 0.01 && f.nEng > 0;
      const ctProxy = lit ? Math.min(4, f.mach * 0.6) : 0;
      updatePlumeUniforms(plume, {
        throttleAct: f.throttleAct,
        pChamber: f.pChamber,
        pAmb: f.pAmb,
        mach: f.mach,
        ct: ctProxy,
        nEng: f.nEng,
      });

      // THROTTLE MADE VISIBLE: scale the raymarch box (plume length + width) with
      // throttle so 40% reads as a short stub and 100% as a long torch — the modulation
      // is now obvious instead of on/off (density + brightness already track throttle in
      // fx/plume.ts). Off => collapse the box so nothing marches.
      const thr = lit ? f.throttleAct : 0;
      const lenScale = lit ? 0.28 + 0.72 * thr : 0.02;
      const wideScale = lit ? 0.5 + 0.5 * thr : 0.3;
      plumeProxy.scale.set(plumeProxyWide * wideScale, plumeProxyLen * lenScale, plumeProxyWide * wideScale);
      plumeProxy.position.y = bellY - plumeProxyLen * lenScale * 0.5 + plumeProxyLen * lenScale * 0.12;

      // green flash decay + push to the plume uniform
      if (greenFlash > 0) greenFlash = Math.max(0, greenFlash - dtSec * 3.0); // ~0.33 s
      plume.greenFlash.value = greenFlash;

      // plume light tracks throttle (color warms with throttle; off when unlit)
      plumeLight.intensity = lit ? 40 + 260 * f.throttleAct : 0;
      plumeLight.color.setRGB(1.0, 0.42 + 0.2 * f.throttleAct, 0.14 + 0.12 * f.throttleAct);

      // --- markers (diegetic, interpolate-never-snap via the interp frame) ------
      // v2 §4.5/§9.9: the solve-convergence reference is the TARGET ESTIMATE when
      // one is streaming (v4), else the classic fixed pad at the origin. This is
      // the renderer-side half of "null the offset to wherever the target is".
      const est = readTargetEst(f);
      targetMarker.update(est);
      markers.update({
        predImpact: f.predImpact,
        igniteH: f.igniteH,
        vehSimPos: [s.r.x, s.r.y, s.r.z],
        padCenter: est && est.valid ? est.xy : [0, 0],
        dtSec,
      });
    },
  };
}
