// audio_smoke.mts — node smoke: feed live decoded frames from core --serve through
// the PURE audio propagation math and assert sane outputs.
//
// Per the S3 gate: "a node smoke that feeds 500 decoded frames from your --serve
// into the propagation math and asserts sane outputs (delays, gains finite/monotone
// vs range)."
//
// This connects to ws://127.0.0.1:<port> (default 8142), decodes real TLM frames
// with the SAME decode.ts the browser uses, converts sim→three via frame.ts, and
// runs each frame through computePropagation() against a fixed pad-cam listener.
// It asserts every propagation output is finite and that the cross-frame relation
// between slant RANGE and (gain, absorption cutoff) is monotone in the physical
// sense (farther ⇒ never louder, never brighter). Exit 0 = pass, 1 = fail.
//
// Run:  node --experimental-strip-types smoke/audio_smoke.mts [port] [nFrames]
//   (frame.ts pulls in `three` only for the Vector3/Quaternion helper types we do
//    NOT call here — we use the raw-array forms simToThreePosArray, so no three is
//    imported by this smoke's code path.)

import { readFileSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { decodeTlm, frameMagic, TLM_MAGIC, HELLO_MAGIC, EVT_MAGIC, STATS_MAGIC } from "../src/net/decode.ts";
import { simToThreePosArray } from "../src/net/frame.ts";
import { decodeEvt, EVT_SIZE } from "../src/audio/evtDecode.ts";
import {
  computePropagation,
  slantRange,
  spreadingGain,
  absorptionCutoffHz,
  type Vec3,
} from "../src/audio/propagation.ts";

const PORT = Number(process.argv[2] ?? 8142);
const N_TARGET = Number(process.argv[3] ?? 500);
// optional listener override: "x,y,z" (three world coords). Default = pad-cam ~2 km
// back (matches AudioEngine's DEFAULT_LISTENER). Pass e.g. "100,20,100" to sit near
// the pad and exercise the crackle-fade-in reveal as the vehicle descends.
const URL = `ws://127.0.0.1:${PORT}`;

let LISTENER: Vec3 = { x: 1400, y: 40, z: 1400 };
if (process.argv[4]) {
  const [x, y, z] = process.argv[4].split(",").map(Number);
  if ([x, y, z].every(Number.isFinite)) LISTENER = { x, y, z };
}

interface Row {
  seq: number;
  t: number;
  range: number;
  delay: number;
  gain: number;
  cutoff: number;
  crackle: number;
  doppler: number;
  turb: number;
  throttle: number;
  nEng: number;
}

function fail(msg: string): never {
  console.error(`SMOKE FAIL: ${msg}`);
  process.exit(1);
}

function finiteOrFail(name: string, v: number, seq: number): void {
  if (!Number.isFinite(v)) fail(`${name} not finite at seq=${seq}: ${v}`);
}

// ---- golden drift check: the EVT constant inlined in evtDecode.test.ts must match
// the real wire golden file (which vitest can't read without @types/node). We do it
// here, in Node, so a byte drift is still caught by CI-equivalent runs.
(function checkEvtGolden(): void {
  const here = dirname(fileURLToPath(import.meta.url));
  const goldenPath = resolve(here, "../../goldens/protocol/evt.hex");
  try {
    const hex = readFileSync(goldenPath, "utf8").trim().replace(/\s+/g, "");
    const n = hex.length / 2;
    const u8 = new Uint8Array(n);
    for (let i = 0; i < n; i++) u8[i] = parseInt(hex.substr(i * 2, 2), 16);
    if (u8.byteLength !== EVT_SIZE) fail(`golden evt.hex is ${u8.byteLength} B, expected ${EVT_SIZE}`);
    const e = decodeEvt(u8.buffer);
    if (!Number.isFinite(e.t)) fail("golden evt.hex decodes to non-finite t");
    console.error(`[smoke] EVT golden OK (code=${e.code} step=${e.step} t=${e.t.toFixed(3)})`);
  } catch (err: any) {
    if (err?.code === "ENOENT") console.error(`[smoke] (evt golden not found at ${goldenPath} — skipping drift check)`);
    else throw err;
  }
})();

const rows: Row[] = [];
let hello = 0;
let evt = 0;
let stats = 0;
let tlm = 0;

const ws = new WebSocket(URL);
ws.binaryType = "arraybuffer";

const timeout = setTimeout(() => fail(`timed out waiting for ${N_TARGET} TLM frames (got ${tlm})`), 30000);

ws.onopen = () => console.error(`[smoke] connected ${URL}`);
ws.onerror = (e: any) => fail(`ws error: ${e?.message ?? e}`);
ws.onclose = () => {
  if (tlm < N_TARGET) fail(`socket closed early (got ${tlm}/${N_TARGET} TLM)`);
};

ws.onmessage = (ev: MessageEvent) => {
  const buf = ev.data as ArrayBuffer;
  if (!(buf instanceof ArrayBuffer)) return;
  const magic = frameMagic(buf);
  switch (magic) {
    case HELLO_MAGIC:
      hello++;
      return;
    case EVT_MAGIC:
      evt++;
      return;
    case STATS_MAGIC:
      stats++;
      return;
    case TLM_MAGIC:
      break;
    default:
      return;
  }

  const f = decodeTlm(buf);
  const [sx, sy, sz] = simToThreePosArray(f.r[0], f.r[1], f.r[2]);
  const [vx, vy, vz] = simToThreePosArray(f.v[0], f.v[1], f.v[2]);
  const src: Vec3 = { x: sx, y: sy, z: sz };
  const vel: Vec3 = { x: vx, y: vy, z: vz };

  const p = computePropagation(LISTENER, src, vel, f.t);

  // every propagation output must be finite
  finiteOrFail("range", p.range, f.seq);
  finiteOrFail("delay", p.delay, f.seq);
  finiteOrFail("gain", p.gain, f.seq);
  finiteOrFail("cutoff", p.cutoffHz, f.seq);
  finiteOrFail("crackle", p.crackle, f.seq);
  finiteOrFail("doppler", p.dopplerRatio, f.seq);
  finiteOrFail("turbulence", p.turbulence, f.seq);

  // range-sanity: independently recompute and cross-check against computePropagation
  const rng2 = slantRange(src, LISTENER);
  if (Math.abs(rng2 - p.range) > 1e-3) fail(`range mismatch seq=${f.seq}: ${rng2} vs ${p.range}`);

  // bounded ranges
  if (p.gain < 0 || p.gain > 1) fail(`gain out of [0,1] seq=${f.seq}: ${p.gain}`);
  if (p.crackle < 0 || p.crackle > 1) fail(`crackle out of [0,1] seq=${f.seq}: ${p.crackle}`);
  if (p.dopplerRatio <= 0) fail(`doppler non-positive seq=${f.seq}: ${p.dopplerRatio}`);
  if (p.delay < 0) fail(`negative delay seq=${f.seq}: ${p.delay}`);
  // delay must equal range / 343
  if (Math.abs(p.delay - p.range / 343) > 1e-6) fail(`delay != range/c seq=${f.seq}`);

  rows.push({
    seq: f.seq,
    t: f.t,
    range: p.range,
    delay: p.delay,
    gain: p.gain,
    cutoff: p.cutoffHz,
    crackle: p.crackle,
    doppler: p.dopplerRatio,
    turb: p.turbulence,
    throttle: f.throttleAct,
    nEng: f.nEng,
  });

  if (++tlm >= N_TARGET) {
    clearTimeout(timeout);
    ws.close();
    analyze();
  }
};

function analyze(): void {
  if (rows.length < 2) fail(`too few frames: ${rows.length}`);

  // ---- MONOTONICITY vs RANGE (the physical invariant) ----------------------
  // Sort by slant range and assert gain is non-increasing and absorption cutoff is
  // non-increasing with distance. Because gain/cutoff are PURE functions of range
  // in our model, sorting by range must yield a monotone series (any inversion is a
  // model bug). Use a tiny epsilon for float noise / equal ranges.
  const byRange = [...rows].sort((a, b) => a.range - b.range);
  const EPS = 1e-6;
  for (let i = 1; i < byRange.length; i++) {
    const lo = byRange[i - 1];
    const hi = byRange[i];
    if (hi.gain > lo.gain + EPS) {
      fail(`gain NOT monotone vs range: r=${hi.range.toFixed(1)} gain=${hi.gain} > r=${lo.range.toFixed(1)} gain=${lo.gain}`);
    }
    if (hi.cutoff > lo.cutoff + 1e-3) {
      fail(`absorption cutoff NOT monotone vs range: r=${hi.range.toFixed(1)} cut=${hi.cutoff} > r=${lo.range.toFixed(1)} cut=${lo.cutoff}`);
    }
  }

  // cross-check the sorted extremes against the closed-form model
  const nearest = byRange[0];
  const farthest = byRange[byRange.length - 1];
  if (Math.abs(nearest.gain - spreadingGain(nearest.range)) > 1e-6) fail("nearest gain != model");
  if (Math.abs(farthest.cutoff - absorptionCutoffHz(farthest.range)) > 1e-3) fail("farthest cutoff != model");

  // ---- report --------------------------------------------------------------
  const ranges = rows.map((r) => r.range);
  const delays = rows.map((r) => r.delay);
  const rMin = Math.min(...ranges);
  const rMax = Math.max(...ranges);
  const dMin = Math.min(...delays);
  const dMax = Math.max(...delays);
  const crackleMax = Math.max(...rows.map((r) => r.crackle));
  const dopMin = Math.min(...rows.map((r) => r.doppler));
  const dopMax = Math.max(...rows.map((r) => r.doppler));
  const anyEngine = rows.some((r) => r.nEng > 0 && r.throttle > 0);

  console.log("=== AUDIO PROPAGATION SMOKE — PASS ===");
  console.log(`frames: TLM=${tlm}  HELLO=${hello}  EVT=${evt}  STATS=${stats}`);
  console.log(`listener (three): (${LISTENER.x}, ${LISTENER.y}, ${LISTENER.z})`);
  console.log(`slant range:  min ${rMin.toFixed(1)} m   max ${rMax.toFixed(1)} m`);
  console.log(`retarded delay: min ${dMin.toFixed(3)} s   max ${dMax.toFixed(3)} s   ("you are up to ${dMax.toFixed(1)} s away")`);
  console.log(`gain @nearest ${nearest.gain.toFixed(4)}  @farthest ${farthest.gain.toExponential(2)}`);
  console.log(`absorption knee: @nearest ${nearest.cutoff.toFixed(0)} Hz  @farthest ${farthest.cutoff.toFixed(0)} Hz`);
  console.log(`crackle audibility max: ${(crackleMax * 100).toFixed(0)}%   doppler ratio range: ${dopMin.toFixed(3)}..${dopMax.toFixed(3)}`);
  // the spectral reveal arc: crackle at the farthest vs nearest sampled range
  console.log(`spectral reveal: crackle @farthest(${(farthest.range/1000).toFixed(1)}km)=${(farthest.crackle*100).toFixed(0)}%  →  @nearest(${(nearest.range/1000).toFixed(1)}km)=${(nearest.crackle*100).toFixed(0)}%  (fades IN as range closes)`);
  console.log(`engine active in stream: ${anyEngine}`);
  console.log("invariants: all propagation outputs finite; gain & cutoff monotone-non-increasing vs slant range; delay == range/343; gain,crackle in [0,1]; doppler>0.");
  process.exit(0);
}
