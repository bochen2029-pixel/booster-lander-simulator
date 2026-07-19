// picker.ts — scenario / seed / run PICKER (brainstorm §2 top-strip).
//
// (Re)starts a sidecar RUN via the control plane (Tauri invoke `relaunch_core`).
// The button is labelled RUN, not "launch": this is a booster DESCENT simulator —
// pressing it starts a descent run (scenario/seed/run), it does not launch the
// vehicle skyward (there is no ascent phase; see core/state.h PH_* — COAST→…→LANDED).
// In a plain browser it degrades to READ-ONLY: the inputs reflect the current
// run identity but the RUN button is disabled with an explaining tooltip
// (canon §11.1 "picker/chip degrade gracefully to read-only when no Tauri").

import type { CockpitStateMachine } from "../state";
import type { bridge as Bridge } from "../tauriBridge";

// Scenario names accepted by `booster-core --serve --scenario S`. Kept small and
// matching the core's known set; free-typing is still allowed for forward-compat
// (the capability/server-side validators are the real gate).
const SCENARIOS = ["terminal", "aero_offset", "entry"];

export function createPicker(
  machine: CockpitStateMachine,
  bridge: typeof Bridge
): HTMLElement {
  const root = document.createElement("div");
  root.className = "lz-picker";

  const scenario = document.createElement("input");
  scenario.className = "lz-picker__scenario";
  scenario.setAttribute("list", "lz-picker-scenarios");
  scenario.title = "scenario";
  scenario.spellcheck = false;

  const datalist = document.createElement("datalist");
  datalist.id = "lz-picker-scenarios";
  for (const s of SCENARIOS) {
    const o = document.createElement("option");
    o.value = s;
    datalist.appendChild(o);
  }

  const seed = document.createElement("input");
  seed.className = "lz-picker__seed";
  seed.type = "number";
  seed.min = "0";
  seed.max = "1000000";
  seed.title = "seed";

  const run = document.createElement("input");
  run.className = "lz-picker__run";
  run.type = "number";
  run.min = "0";
  run.max = "1000000";
  run.title = "run";

  const launch = document.createElement("button");
  launch.className = "lz-picker__launch";
  launch.textContent = "RUN";

  const sLabel = tag("scenario");
  const seedLabel = tag("seed");
  const runLabel = tag("run");

  root.append(datalist, sLabel, scenario, seedLabel, seed, runLabel, run, launch);

  if (!bridge.available) {
    launch.disabled = true;
    scenario.disabled = true;
    seed.disabled = true;
    run.disabled = true;
    launch.title = "read-only: relaunch requires the Tauri shell (no control plane in a plain browser)";
    root.classList.add("lz-picker--readonly");
  }

  // Keep inputs synced with the live launch identity (from supervisor state), but
  // don't clobber a field the user is actively editing.
  machine.subscribe((s) => {
    if (document.activeElement !== scenario) scenario.value = s.scenario;
    if (document.activeElement !== seed) seed.value = String(s.seed);
    if (document.activeElement !== run) run.value = String(s.run);
  });

  async function doLaunch() {
    if (!bridge.available) return;
    const sc = scenario.value.trim() || "entry";
    const sd = clampInt(seed.value, 0, 1_000_000, 42);
    const rn = clampInt(run.value, 0, 1_000_000, 1);
    launch.disabled = true;
    launch.textContent = "…";
    machine.beginRelaunch(sc, sd, rn);
    const port = await bridge.relaunch(sc, sd, rn);
    if (port == null) {
      // invoke failed (validation or spawn error) — the chip will show FAILED via
      // core://state; re-enable so the user can adjust and retry.
      console.warn("[shell] relaunch returned null");
    }
    setTimeout(() => {
      launch.disabled = false;
      launch.textContent = "RUN";
    }, 900);
  }

  launch.addEventListener("click", doLaunch);
  for (const el of [scenario, seed, run]) {
    el.addEventListener("keydown", (e) => {
      if ((e as KeyboardEvent).key === "Enter") doLaunch();
    });
  }

  return root;
}

function tag(text: string): HTMLElement {
  const el = document.createElement("span");
  el.className = "lz-picker__tag";
  el.textContent = text;
  return el;
}

function clampInt(v: string, lo: number, hi: number, fallback: number): number {
  const n = Number.parseInt(v, 10);
  if (!Number.isFinite(n)) return fallback;
  return Math.min(hi, Math.max(lo, n));
}
