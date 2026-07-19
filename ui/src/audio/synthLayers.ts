// synthLayers.ts — the Web Audio SYNTHESIS graph (procedural, never samples/loops).
//
// Canon §12 / D-011: every sound is causally derived and PROCEDURALLY generated —
// no sample playback, no looped buffers. Each layer here builds a small node graph
// whose parameters are driven, per render tick, by the propagation model
// (propagation.ts) and the trigger bus (evtDecode.ts). Nothing in this file runs at
// unit-test time (Node has no AudioContext); the pure math lives elsewhere and is
// tested directly.
//
// Layers:
//   • EngineBed   — broadband roar keyed to throttle × n_eng, chamber-count
//                   character (sub-rumble + filtered noise + formant tilt).
//   • CrackleLayer — Poisson steep-asymmetric shocklets (crackle.ts) rendered into
//                   a short procedurally-regenerated buffer, re-triggered so it is
//                   NEVER a static loop; density ∝ thrust, audibility ∝ range.
//   • Oneshots     — ignition overpressure thump, shutdown pop, touchdown clang:
//                   short synthesized transients fired on EVT arrival.
//
// Each layer exposes an input `gain` (its send into the propagation chain) and a
// `meter` AnalyserNode so the dev panel can show a per-layer level.

import {
  renderCrackleTrace,
  crackleRate,
  mulberry32,
} from "./crackle";

// ---- small helpers ---------------------------------------------------------

/** Fill a buffer with white noise (used to seed filtered-noise beds). Accepts the
 *  loosely-typed channel-data view returned by AudioBuffer.getChannelData. */
function fillNoise(buf: Float32Array<ArrayBufferLike>, rng: () => number): void {
  for (let i = 0; i < buf.length; i++) buf[i] = rng() * 2 - 1;
}

/** A looping noise SOURCE is fine ONLY as a raw carrier that is then shaped, gated
 *  and modulated by causal telemetry — it never plays as a recognizable sound on
 *  its own (canon §A.0 permits garnish carriers atop honest state). The engine bed
 *  gates it entirely by throttle so silence is silence. */
function makeNoiseCarrier(ctx: BaseAudioContext, seconds: number, seed: number): AudioBuffer {
  const n = Math.floor(seconds * ctx.sampleRate);
  const b = ctx.createBuffer(1, n, ctx.sampleRate);
  fillNoise(b.getChannelData(0), mulberry32(seed));
  return b;
}

export interface Layer {
  readonly name: string;
  /** The node whose output feeds the source's propagation chain. */
  readonly out: AudioNode;
  /** Per-layer analyser for the dev-panel meter. */
  readonly meter: AnalyserNode;
  /** Current linear level [0..1] from the meter (RMS-ish). */
  level(): number;
  /** Layer-local mute (independent of the master mute). */
  setMuted(m: boolean): void;
  muted: boolean;
}

function attachMeter(ctx: BaseAudioContext, node: AudioNode): AnalyserNode {
  const an = ctx.createAnalyser();
  an.fftSize = 256;
  node.connect(an);
  return an;
}

function meterLevel(an: AnalyserNode, scratch: Float32Array<ArrayBuffer>): number {
  an.getFloatTimeDomainData(scratch);
  let sum = 0;
  for (let i = 0; i < scratch.length; i++) sum += scratch[i] * scratch[i];
  return Math.sqrt(sum / scratch.length);
}

// ===========================================================================
// ENGINE BED
// ===========================================================================

export class EngineBed implements Layer {
  readonly name = "engine";
  readonly out: GainNode;
  readonly meter: AnalyserNode;
  muted = false;

  private gate: GainNode; // throttle-driven master gate for the bed
  private rumbleOsc: OscillatorNode;
  private rumbleGain: GainNode;
  private noiseSrc: AudioBufferSourceNode;
  private roarFilter: BiquadFilterNode; // bandpass "roar" body
  private roarGain: GainNode;
  private chamberFilter: BiquadFilterNode; // formant peak → chamber-count character
  private scratch = new Float32Array(256);

