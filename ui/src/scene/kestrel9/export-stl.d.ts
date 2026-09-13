// export-stl.d.ts — hand-written type surface for the vendored export-stl.js (see PROVENANCE.md).
import type { Group } from "three/webgpu";
import type { Kestrel9, ThreeLike } from "./kestrel9.js";

export type ExportVariant = "cfd" | "print";
/** Rebuild the vehicle from closed primitives at the export pose (legs down, fins neutral). */
export function buildExportModel(THREE: ThreeLike, booster: Kestrel9, variant?: ExportVariant): Group;
/** Binary STL of a group (sim world frame, Z-up by default). */
export function toBinarySTL(THREE: ThreeLike, root: Group, opts?: { scale?: number; zUp?: boolean; header?: string }): ArrayBuffer;
/** Build + write + trigger a browser download. */
export function exportSTL(THREE: ThreeLike, booster: Kestrel9, opts?: { variant?: ExportVariant; scale?: number; filename?: string }): void;
