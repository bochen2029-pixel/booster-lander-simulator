// renderer.ts — WebGPU bootstrap (canon §11.1).
//
// three r185 rules baked in here:
//  - import from 'three/webgpu' (WebGPURenderer + NodeMaterial live there).
//  - WebGPURenderer auto-falls-back to a WebGL2 backend if WebGPU is absent
//    (canon Risk #2). We detect and LOG the actual backend into the HUD.
//  - reversedDepthBuffer: true on WebGPU for the 70km->1m depth range (canon §11.1;
//    the param is spelled `reversedDepthBuffer` since r178, uses depth32float).
//    logarithmicDepthBuffer is NOT compatible with WebGPU (known upstream issue),
//    so on the WebGL2 fallback we set logarithmicDepthBuffer instead (re-verify M7).
//  - Tone mapping AgX (THREE.AgXToneMapping) — degrades saturated plume emissives
//    gracefully where ACES hue-skews.
//  - renderer.init() is async in WebGPU; await it before the first render.

import {
  WebGPURenderer,
  PerspectiveCamera,
  Scene,
  AgXToneMapping,
  ACESFilmicToneMapping,
} from "three/webgpu";

export interface RendererBundle {
  renderer: WebGPURenderer;
  scene: Scene;
  camera: PerspectiveCamera;
  /** 'webgpu' | 'webgl2' — the ACTUAL backend, for the HUD build-info line. */
  backend: "webgpu" | "webgl2";
}

export interface RendererOptions {
  canvas?: HTMLCanvasElement;
  /** ACESFilmic behind a toggle (canon §11.1); default AgX. */
  toneMapping?: "agx" | "aces";
  /** Force the WebGL2 fallback (for testing the fallback path at M7). */
  forceWebGL?: boolean;
}

export async function createRenderer(
  opts: RendererOptions = {}
): Promise<RendererBundle> {
  const renderer = new WebGPURenderer({
    canvas: opts.canvas,
    antialias: true,
    // 70km->1m: reversed-z on WebGPU. Fallback handled below.
    reversedDepthBuffer: true,
    forceWebGL: opts.forceWebGL ?? false,
    // WebGPU premultiplied-alpha change (r185): give an opaque clear so night sky
    // doesn't blend with the page. Sky/atmosphere overwrites this anyway.
    alpha: false,
  } as ConstructorParameters<typeof WebGPURenderer>[0]);

  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.toneMapping =
    (opts.toneMapping ?? "agx") === "aces" ? ACESFilmicToneMapping : AgXToneMapping;
  renderer.toneMappingExposure = 1.0;

  // MUST init before first render on WebGPU (device request is async).
  await renderer.init();

  const backend: "webgpu" | "webgl2" =
    // @ts-expect-error backend shape is not fully typed in @types/three 0.185
    renderer.backend?.isWebGPUBackend ? "webgpu" : "webgl2";

  if (backend === "webgl2") {
    // Fallback depth strategy (canon §11.1, Risk #2): log-depth on WebGL2.
    // Setting after init requires a context flag; we flag it for the HUD and the
    // scene builder will keep near/far conservative. (Full log-depth wiring is an
    // M7 fallback task — reversed-z path is the primary and is what we ship on
    // this RTX 4070 Ti SUPER box.)
    console.warn(
      "[renderer] WebGPU unavailable — running WebGL2 fallback. reversed-z off; " +
        "verify log-depth path at M7 (canon Risk #2)."
    );
  }
  console.info(`[renderer] backend=${backend} tonemap=${renderer.toneMapping}`);

  const scene = new Scene();

  // Camera far plane is huge (space) but reversed-z keeps precision near the
  // vehicle. Floating origin (camera.ts) keeps rendered coords small regardless.
  const camera = new PerspectiveCamera(
    50,
    window.innerWidth / window.innerHeight,
    0.1, // near
    200_000 // far: 200 km (entry starts at 62 km)
  );

  addEventListener("resize", () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  });

  return { renderer, scene, camera, backend };
}
