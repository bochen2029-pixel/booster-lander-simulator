// commands.ts — the ONE client->server frame (BlCmd), Mode 2 interactive (canon §M2).
//
// MIRRORS core/protocol.h BlCmd EXACTLY (24 bytes, little-endian): magic 'CMD0' @0,
// type @4 (u16), seq @6 (u16), p[4] @8 (f32 x4). The renderer is a pure observer by
// default; these frames are honored ONLY when core runs `--serve --interactive` (else
// the server drops them). Determinism is deliberately waived on that path; the server
// journals every injection with its sim-time. commands.test.ts pins the byte layout so
// this can never skew from the C struct.

export const CMD_MAGIC = 0x30444d43; // 'CMD0' as LE u32 (C:0x43 M:0x4d D:0x44 0:0x30)
export const CMD_SIZE = 24;

/** BlCmd.type (mirror of protocol.h BL_CMD_*). */
export enum CmdType {
  Gust = 1, // p[0]=peak m/s (0=>server default), p[1]=dir deg, p[2]=half-width m (0=>default)
  EngineOut = 2, // p[0]=engine (1|2 side; 0=>server picks a seeded side)
}

/** Encode a BlCmd into a 24-byte ArrayBuffer ready for TelemetryClient.send(). */
export function encodeCmd(
  type: CmdType,
  seq: number,
  p: [number, number, number, number] = [0, 0, 0, 0]
): ArrayBuffer {
  const buf = new ArrayBuffer(CMD_SIZE);
  const dv = new DataView(buf);
  dv.setUint32(0, CMD_MAGIC, true);
  dv.setUint16(4, type & 0xffff, true);
  dv.setUint16(6, seq & 0xffff, true);
  for (let i = 0; i < 4; i++) dv.setFloat32(8 + i * 4, p[i] ?? 0, true);
  return buf;
}

/** A wind-gust injection: a shear pulse of `peak` m/s from bearing `dir` at the vehicle's
 *  current altitude (the server centers the band there). 0 => server defaults. */
export function gustCmd(seq: number, peakMs = 0, dirDeg = 0, halfWidthM = 0): ArrayBuffer {
  return encodeCmd(CmdType.Gust, seq, [peakMs, dirDeg, halfWidthM, 0]);
}

/** An engine-out injection: side engine `engine` (1|2), or 0 to let the server pick a
 *  seeded side. Fires on the next multi-engine burn. */
export function engineOutCmd(seq: number, engine = 0): ArrayBuffer {
  return encodeCmd(CmdType.EngineOut, seq, [engine, 0, 0, 0]);
}
