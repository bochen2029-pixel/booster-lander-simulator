// fdai.ts — the FDAI (Flight Director Attitude Indicator): an Apollo-style 8-ball driven by the
// streamed attitude quaternion + body rates through the IMU kernel (imu.ts). PLAN.md Phase 1.2:
// "Apollo's look, not Apollo's mechanics" — display-only, additive, the first instrument in this
// cockpit that makes an attitude departure visible (today a LOC draw is a line in a summary).
//
// THE BALL is a sphere painted in the stable-member frame and viewed from the case: the viewer sits
// on the case +X axis (the outer-gimbal / vehicle long axis) with case +Z up the screen and case +Y
// to the right, so a point painted at p_SM appears at p_case = Rᵀ·p_SM with R = Rx(OGA)·Rz(MGA)·Ry(IGA)
// the case→SM rotation. Painted features (all in SM coordinates):
//   • the split circle p.z = 0 (upper hemisphere dark, lower light) — horizontal through the
//     centre at 0/0/0, tilts with roll, slides up/down with pitch (IGA);
//   • a 30° latitude/longitude grid about the SM Z (middle-gimbal) axis;
//   • the GIMBAL-LOCK caps around ±SM Y — the points that face the viewer when |MGA| → 90°. At
//     zero attitude they sit at the left/right limb, exactly where the Apollo ball's red zones are;
//     a yaw (MGA) slides the ball sideways toward them, the margin bar counts it down;
//   • the zero marker at +SM X (upright, heading 0).
// The pure parts (gimbalRotation, ballPixel) are exported and unit-tested; the DOM part is not.
import { Quaternion, type Vector3 } from "three";
import { IMU_FLAG_LOST, IMU_FLAG_ON, Phase, type TlmFrame } from "../net/decode";
import {
  LOCK_DEG,
  WARN_DEG,
  alignRefsmmat,
  computeImu,
  landingSiteRefsmmat,
  makeImuReadout,
  type ImuReadout,
} from "./imu";

const D2R = Math.PI / 180;

/** Row-major 3×3 of R = Rx(o)·Rz(m)·Ry(i) (angles in radians). Same factorization as imu.ts. */
export function gimbalRotation(o: number, m: number, i: number, out: Float64Array): Float64Array {
  const co = Math.cos(o), so = Math.sin(o);
  const cm = Math.cos(m), sm = Math.sin(m);
  const ci = Math.cos(i), si = Math.sin(i);
  out[0] = cm * ci; out[1] = -sm; out[2] = cm * si;
  out[3] = co * sm * ci + so * si; out[4] = co * cm; out[5] = co * sm * si - so * ci;
  out[6] = so * sm * ci - co * si; out[7] = so * cm; out[8] = so * sm * si + co * ci;
  return out;
}

/** Ball paint, RGB 0..255. */
export const BALL_PAINT = {
  upper: [36, 48, 62] as const,
  lower: [146, 124, 100] as const,
  split: [232, 238, 244] as const,
  grid: [176, 190, 202] as const,
  lock: [214, 48, 40] as const,
  warn: [232, 160, 52] as const,
  zero: [255, 176, 64] as const,
};
const COS_LOCK = Math.cos(LOCK_DEG * D2R);
const COS_WARN_LO = Math.cos((WARN_DEG + 0.7) * D2R);
const COS_WARN_HI = Math.cos((WARN_DEG - 0.7) * D2R);
const COS_ZERO = Math.cos(3.2 * D2R);
const LINE_DEG = 0.75; // half-width of a painted line, in degrees on the sphere

function nearMultiple(deg: number, step: number, halfWidth: number, skipZero: boolean): boolean {
  const k = Math.round(deg / step);
  if (skipZero && k === 0) return false;
  return Math.abs(deg - k * step) < halfWidth;
}

