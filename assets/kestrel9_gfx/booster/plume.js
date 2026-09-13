/* KESTREL-9 plume — analytic, telemetry-driven. Companion to kestrel9.js.
 *
 * The jet is BUILT ON THE NOZZLE CONTOUR: an inner-bell flame that fills the
 * expansion cone from the throat to the exit plane, then a free jet whose ring
 * radius starts exactly at the exit radius and flares downstream. Shock-cell
 * spacing, length and flare all come from telemetry (§11.6): throttle, p_amb,
 * mach, q̄. Only the engines the telemetry says are lit produce anything.
 *
 *   const plume = createPlume(THREE, booster);
 *   plume.setTelemetry(tlm);  plume.update(dt);  plume.flash();
 */

const TAU = Math.PI * 2;
const P0 = 101325;
const R_THROAT = 0.115;   // matches kestrel9.js bell profile
const CONTOUR_P = 0.6;

function cv(w, h) { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; }

function coreTex(THREE) {
  const c = cv(64, 512), x = c.getContext('2d');
  const g = x.createLinearGradient(0, 0, 0, 512);
  g.addColorStop(0.00, 'rgba(255,250,240,1.00)');
  g.addColorStop(0.06, 'rgba(255,226,196,0.95)');
  g.addColorStop(0.20, 'rgba(255,170,104,0.78)');
  g.addColorStop(0.46, 'rgba(240,118,52,0.48)');
  g.addColorStop(0.74, 'rgba(160,66,28,0.2)');
  g.addColorStop(1.00, 'rgba(80,34,16,0.00)');
  x.fillStyle = g; x.fillRect(0, 0, 64, 512);
  for (let i = 0; i < 46; i++) {
    x.fillStyle = 'rgba(255,214,158,' + (0.03 + Math.random() * 0.07) + ')';
    x.fillRect(Math.random() * 64, Math.random() * 512, 1 + Math.random() * 3, 40 + Math.random() * 180);
  }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  return t;
}

