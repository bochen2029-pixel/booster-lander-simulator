/* KESTREL-9 — procedural Falcon-9-class booster model.
 *
 * Drop-in for ui/src/scene/. THREE is injected so this file has zero imports and
 * works identically under WebGPURenderer (r185 auto-transpiles standard materials
 * to TSL) and the WebGL2 fallback. No binary assets: every texture is generated
 * on a canvas at build time from the §5 dimensions.
 *
 *   import * as THREE from 'three';
 *   import { createKestrel9 } from './kestrel9.js';
 *   const booster = createKestrel9(THREE, hello.vehicle);   // HELLO geometry block
 *   scene.add(booster.group);
 *   ...
 *   booster.setTelemetry(tlm);   // called per decoded TLM packet
 *
 * Local frame == sim BODY frame mapped through §10.7:
 *   +Y  = vehicle axis toward the interstage        (body +Z)
 *   +X  = leg 0 azimuth                             (body +X)
 *   -Z  = leg 1 azimuth (body +Y, i.e. body azimuth 90 deg)
 *   origin = base-plane centre / engine gimbal plane
 * So the renderer only has to write group.position / group.quaternion from the
 * single canonical conversion; nothing in here needs to know about sim Z-up.
 */

export const VEHICLE = {
  radius: 1.83,          // §5.1  D = 3.66 m
  barrel: 41.2,          // §5.1  stage cylinder length
  interstage: 6.5,       // §5.1  open cylinder
  finAz: [45, 135, 225, 315],   // §5.4 deg
  legAz: [0, 90, 180, 270],     // §5.6 deg
  legSpan: 9.0,          // §5.6  deployed footprint 18 m diameter
  finPanel: [1.2, 2.0],  // §5.4  m (radial span x axial height)
  rcsY: 40.5,            // §5.5
  engineRing: 1.28,      // octaweb ring radius
  bellExit: 0.46,
  bellLength: 1.62,
  blackBand: 3.4,        // engine-section paint break above the base plane
};

/* ------------------------------------------------------------------ texture kit */

const TAU = Math.PI * 2;
const rnd = (s => () => (s = (s * 1664525 + 1013904223) >>> 0) / 4294967296)(20260911);

function cv(w, h) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  return c;
}

function tex(THREE, canvas, { srgb = false, repeatX = 1, repeatY = 1, aniso = 8 } = {}) {
  const t = new THREE.CanvasTexture(canvas);
  t.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(repeatX, repeatY);
  t.anisotropy = aniso;
  return t;
}

/** Sobel height -> tangent-space normal map (wraps in U so the barrel seam is invisible). */
function normalFrom(THREE, heightCanvas, strength = 2.2) {
  const w = heightCanvas.width, h = heightCanvas.height;
  const src = heightCanvas.getContext('2d').getImageData(0, 0, w, h).data;
  const out = new Uint8Array(w * h * 4);
  const at = (x, y) => src[((((y % h) + h) % h) * w + (((x % w) + w) % w)) * 4];
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const dx = (at(x + 1, y) - at(x - 1, y)) / 255 * strength;
      const dy = (at(x, y + 1) - at(x, y - 1)) / 255 * strength;
      const nx = -dx, ny = -dy, nz = 1, l = Math.hypot(nx, ny, nz), i = (y * w + x) * 4;
      out[i] = (nx / l * 0.5 + 0.5) * 255;
      out[i + 1] = (ny / l * 0.5 + 0.5) * 255;
      out[i + 2] = (nz / l * 0.5 + 0.5) * 255;
      out[i + 3] = 255;
    }
  }
  const t = new THREE.DataTexture(out, w, h);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.needsUpdate = true;
  return t;
}

function grain(ctx, w, h, amount, scale) {
  for (let i = 0; i < amount; i++) {
    const r = scale * (0.4 + rnd() * 2.4);
    const g = ctx.createRadialGradient(rnd() * w, rnd() * h, 0, 0, 0, r);
    ctx.globalAlpha = 0.05 + rnd() * 0.06;
    g.addColorStop(0, rnd() > 0.5 ? '#ffffff' : '#000000');
    g.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = g;
    ctx.beginPath();
    const x = rnd() * w, y = rnd() * h;
    ctx.arc(x, y, r, 0, TAU); ctx.fill();
  }
  ctx.globalAlpha = 1;
}

/* The barrel set: 1024 (around, u) x 2048 (along axis, v=0 base -> v=1 top).
 * Everything is authored in metres and mapped so panel joints land on real
 * section boundaries. Three canvases: albedo, roughness, height(->normal). */
