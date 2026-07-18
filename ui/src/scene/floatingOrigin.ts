// floatingOrigin.ts — camera-relative rendering for the 70km->1m continuous shot
// (canon §11.1 "Floating origin", §14 M7 gate, Risk #10).
//
// THE PROBLEM: fp32 is the vertex/GPU reality. At h=70 km a fp32 position quantum
// is ~8 mm (canon §6.1) — visible jitter at telescope zoom, and z-fighting across
// a 70 km depth range. The sim plant is fp64 and telemetry positions arrive as
// fp32 *relative to the pad origin*, which is already small enough near the ground
// but NOT when the camera is 62 km away framing a booster.
//
// THE STRATEGY (three of them, layered):
//
//  1. AUTHORITATIVE DOUBLES IN JS. We keep the world/scene positions in JS Numbers
//     (fp64). All telemetry math (interp, cloud, ghost line) is done in doubles.
//
//  2. CAMERA-RELATIVE REBASE. We never push large absolute coordinates to the GPU.
//     Instead we hold a `worldOrigin` (a fp64 Vector3d) and render everything
//     offset by (-worldOrigin). The camera itself sits near (0,0,0) in render
//     space. When the camera drifts more than REBASE_THRESHOLD (2 km) from the
//     current origin, we recenter: set worldOrigin = camera's world position,
//     shift the `world` group by the delta. Rebase is seamless because it is an
//     exact fp64 subtraction applied uniformly.
//
//     Concretely: renderPos = (worldPos_f64 - worldOrigin_f64) downcast to f32.
//     Near the camera the subtraction cancels the large magnitude, so the f32 we
//     hand the GPU is small (|.| < a few km) and its quantum is sub-mm. Far away
//     (the horizon, stars) precision doesn't matter — you can't resolve 8 mm at
//     62 km anyway.
//
//  3. REVERSED-Z DEPTH. renderer.ts sets reversedDepthBuffer:true (depth32float).
//     Reversed-z puts fp precision where it matters (near plane) and, combined
//     with the small camera-relative coordinates, kills z-fighting from 200 km far
//     plane down to a 1 m booster in one continuous projection — no near/far
//     swapping, no cascaded frusta.
//
// Stars & sky (takram atmosphere) are drawn in a separate always-centered pass
// (they follow the camera; infinite distance), so they never participate in rebase.

import { Vector3, Group, PerspectiveCamera } from "three/webgpu";

/** fp64 vector (three's Vector3 stores fp64 in JS Numbers already, but we name it
 *  to make the precision contract explicit at call sites). */
export type Vec3d = Vector3;

const REBASE_THRESHOLD_M = 2000; // canon §11.1: rebase when |camera offset| > 2 km

export class FloatingOrigin {
  /** The fp64 world-space point currently mapped to render-space (0,0,0). */
  readonly worldOrigin: Vector3 = new Vector3(0, 0, 0);
  private rebaseCount = 0;

  private readonly _tmp = new Vector3();

  /**
   * Map an authoritative fp64 world position to render space (subtract origin).
   * Call for every large-magnitude object each frame (booster, ghost knots, pad).
   */
  toRender(worldPos: Vector3, out: Vector3): Vector3 {
    return out.copy(worldPos).sub(this.worldOrigin);
  }

  /**
   * Given the camera's authoritative fp64 world position (where we WANT it, in the
   * sim/render world), position the actual three camera near the origin and rebase
   * the `world` group if the camera has drifted too far. Returns true if a rebase
   * occurred (useful to invalidate cached render positions).
   *
   * `world` is the Group holding all rebased content (its .position is the running
   * -worldOrigin offset).
   */
  update(cameraWorldPos: Vector3, camera: PerspectiveCamera, world: Group): boolean {
    // camera position in render space:
    this.toRender(cameraWorldPos, this._tmp);
    let rebased = false;

    if (this._tmp.length() > REBASE_THRESHOLD_M) {
      // Recenter the origin on the camera's world position (exact fp64 op).
      this.worldOrigin.copy(cameraWorldPos);
      this.rebaseCount++;
      rebased = true;
      // world group offset is -worldOrigin; content queries toRender() so it is
      // consistent automatically. The group offset exists for static children that
      // were added in world coordinates (the ugly pad/grid).
      world.position.copy(this.worldOrigin).multiplyScalar(-1);
      // camera now at (cameraWorldPos - worldOrigin) == 0:
      camera.position.set(0, 0, 0);
    } else {
      camera.position.copy(this._tmp);
      // keep the world group offset in sync (it only changes on rebase)
      world.position.copy(this.worldOrigin).multiplyScalar(-1);
    }
    return rebased;
  }

  get rebases(): number {
    return this.rebaseCount;
  }
}
