// propagation.ts — the acoustic PROPAGATION MODEL (pure math, no Web Audio).
//
// This is the heart of the S3 sketch and the part canon §12 / D-011 cares about:
// sound from a rocket 62 km away does not arrive instantly, does not arrive at full
// bandwidth, and does not arrive at its emitted pitch. Everything here is a pure,
// deterministic function of geometry + telemetry so it can be unit-tested against
// hand-computed values (propagation.test.ts) with NO browser and NO AudioContext.
//
// Canon refs: §A.4 (EVT propagation-delayed), §B.8 (audio doctrine — d/343 s delay,
// 1/r gain, distance-lowpass air absorption, Doppler via variable delay), the D-011
// "spectral reveal": at 20 km slant the rocket is pure infrasonic rumble; crackle
// fades IN as range closes.
//
// Coordinate note: all positions here are in the THREE render/world frame (Y-up),
// already converted from sim by frame.ts upstream. The propagator only needs a
// listener position and a source position in one consistent frame; it is
// frame-agnostic (it only ever takes differences and magnitudes).

/** Speed of sound near the surface [m/s]. Canon pins 343 (2.92 s/km). */
export const SPEED_OF_SOUND = 343;

/** 1 km of slant range = this many seconds of delay. (1000/343 ≈ 2.915) */
export const SECONDS_PER_KM = 1000 / SPEED_OF_SOUND;

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

/** Euclidean distance between two points [m]. */
export function slantRange(a: Vec3, b: Vec3): number {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  const dz = a.z - b.z;
  return Math.hypot(dx, dy, dz);
}

/** Retarded-time delay [s] for a source at `dist` metres from the listener. */
export function propagationDelay(dist: number): number {
  return dist / SPEED_OF_SOUND;
}

// ---------------------------------------------------------------------------
// 1/r SPREADING GAIN
// ---------------------------------------------------------------------------
//
// Geometric spreading of a point source falls as 1/r. We reference it to a
// nominal near distance so the gain is 1.0 at the reference and never blows up
// as r→0 (the vehicle can pass very close to an onboard listener). The reference
// distance is the scale at which "full loudness" is defined for the mix.

export const REF_DISTANCE = 100; // m — gain = 1.0 here

/**
 * Geometric 1/r spreading gain, clamped to [0,1], referenced to REF_DISTANCE.
 * Monotonically NON-INCREASING in distance (a smoke-test invariant).
 */
export function spreadingGain(dist: number, refDistance = REF_DISTANCE): number {
  if (dist <= refDistance) return 1;
  return refDistance / dist;
}

// ---------------------------------------------------------------------------
// FREQUENCY-DEPENDENT ATMOSPHERIC ABSORPTION  (the "spectral reveal")
// ---------------------------------------------------------------------------
//
// Real atmospheric absorption (ISO 9613-1 / the project's own US76 model) rises
// steeply with frequency AND with distance: high frequencies are scrubbed out
// over kilometres, low-frequency rumble survives. We approximate the *audible
// consequence* with a single one-pole lowpass whose cutoff (the "knee") collapses
// toward infrasound as slant range grows. This is a SKETCH knee, not a per-band
// α(f,T,RH) integral — but it is monotone and hits the canon's two anchor points:
//
//   • ~20 km slant  → knee in deep infrasound (~30 Hz): pure rumble, crackle gone.
//   • close (≤ ~200 m) → knee wide open (~18 kHz): full crackle/tearing present.
//
// Model: log-linear interpolation of the cutoff between a near and far anchor,
// clamped. High cutoff = bright/close; low cutoff = muffled/far. The crackle
// AUDIBILITY then follows from where this knee sits relative to crackle's band.

/** Absorption knee anchor points (canon-pinned reveal). */
export const ABSORPTION = {
  nearRange: 150, // m   — at/below this, spectrum is essentially open
  farRange: 20000, // m   — the canon "20 km = infrasonic rumble" anchor
  nearCutoffHz: 18000, // full bandwidth up close
  farCutoffHz: 30, // pure infrasound at 20 km
} as const;

