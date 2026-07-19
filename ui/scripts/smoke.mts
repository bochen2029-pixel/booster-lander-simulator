// smoke.mts — S1 live-wire smoke gate. Connects to a running booster-core --serve,
// decodes N TLM frames (default 500) THROUGH THE REAL decode.ts / events.ts (the
// exact decoders the scene consumes), and asserts every field the S1 scene reads is
// SANE: finite and in a physically plausible range. Also confirms HELLO/EVT decode,
// and that pred_impact + ignite_h (the v3 diegetic-marker fields) are live.
//
// Run: node --experimental-transform-types scripts/smoke.mts [ws://127.0.0.1:8141] [N]
// Exit 0 = all assertions passed; exit 1 = a field was out of range / short stream.
//
// This is the headless gate that stands in for eyes-on (which happens at
// integration): it proves the observer path end-to-end without a browser.

import { decodeTlm, frameMagic, TLM_MAGIC, HELLO_MAGIC, EVT_MAGIC, STATS_MAGIC, Phase, Verdict } from "../src/net/decode.ts";
import { decodeHello, decodeEvt, decodeStats, EvtCode, EVT_LABEL } from "../src/net/events.ts";
import type { TlmFrame } from "../src/net/decode.ts";
import type { EvtFrame } from "../src/net/events.ts";

const URL = process.argv[2] ?? "ws://127.0.0.1:8141";
const N = Number(process.argv[3] ?? 500);

interface Range { min: number; max: number }
// plausible ranges for the fields the S1 scene consumes (generous but catches NaN /
// garbage / wrong-offset decodes). Altitude entry ~62 km; speeds up to ~2 km/s.
const RANGES: Record<string, Range> = {
  "r.z (alt)": { min: -50, max: 80000 },
  "|v|": { min: 0, max: 3000 },
  throttleAct: { min: 0, max: 1.01 },
  mach: { min: 0, max: 30 },
  qbar: { min: 0, max: 200000 },
  pChamber: { min: 0, max: 3e7 },
  pAmb: { min: 0, max: 120000 },
  nEng: { min: 0, max: 9 },
  tGo: { min: -1, max: 2000 },
  igniteH: { min: 0, max: 20000 },
  "predImpact.x": { min: -100000, max: 100000 },
  "predImpact.y": { min: -100000, max: 100000 },
  deployFrac: { min: 0, max: 1.01 },
};

const fails: string[] = [];
function chk(name: string, v: number): void {
  if (!Number.isFinite(v)) {
    fails.push(`${name} not finite: ${v}`);
    return;
  }
  const r = RANGES[name];
  if (r && (v < r.min || v > r.max)) fails.push(`${name}=${v} out of [${r.min},${r.max}]`);
}

function assertFrame(f: TlmFrame): void {
  chk("r.z (alt)", f.r[2]);
  chk("|v|", Math.hypot(f.v[0], f.v[1], f.v[2]));
  chk("throttleAct", f.throttleAct);
  chk("mach", f.mach);
  chk("qbar", f.qbar);
  chk("pChamber", f.pChamber);
  chk("pAmb", f.pAmb);
  chk("nEng", f.nEng);
  chk("tGo", f.tGo);
  chk("igniteH", f.igniteH);
  chk("predImpact.x", f.predImpact[0]);
  chk("predImpact.y", f.predImpact[1]);
  chk("deployFrac", f.deployFrac);
  // quaternion must be ~unit (attitude the scene applies)
  const qn = Math.hypot(f.quat[0], f.quat[1], f.quat[2], f.quat[3]);
  if (Math.abs(qn - 1) > 0.05) fails.push(`quat not unit: |q|=${qn.toFixed(4)}`);
  // enums in range
  if (f.phase < Phase.Init || f.phase > Phase.LOC) fails.push(`phase enum OOR: ${f.phase}`);
  if (f.verdict < Verdict.None || f.verdict > Verdict.Crashed) fails.push(`verdict OOR: ${f.verdict}`);
}

let tlm = 0, hello = 0, stats = 0;
const evts: EvtFrame[] = [];
let first: TlmFrame | null = null, last: TlmFrame | null = null;
let seqGaps = 0, lastSeq = -1;
let helloOk = false;
let graded = false;
let streamEnded = false;
// a run counts as "complete" if we saw a terminal beat (touchdown/verdict) — then
// a short stream is a finished descent, not a stall.
const MIN_HEALTHY = 250;

const ws = new WebSocket(URL);
ws.binaryType = "arraybuffer";

const done = (code: number) => {
  try { ws.close(); } catch { /* already closed */ }
  process.exit(code);
};

ws.onerror = () => {
  console.error(`SMOKE FAIL: cannot connect to ${URL} (is --serve running on that port?)`);
  done(1);
};

ws.onopen = () => console.log(`smoke: connected ${URL}, decoding up to ${N} TLM frames (or one full descent)...`);

