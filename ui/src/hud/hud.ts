// hud.ts — the S1 HUD strip (brainstorm §5 S1 item 3, canon §B.7 flat layer).
//
// DOM-based, imperative, telemetry NEVER enters a React render path (canon §1:
// "Zustand for control state only — telemetry never enters the React render
// path"). This is the flat-glass sibling of the diegetic markers. Every readout
// is a streamed field or an EVT-derived state.
//
// Contents:
//   - PHASE LADDER: the 14 phases + the live phase from TLM (and EVT PHASE_CHANGE
//     beats drive the highlight). The guidance's climb up the ladder.
//   - t_go, fuel (LOX/RP1), altitude, speed, mach, throttle
//   - qbar-vs-STRUCT-line bar (the aero pressure against the structural limit)
//   - verdict badge on touchdown
//   - frame-time strip (the 8.3 ms budget made visible — canon §B.9)
//
// Theming: CSS custom properties (nominal / caution / abort), swapped by a data-
// attr on the root — the TERMINAL pattern (brainstorm_terminal §2). No per-frame
// reflow beyond textContent / width / class writes.

import type { InterpSample } from "../net/interp";
import { Phase, Verdict } from "../net/decode";
import { EVT_LABEL, type EvtFrame, EvtCode } from "../net/events";

const PHASE_NAMES: Record<Phase, string> = {
  [Phase.Init]: "INIT",
  [Phase.Coast]: "COAST",
  [Phase.EntryBurn]: "ENTRY BURN",
  [Phase.AeroDescent]: "AERO DESCENT",
  [Phase.LandingBurn]: "LANDING BURN",
  [Phase.Touchdown]: "TOUCHDOWN",
  [Phase.Settling]: "SETTLING",
  [Phase.Landed]: "LANDED",
  [Phase.Tipped]: "TIPPED",
  [Phase.Crashed]: "CRASHED",
  [Phase.FuelDepleted]: "FUEL OUT",
  [Phase.StructFail]: "STRUCT FAIL",
  [Phase.ThermalFail]: "THERMAL FAIL",
  [Phase.LOC]: "LOSS OF CTRL",
};

// The nominal descent ladder (the beats the guidance climbs, in order). Terminal
// failure phases are shown only if reached.
const LADDER: Phase[] = [
  Phase.Coast,
  Phase.EntryBurn,
  Phase.AeroDescent,
  Phase.LandingBurn,
  Phase.Touchdown,
  Phase.Landed,
];

const VERDICT_TEXT: Record<Verdict, string> = {
  [Verdict.None]: "—",
  [Verdict.LandedPerfect]: "PERFECT",
  [Verdict.LandedGood]: "GOOD",
  [Verdict.LandedHard]: "HARD",
  [Verdict.Tipped]: "TIPPED",
  [Verdict.Crashed]: "CRASHED",
};

const VERDICT_CLASS: Record<Verdict, string> = {
  [Verdict.None]: "",
  [Verdict.LandedPerfect]: "verdict-good",
  [Verdict.LandedGood]: "verdict-good",
  [Verdict.LandedHard]: "verdict-caution",
  [Verdict.Tipped]: "verdict-abort",
  [Verdict.Crashed]: "verdict-abort",
};

// structural qbar reference [Pa] — the STRUCT line the aero bar is drawn against.
// (Canon lists qbar vs the STRUCT line; the shipped STATS carries max_qbar but no
// structural limit constant, so we use a documented reference. Tagged artistic.)
const QBAR_STRUCT_PA = 45000; // ~45 kPa reference max-q envelope

