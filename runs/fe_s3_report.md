# S3 — Tier-A Web Audio SKETCH (fe-audio) — build report

**Agent:** S3-AUDIO · **Lane:** fe-audio (intercom id `qbjxcys4`)
**Worktree:** `C:\Booster_Lander_Simulator\_fe_audio_wt\` (real tree NEVER touched — verified `git status ui/` clean)
**Date:** 2026-07-18 ~20:33 -05:00
**Scope:** S3 of the LZ-COCKPIT ladder — *a SKETCH that proves the propagation model*,
per `C:\Brainstorm\booster-3d-frontend-brainstorm.md` §2/§5 + `runs/brainstorm_canon.md`
§B.8 audio doctrine (canon §12, sharpened by D-011: **causally derived, never loops**).

---

## 1. What was built

A **third pure observer** (canon §B.8) that consumes the *same* one-way telemetry the
renderer does, decodes EVT itself, and turns state + events into procedurally-synthesized,
propagation-honest sound. It writes **nothing** back. Muted by default; the dev panel's
ENABLE button is the first-interaction gesture that resumes the AudioContext.

Self-contained under `ui/src/audio/` (new) + one **additive, muted-by-default** mount in
`ui/src/main.ts` (**+18 / −2 lines**). Did **not** touch `core/`, `shell/`, `scene/`,
`fx/`, `hud/` (sibling agents own those).

### Files (all new unless noted)

| file | lines | role |
|---|---:|---|
| `ui/src/audio/propagation.ts` | 333 | **PURE math** — retarded-time delay, 1/r gain, US76-approx absorption knee vs slant range, Doppler (graceful supersonic clamp), turbulence flutter, the retarded-time event QUEUE, `computePropagation()` snapshot. No Web Audio. |
| `ui/src/audio/crackle.ts` | 194 | **PURE** crackle generator — seeded RNG, Poisson shocklet inter-arrivals, asymmetric shocklet waveform, `skewness()`, `renderCrackleTrace()`. The measured positive-skewness signature. No Web Audio. |
| `ui/src/audio/evtDecode.ts` | 62 | Minimal 48-byte EVT decoder (mirrors `protocol.h BlEvt`) — the audio trigger bus. |
| `ui/src/audio/synthLayers.ts` | 346 | Web Audio synthesis: `EngineBed` (broadband bed ∝ throttle×n_eng, chamber-count character), `CrackleLayer` (Poisson shocklets rendered into per-seed regenerating chunks — never a loop), `OneshotLayer` (ignition thump, shutdown pop, touchdown clang, triple boom). |
| `ui/src/audio/propagationChain.ts` | 72 | Web Audio realisation of the continuous-source chain: `DelayNode`(retarded time+Doppler) → lowpass(absorption knee) → gain(1/r×turbulence). |
| `ui/src/audio/audioEngine.ts` | 334 | Orchestrator — owns AudioContext + master bus (muted, limiter), `setListener(pos)` API, `onTlm`/`onEvtBytes`, retarded-time queue drain, per-layer meters, status snapshot. |
| `ui/src/audio/devPanel.ts` | 121 | Dev overlay — per-layer meters + per-layer mute, master ENABLE/MUTE, the **arrival-delay readout ("you are 6.2 s away")** + range/knee/crackle/Doppler/in-flight-events. |
| `ui/src/audio/index.ts` | 62 | Public surface — `mountAudio()` (builds engine + panel, muted) + the wiring contract. |
| `ui/src/audio/propagation.test.ts` | 197 | 18 unit tests — retarded-time queue, absorption knee vs range, 1/r, Doppler clamp, turbulence. |
| `ui/src/audio/crackle.test.ts` | 165 | 14 unit tests — Poisson timing statistics + the **positive-skewness** fingerprint. |
| `ui/src/audio/evtDecode.test.ts` | 67 | 4 unit tests — golden bytes + offset sentinels + bad-magic. |
| `ui/smoke/audio_smoke.mts` | 206 | Node smoke — feeds live decoded frames from `--serve` through the propagation math. |
| `ui/src/main.ts` | (+18/−2) | **Additive** mount: `mountAudio()`; tee `onTlm`→audio; add `onEvtBytes`→audio; `audio.tick()`/`updatePanel()`/`setListener(camWorld)` in the loop. |

---

## 2. The propagation model (the point of S3)

All camera-relative, from streamed state. Positions are in the frozen THREE render frame
(converted once by `frame.ts`). Every function is a pure, deterministic function of geometry
+ telemetry so it is unit-testable with no browser.

- **Retarded time** — sound arrives `distance / 343` s late (2.92 s/km). Discrete events
  (ignition/shutdown/touchdown/boom) are **buffered in a `RetardedEventQueue` and released at
  their arrival time** → *the silent touchdown, then the sound wall*. Continuous bed recomputes
  its delay every tick and feeds it to a `DelayNode`.
- **1/r spreading** — `spreadingGain`, referenced to 100 m, clamped [0,1], monotone.
- **Frequency-dependent atmospheric absorption (US76-approx "spectral reveal")** — a single
  lowpass whose **knee collapses toward infrasound as slant range grows**, log-log interpolated
  between two canon anchors: **≤150 m → 18 kHz** (full bandwidth) and **20 km → 30 Hz** (pure
  infrasonic rumble). A derived `crackleAudibility(range)` makes crackle **fade IN as range
  closes** (0 far → 1 near) — the D-011 reveal, driving the crackle layer's send gain.
- **Doppler** — classic stationary-listener `f' = f·c/(c+v_radial)` from the radial velocity,
  **clamped gracefully** to a finite positive band `[0.25, 3.0]` so supersonic closing never
  yields a zero/negative/NaN rate. On the continuous bed it is imposed as slowly-varying delay
  (the physically-honest way) plus a subtle bias.
