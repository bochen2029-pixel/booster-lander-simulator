// wireLog.ts — raw WIRE LOG panel (brainstorm §2 top-strip toggles;
// understory Tier-3 "raw wire log (hex frames)").
//
// "It feels slow / wrong" must always have bytes behind it. This panel shows a
// rolling tail of the raw frames as they arrive on the socket: kind (from the
// 4-byte magic), byte length, and a hex head. It is fed by a tap the client
// exposes (onFrameTap) BEFORE decode, so it shows exactly what came off the wire
// — including a frame that fails to decode. Toggle to keep the hot path cheap.

import { frameMagic, TLM_MAGIC, HELLO_MAGIC, EVT_MAGIC, STATS_MAGIC } from "../../net/decode";

const KIND: Record<number, string> = {
  [TLM_MAGIC]: "TLM",
  [HELLO_MAGIC]: "HELLO",
  [EVT_MAGIC]: "EVT",
  [STATS_MAGIC]: "STATS",
};

const MAX_ROWS = 200;
const HEX_HEAD = 16; // bytes of hex to show per frame

export interface WireLogHandle {
  root: HTMLElement;
  /** Feed one raw frame. Cheap no-op while the panel is hidden. */
  push(buf: ArrayBuffer): void;
  toggle(): void;
  visible(): boolean;
}

export function createWireLog(): WireLogHandle {
  const root = document.createElement("div");
  root.className = "lz-panel lz-wirelog";
  root.style.display = "none";

  const header = document.createElement("div");
  header.className = "lz-panel__header";
  header.textContent = "WIRE LOG · raw frames (hex head)";

  const body = document.createElement("div");
  body.className = "lz-panel__body";

  root.append(header, body);

  let shown = false;
  // Throttle DOM writes: TLM arrives at 125 Hz; render at most ~20 rows/sec by
  // coalescing. We keep counts and only append a representative sample + a live
  // "…N more TLM" collapse so the panel stays legible and cheap.
  let pending: string[] = [];
  let sinceTlm = 0;
  let raf = 0;

  function fmt(buf: ArrayBuffer): string {
    const magic = frameMagic(buf);
    const kind = KIND[magic] ?? `0x${magic.toString(16)}`;
    const n = Math.min(HEX_HEAD, buf.byteLength);
    const bytes = new Uint8Array(buf, 0, n);
    let hex = "";
    for (let i = 0; i < n; i++) hex += bytes[i].toString(16).padStart(2, "0") + (i % 4 === 3 ? " " : "");
    return `${kind.padEnd(6)} ${String(buf.byteLength).padStart(4)}B  ${hex.trim()}`;
  }

  function flush() {
    raf = 0;
    if (!shown || pending.length === 0) {
      pending = [];
      return;
    }
    for (const line of pending) {
      const row = document.createElement("div");
      row.className = "lz-wirelog__row";
      row.textContent = line;
      body.appendChild(row);
    }
    pending = [];
    while (body.childElementCount > MAX_ROWS) body.removeChild(body.firstChild!);
    body.scrollTop = body.scrollHeight;
  }

  return {
    root,
    push(buf: ArrayBuffer) {
      if (!shown) return; // hidden → do nothing (keep hot path free)
      const magic = frameMagic(buf);
      if (magic === TLM_MAGIC) {
        // Coalesce dense TLM: show every 12th (~10 Hz) to keep the panel readable.
        sinceTlm++;
        if (sinceTlm % 12 !== 0) return;
      }
      pending.push(fmt(buf));
      if (!raf) raf = requestAnimationFrame(flush);
    },
    toggle() {
      shown = !shown;
      root.style.display = shown ? "flex" : "none";
    },
    visible() {
      return shown;
    },
  };
}
