// exportStowed.ts — the CFD body in the AERO-DESCENT configuration (legs STOWED, fins neutral).
// The vendored exporter poses the vehicle legs-down for printing/landing; the frozen aero table in
// core/dynamics.c (CA/CN vs Mach) models the descent, where the legs are folded until the last
// seconds. This wrapper intercepts the exporter's pose call so deploy_frac stays 0, without
// touching the verbatim file. DEV-only: `__exportStowedSTL(name)` posts the binary STL to the vite
// capture sink (runs/shots/<name>.jpg — rename to .stl), because a headless page cannot download.
import type { Kestrel9, KestrelTelemetry } from "./kestrel9.js";
import { buildExportModel, toBinarySTL } from "./export-stl.js";
import type { ThreeLike } from "./threeShim";

export function buildStowedSTL(THREE: ThreeLike, booster: Kestrel9): ArrayBuffer {
  const orig = booster.setTelemetry;
  booster.setTelemetry = (t: KestrelTelemetry = {}) => orig.call(booster, { ...t, deploy_frac: 0 });
  try {
    const model = buildExportModel(THREE, booster, "cfd");
    return toBinarySTL(THREE, model, { scale: 1, header: "KESTREL-9 cfd metres 1:1 Z-up LEGS STOWED (aero descent)" });
  } finally {
    booster.setTelemetry = orig;
  }
}

function toBase64(buf: ArrayBuffer): string {
  const bytes = new Uint8Array(buf);
  let s = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) s += String.fromCharCode.apply(null, Array.from(bytes.subarray(i, i + chunk)));
  return btoa(s);
}

/** POST the STL bytes to the dev capture sink; returns the byte count. */
export async function postStowedSTL(THREE: ThreeLike, booster: Kestrel9, name: string): Promise<number> {
  const buf = buildStowedSTL(THREE, booster);
  const r = await fetch(`/__cap?name=${encodeURIComponent(name)}`, { method: "POST", body: toBase64(buf) });
  if (!r.ok) throw new Error(`capture sink ${r.status}`);
  return buf.byteLength;
}