function barrelMaps(THREE, V) {
  const W = 1024, H = 2048, L = V.barrel, m2v = y => H - (y / L) * H;
  const ca = cv(W, H), cr = cv(W, H), ch = cv(W, H);
  const a = ca.getContext('2d'), r = cr.getContext('2d'), d = ch.getContext('2d');

  a.fillStyle = '#d9dce1'; a.fillRect(0, 0, W, H);
  r.fillStyle = '#8a8a8a'; r.fillRect(0, 0, W, H);   // rough 0.54 paint
  d.fillStyle = '#808080'; d.fillRect(0, 0, W, H);

  // large-scale paint mottle + cure discolouration
  for (let i = 0; i < 90; i++) {
    const x = rnd() * W, y = rnd() * H, rr = 60 + rnd() * 420;
    const g = a.createRadialGradient(x, y, 0, x, y, rr);
    g.addColorStop(0, rnd() > 0.5 ? 'rgba(255,255,255,0.10)' : 'rgba(120,126,138,0.10)');
    g.addColorStop(1, 'rgba(0,0,0,0)');
    a.fillStyle = g; a.beginPath(); a.arc(x, y, rr, 0, TAU); a.fill();
  }

  // black engine-section band (paint break at 3.4 m) with a weathered edge
  const bandTop = m2v(V.blackBand);
  a.fillStyle = '#15171b'; a.fillRect(0, bandTop, W, H - bandTop);
  r.fillStyle = '#9a9a9a'; r.fillRect(0, bandTop, W, H - bandTop);
  for (let i = 0; i < 300; i++) {
    const x = rnd() * W, w2 = 6 + rnd() * 34, hh = rnd() * 26;
    a.fillStyle = 'rgba(21,23,27,' + (0.35 + rnd() * 0.5) + ')';
    a.fillRect(x, bandTop - hh, w2, hh + 2);
  }

  // circumferential barrel-section joints ~1.53 m, with rivet rows
  for (let y = 1.53; y < L; y += 1.53) {
    const v = m2v(y);
    a.fillStyle = 'rgba(96,101,112,0.55)'; a.fillRect(0, v - 1, W, 2.5);
    a.fillStyle = 'rgba(255,255,255,0.32)'; a.fillRect(0, v + 2, W, 1.2);
    r.fillStyle = 'rgba(190,190,190,0.75)'; r.fillRect(0, v - 2, W, 5);
    d.fillStyle = 'rgba(40,40,40,0.85)'; d.fillRect(0, v - 1, W, 2.5);
    for (let x = 0; x < W; x += 7) {
      d.fillStyle = 'rgba(210,210,210,0.55)';
      d.beginPath(); d.arc(x + 3, v + 6, 1.5, 0, TAU); d.fill();
    }
  }
  // longitudinal roll seams
  for (const u of [0.02, 0.35, 0.68]) {
    const x = u * W;
    a.fillStyle = 'rgba(104,110,122,0.42)'; a.fillRect(x, 0, 2, H);
    d.fillStyle = 'rgba(46,46,46,0.8)'; d.fillRect(x, 0, 2, H);
  }

  // access panels / hatches
  for (let i = 0; i < 22; i++) {
    const x = rnd() * W, y = m2v(4 + rnd() * (L - 8)), w2 = 26 + rnd() * 40, h2 = 20 + rnd() * 34;
    a.fillStyle = 'rgba(204,208,216,0.8)'; a.fillRect(x, y, w2, h2);
    a.strokeStyle = 'rgba(108,114,126,0.7)'; a.lineWidth = 1.6; a.strokeRect(x, y, w2, h2);
    r.fillStyle = 'rgba(150,150,150,0.6)'; r.fillRect(x, y, w2, h2);
    d.fillStyle = 'rgba(58,58,58,0.7)'; d.strokeStyle = 'rgba(58,58,58,0.9)';
    d.lineWidth = 2; d.strokeRect(x, y, w2, h2);
  }

  // vent bosses and small hardware
  for (let i = 0; i < 40; i++) {
    const x = rnd() * W, y = m2v(2 + rnd() * (L - 4)), rr = 3 + rnd() * 6;
    a.fillStyle = 'rgba(88,94,104,0.55)'; a.beginPath(); a.arc(x, y, rr, 0, TAU); a.fill();
    d.fillStyle = '#4a4a4a'; d.beginPath(); d.arc(x, y, rr, 0, TAU); d.fill();
  }

  // weather streaking, strongest just above the band
  for (let i = 0; i < 260; i++) {
    const x = rnd() * W, y0 = m2v(V.blackBand + rnd() * 16), len = 30 + rnd() * 260, w2 = 1 + rnd() * 5;
    const g = a.createLinearGradient(0, y0 - len, 0, y0);
    g.addColorStop(0, 'rgba(120,126,138,0)');
    g.addColorStop(1, 'rgba(88,94,106,' + (0.05 + rnd() * 0.13) + ')');
    a.fillStyle = g; a.fillRect(x, y0 - len, w2, len);
  }

  // markings — generic vehicle id, no logos
  a.save();
  a.translate(W * 0.5, m2v(30));
  a.rotate(-Math.PI / 2);
  a.fillStyle = '#20242c';
  a.font = '700 62px ui-sans-serif, Helvetica, Arial';
  a.letterSpacing = '10px';
  a.fillText('KESTREL 9', 0, 0);
  a.font = '600 26px ui-monospace, Menlo, monospace';
  a.fillStyle = '#4a505c';
  a.fillText('K9-013   BLOCK 5', 0, 42);
  a.restore();
  a.save();
  a.translate(W * 0.5 + 260, m2v(12));
  a.rotate(-Math.PI / 2);
  a.fillStyle = '#3a4048';
  a.font = '600 20px ui-monospace, Menlo, monospace';
  a.fillText('NO STEP', 0, 0);
  a.restore();

  grain(a, W, H, 400, 12);
  grain(r, W, H, 500, 16);

  return {
    map: tex(THREE, ca, { srgb: true }),
    roughnessMap: tex(THREE, cr),
    normalMap: normalFrom(THREE, ch, 2.6),
  };
}

/* Soot overlay alpha: heaviest at the base, ragged top edge, four clean
 * leg-shadow stripes (the real post-entry pattern, §11.5) and one dark
 * gas-generator streak. u=0 -> +Z, 0.25 -> -X, 0.5 -> -Z, 0.75 -> +X == leg azimuths. */
