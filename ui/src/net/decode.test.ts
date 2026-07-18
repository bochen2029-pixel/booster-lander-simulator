// decode.test.ts — proves decode.ts mirrors core/protocol.h byte-for-byte.
//
// Strategy: build a TLM buffer here using the SAME offsets, with a distinct,
// offset-derived sentinel value in every field, then decode and assert each field
// round-trips to its sentinel. Any offset drift shifts a value and fails loudly.
// This stands in for the goldens/protocol/*.hex check until the C side emits the
// canonical bytes (canon §13.1 "protocol struct sizes/offsets + canonical bytes").

import { describe, it, expect } from "vitest";
import {
  decodeTlm,
  TLM_MAGIC,
  PROTO_VERSION,
  TLM_FIXED_SIZE,
  PLAN_KNOT_SIZE,
  CLOUD_SAMPLE_SIZE,
  tlmFrameSize,
  Phase,
  Verdict,
} from "./decode";

const LE = true;

function buildFrame(planN: number, cloudN: number): ArrayBuffer {
  const buf = new ArrayBuffer(tlmFrameSize(planN, cloudN));
  const dv = new DataView(buf);
  // sentinel: each float field = its own byte offset (so a shift is detectable).
  const f = (off: number) => dv.setFloat32(off, off, LE);

  dv.setUint32(0, TLM_MAGIC, LE);
  dv.setUint16(4, PROTO_VERSION, LE);
  dv.setUint16(6, 0x0005, LE); // flags: SEA_ACTIVE | NAV_NOISY
  dv.setBigUint64(8, 123456789n, LE);
  dv.setFloat64(16, 42.5, LE);
  dv.setUint32(24, 7, LE);
  // 28 pad
  for (let o = 32; o <= 80; o += 4) f(o); // r,v,quat,w
  for (let o = 84; o <= 108; o += 4) f(o); // mass..rp1
  for (let o = 112; o <= 148; o += 4) f(o); // actuators
  dv.setUint16(152, 0x00ab, LE); // rcs_mask
  dv.setUint8(154, 3); // n_eng
  dv.setUint8(155, Phase.LandingBurn);
  dv.setUint8(156, 2); // guidance_mode = mppi
  dv.setUint8(157, Verdict.LandedGood);
  dv.setUint16(158, 0x0010, LE); // solver_flags
  for (let o = 160; o <= 208; o += 4) f(o); // env & derived
  for (let o = 212; o <= 228; o += 4) f(o); // t_go, dist_pad, pred_impact[2], ignite_h (v3)
  for (let o = 232; o <= 248; o += 4) f(o); // deploy, stroke
  for (let o = 252; o <= 260; o += 4) f(o); // f_aero
  for (let o = 264; o <= 280; o += 4) f(o); // deck_z, deck_quat
  dv.setUint16(284, planN, LE);
  dv.setUint16(286, cloudN, LE);

  let off = TLM_FIXED_SIZE;
  for (let i = 0; i < planN; i++) {
    dv.setFloat32(off, i * 10 + 1, LE);
    dv.setFloat32(off + 4, i * 10 + 2, LE);
    dv.setFloat32(off + 8, i * 10 + 3, LE);
    dv.setFloat32(off + 12, i * 0.01, LE); // throttle
    off += PLAN_KNOT_SIZE;
  }
  for (let i = 0; i < cloudN; i++) {
    dv.setFloat32(off, i * 100 + 1, LE);
    dv.setFloat32(off + 4, i * 100 + 2, LE);
    dv.setFloat32(off + 8, i / cloudN, LE); // weight
    off += CLOUD_SAMPLE_SIZE;
  }
  return buf;
}

describe("TLM decoder mirrors protocol.h", () => {
  it("fixed size is 288", () => {
    expect(TLM_FIXED_SIZE).toBe(288);
  });

  it("decodes every field at the right offset", () => {
    const f = decodeTlm(buildFrame(3, 5));

    expect(f.step).toBe(123456789);
    expect(f.t).toBeCloseTo(42.5, 9);
    expect(f.seq).toBe(7);
    expect(f.flags).toBe(0x0005);

    // float sentinels == their offsets
    expect(f.r).toEqual([32, 36, 40]);
    expect(f.v).toEqual([44, 48, 52]);
    expect(f.quat).toEqual([56, 60, 64, 68]);
    expect(f.w).toEqual([72, 76, 80]);
    expect(f.mass).toBe(84);
    expect(f.comZ).toBe(88);
    expect(f.Idiag).toEqual([92, 96, 100]);
    expect(f.propLox).toBe(104);
    expect(f.propRp1).toBe(108);
    expect(f.throttleCmd).toBe(112);
    expect(f.throttleAct).toBe(116);
    expect(f.gimbalCmd).toEqual([120, 124]);
    expect(f.gimbalAct).toEqual([128, 132]);
    expect(f.finsAct).toEqual([136, 140, 144, 148]);

    expect(f.rcsMask).toBe(0x00ab);
    expect(f.nEng).toBe(3);
    expect(f.phase).toBe(Phase.LandingBurn);
    expect(f.guidanceMode).toBe(2);
    expect(f.verdict).toBe(Verdict.LandedGood);
    expect(f.solverFlags).toBe(0x0010);

    expect(f.mach).toBe(160);
    expect(f.qbar).toBe(164);
    expect(f.alphaTotal).toBe(168);
    expect(f.pAmb).toBe(172);
    expect(f.pChamber).toBe(176);
    expect(f.windLocal).toEqual([180, 184, 188]);
    expect(f.aBody).toEqual([192, 196, 200]);
    expect(f.qdotHeat).toBe(204);
    expect(f.Qheat).toBe(208);

    expect(f.tGo).toBe(212);
    expect(f.distPad).toBe(216);
    expect(f.predImpact).toEqual([220, 224]); // v3
    expect(f.igniteH).toBe(228); // v3
    expect(f.deployFrac).toBe(232);
    expect(f.stroke).toEqual([236, 240, 244, 248]);
    expect(f.fAero).toEqual([252, 256, 260]);
    expect(f.deckZ).toBe(264);
    expect(f.deckQuat).toEqual([268, 272, 276, 280]);

    // tails
    expect(f.plan).toHaveLength(3);
    expect(f.plan[2].r).toEqual([21, 22, 23]);
    expect(f.plan[1].throttle).toBeCloseTo(0.01, 6);
    expect(f.cloud).toHaveLength(5);
    expect(f.cloud[4].xy).toEqual([401, 402]);
    expect(f.cloud[2].weight).toBeCloseTo(2 / 5, 6);
  });

  it("rejects a bad magic", () => {
    const buf = buildFrame(0, 0);
    new DataView(buf).setUint32(0, 0xdeadbeef, LE);
    expect(() => decodeTlm(buf)).toThrow(/bad magic/);
  });

  it("handles empty tails", () => {
    const f = decodeTlm(buildFrame(0, 0));
    expect(f.plan).toHaveLength(0);
    expect(f.cloud).toHaveLength(0);
  });
});
