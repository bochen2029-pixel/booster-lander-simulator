// audio/index.ts — public surface of the S3 Tier-A Web Audio SKETCH.
//
// One additive, self-contained mount for main.ts. The audio observer is a THIRD
// pure observer (canon §B.8): it consumes the same decoded TLM + raw EVT bytes the
// renderer already routes, converts them to procedurally-synthesized,
// propagation-honest sound, and writes nothing back. It is MUTED BY DEFAULT — the
// dev panel's ENABLE button is the required first-interaction gesture that resumes
// the AudioContext (canon §B.8: "audio context resumes on first interaction").
//
// Wiring contract for main.ts (all additive; touches nothing else):
//     import { mountAudio } from "./audio";
//     const audio = mountAudio();              // builds engine + dev panel, muted
//     client handlers:  onTlm: (f) => { interp.push(f); audio.onTlm(f); }
//                       onEvtBytes: (b) => audio.onEvtBytes(b)
//     render loop:      audio.tick();          // keeps crackle regenerating
//                       audio.updatePanel();   // refresh meters/readout
//     director:         audio.setListener(camWorldThree)   // when the camera moves

import { AudioEngine, type AudioEngineOpts } from "./audioEngine";
import { mountDevPanel, type DevPanelHandle } from "./devPanel";
import type { TlmFrame } from "../net/decode";
import type { Vec3 } from "./propagation";

export { AudioEngine } from "./audioEngine";
export type { AudioStatus, LayerMeter } from "./audioEngine";
export * from "./propagation";
export * from "./crackle";
export * as evt from "./evtDecode";

export interface AudioMount {
  engine: AudioEngine;
  panel: DevPanelHandle;
  onTlm(f: TlmFrame): void;
  onEvtBytes(buf: ArrayBuffer): void;
  /** Call once per render frame (pumps crackle regeneration + panel meters). */
  tick(): void;
  updatePanel(): void;
  /** Director hook: set the active-camera listener (THREE world coords). */
  setListener(pos: Vec3): void;
  destroy(): Promise<void>;
}

/**
 * Build the audio observer + its dev panel. Muted by default; no sound until the
 * user clicks ENABLE (which resumes the AudioContext from a user gesture). Returns
 * a small handle main.ts wires into the existing client + render loop additively.
 */
export function mountAudio(opts: AudioEngineOpts = {}): AudioMount {
  const engine = new AudioEngine(opts);
  // build the context graph eagerly (suspended + muted → silent, but meters exist)
  engine.init();
  const panel = mountDevPanel(engine, () => void engine.resume());

  return {
    engine,
    panel,
    onTlm: (f) => engine.onTlm(f),
    onEvtBytes: (b) => engine.onEvtBytes(b),
    tick: () => engine.tick(),
    updatePanel: () => panel.update(),
    setListener: (p) => engine.setListener(p),
    destroy: async () => {
      panel.destroy();
      await engine.dispose();
    },
  };
}
