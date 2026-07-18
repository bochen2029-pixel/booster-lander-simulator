// interp.ts — telemetry interpolation buffer (canon §11.2, §11.13 raw toggle).
//
// Contract:
//  - Ring-buffer the last N>=2 frames for interpolation, AND keep the full-run
//    history (125 Hz x 600 s ~= 17 MB) — the history IS the replay system.
//  - Render ~one packet interval in the PAST: at wall time `now`, pick the pair
//    (a,b) whose sim-times straddle `renderTime = now - delayScale*packetDt` and
//    lerp position / slerp attitude / HOLD actuators (they are commands, not
//    continuous signals — interpolating a PWM mask is meaningless).
//  - This absorbs 125Hz->displayHz beating without touching the sim (directive 8).
//  - "Raw" toggle disables interpolation: return the newest frame verbatim, so the
//    HUD can show true oscillation (directive 8 / canon §11.2). No smoothing that
//    hides real motion — ever.
//
// Only STATE is interpolated. Everything discrete (phase, verdict, masks, tail
// arrays) is taken from the *newer* frame of the straddling pair.

import { Vector3, Quaternion } from "three";
import type { TlmFrame } from "./decode";

const PACKET_HZ = 125;
export const PACKET_DT = 1 / PACKET_HZ; // 8 ms

/** A render-ready, frame-converted-later interpolated sample (still SIM frame). */
export interface InterpSample {
  r: Vector3; // sim frame
  v: Vector3; // sim frame
  q: Quaternion; // sim frame xyzw
  w: Vector3;
  // discrete / held straight from the newer frame:
  frame: TlmFrame; // the newer frame — source of phase, actuators, tails, etc.
  alpha: number; // interpolation fraction used (0..1), for debugging
}

export class InterpBuffer {
  /** Full-run history (the replay ring). Grows to cap then reuses. */
  private history: TlmFrame[] = [];
  private cap: number;
  private head = 0; // next write index into history when full
  private full = false;

  /** Render this many packet-intervals in the past (>=1 so we always have a pair). */
  delayPackets = 1.0;
  /** When true, skip interpolation and emit the newest frame raw (directive 8). */
  raw = false;

  // scratch (no per-frame allocation)
  private readonly _ra = new Vector3();
  private readonly _rb = new Vector3();
  private readonly _qa = new Quaternion();
  private readonly _qb = new Quaternion();
  private lastSeq = -1;
  /** Count of detected sequence gaps (dropped frames) — surfaced to the HUD. */
  droppedFrames = 0;

  constructor(runSeconds = 600) {
    this.cap = Math.ceil(runSeconds * PACKET_HZ) + 8;
  }

  /** Ingest a decoded frame. Detects seq gaps (canon §10.1 backpressure telltale). */
  push(f: TlmFrame): void {
    if (this.lastSeq >= 0) {
      const gap = f.seq - this.lastSeq - 1;
      if (gap > 0) this.droppedFrames += gap;
    }
    this.lastSeq = f.seq;

    if (!this.full) {
      this.history.push(f);
      if (this.history.length >= this.cap) this.full = true;
    } else {
      this.history[this.head] = f;
      this.head = (this.head + 1) % this.cap;
    }
  }

  get length(): number {
    return this.history.length;
  }

  /** Newest ingested frame, or null if empty. */
  latest(): TlmFrame | null {
    if (this.history.length === 0) return null;
    if (!this.full) return this.history[this.history.length - 1];
    return this.history[(this.head - 1 + this.cap) % this.cap];
  }

  /**
   * Sample at sim-time `simNow` (typically the sim-time the newest packet reports,
   * i.e. we render relative to the stream clock, not wall clock — see loop.ts).
   * Returns null until at least one frame exists.
   */
  sample(simNow: number, out?: InterpSample): InterpSample | null {
    const newest = this.latest();
    if (!newest) return null;

    const dst = out ?? this.makeSample(newest);

    if (this.raw || this.history.length < 2) {
      dst.r.set(newest.r[0], newest.r[1], newest.r[2]);
      dst.v.set(newest.v[0], newest.v[1], newest.v[2]);
      dst.q.set(newest.quat[0], newest.quat[1], newest.quat[2], newest.quat[3]);
      dst.w.set(newest.w[0], newest.w[1], newest.w[2]);
      dst.frame = newest;
      dst.alpha = 0;
      return dst;
    }

    const renderTime = simNow - this.delayPackets * PACKET_DT;

    // Find the straddling pair (a older, b newer) around renderTime by walking
    // backward from newest. History is time-ordered per stream (seq monotonic).
    const [a, b, alpha] = this.straddle(renderTime);

    this._ra.set(a.r[0], a.r[1], a.r[2]);
    this._rb.set(b.r[0], b.r[1], b.r[2]);
    dst.r.copy(this._ra).lerp(this._rb, alpha);

    // velocity: lerp is fine (it is a state, shown on a tape); actuators HELD.
    this._ra.set(a.v[0], a.v[1], a.v[2]);
    this._rb.set(b.v[0], b.v[1], b.v[2]);
    dst.v.copy(this._ra).lerp(this._rb, alpha);

    this._qa.set(a.quat[0], a.quat[1], a.quat[2], a.quat[3]);
    this._qb.set(b.quat[0], b.quat[1], b.quat[2], b.quat[3]);
    dst.q.slerpQuaternions(this._qa, this._qb, alpha);

    this._ra.set(a.w[0], a.w[1], a.w[2]);
    this._rb.set(b.w[0], b.w[1], b.w[2]);
    dst.w.copy(this._ra).lerp(this._rb, alpha);

    // Discrete/held state comes from the NEWER frame of the pair.
    dst.frame = b;
    dst.alpha = alpha;
    return dst;
  }

  private makeSample(f: TlmFrame): InterpSample {
    return {
      r: new Vector3(),
      v: new Vector3(),
      q: new Quaternion(),
      w: new Vector3(),
      frame: f,
      alpha: 0,
    };
  }

  /** Return [older, newer, alpha in 0..1] straddling `tTarget`. Clamps at ends. */
  private straddle(tTarget: number): [TlmFrame, TlmFrame, number] {
    const n = this.history.length;
    const at = (i: number): TlmFrame => {
      if (!this.full) return this.history[i];
      return this.history[(this.head + i) % this.cap]; // oldest-first logical order
    };
    const newest = at(n - 1);
    const oldest = at(0);

    if (tTarget >= newest.t) return [newest, newest, 0];
    if (tTarget <= oldest.t) return [oldest, oldest, 0];

    // Linear back-scan (render target is always within ~1-2 frames of newest).
    for (let i = n - 1; i > 0; i--) {
      const b = at(i);
      const a = at(i - 1);
      if (a.t <= tTarget && tTarget <= b.t) {
        const span = b.t - a.t;
        const alpha = span > 1e-9 ? (tTarget - a.t) / span : 0;
        return [a, b, alpha];
      }
    }
    return [oldest, newest, 0]; // unreachable given the clamps above
  }
}
