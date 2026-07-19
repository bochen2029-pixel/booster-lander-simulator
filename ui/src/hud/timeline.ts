// timeline.ts — the EVT TIMELINE glyph scrubber (brainstorm §2 bottom rail, §5 S1
// item 4). DISPLAY-ONLY in S1: scrubbing lands in S2 (the ring buffer + re-sim).
// Here we render the EVT beats LIVE as they arrive, laid out along a time axis.
//
// Each EVT frame becomes a glyph on the timeline at its sim time `t`. The axis
// auto-extends as time advances (the run's duration isn't known ahead). A moving
// "now" cursor tracks the newest telemetry time. The beats accumulate so the whole
// descent reads as a strip of events — ignition ▲, mach1 ≋, legs ⋔, touchdown ⊥.
//
// Telemetry-honest: populated ONLY from EVT frames (the trigger bus). No synthetic
// beats. The layout math (t -> x fraction) is a pure function, unit-tested.

import { EVT_GLYPH, EVT_LABEL, type EvtFrame, EvtCode } from "../net/events";

/** Pure: map a sim time to an x-fraction [0,1] across the timeline window
 *  [t0, tNow]. Clamped. Tested in timeline.test.ts. */
export function timeToFrac(t: number, t0: number, tNow: number): number {
  const span = tNow - t0;
  if (span <= 1e-6) return 0;
  return Math.min(1, Math.max(0, (t - t0) / span));
}

const CSS = `
.evt-timeline {
  position: fixed; left: 0; right: 0; bottom: 0; height: 52px; z-index: 20;
  background: linear-gradient(180deg, rgba(6,10,14,0) 0%, rgba(6,10,14,.72) 40%);
  pointer-events: none; font: 10px ui-monospace, Menlo, monospace; color: #8fb0c4;
}
.evt-timeline .axis {
  position: absolute; left: 12px; right: 12px; bottom: 16px; height: 2px;
  background: rgba(120,150,175,.28);
}
.evt-timeline .now {
  position: absolute; bottom: 10px; width: 1px; height: 16px; background: #7fe0a8;
  box-shadow: 0 0 6px #7fe0a8;
}
.evt-timeline .beat {
  position: absolute; bottom: 12px; transform: translateX(-50%); text-align: center;
  color: #bfe0d0;
}
.evt-timeline .beat .glyph { font-size: 13px; line-height: 1; text-shadow: 0 0 5px rgba(127,224,168,.5); }
.evt-timeline .beat .tag { font-size: 8px; color: #6f8ea2; white-space: nowrap; }
.evt-timeline .caption {
  position: absolute; left: 12px; bottom: 34px; color: #5f7a8c; letter-spacing: .05em;
}
`;

interface Beat {
  t: number;
  code: EvtCode;
  el: HTMLElement;
}

export interface TimelineHandle {
  root: HTMLElement;
  /** Add a beat live as an EVT arrives. */
  onEvt(evt: EvtFrame): void;
  /** Advance the "now" cursor + relayout beats against [t0, tNow]. */
  tick(tNow: number): void;
  dispose(): void;
}

export function installTimeline(): TimelineHandle {
  const style = document.createElement("style");
  style.textContent = CSS;
  document.head.appendChild(style);

  const root = document.createElement("div");
  root.className = "evt-timeline";
  const axis = document.createElement("div");
  axis.className = "axis";
  const now = document.createElement("div");
  now.className = "now";
  const caption = document.createElement("div");
  caption.className = "caption";
  caption.textContent = "EVT TIMELINE";
  root.append(axis, now, caption);
  document.body.appendChild(root);

  const beats: Beat[] = [];
  let t0 = 0;
  let haveT0 = false;

  function layout(tNow: number): void {
    // axis spans 12px..(width-12px)
    const left = 12;
    const width = root.clientWidth - 24;
    for (const b of beats) {
      const frac = timeToFrac(b.t, t0, tNow);
      b.el.style.left = `${left + frac * width}px`;
    }
    const nowFrac = timeToFrac(tNow, t0, tNow);
    now.style.left = `${left + nowFrac * width}px`;
  }

  const handle: TimelineHandle = {
    root,
    onEvt(evt) {
      if (!haveT0) {
        t0 = evt.t;
        haveT0 = true;
      }
      const el = document.createElement("div");
      el.className = "beat";
      const glyph = document.createElement("div");
      glyph.className = "glyph";
      glyph.textContent = EVT_GLYPH[evt.code as EvtCode] ?? "•";
      const tag = document.createElement("div");
      tag.className = "tag";
      tag.textContent = EVT_LABEL[evt.code as EvtCode] ?? "";
      el.append(glyph, tag);
      el.title = `${EVT_LABEL[evt.code as EvtCode] ?? evt.code} @ ${evt.t.toFixed(2)}s`;
      root.appendChild(el);
      beats.push({ t: evt.t, code: evt.code, el });
    },
    tick(tNow) {
      if (!haveT0) return;
      layout(tNow);
    },
    dispose() {
      root.remove();
      style.remove();
    },
  };
  return handle;
}
