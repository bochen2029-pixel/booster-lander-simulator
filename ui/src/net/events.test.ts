// events.test.ts — EVT/HELLO/STATS decoders vs hand-built canonical bytes. These
// mirror core/protocol.h (BlEvt=48, BlHello=72, BlStats=48); an offset drift here
// is caught the same way decode.test.ts guards the TLM head.

import { describe, it, expect } from "vitest";
import {
  decodeEvt,
  decodeHello,
  decodeStats,
  EvtCode,
  EVT_SIZE,
  HELLO_SIZE,
  STATS_SIZE,
  EVT_LABEL,
  EVT_GLYPH,
  EVT_CODE_COUNT,
} from "./events";
import { EVT_MAGIC, HELLO_MAGIC, STATS_MAGIC, PROTO_VERSION } from "./decode";

const LE = true;

describe("decodeEvt", () => {
  it("round-trips a MACH1_CROSS with r_emit args at the right offsets", () => {
    const buf = new ArrayBuffer(EVT_SIZE);
    const dv = new DataView(buf);
    dv.setUint32(0, EVT_MAGIC, LE);
    dv.setUint16(4, EvtCode.Mach1Cross, LE);
    dv.setBigUint64(8, 12345n, LE);
    dv.setFloat64(16, 42.5, LE);
    dv.setFloat32(24, 111.0, LE); // r_emit x
    dv.setFloat32(28, 222.0, LE); // r_emit y
    dv.setFloat32(32, 333.0, LE); // r_emit z
    const e = decodeEvt(buf);
    expect(e.code).toBe(EvtCode.Mach1Cross);
    expect(e.step).toBe(12345);
    expect(e.t).toBeCloseTo(42.5, 9);
    expect(e.args[0]).toBeCloseTo(111, 5);
    expect(e.args[1]).toBeCloseTo(222, 5);
    expect(e.args[2]).toBeCloseTo(333, 5);
  });

  it("decodes a TOUCHDOWN with v_impact/tilt", () => {
    const buf = new ArrayBuffer(EVT_SIZE);
    const dv = new DataView(buf);
    dv.setUint32(0, EVT_MAGIC, LE);
    dv.setUint16(4, EvtCode.Touchdown, LE);
    dv.setFloat32(24, 2.4, LE); // v_impact
    dv.setFloat32(28, 0.03, LE); // tilt
    const e = decodeEvt(buf);
    expect(e.code).toBe(EvtCode.Touchdown);
    expect(e.args[0]).toBeCloseTo(2.4, 5);
    expect(e.args[1]).toBeCloseTo(0.03, 5);
  });
});

describe("EVT tables", () => {
  it("has a label + glyph for all 14 codes", () => {
    expect(EVT_CODE_COUNT).toBe(14);
    for (let c = 0; c < EVT_CODE_COUNT; c++) {
      expect(EVT_LABEL[c as EvtCode]).toBeTruthy();
      expect(EVT_GLYPH[c as EvtCode]).toBeTruthy();
    }
  });
});

describe("decodeHello", () => {
  it("reads geometry + session fields at the v3 offsets", () => {
    const buf = new ArrayBuffer(HELLO_SIZE);
    const dv = new DataView(buf);
    dv.setUint32(0, HELLO_MAGIC, LE);
    dv.setUint16(4, PROTO_VERSION, LE);
    dv.setFloat64(8, 0.0, LE); // t0
    dv.setBigUint64(16, 42n, LE); // seed
    dv.setFloat32(24, 0.002, LE); // dt
    dv.setFloat32(28, 125.0, LE); // tlm_hz
    dv.setUint32(32, 4, LE); // tlm_decim
    dv.setUint32(36, 17, LE); // run_idx
    dv.setFloat32(40, 47.7, LE); // veh_len
    dv.setFloat32(44, 3.66, LE); // veh_dia
    dv.setFloat32(48, 18.0, LE); // leg_span
    dv.setFloat32(52, 30.0, LE); // pad_radius
    dv.setFloat32(56, 0.0, LE); // deck_z
    dv.setFloat32(60, 9.7e6, LE); // pc_ref
    dv.setUint16(64, 64, LE); // plan_max
    dv.setUint16(66, 128, LE); // cloud_max
    dv.setUint8(68, 1); // scenario
    dv.setUint8(69, 2); // guidance_mode
    dv.setUint8(70, 0x1f); // modules
    const h = decodeHello(buf);
    expect(h.ver).toBe(PROTO_VERSION);
    expect(h.seed).toBe(42);
    expect(h.tlmHz).toBeCloseTo(125, 3);
    expect(h.runIdx).toBe(17);
    expect(h.vehLen).toBeCloseTo(47.7, 4);
    expect(h.vehDia).toBeCloseTo(3.66, 4);
    expect(h.legSpan).toBeCloseTo(18, 4);
    expect(h.padRadius).toBeCloseTo(30, 4);
    expect(h.planMax).toBe(64);
    expect(h.cloudMax).toBe(128);
    expect(h.guidanceMode).toBe(2);
  });
});

describe("decodeStats", () => {
  it("reads the peaks + fuel + twr + fps (tlm_seq is a float field)", () => {
    const buf = new ArrayBuffer(STATS_SIZE);
    const dv = new DataView(buf);
    dv.setUint32(0, STATS_MAGIC, LE);
    dv.setUint16(4, PROTO_VERSION, LE);
    dv.setBigUint64(8, 999n, LE);
    dv.setFloat64(16, 12.0, LE);
    dv.setFloat32(24, 43000.0, LE); // max_qbar
    dv.setFloat32(28, 1.2e5, LE); // peak_qdot
    dv.setFloat32(32, 8200.0, LE); // fuel_kg
    dv.setFloat32(36, 1.35, LE); // twr
    dv.setFloat32(40, 4321.0, LE); // tlm_seq (as float)
    dv.setFloat32(44, 124.8, LE); // fps_emit
    const s = decodeStats(buf);
    expect(s.ver).toBe(PROTO_VERSION);
    expect(s.maxQbar).toBeCloseTo(43000, 1);
    expect(s.fuelKg).toBeCloseTo(8200, 1);
    expect(s.twr).toBeCloseTo(1.35, 4);
    expect(s.tlmSeq).toBeCloseTo(4321, 1);
    expect(s.fpsEmit).toBeCloseTo(124.8, 3);
  });
});