/**
 * Colour of the ball at screen coordinates (u right, v up, unit disc) for the case→SM rotation R
 * (row-major, from gimbalRotation). Writes RGB at out[o..o+2] and returns false outside the disc.
 * p_case = (d, u, v) with d = the depth toward the viewer; p_SM = R · p_case.
 */
export function ballPixel(u: number, v: number, R: Float64Array, out: Uint8ClampedArray | number[], o: number): boolean {
  const rr = u * u + v * v;
  if (rr > 1) return false;
  const d = Math.sqrt(1 - rr);
  const px = R[0] * d + R[1] * u + R[2] * v;
  const py = R[3] * d + R[4] * u + R[5] * v;
  const pz = R[6] * d + R[7] * u + R[8] * v;

  let c: readonly [number, number, number];
  const ay = Math.abs(py);
  if (px > COS_ZERO) c = BALL_PAINT.zero;
  else if (ay > COS_LOCK) c = BALL_PAINT.lock;
  else if (ay > COS_WARN_LO && ay < COS_WARN_HI) c = BALL_PAINT.warn;
  else {
    const latDeg = Math.asin(pz < -1 ? -1 : pz > 1 ? 1 : pz) / D2R;
    const lonDeg = Math.atan2(py, px) / D2R;
    const cosLat = Math.cos(latDeg * D2R);
    if (Math.abs(latDeg) < LINE_DEG * 1.6) c = BALL_PAINT.split;
    else if (nearMultiple(latDeg, 30, LINE_DEG, true)) c = BALL_PAINT.grid;
    else if (cosLat > 0.05 && nearMultiple(lonDeg, 30, LINE_DEG / cosLat, false)) c = BALL_PAINT.grid;
    else c = pz >= 0 ? BALL_PAINT.upper : BALL_PAINT.lower;
  }
  // sphere shading: darker toward the limb, a little lift toward the upper-left
  const shade = 0.42 + 0.58 * d + 0.08 * (v - u) * (1 - d);
  out[o] = c[0] * shade;
  out[o + 1] = c[1] * shade;
  out[o + 2] = c[2] * shade;
  return true;
}

