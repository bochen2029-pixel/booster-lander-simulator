/* <booster-stage> — cinematic viewer + inspector shell for the KESTREL-9 model.
 *
 * Self-contained web component: procedural sky/env/pad, sun + pad floods, a
 * custom two-pass bloom, wall-jet dust, orbit camera, and an inspector rail that
 * drives the model through the SAME fields the TLM packet carries (§10.3), so
 * anything that looks right here looks right when the socket is live.
 *
 * In the repo: keep kestrel9.js + plume.js, swap `import * as THREE from 'three'`
 * for the CDN URL below, and drive booster.setTelemetry(decodedTlm) instead of
 * the rail. The stage itself is a dev tool, not part of the flight renderer.
 */
import * as THREE from 'https://cdn.jsdelivr.net/npm/three@0.185.1/build/three.module.js';
import { createKestrel9 } from './booster/kestrel9.js';
import { createPlume } from './booster/plume.js';
import { exportSTL } from './booster/export-stl.js';

const TAU = Math.PI * 2;
const D2R = Math.PI / 180;
const LS = 'k9-stage-v1';

/* ---------------------------------------------------------------- presets */

const SKIES = {
  noon: { elev: 54, az: 112, turb: 2.0, zenith: 0x3b7cc9, horizon: 0xcbd9e8, sun: 0xfff6ea, sunI: 2.9, amb: 0.4, fog: 0xc3d4e6, fogD: 0.00006, ground: 0x8c8272 },
  dusk: { elev: 11, az: 126, turb: 2.6, zenith: 0x2a4c78, horizon: 0xd8a887, sun: 0xffd9b0, sunI: 3.4, amb: 0.45, fog: 0x9d7a5c, fogD: 0.00007, ground: 0x6f6355 },
  night: { elev: -9, az: 126, turb: 3.0, zenith: 0x05070f, horizon: 0x0d1524, sun: 0x9fb6e0, sunI: 0.3, amb: 0.12, fog: 0x0b1220, fogD: 0.00014, ground: 0x2a2a2c },
  high: { elev: 26, az: 118, turb: 0.35, zenith: 0x02030a, horizon: 0x1d4f86, sun: 0xffffff, sunI: 5.2, amb: 0.22, fog: 0x0a1526, fogD: 0.000012, ground: 0x7d7568 },
};

const PRESETS = {
  entry: { label: 'ENTRY BURN', sky: 'high', y: 180, tilt: 7, throttle: 0.86, nEng: 3, deploy: 0, fins: [14, -11, 12, -13], soot: 0.45, frost: 0.1, pAmb: 900, mach: 3.4, qbar: 3200, dust: 0, cam: 'hero' },
  aero: { label: 'AERO DESCENT', sky: 'high', y: 150, tilt: 5, throttle: 0, nEng: 0, deploy: 0, fins: [17, -9, 15, -12], soot: 0.72, frost: 0.06, pAmb: 26500, mach: 1.35, qbar: 34000, dust: 0, cam: 'fins' },
  burn: { label: 'LANDING BURN', sky: 'dusk', y: 96, tilt: 2.2, throttle: 0.74, nEng: 1, deploy: 0.35, fins: [6, -5, 5, -6], soot: 0.78, frost: 0.05, pAmb: 98000, mach: 0.28, qbar: 6200, dust: 0.35, cam: 'hero' },
  touchdown: { label: 'TOUCHDOWN', sky: 'dusk', y: 4.4, tilt: 0.7, throttle: 0.41, nEng: 1, deploy: 1, fins: [2, -2, 2, -2], soot: 0.8, frost: 0.04, pAmb: 101325, mach: 0.01, qbar: 40, dust: 1, cam: 'legs' },
  landed: { label: 'LANDED / GOOD', sky: 'dusk', y: 3.6, tilt: 0.4, throttle: 0, nEng: 0, deploy: 1, fins: [0, 0, 0, 0], soot: 0.8, frost: 0.02, pAmb: 101325, mach: 0, qbar: 0, dust: 0.12, cam: 'hero' },
  factory: { label: 'PRE-FLIGHT', sky: 'noon', y: 3.6, tilt: 0, throttle: 0, nEng: 0, deploy: 1, fins: [0, 0, 0, 0], soot: 0, frost: 0.5, pAmb: 101325, mach: 0, qbar: 0, dust: 0, cam: 'hero' },
  static: { label: 'STATIC FIRE · 9 ENG', sky: 'dusk', y: 3.6, tilt: 0, throttle: 1, nEng: 9, deploy: 1, fins: [0, 0, 0, 0], soot: 0.1, frost: 0.6, pAmb: 101325, mach: 0, qbar: 0, dust: 0.6, cam: 'engines' },
};

const CAMS = {
  hero: { pos: [98, 32, 64], tgt: [0, 24, 0], fov: 28 },
  fins: { pos: [13, 49, 9], tgt: [0, 45, 0], fov: 34 },
  engines: { pos: [11, 0.9, 8], tgt: [0, 1.6, 0], fov: 38 },
  legs: { pos: [21, 5.4, 15], tgt: [0, 5.6, 0], fov: 34 },
  wide: { pos: [274, 56, 180], tgt: [0, 28, 0], fov: 26 },
};

/* ---------------------------------------------------------------- textures */

function cvs(w, h) { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; }

function skyEquirect(sky) {
  const W = 512, H = 256, c = cvs(W, H), x = c.getContext('2d');
  const zen = new THREE.Color(sky.zenith), hor = new THREE.Color(sky.horizon);
  const g = x.createLinearGradient(0, 0, 0, H * 0.5);
  g.addColorStop(0, '#' + zen.getHexString());
  g.addColorStop(1, '#' + hor.getHexString());
  x.fillStyle = g; x.fillRect(0, 0, W, H * 0.5 + 1);
  const gnd = new THREE.Color(sky.ground);
  const g2 = x.createLinearGradient(0, H * 0.5, 0, H);
  g2.addColorStop(0, '#' + gnd.clone().lerp(hor, 0.5).getHexString());
  g2.addColorStop(1, '#' + gnd.clone().multiplyScalar(0.5).getHexString());
  x.fillStyle = g2; x.fillRect(0, H * 0.5, W, H * 0.5);
  // sun disc + glow, placed by elevation/azimuth so reflections match the light
  const sx = ((sky.az / 360) % 1) * W, sy = H * 0.5 - (sky.elev / 90) * H * 0.5;
  const sg = x.createRadialGradient(sx, sy, 0, sx, sy, 86);
  const sc = new THREE.Color(sky.sun);
  sg.addColorStop(0, '#' + sc.getHexString());
  sg.addColorStop(0.06, 'rgba(255,244,220,0.85)');
  sg.addColorStop(1, 'rgba(255,230,190,0)');
  x.globalCompositeOperation = 'lighter';
  x.fillStyle = sg; x.beginPath(); x.arc(sx, sy, 86, 0, TAU); x.fill();
  const t = new THREE.Texture(c);
  t.mapping = THREE.EquirectangularReflectionMapping;
  t.colorSpace = THREE.SRGBColorSpace;
  t.needsUpdate = true;
  return t;
}

