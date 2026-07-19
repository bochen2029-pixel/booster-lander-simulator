// crackle.ts — the CRACKLE signature: Poisson-distributed steep asymmetric shocklets.
//
// Canon §B.8 pins the MEASURED character of rocket crackle precisely, and D-011
// makes it a first-class acoustic fingerprint:
//
//   "crackle (Poisson steep-asymmetric shocklets, rate ∝ throttle, POSITIVE
//    SKEWNESS 0.1–0.5 — the measured signature). Distance softens crackle
//    tearing → popcorn."
//
// So crackle is NOT noise and NOT a loop (canon §12 / D-011 — causally derived,
// never looped). It is a Poisson point process of individual pressure SHOCKLETS,
// each a fast asymmetric transient (steep compressive rise, slower rarefaction
// tail) whose population has positive skewness. This module is the PURE generator:
//
//   • shocklet inter-arrival times   (exponential — Poisson process)
//   • the per-shocklet asymmetric waveform sample function
//   • the population statistics (skewness) so a unit test can assert the signature
//
// It has zero Web Audio dependency; the audio layer renders these shocklets into a
// buffer / schedules them. A seeded RNG makes every statistic reproducible.

/** Small, fast, seedable PRNG (mulberry32) — deterministic tests. */
export function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return function () {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** Standard exponential sample (mean 1) from a uniform in (0,1]. */
export function expSample(u: number): number {
  // guard u==0 → -log(0)=+Inf; u in (0,1]
  const uu = u <= 0 ? Number.MIN_VALUE : u;
  return -Math.log(uu);
}

// ---------------------------------------------------------------------------
// CRACKLE RATE  (shocklet density ∝ thrust)
// ---------------------------------------------------------------------------
//
// Density of shocklets scales with jet thrust ≈ throttle × n_eng. At full thrust
// the tearing is a dense "ripping" texture; at idle it is sparse "popcorn". We map
// (throttle, n_eng) → a Poisson rate λ [shocklets/sec].

export const CRACKLE = {
  ratePerEngineFull: 900, // shocklets/s per engine at full throttle
  minThrottle: 0.4, // engines don't run below this (canon throttle floor)
} as const;

/** Poisson rate λ [shocklets/s] for the crackle process given thrust state. */
export function crackleRate(
  throttle: number,
  nEng: number,
  cfg: typeof CRACKLE = CRACKLE
): number {
  if (nEng <= 0 || throttle < cfg.minThrottle) return 0;
  // rate rises a bit faster than linearly in throttle (jet noise ~ v^8-ish, but
  // we keep the sketch modest: throttle^1.5)
  const thr = Math.pow(throttle, 1.5);
  return cfg.ratePerEngineFull * nEng * thr;
}

/**
 * Generate the inter-arrival times [s] of shocklets over a window `dt` given a
 * Poisson rate λ and a seeded uniform generator. Returns the arrival OFFSETS
 * within [0, dt). Exponential gaps = a true Poisson process (canon: "Poisson").
 */
export function shockletArrivals(rate: number, dt: number, rng: () => number): number[] {
  const out: number[] = [];
  if (rate <= 0) return out;
  let t = 0;
  const mean = 1 / rate;
  // cap to avoid pathological loops if rate*dt is enormous
  const cap = Math.ceil(rate * dt * 4) + 16;
  for (let i = 0; i < cap; i++) {
    t += expSample(rng()) * mean;
    if (t >= dt) break;
    out.push(t);
  }
  return out;
}

// ---------------------------------------------------------------------------
// THE ASYMMETRIC SHOCKLET WAVEFORM  (the positive-skewness engine)
// ---------------------------------------------------------------------------
//
// A single shocklet is an N-wave-like transient with a STEEP compressive spike and
// a longer, shallower rarefaction tail. Sampling the pressure of such a transient
// densely yields a distribution with POSITIVE SKEWNESS (a few large positive
// compressive samples, many small negative rarefaction samples) — exactly the
// 0.1–0.5 signature the canon cites for the *ensemble* waveform.
//
// Model of one shocklet, peak amplitude `amp`, characteristic width `w` [s]:
//   rise  (τ<0, |τ|<w):  fast, cubic-eased ramp up to +amp at τ=0   (the shock)
//   fall  (τ>0):         +amp * exp(-τ/(w*tailRatio))  minus a small
//                         rarefaction undershoot so the mean is ~0
// The steep rise + slow decaying tail is the asymmetry.

export const SHOCKLET = {
  width: 0.0012, // s — compressive front timescale (~sub-ms, kHz content)
  tailRatio: 3.5, // rarefaction tail is this many × the front width
  undershoot: 0.35, // depth of the rarefaction dip (fraction of amp)
} as const;

/**
 * Pressure of one unit-amplitude shocklet at time offset τ [s] from its shock
 * front (τ<0 before the shock, τ>0 in the tail). Peak = +1 at τ=0.
 */
export function shockletSample(tau: number, cfg: typeof SHOCKLET = SHOCKLET): number {
  const w = cfg.width;
  if (tau < -w) return 0;
  if (tau < 0) {
    // steep cubic rise from 0 → 1 over [-w, 0]
    const x = (tau + w) / w; // 0..1
    return x * x * x;
  }
  // tail: fast-decaying compressive lobe minus a shallow rarefaction dip
  const tt = cfg.width * cfg.tailRatio;
  const comp = Math.exp(-tau / tt);
  // a slower, negative rarefaction lobe that pulls the tail below zero briefly
  const rare = cfg.undershoot * Math.exp(-tau / (tt * 2.2)) * (tau / (tt * 0.9));
  return comp - rare;
}

/**
 * Peak amplitude of an individual shocklet, drawn from a heavy-ish positive
 * distribution so a few shocklets are much louder than the rest (contributes to
 * the crackle's spiky, positively-skewed loudness envelope). Uses an exponential
 * magnitude (mean 1) lightly clamped.
 */
export function shockletAmplitude(rng: () => number): number {
  const a = expSample(rng()); // mean 1, positively skewed
  return Math.min(a, 6);
}

// ---------------------------------------------------------------------------
// SKEWNESS  (the falsifiable signature)
// ---------------------------------------------------------------------------

/** Sample skewness (Fisher-Pearson) of an array. */
export function skewness(xs: ArrayLike<number>): number {
  const n = xs.length;
  if (n < 3) return 0;
  let mean = 0;
  for (let i = 0; i < n; i++) mean += xs[i];
  mean /= n;
  let m2 = 0;
  let m3 = 0;
  for (let i = 0; i < n; i++) {
    const d = xs[i] - mean;
    m2 += d * d;
    m3 += d * d * d;
  }
  m2 /= n;
  m3 /= n;
  const sd = Math.sqrt(m2);
  if (sd < 1e-12) return 0;
  return m3 / (sd * sd * sd);
}

/**
 * Render a densely-sampled crackle pressure TRACE by superposing Poisson shocklets
 * over a window. Used both by the audio buffer builder and by the skewness test
 * (which asserts the trace has the canon-pinned positive skewness). Pure.
 *
 * @param durationSec length of the trace [s]
 * @param sampleRate  samples/s (e.g. 48000)
 * @param rate        Poisson shocklet rate λ [shocklets/s]
 * @param rng         seeded uniform generator
 */
export function renderCrackleTrace(
  durationSec: number,
  sampleRate: number,
  rate: number,
  rng: () => number
): Float32Array {
  const n = Math.max(1, Math.floor(durationSec * sampleRate));
  const buf = new Float32Array(n);
  if (rate <= 0) return buf;

  const arrivals = shockletArrivals(rate, durationSec, rng);
  const w = SHOCKLET.width;
  const tailSpan = SHOCKLET.width * SHOCKLET.tailRatio * 6; // where the tail is ~0
  for (const ta of arrivals) {
    const amp = shockletAmplitude(rng);
    const i0 = Math.max(0, Math.floor((ta - w) * sampleRate));
    const i1 = Math.min(n, Math.ceil((ta + tailSpan) * sampleRate));
    for (let i = i0; i < i1; i++) {
      const tau = i / sampleRate - ta;
      buf[i] += amp * shockletSample(tau);
    }
  }
  // normalise to avoid clipping while preserving shape/skew
  let peak = 0;
  for (let i = 0; i < n; i++) {
    const a = Math.abs(buf[i]);
    if (a > peak) peak = a;
  }
  if (peak > 1) {
    const inv = 0.98 / peak;
    for (let i = 0; i < n; i++) buf[i] *= inv;
  }
  return buf;
}
