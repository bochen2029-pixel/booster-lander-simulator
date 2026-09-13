// kestrel9.d.ts — hand-written type surface for the vendored kestrel9.js (see PROVENANCE.md).
// Keep in step with the JS if it is ever re-vendored. THREE is injected, never imported, by the
// module itself; the app hands it the namespace it renders with (three/webgpu, via threeShim.ts).
import type {
  Group,
  Material,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  Object3D,
  Texture,
  Vector3,
} from "three/webgpu";

export type ThreeLike = typeof import("three/webgpu");

/** Canon §5 dimensions the model is built from (metres, degrees). */
export interface KestrelVehicleDims {
  radius: number;
  barrel: number;
  interstage: number;
  finAz: number[];
  legAz: number[];
  legSpan: number;
  finPanel: [number, number];
  rcsY: number;
  engineRing: number;
  bellExit: number;
  bellLength: number;
  blackBand: number;
}
export const VEHICLE: KestrelVehicleDims;

/** Telemetry fields read by setTelemetry — named exactly as the C packet (canon §10.3). */
export interface KestrelTelemetry {
  throttle_act?: number;
  throttle?: number;
  gimbal_act?: ArrayLike<number>;
  fins_act?: ArrayLike<number>;
  deploy_frac?: number;
  stroke?: ArrayLike<number>;
  n_eng?: number;
  Q_heat?: number;
  soot?: number;
  frost?: number;
}

export interface KestrelEngine {
  gimbal: Group;
  mesh: Object3D;
  index: number;
  matOuter: MeshStandardMaterial;
  matInner: MeshStandardMaterial;
  matLip: MeshBasicMaterial;
  /** bell-exit anchor in the group frame */
  pos: Vector3;
}
export interface KestrelFin {
  pivot: Group;
  hinge: Group;
  panel: Group;
}
export interface KestrelLeg {
  pivot: Group;
  leg: Group;
  segs: Mesh[];
  foot: Group;
  collar: Mesh;
  strut: Mesh;
}
export interface KestrelParts {
  fins: KestrelFin[];
  legs: KestrelLeg[];
  engines: KestrelEngine[];
  rcsPods: { pivot: Group; body: Mesh; nozzles: Mesh[] }[];
  puffs: unknown[];
  barrel: Mesh;
  soot: Mesh;
  frost: Mesh;
  raceway: Group;
  interstage: Group;
  engineSection: Group;
}
export interface KestrelState {
  throttle: number;
  gimbal: ArrayLike<number>;
  fins: ArrayLike<number>;
  deploy: number;
  stroke: ArrayLike<number>;
  soot: number;
  frost: number;
  nEng: number;
  hot: number;
}

export interface Kestrel9 {
  /** The vehicle root, authored in the sim body frame already permuted to three (§10.7). */
  group: Group;
  parts: KestrelParts;
  materials: Record<string, Material>;
  vehicle: KestrelVehicleDims;
  state: KestrelState;
  setTelemetry(t?: KestrelTelemetry): Kestrel9;
  setEnvMap(envMap: Texture | null, intensity?: number): Kestrel9;
  /** bell-exit anchors (group frame), for plume / light placement */
  enginePositions: Vector3[];
  dispose(): void;
}

export interface KestrelOptions {
  vehicle?: Partial<KestrelVehicleDims>;
  quality?: "low" | "medium" | "ultra";
}

export function createKestrel9(THREE: ThreeLike, opts?: KestrelOptions): Kestrel9;
export default createKestrel9;
