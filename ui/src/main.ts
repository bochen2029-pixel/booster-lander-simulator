// main.ts — M3 entry. Wires: WebGPU renderer + WS client + interp buffer + ugly
// scene + floating origin + a render loop that renders ~1 packet in the past.
//
// This is the minimum that satisfies the M3 gate (canon §14): 10-minute stream,
// zero drops, interp jitter < 1 frame, conversion vectors pass. The cinematic
// systems (procedural booster, plume, sky, audio, HUD, director) land at M7 and
// slot into the same loop.

import { createRenderer } from "./scene/renderer";
import { buildUglyScene } from "./scene/uglyScene";
import { FloatingOrigin } from "./scene/floatingOrigin";
import { InterpBuffer } from "./net/interp";
import type { InterpSample } from "./net/interp";
import { TelemetryClient } from "./net/client";
import { simToThreePosition } from "./net/frame";
import { Vector3 } from "three/webgpu";

async function boot() {
  const { renderer, scene, camera, backend } = await createRenderer();
  document.body.appendChild(renderer.domElement);

  const ugly = buildUglyScene(scene);
  const origin = new FloatingOrigin();
  const interp = new InterpBuffer(/* runSeconds */ 600);

  // simple orbit-ish camera target follows the booster (M7 swaps in the rig system)
  const camWorld = new Vector3(60, 40, 60); // where we place the camera, world coords
  const boosterWorldThree = new Vector3();

  const client = new TelemetryClient({
    onOpen: () => console.info("[net] connected to core --serve"),
    onClose: () => console.warn("[net] disconnected; retrying"),
    onTlm: (f) => interp.push(f),
    // onHelloBytes/onEvtBytes/onStatsBytes wired at M7 (geometry, director, HUD)
  });
  client.connect();

  const hud = installHudStub(backend);

  let sample: InterpSample | undefined;
  let framesOverBudget = 0;

  renderer.setAnimationLoop(() => {
    const t0 = performance.now();
    const newest = interp.latest();
    if (newest) {
      // Render relative to the STREAM clock (newest sim-time), one packet in the
      // past, so interpolation always has a straddling pair (canon §11.2).
      const s = interp.sample(newest.t, sample);
      if (s) {
        sample = s;
        ugly.update(s);

        // Aim the camera at the booster (render space). Booster world-three pos:
        simToThreePosition(s.r.x, s.r.y, s.r.z, boosterWorldThree);
        // keep camera a fixed offset above/behind for M3
        camWorld.set(boosterWorldThree.x + 60, boosterWorldThree.y + 30, boosterWorldThree.z + 60);
        origin.update(camWorld, camera, ugly.world);
        camera.lookAt(origin.toRender(boosterWorldThree, new Vector3()));

        hud.update(s, interp.droppedFrames, origin.rebases);
      }
    }
    renderer.render(scene, camera);

    const dt = performance.now() - t0;
    if (dt > 8.3) framesOverBudget++;
    else framesOverBudget = 0;
    // (auto-scaler hysteresis lives at M7; here we just count)
  });
}

// --- throwaway HUD so M3 has something on screen; replaced wholesale at M7 -----
function installHudStub(backend: string) {
  const el = document.createElement("div");
  el.style.cssText =
    "position:fixed;top:8px;left:8px;font:12px monospace;color:#8f8;" +
    "background:rgba(0,0,0,.55);padding:6px 8px;white-space:pre;pointer-events:none;z-index:10";
  document.body.appendChild(el);
  return {
    update(s: InterpSample, dropped: number, rebases: number) {
      const f = s.frame;
      el.textContent =
        `backend ${backend}   three r185\n` +
        `phase ${f.phase}  verdict ${f.verdict}  seq ${f.seq}  dropped ${dropped}\n` +
        `alt ${s.r.z.toFixed(1)} m   |v| ${s.v.length().toFixed(1)} m/s   mach ${f.mach.toFixed(2)}\n` +
        `throttle ${(f.throttleAct * 100).toFixed(0)}%  n_eng ${f.nEng}  rebases ${rebases}\n` +
        `interp a=${s.alpha.toFixed(2)}${interpRaw()}`;
    },
  };
  function interpRaw() {
    return "";
  }
}

boot().catch((e) => {
  console.error("boot failed:", e);
  document.body.innerHTML =
    `<pre style="color:#f88;font:14px monospace;padding:20px">Renderer boot failed:\n${e?.stack ?? e}</pre>`;
});
