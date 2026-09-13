(async () => {
  const land = __doc.world.children[0];
  const ground = land.children[0], pad = land.children[1];
  const names = __doc.world.children.map((c, i) => i + ':' + (c.name || c.type) + (c.visible ? '' : '(hidden)'));
  const out = { worldPos: __doc.world.position.toArray(), landVisible: land.visible, groundVisible: ground.visible, children: names };
  // straight down onto the pad from 60 m: centre pixel = pad, corner = ground
  out.down = await __shotProbe([0.01, 60, 0.01], [0, 0, 0], [[160, 90], [20, 20], [300, 160]]);
  // grazing pose again, ground hidden
  ground.visible = false;
  out.graze_no_ground = await __shotProbe([18, 8, 18], [0, 5, 0], [[160, 170], [60, 150], [160, 40]]);
  ground.visible = true;
  // grazing pose, pad hidden too, and land hidden
  land.visible = false;
  out.graze_no_land = await __shotProbe([18, 8, 18], [0, 5, 0], [[160, 170], [60, 150], [160, 40]]);
  land.visible = true;
  // the earth globe: find it
  const earth = __doc.world.children.find((c) => c.name && /earth/i.test(c.name));
  out.earth = earth ? { name: earth.name, visible: earth.visible, n: earth.children.length } : 'not-named';
  const globes = [];
  __doc.world.traverse((o) => { if (o.isMesh && o.geometry && o.geometry.type === 'SphereGeometry' && o.geometry.parameters.radius > 10000) globes.push({ name: o.name || o.parent.name || '?', r: o.geometry.parameters.radius, visible: o.visible, parentVisible: o.parent.visible, y: o.getWorldPosition(new o.position.constructor()).y, mat: o.material.type }); });
  out.globes = globes;
  return JSON.stringify(out);
})();