function scrubTexture() {
  const W = 1024, H = 1024, c = cvs(W, H), x = c.getContext('2d');
  x.fillStyle = '#8c8474'; x.fillRect(0, 0, W, H);
  for (let i = 0; i < 4200; i++) {
    const r = 3 + Math.random() * 34;
    x.fillStyle = `rgba(${132 + Math.random() * 32 | 0},${126 + Math.random() * 30 | 0},${106 + Math.random() * 26 | 0},${0.03 + Math.random() * 0.1})`;
    x.beginPath(); x.arc(Math.random() * W, Math.random() * H, r, 0, TAU); x.fill();
  }
  for (let i = 0; i < 1400; i++) {   // scrub tufts
    x.fillStyle = `rgba(${96 + Math.random() * 22 | 0},${98 + Math.random() * 22 | 0},${74 + Math.random() * 18 | 0},0.3)`;
    x.beginPath(); x.arc(Math.random() * W, Math.random() * H, 2 + Math.random() * 6, 0, TAU); x.fill();
  }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(200, 200);
  t.anisotropy = 8;
  return t;
}

function padTextures() {
  const S = 1024, ca = cvs(S, S), cr = cvs(S, S);
  const a = ca.getContext('2d'), r = cr.getContext('2d');
  a.fillStyle = '#9a9994'; a.fillRect(0, 0, S, S);
  r.fillStyle = '#b4b4b4'; r.fillRect(0, 0, S, S);
  const C = S / 2;
  // concrete pour joints
  a.strokeStyle = 'rgba(110,110,106,0.55)'; a.lineWidth = 2.5;
  for (let i = 0; i <= 8; i++) {
    a.beginPath(); a.moveTo((i / 8) * S, 0); a.lineTo((i / 8) * S, S); a.stroke();
    a.beginPath(); a.moveTo(0, (i / 8) * S); a.lineTo(S, (i / 8) * S); a.stroke();
  }
  for (let i = 0; i < 1400; i++) {
    const rr = 3 + Math.random() * 22;
    a.fillStyle = `rgba(${140 + Math.random() * 34 | 0},${138 + Math.random() * 32 | 0},${132 + Math.random() * 30 | 0},${0.02 + Math.random() * 0.08})`;
    a.beginPath(); a.arc(Math.random() * S, Math.random() * S, rr, 0, TAU); a.fill();
  }
  // fine aggregate speckle
  for (let i = 0; i < 9000; i++) {
    a.fillStyle = `rgba(${90 + Math.random() * 80 | 0},${90 + Math.random() * 76 | 0},${86 + Math.random() * 70 | 0},${0.08 + Math.random() * 0.18})`;
    a.fillRect(Math.random() * S, Math.random() * S, 1 + Math.random() * 1.5, 1 + Math.random() * 1.5);
  }
  // generic circle-X target markings
  a.strokeStyle = 'rgba(240,240,236,0.85)'; a.lineWidth = 14;
  a.beginPath(); a.arc(C, C, S * 0.30, 0, TAU); a.stroke();
  a.lineWidth = 9;
  a.beginPath(); a.arc(C, C, S * 0.40, 0, TAU); a.stroke();
  a.lineWidth = 16;
  for (const k of [0, 1]) {
    a.beginPath();
    a.moveTo(C + Math.cos(Math.PI / 4 + k * Math.PI / 2) * S * 0.30, C + Math.sin(Math.PI / 4 + k * Math.PI / 2) * S * 0.30);
    a.lineTo(C - Math.cos(Math.PI / 4 + k * Math.PI / 2) * S * 0.30, C - Math.sin(Math.PI / 4 + k * Math.PI / 2) * S * 0.30);
    a.stroke();
  }
  // scorch — the pad earns its soot (§11.3)
  const sg = a.createRadialGradient(C, C, 0, C, C, S * 0.30);
  sg.addColorStop(0, 'rgba(18,18,18,0.86)');
  sg.addColorStop(0.45, 'rgba(28,26,24,0.5)');
  sg.addColorStop(1, 'rgba(40,36,32,0)');
  a.fillStyle = sg; a.beginPath(); a.arc(C, C, S * 0.3, 0, TAU); a.fill();
  const rg = r.createRadialGradient(C, C, 0, C, C, S * 0.32);
  rg.addColorStop(0, 'rgba(150,150,150,1)'); rg.addColorStop(1, 'rgba(180,180,180,0)');
  r.fillStyle = rg; r.beginPath(); r.arc(C, C, S * 0.32, 0, TAU); r.fill();
  const mk = c2 => { const t = new THREE.CanvasTexture(c2); t.anisotropy = 8; return t; };
  const m = mk(ca); m.colorSpace = THREE.SRGBColorSpace;
  return { map: m, roughnessMap: mk(cr) };
}

