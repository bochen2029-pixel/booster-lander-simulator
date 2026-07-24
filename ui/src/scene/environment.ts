// environment.ts — physical (Preetham) sky + image-based lighting (IBL). The single biggest
// realism lever: IBL gives every PBR metal real specular reflections + sky/ground ambient
// (kills the matte-plastic look), and the SkyMesh replaces the flat two-tone background with a
// scattered gradient + sun + horizon glow. WebGPU-native (SkyMesh is a TSL NodeMaterial; IBL is
// baked with PMREMGenerator). Renderer-side only; never crosses the telemetry boundary.

import { PMREMGenerator, Scene, Vector3 } from "three/webgpu";
import type { WebGPURenderer } from "three/webgpu";
import { SkyMesh } from "three/addons/objects/SkyMesh.js";

export interface SkyEnv {
  sky: SkyMesh;
  setSun(x: number, y: number, z: number): void;
  setVisible(v: boolean): void;
  setEnvIntensity(v: number): void;
}

const SKY = { turbidity: 2.8, rayleigh: 1.9, mieCoefficient: 0.005, mieDirectionalG: 0.85 };
const SUN_DIST = 400_000;

export function installSkyAndIBL(scene: Scene, renderer: WebGPURenderer, sunDir: Vector3): SkyEnv {
  const sunPos = sunDir.clone().normalize().multiplyScalar(SUN_DIST);

  const sky = new SkyMesh();
  sky.scale.setScalar(450_000);
  sky.turbidity.value = SKY.turbidity;
  sky.rayleigh.value = SKY.rayleigh;
  sky.mieCoefficient.value = SKY.mieCoefficient;
  sky.mieDirectionalG.value = SKY.mieDirectionalG;
  sky.sunPosition.value.copy(sunPos);
  (sky.material as { fog?: boolean }).fog = false;
  sky.material.depthWrite = false;
  scene.add(sky);

  const pmrem = new PMREMGenerator(renderer);
  const envScene = new Scene();
  const envSky = new SkyMesh();
  envSky.scale.setScalar(450_000);
  envSky.turbidity.value = SKY.turbidity;
  envSky.rayleigh.value = SKY.rayleigh;
  envSky.mieCoefficient.value = SKY.mieCoefficient;
  envSky.mieDirectionalG.value = SKY.mieDirectionalG;
  envSky.sunPosition.value.copy(sunPos);
  envScene.add(envSky);

  function bake(): void {
    try {
      const rt = pmrem.fromScene(envScene as unknown as Scene, 0.0);
      scene.environment = rt.texture;
    } catch (e) {
      console.warn("[env] PMREM bake deferred:", e);
    }
  }
  bake();
  // Modest IBL: enough sky reflection to make the metal + WATER read, without flooding
  // horizontal surfaces to white — the physical sky env is very bright in absolute terms.
  (scene as { environmentIntensity?: number }).environmentIntensity = 0.42;

  return {
    sky,
    setSun(x, y, z) {
      const p = new Vector3(x, y, z).normalize().multiplyScalar(SUN_DIST);
      sky.sunPosition.value.copy(p);
      envSky.sunPosition.value.copy(p);
      bake();
    },
    setVisible(v) { sky.visible = v; },
    setEnvIntensity(v) { (scene as { environmentIntensity?: number }).environmentIntensity = v; },
  };
}
