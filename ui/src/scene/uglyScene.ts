// uglyScene.ts — the M3 acceptance scene (canon §14 M3: "ugly scene (capsule +
// plane)"). Deliberately minimal: a capsule stands in for the booster, a plane for
// the pad. Its ONLY job is to prove the pipeline end-to-end: decode -> interp ->
// convert -> pose a mesh, at display refresh, with zero drops over 10 min and
// sub-frame jitter. The cinematic scene (procedural booster, plume) is M7.

import {
  Mesh,
  CapsuleGeometry,
  PlaneGeometry,
  MeshStandardMaterial,
  MeshBasicMaterial,
  DirectionalLight,
  AmbientLight,
  Group,
  Vector3,
  Quaternion,
  GridHelper,
  AxesHelper,
} from "three/webgpu";
import type { Scene } from "three/webgpu";
import { simToThreePosition, simToThreeQuaternion } from "../net/frame";
import type { InterpSample } from "../net/interp";

export interface UglyScene {
  /** Root that gets rebased by the floating-origin camera (camera-relative). */
  world: Group;
  booster: Mesh;
  update: (s: InterpSample) => void;
}

export function buildUglyScene(scene: Scene): UglyScene {
  const world = new Group();
  scene.add(world);

  // Pad: a big plane at three-y=0 (sim z=0 -> three y=0).
  const pad = new Mesh(
    new PlaneGeometry(120, 120),
    new MeshStandardMaterial({ color: 0x2a2a30, roughness: 0.9 })
  );
  pad.rotation.x = -Math.PI / 2; // lay flat (XZ)
  pad.receiveShadow = true;
  world.add(pad);

  world.add(new GridHelper(120, 24, 0x444455, 0x222233));

  // Booster stand-in: a capsule ~= KESTREL-9 (47.7 m tall, 3.66 m dia).
  // three units == meters. The capsule's local +Y is its long axis; the sim body
  // +Z axis (toward interstage) maps to three +Y after conversion, so the mesh's
  // native orientation already matches a vertical booster at identity attitude.
  const booster = new Mesh(
    new CapsuleGeometry(1.83, 47.7 - 2 * 1.83, 8, 24),
    new MeshStandardMaterial({ color: 0xd8d8dc, roughness: 0.6, metalness: 0.1 })
  );
  booster.castShadow = true;
  // The capsule origin is its center; the sim reports the base-plane center. Push
  // the mesh up by half its length so the reported point sits at the base.
  const boosterPivot = new Group();
  booster.position.y = 47.7 / 2;
  boosterPivot.add(booster);
  world.add(boosterPivot);

  // a tiny axis gnomon at the base so orientation is legible while debugging
  const gnomon = new AxesHelper(6);
  boosterPivot.add(gnomon);

  // reference "north" marker (sim +Y -> three -Z) so we can eyeball the frame
  const north = new Mesh(
    new PlaneGeometry(2, 2),
    new MeshBasicMaterial({ color: 0x33cc66 })
  );
  north.position.set(0, 0.05, -50); // three -Z == sim north
  north.rotation.x = -Math.PI / 2;
  world.add(north);

  // lighting (M3 only — M7 replaces with sun cascade + plume light)
  const sun = new DirectionalLight(0xffffff, 2.0);
  sun.position.set(40, 80, 30);
  sun.castShadow = true;
  scene.add(sun);
  scene.add(new AmbientLight(0x334455, 0.6));

  const _p = new Vector3();
  const _q = new Quaternion();

  return {
    world,
    booster,
    update(s: InterpSample) {
      // s.r / s.q are SIM frame; convert HERE (the only place — via frame.ts).
      simToThreePosition(s.r.x, s.r.y, s.r.z, _p);
      simToThreeQuaternion(s.q.x, s.q.y, s.q.z, s.q.w, _q);
      boosterPivot.position.copy(_p);
      boosterPivot.quaternion.copy(_q);
    },
  };
}