/**
 * Lowpass cutoff [Hz] modelling frequency-dependent air absorption vs slant range.
 * Monotonically NON-INCREASING in distance. Log-log interpolation between the two
 * canon anchor points, clamped outside [nearRange, farRange].
 */
export function absorptionCutoffHz(
  dist: number,
  cfg: typeof ABSORPTION = ABSORPTION
): number {
  const { nearRange, farRange, nearCutoffHz, farCutoffHz } = cfg;
  if (dist <= nearRange) return nearCutoffHz;
  if (dist >= farRange) return farCutoffHz;
  // interpolate in log(range) → log(cutoff) so the fade is perceptually even
  const lr = Math.log(dist);
  const lr0 = Math.log(nearRange);
  const lr1 = Math.log(farRange);
  const t = (lr - lr0) / (lr1 - lr0); // 0..1
  const lc0 = Math.log(nearCutoffHz);
  const lc1 = Math.log(farCutoffHz);
  return Math.exp(lc0 + t * (lc1 - lc0));
}

/**
 * Crackle audibility [0..1]: how much of the steep-shocklet band survives to the
 * listener. Crackle energy lives roughly 800 Hz–8 kHz; when the absorption knee
 * drops below that band the crackle "fades in as range closes" (D-011). This is a
 * derived convenience for the crackle layer's send gain — 0 far away, 1 up close.
 */
export function crackleAudibility(
  dist: number,
  bandLoHz = 700,
  bandHiHz = 6000,
  cfg: typeof ABSORPTION = ABSORPTION
): number {
  const knee = absorptionCutoffHz(dist, cfg);
  if (knee <= bandLoHz) return 0;
  if (knee >= bandHiHz) return 1;
  // fraction of the (log) crackle band below the knee
  const t = (Math.log(knee) - Math.log(bandLoHz)) / (Math.log(bandHiHz) - Math.log(bandLoHz));
  return Math.min(1, Math.max(0, t));
}

// ---------------------------------------------------------------------------
// DOPPLER  (simple, from radial velocity)
// ---------------------------------------------------------------------------
//
// Classic stationary-listener Doppler: f' = f * c / (c + v_radial), where
// v_radial > 0 means the source is receding (pitch drops). Supersonic closing
// (v_radial <= -c) is unphysical for a single classical tone (the denominator
// hits zero / flips); we CLAMP GRACEFULLY to a max pitch multiplier rather than
// producing NaN/negative rates (canon: "clamp supersonic gracefully").

/** Max Doppler pitch multiplier when clamping near/through Mach 1 closing. */
export const DOPPLER_MAX_RATIO = 3.0;
export const DOPPLER_MIN_RATIO = 0.25;

/**
 * Radial velocity [m/s] of the source relative to the listener: the component of
 * the source velocity along the listener→source line. POSITIVE = receding.
 * (Listener assumed stationary for the sketch; the camera moves slowly vs sound.)
 */
export function radialVelocity(
  listener: Vec3,
  source: Vec3,
  sourceVel: Vec3
): number {
  const dx = source.x - listener.x;
  const dy = source.y - listener.y;
  const dz = source.z - listener.z;
  const r = Math.hypot(dx, dy, dz);
  if (r < 1e-6) return 0;
  // component of velocity along the outward radial unit vector
  return (sourceVel.x * dx + sourceVel.y * dy + sourceVel.z * dz) / r;
}

/**
 * Doppler pitch ratio f'/f from a radial velocity, clamped to a finite, positive
 * band so supersonic closing never yields a zero/negative playback rate.
 * Always in [DOPPLER_MIN_RATIO, DOPPLER_MAX_RATIO] and finite.
 */