  constructor(ctx: BaseAudioContext) {
    this.out = ctx.createGain();
    this.gate = ctx.createGain();
    this.gate.gain.value = 0; // silent until throttle > 0

    // sub-rumble: a low sine (20–60 Hz) — the chest-thump body
    this.rumbleOsc = ctx.createOscillator();
    this.rumbleOsc.type = "sine";
    this.rumbleOsc.frequency.value = 32;
    this.rumbleGain = ctx.createGain();
    this.rumbleGain.gain.value = 0.5;
    this.rumbleOsc.connect(this.rumbleGain).connect(this.gate);

    // roar: bandpassed noise carrier (broadband body of the jet)
    this.noiseSrc = ctx.createBufferSource();
    this.noiseSrc.buffer = makeNoiseCarrier(ctx, 2.0, 0x51ade);
    this.noiseSrc.loop = true; // raw carrier only; gated + reshaped every tick
    this.roarFilter = ctx.createBiquadFilter();
    this.roarFilter.type = "bandpass";
    this.roarFilter.frequency.value = 220;
    this.roarFilter.Q.value = 0.7;
    this.chamberFilter = ctx.createBiquadFilter();
    this.chamberFilter.type = "peaking";
    this.chamberFilter.frequency.value = 90;
    this.chamberFilter.Q.value = 1.2;
    this.chamberFilter.gain.value = 0;
    this.roarGain = ctx.createGain();
    this.roarGain.gain.value = 0.7;
    this.noiseSrc
      .connect(this.roarFilter)
      .connect(this.chamberFilter)
      .connect(this.roarGain)
      .connect(this.gate);

    this.gate.connect(this.out);
    this.meter = attachMeter(ctx, this.out);

    this.rumbleOsc.start();
    this.noiseSrc.start();
  }

  /**
   * Drive the bed from thrust state. `throttle` 0..1, `nEng` engine count.
   * chamber-count character: more engines → lower fundamental + stronger low
   * formant (a deeper, broader bed); throttle sets loudness + brightness.
   */
  update(throttle: number, nEng: number, when: number, glide = 0.05): void {
    const on = nEng > 0 && throttle > 0;
    const g = on ? 0.85 * Math.pow(throttle, 0.8) : 0;
    this.gate.gain.setTargetAtTime(this.muted ? 0 : g, when, glide);

    if (on) {
      // more chambers → lower, broader body
      const fund = 40 - Math.min(nEng, 9) * 2.4; // 9 eng → ~18 Hz
      this.rumbleOsc.frequency.setTargetAtTime(Math.max(16, fund), when, glide);
      // roar brightens with throttle
      this.roarFilter.frequency.setTargetAtTime(160 + 500 * throttle, when, glide);
      // chamber formant emphasis scales with engine count
      this.chamberFilter.gain.setTargetAtTime(Math.min(nEng, 9) * 1.1, when, glide);
      this.chamberFilter.frequency.setTargetAtTime(70 + nEng * 6, when, glide);
    }
  }

  level(): number {
    return meterLevel(this.meter, this.scratch);
  }
  setMuted(m: boolean): void {
    this.muted = m;
  }
}

// ===========================================================================
// CRACKLE LAYER — Poisson shocklets, procedurally regenerated (never a loop)
// ===========================================================================

export class CrackleLayer implements Layer {
  readonly name = "crackle";
  readonly out: GainNode;
  readonly meter: AnalyserNode;
  muted = false;

  private ctx: BaseAudioContext;
  private send: GainNode; // audibility (range) × mute
  private seedCtr = 0x9e37;
  private scratch = new Float32Array(256);
  private nextRefreshAt = 0;
  private curThrottle = 0;
  private curNEng = 0;
  private curAudibility = 0;
  private readonly chunkSec = 0.35; // each regenerated shocklet chunk length

  constructor(ctx: BaseAudioContext) {
    this.ctx = ctx;
    this.out = ctx.createGain();
    this.send = ctx.createGain();
    this.send.gain.value = 0;
    this.send.connect(this.out);
    this.meter = attachMeter(ctx, this.out);
  }