// A booster run is FINITE: the server streams one descent then closes the socket.
// If that happens after a healthy number of frames, it's a completed run, not a
// stall — finish and grade what we got (touchdown + verdict are the good ending).
ws.onclose = () => {
  if (!graded) {
    streamEnded = true;
    finish();
  }
};

ws.onmessage = (ev: MessageEvent) => {
  const buf = ev.data as ArrayBuffer;
  switch (frameMagic(buf)) {
    case TLM_MAGIC: {
      const f = decodeTlm(buf);
      if (!first) first = f;
      last = f;
      if (lastSeq >= 0 && f.seq - lastSeq - 1 > 0) seqGaps += f.seq - lastSeq - 1;
      lastSeq = f.seq;
      assertFrame(f);
      tlm++;
      if (tlm >= N) finish();
      break;
    }
    case HELLO_MAGIC: {
      hello++;
      const h = decodeHello(buf);
      helloOk = h.ver === 3 && h.vehLen > 0 && h.tlmHz > 0;
      if (!helloOk) fails.push(`HELLO implausible: ver=${h.ver} vehLen=${h.vehLen} tlmHz=${h.tlmHz}`);
      break;
    }
    case EVT_MAGIC: {
      const e = decodeEvt(buf);
      if (e.code < EvtCode.PhaseChange || e.code > EvtCode.SolverDegraded) fails.push(`EVT code OOR: ${e.code}`);
      if (!Number.isFinite(e.t)) fails.push(`EVT t not finite`);
      evts.push(e);
      break;
    }
    case STATS_MAGIC: {
      stats++;
      const s = decodeStats(buf);
      if (!Number.isFinite(s.fpsEmit) || s.fpsEmit < 0) fails.push(`STATS fps bad: ${s.fpsEmit}`);
      break;
    }
    default:
      break;
  }
};

function finish(): void {
  if (graded) return;
  graded = true;
  const f0 = first as TlmFrame | null;
  const fN = last as TlmFrame | null;
  console.log(`\n=== S1 wire smoke ===`);
  console.log(`TLM decoded: ${tlm}  HELLO: ${hello}  EVT: ${evts.length}  STATS: ${stats}  seqGaps: ${seqGaps}`);
  if (f0 && fN) {
    console.log(`t: ${f0.t.toFixed(2)}s -> ${fN.t.toFixed(2)}s   alt: ${f0.r[2].toFixed(0)}m -> ${fN.r[2].toFixed(0)}m   phase: ${f0.phase} -> ${fN.phase}`);
    console.log(`pred_impact last: [${fN.predImpact[0].toFixed(1)}, ${fN.predImpact[1].toFixed(1)}]  ignite_h: ${fN.igniteH.toFixed(1)}m  throttle: ${(fN.throttleAct*100).toFixed(0)}%  nEng: ${fN.nEng}`);
  }
  const beats = evts.map((e) => EVT_LABEL[e.code as EvtCode]).slice(0, 12).join(" ");
  if (beats) console.log(`EVT beats: ${beats}${evts.length > 12 ? " ..." : ""}`);

  // gate assertions.
  // If the stream ENDED (finite descent), accept a short frame count as long as we
  // got a healthy run — ideally through touchdown/verdict. Only a mid-stream stall
  // (timeout with too few frames) is a real failure.
  const sawTerminal = evts.some(
    (e) => e.code === EvtCode.Touchdown || e.code === EvtCode.Verdict
  );
  if (streamEnded) {
    if (tlm < MIN_HEALTHY) fails.push(`stream ended early: only ${tlm} frames (<${MIN_HEALTHY})`);
    else if (!sawTerminal)
      console.log(`note: stream ended after ${tlm} frames without a terminal beat (partial run — still all-sane)`);
  } else if (tlm < N) {
    fails.push(`only ${tlm}/${N} TLM frames arrived (stream stalled, socket still open)`);
  }
  if (hello < 1) fails.push(`no HELLO frame`);
  if (!helloOk) fails.push(`HELLO not validated`);

  if (fails.length) {
    console.error(`\nSMOKE FAIL (${fails.length}):`);
    for (const m of fails.slice(0, 20)) console.error("  - " + m);
    done(1);
  }
  const ending = sawTerminal ? " (full descent through touchdown/verdict)" : streamEnded ? " (partial run, clean end)" : "";
  console.log(`\nSMOKE PASS: ${tlm} frames${ending}, all consumed fields finite + in range; HELLO v3 ok; ${evts.length} EVT beats decoded.`);
  done(0);
}

// safety timeout: if the stream stalls, fail rather than hang (survival rule)
setTimeout(() => {
  if (tlm < N) {
    fails.push(`timeout: ${tlm}/${N} frames after 30s`);
    finish();
  }
}, 30000);