export function dopplerRatio(vRadial: number, c = SPEED_OF_SOUND): number {
  const denom = c + vRadial;
  // Guard the singularity: closing faster than sound → clamp to max ratio.
  if (denom <= c / DOPPLER_MAX_RATIO) return DOPPLER_MAX_RATIO;
  const ratio = c / denom;
  return Math.min(DOPPLER_MAX_RATIO, Math.max(DOPPLER_MIN_RATIO, ratio));
}

// ---------------------------------------------------------------------------
// TURBULENCE AMPLITUDE FLUTTER  (slow scintillation on distant sources)
// ---------------------------------------------------------------------------
//
// Sound travelling many kilometres through a turbulent boundary layer scintillates
// — a slow, shallow amplitude wobble that is a strong "this is far away, outdoors"
// cue. It is visual-only garnish ON TOP of honest gain (canon §A.0 permits garnish
// atop honest state), so it is bounded and *increases with range* (near sources are
// steady). Deterministic given (t, range, seed) so tests can pin it.

export interface TurbulenceCfg {
  maxDepth: number; // peak fractional amplitude wobble at long range (e.g. 0.25)
  onsetRange: number; // m — below this, essentially steady
  fullRange: number; // m — at/above this, full depth
  rateHz: number; // base flutter rate
}

export const TURBULENCE: TurbulenceCfg = {
  maxDepth: 0.28,
  onsetRange: 500,
  fullRange: 8000,
  rateHz: 0.7,
};

/** Depth [0..maxDepth] of the flutter at a given range (monotone non-decreasing). */
export function turbulenceDepth(dist: number, cfg: TurbulenceCfg = TURBULENCE): number {
  if (dist <= cfg.onsetRange) return 0;
  if (dist >= cfg.fullRange) return cfg.maxDepth;
  const t = (dist - cfg.onsetRange) / (cfg.fullRange - cfg.onsetRange);
  return t * cfg.maxDepth;
}

/**
 * Turbulence amplitude multiplier at time `t` for a source at `dist`, using two
 * incommensurate sines (so it doesn't read as a pure LFO) modulated by depth.
 * Returns a multiplier centred on 1.0, in [1-depth, 1+depth]. Deterministic.
 */
export function turbulenceFlutter(
  t: number,
  dist: number,
  cfg: TurbulenceCfg = TURBULENCE
): number {
  const depth = turbulenceDepth(dist, cfg);
  if (depth <= 0) return 1;
  const a = Math.sin(2 * Math.PI * cfg.rateHz * t);
  const b = Math.sin(2 * Math.PI * cfg.rateHz * 1.618 * t + 1.3);
  const mix = 0.6 * a + 0.4 * b; // in [-1,1]
  return 1 + depth * mix;
}

// ---------------------------------------------------------------------------
// THE RETARDED-TIME EVENT QUEUE
// ---------------------------------------------------------------------------
//
// Discrete acoustic events (ignition thump, shutdown pop, touchdown clang, sonic
// boom) are EMITTED at a sim-time and a world position, but must be HEARD later —
// after the sound wall reaches the listener. This queue buffers scheduled events
// and releases them when the listener's clock passes (emitTime + delay). "The
// silent touchdown, then the sound wall." (canon §B.8 / §A.4)
//
// It is a pure data structure: you enqueue with a snapshot of the listener at
// emit time, and drain by advancing a monotonic listener clock. The AudioContext
// layer converts a released event into a scheduled one-shot; the queue itself has
// zero Web Audio dependency, so its timing is unit-tested directly.

export interface AcousticEvent<T = unknown> {
  /** Sim time the event was emitted [s]. */
  emitTime: number;
  /** World position the event was emitted from. */
  emitPos: Vec3;
  /** Opaque payload (event kind + args) handed back on release. */
  payload: T;
}

