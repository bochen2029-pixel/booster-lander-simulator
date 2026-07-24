// screenshot.ts — in-app frame capture (renderer-only; never crosses the telemetry
// boundary, canon §B.6). Renders the LIVE scene+camera into an offscreen RenderTarget and
// reads the pixels back, so it is backend-agnostic: it works on the WebGPU path AND the
// WebGL2 fallback, and does NOT depend on `canvas.toDataURL` (which returns blank on a
// WebGL2 canvas without preserveDrawingBuffer, and is unreliable on a WebGPU swap-chain).
//
//   Hotkey P                     — capture at display resolution → save (Tauri) / download.
//   window.__shot(w?,h?,q?)      — (DEV) capture the live camera → JPEG data URL.
//   window.__shotPose(eye,tgt,…) — (DEV) capture from an arbitrary pose (no telemetry needed).
//
// COLOR: rendering to a RenderTarget writes LINEAR working color (three applies tone-map +
// sRGB only when rendering to the canvas). We re-encode linear→sRGB in JS on readback so the
// saved image matches display for LDR content (the plume's HDR highlights simply clip white).

import {
  RenderTarget,
  PerspectiveCamera,
  Vector3,
  UnsignedByteType,
  HalfFloatType,
  LinearFilter,
} from "three/webgpu";
import type { WebGPURenderer, Scene } from "three/webgpu";
import { invoke } from "../shell/tauriBridge"; // null-safe Tauri invoke (no-ops in a plain browser)

export interface ShotHandle {
  /** Capture the LIVE camera → JPEG data URL. */
  capture(w?: number, h?: number, quality?: number): Promise<string>;
}

/** linear [0..1] → sRGB [0..255], the OETF the canvas output applies for us. */
function linToSrgb8(c: number): number {
  c = c < 0 ? 0 : c > 1 ? 1 : c;
  const s = c <= 0.0031308 ? 12.92 * c : 1.055 * Math.pow(c, 1 / 2.4) - 0.055;
  return Math.round(s * 255);
}

