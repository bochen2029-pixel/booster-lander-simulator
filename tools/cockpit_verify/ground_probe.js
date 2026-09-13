(async () => {
  // grazing-angle ground probe: eye 8 m up, 26 m out, looking at a point 5 m up at the origin.
  // pts: bottom-centre (ground ~6 m from the eye), mid-left ground, and a sky pixel for reference.
  const eye = [18, 8, 18], tgt = [0, 5, 0];
  const pts = [[160, 170], [60, 150], [260, 150], [160, 40]];
  const land = __doc.world.children[0];
  const ground = land.children[0], pad = land.children[1];
  const out = {};
  const probe = async (label) => { out[label] = await __shotProbe(eye, tgt, pts); };
  await probe('baseline');
  const k = __lights.key.intensity; __lights.key.intensity = 0; await probe('sun_off'); __lights.key.intensity = k;
  const e = ground.material.envMapIntensity; ground.material.envMapIntensity = 0; ground.material.needsUpdate = true; await probe('ibl_off'); ground.material.envMapIntensity = e; ground.material.needsUpdate = true;
  const h = __lights.hemi.intensity; __lights.hemi.intensity = 0; await probe('hemi_off'); __lights.hemi.intensity = h;
  const f = __scene.fog; __scene.fog = null; await probe('fog_off'); __scene.fog = f;
  const bg = __scene.background; __scene.background = null; await probe('bg_off'); __scene.background = bg;
  const ei = __scene.environmentIntensity; __scene.environmentIntensity = 0; await probe('scene_env_off'); __scene.environmentIntensity = ei;
  out.material = { rough: ground.material.roughness, metal: ground.material.metalness, env: ground.material.envMapIntensity, color: ground.material.color.getHexString(), type: ground.material.type, fogNear: __scene.fog && __scene.fog.near, fogFar: __scene.fog && __scene.fog.far, envInt: __scene.environmentIntensity };
  return JSON.stringify(out);
})();
