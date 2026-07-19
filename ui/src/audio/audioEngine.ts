// audioEngine.ts — the S3 audio observer orchestrator (Tier-A Web Audio SKETCH).
//
// A THIRD pure observer (canon §B.8): it consumes the SAME one-way telemetry the
// renderer does, decodes EVT itself (evtDecode.ts), and turns state + events into
// procedurally-synthesized, propagation-honest sound. It writes NOTHING back.
//
// Responsibilities:
//   • own the AudioContext + master bus (muted by default; resume on first gesture)
//   • setListener(pos): the active-camera listener (director calls this). Default =
//     a fixed pad-cam listener.
//   • onTlm(frame): drive the continuous engine bed + crackle from throttle×n_eng
//     and recompute the propagation snapshot from vehicle vs listener geometry.
//   • onEvtBytes(buf): decode EVT and ENQUEUE discrete sounds into the retarded-
//     time queue (ignition thump, shutdown pop, touchdown clang, sonic boom); drain
//     the queue on the listener clock so they arrive after the sound wall.
//   • expose per-layer meters/mutes + the computed arrival delay for the dev panel.
//
// The vehicle position it uses is the SOURCE; the listener is the camera. Positions
// are in the THREE render/world frame (converted by frame.ts). For the sketch we
// convert the sim-frame TLM r/v into three-frame here via the frozen frame.ts map,
// so the audio observer needs only the raw decoded frame.

import type { TlmFrame } from "../net/decode";
import { decodeEvt, EvtCode, type EvtFrame } from "./evtDecode";
import { simToThreePosArray } from "../net/frame";
import {
  computePropagation,
  RetardedEventQueue,
  slantRange,
  propagationDelay,
  type Vec3,
  type PropagationState,
} from "./propagation";
import { crackleAudibility, absorptionCutoffHz } from "./propagation";
import { EngineBed, CrackleLayer, OneshotLayer, type OneshotKind, type Layer } from "./synthLayers";
import { PropagationChain } from "./propagationChain";

/** Payload buffered per discrete acoustic event in the retarded-time queue. */
interface OneshotPayload {
  kind: OneshotKind;
  extra: number; // e.g. touchdown crush severity
}

export interface AudioEngineOpts {
  /** Default listener (pad long-lens cam) in THREE world coords. */
  defaultListener?: Vec3;
  /** Master target level once unmuted [0..1]. */
  masterLevel?: number;
}

export interface LayerMeter {
  name: string;
  level: number;
  muted: boolean;
}

export interface AudioStatus {
  running: boolean;
  muted: boolean;
  contextState: string;
  /** Slant range vehicle→listener [m]. */
  range: number;
  /** Continuous-bed retarded-time delay [s] — the "you are N s away" readout. */
  delaySec: number;
  cutoffHz: number;
  crackleAudibility: number;
  dopplerRatio: number;
  inFlightEvents: number;
  timeToNextEventSec: number;
  layers: LayerMeter[];
}

/** Default pad-cam listener: ~2 km back, ~40 m up, looking at the pad (three frame:
 *  X east, Y up, Z = -north). Sits south-east of the pad like a tracking camera. */
const DEFAULT_LISTENER: Vec3 = { x: 1400, y: 40, z: 1400 };

export class AudioEngine {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  private engine!: EngineBed;
  private crackle!: CrackleLayer;
  private oneshots!: OneshotLayer;
  private engineChain!: PropagationChain;
  private crackleChain!: PropagationChain;
  private layers: Layer[] = [];

  private listener: Vec3;
  private masterLevel: number;
  private muted = true; // MUTED BY DEFAULT (canon: sketch, toggle)

  private queue = new RetardedEventQueue<OneshotPayload>();
  private lastFrame: TlmFrame | null = null;
  private lastStatus: AudioStatus;

  // audio-context ↔ sim-time alignment: we peg the newest sim-time to "now" so the
  // retarded-time queue's listener clock advances with the stream.
  private simToCtxOffset = 0; // ctxTime ≈ simTime + offset
  private haveClockPeg = false;

  constructor(opts: AudioEngineOpts = {}) {
    this.listener = opts.defaultListener ?? { ...DEFAULT_LISTENER };
    this.masterLevel = opts.masterLevel ?? 0.7;
    this.lastStatus = this.idleStatus();
  }

  // ---- lifecycle ----------------------------------------------------------

