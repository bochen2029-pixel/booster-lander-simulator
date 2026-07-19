// evtDecode.ts — minimal EVT (event) packet decoder for the audio trigger bus.
//
// Canon §A.0: "EVT is the only trigger channel." The renderer's client.ts routes
// EVT frames as raw bytes (onEvtBytes); the audio observer decodes them itself so
// it stays a fully independent third observer (canon §B.8: "its own client"). This
// mirrors core/protocol.h BlEvt EXACTLY:
//
//   struct BlEvt {  uint32 magic; uint16 code; uint16 _pad0;
//                   uint64 step;  double t;    float args[6]; }   // 48 bytes, LE
//
// Event codes are wire-stable, append-only (protocol.h BlEvtCode).

export const EVT_MAGIC = 0x30545645; // 'EVT0'
export const EVT_SIZE = 48;

/** BlEvtCode — wire-stable ordinal values (protocol.h). */
export enum EvtCode {
  PhaseChange = 0,
  IgnitionCmd = 1,
  GreenFlash = 2,
  EngineStart = 3,
  EngineShutdown = 4,
  Mach1Cross = 5, // args = r_emit[3] (emission world XYZ, SIM frame)
  LegDeploy = 6,
  Touchdown = 7, // args = v_impact, tilt
  Verdict = 8, // args[0] = grade
  Gust = 9,
  Fault = 10,
  TargetChanged = 11,
  RcsPulse = 12, // args[0] = mask
  SolverDegraded = 13,
}

export interface EvtFrame {
  code: EvtCode;
  step: number;
  t: number; // sim time [s]
  args: [number, number, number, number, number, number];
}

const LE = true;

/** Decode a 48-byte EVT frame from a raw WebSocket ArrayBuffer. */
export function decodeEvt(buf: ArrayBuffer, byteOffset = 0): EvtFrame {
  const dv = new DataView(buf, byteOffset);
  const magic = dv.getUint32(0, LE);
  if (magic !== EVT_MAGIC) {
    throw new Error(`decodeEvt: bad magic 0x${magic.toString(16)} (expected EVT0)`);
  }
  const code = dv.getUint16(4, LE) as EvtCode;
  // 6: _pad0
  const step = Number(dv.getBigUint64(8, LE));
  const t = dv.getFloat64(16, LE);
  const args: [number, number, number, number, number, number] = [
    dv.getFloat32(24, LE),
    dv.getFloat32(28, LE),
    dv.getFloat32(32, LE),
    dv.getFloat32(36, LE),
    dv.getFloat32(40, LE),
    dv.getFloat32(44, LE),
  ];
  return { code, step, t, args };
}

/** Peek the magic (route helper; audio only cares about EVT). */
export function isEvt(buf: ArrayBuffer, byteOffset = 0): boolean {
  return new DataView(buf, byteOffset).getUint32(0, LE) === EVT_MAGIC;
}
