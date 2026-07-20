// injectPanel.ts — the FAILURE-MODE control panel (Mode 2 interactive, canon §M2).
//
// A cockpit control strip whose buttons INJECT failures into the LIVE descent by
// sending BlCmd frames upstream (net/commands.ts) via TelemetryClient.send(). This is
// the ONE place the renderer crosses the observer boundary, and only when core runs
// `--serve --interactive` (otherwise the server drops the frames — the panel still
// "works", it just no-ops, so we show a hint). Determinism is deliberately waived on
// this path; the server journals every injection. The panel owns NO vehicle truth — it
// only emits commands and shows transient local feedback.

import { gustCmd, engineOutCmd, thrustLossCmd } from "../net/commands";

export interface InjectPanel {
  root: HTMLElement;
  /** For tests / external drivers: fire a named action programmatically. */
  fire(action: "gust" | "engineOut" | "thrustLoss"): void;
}

type Send = (bytes: ArrayBuffer) => void;

/** Mount the failure-mode panel (bottom-center). `send` is TelemetryClient.send. */
export function installInjectPanel(send: Send): InjectPanel {
  let seq = 1;

  const root = document.createElement("div");
  root.id = "inject-panel";
  root.style.cssText =
    "position:fixed;bottom:16px;left:50%;transform:translateX(-50%);z-index:24;" +
    "display:flex;flex-direction:column;align-items:center;gap:6px;" +
    "font:12px ui-monospace,Menlo,Consolas,monospace;pointer-events:none";

  const row = document.createElement("div");
  row.style.cssText = "display:flex;gap:10px;pointer-events:auto";
  root.appendChild(row);

  const hint = document.createElement("div");
  hint.style.cssText =
    "color:#5f7a8c;font-size:10px;letter-spacing:.04em;text-shadow:0 1px 2px #000a;" +
    "opacity:.85;transition:opacity .3s,color .3s";
  hint.textContent = "FAILURE INJECTION · needs core --serve --interactive";
  root.appendChild(hint);

  // one glowing cockpit button
  function mkButton(label: string, accent: string): HTMLButtonElement {
    const b = document.createElement("button");
    b.textContent = label;
    b.style.cssText =
      "pointer-events:auto;cursor:pointer;user-select:none;" +
      "background:rgba(10,14,18,.72);color:" + accent + ";" +
      "border:1px solid " + accent + "66;border-radius:6px;padding:9px 16px;" +
      "font:600 12px ui-monospace,Menlo,Consolas,monospace;letter-spacing:.05em;" +
      "box-shadow:0 2px 10px #0007, inset 0 0 0 1px #ffffff08;transition:all .12s";
    b.onpointerenter = () => {
      b.style.background = "rgba(20,28,36,.85)";
      b.style.boxShadow = "0 2px 16px " + accent + "55, inset 0 0 0 1px " + accent + "44";
    };
    b.onpointerleave = () => {
      b.style.background = "rgba(10,14,18,.72)";
      b.style.boxShadow = "0 2px 10px #0007, inset 0 0 0 1px #ffffff08";
    };
    return b;
  }

  function flash(b: HTMLButtonElement, accent: string, msg: string): void {
    b.style.background = accent;
    b.style.color = "#0b0e12";
    setTimeout(() => {
      b.style.background = "rgba(10,14,18,.72)";
      b.style.color = accent;
    }, 140);
    hint.textContent = "▶ " + msg;
    hint.style.color = accent;
    hint.style.opacity = "1";
    setTimeout(() => {
      hint.style.color = "#5f7a8c";
      hint.style.opacity = ".85";
      hint.textContent = "FAILURE INJECTION · needs core --serve --interactive";
    }, 1600);
  }

  const GUST = "#6fb3ff";
  const EO = "#ff8a5c";
  const THR = "#ffd166";
  const gustBtn = mkButton("💨 WIND GUST", GUST);
  const eoBtn = mkButton("🔥 ENGINE OUT", EO);
  const thrBtn = mkButton("📉 THRUST LOSS", THR);
  row.append(gustBtn, eoBtn, thrBtn);

  function doGust(): void {
    // a RANDOM shear: peak 15–30 m/s from a random bearing (the server centers the
    // 1-cosine band at the vehicle's current altitude, so it hits immediately).
    const peak = 15 + Math.random() * 15;
    const dir = Math.random() * 360;
    send(gustCmd(seq++, peak, dir));
    flash(gustBtn, GUST, `WIND GUST · ${peak.toFixed(0)} m/s @ ${dir.toFixed(0)}°`);
  }
  function doEngineOut(): void {
    // 0 => the server picks a seeded side engine; it fires on the next multi-engine burn.
    send(engineOutCmd(seq++, 0));
    flash(eoBtn, EO, "ENGINE OUT · side engine");
  }
  function doThrustLoss(): void {
    // a RANDOM sudden underperformance: engines drop to 50–75% of rated thrust, live.
    const frac = 0.5 + Math.random() * 0.25;
    send(thrustLossCmd(seq++, frac));
    flash(thrBtn, THR, `THRUST LOSS · ${(frac * 100).toFixed(0)}% rated`);
  }

  gustBtn.onclick = doGust;
  eoBtn.onclick = doEngineOut;
  thrBtn.onclick = doThrustLoss;

  document.body.appendChild(root);

  return {
    root,
    fire(action) {
      if (action === "gust") doGust();
      else if (action === "engineOut") doEngineOut();
      else if (action === "thrustLoss") doThrustLoss();
    },
  };
}
