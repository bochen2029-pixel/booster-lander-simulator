(async () => {
  // v5 end-to-end: wait for the plant's platform to report LOST (imuFlags bit 1), then capture the
  // FDAI panel text + a live-camera HDR frame, and again ~6 s later (the belief diverging).
  const tag = window.__k9tag || 'v';
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const fd = () => (document.querySelector('.fdai-root') || {}).innerText;
  const snap = (label) => { const t = __telem(); return { label, t: +t.t.toFixed(1), phase: t.phase, flags: t.imuFlags, err: +t.imuErrDeg.toFixed(2), margin: +t.imuMarginDeg.toFixed(1), fdai: (fd() || '').replace(/\n/g, ' ') }; };
  const out = [];
  let t0 = performance.now();
  while (!__telem() && performance.now() - t0 < 60000) await sleep(200);
  out.push(snap('first'));
  // wait for ON (bit0) then LOST (bit1), up to 200 s of flight
  t0 = performance.now();
  while (performance.now() - t0 < 200000) {
    const t = __telem();
    if (t && (t.imuFlags & 2)) break;
    if (t && t.phase >= 7) break; // landed/failed without a loss
    await sleep(150);
  }
  out.push(snap('at_loss'));
  __cam('FREE_ORBIT');
  await sleep(400);
  let url = await __shotHDR(1280, 720, 0.85, 1280);
  await fetch('/__cap?name=' + tag + '_noatt', { method: 'POST', body: url });
  await sleep(6000);
  out.push(snap('plus6s'));
  url = await __shotHDR(1280, 720, 0.85, 1280);
  await fetch('/__cap?name=' + tag + '_noatt6', { method: 'POST', body: url });
  return JSON.stringify(out);
})();
