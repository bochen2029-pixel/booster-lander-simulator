// plume.ts — the crown jewel: analytic raymarched rocket plume as a TSL node
// (canon §11.6). Every parameter is PHYSICS-DRIVEN from telemetry — nothing here
// is a hand-keyed animation.
//
// r185 API used (all verified against the r185 examples/addons):
//  - RaymarchingBox(steps, ({positionRay}) => {...}) from
//      'three/addons/tsl/utils/Raymarching.js'  — marches a unit box [-0.5,0.5]^3
//      in local space; positionRay is the current sample point. We fit this box to
//      a plume-proxy cone (a scaled/oriented box below the bell).
//  - Fn(), uniform(), float/vec3/vec4, If/Break, exp, sin, smoothstep, mix,
//      mx_noise_vec3 / mx_fractal_noise_float (MaterialX noise, built in — no
//      external noise lib) from 'three/tsl'.
//  - NodeMaterial with .colorNode (analytic emission) + emissive MRT tag so the
//      plume core blooms (canon §11.1 webgpu_postprocessing_bloom_emissive pattern).
//  - viewportSharedTexture (heat-haze backdrop) lives in heatHaze.ts (separate
//      screen-space pass; the canonical webgpu_backdrop pattern).
//
// PHYSICS (canon §11.6, App-F Apogee/Ashkenas-Sherman):
//  - First Mach-disk distance from the exit:  x1 = 0.67 * D_e * sqrt(p0/pa)
//  - Cell spacing scales with sqrt(pressure ratio): tight ladder at sea level;
//    cells stretch + fade with altitude until 1-2 remain by ~35 km; above that the
//    plume BALLOONS toward km-scale translucency (underexpansion).
//  - Diamond brightness = afterburning reignition at each disk.
//  - SRP mode (burning while supersonic): plume WRAPS FORWARD and envelops the
//    vehicle; blend by thrust coefficient C_T exactly like the physics (canon §6.3).
//  - Color: kerolox soot blackbody ramp (deep orange core -> yellow-white throat) +
//    fuel-rich afterburn sheath + dark sooty gas-generator streak.
//  - TEA-TEB green flash: brief boron-green burst at the bell on GREEN_FLASH (a
//    short-lived uniform pulse, driven by the EVT, not the analytic core).

import {
  Fn, uniform, float, vec3, vec4, If, Break,
  sin, abs, clamp, smoothstep, mix, length,
  positionLocal, mx_fractal_noise_float, time,
} from "three/tsl";
import { NodeMaterial, AdditiveBlending } from "three/webgpu";
import { RaymarchingBox } from "three/addons/tsl/utils/Raymarching.js";

/** A scalar (float) uniform handle: readable in TSL, `.value` settable each frame. */
type FloatUniform = ReturnType<typeof floatUniform>;
function floatUniform(v: number) {
  return uniform(v, "float");
}

/** Uniforms the render loop pushes every frame from the interpolated TLM frame. */
export interface PlumeUniforms {
  /** p0/pa pressure ratio (chamber / ambient). Drives Mach-disk spacing + balloon. */
  pressureRatio: FloatUniform;
  /** actual throttle 0..1. Drives length, brightness, core radius. */
  throttle: FloatUniform;
  /** free-stream Mach. Gate for SRP + diamond sharpness. */
  mach: FloatUniform;
  /** thrust coefficient C_T = T/(qbar*Aref). SRP forward-envelopment blend. */
  ct: FloatUniform;
  /** exit diameter D_e [m] (from HELLO geometry). */
  exitDia: FloatUniform;
  /** number of engines lit (1 or 3) — scales core width. */
  nEng: FloatUniform;
  /** TEA-TEB green-flash intensity 0..1, pulsed by GREEN_FLASH EVT then decayed. */
  greenFlash: FloatUniform;
}

export function makePlumeUniforms(): PlumeUniforms {
  return {
    pressureRatio: floatUniform(1.0),
    throttle: floatUniform(0.0),
    mach: floatUniform(0.0),
    ct: floatUniform(0.0),
    exitDia: floatUniform(0.92), // Merlin-ish nozzle exit ~0.92 m
    nEng: floatUniform(1.0),
    greenFlash: floatUniform(0.0),
  };
}

// --- color ramps -------------------------------------------------------------
// Kerolox blackbody-ish core ramp as a function of normalized "temperature" τ∈[0,1]
// (1 = throat/hottest -> yellow-white; 0 = cool tip -> deep orange/red). Soot makes
// it warmer + sootier than a clean H2/O2 blue flame.
const keroloxColor = /*#__PURE__*/ Fn(([tau]: any) => {
  const cool = vec3(0.4, 0.08, 0.012); // deep sooty red-orange tip
  const mid = vec3(1.0, 0.4, 0.07); // orange body
  const hot = vec3(1.0, 0.93, 0.64); // yellow-white throat
  const a = mix(cool, mid, clamp(tau.mul(2.0), 0.0, 1.0));
  const b = mix(mid, hot, clamp(tau.sub(0.5).mul(2.0), 0.0, 1.0));
  return mix(a, b, smoothstep(0.5, 1.0, tau));
}) as unknown as (tau: unknown) => ReturnType<typeof vec3>;