function sootMap(THREE, V) {
  const W = 1024, H = 1024, c = cv(W, H), x2 = c.getContext('2d');
  const g = x2.createLinearGradient(0, H, 0, 0);
  g.addColorStop(0.00, 'rgba(16,16,18,0.96)');
  g.addColorStop(0.16, 'rgba(18,18,20,0.80)');
  g.addColorStop(0.45, 'rgba(22,22,24,0.42)');
  g.addColorStop(0.78, 'rgba(26,26,28,0.14)');
  g.addColorStop(1.00, 'rgba(26,26,28,0.00)');
  x2.fillStyle = g; x2.fillRect(0, 0, W, H);

  // ragged blotches so the soot line is not a clean gradient
  for (let i = 0; i < 240; i++) {
    const x = rnd() * W, y = H - Math.pow(rnd(), 1.5) * H * 0.85, rr = 12 + rnd() * 90;
    const rg = x2.createRadialGradient(x, y, 0, x, y, rr);
    rg.addColorStop(0, 'rgba(14,14,16,' + (0.10 + rnd() * 0.3) + ')');
    rg.addColorStop(1, 'rgba(14,14,16,0)');
    x2.fillStyle = rg; x2.beginPath(); x2.arc(x, y, rr, 0, TAU); x2.fill();
  }
  // gas-generator streak
  const sg = x2.createLinearGradient(0, H, 0, H * 0.35);
  sg.addColorStop(0, 'rgba(8,8,9,0.95)');
  sg.addColorStop(1, 'rgba(8,8,9,0)');
  x2.fillStyle = sg; x2.fillRect(W * 0.6, H * 0.35, 34, H * 0.65);

  // leg-shadow stripes: erase soot where the stowed legs masked the body
  x2.globalCompositeOperation = 'destination-out';
  for (const u of [0, 0.25, 0.5, 0.75]) {
    const x = u * W, w2 = 52;
    for (const xx of [x - w2 / 2, x - w2 / 2 + W]) {
      const eg = x2.createLinearGradient(xx, 0, xx + w2, 0);
      eg.addColorStop(0, 'rgba(0,0,0,0)');
      eg.addColorStop(0.3, 'rgba(0,0,0,0.92)');
      eg.addColorStop(0.7, 'rgba(0,0,0,0.92)');
      eg.addColorStop(1, 'rgba(0,0,0,0)');
      x2.fillStyle = eg; x2.fillRect(xx, H * 0.12, w2, H * 0.8);
    }
  }
  x2.globalCompositeOperation = 'source-over';
  return tex(THREE, c, { srgb: true });
}

function frostMap(THREE) {
  const W = 512, H = 1024, c = cv(W, H), x2 = c.getContext('2d');
  x2.clearRect(0, 0, W, H);
  for (let i = 0; i < 900; i++) {
    const x = rnd() * W, y = Math.pow(rnd(), 0.8) * H, rr = 4 + rnd() * 46;
    const g = x2.createRadialGradient(x, y, 0, x, y, rr);
    const a0 = 0.05 + rnd() * 0.5;
    g.addColorStop(0, 'rgba(236,244,255,' + a0 + ')');
    g.addColorStop(1, 'rgba(236,244,255,0)');
    x2.fillStyle = g; x2.beginPath(); x2.arc(x, y, rr, 0, TAU); x2.fill();
  }
  return tex(THREE, c, { srgb: true });
}

function metalMaps(THREE, base, rough, scratch) {
  const W = 512, H = 512, ca = cv(W, H), cr = cv(W, H);
  const a = ca.getContext('2d'), r = cr.getContext('2d');
  a.fillStyle = base; a.fillRect(0, 0, W, H);
  const rv = Math.round(rough * 255), rd = Math.max(0, rv - 60);
  r.fillStyle = `rgb(${rv},${rv},${rv})`; r.fillRect(0, 0, W, H);
  for (let i = 0; i < scratch; i++) {
    const x = rnd() * W, y = rnd() * H, l = 10 + rnd() * 150, ang = rnd() * TAU;
    a.strokeStyle = 'rgba(255,255,255,' + (0.03 + rnd() * 0.07) + ')';
    r.strokeStyle = 'rgba(' + rd + ',' + rd + ',' + rd + ',0.5)';
    a.lineWidth = r.lineWidth = 0.6 + rnd() * 1.4;
    a.beginPath(); a.moveTo(x, y); a.lineTo(x + Math.cos(ang) * l, y + Math.sin(ang) * l); a.stroke();
    r.beginPath(); r.moveTo(x, y); r.lineTo(x + Math.cos(ang) * l, y + Math.sin(ang) * l); r.stroke();
  }
  grain(a, W, H, 260, 9);
  grain(r, W, H, 300, 11);
  return { map: tex(THREE, ca, { srgb: true, repeatX: 2, repeatY: 2 }), roughnessMap: tex(THREE, cr, { repeatX: 2, repeatY: 2 }) };
}

