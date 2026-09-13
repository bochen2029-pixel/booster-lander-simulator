(async () => {
  const name = window.__k9name || 'k9_live';
  const t = __telem();
  const url = await __shotHDR(1280, 720, 0.85, 1280);
  const r = await fetch('/__cap?name=' + name, { method: 'POST', body: url });
  return JSON.stringify({ name, status: r.status, t: +t.t.toFixed(1), phase: t.phase, alt: +t.altM.toFixed(1), thr: +t.throttleAct.toFixed(2), mach: +t.mach.toFixed(2), cam: t.cam, imu: { oga: +t.imu.oga.toFixed(1), mga: +t.imu.mga.toFixed(1), iga: +t.imu.iga.toFixed(1), margin: +t.imu.marginDeg.toFixed(1) } });
})();
