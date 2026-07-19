// app.ts — the CONSTELLATION application shell (S2B standalone page).
//
// Orchestration only: boots the shared WebGPU renderer (../scene/renderer), builds
// the pad-centric 3D scene (scene.ts), wires OrbitControls, and connects the DOM
// chrome (dom.ts) callbacks — file load (drop + picker), filter chips, hover/pin
// card, A/B diff. The DOM lives in dom.ts (pure, jsdom-testable); the scene lives
// in scene.ts (pure three); the data spine is runData.ts + design.ts (pure).
//
// Nothing here reads live telemetry. The page is a pure end-state population view
// over per-run CSVs — the honest half of the "MC CONSTELLATION" signature until
// recorded trajectories (.bltlm) land in wave-F2 and make the ribbons real.

import { createRenderer } from "../scene/renderer";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { loadRunCsv, diffRuns, type CauseBucket } from "./runData";
import { buildConstellationScene, type ConstellationScene } from "./scene";
import type { Pick } from "./types";
import { buildDom, type LoadedFile } from "./dom";

export async function boot() {
  const dom = buildDom();
  const { renderer, scene, camera } = await createRenderer({ canvas: dom.canvas });

  // Pad-centric camera: look down at the pad from an angle. OrbitControls with the
  // target at origin (the pad center).
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.target.set(0, 0, 0);
  camera.position.set(70, 90, 90);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.maxPolarAngle = Math.PI * 0.495; // don't go under the ground
  controls.minDistance = 15;
  controls.maxDistance = 900;
  controls.update();

  let cscene: ConstellationScene | null = null;
  let fileA: LoadedFile | null = null;
  let fileB: LoadedFile | null = null;
  const activeFilter = new Set<CauseBucket>(); // empty = show all
  let pinned: Pick | null = null;

  // ── (re)build the scene from the current A (+ optional B) files ─────────────
  function rebuild() {
    if (cscene) {
      cscene.dispose();
      cscene = null;
    }
    if (!fileA) {
      dom.setSummary(null, null);
      return;
    }
    const abMode = !!fileB;
    let flippedRuns: Set<number> | undefined;
    if (fileB) {
      const d = diffRuns(fileA.runs, fileB.runs);
      flippedRuns = new Set(d.transitions.filter((t) => t.flipped).map((t) => t.run));
      dom.setTransition(d, fileA.name, fileB.name);
    } else {
      dom.clearTransition();
    }

    cscene = buildConstellationScene(scene, camera, fileA.runs, {
      ...(fileB ? { runsB: fileB.runs } : {}),
      ...(flippedRuns ? { flippedRuns } : {}),
    });
    applyFilter();
    cscene.setArcsVisible(dom.arcsToggle.checked);

    dom.setSummary(fileA, abMode ? fileB : null);
    controls.target.set(0, 0, 0);
    controls.update();
  }

  function applyFilter() {
    if (!cscene) return;
    cscene.setVisibleBuckets(activeFilter.size === 0 ? null : new Set(activeFilter));
  }

  // ── file loading (drop + picker), routed to A then B ────────────────────────
  async function ingest(file: File, forceSide?: "A" | "B") {
    let text: string;
    try {
      text = await file.text();
    } catch (e) {
      dom.toast(`could not read ${file.name}: ${e}`);
      return;
    }
    let loaded: LoadedFile;
    try {
      const { runs, summary, parse } = loadRunCsv(text);
      loaded = { name: file.name, runs, summary, skipped: parse.skipped.length };
    } catch (e) {
      dom.toast(`parse failed for ${file.name}: ${(e as Error).message}`);
      return;
    }
    const side = forceSide ?? (!fileA ? "A" : "B");
    if (side === "A") fileA = loaded;
    else fileB = loaded;
    pinned = null;
    dom.hideCard();
    rebuild();
    dom.toast(
      `loaded ${file.name} as ${side}: ${loaded.runs.length} runs` +
        (loaded.skipped ? `, ${loaded.skipped} skipped` : "")
    );
  }

  // ── pointer interactions (hover card, click pin) ────────────────────────────
  function ndcFromEvent(ev: PointerEvent): [number, number] {
    const rect = dom.canvas.getBoundingClientRect();
    const x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
    const y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
    return [x, y];
  }

  function fileLabelFor(side: "A" | "B"): string | undefined {
    if (!fileB) return undefined;
    return side === "A" ? fileA?.name : fileB.name;
  }

  dom.canvas.addEventListener("pointermove", (ev) => {
    if (!cscene || pinned) return; // pinned card wins until unpinned
    const [x, y] = ndcFromEvent(ev);
    const hit = cscene.pick(x, y);
    cscene.highlight(hit);
    if (hit) {
      dom.showCard(hit, fileLabelFor(hit.side), false);
      dom.canvas.style.cursor = "pointer";
    } else {
      dom.hideCard();
      dom.canvas.style.cursor = "grab";
    }
  });

  dom.canvas.addEventListener("pointerdown", (ev) => {
    if (!cscene || ev.button !== 0) return;
    const [x, y] = ndcFromEvent(ev);
    const hit = cscene.pick(x, y);
    if (hit) {
      pinned = hit;
      cscene.highlight(hit);
      dom.showCard(hit, fileLabelFor(hit.side), true);
    } else if (pinned) {
      pinned = null;
      cscene.highlight(null);
      dom.hideCard();
    }
  });

  // ── wire DOM controls ───────────────────────────────────────────────────────
  dom.onFilterToggle((bucket) => {
    if (activeFilter.has(bucket)) activeFilter.delete(bucket);
    else activeFilter.add(bucket);
    dom.reflectFilter(activeFilter);
    applyFilter();
  });
  dom.arcsToggle.addEventListener("change", () => {
    cscene?.setArcsVisible(dom.arcsToggle.checked);
  });
  dom.onFiles((files, side) => {
    for (const f of files) void ingest(f, side);
  });
  dom.onClearB(() => {
    fileB = null;
    pinned = null;
    dom.hideCard();
    rebuild();
    dom.toast("cleared B — back to single-population view");
  });
  dom.onUnpin(() => {
    pinned = null;
    cscene?.highlight(null);
    dom.hideCard();
  });
  dom.onResetView(() => {
    camera.position.set(70, 90, 90);
    controls.target.set(0, 0, 0);
    controls.update();
  });

  // ── render loop ─────────────────────────────────────────────────────────────
  renderer.setAnimationLoop(() => {
    controls.update();
    renderer.render(scene, camera);
  });

  // Splash hint until a file is dropped.
  dom.setEmpty(true);
}
