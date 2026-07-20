// commands.test.ts — pins the BlCmd wire layout against core/protocol.h (24 B, LE).

import { describe, it, expect } from "vitest";
import { encodeCmd, gustCmd, engineOutCmd, CmdType, CMD_MAGIC, CMD_SIZE } from "./commands";

describe("encodeCmd (BlCmd wire mirror)", () => {
  it("is 24 bytes with magic 'CMD0' at offset 0", () => {
    const buf = encodeCmd(CmdType.Gust, 1);
    expect(buf.byteLength).toBe(CMD_SIZE);
    const dv = new DataView(buf);
    expect(dv.getUint32(0, true)).toBe(CMD_MAGIC);
    // magic bytes are ASCII 'C','M','D','0'
    const b = new Uint8Array(buf);
    expect([b[0], b[1], b[2], b[3]]).toEqual([0x43, 0x4d, 0x44, 0x30]);
  });

  it("packs type @4, seq @6, and p[4] as f32 @8..24 (all little-endian)", () => {
    const buf = encodeCmd(CmdType.EngineOut, 0x1234, [25.5, -90, 400, 0]);
    const dv = new DataView(buf);
    expect(dv.getUint16(4, true)).toBe(CmdType.EngineOut);
    expect(dv.getUint16(6, true)).toBe(0x1234);
    expect(dv.getFloat32(8, true)).toBeCloseTo(25.5, 5);
    expect(dv.getFloat32(12, true)).toBeCloseTo(-90, 5);
    expect(dv.getFloat32(16, true)).toBeCloseTo(400, 5);
    expect(dv.getFloat32(20, true)).toBeCloseTo(0, 5);
  });

  it("masks seq to 16 bits (wraps like the C u16)", () => {
    const dv = new DataView(encodeCmd(CmdType.Gust, 65537 /* 0x10001 */));
    expect(dv.getUint16(6, true)).toBe(1);
  });

  it("gustCmd / engineOutCmd set the right type + params", () => {
    const g = new DataView(gustCmd(7, 18, 45, 300));
    expect(g.getUint16(4, true)).toBe(CmdType.Gust);
    expect(g.getUint16(6, true)).toBe(7);
    expect(g.getFloat32(8, true)).toBeCloseTo(18, 5);
    expect(g.getFloat32(12, true)).toBeCloseTo(45, 5);
    expect(g.getFloat32(16, true)).toBeCloseTo(300, 5);

    const e = new DataView(engineOutCmd(9, 2));
    expect(e.getUint16(4, true)).toBe(CmdType.EngineOut);
    expect(e.getUint16(6, true)).toBe(9);
    expect(e.getFloat32(8, true)).toBeCloseTo(2, 5);
  });
});
