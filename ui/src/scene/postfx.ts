// postfx.ts — the WebGPU post-processing chain (canon §11.1; the §5.2 bloom the four
// F1 agents left flagged-but-unwired because they had no way to eyes-on it).
//
// A single selective-bloom pass: render the scene to an HDR pass, bloom the bright
// (>threshold) regions, add the glow back, and let PostProcessing apply the AgX tone
// map + sRGB at output (outputColorTransform, on by default). The plume (fx/plume.ts)
// emits HDR (>1) additively and the TEA-TEB green flash + plume light are bright, so
// they bloom; the LDR daytime scene mostly sits under the threshold and stays crisp.
//
// r185 API (verified against the webgpu_postprocessing_bloom examples):
//  - pass(scene, camera)                      from 'three/tsl'
//  - bloom(node, strength, radius, threshold) from 'three/addons/tsl/display/BloomNode.js'
//  - PostProcessing(renderer)                 from 'three/webgpu'
// Works on the WebGPU backend AND the WebGL2 fallback (three's node graph targets both).

import { PostProcessing } from "three/webgpu";
import type { WebGPURenderer, Scene, PerspectiveCamera } from "three/webgpu";
import { pass } from "three/tsl";
import { bloom } from "three/addons/tsl/display/BloomNode.js";

/** Bloom look (tunable — eyes-on). Threshold is a LINEAR-HDR luminance gate: the scene
 * pass is pre-tone-map, so the >1 plume/flash clears it while the tone-mapped daytime
 * sky/hull mostly does not. Raise threshold to bloom less; raise strength to bloom harder. */
export const BLOOM = {
  strength: 0.8,
  radius: 0.55,
  threshold: 0.9,
};

export interface PostFx {
  /** Render the scene through the bloom chain (replaces renderer.render). */
  render(): void;
  /** Live-tune the bloom (dev/eyes-on). */
  set(strength: number, radius: number, threshold: number): void;
}

export function buildPostFx(
  renderer: WebGPURenderer,
  scene: Scene,
  camera: PerspectiveCamera
): PostFx {
  const scenePass = pass(scene, camera);
  const bloomPass = bloom(scenePass, BLOOM.strength, BLOOM.radius, BLOOM.threshold);

  const post = new PostProcessing(renderer);
  // scene color + additive glow; PostProcessing applies AgX + sRGB at output.
  post.outputNode = scenePass.add(bloomPass);

  return {
    render() {
      post.render();
    },
    set(strength, radius, threshold) {
      // BloomNode exposes strength/radius/threshold as uniforms (.value settable).
      const b = bloomPass as unknown as {
        strength: { value: number };
        radius: { value: number };
        threshold: { value: number };
      };
      b.strength.value = strength;
      b.radius.value = radius;
      b.threshold.value = threshold;
    },
  };
}
