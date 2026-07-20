// main.ts — LZ-COCKPIT F2 integrated entry (was M3 ugly-scene entry).
//
// This is the F2 merge of the four F1 waves into ONE render loop:
//   • S1 DOCUMENTARY VIEW  — procedural-lite booster + mounted plume + diegetic
//     markers + DIRECTOR v0 (EVT-cut cameras) + HUD strip + EVT timeline.
//   • S3 AUDIO             — a third pure observer (propagation-honest, muted by
//     default); tees the same decoded TLM + raw EVT bytes, writes nothing back.
//   • S0 SHELL CHROME      — the Tauri supervisor's webview chrome (connection
//     chip / picker / wire-log / stderr) + HELLO identity gate + resolved WS port.
//
// The M3 loop shape is preserved (stream-clock sampling, one packet in the past,
// floating-origin rebase — interpolate-never-snap, canon §11.2). Everything visual
// is keyed to a streamed field or an EVT beat (canon §A.0 — renderer owns no
// truth). Camera state never crosses the telemetry boundary (canon §B.6). The
// shell and the audio engine are BOTH pure observers of the same one-way stream
// (canon §10.1 / §B.8) — neither writes vehicle state.
//
// Mount order (F2 integration): scene → hud → director → audio → shell chrome LAST
// (the shell resolves the WS port the client opens, so it is awaited before the
// TelemetryClient is constructed).

import { createRenderer } from "./scene/renderer";
import { buildDocumentaryScene } from "./scene/documentaryScene";
import { FloatingOrigin } from "./scene/floatingOrigin";
import { InterpBuffer } from "./net/interp";
import type { InterpSample } from "./net/interp";
import { TelemetryClient } from "./net/client";
import { decodeEvt, decodeHello, decodeStats, EvtCode } from "./net/events";
import type { EvtFrame } from "./net/events";
import { Phase } from "./net/decode";
import { installHud } from "./hud/hud";
import { installTimeline } from "./hud/timeline";
import { DirectorRig, PRESETS, type CameraPreset } from "./director/director";
import { simToThreePosition } from "./net/frame";
import { Vector3 } from "three/webgpu";
import { mountAudio } from "./audio"; // S3 audio observer (self-contained, muted by default)
import { mountShell } from "./shell/mount"; // S0 LZ-COCKPIT chrome (self-contained, dual-target)
import { installInjectPanel } from "./hud/injectPanel"; // Mode 2 failure-injection buttons (§M2)
import { buildPostFx } from "./scene/postfx"; // §5.2 emissive/HDR bloom post-pass