function blobTex(THREE, inner, outer, pow = 2) {
  const c = cv(128, 128), x = c.getContext('2d');
  const g = x.createRadialGradient(64, 64, 0, 64, 64, 64);
  for (let i = 0; i <= 8; i++) {
    const t = i / 8, a = Math.pow(1 - t, pow);
    g.addColorStop(t, `rgba(${inner[0]},${inner[1]},${inner[2]},${a})`);
  }
  x.fillStyle = g; x.fillRect(0, 0, 128, 128);
  if (outer) {
    const g2 = x.createRadialGradient(64, 64, 0, 64, 64, 64);
    g2.addColorStop(0, `rgba(${outer[0]},${outer[1]},${outer[2]},0.5)`);
    g2.addColorStop(1, 'rgba(0,0,0,0)');
    x.globalCompositeOperation = 'lighter';
    x.fillStyle = g2; x.fillRect(0, 0, 128, 128);
  }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

/* Unit tube: t = 0 at the bell exit plane, t = 1 downstream. Deformed every
 * frame into the free-jet envelope, so the jet always meets the exit radius. */
function jetTube(THREE, nAx, nRad) {
  const pos = [], uv = [], idx = [], ts = [], cs = [], ss = [];
  for (let j = 0; j <= nAx; j++) {
    const t = j / nAx;
    for (let i = 0; i <= nRad; i++) {
      const a = (i / nRad) * TAU;
      cs.push(Math.cos(a)); ss.push(Math.sin(a)); ts.push(t);
      pos.push(Math.cos(a), -t, Math.sin(a));
      uv.push(i / nRad, 1 - t);
    }
  }
  for (let j = 0; j < nAx; j++) {
    for (let i = 0; i < nRad; i++) {
      const a = j * (nRad + 1) + i, b = a + 1, c = a + nRad + 1, d = c + 1;
      idx.push(a, c, b, b, c, d);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return { geo: g, ts: Float32Array.from(ts), cs: Float32Array.from(cs), ss: Float32Array.from(ss) };
}

function shapeJet(jet, rExit, flare, L, wob, time) {
  const p = jet.geo.attributes.position;
  for (let k = 0; k < jet.ts.length; k++) {
    const t = jet.ts[k];
    const necks = 1 - 0.1 * Math.sin(t * 9.0 + time * 6);           // faint cell waist
    const r = rExit * (1 + (flare - 1) * Math.pow(t, 0.78)) * necks
      * (1 + wob * t * Math.sin(jet.cs[k] * 3 + time * 5));
    p.setXYZ(k, jet.cs[k] * r, -L * t, jet.ss[k] * r);
  }
  p.needsUpdate = true;
  jet.geo.computeBoundingSphere();
}

export function createPlume(THREE, booster, opts = {}) {
  const V = booster.vehicle;
  const rExit = V.bellExit, yExit = -V.bellLength, De = rExit * 2;
  const kept = [];
  const keep = o => (kept.push(o), o);

  const texCore = keep(coreTex(THREE));
  const texBlob = keep(blobTex(THREE, [255, 196, 128], [255, 251, 236], 2.2));
  const texFlash = keep(blobTex(THREE, [130, 255, 150], [232, 255, 232], 1.6));

  const mkMat = (map, blending, opacity, color) => keep(new THREE.MeshBasicMaterial({
    map, blending, transparent: true, opacity, depthWrite: false,
    side: THREE.DoubleSide, color: color ?? 0xffffff,
  }));

  // inner-bell flame: the nozzle expansion contour itself
  const bellPts = [];
  for (let i = 0; i <= 18; i++) {
    const t = i / 18;
    bellPts.push(new THREE.Vector2(
      (R_THROAT + (rExit - R_THROAT) * Math.pow(t, CONTOUR_P)) * 0.965,
      -0.06 + (yExit + 0.06) * t));
  }
  const bellFlameGeo = keep(new THREE.LatheGeometry(bellPts, 44));
  const jetA = jetTube(THREE, 26, 40);   // core
  const jetB = jetTube(THREE, 20, 32);   // sheath
  keep(jetA.geo); keep(jetB.geo);
  const diskGeo = keep(new THREE.SphereGeometry(1, 24, 12));
  const throatGeo = keep(new THREE.SphereGeometry(0.16, 20, 14));
  const _m = new THREE.Matrix4(), _v = new THREE.Vector3();

  const units = booster.parts.engines.map((e, i) => {
    const root = new THREE.Group();
    root.name = 'Plume' + i;
    e.gimbal.add(root);                       // rides the gimbal, so it always aims with the bell

    const bellFlame = new THREE.Mesh(bellFlameGeo, mkMat(texCore, THREE.AdditiveBlending, 0.9));
    bellFlame.name = 'BellFlame' + i;
    const throat = new THREE.Mesh(throatGeo, mkMat(texBlob, THREE.AdditiveBlending, 1));
    throat.name = 'ThroatCore' + i;
    throat.position.y = -0.08;
    throat.scale.set(1, 0.7, 1);

    const jet = new THREE.Group();
    jet.name = 'FreeJet' + i;
    jet.position.y = yExit;                   // starts exactly at the exit plane
    const core = new THREE.Mesh(jetA.geo, mkMat(texCore, THREE.AdditiveBlending, 0.95));
    core.name = 'JetCore' + i;
    const sheath = new THREE.Mesh(jetB.geo, mkMat(texCore, THREE.NormalBlending, 0.26, 0xff9440));
    sheath.name = 'JetSheath' + i;
    sheath.scale.setScalar(1);
    const diamonds = new THREE.InstancedMesh(diskGeo, mkMat(texBlob, THREE.AdditiveBlending, 0.9), 9);
    diamonds.name = 'ShockCells' + i;
    diamonds.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(27), 3);
    jet.add(core, sheath, diamonds);

    const flash = new THREE.Sprite(keep(new THREE.SpriteMaterial({
      map: texFlash, blending: THREE.AdditiveBlending, transparent: true, opacity: 0, depthWrite: false,
    })));
    flash.name = 'TeaTebFlash' + i;
    flash.position.y = yExit;

    root.add(bellFlame, throat, jet, flash);
    root.visible = false;
    return { root, bellFlame, throat, jet, core, sheath, diamonds, flash, index: i };
  });

  // supersonic retropropulsion: the plume wraps forward and envelops the stage
  const srpTube = jetTube(THREE, 18, 30);
  keep(srpTube.geo);
  const srp = new THREE.Mesh(srpTube.geo, mkMat(texCore, THREE.AdditiveBlending, 0, 0xffcf9a));
  srp.name = 'SrpEnvelope';
  srp.rotation.x = Math.PI;
  srp.position.y = -1.0;
  booster.group.add(srp);

  const light = new THREE.PointLight(0xffb066, 0, 300, 2);
  light.name = 'PlumeLight';
  light.position.set(0, yExit - 1.5, 0);
  booster.group.add(light);

  const spot = new THREE.SpotLight(0xffa860, 0, 420, 0.66, 0.5, 1.6);
  spot.name = 'PlumeSpot';
  spot.position.set(0, yExit, 0);
  spot.target.position.set(0, -50, 0);
  booster.group.add(spot, spot.target);

  const S = { throttle: 0, nEng: 0, pAmb: P0, mach: 0, qbar: 0, flash: 0, t: 0, bellAlt: 1e5 };
  const tmpC = new THREE.Color();
  const isLit = (i, n) => n >= 9 || (n > 0 && i === 0) || (n === 3 && (i === 1 || i === 5));

  function setTelemetry(t = {}) {
    if (t.throttle_act !== undefined) S.throttle = t.throttle_act;
    if (t.throttle !== undefined) S.throttle = t.throttle;
    if (t.n_eng !== undefined) S.nEng = t.n_eng;
    if (t.p_amb !== undefined) S.pAmb = Math.max(1, t.p_amb);
    if (t.mach !== undefined) S.mach = t.mach;
    if (t.qbar !== undefined) S.qbar = t.qbar;
    if (t.bell_alt !== undefined) S.bellAlt = t.bell_alt;   // bell exit height over terrain, m
    return api;
  }

  function update(dt = 0.016) {
    S.t += dt;
    const th = S.throttle, n = Math.round(S.nEng);
    const pr = Math.sqrt(P0 / S.pAmb);
    const under = Math.min(1, Math.max(0, 1 - S.pAmb / P0));
    const burning = th > 0.02 && n > 0;

    const spacing = 1.15 * De * pr;                               // §11.6 cell spacing
    let L = (4.2 + 15 * th) * (1 + 2.1 * under) * (1 + 0.03 * Math.sin(S.t * 31));
    let flare = 1.05 + 0.5 * th + 6.0 * under;
    // impinging jet: the column stays a column until it meets the slab; the stage
    // draws the radial wall-jet sheet there (bell_alt is the standoff)
    if (S.bellAlt < L) {
      const squash = Math.max(0.12, S.bellAlt / L);
      L = Math.max(0.6, S.bellAlt * 0.97);
      flare *= 1 + (1 - squash) * 0.7;
    }

    if (burning) {
      shapeJet(jetA, rExit * 0.92, flare, L, 0.02, S.t);
      shapeJet(jetB, rExit * 1.08, flare * 1.15, L * 1.25, 0.05, S.t * 0.7);
    }

    units.forEach((u, i) => {
      const live = burning && isLit(i, n);
      u.root.visible = live || S.flash > 0.01;
      if (!u.root.visible) return;

      u.bellFlame.material.opacity = live ? 0.4 + 0.35 * th : 0;
      u.throat.material.opacity = live ? 0.85 + 0.15 * th : 0;
      u.throat.scale.set(1.3 + 0.2 * th, 0.9, 1.3 + 0.2 * th);
      u.jet.visible = live;
      u.core.material.opacity = live ? 0.3 + 0.22 * th : 0;
      u.sheath.material.opacity = live ? (0.18 + 0.26 * th) * (1 - 0.5 * under) : 0;
      u.core.rotation.y = S.t * 1.3;
      u.sheath.rotation.y = -S.t * 0.9;

      const visible = Math.max(0, Math.min(9, Math.floor(L / spacing)));
      u.diamonds.count = live ? visible : 0;
      for (let k = 0; k < visible; k++) {
        const y = -spacing * (k + 0.5) * (1 + 0.02 * Math.sin(S.t * 21 + k));
        const rr = rExit * (1 + (flare - 1) * Math.pow(Math.min(1, -y / L), 0.78));
        const s = Math.max(0.06, rr * 0.78 * (1 - k / 16) * (0.8 + 0.3 * th));
        u.diamonds.setMatrixAt(k, _m.makeTranslation(0, y, 0).scale(_v.set(s, s * 0.8, s)));
        const fade = Math.pow(1 - k / (visible + 1), 1.25) * (0.3 + 0.4 * th);
        tmpC.setRGB(fade * 1.7, fade * 1.1, fade * 0.74);
        u.diamonds.setColorAt(k, tmpC);
      }
      u.diamonds.instanceMatrix.needsUpdate = true;
      if (u.diamonds.instanceColor) u.diamonds.instanceColor.needsUpdate = true;

      u.flash.material.opacity = S.flash * 0.95;
      u.flash.scale.setScalar(1.3 + 2.4 * (1 - S.flash));
    });

    const ct = burning && S.qbar > 200 ? (th * 845e3 * Math.max(1, n)) / (S.qbar * 10.52) : 0;
    const srpMix = burning && S.mach > 1.1 ? Math.min(1, Math.max(0, (ct - 0.5) / 2.5)) : 0;
    srp.visible = srpMix > 0.01;
    if (srp.visible) {
      shapeJet(srpTube, 1.9 + 1.2 * srpMix, 2.6 + 1.6 * srpMix, 16 + 26 * srpMix, 0.09, S.t * 0.8);
      srp.material.opacity = 0.12 + 0.26 * srpMix;
      srp.rotation.y = S.t * 0.6;
    }

    const power = burning ? th * Math.max(1, n) : 0;
    light.intensity = power * 800 + S.flash * 400;
    light.color.setHSL(0.055 + 0.02 * (1 - th), 0.85 - 0.25 * th, 0.5 + 0.12 * th);
    if (S.flash > 0.01) light.color.setRGB(0.45, 1, 0.55);
    spot.intensity = power * 1500;
    spot.color.copy(light.color);
    S.flash = Math.max(0, S.flash - dt * 3.3);
    return api;
  }

  const api = {
    group: booster.group, units, light, spot, srp, state: S, isLit,
    setTelemetry, update, flash() { S.flash = 1; return api; },
    dispose() { kept.forEach(o => o.dispose && o.dispose()); },
  };
  return api;
}

export default createPlume;