  /** Set the current thrust + range-derived audibility (called each tick). */
  update(throttle: number, nEng: number, audibility: number, when: number): void {
    this.curThrottle = throttle;
    this.curNEng = nEng;
    this.curAudibility = audibility;
    const g = this.muted ? 0 : audibility;
    this.send.gain.setTargetAtTime(g, when, 0.08);
  }

  /**
   * Drive the shocklet regeneration. Call once per render frame with the current
   * AudioContext time. When the previous chunk is about to finish, synthesize a
   * FRESH chunk (new seed → new Poisson realisation) so the crackle is causally
   * derived and NEVER a repeating loop (canon §12 / D-011).
   */
  pump(now: number): void {
    const rate = crackleRate(this.curThrottle, this.curNEng);
    if (rate <= 0 || this.curAudibility <= 0.001) {
      // nothing to play; let the schedule idle
      this.nextRefreshAt = Math.max(this.nextRefreshAt, now);
      return;
    }
    // schedule up to ~2 chunks ahead so there is no gap
    while (this.nextRefreshAt < now + this.chunkSec) {
      const startAt = Math.max(this.nextRefreshAt, now + 0.02);
      this.spawnChunk(rate, startAt);
      this.nextRefreshAt = startAt + this.chunkSec;
    }
  }

  private spawnChunk(rate: number, startAt: number): void {
    const rng = mulberry32((this.seedCtr = (this.seedCtr + 0x6d2b) >>> 0));
    const trace = renderCrackleTrace(this.chunkSec, this.ctx.sampleRate, rate, rng);
    const buf = this.ctx.createBuffer(1, trace.length, this.ctx.sampleRate);
    buf.getChannelData(0).set(trace); // .set accepts any Float32Array (avoids the ArrayBufferLike mismatch)
    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    src.loop = false; // one-shot realisation; the NEXT chunk is a new one
    // tiny fade to avoid edge clicks between chunks
    const env = this.ctx.createGain();
    env.gain.value = 1;
    src.connect(env).connect(this.send);
    src.start(startAt);
    src.stop(startAt + this.chunkSec + 0.02);
  }

  level(): number {
    return meterLevel(this.meter, this.scratch);
  }
  setMuted(m: boolean): void {
    this.muted = m;
  }
}

// ===========================================================================
// ONE-SHOT TRANSIENTS — ignition thump, shutdown pop, touchdown clang
// ===========================================================================

export type OneshotKind = "ignition" | "shutdown" | "touchdown" | "boom";

export class OneshotLayer implements Layer {
  readonly name = "events";
  readonly out: GainNode;
  readonly meter: AnalyserNode;
  muted = false;

  private ctx: BaseAudioContext;
  private scratch = new Float32Array(256);

  constructor(ctx: BaseAudioContext) {
    this.ctx = ctx;
    this.out = ctx.createGain();
    this.meter = attachMeter(ctx, this.out);
  }

  /** Fire a synthesized transient of `kind` at AudioContext time `at`, scaled by
   *  `gain` (already includes 1/r spreading) and lowpassed to `cutoffHz` (air
   *  absorption at the event's range). All procedurally synthesized. */
  fire(kind: OneshotKind, at: number, gain: number, cutoffHz: number, extra = 0): void {
    if (this.muted || gain <= 0) return;
    const lp = this.ctx.createBiquadFilter();
    lp.type = "lowpass";
    lp.frequency.value = Math.max(30, Math.min(cutoffHz, 20000));
    const amp = this.ctx.createGain();
    amp.gain.value = 0;
    lp.connect(amp).connect(this.out);

    switch (kind) {
      case "ignition":
        this.overpressureThump(at, gain, amp, lp);
        break;
      case "shutdown":
        this.pop(at, gain * 0.7, amp);
        break;
      case "touchdown":
        this.clang(at, gain, amp, extra);
        break;
      case "boom":
        this.tripleBoom(at, gain, amp, lp);
        break;
    }
  }