/** Paint the whole disc into an RGBA buffer of size×size pixels (outside the disc: transparent). */
export function renderBall(rgba: Uint8ClampedArray, size: number, R: Float64Array): void {
  const c = size / 2;
  const r = c - 0.5;
  let k = 0;
  for (let y = 0; y < size; y++) {
    const v = (c - (y + 0.5)) / r;
    for (let x = 0; x < size; x++, k += 4) {
      const u = (x + 0.5 - c) / r;
      if (ballPixel(u, v, R, rgba, k)) rgba[k + 3] = 255;
      else rgba[k + 3] = 0;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// DOM instrument
// ---------------------------------------------------------------------------------------------

const CSS = `
.fdai-root {
  --nominal: #7fe0a8; --caution: #ffcf6b; --abort: #ff6b6b;
  --dim: #5a7080; --bg: rgba(6,10,14,.62); --line: rgba(120,150,175,.25);
  position: fixed; right: 14px; top: 236px; width: 236px; z-index: 22; pointer-events: none;
  font: 11px/1.3 ui-monospace, "SF Mono", Menlo, monospace; color: #cfe3f0;
  background: var(--bg); border: 1px solid var(--line); border-radius: 4px; padding: 6px 8px;
  box-sizing: border-box;
}
.fdai-head { display: flex; justify-content: space-between; align-items: baseline; color: var(--dim); font-size: 10px; letter-spacing: .06em; }
.fdai-ann { font-weight: 700; color: var(--dim); }
.fdai-ann.warn { color: var(--caution); }
.fdai-ann.lock, .fdai-ann.noatt, .fdai-ann.loc { color: var(--abort); text-shadow: 0 0 8px rgba(255,107,107,.6); }
.fdai-mid { display: flex; gap: 8px; align-items: stretch; margin-top: 4px; }
.fdai-ball { width: 180px; height: 180px; display: block; flex: none; }
.fdai-margin { flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px; }
.fdai-margin .label { font-size: 8px; color: var(--dim); text-align: center; line-height: 1.1; letter-spacing: .04em; }
.fdai-margin .track { position: relative; width: 10px; height: 118px; background: rgba(255,255,255,.06); border-radius: 3px; overflow: hidden; }
.fdai-margin .fill { position: absolute; left: 0; right: 0; bottom: 0; height: 100%; background: var(--nominal); }
.fdai-margin .fill.warn { background: var(--caution); }
.fdai-margin .fill.lock { background: var(--abort); }
.fdai-margin .tick { position: absolute; left: 0; right: 0; height: 1px; background: rgba(255,255,255,.35); }
.fdai-margin .val { font-size: 12px; color: #dcecf6; }
.fdai-grid { display: grid; grid-template-columns: auto 1fr auto 1fr; gap: 1px 8px; margin-top: 5px; }
.fdai-grid .k { color: var(--dim); }
.fdai-grid .v { text-align: right; color: #dcecf6; font-variant-numeric: tabular-nums; }
.fdai-grid .v.hot { color: var(--abort); }
.fdai-btns { display: flex; gap: 6px; margin-top: 5px; pointer-events: auto; }
.fdai-btns button {
  flex: 1; font: inherit; font-size: 10px; letter-spacing: .06em; color: #cfe3f0;
  background: rgba(255,255,255,.06); border: 1px solid var(--line); border-radius: 3px; padding: 3px 0; cursor: pointer;
}
.fdai-btns button:hover { background: rgba(255,255,255,.14); }
.fdai-btns button.on { color: var(--nominal); border-color: var(--nominal); }
`;

export interface FdaiHandle {
  root: HTMLElement;
  /** the latest kernel output (read by the DEV __telem hook) */
  readonly readout: ImuReadout;
  /** which REFSMMAT is in force */
  readonly reference: "PAD" | "ALIGN";
  /** Per frame with data: sim body→world quaternion, body rates [rad/s], the newer frame. */
  update(qBody: Quaternion, wBody: Vector3, frame: TlmFrame): void;
  /** Per animation frame regardless of data: drives the NO ATT (stale stream) annunciator. */
  tick(nowMs: number): void;
  /** REFSMMAT := current case attitude (all gimbal angles → 0 now). */
  align(): void;
  /** REFSMMAT := the landing-site frame (SM X up, Y east, Z north). */
  pad(): void;
  dispose(): void;
}

const BALL_PX = 180;
const RATE_FULL_SCALE = 10; // deg/s at the end of a rate needle
const STALE_MS = 1500;

export function installFdai(): FdaiHandle {
  const style = document.createElement("style");
  style.textContent = CSS;
  document.head.appendChild(style);

  const root = document.createElement("div");
  root.className = "fdai-root";

  const head = document.createElement("div");
  head.className = "fdai-head";
  const title = document.createElement("span");
  title.textContent = "IMU · FDAI";
  const ann = document.createElement("span");
  ann.className = "fdai-ann";
  ann.textContent = "NO ATT";
  ann.classList.add("noatt");
  head.append(title, ann);
  root.appendChild(head);

  const mid = document.createElement("div");
  mid.className = "fdai-mid";
  const canvas = document.createElement("canvas");
  canvas.className = "fdai-ball";
  const dpr = Math.min(1.5, typeof devicePixelRatio === "number" ? devicePixelRatio : 1);
  const px = Math.round(BALL_PX * dpr);
  canvas.width = px;
  canvas.height = px;
  const ctx = canvas.getContext("2d");
  mid.appendChild(canvas);

  const marginCol = document.createElement("div");
  marginCol.className = "fdai-margin";
  const mLabel = document.createElement("div");
  mLabel.className = "label";
  mLabel.textContent = "MGA\nMARGIN";
  mLabel.style.whiteSpace = "pre";
  const track = document.createElement("div");
  track.className = "track";
  const fill = document.createElement("div");
  fill.className = "fill";
  track.appendChild(fill);
  for (const deg of [LOCK_DEG, WARN_DEG]) {
    const t = document.createElement("div");
    t.className = "tick";
    t.style.bottom = `${(deg / 90) * 100}%`;
    track.appendChild(t);
  }
  const mVal = document.createElement("div");
  mVal.className = "val";
  mVal.textContent = "90.0°";
  marginCol.append(mLabel, track, mVal);
  mid.appendChild(marginCol);
  root.appendChild(mid);

  const grid = document.createElement("div");
  grid.className = "fdai-grid";
  const cells: Record<string, HTMLElement> = {};
  for (const key of ["OGA", "ROLL°/s", "MGA", "PTCH°/s", "IGA", "YAW°/s", "TILT", "REF", "SRC", "ERR"]) {
    const k = document.createElement("span");
    k.className = "k";
    k.textContent = key;
    const v = document.createElement("span");
    v.className = "v";
    v.textContent = "—";
    grid.append(k, v);
    cells[key] = v;
  }
  root.appendChild(grid);

  const btns = document.createElement("div");
  btns.className = "fdai-btns";
  const bAlign = document.createElement("button");
  bAlign.textContent = "ALIGN";
  bAlign.title = "REFSMMAT := current attitude (all gimbal angles read zero now)";
  const bPad = document.createElement("button");
  bPad.textContent = "PAD REF";
  bPad.title = "REFSMMAT := landing-site frame (SM X up, Y east, Z north)";
  bPad.classList.add("on");
  btns.append(bAlign, bPad);
  root.appendChild(btns);

  document.body.appendChild(root);

  // --- state ---------------------------------------------------------------------------------
  const readout = makeImuReadout();
  const qRef = landingSiteRefsmmat();
  let reference: "PAD" | "ALIGN" = "PAD";
  let lastQ: Quaternion | null = null;
  let lastDataMs = -Infinity;
  let stale = true;
  let platformOn = false; // v5: the plant's gimbaled platform is the attitude source
  let platformLost = false;
  let platformErrDeg = 0;
  let platformMarginDeg = 90;
  const _qBelief = new Quaternion();
  let phaseLoc = false;
  const R = new Float64Array(9);
  const rgba = new Uint8ClampedArray(px * px * 4);
  const img = ctx ? ctx.createImageData(px, px) : null;
  let lastDrawn = { o: NaN, m: NaN, i: NaN, rr: NaN, rp: NaN, ry: NaN, ann: "" };

  function drawOverlay(c: CanvasRenderingContext2D, r: ImuReadout): void {
    const cx = px / 2, cy = px / 2, rad = px / 2 - 0.5 * dpr;
    c.save();
    c.scale(1, 1);
    // bezel + roll scale
    c.lineWidth = 2 * dpr;
    c.strokeStyle = "rgba(200,214,224,.55)";
    c.beginPath();
    c.arc(cx, cy, rad - 1 * dpr, 0, Math.PI * 2);
    c.stroke();
    c.strokeStyle = "rgba(200,214,224,.7)";
    c.lineWidth = 1 * dpr;
    for (let k = 0; k < 12; k++) {
      const a = (k * 30 - 90) * D2R;
      const len = k % 3 === 0 ? 7 : 4;
      c.beginPath();
      c.moveTo(cx + Math.cos(a) * (rad - 2 * dpr), cy + Math.sin(a) * (rad - 2 * dpr));
      c.lineTo(cx + Math.cos(a) * (rad - (2 + len) * dpr), cy + Math.sin(a) * (rad - (2 + len) * dpr));
      c.stroke();
    }
    // roll pointer (OGA) on the bezel — the ball already rotates with roll; this reads it
    const ra = (-90 + r.oga) * D2R;
    c.fillStyle = "#ffb040";
    c.beginPath();
    c.moveTo(cx + Math.cos(ra) * (rad - 3 * dpr), cy + Math.sin(ra) * (rad - 3 * dpr));
    c.lineTo(cx + Math.cos(ra + 0.06) * (rad - 12 * dpr), cy + Math.sin(ra + 0.06) * (rad - 12 * dpr));
    c.lineTo(cx + Math.cos(ra - 0.06) * (rad - 12 * dpr), cy + Math.sin(ra - 0.06) * (rad - 12 * dpr));
    c.closePath();
    c.fill();
    // fixed vehicle symbol (the "wings" + centre dot), case-fixed
    c.strokeStyle = "#ffb040";
    c.lineWidth = 2 * dpr;
    c.beginPath();
    c.moveTo(cx - 34 * dpr, cy); c.lineTo(cx - 12 * dpr, cy); c.lineTo(cx - 6 * dpr, cy + 6 * dpr);
    c.lineTo(cx, cy); c.lineTo(cx + 6 * dpr, cy + 6 * dpr); c.lineTo(cx + 12 * dpr, cy); c.lineTo(cx + 34 * dpr, cy);
    c.stroke();
    c.fillStyle = "#ffb040";
    c.beginPath();
    c.arc(cx, cy, 2 * dpr, 0, Math.PI * 2);
    c.fill();
    // rate needles: roll (top, horizontal), yaw (bottom, horizontal), pitch (right, vertical)
    const needle = (val: number, x0: number, y0: number, x1: number, y1: number) => {
      const f = Math.max(-1, Math.min(1, val / RATE_FULL_SCALE));
      const over = Math.abs(val) > RATE_FULL_SCALE;
      c.strokeStyle = over ? "#ff6b6b" : "#dcecf6";
      c.lineWidth = 2 * dpr;
      c.beginPath();
      c.moveTo(x0, y0);
      c.lineTo(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f);
      c.stroke();
    };
    const span = rad * 0.55;
    needle(r.rateRoll, cx, cy - rad + 14 * dpr, cx + span, cy - rad + 14 * dpr);
    needle(r.rateYaw, cx, cy + rad - 14 * dpr, cx + span, cy + rad - 14 * dpr);
    needle(r.ratePitch, cx + rad - 14 * dpr, cy, cx + rad - 14 * dpr, cy - span);
    c.restore();
  }

  function draw(): void {
    if (!ctx || !img) return;
    const r = readout;
    const annText = ann.textContent ?? "";
    const same =
      Math.abs(r.oga - lastDrawn.o) < 0.02 && Math.abs(r.mga - lastDrawn.m) < 0.02 && Math.abs(r.iga - lastDrawn.i) < 0.02 &&
      Math.abs(r.rateRoll - lastDrawn.rr) < 0.05 && Math.abs(r.ratePitch - lastDrawn.rp) < 0.05 &&
      Math.abs(r.rateYaw - lastDrawn.ry) < 0.05 && annText === lastDrawn.ann;
    if (same) return;
    lastDrawn = { o: r.oga, m: r.mga, i: r.iga, rr: r.rateRoll, rp: r.ratePitch, ry: r.rateYaw, ann: annText };
    gimbalRotation(r.oga * D2R, r.mga * D2R, r.iga * D2R, R);
    renderBall(rgba, px, R);
    img.data.set(rgba);
    ctx.clearRect(0, 0, px, px);
    ctx.putImageData(img, 0, 0);
    if (!r.valid || stale) {
      ctx.fillStyle = "rgba(6,10,14,.55)";
      ctx.fillRect(0, 0, px, px);
    }
    drawOverlay(ctx, r);
  }

  function setAnnunciator(): void {
    let text = "OK";
    let cls = "";
    if (stale || !readout.valid || platformLost) { text = "NO ATT"; cls = "noatt"; }
    else if (phaseLoc) { text = "LOC"; cls = "loc"; }
    else if (readout.lock === "LOCK") { text = "GMBL LOCK"; cls = "lock"; }
    else if (readout.lock === "WARN") { text = "LOCK CAUTION"; cls = "warn"; }
    ann.textContent = text;
    ann.className = "fdai-ann" + (cls ? " " + cls : "");
  }

  function refresh(): void {
    setAnnunciator();
    const r = readout;
    const f2 = (x: number) => (x >= 0 ? "+" : "") + x.toFixed(2);
    cells["OGA"]!.textContent = f2(r.oga);
    cells["MGA"]!.textContent = f2(r.mga);
    cells["IGA"]!.textContent = f2(r.iga);
    cells["TILT"]!.textContent = r.tiltDeg.toFixed(1) + "°";
    cells["REF"]!.textContent = reference;
    cells["SRC"]!.textContent = platformOn ? "PLATFORM" : "TRUTH";
    const errEl = cells["ERR"]!;
    errEl.textContent = platformErrDeg.toFixed(2) + "°";
    errEl.classList.toggle("hot", platformErrDeg > 3);
    for (const [key, val] of [["ROLL°/s", r.rateRoll], ["PTCH°/s", r.ratePitch], ["YAW°/s", r.rateYaw]] as const) {
      const el = cells[key]!;
      el.textContent = f2(val);
      el.classList.toggle("hot", Math.abs(val) > RATE_FULL_SCALE);
    }
    mVal.textContent = r.marginDeg.toFixed(1) + "°";
    fill.style.height = `${Math.max(0, Math.min(100, (r.marginDeg / 90) * 100))}%`;
    fill.className = "fill" + (r.lock === "LOCK" ? " lock" : r.lock === "WARN" ? " warn" : "");
    draw();
  }

  bAlign.addEventListener("click", () => api.align());
  bPad.addEventListener("click", () => api.pad());

  const api: FdaiHandle = {
    root,
    get readout() {
      return readout;
    },
    get reference() {
      return reference;
    },
    update(qBody, wBody, frame) {
      lastQ = qBody;
      lastDataMs = performance.now();
      stale = false;
      phaseLoc = frame.phase === Phase.LOC;
      // v5 (D-059): when the plant flies a gimbaled platform, the ball shows what the flight
      // computer BELIEVES (quat_meas) and NO ATT / the margin come from the plant; otherwise the
      // display-only kernel runs on truth as before.
      platformOn = (frame.imuFlags & IMU_FLAG_ON) !== 0;
      platformLost = platformOn && (frame.imuFlags & IMU_FLAG_LOST) !== 0;
      platformErrDeg = platformOn ? frame.imuErr * (180 / Math.PI) : 0;
      if (platformOn) {
        _qBelief.set(frame.quatMeas[0], frame.quatMeas[1], frame.quatMeas[2], frame.quatMeas[3]);
        computeImu(_qBelief, wBody, qRef, readout);
        platformMarginDeg = frame.imuMargin * (180 / Math.PI);
        readout.marginDeg = platformMarginDeg;
        readout.lock = platformMarginDeg <= 5 ? "LOCK" : platformMarginDeg <= 20 ? "WARN" : "OK";
      } else {
        computeImu(qBody, wBody, qRef, readout);
      }
      refresh();
    },
    tick(nowMs) {
      const wasStale = stale;
      stale = nowMs - lastDataMs > STALE_MS;
      if (stale !== wasStale) refresh();
    },
    align() {
      if (!lastQ) return;
      alignRefsmmat(lastQ, qRef);
      reference = "ALIGN";
      bAlign.classList.add("on");
      bPad.classList.remove("on");
      refresh();
    },
    pad() {
      landingSiteRefsmmat(qRef);
      reference = "PAD";
      bPad.classList.add("on");
      bAlign.classList.remove("on");
      refresh();
    },
    dispose() {
      root.remove();
      style.remove();
    },
  };
  refresh();
  return api;
}
