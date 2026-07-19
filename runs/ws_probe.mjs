// ws_probe.mjs — minimal RFC6455 client (Node built-ins only) to sanity-check the
// v4 telemetry wire (D-020): connect to booster-core --serve, validate HELLO.ver==4
// (80 B, world_hash), then decode the first TLM frame's pred_impact[2]+ignite_h and
// the v4 wide-socket fields (target_est @232, n_eng @269, tails @324) at their offsets.
// Not a golden — a live-wire smoke test (mission item 6). Exits after 1 TLM.
import net from "node:net";
import crypto from "node:crypto";

const PORT = Number(process.argv[2] || 8080);
const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const key = crypto.randomBytes(16).toString("base64");
const sock = net.connect(PORT, "127.0.0.1");
let handshook = false;
let buf = Buffer.alloc(0);
let gotHello = false, gotTlm = false;

const fail = (m) => { console.error("PROBE-FAIL:", m); process.exit(1); };
setTimeout(() => fail("timeout waiting for HELLO+TLM"), 15000);

sock.on("connect", () => {
  sock.write(
    `GET / HTTP/1.1\r\nHost: 127.0.0.1:${PORT}\r\nUpgrade: websocket\r\n` +
    `Connection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`
  );
});

sock.on("data", (chunk) => {
  buf = Buffer.concat([buf, chunk]);
  if (!handshook) {
    const idx = buf.indexOf("\r\n\r\n");
    if (idx < 0) return;
    const hdr = buf.slice(0, idx).toString();
    const accept = crypto.createHash("sha1").update(key + GUID).digest("base64");
    if (!hdr.includes(`Sec-WebSocket-Accept: ${accept}`)) fail("bad Accept in handshake");
    handshook = true;
    buf = buf.slice(idx + 4);
  }
  // parse unmasked server frames
  while (buf.length >= 2) {
    const op = buf[0] & 0x0f;
    let len = buf[1] & 0x7f;
    let off = 2;
    if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
    else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); off = 10; }
    if (buf.length < off + len) return;
    const payload = buf.slice(off, off + len);
    buf = buf.slice(off + len);
    if (op !== 0x2) continue; // only binary
    const magic = payload.readUInt32LE(0);
    const ver = payload.readUInt16LE(4);
    if (magic === 0x304c4c48) { // HELLO
      if (ver !== 4) fail(`HELLO.ver=${ver} expected 4`); // v4 since D-020 (was 72B v3)
      if (payload.length !== 80) fail(`HELLO size ${payload.length} expected 80`);
      const worldHash = payload.readUInt32LE(72) >>> 0;
      console.log(`HELLO ok: ver=${ver}, size=${payload.length}, world_hash=0x${worldHash.toString(16).toUpperCase()}`);
      gotHello = true;
    } else if (magic === 0x304d4c54) { // TLM
      if (ver !== 4) fail(`TLM.ver=${ver} expected 4`);
      const rx = payload.readFloatLE(32), ry = payload.readFloatLE(36);
      const vx = payload.readFloatLE(44), vy = payload.readFloatLE(48);
      const tgo = payload.readFloatLE(212);
      const pix = payload.readFloatLE(220), piy = payload.readFloatLE(224);
      const igh = payload.readFloatLE(228);
      // v4 wide-socket group (D-020 offsets): target estimate @232, eng health @268
      const tex = payload.readFloatLE(232), tey = payload.readFloatLE(236);
      const engN = payload.readUInt8(269);
      const planN = payload.readUInt16LE(324), cloudN = payload.readUInt16LE(326);
      // independent recompute of the v1 formula to prove the field is what we documented
      const expx = rx + vx * Math.max(0, Math.min(60, tgo));
      const expy = ry + vy * Math.max(0, Math.min(60, tgo));
      const okx = Math.abs(expx - pix) < 1e-2, oky = Math.abs(expy - piy) < 1e-2;
      console.log(`TLM ok: ver=${ver}, fixed=328, plan_n=${planN}, cloud_n=${cloudN}`);
      console.log(`  t_go=${tgo.toFixed(3)}  pred_impact=[${pix.toFixed(3)}, ${piy.toFixed(3)}]  ignite_h=${igh.toFixed(2)}`);
      console.log(`  target_est=[${tex.toFixed(3)}, ${tey.toFixed(3)}]  n_eng=${engN}`);
      console.log(`  formula check r+v*t_go=[${expx.toFixed(3)}, ${expy.toFixed(3)}]  match=${okx && oky}`);
      if (!(okx && oky)) fail("pred_impact != r+v*t_go");
      gotTlm = true;
    }
    if (gotHello && gotTlm) {
      console.log("PROBE-OK: HELLO+TLM parsed with v4 fields");
      sock.end();
      process.exit(0);
    }
  }
});
sock.on("error", (e) => fail("socket error: " + e.message));
