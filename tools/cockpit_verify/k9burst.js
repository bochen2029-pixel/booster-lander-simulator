(async () => {
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const shots = [];
  const live = async (name) => {
    const t = __telem();
    const url = await __shotHDR(1280, 720, 0.85, 1280);
    await fetch('/__cap?name=' + name, { method: 'POST', body: url });
    shots.push({ name, t: +t.t.toFixed(1), phase: t.phase, alt: +t.altM.toFixed(1), thr: +t.throttleAct.toFixed(2), cam: t.cam, margin: +t.imu.marginDeg.toFixed(1) });
  };
  // wait for LANDING_BURN (phase 4), up to 90 s
  let t0 = performance.now();
  while ((__telem() || {}).phase < 4 && performance.now() - t0 < 90000) await sleep(250);
  __cam('CHASE');
  await sleep(300);
  await live('k9_burn_chase');
  __cam('FREE_ORBIT');
  await sleep(300);
  await live('k9_burn_orbit');
  __cam('PAD_LONG_LENS');
  await sleep(300);
  await live('k9_burn_pad');
  // wait for the ground: phase >= 5 (touchdown/settling/landed or a failure state), up to 60 s
  t0 = performance.now();
  while ((__telem() || {}).phase < 5 && performance.now() - t0 < 60000) await sleep(250);
  __cam('PAD_LONG_LENS');
  await sleep(300);
  await live('k9_td_pad');
  __cam('FREE_ORBIT');
  await sleep(300);
  await live('k9_td_orbit');
  return JSON.stringify(shots);
})();
