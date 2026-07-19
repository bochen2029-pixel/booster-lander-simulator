// client.ts — the socket client (canon §10.1, §11.2).
//
// The webview connects DIRECTLY to core --serve (ws://127.0.0.1:8787), no relay
// hop through Rust (canon §10.1: "the shell only spawns and supervises"). Binary
// frames only; we route by the 4-byte magic and feed TLM frames to the interp
// buffer. HELLO is captured once. Reconnects with backoff (the sidecar may still
// be starting when the webview loads).

import { frameMagic, decodeTlm, TLM_MAGIC, HELLO_MAGIC, EVT_MAGIC, STATS_MAGIC } from "./decode";
import type { TlmFrame } from "./decode";

export interface ClientHandlers {
  onTlm?: (f: TlmFrame) => void;
  onHelloBytes?: (buf: ArrayBuffer) => void; // decoded by hello.ts (geometry block)
  onEvtBytes?: (buf: ArrayBuffer) => void;
  onStatsBytes?: (buf: ArrayBuffer) => void;
  onOpen?: () => void;
  onClose?: () => void;
  // Fires for EVERY binary frame BEFORE routing/decoding (raw bytes). Used by the
  // LZ-COCKPIT shell for the wire-log panel + HELLO identity gate + TLM liveness,
  // without disturbing the hot decode path (canon §10.1: chrome observes, never
  // owns truth). Optional and additive — leaving it unset is a no-op.
  onRawFrame?: (buf: ArrayBuffer) => void;
}

export class TelemetryClient {
  private ws: WebSocket | null = null;
  private url: string;
  private handlers: ClientHandlers;
  private backoffMs = 250;
  private closedByUser = false;

  constructor(handlers: ClientHandlers, url = "ws://127.0.0.1:8787") {
    this.url = url;
    this.handlers = handlers;
  }

  connect(): void {
    this.closedByUser = false;
    const ws = new WebSocket(this.url);
    ws.binaryType = "arraybuffer";
    this.ws = ws;

    ws.onopen = () => {
      this.backoffMs = 250;
      this.handlers.onOpen?.();
    };
    ws.onmessage = (ev) => {
      if (!(ev.data instanceof ArrayBuffer)) return; // no JSON on the hot path
      this.handlers.onRawFrame?.(ev.data); // shell tap (wire log / identity gate)
      this.route(ev.data);
    };
    ws.onclose = () => {
      this.handlers.onClose?.();
      if (!this.closedByUser) {
        setTimeout(() => this.connect(), this.backoffMs);
        this.backoffMs = Math.min(this.backoffMs * 1.7, 3000);
      }
    };
    ws.onerror = () => ws.close();
  }

  private route(buf: ArrayBuffer): void {
    switch (frameMagic(buf)) {
      case TLM_MAGIC:
        if (this.handlers.onTlm) this.handlers.onTlm(decodeTlm(buf));
        break;
      case HELLO_MAGIC:
        this.handlers.onHelloBytes?.(buf);
        break;
      case EVT_MAGIC:
        this.handlers.onEvtBytes?.(buf);
        break;
      case STATS_MAGIC:
        this.handlers.onStatsBytes?.(buf);
        break;
      default:
        // unknown magic — ignore (forward-compat)
        break;
    }
  }

  /** Upstream command channel (canon §10.6, closed enumeration). Bytes only. */
  send(bytes: ArrayBuffer): void {
    if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(bytes);
  }

  close(): void {
    this.closedByUser = true;
    this.ws?.close();
  }
}