async function boot() {
  const { renderer, scene, camera, backend } = await createRenderer();
  document.body.appendChild(renderer.domElement);

  const doc = buildDocumentaryScene(scene);
  // §5.2 bloom post-pass: the HDR plume + green-flash + hot markers glow. Replaces the
  // direct renderer.render() in the loop below (PostProcessing applies AgX + sRGB at output).
  const postfx = buildPostFx(renderer, scene, camera);
  // DEV-only: expose the scene + bloom tuner for headless inspection (eval), since a WebGPU
  // canvas can't be screenshot in the CI/headless env (frontend reports §5.2). Tree-shaken
  // out of production. Renderer-only; never crosses the telemetry boundary.
  if (import.meta.env.DEV) {
    (globalThis as { __doc?: unknown }).__doc = doc;
    (globalThis as { __postfx?: unknown }).__postfx = postfx; // __postfx.set(strength,radius,threshold)
  }
  const origin = new FloatingOrigin();
  const interp = new InterpBuffer(/* runSeconds */ 600);
  const director = new DirectorRig();
  const hud = installHud();
  const timeline = installTimeline();

  // S3 audio observer — a third pure observer of the SAME stream. Muted by default;
  // its dev-panel ENABLE button is the first-interaction gesture that resumes the
  // AudioContext. Self-contained under src/audio/ (touches nothing else here).
  // BOOT ARMOR: an optional observer must NEVER take down the renderer — first light
  // died on a Web Audio construction error inside mountAudio (the createDelay(200)
  // spec-cap bug). On any failure, degrade to a silent no-op and keep flying.
  const audio = (() => {
    try {
      return mountAudio();
    } catch (e) {
      console.error("[audio] disabled — mount failed:", e);
      const noop = () => {};
      return {
        onTlm: noop, onEvtBytes: noop, setListener: noop, tick: noop, updatePanel: noop,
      } as unknown as ReturnType<typeof mountAudio>;
    }
  })();

  // S1 stream-stats chip (top-left, pointer-events:none): live seed/run/fps/backend.
  const chip = installConnectionChip(backend);

  // S0 shell chrome LAST — mounts the top-strip cockpit chrome (lifecycle chip +
  // picker + wire-log + stderr) and resolves the port to stream on (supervisor-
  // chosen in Tauri, default in a plain browser). The shell owns no vehicle truth —
  // it only observes raw frames for the wire log + HELLO identity gate + liveness
  // (canon §10.1). It coexists with the S1 stream chip (different corner/role: S1 =
  // stream stats, S0 = supervisor lifecycle + picker + panels).
  const shell = await mountShell();

  // sim-world scratch for the director + camera plumbing
  const vehSim = new Vector3();
  const vehVel = new Vector3();
  const camEyeThree = new Vector3(); // director eye, three-world
  const camTargetThree = new Vector3(); // director target, three-world
  const camTargetRender = new Vector3();

  // pending EVTs decoded on the socket callback; drained in the render loop so the
  // director/HUD/timeline see them with the current vehicle pose (canon §A.4).
  const evtQueue: EvtFrame[] = [];

  const client = new TelemetryClient(
    {
      onOpen: () => {
        console.info("[net] connected to core --serve");
        chip.set("STREAMING");
        shell.onSocketOpen();
      },
      onClose: () => {
        console.warn("[net] disconnected; retrying");
        chip.set("LOST");
        shell.onSocketClose();
      },
      onTlm: (f) => {
        interp.push(f);
        audio.onTlm(f); // tee the same decoded frame into the audio propagation model
      },
      onHelloBytes: (buf) => {
        try {
          const h = decodeHello(buf);
          doc.applyHello(h);
          if (h.vehLen > 1) director.vehHalfH = h.vehLen / 2; // frame the true center
          chip.setInfo(`seed ${h.seed} run ${h.runIdx} v${h.ver}`);
          console.info("[net] HELLO", h);
        } catch (e) {
          console.warn("[net] HELLO decode failed", e);
        }
      },
      onEvtBytes: (buf) => {
        // fan-out: the director/HUD/timeline drain the queue in the loop (canon
        // §A.4), and the audio engine consumes the raw EVT bytes as its trigger bus.
        try {
          evtQueue.push(decodeEvt(buf));
        } catch (e) {
          console.warn("[net] EVT decode failed", e);
        }
        audio.onEvtBytes(buf); // EVT = the audio trigger bus (canon §A.0)
      },
      onStatsBytes: (buf) => {
        try {
          const st = decodeStats(buf);
          chip.setStats(st.fpsEmit);
        } catch {
          /* stats are advisory; ignore decode hiccups */
        }
      },
      onRawFrame: (buf) => shell.onFrame(buf), // shell wire log + identity gate + liveness
    },
    shell.wsUrl // S0: stream on the supervisor-chosen port (default 8787 in a browser)
  );
  client.connect();

  // FAILURE-MODE panel (Mode 2, canon §M2): buttons that inject a live wind gust /
  // engine-out via the upstream command channel (client.send). This is the ONE place
  // the renderer crosses the observer boundary, and a no-op unless core runs
  // `--serve --interactive` (the server drops the frames otherwise). The sim owns the
  // outcome; the panel only emits the closed-enum command + shows local feedback.
  const inject = installInjectPanel((bytes) => client.send(bytes));
  if (import.meta.env.DEV) (globalThis as { __inject?: unknown }).__inject = inject;

  // camera preset hotkeys (renderer-side only; never crosses the boundary)
  installCameraHotkeys(director, () => vehSim, () => vehVel);

  let sample: InterpSample | undefined;
  let lastWall = performance.now();

  renderer.setAnimationLoop(() => {
    const t0 = performance.now();
    const dtSec = Math.min(0.05, (t0 - lastWall) / 1000);
    lastWall = t0;

    const newest = interp.latest();
    if (newest) {
      // Render relative to the STREAM clock (newest sim-time), one packet in the
      // past, so interpolation always has a straddling pair (canon §11.2).
      const s = interp.sample(newest.t, sample);
      if (s) {
        sample = s;
        vehSim.set(s.r.x, s.r.y, s.r.z);
        vehVel.set(s.v.x, s.v.y, s.v.z);

        // drain EVT beats: feed director cuts, HUD ticker, timeline glyphs, and
        // the plume green flash (canon §A.4 — EVT is the only trigger channel).
        if (evtQueue.length) {
          const ctx = { phase: s.frame.phase as Phase, altitudeM: s.r.z };
          for (const evt of evtQueue) {
            director.onEvt(evt, ctx, vehSim, vehVel);
            hud.onEvt(evt);
            timeline.onEvt(evt);
            if (evt.code === EvtCode.GreenFlash) doc.triggerGreenFlash();
          }
          evtQueue.length = 0;
        }

        // scene + markers (all conversion inside via frame.ts)
        doc.update(s, dtSec);

        // DIRECTOR: advance the rig, then place the camera through the floating
        // origin (camera-relative rendering, canon §11.1). Director poses are in
        // SIM-world; convert to three-world via frame.ts, then origin does rebase.
        director.vehQuat.copy(s.q); // sim body->world attitude, for the ATTACHED onboard cam
        director.update(vehSim, vehVel, dtSec);
        simToThreePosition(director.eye.x, director.eye.y, director.eye.z, camEyeThree);
        simToThreePosition(director.target.x, director.target.y, director.target.z, camTargetThree);
        origin.update(camEyeThree, camera, doc.world);
        camera.lookAt(origin.toRender(camTargetThree, camTargetRender));
        if (Math.abs(camera.fov - director.fov) > 0.01) {
          camera.fov = director.fov;
          camera.updateProjectionMatrix();
        }

        // S3 director hook: the audio listener IS the active camera (canon §B.8).
        // The director's eye in three-world coords is the live cockpit camera.
        audio.setListener(camEyeThree);

        const frameMs = performance.now() - t0;
        hud.update(s, frameMs, {
          convergence: doc.markers.convergence,
          missM: doc.markers.missDistanceM,
        });
        timeline.tick(newest.t);
      }
    }
    audio.tick(); // keep the causal crackle stream regenerating (never a loop)
    audio.updatePanel(); // refresh meters + "you are N s away" readout
    postfx.render(); // §5.2 bloom chain (was renderer.render(scene, camera))
  });
}

