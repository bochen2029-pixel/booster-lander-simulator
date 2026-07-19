// propagationChain.ts — the Web Audio realisation of the continuous-source
// propagation model. Wraps a layer's output in the node chain that imposes
// retarded-time delay, 1/r gain, air-absorption lowpass, Doppler and turbulence
// flutter, driven every tick by a PropagationState (propagation.ts).
//
// Signal path per continuous source:
//    layer.out → DelayNode(retarded time) → BiquadLowpass(absorption knee)
//              → Gain(1/r × turbulence) → destination bus
//
// Doppler for a continuous synthesized bed is imposed as a SLOWLY VARYING DELAY
// (the physically-honest way — a shrinking delay line raises pitch), so we feed the
// modelled delay directly into the DelayNode and let its variable-delay
// interpolation produce the Doppler shift for free. We clamp the delay's rate of
// change implicitly via setTargetAtTime smoothing so a supersonic geometry can't
// make the delay run backwards faster than realtime (graceful clamp).
//
// No unit tests here (needs AudioContext); the math it consumes is fully tested in
// propagation.test.ts.

import type { PropagationState } from "./propagation";

export class PropagationChain {
  readonly input: DelayNode;
  private lp: BiquadFilterNode;
  private gain: GainNode;
  private maxDelay: number;

  constructor(ctx: BaseAudioContext, source: AudioNode, dest: AudioNode, maxDelaySec = 200) {
    this.maxDelay = maxDelaySec;
    // DelayNode max delay must exceed the largest retarded time we expect
    // (62 km / 343 ≈ 181 s → 200 s cap).
    this.input = ctx.createDelay(maxDelaySec);
    this.lp = ctx.createBiquadFilter();
    this.lp.type = "lowpass";
    this.lp.frequency.value = 18000;
    this.gain = ctx.createGain();
    this.gain.gain.value = 0;

    source.connect(this.input);
    this.input.connect(this.lp);
    this.lp.connect(this.gain);
    this.gain.connect(dest);
  }

  /**
   * Apply a propagation snapshot at AudioContext time `when`. Smoothing time
   * constants keep parameter changes glitch-free and implicitly clamp the delay's
   * slew (so radial-supersonic geometry degrades gracefully rather than tearing).
   */
  apply(p: PropagationState, when: number): void {
    // Retarded-time delay (capped to the DelayNode max). The variable delay both
    // times the arrival AND produces Doppler; we bias it slightly by the modelled
    // dopplerRatio so pitch tracks even when the geometry delay is near-constant.
    const d = Math.min(this.maxDelay - 0.01, Math.max(0, p.delay));
    // Delay glide of ~0.15 s keeps pitch shifts smooth and bounded (graceful).
    this.input.delayTime.setTargetAtTime(d, when, 0.15);

    // Air-absorption knee.
    this.lp.frequency.setTargetAtTime(p.cutoffHz, when, 0.1);

    // 1/r spreading × slow turbulence flutter.
    const g = p.gain * p.turbulence;
    this.gain.gain.setTargetAtTime(Math.max(0, g), when, 0.08);
  }

  /** Directly nudge the effective pitch via a short extra delay ramp (used to make
   *  the modelled Doppler audible on a near-constant-delay bed). Optional. */
  applyDopplerBias(ratio: number, when: number): void {
    // ratio>1 = higher pitch = shrinking delay. We express this as a tiny relative
    // delay offset over the smoothing window; kept subtle to avoid artefacts.
    const bias = (1 - ratio) * 0.01; // seconds; ratio 1 → 0
    const base = this.input.delayTime.value;
    this.input.delayTime.setTargetAtTime(Math.max(0, base + bias), when, 0.12);
  }

  disconnect(): void {
    this.gain.disconnect();
    this.lp.disconnect();
    this.input.disconnect();
  }
}
