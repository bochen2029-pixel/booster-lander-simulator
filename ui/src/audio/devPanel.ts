// devPanel.ts — a tiny developer overlay for the audio SKETCH.
//
// Shows: master enable/mute toggle, per-layer level meters + per-layer mute, and
// the computed retarded-time readout ("you are 6.2 s away") plus the propagation
// numbers (range, absorption knee, crackle audibility, Doppler, in-flight events).
// This is a debug surface, not the production HUD; the real HUD is fe-hud's job.
//
// It is DOM-only and self-contained. It never touches the audio graph except
// through the AudioEngine's public API.

import type { AudioEngine } from "./audioEngine";

export interface DevPanelHandle {
  update(): void;
  destroy(): void;
  el: HTMLElement;
}

function bar(label: string): { row: HTMLElement; fill: HTMLElement; mute: HTMLInputElement } {
  const row = document.createElement("div");
  row.style.cssText = "display:flex;align-items:center;gap:6px;margin:2px 0";
  const name = document.createElement("span");
  name.textContent = label;
  name.style.cssText = "width:56px;color:#9fb;opacity:.85";
  const track = document.createElement("div");
  track.style.cssText =
    "flex:1;height:8px;background:#0c1410;border:1px solid #1c3;border-radius:2px;overflow:hidden";
  const fill = document.createElement("div");
  fill.style.cssText = "height:100%;width:0%;background:linear-gradient(90deg,#2f6,#df4)";
  track.appendChild(fill);
  const mute = document.createElement("input");
  mute.type = "checkbox";
  mute.title = "mute " + label;
  mute.style.cssText = "accent-color:#f66;margin:0";
  row.append(name, track, mute);
  return { row, fill, mute };
}

/**
 * Mount the dev panel. Returns a handle whose `update()` should be called each
 * render frame (cheap). `onResumeRequest` is invoked when the user clicks ENABLE —
 * the caller wires it to engine.resume() from that user gesture.
 */
export function mountDevPanel(
  engine: AudioEngine,
  onResumeRequest: () => void,
  layerNames: string[] = ["engine", "crackle", "events"]
): DevPanelHandle {
  const el = document.createElement("div");
  el.style.cssText =
    "position:fixed;right:8px;bottom:8px;z-index:20;width:260px;" +
    "font:11px ui-monospace,Menlo,Consolas,monospace;color:#cfe;" +
    "background:rgba(4,10,7,.82);border:1px solid #163;border-radius:6px;" +
    "padding:8px 10px;backdrop-filter:blur(3px);user-select:none";

  const title = document.createElement("div");
  title.style.cssText = "display:flex;justify-content:space-between;align-items:center;margin-bottom:6px";
  const heading = document.createElement("b");
  heading.textContent = "AUDIO · S3 sketch";
  heading.style.color = "#8fd";
  const enableBtn = document.createElement("button");
  enableBtn.textContent = "▶ ENABLE";
  enableBtn.style.cssText =
    "font:11px monospace;background:#123;color:#9fd;border:1px solid #2a6;" +
    "border-radius:3px;padding:2px 8px;cursor:pointer";
  title.append(heading, enableBtn);

  // the "you are N s away" readout
  const readout = document.createElement("div");
  readout.style.cssText =
    "margin:4px 0 6px;padding:4px 6px;background:#08120c;border-left:3px solid #2c8;" +
    "border-radius:2px;line-height:1.5";

  // per-layer meters
  const bars = layerNames.map((n) => {
    const b = bar(n);
    b.mute.addEventListener("change", () => engine.setLayerMuted(n, b.mute.checked));
    return { name: n, ...b };
  });
  const barsWrap = document.createElement("div");
  barsWrap.style.marginTop = "4px";
  bars.forEach((b) => barsWrap.appendChild(b.row));

  el.append(title, readout, barsWrap);
  document.body.appendChild(el);

  let enabled = false;
  enableBtn.addEventListener("click", () => {
    enabled = !enabled;
    if (enabled) {
      onResumeRequest();
      const nowMuted = engine.isMuted ? false : engine.isMuted; // unmute on enable
      engine.setMuted(false);
      enableBtn.textContent = "❚❚ MUTE";
      enableBtn.style.borderColor = "#c84";
      void nowMuted;
    } else {
      engine.setMuted(true);
      enableBtn.textContent = "▶ ENABLE";
      enableBtn.style.borderColor = "#2a6";
    }
  });

  function fmtDelay(s: number): string {
    if (!isFinite(s)) return "—";
    if (s < 1) return `${(s * 1000).toFixed(0)} ms`;
    return `${s.toFixed(1)} s`;
  }

  function update(): void {
    const st = engine.status();
    const rangeKm = st.range / 1000;
    const nextEv = isFinite(st.timeToNextEventSec) ? `${st.timeToNextEventSec.toFixed(1)}s` : "—";
    readout.innerHTML =
      `<span style="color:#7fd;font-size:13px">you are ${fmtDelay(st.delaySec)} away</span><br>` +
      `range ${rangeKm.toFixed(2)} km · knee ${st.cutoffHz.toFixed(0)} Hz<br>` +
      `crackle ${(st.crackleAudibility * 100).toFixed(0)}% · doppler ×${st.dopplerRatio.toFixed(2)}<br>` +
      `events in flight ${st.inFlightEvents} · next ${nextEv}<br>` +
      `ctx ${st.contextState}${st.muted ? " · MUTED" : ""}`;

    const byName = new Map(st.layers.map((l) => [l.name, l] as const));
    for (const b of bars) {
      const l = byName.get(b.name);
      const lvl = l ? Math.min(1, l.level * 6) : 0; // scale RMS for visibility
      b.fill.style.width = `${(lvl * 100).toFixed(0)}%`;
      b.fill.style.opacity = l && !l.muted ? "1" : "0.25";
    }
  }

  function destroy(): void {
    el.remove();
  }

  return { el, update, destroy };
}