  /** Lazily create the AudioContext + graph. Safe to call before any gesture; the
   *  context starts suspended and produces no sound until resume() + unmute. */
  init(): void {
    if (this.ctx) return;
    const Ctx: typeof AudioContext =
      (globalThis as any).AudioContext || (globalThis as any).webkitAudioContext;
    if (!Ctx) throw new Error("Web Audio API unavailable");
    const ctx = new Ctx();
    this.ctx = ctx;

    const master = ctx.createGain();
    master.gain.value = 0; // muted by default
    // a gentle limiter so transients never clip the sketch bus
    const limiter = ctx.createDynamicsCompressor();
    limiter.threshold.value = -6;
    limiter.knee.value = 6;
    limiter.ratio.value = 12;
    limiter.attack.value = 0.003;
    limiter.release.value = 0.15;
    master.connect(limiter).connect(ctx.destination);
    this.master = master;

    // layers
    this.engine = new EngineBed(ctx);
    this.crackle = new CrackleLayer(ctx);
    this.oneshots = new OneshotLayer(ctx);
    this.layers = [this.engine, this.crackle, this.oneshots];

    // continuous sources get a full propagation chain into the master bus
    this.engineChain = new PropagationChain(ctx, this.engine.out, master);
    this.crackleChain = new PropagationChain(ctx, this.crackle.out, master);
    // one-shots are already delayed by the retarded-time QUEUE (their delay/gain is
    // baked at release), so they connect straight to the master bus.
    this.oneshots.out.connect(master);
  }

  /** Resume the context (call from a user gesture — the splash-screen click). */
  async resume(): Promise<void> {
    this.init();
    if (this.ctx && this.ctx.state === "suspended") {
      await this.ctx.resume();
    }
  }

  get isRunning(): boolean {
    return !!this.ctx && this.ctx.state === "running";
  }

  // ---- the listener (active camera) API -----------------------------------

  /** The director calls this every time the active camera moves/cuts. Positions in
   *  THREE world coords. A camera CUT should crossfade propagation state; for the
   *  sketch we simply retarget and let the per-tick smoothing (0.08–0.15 s) glide. */
  setListener(pos: Vec3): void {
    this.listener = { x: pos.x, y: pos.y, z: pos.z };
  }

  getListener(): Vec3 {
    return { ...this.listener };
  }

  // ---- mute -----------------------------------------------------------------

  setMuted(m: boolean): void {
    this.muted = m;
    if (this.master && this.ctx) {
      this.master.gain.setTargetAtTime(m ? 0 : this.masterLevel, this.ctx.currentTime, 0.05);
    }
  }
  toggleMute(): boolean {
    this.setMuted(!this.muted);
    return this.muted;
  }
  get isMuted(): boolean {
    return this.muted;
  }

  setMasterLevel(v: number): void {
    this.masterLevel = Math.max(0, Math.min(1, v));
    if (!this.muted && this.master && this.ctx) {
      this.master.gain.setTargetAtTime(this.masterLevel, this.ctx.currentTime, 0.05);
    }
  }

  setLayerMuted(name: string, m: boolean): void {
    const l = this.layers.find((x) => x.name === name);
    l?.setMuted(m);
  }

  // ---- telemetry ingest (continuous bed) ----------------------------------

  /** Vehicle SOURCE position (three frame) from a decoded frame. */
  private sourcePos(f: TlmFrame): Vec3 {
    const [x, y, z] = simToThreePosArray(f.r[0], f.r[1], f.r[2]);
    return { x, y, z };
  }
  private sourceVel(f: TlmFrame): Vec3 {
    const [x, y, z] = simToThreePosArray(f.v[0], f.v[1], f.v[2]);
    return { x, y, z };
  }

  /** Feed one decoded telemetry frame. Drives the continuous engine + crackle. */
  onTlm(f: TlmFrame): void {
    this.lastFrame = f;
    if (!this.ctx) return;
    const now = this.ctx.currentTime;

    // peg sim-clock → ctx-clock the first time we see a frame, so the retarded-time
    // queue releases relative to the same wall clock the DelayNodes use.
    if (!this.haveClockPeg) {
      this.simToCtxOffset = now - f.t;
      this.haveClockPeg = true;
    }

    const src = this.sourcePos(f);
    const vel = this.sourceVel(f);
    const p: PropagationState = computePropagation(this.listener, src, vel, f.t);

    // continuous engine bed
    this.engine.update(f.throttleAct, f.nEng, now);
    this.engineChain.apply(p, now);
    this.engineChain.applyDopplerBias(p.dopplerRatio, now);

    // crackle: density ∝ thrust, audibility ∝ range (the spectral reveal)
    this.crackle.update(f.throttleAct, f.nEng, p.crackle, now);
    this.crackleChain.apply(p, now);

    // advance the retarded-time queue on the listener clock and sound arrivals
    this.drainQueue(f.t);
    this.lastStatus = this.buildStatus(p);
  }

  /** Per-render-frame pump (keeps the crackle shocklet stream regenerating). Call
   *  from the render loop; cheap and safe when muted (it still schedules but the
   *  master gate is 0). */
  tick(): void {
    if (!this.ctx) return;
    this.crackle.pump(this.ctx.currentTime);
  }

  // ---- EVT ingest (discrete, retarded-time) -------------------------------