function blobDisc() {
  const S = 256, c = cvs(S, S), x = c.getContext('2d');
  const g = x.createRadialGradient(S / 2, S / 2, 0, S / 2, S / 2, S / 2);
  g.addColorStop(0, 'rgba(255,255,255,1)');
  g.addColorStop(0.25, 'rgba(255,240,220,0.7)');
  g.addColorStop(0.6, 'rgba(255,200,150,0.22)');
  g.addColorStop(1, 'rgba(255,180,120,0)');
  x.fillStyle = g; x.fillRect(0, 0, S, S);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

function dustSprite() {
  const S = 64, c = cvs(S, S), x = c.getContext('2d');
  const g = x.createRadialGradient(S / 2, S / 2, 0, S / 2, S / 2, S / 2);
  g.addColorStop(0, 'rgba(216,203,178,0.85)');
  g.addColorStop(0.5, 'rgba(190,176,152,0.35)');
  g.addColorStop(1, 'rgba(170,158,136,0)');
  x.fillStyle = g; x.fillRect(0, 0, S, S);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

/* ---------------------------------------------------------------- sky shader */

function makeSky() {
  const geo = new THREE.SphereGeometry(9000, 48, 32);
  const mat = new THREE.ShaderMaterial({
    side: THREE.BackSide, depthWrite: false, toneMapped: false,
    uniforms: {
      uZenith: { value: new THREE.Color(0x2f6fc4) },
      uHorizon: { value: new THREE.Color(0xbcd0e4) },
      uGround: { value: new THREE.Color(0x8c8272) },
      uSunDir: { value: new THREE.Vector3(0.4, 0.6, 0.7) },
      uSunCol: { value: new THREE.Color(0xfff4e2) },
      uTurb: { value: 2.2 },
      uStars: { value: 0 },
    },
    vertexShader: `varying vec3 vD; void main(){ vD = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }`,
    fragmentShader: `
      varying vec3 vD;
      uniform vec3 uZenith, uHorizon, uGround, uSunDir, uSunCol;
      uniform float uTurb, uStars;
      float hash(vec3 p){ return fract(sin(dot(p, vec3(127.1,311.7,74.7)))*43758.5453); }
      void main(){
        vec3 d = normalize(vD);
        float h = d.y;
        float t = pow(clamp(h,0.0,1.0), 0.45/max(uTurb*0.25,0.2));
        vec3 col = mix(uHorizon, uZenith, t);
        col = mix(col, uGround, smoothstep(0.0,-0.06,h));
        float mu = max(dot(d, normalize(uSunDir)), 0.0);
        col += uSunCol * pow(mu, 16.0) * 0.10 * uTurb;         // forward scatter
        col += uSunCol * pow(mu, 1400.0) * 7.0;                 // sun disc
        col += uSunCol * pow(mu, 90.0) * 0.24;
        if (uStars > 0.01) {
          vec3 q = floor(d * 420.0);
          float s = hash(q);
          float star = smoothstep(0.9975, 1.0, s) * uStars;
          col += vec3(star) * (0.7 + 0.5 * hash(q + 3.1));
        }
        gl_FragColor = vec4(max(col, 0.0), 1.0);
      }`,
  });
  const m = new THREE.Mesh(geo, mat);
  m.name = 'Sky'; m.frustumCulled = false;
  return m;
}

/* ---------------------------------------------------------------- bloom */

const FS_VERT = `varying vec2 vUv; void main(){ vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }`;

function makeBloom(renderer) {
  const quad = new THREE.Mesh(new THREE.PlaneGeometry(2, 2));
  quad.frustumCulled = false;
  const scene = new THREE.Scene(); scene.add(quad);
  const cam = new THREE.Camera();
  const opt = { depthBuffer: false, stencilBuffer: false };
  const rtScene = new THREE.WebGLRenderTarget(1, 1, { samples: 4 });
  const rtA = new THREE.WebGLRenderTarget(1, 1, opt);
  const rtB = new THREE.WebGLRenderTarget(1, 1, opt);
  for (const rt of [rtScene, rtA, rtB]) rt.texture.colorSpace = THREE.SRGBColorSpace;

  const bright = new THREE.ShaderMaterial({
    uniforms: { tD: { value: null }, thr: { value: 0.92 }, knee: { value: 0.22 } },
    vertexShader: FS_VERT, depthTest: false, depthWrite: false,
    fragmentShader: `varying vec2 vUv; uniform sampler2D tD; uniform float thr, knee;
      void main(){ vec3 c = texture2D(tD, vUv).rgb; float l = max(max(c.r,c.g),c.b);
        float w = clamp((l - thr)/knee, 0.0, 1.0); gl_FragColor = vec4(c*w*w, 1.0); }`,
  });
  const blur = new THREE.ShaderMaterial({
    uniforms: { tD: { value: null }, dir: { value: new THREE.Vector2(1, 0) }, px: { value: new THREE.Vector2() } },
    vertexShader: FS_VERT, depthTest: false, depthWrite: false,
    fragmentShader: `varying vec2 vUv; uniform sampler2D tD; uniform vec2 dir, px;
      void main(){ vec2 o = dir*px; vec3 s = vec3(0.0);
        s += texture2D(tD, vUv - o*4.0).rgb*0.028; s += texture2D(tD, vUv - o*3.0).rgb*0.066;
        s += texture2D(tD, vUv - o*2.0).rgb*0.121; s += texture2D(tD, vUv - o).rgb*0.176;
        s += texture2D(tD, vUv).rgb*0.218;
        s += texture2D(tD, vUv + o).rgb*0.176; s += texture2D(tD, vUv + o*2.0).rgb*0.121;
        s += texture2D(tD, vUv + o*3.0).rgb*0.066; s += texture2D(tD, vUv + o*4.0).rgb*0.028;
        gl_FragColor = vec4(s, 1.0); }`,
  });
  const comp = new THREE.ShaderMaterial({
    uniforms: { tD: { value: null }, tB: { value: null }, amt: { value: 0.45 }, vig: { value: 0.26 }, grain: { value: 0.016 }, time: { value: 0 } },
    vertexShader: FS_VERT, depthTest: false, depthWrite: false,
    fragmentShader: `varying vec2 vUv; uniform sampler2D tD, tB; uniform float amt, vig, grain, time;
      void main(){ vec3 c = texture2D(tD, vUv).rgb + texture2D(tB, vUv).rgb * amt;
        vec2 q = vUv - 0.5; c *= 1.0 - vig * dot(q,q) * 2.0;
        float n = fract(sin(dot(vUv*vec2(1234.5,6789.1) + time, vec2(12.9898,78.233)))*43758.5453);
        c += (n - 0.5) * grain;
        gl_FragColor = vec4(c, 1.0); }`,
  });

  return {
    rtScene, enabled: true,
    setSize(w, h, dpr) {
      rtScene.setSize(w * dpr, h * dpr);
      rtA.setSize(Math.max(1, (w * dpr) >> 2), Math.max(1, (h * dpr) >> 2));
      rtB.setSize(Math.max(1, (w * dpr) >> 2), Math.max(1, (h * dpr) >> 2));
      blur.uniforms.px.value.set(1 / rtA.width, 1 / rtA.height);
    },
    render(t) {
      quad.material = bright; bright.uniforms.tD.value = rtScene.texture;
      renderer.setRenderTarget(rtA); renderer.render(scene, cam);
      quad.material = blur;
      blur.uniforms.tD.value = rtA.texture; blur.uniforms.dir.value.set(1, 0);
      renderer.setRenderTarget(rtB); renderer.render(scene, cam);
      blur.uniforms.tD.value = rtB.texture; blur.uniforms.dir.value.set(0, 1);
      renderer.setRenderTarget(rtA); renderer.render(scene, cam);
      quad.material = comp;
      comp.uniforms.tD.value = rtScene.texture;
      comp.uniforms.tB.value = rtA.texture;
      comp.uniforms.time.value = t;
      renderer.setRenderTarget(null); renderer.render(scene, cam);
    },
    setStrength(v) { comp.uniforms.amt.value = v; },
  };
}

/* ---------------------------------------------------------------- element */

const CSS = `
:host{display:block;position:relative;width:100%;height:100%;background:#07090d;
  font-family:ui-sans-serif,-apple-system,"Helvetica Neue",Helvetica,Arial,sans-serif;overflow:hidden}
canvas{display:block;width:100%;height:100%}
.rail{position:absolute;top:14px;right:14px;width:236px;max-height:calc(100% - 28px);overflow:auto;
  display:flex;flex-direction:column;gap:12px;padding:14px;border-radius:12px;
  background:rgba(9,12,17,0.74);backdrop-filter:blur(14px);
  border:1px solid rgba(255,255,255,0.09);box-shadow:0 18px 50px rgba(0,0,0,0.5);
  color:#e8ecf2;font-size:11px;letter-spacing:0.02em;scrollbar-width:thin}
.rail::-webkit-scrollbar{width:6px}.rail::-webkit-scrollbar-thumb{background:rgba(255,255,255,.16);border-radius:3px}
.grp{display:flex;flex-direction:column;gap:7px}
.hd{font:600 9px/1 ui-monospace,Menlo,monospace;letter-spacing:.18em;color:#7f8b9e;text-transform:uppercase}
.row{display:flex;gap:5px;flex-wrap:wrap}
button{flex:1 1 auto;min-width:44px;padding:6px 6px;border-radius:6px;cursor:pointer;
  background:rgba(255,255,255,0.05);color:#c9d2de;border:1px solid rgba(255,255,255,0.1);
  font:600 9.5px/1.25 ui-monospace,Menlo,monospace;letter-spacing:.06em;transition:background .15s,color .15s}
button:hover{background:rgba(255,255,255,0.12);color:#fff}
button[data-on="1"]{background:#f0742a;border-color:#f0742a;color:#120904}
label{display:flex;flex-direction:column;gap:4px}
.lb{display:flex;justify-content:space-between;color:#93a0b2;font:500 10px/1 ui-sans-serif}
.lb b{color:#f4b183;font:600 10px/1 ui-monospace,Menlo,monospace}
input[type=range]{-webkit-appearance:none;appearance:none;height:3px;border-radius:2px;
  background:rgba(255,255,255,0.16);outline:none;margin:3px 0}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:12px;height:12px;border-radius:50%;
  background:#f0742a;cursor:pointer;border:2px solid #10141b;box-shadow:0 0 0 1px rgba(240,116,42,.5)}
.hud{position:absolute;left:16px;top:16px;color:#dfe6ef;pointer-events:none;
  text-shadow:0 1px 10px rgba(0,0,0,.8)}
.hud h1{margin:0;font:600 13px/1.2 ui-monospace,Menlo,monospace;letter-spacing:.22em;color:#fff}
.hud p{margin:5px 0 0;font:500 10.5px/1.6 ui-monospace,Menlo,monospace;color:#8d9aad;letter-spacing:.06em}
.hud em{color:#f4b183;font-style:normal}
.tag{position:absolute;left:16px;bottom:14px;color:#5d6878;font:500 9.5px/1.4 ui-monospace,Menlo,monospace;
  letter-spacing:.1em;pointer-events:none}
`;

class BoosterStage extends HTMLElement {
  static observedAttributes = ['hide-rail', 'preset', 'cam'];

  attributeChangedCallback(n, _o, v) {
    if (!this._up) return;
    if (n === 'hide-rail') this.rail.style.display = (v === '1' || v === 'true') ? 'none' : '';
    if (n === 'preset' && v && PRESETS[v] && v !== this.S.preset) {
      this.applyPreset(v);
    }
    if (n === 'cam' && v && CAMS[v]) this.setCam(v);
  }

  connectedCallback() {
    if (this._up) return;
    this._up = true;
    const root = this.attachShadow({ mode: 'open' });
    root.innerHTML = `<style>${CSS}</style>
      <canvas></canvas>
      <div class="hud"><h1>KESTREL&#8209;9</h1><p id="ro"></p></div>
      <div class="tag">PROCEDURAL · 47.7 m × 3.66 m · DRIVEN BY TLM FIELDS</div>
      <div class="rail"></div>`;
    this.canvas = root.querySelector('canvas');
    this.rail = root.querySelector('.rail');
    this.ro = root.querySelector('#ro');
    this.init();
  }

  disconnectedCallback() { cancelAnimationFrame(this._raf); this._obs?.disconnect(); }

  init() {
    const renderer = this.renderer = new THREE.WebGLRenderer({ canvas: this.canvas, antialias: false, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
    renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 2));
    renderer.toneMapping = THREE.AgXToneMapping;     // §11.1
    renderer.toneMappingExposure = 1.0;
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFShadowMap;

    const scene = this.scene = new THREE.Scene();
    const camera = this.camera = new THREE.PerspectiveCamera(30, 1, 0.2, 24000);
    this.sky = makeSky(); scene.add(this.sky);

    this.sun = new THREE.DirectionalLight(0xfff4e2, 4);
    this.sun.castShadow = true;
    const sc = this.sun.shadow;
    sc.mapSize.set(2048, 2048);
    sc.camera.near = 1; sc.camera.far = 420;
    sc.camera.left = -46; sc.camera.right = 46; sc.camera.top = 70; sc.camera.bottom = -20;
    sc.bias = -0.0006; sc.normalBias = 0.05;
    scene.add(this.sun, this.sun.target);
    this.hemi = new THREE.HemisphereLight(0xbcd0e4, 0x6b6152, 0.5);
    scene.add(this.hemi);
    // camera-side bounce fill — keeps the white paint reading as paint, not silhouette
    this.fill = new THREE.DirectionalLight(0xbfd2e8, 0.45);
    this.fill.position.set(70, 22, 46);
    scene.add(this.fill);

    // ground + pad
    const gmat = new THREE.MeshStandardMaterial({ map: scrubTexture(), roughness: 0.95, metalness: 0, color: 0xb8b0a0 });
    const ground = new THREE.Mesh(new THREE.PlaneGeometry(9000, 9000), gmat);
    ground.rotation.x = -Math.PI / 2; ground.position.y = -0.35; ground.receiveShadow = true; ground.name = 'Scrub';
    scene.add(ground);
    const pt = padTextures();
    // raised concrete slab: top face at y=0 (feet rest on it), ground 35 cm below -> no coplanar z-fight
    const pad = new THREE.Mesh(new THREE.CylinderGeometry(30, 30.6, 0.6, 128),
      new THREE.MeshStandardMaterial({ map: pt.map, roughnessMap: pt.roughnessMap, roughness: 1, metalness: 0, color: 0xc9c6bd }));
    pad.position.y = -0.3; pad.receiveShadow = true; pad.castShadow = true; pad.name = 'Pad';
    scene.add(pad);
    this.padMat = pad.material;

    // ground impingement: the wall-jet sheet the plume paints on the slab
    const discTex = blobDisc();
    this.jetDisc = new THREE.Mesh(new THREE.CircleGeometry(1, 64), new THREE.MeshBasicMaterial({
      map: discTex, color: 0xffa54a, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false,
    }));
    this.jetDisc.rotation.x = -Math.PI / 2; this.jetDisc.position.y = 0.04; this.jetDisc.name = 'WallJetGlow';
    scene.add(this.jetDisc);

    // site dressing: scattered rock/scrub relief + a distant hangar block for scale
    const rockMat = new THREE.MeshStandardMaterial({ color: 0x7c7465, roughness: 0.94, metalness: 0 });
    const rocks = new THREE.InstancedMesh(new THREE.IcosahedronGeometry(1, 0), rockMat, 420);
    rocks.name = 'SiteRocks';
    const m4 = new THREE.Matrix4(), q4 = new THREE.Quaternion(), v3 = new THREE.Vector3(), s3 = new THREE.Vector3();
    for (let i = 0; i < 420; i++) {
      const a = Math.random() * TAU, rr = 34 + Math.pow(Math.random(), 0.6) * 420;
      const sc = 0.35 + Math.random() * 1.9;
      v3.set(Math.cos(a) * rr, -0.35 + sc * 0.12, Math.sin(a) * rr);
      q4.setFromAxisAngle(new THREE.Vector3(0, 1, 0), Math.random() * TAU);
      s3.set(sc * (0.7 + Math.random()), sc * (0.35 + Math.random() * 0.4), sc * (0.7 + Math.random()));
      rocks.setMatrixAt(i, m4.compose(v3, q4, s3));
    }
    rocks.castShadow = rocks.receiveShadow = true;
    scene.add(rocks);

    const hangarMat = new THREE.MeshStandardMaterial({ color: 0x6d6f72, roughness: 0.82, metalness: 0.1 });
    const hangar = new THREE.Group(); hangar.name = 'SiteHangar';
    const hb = new THREE.Mesh(new THREE.BoxGeometry(74, 19, 34), hangarMat);
    hb.position.set(-360, 9.5, -520); hb.rotation.y = 0.4;
    const hb2 = new THREE.Mesh(new THREE.BoxGeometry(26, 11, 20), hangarMat);
    hb2.position.set(-292, 5.5, -474); hb2.rotation.y = 0.4;
    const tower = new THREE.Mesh(new THREE.CylinderGeometry(1.1, 1.4, 42, 10), hangarMat);
    tower.position.set(-150, 21, -300);
    hangar.add(hb, hb2, tower);
    scene.add(hangar);

    // pad floodlights (night preset)
    this.floods = [];
    for (let i = 0; i < 4; i++) {
      const a = (i / 4) * TAU + Math.PI / 4;
      const s = new THREE.SpotLight(0xcfe0ff, 0, 220, 0.5, 0.6, 1.4);
      s.position.set(Math.cos(a) * 46, 26, Math.sin(a) * 46);
      s.target.position.set(0, 12, 0);
      scene.add(s, s.target);
      this.floods.push(s);
    }

    // the vehicle
    const booster = this.booster = createKestrel9(THREE, { quality: 'ultra' });
    scene.add(booster.group);
    // engine-bay bounce so the nine bells read instead of going to silhouette
    this.bayFill = new THREE.PointLight(0xa8b8cc, 55, 16, 2);
    this.bayFill.position.set(0, -0.5, 0);
    booster.group.add(this.bayFill);
    this.plume = createPlume(THREE, booster);

    // wall-jet dust
    const N = 900;
        this.dust = { n: N, pos: new Float32Array(N * 3), life: new Float32Array(N), vel: new Float32Array(N * 3) };
    const dg = new THREE.BufferGeometry();
    dg.setAttribute('position', new THREE.BufferAttribute(this.dust.pos, 3));
    this.dustPts = new THREE.Points(dg, new THREE.PointsMaterial({
      map: dustSprite(), size: 2.1, sizeAttenuation: true, transparent: true,
      depthWrite: false, opacity: 0.34, blending: THREE.NormalBlending, color: 0xbfae95,
    }));
    this.dustPts.name = 'WallJetDust';
    this.dustPts.frustumCulled = false;
    scene.add(this.dustPts);
    for (let i = 0; i < N; i++) this.dust.life[i] = -1;

    this.pmrem = new THREE.PMREMGenerator(renderer);
    this.bloom = makeBloom(renderer);

    this.S = { ...PRESETS.touchdown, preset: 'touchdown', gimbalX: 0, gimbalY: 0, exposure: 1.02, bloom: 0.42, auto: true };
    let saved = null;
    try { saved = JSON.parse(localStorage.getItem(LS) || 'null'); } catch (e) { }
    if (saved && PRESETS[saved.preset]) Object.assign(this.S, PRESETS[saved.preset], { preset: saved.preset }, saved.over || {});

    this.buildRail();
    if (this.getAttribute('hide-rail') === '1') this.rail.style.display = 'none';
    const ap = this.getAttribute('preset');
    if (ap && PRESETS[ap]) Object.assign(this.S, PRESETS[ap], { preset: ap });
    this.applySky(this.S.sky);
    this.setCam(this.getAttribute('cam') || saved?.cam || this.S.cam, true);
    this.push();

    this._obs = new ResizeObserver(() => this.resize());
    this._obs.observe(this);
    this.resize();
    this.initControls();
    this.t0 = performance.now() / 1000;
    this.tPrev = this.t0;
    this.loop();
  }

  /* ---- ui ---- */
  buildRail() {
    const mk = (tag, cls, txt) => { const e = document.createElement(tag); if (cls) e.className = cls; if (txt) e.textContent = txt; return e; };
    const grp = (title) => { const g = mk('div', 'grp'); g.append(mk('div', 'hd', title)); this.rail.append(g); return g; };
    const btnRow = (parent, items, active, cb) => {
      const r = mk('div', 'row');
      const bs = items.map(([k, label]) => {
        const b = mk('button', null, label);
        b.dataset.on = k === active ? '1' : '0';
        b.dataset.key = k;
        b.onclick = () => { bs.forEach(x => x.dataset.on = '0'); b.dataset.on = '1'; cb(k); };
        r.append(b); return b;
      });
      parent.append(r); return bs;
    };
    const slider = (parent, label, key, min, max, step, fmt) => {
      const l = mk('label'), h = mk('div', 'lb'), n = mk('span', null, label), v = mk('b');
      const i = document.createElement('input');
      i.type = 'range'; i.min = min; i.max = max; i.step = step; i.value = this.S[key];
      const show = () => v.textContent = fmt ? fmt(+i.value) : (+i.value).toFixed(2);
      show();
      i.oninput = () => { this.S[key] = +i.value; show(); this.push(); this.save(); };
      h.append(n, v); l.append(h, i); parent.append(l);
      return { input: i, show };
    };

    const g1 = grp('Flight phase');
    this.presetBtns = btnRow(g1, [['entry', 'ENTRY'], ['aero', 'AERO'], ['burn', 'BURN'], ['touchdown', 'T/D'], ['landed', 'LANDED'], ['factory', 'CLEAN'], ['static', '9-ENG']],
      this.S.preset, k => this.applyPreset(k));

    const g2 = grp('Camera');
    this.camBtns = btnRow(g2, [['hero', 'HERO'], ['engines', 'OCTAWEB'], ['fins', 'FINS'], ['legs', 'LEGS'], ['wide', 'WIDE']],
      this.S.cam, k => this.setCam(k));

    const g3 = grp('Light');
    this.skyBtns = btnRow(g3, [['noon', 'NOON'], ['dusk', 'DUSK'], ['night', 'NIGHT'], ['high', '60 KM']],
      this.S.sky, k => { this.S.sky = k; this.applySky(k); this.save(); });

    const g4 = grp('Actuators · TLM fields');
    this.sl = {};
    this.sl.throttle = slider(g4, 'throttle_act', 'throttle', 0, 1, 0.01, v => (v * 100).toFixed(0) + '%');
    this.sl.nEng = slider(g4, 'n_eng', 'nEng', 0, 9, 1, v => v.toFixed(0));
    this.sl.gimbalX = slider(g4, 'gimbal_act[0]', 'gimbalX', -5, 5, 0.1, v => v.toFixed(1) + '°');
    this.sl.gimbalY = slider(g4, 'gimbal_act[1]', 'gimbalY', -5, 5, 0.1, v => v.toFixed(1) + '°');
    this.sl.deploy = slider(g4, 'deploy_frac', 'deploy', 0, 1, 0.01, v => (v * 100).toFixed(0) + '%');
    this.sl.soot = slider(g4, 'Q_heat → soot', 'soot', 0, 1, 0.01, v => (v * 100).toFixed(0) + '%');
    this.sl.frost = slider(g4, 'LOX frost', 'frost', 0, 1, 0.01, v => (v * 100).toFixed(0) + '%');

    const g5 = grp('Render');
    this.sl.exposure = slider(g5, 'exposure', 'exposure', 0.4, 2, 0.01);
    this.sl.bloom = slider(g5, 'bloom', 'bloom', 0, 1.6, 0.01);
    const r = document.createElement('div'); r.className = 'row';
    const ab = document.createElement('button');
    ab.textContent = 'LIVE MOTION'; ab.dataset.on = this.S.auto ? '1' : '0';
    ab.onclick = () => { this.S.auto = !this.S.auto; ab.dataset.on = this.S.auto ? '1' : '0'; };
    const fb = document.createElement('button');
    fb.textContent = 'TEA-TEB'; fb.dataset.on = '0';
    fb.onclick = () => this.plume.flash();
    r.append(ab, fb); g5.append(r);

    const g6 = grp('Export · STL');
    this.printScale = 200;
    const sr = document.createElement('div'); sr.className = 'row';
    const scaleBtns = [[100, '1:100'], [144, '1:144'], [200, '1:200']].map(([k, label]) => {
      const b = document.createElement('button');
      b.textContent = label; b.dataset.on = k === this.printScale ? '1' : '0'; b.dataset.key = k;
      b.onclick = () => { this.printScale = k; scaleBtns.forEach(x => x.dataset.on = x === b ? '1' : '0'); };
      sr.append(b); return b;
    });
    g6.append(sr);
    const er = document.createElement('div'); er.className = 'row';
    const pb = document.createElement('button');
    pb.textContent = 'PRINT · MM'; pb.dataset.on = '0';
    pb.onclick = () => this.exportSTL('print');
    const cb = document.createElement('button');
    cb.textContent = 'CFD · 1:1 M'; cb.dataset.on = '0';
    cb.onclick = () => this.exportSTL('cfd');
    er.append(pb, cb); g6.append(er);
    const note = document.createElement('div');
    note.style.cssText = 'font:400 9px/1.45 ui-monospace,Menlo,monospace;color:#5d6878;letter-spacing:.04em';
    note.textContent = 'Closed solids, Z-up. CFD = metres for FluidX3D read_stl(); PRINT drops sub-0.5 mm parts.';
    g6.append(note);
  }

  exportSTL(variant = 'cfd') {
    const r = exportSTL(THREE, this.booster, {
      variant, scale: variant === 'print' ? 1000 / this.printScale : 1,
    });
    this.ro.innerHTML += `<br><em>${r.name}</em> · ${r.triangles.toLocaleString()} tris · ${(r.bytes / 1048576).toFixed(1)} MB`;
    return r;
  }

  syncRail() {
    for (const k in this.sl) {
      if (!this.sl[k]) continue;
      this.sl[k].input.value = this.S[k];
      this.sl[k].show();
    }
    this.syncRow(this.presetBtns, this.S.preset);
    this.syncRow(this.camBtns, this.S.cam);
    this.syncRow(this.skyBtns, this.S.sky);
  }

  syncRow(btns, active) {
    btns?.forEach(b => b.dataset.on = b.dataset.key === active ? '1' : '0');
  }

  applyPreset(k) {
    Object.assign(this.S, PRESETS[k], { preset: k });
    this.S.gimbalX = 0; this.S.gimbalY = 0;
    this.applySky(this.S.sky);
    this.setCam(this.S.cam);
    this.syncRail();
    if (k === 'entry' || k === 'burn' || k === 'static') this.plume.flash();
    this.push();
    this.save();
  }

  applySky(key) {
    const s = SKIES[key] || SKIES.dusk;
    this.S.sky = key;
    this.syncRow(this.skyBtns, key);
    const u = this.sky.material.uniforms;
    u.uZenith.value.set(s.zenith); u.uHorizon.value.set(s.horizon);
    u.uGround.value.set(s.ground); u.uSunCol.value.set(s.sun);
    u.uTurb.value = s.turb;
    u.uStars.value = key === 'night' ? 0.9 : key === 'high' ? 1.0 : 0;
    const el = s.elev * D2R, az = s.az * D2R;
    const dir = new THREE.Vector3(Math.cos(el) * Math.cos(az), Math.sin(el), Math.cos(el) * Math.sin(az));
    u.uSunDir.value.copy(dir);
    this.sun.position.copy(dir).multiplyScalar(200);
    this.sun.target.position.set(0, 20, 0);
    this.sun.color.set(s.sun);
    this.sun.intensity = s.sunI;
    this.hemi.color.set(s.horizon); this.hemi.groundColor.set(s.ground);
    this.hemi.intensity = s.amb;
    this.scene.fog = new THREE.FogExp2(s.fog, s.fogD);
    this.floods.forEach(f => f.intensity = key === 'night' ? 900 : 0);
    this.fill.intensity = key === 'night' ? 0.1 : key === 'high' ? 0.3 : 0.45;
    this.fill.color.set(key === 'dusk' ? 0xd8c3b0 : 0xbfd2e8);
    // env map for IBL — metals must see the same sky they're lit by
    const eq = skyEquirect(s);
    const env = this.pmrem.fromEquirectangular(eq).texture;
    this.scene.environment = env;
    this.scene.environmentIntensity = key === 'night' ? 0.35 : 1;
    this._env?.dispose?.();
    this._env = env;
    eq.dispose();
    this.padMat.color.set(key === 'night' ? 0x8d8b84 : 0xc9c6bd);
  }

  setCam(k, snap) {
    const c = CAMS[k] || CAMS.hero;
    this.S.cam = k;
    this.syncRow(this.camBtns, k);
    this.camBase = c;
    this.target = this.target || new THREE.Vector3();
    this.tgtWant = new THREE.Vector3(c.tgt[0], c.tgt[1] + this.altOff(), c.tgt[2]);
    const p = new THREE.Vector3(...c.pos);
    const off = p.clone().sub(new THREE.Vector3(...c.tgt));
    this.orbWant = { r: off.length(), th: Math.atan2(off.x, off.z), ph: Math.acos(THREE.MathUtils.clamp(off.y / off.length(), -1, 1)) };
    this.camera.fov = c.fov;
    this.camera.updateProjectionMatrix();
    if (snap || !this.orb) { this.orb = { ...this.orbWant }; this.target.copy(this.tgtWant); }
    this.save();
  }

  altOff() { return Math.max(0, (this.S?.y ?? 3.6) - 3.6); }

  refreshTarget() {
    if (!this.camBase || !this.tgtWant) return;
    this.tgtWant.y = this.camBase.tgt[1] + this.altOff();
  }

  push() {
    const S = this.S;
    const tlm = {
      throttle_act: S.throttle, n_eng: Math.round(S.nEng),
      gimbal_act: [S.gimbalX * D2R, S.gimbalY * D2R],
      fins_act: (S.fins || [0, 0, 0, 0]).map(d => d * D2R),
      deploy_frac: S.deploy, soot: S.soot, frost: S.frost,
      stroke: S.preset === 'landed' ? [0.31, 0.28, 0.33, 0.3] : [0, 0, 0, 0],
      p_amb: S.pAmb, mach: S.mach, qbar: S.qbar,
      bell_alt: Math.max(0.05, S.y - 1.62),
    };
    this.booster.setTelemetry(tlm);
    this.plume.setTelemetry(tlm);
    this.booster.group.position.y = S.y;
    this.booster.group.rotation.z = -S.tilt * D2R;
    this.booster.group.rotation.x = S.tilt * 0.35 * D2R;
    this.refreshTarget();
    this.renderer.toneMappingExposure = S.exposure;
    this.bloom.setStrength(S.bloom);
    const alt = Math.max(0, S.y - 3.6);
    this.ro.innerHTML =
      `PHASE <em>${PRESETS[S.preset].label}</em><br>` +
      `ALT <em>${alt < 1 ? '0' : alt.toFixed(0)} m</em> · MACH <em>${S.mach.toFixed(2)}</em><br>` +
      `THR <em>${(S.throttle * 100).toFixed(0)}%</em> × <em>${Math.round(S.nEng)}</em> ENG · LEGS <em>${(S.deploy * 100).toFixed(0)}%</em>`;
  }

  save() {
    try {
      localStorage.setItem(LS, JSON.stringify({
        preset: this.S.preset, cam: this.S.cam,
        over: { throttle: this.S.throttle, nEng: this.S.nEng, deploy: this.S.deploy, soot: this.S.soot, sky: this.S.sky, exposure: this.S.exposure, bloom: this.S.bloom },
      }));
    } catch (e) { }
  }

  /* ---- orbit ---- */
  initControls() {
    const el = this.canvas;
    let down = false, lx = 0, ly = 0;
    el.style.touchAction = 'none';
    el.addEventListener('pointerdown', e => { down = true; lx = e.clientX; ly = e.clientY; el.setPointerCapture(e.pointerId); });
    el.addEventListener('pointerup', e => { down = false; el.releasePointerCapture?.(e.pointerId); });
    el.addEventListener('pointermove', e => {
      if (!down) return;
      const dx = e.clientX - lx, dy = e.clientY - ly; lx = e.clientX; ly = e.clientY;
      this.orbWant.th -= dx * 0.005;
      this.orbWant.ph = THREE.MathUtils.clamp(this.orbWant.ph - dy * 0.004, 0.06, Math.PI * 0.495);
    });
    el.addEventListener('wheel', e => {
      e.preventDefault();
      this.orbWant.r = THREE.MathUtils.clamp(this.orbWant.r * Math.pow(1.0016, e.deltaY), 4, 900);
    }, { passive: false });
  }

  resize() {
    const w = this.clientWidth || 960, h = this.clientHeight || 600;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    this.bloom.setSize(w, h, dpr);
  }

  loop = () => {
    this._raf = requestAnimationFrame(this.loop);
    const now = performance.now() / 1000;
    const dt = Math.min(0.05, now - this.tPrev), t = now - this.t0;
    this.tPrev = now;

    // camera easing
    const o = this.orb, w = this.orbWant;
    o.r += (w.r - o.r) * Math.min(1, dt * 6);
    o.th += (w.th - o.th) * Math.min(1, dt * 8);
    o.ph += (w.ph - o.ph) * Math.min(1, dt * 8);
    this.target.lerp(this.tgtWant, Math.min(1, dt * 4));
    this.camera.position.set(
      this.target.x + o.r * Math.sin(o.ph) * Math.sin(o.th),
      this.target.y + o.r * Math.cos(o.ph),
      this.target.z + o.r * Math.sin(o.ph) * Math.cos(o.th));
    this.camera.lookAt(this.target);

    // live motion: gimbal dither + fin articulation + attitude breathing
    if (this.S.auto) {
      const S = this.S, burn = S.throttle > 0.03;
      const gx = burn ? Math.sin(t * 1.7) * 1.1 + Math.sin(t * 5.3) * 0.28 : 0;
      const gy = burn ? Math.cos(t * 1.25) * 0.9 + Math.cos(t * 4.1) * 0.22 : 0;
      const fw = (S.preset === 'aero' || S.preset === 'entry') ? 3.2 : 0.7;
      this.booster.setTelemetry({
        gimbal_act: [(S.gimbalX + gx) * D2R, (S.gimbalY + gy) * D2R],
        fins_act: (S.fins || [0, 0, 0, 0]).map((d, i) => (d + Math.sin(t * (1.3 + i * 0.21) + i) * fw) * D2R),
      });
      this.booster.group.rotation.z = (-S.tilt + Math.sin(t * 0.7) * 0.25) * D2R;
      this.booster.group.position.y = S.y + (S.y > 10 ? Math.sin(t * 0.45) * 0.8 : 0);
    }

    this.plume.update(dt);
    this.stepDust(dt);
    // wall-jet glow on the slab: brightness ~ thrust / h^2, radius grows as the jet is squashed
    {
      const S = this.S, h = Math.max(0.4, S.y - 1.62);
      const power = S.throttle > 0.02 && S.nEng > 0 ? S.throttle * Math.max(1, Math.round(S.nEng)) : 0;
      const k = Math.min(3, power) * Math.min(1, 30 / (h * h));
      const flick = 1 + 0.08 * Math.sin(t * 43) + 0.05 * Math.sin(t * 71 + 2);
      this.jetDisc.visible = k > 0.005 && S.y < 60;
      this.jetDisc.material.opacity = Math.min(0.6, k * 0.28) * flick;
      const rad = (2.6 + 3.2 * Math.min(1.5, k) + 0.9 * Math.min(1, 12 / h)) * flick;
      this.jetDisc.scale.set(rad, rad, 1);
      this.jetDisc.position.x = this.booster.group.position.x;
      this.jetDisc.position.z = this.booster.group.position.z;
    }
    this.sky.position.copy(this.camera.position);

    if (this.bloom.enabled) {
      this.renderer.setRenderTarget(this.bloom.rtScene);
      this.renderer.render(this.scene, this.camera);
      this.bloom.render(t);
    } else {
      this.renderer.setRenderTarget(null);
      this.renderer.render(this.scene, this.camera);
    }
  };

  /* radial wall jet: intensity ~ thrust/h^2, Görtler-ish azimuthal banding */
  stepDust(dt) {
    const d = this.dust, S = this.S;
    const h = Math.max(0.6, S.y - 3.4);
    const power = S.throttle * Math.max(1, S.nEng) * (S.dust || 0) * Math.min(1, 40 / (h * h));
    this.dustPts.material.opacity = Math.min(0.34, 0.06 + power * 0.5);
    let spawn = power * 260 * dt;
    for (let i = 0; i < d.n; i++) {
      const j = i * 3;
      if (d.life[i] <= 0) {
        if (spawn > 0 && Math.random() < 0.5) {
          spawn--;
          const a = Math.random() * TAU, band = 1 + 0.5 * Math.sin(a * 9);
          const r0 = 2.2 + Math.random() * 3.4;
          d.pos[j] = Math.cos(a) * r0; d.pos[j + 1] = 0.25 + Math.random() * 0.5; d.pos[j + 2] = Math.sin(a) * r0;
          const sp = (9 + Math.random() * 16) * band;
          d.vel[j] = Math.cos(a) * sp; d.vel[j + 1] = 1.0 + Math.random() * 3.2;
          d.vel[j + 2] = Math.sin(a) * sp;
          d.life[i] = 1.4 + Math.random() * 2.2;
        } else { d.pos[j + 1] = -9999; continue; }
      }
      d.life[i] -= dt;
      d.vel[j] *= 1 - 1.5 * dt; d.vel[j + 2] *= 1 - 1.5 * dt;
      d.vel[j + 1] += (1.2 - d.vel[j + 1] * 0.6) * dt;
      d.pos[j] += d.vel[j] * dt;
      d.pos[j + 1] += d.vel[j + 1] * dt;
      d.pos[j + 2] += d.vel[j + 2] * dt;
      if (d.life[i] <= 0) d.pos[j + 1] = -9999;
    }
    this.dustPts.geometry.attributes.position.needsUpdate = true;
  }
}

if (!customElements.get('booster-stage')) customElements.define('booster-stage', BoosterStage);
export { BoosterStage };