const CSS = `
.hud-root {
  --nominal: #7fe0a8; --caution: #ffcf6b; --abort: #ff6b6b;
  --dim: #5a7080; --bg: rgba(6,10,14,.62); --line: rgba(120,150,175,.25);
  position: fixed; inset: 0; pointer-events: none; z-index: 20;
  font: 12px/1.35 ui-monospace, "SF Mono", Menlo, monospace; color: #cfe3f0;
}
.hud-phase-ladder {
  position: absolute; left: 14px; top: 84px; display: flex; flex-direction: column;
  gap: 3px; background: var(--bg); padding: 8px 10px; border: 1px solid var(--line);
  border-radius: 4px; min-width: 150px;
}
.hud-phase-ladder .rung { display: flex; align-items: center; gap: 7px; color: var(--dim); }
.hud-phase-ladder .rung.done { color: #9fb4c4; }
.hud-phase-ladder .rung.active { color: var(--nominal); font-weight: 600; }
.hud-phase-ladder .rung .dot { width: 7px; height: 7px; border-radius: 50%; background: currentColor; opacity: .55; }
.hud-phase-ladder .rung.active .dot { opacity: 1; box-shadow: 0 0 6px currentColor; }
.hud-left-stats {
  position: absolute; left: 14px; bottom: 96px; background: var(--bg);
  padding: 8px 10px; border: 1px solid var(--line); border-radius: 4px; min-width: 150px;
  white-space: pre; display: grid; grid-template-columns: auto auto; gap: 2px 12px;
}
.hud-left-stats .k { color: var(--dim); }
.hud-left-stats .v { text-align: right; color: #dcecf6; }
.hud-qbar {
  position: absolute; left: 14px; bottom: 60px; width: 172px; background: var(--bg);
  border: 1px solid var(--line); border-radius: 4px; padding: 6px 8px;
}
.hud-qbar .label { color: var(--dim); font-size: 10px; display: flex; justify-content: space-between; }
.hud-qbar .track { position: relative; height: 8px; margin-top: 4px; background: rgba(255,255,255,.06); border-radius: 3px; overflow: hidden; }
.hud-qbar .fill { position: absolute; left: 0; top: 0; bottom: 0; background: var(--nominal); transition: none; }
.hud-qbar .struct { position: absolute; top: -2px; bottom: -2px; width: 2px; background: var(--abort); }
.hud-verdict {
  position: absolute; top: 14px; right: 16px; background: var(--bg); border: 1px solid var(--line);
  border-radius: 4px; padding: 6px 12px; font-weight: 700; letter-spacing: .06em; color: var(--dim);
}
.hud-verdict.verdict-good { color: var(--nominal); border-color: var(--nominal); box-shadow: 0 0 14px rgba(127,224,168,.4); }
.hud-verdict.verdict-caution { color: var(--caution); border-color: var(--caution); }
.hud-verdict.verdict-abort { color: var(--abort); border-color: var(--abort); box-shadow: 0 0 14px rgba(255,107,107,.4); }
.hud-frametime {
  position: absolute; right: 16px; bottom: 60px; width: 172px; background: var(--bg);
  border: 1px solid var(--line); border-radius: 4px; padding: 6px 8px;
}
.hud-frametime .label { color: var(--dim); font-size: 10px; display: flex; justify-content: space-between; }
.hud-frametime .bars { display: flex; align-items: flex-end; gap: 1px; height: 22px; margin-top: 4px; }
.hud-frametime .bar { flex: 1; background: var(--nominal); min-height: 1px; }
.hud-frametime .bar.over { background: var(--abort); }
.hud-ticker {
  position: absolute; bottom: 60px; left: 50%; transform: translateX(-50%);
  color: var(--dim); font-size: 11px; letter-spacing: .04em; background: var(--bg);
  border: 1px solid var(--line); border-radius: 4px; padding: 4px 10px; max-width: 40vw; overflow: hidden; white-space: nowrap;
}
`;

const FT_SAMPLES = 60;

export interface HudHandle {
  root: HTMLElement;
  update(s: InterpSample, frameMs: number, extras?: { convergence?: number; missM?: number }): void;
  onEvt(evt: EvtFrame): void;
  dispose(): void;
}