export function installScreenshot(
  renderer: WebGPURenderer,
  scene: Scene,
  getCamera: () => PerspectiveCamera
): ShotHandle {
  let shotN = 0;

  // RT CACHE: a fresh RenderTarget per capture makes the WebGPU renderer re-prepare
  // render state each time (new target = new renderContext), which re-opens the
  // async-pipeline white-placeholder window. Reuse one RT per (size, type).
  const rtCache = new Map<string, RenderTarget>();
  function getRT(W: number, H: number, half: boolean): RenderTarget {
    const key = `${W}x${H}:${half ? "h" : "b"}`;
    let rt = rtCache.get(key);
    if (!rt) {
      rt = new RenderTarget(W, H, {
        type: half ? HalfFloatType : UnsignedByteType,
        minFilter: LinearFilter,
        magFilter: LinearFilter,
        depthBuffer: true,
      });
      rtCache.set(key, rt);
    }
    return rt;
  }

  async function captureWith(
    cam: PerspectiveCamera,
    w: number,
    h: number,
    quality: number,
    maxOut?: number
  ): Promise<string> {
    const W = Math.max(16, Math.round(w));
    const H = Math.max(16, Math.round(h));

    const rt = getRT(W, H, false);

    const prevAspect = cam.aspect;
    cam.aspect = W / H;
    cam.updateProjectionMatrix();

    const prevTarget = renderer.getRenderTarget();
    // PIPELINE WARM-UP: r185 WebGPU builds pipelines ASYNC, and the pipeline variant is
    // keyed to the OUTPUT TARGET FORMAT — an offscreen RenderTarget is a different format
    // than the canvas, so a one-shot RT render draws WHITE PLACEHOLDERS for any material
    // whose RT-variant pipeline is cold (the "unlit white mesh" capture artifact). Bind
    // the RT FIRST so compileAsync keys the right variants, render once to flush the
    // remainder, give the async compiler a beat, then render the real frame.
    renderer.setRenderTarget(rt);
    await renderer.compileAsync(scene, cam);
    await renderer.renderAsync(scene, cam);
    await new Promise((r) => setTimeout(r, 120));
    await renderer.renderAsync(scene, cam);
    // WebGPU readback RETURNS the pixel buffer (bottom-up RGBA, linear working color).
    const raw = (await renderer.readRenderTargetPixelsAsync(
      rt,
      0,
      0,
      W,
      H
    )) as Uint8Array;
    renderer.setRenderTarget(prevTarget);

    cam.aspect = prevAspect;
    cam.updateProjectionMatrix();

    // RT pixels are LINEAR and bottom-up; re-encode to sRGB and flip Y into a 2D canvas.
    // WebGPU readback pads each row up to a 256-byte multiple, so the SOURCE row stride can
    // exceed W*4 (that padding caused a diagonal-shear artifact when assumed tight). Derive
    // the real stride from the returned buffer length; the destination stays tightly packed.
    // WebGPU pads each readback row up to a 256-BYTE multiple (the WebGL2 backend returns
    // tight packing instead). Detect padding by length and use the 256-aligned source stride
    // — an off-by-one here shears the image diagonally.
    const dstRow = W * 4;
    const srcRow = raw.length > W * H * 4 ? Math.ceil(dstRow / 256) * 256 : dstRow;
    const cv = document.createElement("canvas");
    cv.width = W;
    cv.height = H;
    const ctx = cv.getContext("2d")!;
    const img = ctx.createImageData(W, H);
    const out = img.data;
    for (let y = 0; y < H; y++) {
      // readRenderTargetPixelsAsync returns rows TOP-DOWN here — copy straight (a Y-flip
      // was inverting every capture: sky at the bottom, rocket nose-down).
      const src = y * srcRow;
      const dst = y * dstRow;
      for (let x = 0; x < dstRow; x += 4) {
        out[dst + x] = linToSrgb8(raw[src + x] / 255);
        out[dst + x + 1] = linToSrgb8(raw[src + x + 1] / 255);
        out[dst + x + 2] = linToSrgb8(raw[src + x + 2] / 255);
        out[dst + x + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);

    // Optional downscale so a headless caller can return a small inline data URL
    // ("capture then resize") without tripping the eval token cap.
    if (maxOut && Math.max(W, H) > maxOut) {
      const s = maxOut / Math.max(W, H);
      const sc = document.createElement("canvas");
      sc.width = Math.round(W * s);
      sc.height = Math.round(H * s);
      sc.getContext("2d")!.drawImage(cv, 0, 0, sc.width, sc.height);
      return sc.toDataURL("image/jpeg", quality);
    }
    return cv.toDataURL("image/jpeg", quality);
  }

  async function capture(
    w?: number,
    h?: number,
    quality = 0.9,
    maxOut?: number
  ): Promise<string> {
    return captureWith(
      getCamera(),
      w ?? window.innerWidth,
      h ?? window.innerHeight,
      quality,
      maxOut
    );
  }

  // ---- DEV tone-mapped HDR capture (representative of the on-canvas AgX look) ----
  // The plain capture above renders LINEAR to a UnsignedByte RT (HDR pre-clips to white),
  // so it does NOT match the tone-mapped canvas. This variant renders to a HalfFloat RT so
  // HDR survives, then applies exposure + a filmic (ACES) rolloff + sRGB in JS. Not pixel-
  // exact AgX, but faithful enough to tune materials/lighting/exposure/composition against.
  function halfToFloat(h: number): number {
    const s = (h & 0x8000) >> 15, e = (h & 0x7c00) >> 10, f = h & 0x03ff;
    if (e === 0) return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
    if (e === 31) return f ? NaN : (s ? -1 : 1) * Infinity;
    return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
  }
  // Faithful port of three's AgXToneMapping (Filament/Blender, Rec2020) so captures match the
  // on-canvas look exactly. In: linear-sRGB × exposure. Out: linear-sRGB (caller applies OETF).
  function m3(c: number[], r: number, g: number, b: number, o: number): number {
    return c[o] * r + c[o + 3] * g + c[o + 6] * b;
  }
  const SRGB_2020 = [0.6274, 0.0691, 0.0164, 0.3293, 0.9195, 0.088, 0.0433, 0.0113, 0.8956];
  const AGX_IN = [0.856627153315983, 0.137318972929847, 0.11189821299995, 0.0951212405381588, 0.761241990602591, 0.0767994186031903, 0.0482516061458583, 0.101439036467562, 0.811302368396859];
  const AGX_OUT = [1.1271005818144368, -0.1413297634984383, -0.14132976349843826, -0.11060664309660323, 1.157823702216272, -0.11060664309660294, -0.016493938717834573, -0.016493938717834257, 1.2519364065950405];
  const R2020_SRGB = [1.6605, -0.1246, -0.0182, -0.5876, 1.1329, -0.1006, -0.0728, -0.0083, 1.1187];
  const AGX_MIN = -12.47393, AGX_MAX = 4.026069;
  function agxCurve(x: number): number {
    const x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
  }
  function agx(r: number, g: number, b: number, exp: number): [number, number, number] {
    r *= exp; g *= exp; b *= exp;
    let R = m3(SRGB_2020, r, g, b, 0), G = m3(SRGB_2020, r, g, b, 1), B = m3(SRGB_2020, r, g, b, 2);
    let Ri = m3(AGX_IN, R, G, B, 0), Gi = m3(AGX_IN, R, G, B, 1), Bi = m3(AGX_IN, R, G, B, 2);
    const enc = (v: number) => Math.min(1, Math.max(0, (Math.log2(Math.max(v, 1e-10)) - AGX_MIN) / (AGX_MAX - AGX_MIN)));
    Ri = agxCurve(enc(Ri)); Gi = agxCurve(enc(Gi)); Bi = agxCurve(enc(Bi));
    let Ro = m3(AGX_OUT, Ri, Gi, Bi, 0), Go = m3(AGX_OUT, Ri, Gi, Bi, 1), Bo = m3(AGX_OUT, Ri, Gi, Bi, 2);
    Ro = Math.pow(Math.max(0, Ro), 2.2); Go = Math.pow(Math.max(0, Go), 2.2); Bo = Math.pow(Math.max(0, Bo), 2.2);
    return [
      Math.min(1, Math.max(0, m3(R2020_SRGB, Ro, Go, Bo, 0))),
      Math.min(1, Math.max(0, m3(R2020_SRGB, Ro, Go, Bo, 1))),
      Math.min(1, Math.max(0, m3(R2020_SRGB, Ro, Go, Bo, 2))),
    ];
  }
  async function captureHDR(
    cam: PerspectiveCamera, w: number, h: number, quality: number, maxOut?: number
  ): Promise<string> {
    const W = Math.max(16, Math.round(w)), H = Math.max(16, Math.round(h));
    const exposure = (renderer as unknown as { toneMappingExposure: number }).toneMappingExposure ?? 1;
    const rt = getRT(W, H, true);
    const prevAspect = cam.aspect;
    cam.aspect = W / H; cam.updateProjectionMatrix();
    const prevTarget = renderer.getRenderTarget();
    // warm the RT-format pipelines first — see captureWith (white-placeholder artifact)
    renderer.setRenderTarget(rt);
    await renderer.compileAsync(scene, cam);
    await renderer.renderAsync(scene, cam);
    await new Promise((r) => setTimeout(r, 120));
    await renderer.renderAsync(scene, cam);
    let raw = (await renderer.readRenderTargetPixelsAsync(rt, 0, 0, W, H)) as ArrayLike<number>;
    // VERIFY-AND-RETRY: a scene change (e.g. the plume light dying at engine cut) can
    // invalidate pipelines mid-run, and compileAsync provably misses some variants —
    // cold objects then render as EXACT-1.0 flat white. Real AgX'd content almost never
    // holds exact (1,1,1) over a large fraction of the frame; if it does, re-render
    // until the async compiler catches up (bounded).
    {
      const bpe0 = (raw as { BYTES_PER_ELEMENT?: number }).BYTES_PER_ELEMENT || 4;
      const isHalf0 = bpe0 === 2;
      const one = (i: number) => (isHalf0 ? raw[i] === 0x3c00 : (raw[i] as number) === 1.0);
      const whiteFrac = (): number => {
        let white = 0, n = 0;
        for (let i = 0; i + 3 < raw.length; i += 401 * 4) {
          n++;
          if (one(i) && one(i + 1) && one(i + 2)) white++;
        }
        return n ? white / n : 0;
      };
      for (let tries = 0; tries < 3 && whiteFrac() > 0.06; tries++) {
        await new Promise((r) => setTimeout(r, 300));
        await renderer.renderAsync(scene, cam);
        raw = (await renderer.readRenderTargetPixelsAsync(rt, 0, 0, W, H)) as ArrayLike<number>;
      }
    }
    renderer.setRenderTarget(prevTarget);
    cam.aspect = prevAspect; cam.updateProjectionMatrix();
    // Half-float readback: values may arrive as Uint16 (half bits) or Float32 depending on the
    // backend — detect by the constructor.
    const bpe = (raw as { BYTES_PER_ELEMENT?: number }).BYTES_PER_ELEMENT || 4;
    const isHalf = bpe === 2;
    const val = (i: number) => (isHalf ? halfToFloat(raw[i]) : (raw[i] as number));
    const dstRow = W * 4;
    // WebGPU pads each readback row to a 256-BYTE multiple; convert to elements via bpe
    // (half-float = 2 bytes/elt, float32 = 4) — the element-space assumption sheared the image.
    const srcRow = raw.length > W * H * 4 ? (Math.ceil((W * 4 * bpe) / 256) * 256) / bpe : dstRow;
    const cv = document.createElement("canvas"); cv.width = W; cv.height = H;
    const ctx = cv.getContext("2d")!; const img = ctx.createImageData(W, H); const out = img.data;
    const oetf = (lin: number) => {
      lin = lin < 0 ? 0 : lin > 1 ? 1 : lin;
      return lin <= 0.0031308 ? 12.92 * lin : 1.055 * Math.pow(lin, 1 / 2.4) - 0.055;
    };
    for (let y = 0; y < H; y++) {
      const src = y * srcRow, dst = y * dstRow;
      for (let x = 0; x < dstRow; x += 4) {
        const [lr, lg, lb] = agx(val(src + x), val(src + x + 1), val(src + x + 2), exposure);
        out[dst + x] = Math.round(oetf(lr) * 255);
        out[dst + x + 1] = Math.round(oetf(lg) * 255);
        out[dst + x + 2] = Math.round(oetf(lb) * 255);
        out[dst + x + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    if (maxOut && Math.max(W, H) > maxOut) {
      const s = maxOut / Math.max(W, H);
      const sc = document.createElement("canvas"); sc.width = Math.round(W * s); sc.height = Math.round(H * s);
      sc.getContext("2d")!.drawImage(cv, 0, 0, sc.width, sc.height);
      return sc.toDataURL("image/jpeg", quality);
    }
    return cv.toDataURL("image/jpeg", quality);
  }

  /** Save a data URL to disk in Tauri (save_screenshot command), else browser-download it. */
  async function save(dataUrl: string): Promise<void> {
    const name = `booster_shot_${String(++shotN).padStart(3, "0")}.jpg`;
    const tauri = (window as { __TAURI_INTERNALS__?: unknown }).__TAURI_INTERNALS__;
    if (tauri) {
      const b64 = dataUrl.slice(dataUrl.indexOf(",") + 1);
      const path = await invoke<string>("save_screenshot", { name, dataB64: b64 });
      if (path) {
        console.info(`[shot] saved ${path}`);
        flash(`saved ${path}`);
        return;
      }
      console.warn("[shot] Tauri save returned null; falling back to download");
    }
    const a = document.createElement("a");
    a.href = dataUrl;
    a.download = name;
    a.click();
    flash(`downloaded ${name}`);
  }

  function flash(msg: string) {
    const el = document.createElement("div");
    el.textContent = `📸 ${msg}`;
    el.style.cssText =
      "position:fixed;bottom:18px;left:50%;transform:translateX(-50%);z-index:60;" +
      "background:rgba(8,12,16,.86);color:#7fe0a8;border:1px solid #7fe0a855;border-radius:6px;" +
      "padding:7px 14px;font:600 12px ui-monospace,Consolas,monospace;pointer-events:none;" +
      "box-shadow:0 3px 14px #0009";
    document.body.appendChild(el);
    setTimeout(() => el.remove(), 2600);
  }

  // Hotkey P — capture the current view at display resolution and save it.
  window.addEventListener("keydown", (e) => {
    if (e.key.toLowerCase() === "p" && !e.repeat && !e.metaKey && !e.ctrlKey) {
      capture().then(save).catch((err) => console.error("[shot] failed", err));
    }
  });

  if (import.meta.env.DEV) {
    // ONE reusable pose camera for all __shotPose* calls: a fresh camera per call forces
    // the WebGPU renderer to rebuild every render pipeline for the scene each capture
    // (each cold pipeline = a white-placeholder risk + wasted compile time).
    const poseCam = new PerspectiveCamera(50, 16 / 9, 0.1, 4_000_000);
    const posePose = (
      eye: [number, number, number],
      target: [number, number, number],
      fovDeg: number,
      w: number,
      h: number
    ): PerspectiveCamera => {
      poseCam.fov = fovDeg;
      poseCam.aspect = w / h;
      poseCam.position.set(eye[0], eye[1], eye[2]);
      poseCam.lookAt(new Vector3(target[0], target[1], target[2]));
      poseCam.updateProjectionMatrix();
      poseCam.updateMatrixWorld(true);
      return poseCam;
    };
    (globalThis as { __shot?: unknown }).__shot = (
      w?: number,
      h?: number,
      q?: number,
      maxOut?: number
    ) => capture(w, h, q, maxOut);
    (globalThis as { __shotPose?: unknown }).__shotPose = async (
      eye: [number, number, number],
      target: [number, number, number],
      fovDeg = 50,
      w = 960,
      h = 540,
      q = 0.9,
      maxOut?: number
    ) => captureWith(posePose(eye, target, fovDeg, w, h), w, h, q, maxOut);
    // Tone-mapped (representative) variants — same poses, filmic exposure applied.
    (globalThis as { __shotHDR?: unknown }).__shotHDR = (w = 1024, h = 576, q = 0.6, maxOut = 1024) =>
      captureHDR(getCamera(), w, h, q, maxOut);
    (globalThis as { __shotPoseHDR?: unknown }).__shotPoseHDR = async (
      eye: [number, number, number], target: [number, number, number],
      fovDeg = 50, w = 1024, h = 576, q = 0.6, maxOut = 1024
    ) => captureHDR(posePose(eye, target, fovDeg, w, h), w, h, q, maxOut);
    // RAW-PIXEL PROBE (debug): render offscreen and return the LINEAR pre-tonemap RGB at
    // the given pixel coords of a 320×180 frame — ground truth for "is this lit, unlit,
    // fallback-white, or a tonemap artifact".
    (globalThis as { __shotProbe?: unknown }).__shotProbe = async (
      eye: [number, number, number],
      target: [number, number, number],
      pts: Array<[number, number]>
    ) => {
      const W = 320, H = 180;
      const cam = posePose(eye, target, 50, W, H);
      const rt = getRT(W, H, true);
      const prevTarget = renderer.getRenderTarget();
      renderer.setRenderTarget(rt);
      await renderer.compileAsync(scene, cam);
      await renderer.renderAsync(scene, cam);
      await new Promise((r) => setTimeout(r, 250));
      await renderer.renderAsync(scene, cam);
      const raw = (await renderer.readRenderTargetPixelsAsync(rt, 0, 0, W, H)) as ArrayLike<number>;
      renderer.setRenderTarget(prevTarget);
      const bpe = (raw as { BYTES_PER_ELEMENT?: number }).BYTES_PER_ELEMENT || 4;
      const isHalf = bpe === 2;
      const v = (i: number) => (isHalf ? halfToFloat(raw[i] as number) : (raw[i] as number));
      const srcRow = raw.length > W * H * 4 ? (Math.ceil((W * 4 * bpe) / 256) * 256) / bpe : W * 4;
      return pts.map(([x, y]) =>
        [v(y * srcRow + x * 4), v(y * srcRow + x * 4 + 1), v(y * srcRow + x * 4 + 2)].map(
          (n) => +n.toFixed(4)
        )
      );
    };
  }

  return { capture };
}
