(async () => {
  const tag = window.__k9tag || 'v';
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const out = [];
  const posed = async (name, o) => {
    const t = __telem();
    if (!t) { out.push({ name, err: 'no-telem' }); return; }
    const wp = __doc.world.position;
    const v = t.vel, lead = o.lead === undefined ? 1.2 : o.lead;
    const px = t.pos[0] + v[0] * lead, py = t.pos[1] + v[1] * lead, pz = t.pos[2] + v[2] * lead;
    const com = [px + wp.x, pz + wp.y, -py + wp.z];
    const base = [com[0], com[1] - t.comZ, com[2]];
    const tgt = [base[0], base[1] + o.h, base[2]];
    const eye = [tgt[0] + o.d * Math.cos(o.el) * Math.cos(o.az), tgt[1] + o.d * Math.sin(o.el), tgt[2] + o.d * Math.cos(o.el) * Math.sin(o.az)];
    const url = await __shotPoseHDR(eye, tgt, o.fov || 40, 1280, 720, 0.85, 1280);
    await fetch('/__cap?name=' + tag + '_' + name, { method: 'POST', body: url });
    out.push({ name, t: +t.t.toFixed(1), ph: t.phase, alt: +t.altM.toFixed(0), thr: +t.throttleAct.toFixed(2), spd: +Math.hypot(v[0], v[1], v[2]).toFixed(0) });
  };
  // wait for telemetry + LANDING_BURN
  let t0 = performance.now();
  while (((__telem() || {}).phase || 0) < 4 && performance.now() - t0 < 120000) await sleep(200);
  // burn: side views with velocity lead, wide enough to survive the lead error
  await posed('burn1', { d: 160, el: 0.1, az: 0.8, h: 20, fov: 30 });
  await posed('burn2', { d: 120, el: 0.15, az: 2.2, h: 15, fov: 30 });
  // final approach: wait until speed < 40 m/s or phase >= 5
  t0 = performance.now();
  while (performance.now() - t0 < 60000) {
    const t = __telem(); if (!t) break;
    const spd = Math.hypot(t.vel[0], t.vel[1], t.vel[2]);
    if (t.phase >= 5 || (t.phase === 4 && spd < 40)) break;
    await sleep(150);
  }
  await posed('final1', { d: 90, el: 0.12, az: 0.9, h: 12, fov: 32, lead: 1.0 });
  await posed('final2', { d: 60, el: 0.08, az: 2.6, h: 6, fov: 32, lead: 1.0 });
  // landed
  t0 = performance.now();
  while (((__telem() || {}).phase || 0) < 5 && performance.now() - t0 < 60000) await sleep(200);
  await sleep(1500);
  await posed('hero', { d: 70, el: 0.18, az: 0.7, h: 22, fov: 40, lead: 0 });
  await posed('base', { d: 26, el: 0.12, az: 1.1, h: 5, fov: 40, lead: 0 });
  await posed('hull', { d: 16, el: 0.05, az: 2.4, h: 30, fov: 35, lead: 0 });
  await posed('octaweb', { d: 16, el: 0.15, az: 0.4, h: 1.0, fov: 45, lead: 0 });
  return JSON.stringify(out);
})();
