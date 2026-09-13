// plume.d.ts — hand-written type surface for the vendored plume.js (see PROVENANCE.md).
import type { Group, InstancedMesh, Mesh, PointLight, SpotLight, Sprite } from "three/webgpu";
import type { Kestrel9, ThreeLike } from "./kestrel9.js";

/** Telemetry fields read by setTelemetry — the packet's own names plus `bell_alt` (derived). */
export interface PlumeTelemetry {
  throttle_act?: number;
  throttle?: number;
  n_eng?: number;
  p_amb?: number;
  mach?: number;
  qbar?: number;
  /** bell-exit height over terrain [m]; clamps the free jet into a radial wall jet on impingement */
  bell_alt?: number;
}

export interface PlumeUnit {
  root: Group;
  bellFlame: Mesh;
  throat: Mesh;
  jet: Group;
  core: Mesh;
  sheath: Mesh;
  diamonds: InstancedMesh;
  flash: Sprite;
  index: number;
}
export interface PlumeState {
  throttle: number;
  nEng: number;
  pAmb: number;
  mach: number;
  qbar: number;
  flash: number;
  t: number;
  bellAlt: number;
}

export interface KestrelPlume {
  /** the booster group the plume attached itself to */
  group: Group;
  units: PlumeUnit[];
  light: PointLight;
  spot: SpotLight;
  srp: Mesh;
  state: PlumeState;
  isLit(i: number, n: number): boolean;
  setTelemetry(t?: PlumeTelemetry): KestrelPlume;
  update(dt?: number): KestrelPlume;
  /** TEA-TEB green flash (call on EVT GREEN_FLASH) */
  flash(): KestrelPlume;
  dispose(): void;
}

export function createPlume(THREE: ThreeLike, booster: Kestrel9, opts?: object): KestrelPlume;
export default createPlume;