- **Turbulence flutter** — a slow, shallow amplitude wobble that **grows with range** (near
  sources steady), garnish atop honest gain (canon §A.0), deterministic in `(t, range)`.

## 3. Synthesis layers (procedural — canon §12)

- **Engine bed** — sub-rumble sine (fundamental drops with engine count) + bandpassed noise
  carrier (brightens with throttle) + peaking "chamber" formant (emphasis ∝ n_eng). Fully
  gated by throttle×n_eng: silence is silence.
- **Crackle** — Poisson steep-asymmetric shocklets, rate ∝ `throttle^1.5 × n_eng`, rendered
  into short buffers that are **regenerated with a fresh seed each chunk** → causally derived,
  **never a repeating loop**. Send gain = `crackleAudibility(range)`.
- **One-shots** — ignition overpressure thump (pitch-swept sine + noise crack), shutdown pop,
  touchdown clang (inharmonic partials + structural thud, decay ∝ crush severity), triple sonic
  boom (three N-wave snaps ~0.12/0.18 s apart). Each lowpassed to the absorption knee at the
  event's range and scaled by 1/r at emission — all baked at retarded-time release.

## 4. Listener = active camera

`AudioEngine.setListener(pos)` is the director hook (canon §B.8: observer = active camera).
Default = a fixed pad-cam listener `(1400, 40, 1400)` three-world. For the sketch the M3
follow-cam world position is fed in each frame; a real camera CUT will crossfade (per-tick
0.08–0.15 s smoothing glides today; explicit 250 ms crossfade is a TODO — see §7).

---

## 5. Gates — all green

| gate | result |
|---|---|
| `pnpm typecheck` | **green** (tsc --noEmit, strict + noUnusedLocals + exactOptionalPropertyTypes) |
| `pnpm test` (vitest) | **62/62 green** — 26 pre-existing untouched + **36 new** (18 propagation, 14 crackle, 4 EVT) |
| `pnpm build` (tsc + vite) | **green** — the additive `main.ts` mount bundles end-to-end (24 modules; the 790 kB chunk is three.js, pre-existing) |
| node smoke (500+ live frames) | **PASS** — see §6 |

**Unit tests for the pure math (as required):**
- retarded-time queue: releases only after `emit + dist/c`, exactly once, in **arrival order**
  even when enqueued out of order; `timeToNext`/`inFlight` correct; gain = 1/r at emit geometry.
- absorption knee vs range: hits the 20 km→infrasound anchor; **strictly monotone non-increasing**
  across the whole descent; crackle audibility fades in monotonically as range closes.
- Poisson shocklet statistics: mean count ≈ λ·dt; inter-arrival **gap CV ≈ 1** (Poisson);
  rate ∝ thrust and gates at the throttle floor; **rendered trace is positively skewed**
  (sign stable across 12 seeds); deterministic given a seed.

## 6. Node smoke — live `--serve` → propagation math

`node --experimental-transform-types ui/smoke/audio_smoke.mts <port> <nFrames> [listener]`
connects to `ws://127.0.0.1:8142`, decodes real TLM with the same `decode.ts` the browser uses,
converts sim→three, and drives each frame through `computePropagation()`. It **asserts every
propagation output is finite**, `delay == range/343`, `gain,crackle ∈ [0,1]`, `doppler > 0`, and
that **gain + absorption cutoff are monotone-non-increasing vs slant range** (any inversion = a
model bug). Also cross-checks a live-file **EVT golden drift** and independently re-derives range.

Three regimes exercised (`booster-core.exe` copied into the worktree; served on **8142 only**):

- **`entry` s42 (62 km):** 500 frames. range 58.3–64.2 km · delay **up to 187 s** · knee pinned
  30 Hz (pure infrasound) · crackle 0% · Doppler clamped 3.00 (supersonic closing, graceful).
  → the far anchor, exactly as canon predicts.
- **`terminal` s42 pad-cam (2 km):** 1500 frames. range 2.0–2.8 km · delay 5.8–8.1 s · knee
  393–608 Hz · Doppler 1.03–1.59.
- **`terminal` s42 near-pad listener `(100,20,100)`:** the full **spectral reveal arc** —
  range **1996 m → 375 m**, delay up to 5.8 s, knee **611 Hz → 5440 Hz**, **crackle 0% @2 km →
  95% @0.4 km**, Doppler 1.28–2.19. *This is the D-011 reveal, proven off the live wire.*