// --- S1 stream-stats chip (top-left): STREAMING / LOST + HELLO + fps + build -----
// Coexists with the S0 shell top-strip chrome (which owns the supervisor lifecycle
// chip + picker + panels). This one is a passive stream-stats readout.
function installConnectionChip(backend: string) {
  const el = document.createElement("div");
  el.style.cssText =
    "position:fixed;top:14px;left:14px;font:11px ui-monospace,Menlo,monospace;" +
    "background:rgba(6,10,14,.62);border:1px solid rgba(120,150,175,.25);border-radius:4px;" +
    "padding:5px 10px;color:#8fb0c4;z-index:21;pointer-events:none;display:flex;gap:8px;align-items:center";
  const dot = document.createElement("span");
  dot.style.cssText =
    "width:8px;height:8px;border-radius:50%;background:#ffcf6b;box-shadow:0 0 6px #ffcf6b";
  const txt = document.createElement("span");
  txt.textContent = "SPAWNING";
  const info = document.createElement("span");
  info.style.color = "#5f7a8c";
  const fps = document.createElement("span");
  fps.style.color = "#5f7a8c";
  const build = document.createElement("span");
  build.style.color = "#5f7a8c";
  build.textContent = `· ${backend} · three r185`;
  el.append(dot, txt, info, fps, build);
  document.body.appendChild(el);
  return {
    set(state: "STREAMING" | "LOST" | "SPAWNING") {
      txt.textContent = state;
      dot.style.background =
        state === "STREAMING" ? "#7fe0a8" : state === "LOST" ? "#ff6b6b" : "#ffcf6b";
      dot.style.boxShadow = `0 0 6px ${dot.style.background}`;
    },
    setInfo(s: string) {
      info.textContent = `· ${s}`;
    },
    setStats(fpsEmit: number) {
      fps.textContent = `· ${fpsEmit.toFixed(0)} Hz`;
    },
  };
}

// --- camera preset hotkeys (1..4 presets, 0/a = AUTO) --------------------------
function installCameraHotkeys(
  director: DirectorRig,
  getPos: () => Vector3,
  getVel: () => Vector3
) {
  window.addEventListener("keydown", (e) => {
    const idx = "1234".indexOf(e.key);
    if (idx >= 0) {
      director.select(PRESETS[idx] as CameraPreset, getPos(), getVel());
    } else if (e.key === "0" || e.key.toLowerCase() === "a") {
      director.setAuto(true);
    }
  });

  // KSP-style manual orbit (operator doctrine: the user OWNS the external angle).
  // Drag = azimuth/elevation, wheel = zoom. Applies to the default external view;
  // grabbing the mouse also drops any cinematic preset back to the external cam.
  let dragging = false;
  window.addEventListener("pointerdown", (e) => {
    if (e.button === 0 && (e.target as HTMLElement)?.tagName === "CANVAS") {
      dragging = true;
      if (director.preset !== "FREE_ORBIT") {
        director.select("FREE_ORBIT", getPos(), getVel());
      }
    }
  });
  window.addEventListener("pointerup", () => (dragging = false));
  window.addEventListener("pointermove", (e) => {
    if (dragging) director.orbitInput(e.movementX, e.movementY);
  });
  window.addEventListener(
    "wheel",
    (e) => {
      if ((e.target as HTMLElement)?.tagName === "CANVAS") director.zoomInput(e.deltaY);
    },
    { passive: true }
  );
}

boot().catch((e) => {
  console.error("boot failed:", e);
  document.body.innerHTML =
    `<pre style="color:#f88;font:14px monospace;padding:20px">Renderer boot failed:\n${e?.stack ?? e}</pre>`;
});
