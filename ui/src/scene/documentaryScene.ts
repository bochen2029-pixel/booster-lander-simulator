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
  ConeGeometry,
  BoxGeometry,
  PlaneGeometry,
  CircleGeometry,
  MeshStandardMaterial,
  MeshBasicMaterial,
  HemisphereLight,
  DirectionalLight,
  PointLight,
  Group,
  Vector3,
  Quaternion,
  Color,
  Fog,
  GridHelper,
  LineBasicMaterial,
  DoubleSide,
  Points,
  PointsMaterial,
  BufferGeometry,
  BufferAttribute,
} from "three/webgpu";
import type { Scene } from "three/webgpu";
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
const DAY_FOG = 0x9fcbee;
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
  /** Per-frame update from the interpolated sample. */
  update(s: InterpSample, dtSec: number): void;
}

export function buildDocumentaryScene(scene: Scene): DocumentaryScene {
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
  const pad = new Mesh(new CircleGeometry(30, 64), padMat);
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
  key.shadow.camera.far = 500;
  key.shadow.camera.left = -80;
  key.shadow.camera.right = 80;
  key.shadow.camera.top = 80;
  key.shadow.camera.bottom = -80;
  key.shadow.mapSize.set(2048, 2048);
  scene.add(key);
  const rim = new DirectionalLight(0x6f9bff, 0.7);
  rim.position.set(-70, 50, -60);
  scene.add(rim);

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

  // materials
  const hullMat = new MeshStandardMaterial({ color: 0xbfc3c9, roughness: 0.55, metalness: 0.15 });
  const darkMat = new MeshStandardMaterial({ color: 0x2a2d33, roughness: 0.7, metalness: 0.3 });
  const finMat = new MeshStandardMaterial({ color: 0x3a3d44, roughness: 0.6, metalness: 0.4, side: DoubleSide });
  const legMat = new MeshStandardMaterial({ color: 0x1c1f24, roughness: 0.65, metalness: 0.35 });
  // bells NEVER glow (regen-cooled, canon §B.2 encoded rule) — plain dark metal
  const bellMat = new MeshStandardMaterial({ color: 0x17191d, roughness: 0.5, metalness: 0.6 });

  // geometry containers we rebuild on HELLO
  const bodyGroup = new Group();
  boosterPivot.add(bodyGroup);
  const finPivots: Group[] = [];
  const legPivots: Group[] = [];
  let bellY = 0; // three-space Y of the bell exit (where the plume attaches)
  let vehLen = 47.7;
  let vehDia = 3.66;
  let legSpan = 18;
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
    // tank body: 78% of length; interstage cone: 8%; octaweb block at base: rest.
    const tankLen = vehLen * 0.78;
    const isLen = vehLen * 0.08;
    const octaLen = vehLen * 0.14;

    // The sim reports the base-plane center; body is built with base at local y=0,
    // growing +Y. (After frame conversion, sim +Z up -> three +Y up, so a vertical
    // booster at identity attitude already stands up.)
    const octa = new Mesh(new CylinderGeometry(R * 0.98, R * 0.98, octaLen, 24), darkMat);
    octa.position.y = octaLen / 2;
    octa.castShadow = true;
    bodyGroup.add(octa);

    const tank = new Mesh(new CylinderGeometry(R, R, tankLen, 32), hullMat);
    tank.position.y = octaLen + tankLen / 2;
    tank.castShadow = true;
    bodyGroup.add(tank);

    const inter = new Mesh(new ConeGeometry(R, isLen, 32), darkMat);
    inter.position.y = octaLen + tankLen + isLen / 2;
    inter.castShadow = true;
    bodyGroup.add(inter);

    // center bell (gimbaled). Sits just below the base plane (protruding -Y).
    const bell = new Mesh(new ConeGeometry(R * 0.28, R * 0.9, 20, 1, true), bellMat);
    bell.rotation.x = Math.PI; // opening downward
    bell.position.y = -R * 0.45;
    bodyGroup.add(bell);
    bellY = -R * 0.9; // exit plane (plume attaches here)

    // 4 grid fins near the top of the tank, hinged by fins_act (canon §B.2)
    const finTopY = octaLen + tankLen * 0.92;
    for (let i = 0; i < 4; i++) {
      const ang = (i * Math.PI) / 2;
      const pivot = new Group();
      pivot.position.set(Math.cos(ang) * R, finTopY, Math.sin(ang) * R);
      pivot.rotation.y = -ang; // face outward
      const fin = new Mesh(new BoxGeometry(0.15, 1.6, 2.2), finMat);
      fin.position.x = 0.9; // stand off the hull
      fin.castShadow = true;
      pivot.add(fin);
      boosterPivot.add(pivot);
      finPivots.push(pivot);
    }

    // 4 landing legs, hinged at the base rim (canon §B.2). STOWED (deploy_frac 0, high
    // altitude) = folded UP along the hull; DEPLOYED (deploy_frac 1, landing) = swung
    // out+down into the landing tripod. The pivot faces radially outward (rotation.y=-ang),
    // so a rotation about its local Z swings the strut in the radial–vertical plane; the
    // deploy_frac -> angle mapping lives in update() (was inverted — legs read extended at
    // altitude, retracting on touchdown; now correct).
    const legLen = legSpan * 0.55;
    for (let i = 0; i < 4; i++) {
      const ang = (i * Math.PI) / 2 + Math.PI / 4;
      const pivot = new Group();
      pivot.position.set(Math.cos(ang) * R * 0.92, octaLen * 0.2, Math.sin(ang) * R * 0.92);
      pivot.rotation.y = -ang; // local +X points radially outward
      // strut: cylinder along local Y, TOP at the hinge, hanging DOWN (-Y) at rest
      const strut = new Mesh(new CylinderGeometry(0.16, 0.11, legLen, 8), legMat);
      strut.position.y = -legLen / 2;
      strut.castShadow = true;
      pivot.add(strut);
      // foot pad at the strut's far end
      const foot = new Mesh(new CylinderGeometry(0.6, 0.6, 0.16, 12), legMat);
      foot.position.y = -legLen;
      foot.castShadow = true;
      pivot.add(foot);
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
      // rebuild pad radius (land pad + the droneship deck bullseye)
      if (h.padRadius > 0) {
        pad.geometry.dispose();
        pad.geometry = new CircleGeometry(h.padRadius, 64);
        sea.setPadRadius(h.padRadius);
      }
      rebuildBooster();
    },

    triggerGreenFlash() {
      greenFlash = 1.0;
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
      hemi.intensity = 0.12 + 1.23 * dayF; // sky bounce fades toward vacuum; the sun key stays
      starMat.opacity = (1 - dayF) * 0.9; // stars fade in as the sky goes to space
      earth.update(dayF, dtSec); // globe owns the frame at altitude (dayF<0.45)
      // ...and the flat local ground owns it below — MUTUALLY EXCLUSIVE with the globe
      // (same 0.45 threshold), so the "two overlapping earths" can never happen. (SEA
      // hides the land group entirely; the globe is fine over the ocean.)
      landGroup.visible = !seaOn && dayF >= 0.62;

      // --- pose the booster: sim r/q -> three (frame.ts is the ONLY conversion) -
      simToThreePosition(s.r.x, s.r.y, s.r.z, _p);
      simToThreeQuaternion(s.q.x, s.q.y, s.q.z, s.q.w, _q);
      boosterPivot.position.copy(_p);
      boosterPivot.quaternion.copy(_q);

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
      const LEG_STOWED = Math.PI * 0.96;
      const LEG_DEPLOYED = 0.52;
      const legAngle = LEG_STOWED + (LEG_DEPLOYED - LEG_STOWED) * deploy;
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