All three: `=== AUDIO PROPAGATION SMOKE — PASS ===`. Serve killed after each; port 8142 freed.

## 7. Canon §12 compliance map

| canon §B.8 / §12 / D-011 requirement | status | where |
|---|---|---|
| All WebAudio-**synthesized**, no samples | ✅ | `synthLayers.ts` (oscillators/filters/procedural buffers only) |
| Crackle = Poisson steep-asymmetric shocklets, **positive skewness 0.1–0.5** | ✅ implemented + **asserted** | `crackle.ts` + `crackle.test.ts` |
| Crackle rate ∝ throttle | ✅ | `crackleRate(throttle, nEng)` |
| **Causally derived, NEVER loops** (D-011) | ✅ | crackle chunks regenerate with a fresh seed each time; bed noise is a gated carrier that never sounds on its own |
| Retarded-time per-source delay `d/343` | ✅ | `RetardedEventQueue` + `DelayNode` |
| 1/r spreading | ✅ | `spreadingGain` |
| Frequency-dependent absorption from **our US76 model** — 20 km = infrasonic rumble, crackle fades in | ✅ (SKETCH knee, not per-band α(f,T,RH)) | `absorptionCutoffHz` + `crackleAudibility` |
| Doppler via variable delay, **clamp supersonic gracefully** | ✅ | `dopplerRatio` clamp + `PropagationChain` |
| Slow turbulence flutter on distant sources | ✅ | `turbulenceFlutter` |
| Ignition thump / shutdown pop / touchdown clang | ✅ | `OneshotLayer` (fired on EVT arrival) |
| Sonic boom = a **triple** (~0.12/0.18 s) from `MACH1_CROSS` emit pos + camera distance | ✅ | `OneshotLayer.tripleBoom`, `MACH1_CROSS` args carry r_emit |
| Observer = active camera; `setListener` API for the director | ✅ | `AudioEngine.setListener` |
| **EVT is the only trigger channel** (§A.0) | ✅ | `evtDecode.ts` → `onEvt` |
| Context resumes on first interaction (splash click) | ✅ | dev-panel ENABLE → `engine.resume()` |
| Dev panel: per-layer meters + mute + arrival-delay readout | ✅ | `devPanel.ts` |
| Muted by default | ✅ | master gain 0; `muted=true` |
| Renderer/audio owns no truth, one-way | ✅ | nothing written upstream |

## 8. Limitations (honest)

- **AUDIBILITY is not verified.** The *math* is unit-tested and smoke-verified end-to-end, but
  whether it *sounds* like a rocket needs **integration eyes/ears** in a real WebGPU browser with
  the audio graph running and the ENABLE gesture. Headless has no AudioContext; this report
  proves the model, not the mix. **This is the one gate S3 cannot close from a terminal.**
- **Absorption is a single-knee SKETCH**, not the full per-band `α(f, T, RH)` US76 integral. It
  hits both canon anchors and is monotone, but the mid-band roll-off shape is approximate. A Tier-B
  upgrade would use a multi-band filterbank or a measured IR (canon's ConvolverNode path).
- **Camera-cut crossfade** (canon's 250 ms propagation-state crossfade) is approximated by the
  per-parameter smoothing constants; an explicit crossfade on `setListener` discontinuities is a
  clean next step.
- **No ground-reflection comb, RCS hiss, aero whistle, TEA-TEB pop, leg thunk, or LUFS metering**
  yet — those are the fuller Tier-A mix (and mostly the IMAX layer's job per D-011). S3 is the
  propagation-model sketch; these are enumerated but out of this rung's scope.
- **Doppler on the continuous bed** is a delay-line bias, not a sample-accurate resampler; fine for
  the sketch, audibly subtle. The one-shots carry no Doppler (they're impulses).
- **Smoke uses `--experimental-transform-types`** (Node 24) to run the TS directly (the decoders use
  `enum`, unsupported by strip-only mode). No bundler step needed; documented in the smoke header.
- The EVT golden is **inlined** as a constant in `evtDecode.test.ts` (the app's tsconfig is DOM-only,
  no `@types/node`, so vitest can't `fs.readFile`); the **live file** is drift-checked by the smoke.

## 9. Wiring contract (for frontend-main / the director)

```ts
import { mountAudio } from "./audio";
const audio = mountAudio();                 // builds engine + dev panel, MUTED
// client handlers:
onTlm:      (f) => { interp.push(f); audio.onTlm(f); }
onEvtBytes: (b) => audio.onEvtBytes(b)
// render loop:
audio.tick();          // keeps the causal crackle stream regenerating
audio.updatePanel();   // refresh meters + "you are N s away" readout
// director:
audio.setListener(camWorldThree)            // whenever the active camera moves
```

---

*Everything after this is the IMAX layer's job (Tier-B native spatial audio, full mix, −16 LUFS).
S3 proves the propagation model — done, gated, and green except the honest audibility caveat.*
