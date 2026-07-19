// types.ts — shared pure types for the CONSTELLATION view (no three/DOM imports),
// so both the 3D scene (scene.ts) and the DOM chrome (dom.ts) can reference them
// without the DOM layer pulling in the WebGPU renderer.

import type { ClassifiedRun } from "./runData";

/** Which population a glyph belongs to in A/B mode. */
export type Side = "A" | "B";

/** A picked glyph: the run + which side (A/B population) it came from. */
export interface Pick {
  run: ClassifiedRun;
  side: Side;
}