/** Nozzle: cold inconel/straw at the throat -> soot black at the exit lip. */
function nozzleMaps(THREE) {
  const W = 64, H = 512, ca = cv(W, H), cr = cv(W, H);
  const a = ca.getContext('2d'), r = cr.getContext('2d');
  const g = a.createLinearGradient(0, 0, 0, H);
  g.addColorStop(0.00, '#c3b7a4');
  g.addColorStop(0.18, '#cdae7e');
  g.addColorStop(0.42, '#95897c');
  g.addColorStop(0.72, '#645c53');
  g.addColorStop(1.00, '#3e3934');
  a.fillStyle = g; a.fillRect(0, 0, W, H);
  const g2 = r.createLinearGradient(0, 0, 0, H);
  g2.addColorStop(0, '#4e4e4e'); g2.addColorStop(0.5, '#666666'); g2.addColorStop(1, '#8e8e8e');
  r.fillStyle = g2; r.fillRect(0, 0, W, H);
  for (let i = 0; i < 120; i++) {
    const y = rnd() * H;
    a.fillStyle = 'rgba(255,255,255,0.09)'; a.fillRect(0, y, W, 1 + rnd() * 2);
  }
  grain(a, W, H, 160, 6);
  return { map: tex(THREE, ca, { srgb: true }), roughnessMap: tex(THREE, cr) };
}

/* ------------------------------------------------------------------ geometry kit */

/** Lathe with an angular ripple — reads as a regeneratively-cooled tube bundle. */
function rippledLathe(THREE, pts, seg, tubes, amp) {
  const g = new THREE.LatheGeometry(pts, seg);
  const p = g.attributes.position;
  for (let i = 0; i < p.count; i++) {
    const x = p.getX(i), z = p.getZ(i), r = Math.hypot(x, z);
    if (r < 1e-4) continue;
    const th = Math.atan2(z, x);
    const k = 1 + amp * Math.cos(th * tubes);
    p.setX(i, Math.cos(th) * r * k);
    p.setZ(i, Math.sin(th) * r * k);
  }
  g.computeVertexNormals();
  return g;
}

function bellProfileUnused() { /* profile is built inside createKestrel9 where THREE is bound */ }

/* ------------------------------------------------------------------ the model */