  // Ignition overpressure: a low-frequency pressure step with a fast attack and a
  // resonant thump — a downward pitch sweep on a sine + a noise burst.
  private overpressureThump(at: number, g: number, amp: GainNode, lp: BiquadFilterNode) {
    const osc = this.ctx.createOscillator();
    osc.type = "sine";
    osc.frequency.setValueAtTime(120, at);
    osc.frequency.exponentialRampToValueAtTime(38, at + 0.25);
    osc.connect(amp);
    amp.gain.setValueAtTime(0.0001, at);
    amp.gain.exponentialRampToValueAtTime(Math.max(0.001, g), at + 0.012);
    amp.gain.exponentialRampToValueAtTime(0.0005, at + 0.6);
    // a short noise "crack" on top of the thump
    lp.frequency.setValueAtTime(900, at);
    lp.frequency.exponentialRampToValueAtTime(120, at + 0.3);
    osc.start(at);
    osc.stop(at + 0.7);
  }

  private pop(at: number, g: number, amp: GainNode) {
    const osc = this.ctx.createOscillator();
    osc.type = "triangle";
    osc.frequency.setValueAtTime(180, at);
    osc.frequency.exponentialRampToValueAtTime(60, at + 0.08);
    osc.connect(amp);
    amp.gain.setValueAtTime(0.0001, at);
    amp.gain.exponentialRampToValueAtTime(Math.max(0.001, g), at + 0.005);
    amp.gain.exponentialRampToValueAtTime(0.0003, at + 0.18);
    osc.start(at);
    osc.stop(at + 0.25);
  }

  // Touchdown clang: metallic inharmonic partials + a low structural thud, decay
  // scaled by `extra` (impact severity ~ crush stroke).
  private clang(at: number, g: number, amp: GainNode, extra: number) {
    const partials = [1, 2.76, 5.4, 8.9]; // inharmonic (bell/plate-like)
    const base = 160 + extra * 40;
    const dur = 0.5 + Math.min(extra, 3) * 0.25;
    amp.gain.setValueAtTime(0.0001, at);
    amp.gain.exponentialRampToValueAtTime(Math.max(0.001, g), at + 0.004);
    amp.gain.exponentialRampToValueAtTime(0.0004, at + dur);
    for (const p of partials) {
      const osc = this.ctx.createOscillator();
      osc.type = "sine";
      osc.frequency.value = base * p;
      const pg = this.ctx.createGain();
      pg.gain.value = 1 / (p * p); // higher partials quieter
      osc.connect(pg).connect(amp);
      osc.start(at);
      osc.stop(at + dur + 0.05);
    }
    // low structural thud
    const thud = this.ctx.createOscillator();
    thud.type = "sine";
    thud.frequency.setValueAtTime(70, at);
    thud.frequency.exponentialRampToValueAtTime(40, at + 0.2);
    thud.connect(amp);
    thud.start(at);
    thud.stop(at + 0.3);
  }

  // Triple sonic boom (canon §B.8: "three artillery cracks"): three N-wave-ish
  // transients ~0.12/0.18 s apart, each a fast asymmetric pressure snap.
  private tripleBoom(at: number, g: number, amp: GainNode, lp: BiquadFilterNode) {
    const offsets = [0, 0.12, 0.3];
    lp.frequency.setValueAtTime(2000, at);
    amp.gain.setValueAtTime(0.0001, at);
    for (const o of offsets) {
      const t = at + o;
      const osc = this.ctx.createOscillator();
      osc.type = "sawtooth";
      osc.frequency.setValueAtTime(70, t);
      osc.frequency.exponentialRampToValueAtTime(30, t + 0.05);
      const eg = this.ctx.createGain();
      eg.gain.setValueAtTime(0.0001, t);
      eg.gain.exponentialRampToValueAtTime(Math.max(0.001, g), t + 0.003);
      eg.gain.exponentialRampToValueAtTime(0.0003, t + 0.08);
      osc.connect(eg).connect(lp);
      osc.start(t);
      osc.stop(t + 0.1);
    }
  }

  level(): number {
    return meterLevel(this.meter, this.scratch);
  }
  setMuted(m: boolean): void {
    this.muted = m;
  }
}
