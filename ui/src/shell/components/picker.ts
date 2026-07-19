// picker.ts — scenario / seed / run PICKER + the PLAY MENU (brainstorm §2
// top-strip; canon v2 §10.6/D-019 "keep INJECT_DISTURBANCE one keystroke away").
//
// (Re)starts a sidecar RUN via the control plane (Tauri invoke `relaunch_core`).
// The button is labelled RUN, not "launch": this is a booster DESCENT simulator —
// pressing it starts a descent run (scenario/seed/run), it does not launch the
// vehicle skyward (there is no ascent phase; see core/state.h PH_* — COAST→…→LANDED).
// In a plain browser it degrades to READ-ONLY: the inputs reflect the current
// run identity but the RUN button is disabled with an explaining tooltip
// (canon §11.1 "picker/chip degrade gracefully to read-only when no Tauri").
//
// THE PLAY MENU (v2): optional disturbance specs passed through to the core CLI —
//   gust    "12@5000:800"  -> --gust        (BUILT, D-017 — works today)
//   dir     "90"           -> --gust-dir    (BUILT, D-017)
//   eng-out "1@85"         -> --engine-out  (N0 core; pre-N0 the core rejects
//   target  "drift:2"      -> --target       loudly — see the STDERR panel)
// Empty = flag absent = the byte-identical clean baseline. The core's parser is
// the semantic gate; the shell only enforces a safe character set.

import type { CockpitStateMachine } from "../state";
import { disturbSummary } from "../state";
import type { bridge as Bridge, DisturbSpec } from "../tauriBridge";

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

  // --- the PLAY MENU inputs (v2 §10.6/D-019; all optional, empty = off) -------
  const gust = document.createElement("input");
  gust.className = "lz-picker__gust";
  gust.placeholder = "peak@alt[:hw]";
  gust.title = "dial-a-gust (D-017): --gust <peak_mps>@<alt_m>[:<halfwidth_m>], e.g. 12@5000:800";
  gust.spellcheck = false;

  const gustDir = document.createElement("input");
  gustDir.className = "lz-picker__gdir";
  gustDir.placeholder = "dir°";
  gustDir.title = "gust bearing in degrees (0 = +x): --gust-dir";
  gustDir.spellcheck = false;

  const engineOut = document.createElement("input");
  engineOut.className = "lz-picker__eo";
  engineOut.placeholder = "k@t";
  engineOut.title =
    "engine-out (canon §4.6): --engine-out <k>@<t_s> — kill engine k at t seconds. Requires the N0 core; before that the core rejects the flag loudly (see STDERR).";
  engineOut.spellcheck = false;

  const target = document.createElement("input");
  target.className = "lz-picker__target";
  target.placeholder = "target";
  target.title =
    "movable target (canon §4.5): --target <spec>, e.g. drift:2 — seeded deck wander. Requires the N0 core; before that the core rejects the flag loudly (see STDERR).";
  target.spellcheck = false;

  const sLabel = tag("scenario");
  const seedLabel = tag("seed");
  const runLabel = tag("run");
  const disturbLabel = tag("disturb");
  disturbLabel.classList.add("lz-picker__tag--disturb");
  disturbLabel.title = "the play menu: throw a deterministic disturbance and watch guidance re-solve";

  root.append(
    datalist,
    sLabel,
    scenario,
    seedLabel,
    seed,
    runLabel,
    run,
    disturbLabel,
    gust,
    gustDir,
    engineOut,
    target,
    launch
  );

  const allInputs = [scenario, seed, run, gust, gustDir, engineOut, target];

  if (!bridge.available) {
    launch.disabled = true;
    for (const el of allInputs) el.disabled = true;
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

  /** Collect + lightly sanitize the play-menu specs (the core is the real gate). */
  function readDisturb(): DisturbSpec {
    const clean = (v: string, re: RegExp) => {
      const t = v.trim();
      return t && re.test(t) ? t : "";
    };
    return {
      gust: clean(gust.value, /^[0-9.]+@[0-9.]+(:[0-9.]+)?$/),
      gustDir: clean(gustDir.value, /^-?[0-9.]+$/),
      engineOut: clean(engineOut.value, /^[0-9]+@[0-9.]+$/),
      target: clean(target.value, /^[A-Za-z0-9_.:@,+]+$/),
    };
  }

  async function doLaunch() {
    if (!bridge.available) return;
    const sc = scenario.value.trim() || "entry";
    const sd = clampInt(seed.value, 0, 1_000_000, 42);
    const rn = clampInt(run.value, 0, 1_000_000, 1);
    const disturb = readDisturb();
    launch.disabled = true;
    launch.textContent = "…";
    machine.beginRelaunch(sc, sd, rn, disturbSummary(disturb));
    const port = await bridge.relaunch(sc, sd, rn, disturb);
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
  for (const el of allInputs) {
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
