// hello_gate_live.mts — F2 integrator live check: run S0's EXACT identity-gate
// logic (verifyHello, verbatim from src/shell/hello.ts) against the first HELLO
// frame off a live core --serve. Proves the shell's on-wire identity gate end-to-
// end. The verifyHello body + its two constants are inlined here VERBATIM (node's
// bare-ESM resolver won't follow hello.ts's extensionless `../net/decode` import;
// Vite does — so we mirror the exact bytes-check rather than import through it).
// Exit 0 = gate PASS on the live frame; exit 1 = reject / no HELLO.

// --- verbatim from src/net/decode.ts ---
const HELLO_MAGIC = 0x304c4c48; // 'HLL0' little-endian
const PROTO_VERSION = 3;
const TLM_MAGIC = 0x304d4c54; // 'TLM0'
// --- verbatim from src/shell/hello.ts (verifyHello) ---
const LE = true;
interface HelloIdentity { ok: boolean; magic: number; ver: number; reason?: string; }
function verifyHello(buf: ArrayBuffer, byteOffset = 0): HelloIdentity {
  if (buf.byteLength - byteOffset < 6) {
    return { ok: false, magic: 0, ver: 0, reason: `frame too short (${buf.byteLength} B)` };
  }
  const dv = new DataView(buf, byteOffset);
  const magic = dv.getUint32(0, LE);
  const ver = dv.getUint16(4, LE);
  if (magic !== HELLO_MAGIC) {
    return { ok: false, magic, ver, reason: `bad magic 0x${magic.toString(16)} (expected HLL0 0x${HELLO_MAGIC.toString(16)})` };
  }
  if (ver !== PROTO_VERSION) {
    return { ok: false, magic, ver, reason: `protocol version ${ver} != ${PROTO_VERSION} (refusing to trust stream)` };
  }
  return { ok: true, magic, ver };
}

const URL = process.argv[2] ?? "ws://127.0.0.1:8145";
const ws = new WebSocket(URL);
ws.binaryType = "arraybuffer";
let done = false;
const finish = (code: number) => { if (done) return; done = true; try { ws.close(); } catch {} process.exit(code); };
ws.onerror = () => { console.error(`HELLO-GATE FAIL: cannot connect ${URL}`); finish(1); };
ws.onmessage = (ev: MessageEvent) => {
  const buf = ev.data as ArrayBuffer;
  const m = new DataView(buf).getUint32(0, LE);
  if (m === HELLO_MAGIC) {
    const id = verifyHello(buf);
    console.log(`HELLO frame: magic=0x${id.magic.toString(16)} ver=${id.ver} ok=${id.ok}` + (id.reason ? ` reason=${id.reason}` : ""));
    if (id.ok) { console.log("=== S0 HELLO IDENTITY GATE: PASS (live v3 verified against the wire) ==="); finish(0); }
    else { console.error(`=== S0 HELLO IDENTITY GATE: FAIL — ${id.reason} ===`); finish(1); }
  } else if (m === TLM_MAGIC) {
    // HELLO must be the FIRST frame; if TLM arrives first the gate is violated
    console.error("HELLO-GATE FAIL: TLM arrived before any HELLO (identity gate would reject)");
    finish(1);
  }
};
setTimeout(() => { console.error("HELLO-GATE FAIL: no HELLO in 15s"); finish(1); }, 15000);