  /** Feed a raw EVT ArrayBuffer (client.onEvtBytes). Decodes + enqueues sound. */
  onEvtBytes(buf: ArrayBuffer): void {
    let evt: EvtFrame;
    try {
      evt = decodeEvt(buf);
    } catch {
      return; // not an EVT / bad frame — ignore (forward-compat)
    }
    this.onEvt(evt);
  }

  /** Handle a decoded EVT: map to a discrete sound and buffer it at retarded time. */
  onEvt(evt: EvtFrame): void {
    const f = this.lastFrame;
    // emission position: usually the vehicle's current position; MACH1_CROSS carries
    // its own emit position in args (SIM frame → convert).
    let emitPos: Vec3;
    if (evt.code === EvtCode.Mach1Cross) {
      const [x, y, z] = simToThreePosArray(evt.args[0], evt.args[1], evt.args[2]);
      emitPos = { x, y, z };
    } else if (f) {
      emitPos = this.sourcePos(f);
    } else {
      emitPos = { x: 0, y: 0, z: 0 };
    }

    const kind = this.evtToKind(evt);
    if (!kind) return;
    const extra =
      evt.code === EvtCode.Touchdown ? Math.abs(evt.args[0]) / 5 : 0; // v_impact→severity

    this.queue.enqueue(
      { emitTime: evt.t, emitPos, payload: { kind, extra } },
      this.listener
    );
  }

  private evtToKind(evt: EvtFrame): OneshotKind | null {
    switch (evt.code) {
      case EvtCode.IgnitionCmd:
      case EvtCode.EngineStart:
        return "ignition";
      case EvtCode.EngineShutdown:
        return "shutdown";
      case EvtCode.Touchdown:
        return "touchdown";
      case EvtCode.Mach1Cross:
        return "boom";
      default:
        return null; // phase/verdict/gust/etc. don't sound in the sketch
    }
  }

  /** Release any events whose sound wall has reached the listener and fire them. */
  private drainQueue(simNow: number): void {
    if (!this.ctx) return;
    // listener clock in SIM time; convert each arrival to ctx time for scheduling.
    const released = this.queue.advance(simNow);
    for (const r of released) {
      const ctxAt = Math.max(this.ctx.currentTime + 0.005, r.arrivalTime + this.simToCtxOffset);
      // absorption knee at the event's range:
      const cutoff = absorptionCutoffHz(r.range);
      this.oneshots.fire(r.payload.kind, ctxAt, r.gain, cutoff, r.payload.extra);
    }
  }

  // ---- status / metering --------------------------------------------------

  private buildStatus(p: PropagationState): AudioStatus {
    return {
      running: this.isRunning,
      muted: this.muted,
      contextState: this.ctx?.state ?? "none",
      range: p.range,
      delaySec: p.delay,
      cutoffHz: p.cutoffHz,
      crackleAudibility: p.crackle,
      dopplerRatio: p.dopplerRatio,
      inFlightEvents: this.queue.inFlight,
      timeToNextEventSec: this.queue.timeToNext(),
      layers: this.layers.map((l) => ({ name: l.name, level: l.level(), muted: l.muted })),
    };
  }

  private idleStatus(): AudioStatus {
    return {
      running: false,
      muted: this.muted,
      contextState: "none",
      range: slantRange(this.listener, { x: 0, y: 0, z: 0 }),
      delaySec: propagationDelay(slantRange(this.listener, { x: 0, y: 0, z: 0 })),
      cutoffHz: absorptionCutoffHz(slantRange(this.listener, { x: 0, y: 0, z: 0 })),
      crackleAudibility: crackleAudibility(slantRange(this.listener, { x: 0, y: 0, z: 0 })),
      dopplerRatio: 1,
      inFlightEvents: 0,
      timeToNextEventSec: Infinity,
      layers: [],
    };
  }

  /** Snapshot for the dev panel (updates the live meters each call). */
  status(): AudioStatus {
    if (!this.ctx || !this.lastFrame) return { ...this.idleStatus(), running: this.isRunning };
    // refresh meters live
    this.lastStatus.layers = this.layers.map((l) => ({
      name: l.name,
      level: l.level(),
      muted: l.muted,
    }));
    this.lastStatus.running = this.isRunning;
    this.lastStatus.muted = this.muted;
    this.lastStatus.contextState = this.ctx.state;
    this.lastStatus.inFlightEvents = this.queue.inFlight;
    this.lastStatus.timeToNextEventSec = this.queue.timeToNext();
    return this.lastStatus;
  }

  /** Tear down (kill sound, free the context). */
  async dispose(): Promise<void> {
    try {
      this.engineChain?.disconnect();
      this.crackleChain?.disconnect();
    } catch {
      /* ignore */
    }
    if (this.ctx) {
      await this.ctx.close();
      this.ctx = null;
    }
  }
}
