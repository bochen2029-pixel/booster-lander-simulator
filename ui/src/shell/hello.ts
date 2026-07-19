// hello.ts — HELLO frame identity gate (canon §10.2/§10.3; understory §0.1).
//
// The supervisor guarantees the PORT is one WE spawned, but the frontend still
// performs an independent IDENTITY GATE on the wire before trusting the stream:
// the first frame must be a HELLO with magic 'HLL0' and ver == PROTO_VERSION (3).
// This is the exact analog of understory's "don't attach to whatever is squatting
// on the port" — if some other producer answers, its bytes won't pass this gate.
//
// Byte layout (verified against goldens/protocol/hello.hex, decoded 2026-07-18):
//   [0..4)  u32  magic = 0x304c4c48  ('HLL0', little-endian on the wire)
//   [4..6)  u16  ver                 (== 3 today)
//   ... geometry block follows (scalar pad/vehicle params); S1 will decode it.
// The measured HELLO frame is 72 bytes total.

import { HELLO_MAGIC, PROTO_VERSION } from "../net/decode";

export interface HelloIdentity {
  ok: boolean;
  magic: number;
  ver: number;
  /** Human-readable reason when ok === false. */
  reason?: string;
}

const LE = true;

/** Verify a raw first-frame ArrayBuffer is a protocol-v3 HELLO. Pure/allocation-free. */
export function verifyHello(buf: ArrayBuffer, byteOffset = 0): HelloIdentity {
  if (buf.byteLength - byteOffset < 6) {
    return { ok: false, magic: 0, ver: 0, reason: `frame too short (${buf.byteLength} B)` };
  }
  const dv = new DataView(buf, byteOffset);
  const magic = dv.getUint32(0, LE);
  const ver = dv.getUint16(4, LE);
  if (magic !== HELLO_MAGIC) {
    return {
      ok: false,
      magic,
      ver,
      reason: `bad magic 0x${magic.toString(16)} (expected HLL0 0x${HELLO_MAGIC.toString(16)})`,
    };
  }
  if (ver !== PROTO_VERSION) {
    return {
      ok: false,
      magic,
      ver,
      reason: `protocol version ${ver} != ${PROTO_VERSION} (refusing to trust stream)`,
    };
  }
  return { ok: true, magic, ver };
}
