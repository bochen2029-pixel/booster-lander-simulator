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

  async function captureWith(
    cam: PerspectiveCamera,
    w: number,
    h: number,
    quality: number,
    maxOut?: number
  ): Promise<string> {
    const W = Math.max(16, Math.round(w));
    const H = Math.max(16, Math.round(h));

    const rt = new RenderTarget(W, H, {
      type: UnsignedByteType,
      minFilter: LinearFilter,
      magFilter: LinearFilter,
      depthBuffer: true,
    });

    const prevAspect = cam.aspect;
    cam.aspect = W / H;
    cam.updateProjectionMatrix();

    const prevTarget = renderer.getRenderTarget();
    renderer.setRenderTarget(rt);
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
    rt.dispose();

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
    ) => {
      const cam = new PerspectiveCamera(fovDeg, w / h, 0.1, 4_000_000);
      cam.position.set(eye[0], eye[1], eye[2]);
      cam.lookAt(new Vector3(target[0], target[1], target[2]));
      cam.updateMatrixWorld(true);
      return captureWith(cam, w, h, q, maxOut);
    };
  }

  return { capture };
}
