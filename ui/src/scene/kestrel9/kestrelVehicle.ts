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
  /** the THREE namespace to inject (default: the DataTexture shim, see threeShim.ts) */
  three?: ThreeLike;
}

export function buildKestrelVehicle(opts: KestrelBuildOptions = {}): KestrelVehicle {
  const THREE = opts.three ?? makeKestrelThree();
  const quality: KestrelQuality = opts.quality ?? "ultra";
  let dims = dimsFromHello(opts.hello);

  const group = new THREE.Group();
  group.name = "KestrelVehicle";

  let booster = createKestrel9(THREE, { vehicle: dims, quality });
  let plume = createPlume(THREE, booster);
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
