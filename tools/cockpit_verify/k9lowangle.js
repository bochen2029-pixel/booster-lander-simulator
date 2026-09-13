(async () => {
  // LIVE-CAMERA low-angle test: drive the FREE_ORBIT rig with synthetic pointer/wheel events
  // (the director is not a global) down to a grazing elevation and in to 40 m, then capture the
  // live camera (__shotHDR) — the same RT path the posed shots use, but the live camera.
  const tag = window.__k9tag || 'v';
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  let t0 = performance.now();
  while (((__telem() || {}).phase || 0) < 5 && performance.now() - t0 < 120000) await sleep(200);
  await sleep(1500);
  __cam('FREE_ORBIT');
  const canvas = document.querySelector('canvas');
  const r = canvas.getBoundingClientRect();
  const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
  canvas.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, clientX: cx, clientY: cy, pointerId: 1 }));
  // el: 0.08 -> ~0.02 (dy = -20 px * 0.003); az unchanged
  window.dispatchEvent(new PointerEvent('pointermove', { bubbles: true, clientX: cx, clientY: cy - 20, movementX: 0, movementY: -20, pointerId: 1 }));
  window.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 1 }));
  // zoom: 150 m -> 40 m (clamp)
  canvas.dispatchEvent(new WheelEvent('wheel', { bubbles: true, deltaY: -1200 }));
  await sleep(2500);
  const shots = {};
  const t = __telem();
  const url = await __shotHDR(1280, 720, 0.85, 1280);
  await fetch('/__cap?name=' + tag + '_lowlive', { method: 'POST', body: url });
  shots.lowlive = { t: +t.t.toFixed(1), phase: t.phase, cam: t.cam };
  // and the SAME view through the posed path for the A/B: eye = live camera position
  const cam = __renderer.xr && __renderer.xr.getCamera ? null : null;
  return JSON.stringify(shots);
})();
