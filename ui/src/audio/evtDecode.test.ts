// evtDecode.test.ts — the audio trigger-bus EVT decoder vs the frozen golden bytes.
//
// The audio observer decodes EVT itself (canon §B.8: "its own client"). This test
// golden-checks decodeEvt against the EXACT bytes core --serve emits.
//
// GOLDEN_HEX below is a byte-for-byte copy of goldens/protocol/evt.hex (the frozen
// wire golden). We inline it as a constant rather than reading it via node:fs so the
// test needs no `@types/node` and stays green under the app's DOM-only tsconfig
// (the same reason decode.test.ts / frame.test.ts embed their vectors). The LIVE
// wire is separately validated by smoke/audio_smoke.mts, which decodes real EVT
// frames off the socket. A drift check against the file lives there.

import { describe, it, expect } from "vitest";
import { decodeEvt, EvtCode, EVT_MAGIC, EVT_SIZE, isEvt } from "./evtDecode";

// === goldens/protocol/evt.hex (verbatim) ===================================
const GOLDEN_HEX =
  "4556543000000000f401000000000000030000000000f03f000080400000803f00000000000000000000000000000000";

function hexToBuf(hex: string): ArrayBuffer {
  const clean = hex.trim().replace(/\s+/g, "");
  const n = clean.length / 2;
  const u8 = new Uint8Array(n);
  for (let i = 0; i < n; i++) u8[i] = parseInt(clean.substr(i * 2, 2), 16);
  return u8.buffer;
}

describe("EVT golden bytes (goldens/protocol/evt.hex)", () => {
  const buf = hexToBuf(GOLDEN_HEX);

  it("golden frame is exactly 48 bytes and is an EVT frame", () => {
    expect(buf.byteLength).toBe(EVT_SIZE);
    expect(isEvt(buf)).toBe(true);
  });

  it("decodes the golden EVT header (magic/code/step/t) and finite args", () => {
    const e = decodeEvt(buf);
    expect(EVT_MAGIC).toBe(0x30545645); // 'EVT0'
    expect(e.code).toBe(EvtCode.PhaseChange); // code 0 in the golden
    expect(e.step).toBe(500); // 0x1F4
    expect(e.t).toBeCloseTo(1.0, 6);
    expect(e.args).toHaveLength(6);
    for (const a of e.args) expect(Number.isFinite(a)).toBe(true);
  });
});

describe("EVT synthesized round-trip (offset sentinels)", () => {
  it("reads back the exact fields written at their protocol.h offsets", () => {
    const u8 = new Uint8Array(EVT_SIZE);
    const dv = new DataView(u8.buffer);
    dv.setUint32(0, EVT_MAGIC, true);
    dv.setUint16(4, EvtCode.Mach1Cross, true); // code
    dv.setBigUint64(8, 12345n, true); // step
    dv.setFloat64(16, 42.5, true); // t
    // Mach1Cross carries r_emit[3] in args[0..2]; the rest are sentinels
    dv.setFloat32(24, 111.0, true);
    dv.setFloat32(28, 222.0, true);
    dv.setFloat32(32, 333.0, true);
    dv.setFloat32(36, 4.0, true);
    dv.setFloat32(40, 5.0, true);
    dv.setFloat32(44, 6.0, true);

    const e = decodeEvt(u8.buffer);
    expect(e.code).toBe(EvtCode.Mach1Cross);
    expect(e.step).toBe(12345);
    expect(e.t).toBeCloseTo(42.5, 9);
    expect(e.args).toEqual([111, 222, 333, 4, 5, 6]);
  });

  it("rejects a non-EVT frame loudly", () => {
    const u8 = new Uint8Array(EVT_SIZE);
    new DataView(u8.buffer).setUint32(0, 0xdeadbeef, true);
    expect(() => decodeEvt(u8.buffer)).toThrow(/bad magic/);
    expect(isEvt(u8.buffer)).toBe(false);
  });
});