export function installHud(): HudHandle {
  const style = document.createElement("style");
  style.textContent = CSS;
  document.head.appendChild(style);

  const root = document.createElement("div");
  root.className = "hud-root";

  // phase ladder
  const ladder = document.createElement("div");
  ladder.className = "hud-phase-ladder";
  const rungs = new Map<Phase, HTMLElement>();
  for (const ph of LADDER) {
    const rung = document.createElement("div");
    rung.className = "rung";
    const dot = document.createElement("span");
    dot.className = "dot";
    const lbl = document.createElement("span");
    lbl.textContent = PHASE_NAMES[ph];
    rung.append(dot, lbl);
    ladder.appendChild(rung);
    rungs.set(ph, rung);
  }
  root.appendChild(ladder);

  // left stats grid
  const stats = document.createElement("div");
  stats.className = "hud-left-stats";
  const rows: Record<string, HTMLElement> = {};
  for (const key of ["ALT", "SPD", "MACH", "t_go", "THR", "ENG", "LOX", "RP1"]) {
    const k = document.createElement("span");
    k.className = "k";
    k.textContent = key;
    const v = document.createElement("span");
    v.className = "v";
    v.textContent = "—";
    stats.append(k, v);
    rows[key] = v;
  }
  root.appendChild(stats);

  // qbar bar
  const qbar = document.createElement("div");
  qbar.className = "hud-qbar";
  qbar.innerHTML =
    `<div class="label"><span>qbar</span><span class="qval">0 Pa</span></div>` +
    `<div class="track"><div class="fill"></div><div class="struct"></div></div>`;
  const qFill = qbar.querySelector(".fill") as HTMLElement;
  const qVal = qbar.querySelector(".qval") as HTMLElement;
  const qStruct = qbar.querySelector(".struct") as HTMLElement;
  qStruct.style.left = "100%"; // STRUCT line at the right edge (== QBAR_STRUCT_PA)
  root.appendChild(qbar);

  // verdict badge
  const verdict = document.createElement("div");
  verdict.className = "hud-verdict";
  verdict.textContent = "VERDICT —";
  root.appendChild(verdict);

  // frame-time strip
  const ft = document.createElement("div");
  ft.className = "hud-frametime";
  ft.innerHTML = `<div class="label"><span>frame</span><span class="ftval">0.0 ms</span></div><div class="bars"></div>`;
  const ftBars = ft.querySelector(".bars") as HTMLElement;
  const ftVal = ft.querySelector(".ftval") as HTMLElement;
  const barEls: HTMLElement[] = [];
  for (let i = 0; i < FT_SAMPLES; i++) {
    const b = document.createElement("div");
    b.className = "bar";
    ftBars.appendChild(b);
    barEls.push(b);
  }
  const ftRing: number[] = new Array(FT_SAMPLES).fill(0);
  let ftHead = 0;
  root.appendChild(ft);

  // EVT ticker (the beats print here as they arrive)
  const ticker = document.createElement("div");
  ticker.className = "hud-ticker";
  ticker.textContent = "";
  root.appendChild(ticker);

  document.body.appendChild(root);

  let lastPhase = -1;

  const handle: HudHandle = {
    root,
    update(s, frameMs, extras) {
      const f = s.frame;

      // phase ladder highlight
      if (f.phase !== lastPhase) {
        lastPhase = f.phase;
        const activeIdx = LADDER.indexOf(f.phase as Phase);
        LADDER.forEach((ph, i) => {
          const rung = rungs.get(ph)!;
          rung.classList.remove("active", "done");
          if (i < activeIdx) rung.classList.add("done");
          else if (i === activeIdx) rung.classList.add("active");
        });
        // caution/abort palette on terminal failure phases
        const failed =
          f.phase === Phase.Crashed ||
          f.phase === Phase.Tipped ||
          f.phase === Phase.StructFail ||
          f.phase === Phase.ThermalFail ||
          f.phase === Phase.LOC ||
          f.phase === Phase.FuelDepleted;
        root.dataset.state = failed ? "abort" : "nominal";
      }

      // stats (alt = sim Z; speed = |v|)
      rows["ALT"].textContent = `${s.r.z.toFixed(0)} m`;
      rows["SPD"].textContent = `${s.v.length().toFixed(1)} m/s`;
      rows["MACH"].textContent = f.mach.toFixed(2);
      rows["t_go"].textContent = f.tGo > 0 ? `${f.tGo.toFixed(1)} s` : "—";
      rows["THR"].textContent = `${(f.throttleAct * 100).toFixed(0)}%`;
      // ENG: live engine count (v2 §4.6 engine-out readiness — n_eng has been on
      // the wire since v1 §10.3; when an ENGINE_OUT fires it drops 3→2 live).
      rows["ENG"].textContent = `${f.nEng}×`;
      rows["LOX"].textContent = `${f.propLox.toFixed(0)} kg`;
      rows["RP1"].textContent = `${f.propRp1.toFixed(0)} kg`;

      // qbar bar vs STRUCT line
      const qFrac = Math.min(1.15, f.qbar / QBAR_STRUCT_PA);
      qFill.style.width = `${Math.min(100, qFrac * 100)}%`;
      qVal.textContent = `${(f.qbar / 1000).toFixed(1)} kPa`;
      qFill.style.background =
        qFrac > 1 ? "var(--abort)" : qFrac > 0.8 ? "var(--caution)" : "var(--nominal)";

      // verdict badge
      const vtxt = VERDICT_TEXT[f.verdict as Verdict] ?? "—";
      verdict.textContent = `VERDICT ${vtxt}`;
      verdict.className = "hud-verdict " + (VERDICT_CLASS[f.verdict as Verdict] ?? "");

      // frame-time strip (8.3 ms budget line)
      ftRing[ftHead] = frameMs;
      ftHead = (ftHead + 1) % FT_SAMPLES;
      ftVal.textContent = `${frameMs.toFixed(1)} ms`;
      for (let i = 0; i < FT_SAMPLES; i++) {
        const ms = ftRing[(ftHead + i) % FT_SAMPLES];
        const b = barEls[i];
        const h = Math.min(22, (ms / 16.6) * 22); // scale: 16.6ms = full height
        b.style.height = `${Math.max(1, h)}px`;
        if (ms > 8.3) b.classList.add("over");
        else b.classList.remove("over");
      }
      void extras; // convergence/missM surfaced by the MIND rail (future); accepted here
    },

    onEvt(evt) {
      const label = EVT_LABEL[evt.code as EvtCode] ?? `EVT${evt.code}`;
      ticker.textContent = `${evt.t.toFixed(1)}s  ${label}`;
    },

    dispose() {
      root.remove();
      style.remove();
    },
  };
  return handle;
}