/** An event that has propagated and is ready to sound. */
export interface ReleasedEvent<T = unknown> {
  payload: T;
  /** Listener-clock time it arrives [s] = emitTime + delay. */
  arrivalTime: number;
  /** Slant range at emission [m]. */
  range: number;
  /** Propagation delay it experienced [s]. */
  delay: number;
  /** 1/r spreading gain at the arrival geometry. */
  gain: number;
}

/**
 * Buffers acoustic events and releases them at their retarded-time arrival.
 * Listener position is sampled per-enqueue (the geometry that matters is at emit
 * time for the delay/gain of a discrete crack). Pure — safe to unit-test.
 */
export class RetardedEventQueue<T = unknown> {
  private pending: ReleasedEvent<T>[] = [];
  private listenerClock = 0;

  /** Enqueue an event given the CURRENT listener position (at emit time). */
  enqueue(ev: AcousticEvent<T>, listener: Vec3): ReleasedEvent<T> {
    const range = slantRange(ev.emitPos, listener);
    const delay = propagationDelay(range);
    const rel: ReleasedEvent<T> = {
      payload: ev.payload,
      arrivalTime: ev.emitTime + delay,
      range,
      delay,
      gain: spreadingGain(range),
    };
    // insert keeping arrival order (small N — linear insert is fine)
    let i = this.pending.length;
    while (i > 0 && this.pending[i - 1].arrivalTime > rel.arrivalTime) i--;
    this.pending.splice(i, 0, rel);
    return rel;
  }

  /**
   * Advance the listener clock to `t` and return every event whose arrival time
   * has now passed (in arrival order). Idempotent forward progress: each event is
   * released exactly once.
   */
  advance(t: number): ReleasedEvent<T>[] {
    this.listenerClock = t;
    const out: ReleasedEvent<T>[] = [];
    while (this.pending.length > 0 && this.pending[0].arrivalTime <= t) {
      out.push(this.pending.shift() as ReleasedEvent<T>);
    }
    return out;
  }

  /** Events still in flight (buffered, not yet arrived). */
  get inFlight(): number {
    return this.pending.length;
  }

  /** Current listener clock [s]. */
  get clock(): number {
    return this.listenerClock;
  }

  /** Seconds until the next event arrives, or Infinity if none pending. */
  timeToNext(): number {
    if (this.pending.length === 0) return Infinity;
    return this.pending[0].arrivalTime - this.listenerClock;
  }

  clear(): void {
    this.pending.length = 0;
  }
}

// ---------------------------------------------------------------------------
// CONTINUOUS-SOURCE PROPAGATION SNAPSHOT
// ---------------------------------------------------------------------------
//
// The engine bed is a CONTINUOUS source: at every render tick we recompute its
// delay/gain/cutoff/doppler from the current geometry. `computePropagation` bundles
// the whole model into one struct so the audio engine (and the node smoke test)
// can drive parameter smoothing from a single call.

export interface PropagationState {
  range: number; // slant range [m]
  delay: number; // retarded-time delay [s] (informational for the readout)
  gain: number; // 1/r spreading gain [0..1]
  cutoffHz: number; // absorption lowpass knee [Hz]
  crackle: number; // crackle audibility [0..1]
  dopplerRatio: number; // pitch multiplier f'/f (finite, clamped)
  turbulence: number; // amplitude multiplier around 1.0
}

/**
 * Full continuous-source propagation snapshot for one source vs the listener.
 * All outputs finite. `simTime` drives the turbulence flutter phase.
 */
export function computePropagation(
  listener: Vec3,
  source: Vec3,
  sourceVel: Vec3,
  simTime: number
): PropagationState {
  const range = slantRange(source, listener);
  const vr = radialVelocity(listener, source, sourceVel);
  return {
    range,
    delay: propagationDelay(range),
    gain: spreadingGain(range),
    cutoffHz: absorptionCutoffHz(range),
    crackle: crackleAudibility(range),
    dopplerRatio: dopplerRatio(vr),
    turbulence: turbulenceFlutter(simTime, range),
  };
}