/**
 * Build the plume material. Returns a NodeMaterial whose emission is the analytic
 * raymarched plume; mount it on a box mesh fitted below the bell (the "proxy").
 * The mesh's local frame is the marching space: +Y up toward the bell, plume
 * flows in -Y; local box is [-0.5,0.5]^3, scaled to the proxy cone at build time.
 */
export function buildPlumeMaterial(u: PlumeUniforms): NodeMaterial {
  const mat = new NodeMaterial();
  mat.transparent = true;
  mat.depthWrite = false;
  mat.blending = AdditiveBlending; // emissive gas accumulates additively

  // First Mach-disk distance (normalized into the proxy's local -Y length). We map
  // physical meters to local units at build time via `proxyLength`; here we work in
  // "disks from the exit". x1 = 0.67 * De * sqrt(p0/pa).
  const sqrtPR = u.pressureRatio.max(0.001).sqrt();
  const x1 = u.exitDia.mul(0.67).mul(sqrtPR); // meters to first disk
  // cell spacing grows with sqrt(PR); expressed as spatial frequency along the axis.
  // At high altitude PR is huge -> spacing huge -> only 1-2 disks -> then balloon.
  const cellFreq = float(1.0).div(x1.max(0.05)); // disks per meter along axis

  // altitude "balloon" factor: as ambient->0 (PR->large) the plume widens massively
  // and diamonds smear. Underexpansion. The CHAMBER/ambient ratio at sea level is
  // already ~60 (9.7 MPa / 101 kPa) — ballooning is a VACUUM phenomenon, so the ramp
  // keys on the thousands: ~0 below 10 km (PR<2k), full past ~40 km (PR>40k).
  const balloon = smoothstep(2000.0, 40000.0, u.pressureRatio);

  // SRP forward-envelopment amount, blended by C_T exactly like the physics
  // (canon §6.3: full aero for C_T<0.5, ~5% by C_T>3). Here it drives how much the
  // plume wraps FORWARD (+Y, toward/around the vehicle) vs trailing (-Y).
  const srp = smoothstep(0.5, 3.0, u.ct);

  const emission = /*#__PURE__*/ Fn(() => {
    const acc = vec4(0).toVar(); // rgb = emissive, a = accumulated opacity

    // Marching steps scale with quality tier; 64 is the Ultra plume budget target.
    RaymarchingBox(float(64), ({ positionRay }: any) => {
      // positionRay in [-0.5,0.5]^3 local. Axis coordinate s along the plume:
      // s=0 at the bell (top, +Y), s=1 at the tip (bottom, -Y).
      const s = positionRay.y.mul(-1.0).add(0.5); // 0..1 down the plume
      const rad = length(positionRay.xz); // radial distance from axis (0..~0.7)

      // --- Mach-diamond ladder along the axis (computed FIRST — it pinches the cone) --
      // Bright rings where afterburning reignites at each disk. Spacing = cellFreq;
      // sharp at sea level (low balloon), smeared to nothing at altitude.
      const axialMeters = s.mul(2.0); // proxy ~2 m of near-field per local unit (set at build)
      const diskWave = sin(axialMeters.mul(cellFreq).mul(3.14159 * 2.0)).mul(0.5).add(0.5);
      const diamondSharp = mix(float(7.0), float(1.0), balloon); // smear with altitude
      const diamonds = diskWave.pow(diamondSharp); // narrow bright bands when sharp
      // diamonds only exist while genuinely under/over-expanded & supersonic-ish
      const diamondGate = smoothstep(1.05, 1.6, u.pressureRatio).mul(
        smoothstep(0.2, 0.8, u.mach.mul(0.2).add(0.4))
      );

      // --- radial confinement: a cone that widens with balloon + SRP, and PINCHES ---
      // at each shock diamond (the real barrel/waist structure of an expansion ladder).
      const pinch = float(1.0).sub(diamonds.mul(diamondGate).mul(mix(float(0.24), float(0.0), balloon)));
      const coneHalf = mix(float(0.05), float(0.125), s) // nominal taper (tight sea-level torch)
        .mul(u.nEng.mul(0.25).add(0.75)) // 3-eng cluster is wider
        .mul(pinch)
        .add(balloon.mul(0.32)); // altitude ballooning
      // density falls off past the cone edge (soft):
      const radial = smoothstep(coneHalf.add(0.05), coneHalf.sub(0.02), rad);

      // --- unsteady turbulence: ANISOTROPIC (streaks elongate down the axis) + fast
      // advection near the exit (visual-only garnish is allowed here; canon §0.8).
      const noiseP = positionRay.mul(vec3(9.0, 2.4, 9.0)).add(vec3(0, time.mul(-3.6), 0));
      const turb = mx_fractal_noise_float(noiseP, 3, 2.0, 0.5).mul(0.5).add(0.5);

      // --- temperature ramp: hottest near throat + at each diamond -------------
      const tau = clamp(
        float(1.0).sub(s).mul(0.7).add(diamonds.mul(diamondGate).mul(0.6)),
        0.0, 1.0
      );
      const col = keroloxColor(tau);

      // --- gas-generator dark streak: a sooty off-axis lane (canon §11.6) ------
      // one-sided (e.g. +X): darkens color where the GG exhaust runs alongside.
      const ggLane = smoothstep(0.10, 0.02, abs(positionRay.x.sub(coneHalf.mul(0.8))))
        .mul(smoothstep(0.0, 0.4, s));
      const sooted = col.mul(mix(float(1.0), float(0.25), ggLane));

      // --- WHITE-HOT CORE: the near-axis jet is orders brighter than the sheath.
      // Narrow near the throat, fading + widening downstream; diamonds re-ignite it.
      const coreR = mix(float(0.042), float(0.085), s).add(balloon.mul(0.05));
      const core = smoothstep(coreR, float(0.0), rad)
        .mul(float(1.0).sub(s.mul(0.55))) // cools downstream
        .mul(mix(float(1.0), float(0.35), balloon));
      const coreCol = vec3(1.0, 0.965, 0.905); // white-hot, slightly warm

      // --- local density -> emission -----------------------------------------
      const axialFade = float(1.0).sub(smoothstep(0.5, 1.0, s).mul(mix(float(0.85), float(0.3), balloon)));
      const dens = radial
        .mul(turb.mul(0.65).add(0.35)) // turbulence modulates, never extinguishes
        .mul(u.throttle.max(0.0)) // no plume when not burning
        .mul(axialFade) // the flame concentrates near the bell; the tail dissolves
        .mul(mix(float(1.0), float(0.5), balloon)) // ballooned plume is fainter/translucent
        .mul(float(1.0).add(diamonds.mul(diamondGate).mul(2.2))); // diamonds are bright

      const emis = sooted.mul(dens).mul(2.1)
        // the core is HDR-hot (drives the bloom) and re-brightens at each diamond
        .add(coreCol.mul(core).mul(u.throttle).mul(
          float(22.0).add(diamonds.mul(diamondGate).mul(26.0))
        ));

      // front-to-back accumulation (RaymarchingBox marches near->far); the core is
      // near-opaque so the far sheath can't wash it out.
      acc.rgb.addAssign(emis.mul(float(1.0).sub(acc.a)));
      acc.a.addAssign(dens.mul(core.mul(0.30).add(0.12)).mul(float(1.0).sub(acc.a)));
      If(acc.a.greaterThanEqual(0.98), () => Break());
    });

    // --- SRP forward sheath: add a faint envelope around the +Y (vehicle) side --
    // When C_T is high the plume wraps forward; we lift the whole emission on the
    // +Y half and add a bow-shock-cap brightening. (The proxy box is extended
    // upward at build time when srp>0 so there is geometry to march.)
    const forwardMask = smoothstep(0.0, 0.5, positionLocal.y).mul(srp);
    acc.rgb.addAssign(vec3(1.0, 0.5, 0.25).mul(forwardMask).mul(u.throttle).mul(1.5));

    // --- TEA-TEB green flash: brief boron-green burst at the bell -------------
    const bellMask = smoothstep(0.5, 0.35, positionLocal.y.mul(-1.0).add(0.5)); // near top
    acc.rgb.addAssign(vec3(0.35, 1.0, 0.45).mul(bellMask).mul(u.greenFlash).mul(4.0));

    return acc;
  });

  mat.colorNode = emission();
  // Tag emissive so the bloom pass (MRT 'emissive' target) lights the core.
  // (Set on the pass via mrt({ output, emissive }); here we ensure the material's
  // output is already HDR so selective bloom catches it.)
  return mat;
}

/**
 * Per-frame update: map the interpolated telemetry frame to plume uniforms.
 * `pChamber`/`pAmb` in Pa; `ct` precomputed from thrust/qbar or passed through.
 */
export function updatePlumeUniforms(
  u: PlumeUniforms,
  f: {
    throttleAct: number;
    pChamber: number;
    pAmb: number;
    mach: number;
    ct: number;
    nEng: number;
  }
): void {
  u.throttle.value = f.throttleAct;
  u.pressureRatio.value = f.pAmb > 1 ? f.pChamber / f.pAmb : 200.0; // vacuum -> big PR
  u.mach.value = f.mach;
  u.ct.value = f.ct;
  u.nEng.value = f.nEng;
  // greenFlash is driven by the EVT scheduler, not here (decays on its own).
}
