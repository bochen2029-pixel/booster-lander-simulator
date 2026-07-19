// events.ts — EVT/HELLO/STATS decoders (canon §A.4/§A.5). client.ts hands these
// three packet kinds to the app as raw ArrayBuffers (onEvtBytes/onHelloBytes/
// onStatsBytes); TLM has its own decoder in decode.ts. These mirror the exact
// pack(1) layouts in core/protocol.h (BlEvt=48, BlHello=72, BlStats=48).
//
// The EVT channel is the ONLY trigger bus (canon §A.0): director cuts, HUD phase
// ladder beats, timeline glyphs, and the plume green-flash pulse all key off EVT.

const LE = true;

// --- EVT codes (BlEvtCode, protocol.h) --------------------------------------
export enum EvtCode {
  PhaseChange = 0,
  IgnitionCmd = 1,
  GreenFlash = 2,
  EngineStart = 3,
  EngineShutdown = 4,
  Mach1Cross = 5, // args = r_emit[3] (emission world pos)
  LegDeploy = 6,
  Touchdown = 7, // args = v_impact, tilt
  Verdict = 8, // args[0] = grade
  Gust = 9, // args = vec[3]
  Fault = 10, // args[0] = type
  TargetChanged = 11, // args[0] = pad
  RcsPulse = 12, // args[0] = mask
  SolverDegraded = 13,
}

/** All 14 codes in order — the phase-ladder / timeline iterates this. */
export const EVT_CODE_COUNT = 14;

/** Short human glyph labels for the timeline scrubber + HUD ticker. */
export const EVT_LABEL: Record<EvtCode, string> = {
  [EvtCode.PhaseChange]: "PHASE",
  [EvtCode.IgnitionCmd]: "IGNITE",
  [EvtCode.GreenFlash]: "TEA-TEB",
  [EvtCode.EngineStart]: "START",
  [EvtCode.EngineShutdown]: "SHUTDN",
  [EvtCode.Mach1Cross]: "MACH1",
  [EvtCode.LegDeploy]: "LEGS",
  [EvtCode.Touchdown]: "TD",
  [EvtCode.Verdict]: "VERDICT",
  [EvtCode.Gust]: "GUST",
  [EvtCode.Fault]: "FAULT",
  [EvtCode.TargetChanged]: "DIVERT",
  [EvtCode.RcsPulse]: "RCS",
  [EvtCode.SolverDegraded]: "SOLVER",
};

/** Single-char glyph for the compact timeline dots. */
export const EVT_GLYPH: Record<EvtCode, string> = {
  [EvtCode.PhaseChange]: "◆",
  [EvtCode.IgnitionCmd]: "▲",
  [EvtCode.GreenFlash]: "✦",
  [EvtCode.EngineStart]: "▶",
  [EvtCode.EngineShutdown]: "■",
  [EvtCode.Mach1Cross]: "≋",
  [EvtCode.LegDeploy]: "⋔",
  [EvtCode.Touchdown]: "⊥",
  [EvtCode.Verdict]: "★",
  [EvtCode.Gust]: "≈",
  [EvtCode.Fault]: "!",
  [EvtCode.TargetChanged]: "⊕",
  [EvtCode.RcsPulse]: "·",
  [EvtCode.SolverDegraded]: "⚠",
};

export interface EvtFrame {
  code: EvtCode;
  step: number;
  t: number; // sim seconds (f64)
  args: [number, number, number, number, number, number];
}

export const EVT_SIZE = 48;

/** Decode a BlEvt (48 B). Layout: magic(0) code(u16@4) _pad(6) step(u64@8)
 *  t(f64@16) args[6](f32@24). */
export function decodeEvt(buf: ArrayBuffer, byteOffset = 0): EvtFrame {
  const dv = new DataView(buf, byteOffset);
  const code = dv.getUint16(4, LE) as EvtCode;
  const step = Number(dv.getBigUint64(8, LE));
  const t = dv.getFloat64(16, LE);
  const f = (o: number) => dv.getFloat32(o, LE);
  const args: EvtFrame["args"] = [f(24), f(28), f(32), f(36), f(40), f(44)];
  return { code, step, t, args };
}

// --- HELLO (BlHello, 72 B) ---------------------------------------------------
export interface HelloFrame {
  ver: number;
  t0: number;
  seed: number;
  dt: number;
  tlmHz: number;
  tlmDecim: number;
  runIdx: number;
  vehLen: number;
  vehDia: number;
  legSpan: number;
  padRadius: number;
  deckZ: number;
  pcRef: number;
  planMax: number;
  cloudMax: number;
  scenario: number;
  guidanceMode: number;
  modules: number;
}

export const HELLO_SIZE = 72;

export function decodeHello(buf: ArrayBuffer, byteOffset = 0): HelloFrame {
  const dv = new DataView(buf, byteOffset);
  const f = (o: number) => dv.getFloat32(o, LE);
  return {
    ver: dv.getUint16(4, LE),
    t0: dv.getFloat64(8, LE),
    seed: Number(dv.getBigUint64(16, LE)),
    dt: f(24),
    tlmHz: f(28),
    tlmDecim: dv.getUint32(32, LE),
    runIdx: dv.getUint32(36, LE),
    vehLen: f(40),
    vehDia: f(44),
    legSpan: f(48),
    padRadius: f(52),
    deckZ: f(56),
    pcRef: f(60),
    planMax: dv.getUint16(64, LE),
    cloudMax: dv.getUint16(66, LE),
    scenario: dv.getUint8(68),
    guidanceMode: dv.getUint8(69),
    modules: dv.getUint8(70),
  };
}

// --- STATS (BlStats, 48 B) ---------------------------------------------------
export interface StatsFrame {
  ver: number;
  step: number;
  t: number;
  maxQbar: number;
  peakQdot: number;
  fuelKg: number;
  twr: number;
  tlmSeq: number;
  fpsEmit: number;
}

export const STATS_SIZE = 48;

/** Decode BlStats. head: magic(0) ver(u16@4) _pad(6) step(u64@8) t(f64@16)
 *  then all f32: max_qbar(24) peak_qdot(28) fuel_kg(32) twr(36) tlm_seq(40, as
 *  float) fps_emit(44). (Layout per protocol.h BlStats.) */
export function decodeStats(buf: ArrayBuffer, byteOffset = 0): StatsFrame {
  const dv = new DataView(buf, byteOffset);
  const f = (o: number) => dv.getFloat32(o, LE);
  return {
    ver: dv.getUint16(4, LE),
    step: Number(dv.getBigUint64(8, LE)),
    t: dv.getFloat64(16, LE),
    maxQbar: f(24),
    peakQdot: f(28),
    fuelKg: f(32),
    twr: f(36),
    tlmSeq: f(40),
    fpsEmit: f(44),
  };
}
