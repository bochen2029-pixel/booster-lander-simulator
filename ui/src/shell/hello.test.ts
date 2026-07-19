// hello.test.ts — proves the HELLO identity gate (verifyHello) against the
// canonical golden bytes (goldens/protocol/hello.hex) and the failure paths.
//
// The golden is ASCII hex text (two chars per byte). We decode it here and run
// the exact gate the cockpit shell runs on the first wire frame. This is the S0
// identity-gate proof: a real HELLO passes; a wrong magic, a wrong version, and
// a short frame are all rejected with readable reasons.

import { describe, it, expect } from "vitest";
// Import the canonical golden as raw text (Vite/vitest ?raw loader — no node
// types needed, and it still catches golden drift because it reads the real file
// at ../../../goldens/protocol/hello.hex through the bundler's resolver).
import helloHex from "../../../goldens/protocol/hello.hex?raw";
import { verifyHello } from "./hello";
import { HELLO_MAGIC, PROTO_VERSION } from "../net/decode";

/** Parse an ASCII-hex golden string into an ArrayBuffer. */
function hexToBuffer(text: string): ArrayBuffer {
  const clean = text.replace(/[^0-9a-fA-F]/g, "");
  const n = clean.length >> 1;
  const bytes = new Uint8Array(n);
  for (let i = 0; i < n; i++) bytes[i] = parseInt(clean.substr(i * 2, 2), 16);
  return bytes.buffer;
}
function readHexGolden(): ArrayBuffer {
  return hexToBuffer(helloHex);
}

describe("HELLO identity gate", () => {
  it("accepts the canonical golden HELLO (magic HLL0, ver 3)", () => {
    const buf = readHexGolden();
    const id = verifyHello(buf);
    expect(id.ok).toBe(true);
    expect(id.magic).toBe(HELLO_MAGIC);
    expect(id.ver).toBe(PROTO_VERSION);
  });

  it("rejects a frame with the wrong magic", () => {
    const buf = readHexGolden();
    new DataView(buf).setUint32(0, 0x30_4d_4c_54, true); // TLM0, not HLL0
    const id = verifyHello(buf);
    expect(id.ok).toBe(false);
    expect(id.reason).toMatch(/bad magic/);
  });

  it("rejects a mismatched protocol version (squatter on the port)", () => {
    const buf = readHexGolden();
    new DataView(buf).setUint16(4, 2, true); // pretend v2
    const id = verifyHello(buf);
    expect(id.ok).toBe(false);
    expect(id.reason).toMatch(/version 2 != 3/);
  });

  it("rejects a too-short frame", () => {
    const id = verifyHello(new ArrayBuffer(4));
    expect(id.ok).toBe(false);
    expect(id.reason).toMatch(/too short/);
  });
});