export function createKestrel9(THREE, opts = {}) {
  const V = Object.assign({}, VEHICLE, opts.vehicle || opts);
  const quality = opts.quality || 'ultra';
  const seg = quality === 'low' ? 48 : quality === 'medium' ? 96 : 160;
  const R = V.radius;
  const disposables = [];
  const keep = o => (disposables.push(o), o);

  /* ---- materials ---------------------------------------------------------- */
  const bm = barrelMaps(THREE, V);
  const oct = metalMaps(THREE, '#8f9299', 0.44, 700);
  const ti = metalMaps(THREE, '#a9a29a', 0.38, 520);
  const noz = nozzleMaps(THREE);

  const materials = {
    bodyPaint: new THREE.MeshStandardMaterial({
      name: 'K9_BodyPaint', ...bm, metalness: 0.1, roughness: 1.0,
      normalScale: new THREE.Vector2(0.8, 0.8), envMapIntensity: 0.72,
    }),
    bodySoot: new THREE.MeshBasicMaterial({
      name: 'K9_Soot', map: sootMap(THREE, V), transparent: true, opacity: 0,
      depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2,
    }),
    frost: new THREE.MeshStandardMaterial({
      name: 'K9_Frost', map: frostMap(THREE), transparent: true, opacity: 0.0,
      roughness: 0.92, metalness: 0, depthWrite: false,
      polygonOffset: true, polygonOffsetFactor: -3,
    }),
    interstage: new THREE.MeshStandardMaterial({
      name: 'K9_Interstage', color: 0x1a1c20, metalness: 0.28, roughness: 0.52,
      roughnessMap: oct.roughnessMap, envMapIntensity: 0.9,
    }),
    interstageInner: new THREE.MeshStandardMaterial({
      name: 'K9_InterstageInner', color: 0x111316, metalness: 0.3, roughness: 0.62,
      side: THREE.BackSide,
    }),
    octaweb: new THREE.MeshStandardMaterial({
      name: 'K9_Octaweb', ...oct, color: 0x8b8f95, metalness: 0.9, roughness: 1.0, envMapIntensity: 1.4,
    }),
    heatShield: new THREE.MeshStandardMaterial({
      name: 'K9_HeatShield', color: 0x34373b, metalness: 0.55, roughness: 0.62,
    }),
    nozzle: new THREE.MeshStandardMaterial({
      name: 'K9_Nozzle', ...noz, metalness: 0.62, roughness: 0.88, envMapIntensity: 2.0,
      emissive: 0x330800, emissiveIntensity: 0,
    }),
    nozzleInner: new THREE.MeshStandardMaterial({
      name: 'K9_NozzleInner', color: 0x0c0c0d, metalness: 0.55, roughness: 0.5,
      side: THREE.BackSide, emissive: 0xff8a2a, emissiveIntensity: 0,
    }),
    titanium: new THREE.MeshStandardMaterial({
      name: 'K9_Titanium', ...ti, color: 0x8d887f, metalness: 1.0, roughness: 1.0, envMapIntensity: 0.95,
    }),
    legTube: new THREE.MeshStandardMaterial({
      name: 'K9_LegTube', color: 0x2c2f34, metalness: 0.35, roughness: 0.5,
      roughnessMap: ti.roughnessMap, envMapIntensity: 0.9,
    }),
    legPiston: new THREE.MeshStandardMaterial({
      name: 'K9_LegPiston', color: 0xb9bec6, metalness: 0.9, roughness: 0.28,
      roughnessMap: ti.roughnessMap, envMapIntensity: 1.3,
    }),
    jointRing: new THREE.MeshStandardMaterial({
      name: 'K9_JointRing', color: 0x8f949b, metalness: 0.3, roughness: 0.64, envMapIntensity: 0.75,
    }),
    legStruct: new THREE.MeshStandardMaterial({
      name: 'K9_LegStruct', color: 0x24262a, metalness: 0.7, roughness: 0.5,
    }),
    rcs: new THREE.MeshStandardMaterial({
      name: 'K9_RCS', color: 0x9aa0a8, metalness: 0.9, roughness: 0.35,
    }),
    raceway: new THREE.MeshStandardMaterial({
      name: 'K9_Raceway', color: 0xc6cad1, metalness: 0.25, roughness: 0.58,
      roughnessMap: bm.roughnessMap, envMapIntensity: 0.85,
    }),
  };
  Object.values(materials).forEach(keep);

  const group = new THREE.Group();
  group.name = 'KESTREL9';
  const parts = { fins: [], legs: [], engines: [], rcsPods: [], puffs: [] };

  const addMesh = (geo, mat, name, parent = group) => {
    keep(geo);
    const m = new THREE.Mesh(geo, mat);
    m.name = name; m.castShadow = true; m.receiveShadow = true;
    parent.add(m);
    return m;
  };

  /* ---- tank barrel -------------------------------------------------------- */
  parts.barrel = addMesh(
    new THREE.CylinderGeometry(R, R, V.barrel, seg, 1, true),
    materials.bodyPaint, 'Barrel');
  parts.barrel.position.y = V.barrel / 2;

  parts.soot = addMesh(
    new THREE.CylinderGeometry(R * 1.002, R * 1.002, V.barrel, seg, 1, true),
    materials.bodySoot, 'SootOverlay');
  parts.soot.position.y = V.barrel / 2;
  parts.soot.castShadow = false; parts.soot.receiveShadow = false;

  parts.frost = addMesh(
    new THREE.CylinderGeometry(R * 1.004, R * 1.004, V.barrel - 8, seg, 1, true),
    materials.frost, 'FrostOverlay');
  parts.frost.position.y = 8 + (V.barrel - 8) / 2;
  parts.frost.castShadow = false;

  // section joint rings — silhouette detail at close range (skip the black band)
  const ringGeo = new THREE.CylinderGeometry(R * 1.0035, R * 1.0035, 0.07, seg, 1, true);
  const ringYs = [];
  for (let y = 1.53; y < V.barrel - 0.4; y += 1.53) if (y > V.blackBand + 0.6) ringYs.push(y);
  const rings = new THREE.InstancedMesh(keep(ringGeo), materials.jointRing, ringYs.length);
  rings.name = 'SectionJoints'; rings.castShadow = true;
  const mtx = new THREE.Matrix4();
  ringYs.forEach((y, i) => rings.setMatrixAt(i, mtx.makeTranslation(0, y, 0)));
  group.add(rings);

  /* ---- external raceway (the long dorsal conduit) ------------------------- */
  const race = new THREE.Group();
  race.name = 'Raceway';
  race.rotation.y = THREE.MathUtils.degToRad(-22.5);
  const raceLen = V.barrel - 3.0;
  const shell = addMesh(
    new THREE.CylinderGeometry(R + 0.17, R + 0.17, raceLen, 24, 1, true, -0.135, 0.27),
    materials.raceway, 'RacewayShell', race);
  shell.position.y = 2.6 + raceLen / 2;
  for (const s of [-1, 1]) {
    const w = addMesh(new THREE.PlaneGeometry(0.17, raceLen), materials.raceway, 'RacewayWall', race);
    w.position.set(Math.sin(s * 0.135) * (R + 0.085), 2.6 + raceLen / 2, Math.cos(s * 0.135) * (R + 0.085));
    w.rotation.y = s * 0.135 + (s > 0 ? Math.PI / 2 : -Math.PI / 2);
  }
  const cap = addMesh(new THREE.SphereGeometry(0.19, 16, 12, 0, TAU, 0, Math.PI / 2),
    materials.raceway, 'RacewayCap', race);
  cap.position.set(0, 2.6 + raceLen, R + 0.05);
  group.add(race);
  parts.raceway = race;

  /* ---- interstage --------------------------------------------------------- */
  const isG = new THREE.Group(); isG.name = 'Interstage'; group.add(isG);
  const isOuter = addMesh(new THREE.CylinderGeometry(R, R, V.interstage, seg, 1, true),
    materials.interstage, 'InterstageOuter', isG);
  isOuter.position.y = V.barrel + V.interstage / 2;
  const isInner = addMesh(new THREE.CylinderGeometry(R - 0.05, R - 0.05, V.interstage, seg, 1, true),
    materials.interstageInner, 'InterstageInner', isG);
  isInner.position.y = V.barrel + V.interstage / 2;
  const isRim = addMesh(new THREE.RingGeometry(R - 0.05, R, seg, 1), materials.octaweb, 'InterstageRim', isG);
  isRim.position.y = V.barrel + V.interstage;
  isRim.rotation.x = -Math.PI / 2;
  const isBand = addMesh(new THREE.CylinderGeometry(R * 1.01, R * 1.01, 0.16, seg, 1, true),
    materials.octaweb, 'FinRootRing', isG);
  isBand.position.y = V.barrel + 0.1;
  parts.interstage = isG;

  /* ---- grid fins (real lattice, hinged on a radial axis) ------------------ */
  const [finSpan, finHeight] = V.finPanel;
  const cellsR = 5, cellsA = 8, wallT = 0.014, finDepth = 0.13;
  const wallRad = keep(new THREE.BoxGeometry(wallT, finHeight, finDepth));
  const wallAx = keep(new THREE.BoxGeometry(finSpan, wallT, finDepth));

  V.finAz.forEach((azDeg, i) => {
    const pivot = new THREE.Group();
    pivot.name = 'GridFinPivot' + i;
    pivot.rotation.y = THREE.MathUtils.degToRad(-azDeg);
    group.add(pivot);

    const hinge = new THREE.Group();
    hinge.name = 'GridFin' + i;
    hinge.position.set(R - 0.02, V.barrel - 0.35, 0);
    pivot.add(hinge);

    // root fairing + actuator barrel
    const fair = addMesh(new THREE.BoxGeometry(0.5, 0.9, 0.62), materials.octaweb, 'FinRoot' + i, hinge);
    fair.position.set(0.12, 0, 0);
    const act = addMesh(new THREE.CylinderGeometry(0.11, 0.11, 0.7, 20), materials.octaweb, 'FinHinge' + i, hinge);
    act.rotation.z = Math.PI / 2; act.position.set(0.3, 0, 0);

    const panel = new THREE.Group();
    panel.name = 'FinPanel' + i;
    panel.position.set(0.5 + finSpan / 2, 0.15, 0);
    hinge.add(panel);

    const rimT = 0.05;
    for (const [w, h, x, y] of [
      [finSpan + 0.06, rimT, 0, finHeight / 2], [finSpan + 0.06, rimT, 0, -finHeight / 2],
      [rimT, finHeight, -finSpan / 2, 0], [rimT, finHeight, finSpan / 2, 0]]) {
      const b = addMesh(new THREE.BoxGeometry(w, h, finDepth + 0.045), materials.titanium, 'FinRim' + i, panel);
      b.position.set(x, y, 0);
    }
    const ir = new THREE.InstancedMesh(wallRad, materials.titanium, cellsR - 1);
    const ia = new THREE.InstancedMesh(wallAx, materials.titanium, cellsA - 1);
    ir.name = 'FinLatticeR' + i; ia.name = 'FinLatticeA' + i;
    ir.castShadow = ia.castShadow = true;
    for (let k = 1; k < cellsR; k++) ir.setMatrixAt(k - 1, mtx.makeTranslation(-finSpan / 2 + (finSpan * k) / cellsR, 0, 0));
    for (let k = 1; k < cellsA; k++) ia.setMatrixAt(k - 1, mtx.makeTranslation(0, -finHeight / 2 + (finHeight * k) / cellsA, 0));
    panel.add(ir, ia);

    parts.fins.push({ pivot, hinge, panel });
  });

  /* ---- octaweb + heat shield --------------------------------------------- */
  const eng = new THREE.Group(); eng.name = 'EngineSection'; group.add(eng);

  const shieldShape = new THREE.Shape();
  shieldShape.absarc(0, 0, R - 0.02, 0, TAU, false);
  const holeAt = (x, z, r) => {
    const p = new THREE.Path(); p.absarc(x, z, r, 0, TAU, true); shieldShape.holes.push(p);
  };
  holeAt(0, 0, 0.56);
  const engPos = [[0, 0]];
  for (let i = 0; i < 8; i++) {
    const th = (i / 8) * TAU + Math.PI / 8;
    const x = Math.cos(th) * V.engineRing, z = Math.sin(th) * V.engineRing;
    engPos.push([x, z]); holeAt(x, z, 0.56);
  }
  const shield = addMesh(new THREE.ShapeGeometry(shieldShape, 48), materials.heatShield, 'HeatShield', eng);
  shield.rotation.x = Math.PI / 2; shield.position.y = 0.02;

  const octaShell = addMesh(new THREE.CylinderGeometry(R * 0.99, R * 0.985, 2.4, seg, 1, true),
    materials.octaweb, 'OctawebShell', eng);
  octaShell.position.y = 1.2;
  for (let i = 0; i < 8; i++) {
    const rib = addMesh(new THREE.BoxGeometry(R - 0.1, 0.34, 0.12), materials.octaweb, 'OctawebRib' + i, eng);
    rib.rotation.y = (i / 8) * TAU;
    rib.position.set(Math.cos((i / 8) * TAU) * (R / 2), 0.22, -Math.sin((i / 8) * TAU) * (R / 2));
  }

  /* ---- 9 Merlin-class engines -------------------------------------------- */
  const profile = (() => {
    const pts = [], rt = 0.115, re = V.bellExit, yT = -0.06, yE = -V.bellLength;
    pts.push(new THREE.Vector2(0.215, 0.42), new THREE.Vector2(0.215, 0.10), new THREE.Vector2(rt, yT));
    for (let i = 1; i <= 24; i++) {
      const t = i / 24;
      pts.push(new THREE.Vector2(rt + (re - rt) * Math.pow(t, 0.6), yT + (yE - yT) * t));
    }
    pts.push(new THREE.Vector2(re + 0.024, yE - 0.035));
    return pts;
  })();
  const bellGeo = keep(rippledLathe(THREE, profile, Math.min(seg, 120), 48, 0.012));
  const bellInnerGeo = keep(rippledLathe(THREE, profile.map(p => new THREE.Vector2(p.x * 0.955, p.y)), 72, 48, 0.006));

  const engineProto = new THREE.Group();
  engineProto.name = 'MerlinProto';
  const bell = new THREE.Mesh(bellGeo, materials.nozzle);
  bell.name = 'Bell'; bell.castShadow = true; bell.receiveShadow = true;
  const bellIn = new THREE.Mesh(bellInnerGeo, materials.nozzleInner);
  bellIn.name = 'BellInner';
  engineProto.add(bell, bellIn);
  // powerhead: turbopump, manifolds, gimbal actuator stubs
  const pump = new THREE.Mesh(keep(new THREE.CylinderGeometry(0.17, 0.19, 0.42, 20)), materials.octaweb);
  pump.name = 'Turbopump'; pump.position.set(0.26, 0.52, 0.06); pump.rotation.z = 0.18;
  const gas = new THREE.Mesh(keep(new THREE.CylinderGeometry(0.055, 0.055, 0.9, 12)), materials.octaweb);
  gas.name = 'GasGenDuct'; gas.position.set(-0.26, 0.2, 0.1); gas.rotation.x = 0.25;
  const manifold = new THREE.Mesh(keep(new THREE.TorusGeometry(0.2, 0.036, 10, 28)), materials.octaweb);
  manifold.name = 'FuelManifold'; manifold.position.y = 0.08; manifold.rotation.x = Math.PI / 2;
  const dome = new THREE.Mesh(keep(new THREE.SphereGeometry(0.2, 20, 14, 0, TAU, 0, Math.PI / 2)), materials.octaweb);
  dome.name = 'ChamberDome'; dome.position.y = 0.42;
  for (const s of [-1, 1]) {
    const jack = new THREE.Mesh(keep(new THREE.CylinderGeometry(0.045, 0.045, 0.5, 10)), materials.legPiston);
    jack.name = 'GimbalActuator'; jack.position.set(s * 0.22, 0.3, -0.2); jack.rotation.x = -0.35;
    engineProto.add(jack);
  }
  engineProto.add(pump, gas, manifold, dome);
  engineProto.traverse(o => { if (o.isMesh) o.castShadow = true; });

  engPos.forEach(([x, z], i) => {
    const gim = new THREE.Group();
    gim.name = i === 0 ? 'EngineCenterGimbal' : 'EngineRing' + i + 'Gimbal';
    gim.position.set(x, 0.0, z);
    const e = engineProto.clone(true);
    e.name = i === 0 ? 'MerlinCenter' : 'Merlin' + i;
    e.rotation.y = Math.atan2(z, x);
    const matOuter = keep(materials.nozzle.clone());
    const matInner = keep(materials.nozzleInner.clone());
    e.traverse(o => {
      if (o.name === 'Bell') o.material = matOuter;
      else if (o.name === 'BellInner') o.material = matInner;
    });
    // hot exit lip — only ever lit on an engine the telemetry says is burning
    const matLip = keep(new THREE.MeshBasicMaterial({
      color: 0xffb060, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false,
    }));
    const lip = new THREE.Mesh(keep(new THREE.TorusGeometry(V.bellExit + 0.01, 0.035, 10, 64)), matLip);
    lip.name = 'BellLip'; lip.rotation.x = Math.PI / 2; lip.position.y = -V.bellLength - 0.02;
    e.add(lip);
    gim.add(e);
    eng.add(gim);
    parts.engines.push({ gimbal: gim, mesh: e, index: i, matOuter, matInner, matLip, pos: new THREE.Vector3(x, -V.bellLength, z) });
  });
  parts.engineSection = eng;

  /* ---- landing legs ------------------------------------------------------- */
  const legGeoms = [
    keep(new THREE.CylinderGeometry(0.31, 0.29, 3.4, 24)),
    keep(new THREE.CylinderGeometry(0.245, 0.235, 3.0, 22)),
    keep(new THREE.CylinderGeometry(0.19, 0.185, 2.6, 20)),
  ];
  const footGeo = keep(new THREE.CylinderGeometry(0.82, 0.62, 0.26, 28));
  const padGeo = keep(new THREE.CylinderGeometry(0.88, 0.88, 0.07, 28));
  const strutGeo = keep(new THREE.CylinderGeometry(0.085, 0.085, 2.6, 12));

  V.legAz.forEach((azDeg, i) => {
    const pivot = new THREE.Group();
    pivot.name = 'LegPivot' + i;
    pivot.rotation.y = THREE.MathUtils.degToRad(-azDeg);
    group.add(pivot);

    const hip = addMesh(new THREE.BoxGeometry(0.62, 0.95, 1.05), materials.legStruct, 'LegHip' + i, pivot);
    hip.position.set(R - 0.02, 0.5, 0);

    const leg = new THREE.Group();
    leg.name = 'Leg' + i;
    leg.position.set(R + 0.05, 0.45, 0);
    pivot.add(leg);

    const segs = legGeoms.map((g, k) => {
      const m = new THREE.Mesh(g, k === 2 ? materials.legPiston : materials.legTube);
      m.name = 'LegSeg' + i + '_' + k; m.castShadow = true;
      leg.add(m); return m;
    });
    const collar = addMesh(new THREE.CylinderGeometry(0.33, 0.33, 0.2, 20), materials.legStruct, 'LegCollar' + i, leg);
    const foot = new THREE.Group(); foot.name = 'LegFoot' + i; leg.add(foot);
    const fm = new THREE.Mesh(footGeo, materials.legStruct); fm.name = 'FootBody' + i; fm.castShadow = true;
    const pm = new THREE.Mesh(padGeo, materials.heatShield); pm.name = 'FootPad' + i; pm.position.y = -0.15;
    foot.add(fm, pm);
    const strut = new THREE.Mesh(strutGeo, materials.legStruct);
    strut.name = 'LegStrut' + i; strut.castShadow = true; leg.add(strut);

    parts.legs.push({ pivot, leg, segs, foot, collar, strut });
  });

  /* ---- RCS pods ----------------------------------------------------------- */
  [90, 270].forEach((azDeg, i) => {
    const pivot = new THREE.Group();
    pivot.name = 'RcsPod' + i;
    pivot.rotation.y = THREE.MathUtils.degToRad(-azDeg);
    group.add(pivot);
    const body = addMesh(new THREE.BoxGeometry(0.42, 0.95, 0.7), materials.rcs, 'RcsBody' + i, pivot);
    body.position.set(R + 0.1, V.rcsY, 0);
    const noz4 = [];
    for (let k = 0; k < 4; k++) {
      const n = addMesh(new THREE.CylinderGeometry(0.045, 0.075, 0.16, 12), materials.rcs, 'RcsNozzle' + i + k, pivot);
      const a = (k / 4) * TAU;
      n.position.set(R + 0.12, V.rcsY + Math.cos(a) * 0.32, Math.sin(a) * 0.26);
      n.rotation.z = -Math.PI / 2 - Math.cos(a) * 0.9;
      n.rotation.y = Math.sin(a) * 0.9;
      noz4.push(n);
    }
    parts.rcsPods.push({ pivot, body, nozzles: noz4 });
  });

  /* ---- state -------------------------------------------------------------- */
  const S = {
    throttle: 0, gimbal: [0, 0], fins: [0, 0, 0, 0], deploy: 0,
    stroke: [0, 0, 0, 0], soot: 0, frost: 0.22, nEng: 0, hot: 0,
  };
  const LEG_L0 = 7.6, LEG_L1 = 8.22, TH_STOW = 0.06, TH_DEPLOY = 2.094; // rad, 120 deg -> foot at r 9.0, y -3.6

  function layoutLeg(L, i) {
    const l = parts.legs[i];
    const o = (9.0 - L) / 2;
    l.segs[0].position.y = 1.7;
    l.segs[1].position.y = 3.4 - o + 1.5;
    l.segs[2].position.y = (3.4 - o) + (3.0 - o) + 1.3;
    l.collar.position.y = 3.4 - o;
    l.foot.position.y = L;
    l.strut.position.set(-0.3, 1.55, 0);
    l.strut.rotation.z = 0.16;
  }

  function setTelemetry(t = {}) {
    if (t.throttle_act !== undefined) S.throttle = t.throttle_act;
    if (t.throttle !== undefined) S.throttle = t.throttle;
    if (t.gimbal_act) S.gimbal = t.gimbal_act;
    if (t.fins_act) S.fins = t.fins_act;
    if (t.deploy_frac !== undefined) S.deploy = t.deploy_frac;
    if (t.stroke) S.stroke = t.stroke;
    if (t.n_eng !== undefined) S.nEng = t.n_eng;
    if (t.Q_heat !== undefined) S.soot = Math.min(1, t.Q_heat / 2.2e8);
    if (t.soot !== undefined) S.soot = t.soot;
    if (t.frost !== undefined) S.frost = t.frost;

    // gimbal: body X/Y -> three X/-Z (§10.7 permutation, small-angle safe)
    const gx = S.gimbal[0] || 0, gy = S.gimbal[1] || 0;
    const n = Math.round(S.nEng);
    S.hot = S.throttle > 0.05 ? Math.min(1, S.hot + 0.004) : S.hot * 0.985;
    parts.engines.forEach((e, i) => {
      const live = n >= 9 || (n > 0 && i === 0) || (n === 3 && (i === 1 || i === 5));
      e.gimbal.rotation.set(live ? gx : 0, 0, live ? -gy : 0);
      // regeneratively-cooled bells do NOT glow (§11.5) — the lit engine's MOUTH does:
      // combustion light on the inner wall + hot exit lip, decaying to nothing when shut down
      e.matInner.emissiveIntensity = live ? 1.9 + 1.6 * S.throttle : S.hot * 0.02;
      e.matOuter.emissiveIntensity = live ? S.hot * 0.2 : S.hot * 0.03;
      e.matLip.opacity = live ? 0.55 + 0.45 * S.throttle : 0;
    });

    parts.fins.forEach((f, i) => { f.panel.rotation.x = S.fins[i] || 0; });

    const dep = THREE.MathUtils.clamp(S.deploy, 0, 1);
    const th = TH_STOW + (TH_DEPLOY - TH_STOW) * dep;
    parts.legs.forEach((l, i) => {
      l.leg.rotation.z = -th;
      const L = (LEG_L0 + (LEG_L1 - LEG_L0) * dep) - (S.stroke[i] || 0);
      layoutLeg(L, i);
      l.foot.rotation.z = th;
      l.leg.visible = true;
    });

    materials.bodySoot.opacity = S.soot * 0.92;
    materials.frost.opacity = S.frost * 0.55;
    return api;
  }

  function setEnvMap(envMap, intensity = 1) {
    for (const m of Object.values(materials)) {
      if (m.isMeshStandardMaterial) { m.envMap = envMap; m.envMapIntensity = (m.envMapIntensity ?? 1) * intensity; m.needsUpdate = true; }
    }
    return api;
  }

  const api = {
    group, parts, materials, vehicle: V, state: S,
    setTelemetry, setEnvMap,
    /** world-space bell-exit anchors, for plume / light placement */
    enginePositions: parts.engines.map(e => e.pos.clone()),
    dispose() {
      disposables.forEach(o => o.dispose && o.dispose());
      group.traverse(o => { if (o.isMesh || o.isInstancedMesh) o.geometry?.dispose?.(); });
    },
  };
  setTelemetry({ deploy_frac: 0, throttle_act: 0, n_eng: 0 });
  return api;
}

export default createKestrel9;
