// kestrelVehicle.ts — the scene handle for the vendored Kestrel-9 renderer (PLAN.md Phase 1.1:
// "drop kestrel9.js + plume.js into ui/src/scene/, feed setTelemetry(tlm) from the decoded frame").
//
// The handle owns ONE stable outer Group (add it to boosterPivot once); the booster + plume live
// under it and are replaced WHOLESALE when HELLO brings different dimensions — this backend drops a
// live mesh to unlit white if you swap its geometry (documentaryScene.ts:617), so we never do.
import type { Group } from "three/webgpu";
import type { TlmFrame } from "../../net/decode";
import type { HelloFrame } from "../../net/events";
import { createKestrel9, VEHICLE, type Kestrel9, type KestrelVehicleDims } from "./kestrel9.js";
import { createPlume, type KestrelPlume } from "./plume.js";
import { makeKestrelThree, type ThreeLike } from "./threeShim";
import { makeKestrelTlm, tlmToKestrel } from "./kestrelTlm";

export type KestrelQuality = "low" | "medium" | "ultra";

export interface KestrelVehicle {
  /** Stable root: origin = base plane / gimbal plane, local +Y = long axis (already §10.7-permuted). */
  group: Group;
  /** live model handles (replaced on a HELLO rebuild — re-read after applyHello) */
  readonly booster: Kestrel9;
  readonly plume: KestrelPlume;
  /** bell-exit depth below the base plane [m] — for bell_alt */
  readonly bellLength: number;
  /** Per frame: push the decoded frame through the adapter and advance the plume. */
  update(f: TlmFrame, bellAltM: number, dtSec: number): void;
  /** Rebuild wholesale if HELLO's dimensions differ from the built ones. */
  applyHello(h: HelloFrame): void;
  /** TEA-TEB flash (EVT GREEN_FLASH). */
  triggerGreenFlash(): void;
  dispose(): void;
}

/** Canon §5 defaults overridden by HELLO (veh_dia, veh_len, leg_span = deployed footprint DIAMETER). */
export function dimsFromHello(h: HelloFrame | undefined, base: KestrelVehicleDims = VEHICLE): KestrelVehicleDims {
  const d: KestrelVehicleDims = { ...base, finAz: [...base.finAz], legAz: [...base.legAz], finPanel: [...base.finPanel] };
  if (!h) return d;
  if (h.vehDia > 0) d.radius = h.vehDia / 2;
  if (h.vehLen > 0) d.barrel = Math.max(1, h.vehLen - d.interstage);
  if (h.legSpan > 0) d.legSpan = h.legSpan / 2;
  return d;
}

function sameDims(a: KestrelVehicleDims, b: KestrelVehicleDims): boolean {
  const eps = 1e-3;
  return Math.abs(a.radius - b.radius) < eps && Math.abs(a.barrel - b.barrel) < eps && Math.abs(a.legSpan - b.legSpan) < eps;
}

export interface KestrelBuildOptions {
  hello?: HelloFrame;
  quality?: KestrelQuality;
  /** the THREE namespace to inject (default: the real three/webgpu, see threeShim.ts) */
  three?: ThreeLike;
}

// ---------------------------------------------------------------------------------------------
// SCENE TUNING (D-056 follow-up, 2026-09-12) — the asset was lit for its own viewer (exposure 1.0,
// a modest sky). This scene runs a physical-sky PMREM at environmentIntensity 0.42 with exposure
// 0.34 and AgX, calibrated in commit 908bc53 for a white paint at envMapIntensity 0.3. Left at the
// asset's 0.72–2.0 the hull is a bluish sky-mirror (probe 0.93/1.14/1.40 linear at 40 m). These
// knobs are applied AFTER construction — the vendored file stays verbatim.
// ---------------------------------------------------------------------------------------------
/** absolute envMapIntensity for the paint (the 908bc53 calibration), a multiplier for the rest */
export const K9_ENV = { bodyPaint: 0.3, frost: 0.3, raceway: 0.35, othersScale: 0.6 } as const;
/** HDR gain on the plume so it crosses the bloom threshold (the asset's plume is LDR ≤ 1.0) */
export const K9_PLUME_GAIN = { bellFlame: 2.6, throat: 4.0, core: 3.0, sheath: 1.6, diamonds: 2.6, srp: 2.2, flash: 2.0 } as const;

function tuneMaterials(booster: Kestrel9, plume: KestrelPlume): void {
  for (const [name, m] of Object.entries(booster.materials)) {
    const std = m as unknown as { isMeshStandardMaterial?: boolean; envMapIntensity: number; needsUpdate: boolean };
    if (!std.isMeshStandardMaterial) continue;
    if (name === "bodyPaint") std.envMapIntensity = K9_ENV.bodyPaint;
    else if (name === "frost") std.envMapIntensity = K9_ENV.frost;
    else if (name === "raceway") std.envMapIntensity = K9_ENV.raceway;
    else std.envMapIntensity *= K9_ENV.othersScale;
    std.needsUpdate = true;
  }
  const gain = (mesh: { material: unknown }, k: number) => {
    const mat = mesh.material as { color?: { multiplyScalar(s: number): unknown } };
    mat.color?.multiplyScalar(k);
  };
  for (const u of plume.units) {
    gain(u.bellFlame, K9_PLUME_GAIN.bellFlame);
    gain(u.throat, K9_PLUME_GAIN.throat);
    gain(u.core, K9_PLUME_GAIN.core);
    gain(u.sheath, K9_PLUME_GAIN.sheath);
    gain(u.diamonds, K9_PLUME_GAIN.diamonds);
    gain(u.flash, K9_PLUME_GAIN.flash);
  }
  gain(plume.srp, K9_PLUME_GAIN.srp);
}

export function buildKestrelVehicle(opts: KestrelBuildOptions = {}): KestrelVehicle {
  const THREE = opts.three ?? makeKestrelThree();
  const quality: KestrelQuality = opts.quality ?? "ultra";
  let dims = dimsFromHello(opts.hello);

  const group = new THREE.Group();
  group.name = "KestrelVehicle";

  let booster = createKestrel9(THREE, { vehicle: dims, quality });
  let plume = createPlume(THREE, booster);
  tuneMaterials(booster, plume);
  group.add(booster.group);

  const tlm = makeKestrelTlm();

  const api: KestrelVehicle = {
    group,
    get booster() {
      return booster;
    },
    get plume() {
      return plume;
    },
    get bellLength() {
      return booster.vehicle.bellLength;
    },

    update(f, bellAltM, dtSec) {
      tlmToKestrel(f, bellAltM, tlm);
      booster.setTelemetry(tlm);
      plume.setTelemetry(tlm);
      plume.update(dtSec);
    },

    applyHello(h) {
      const next = dimsFromHello(h);
      if (sameDims(next, dims)) return;
      dims = next;
      group.remove(booster.group);
      plume.dispose();
      booster.dispose();
      booster = createKestrel9(THREE, { vehicle: dims, quality });
      plume = createPlume(THREE, booster);
      tuneMaterials(booster, plume);
      group.add(booster.group);
    },

    triggerGreenFlash() {
      plume.flash();
    },

    dispose() {
      group.remove(booster.group);
      plume.dispose();
      booster.dispose();
    },
  };
  return api;
}
