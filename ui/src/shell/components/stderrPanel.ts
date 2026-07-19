// stderrPanel.ts — sidecar STDERR panel (brainstorm §2; understory Tier-3
// "sidecar stderr tail ... a failed launch is opaque" without it).
//
// Surfaces the core's stderr — the readiness line, the "streaming @125 Hz"
// marker, and crucially the actual failure cause on a bad launch (bind failure,
// bad scenario, etc.). Fed live via the control plane (core://stderr) and
// seeded once from get_stderr_tail on mount. In a plain browser there is no
// control plane, so the panel shows an honest "no control plane" note.

import type { bridge as Bridge } from "../tauriBridge";

const MAX_ROWS = 400;

export interface StderrHandle {
  root: HTMLElement;
  push(line: string): void;
  seed(text: string): void;
  toggle(): void;
  visible(): boolean;
}

export function createStderrPanel(bridge: typeof Bridge): StderrHandle {
  const root = document.createElement("div");
  root.className = "lz-panel lz-stderr";
  root.style.display = "none";

  const header = document.createElement("div");
  header.className = "lz-panel__header";
  header.textContent = "SIDECAR STDERR · booster-core --serve";

  const body = document.createElement("div");
  body.className = "lz-panel__body";

  root.append(header, body);

  if (!bridge.available) {
    const row = document.createElement("div");
    row.className = "lz-stderr__row lz-stderr__row--muted";
    row.textContent = "(no control plane — sidecar stderr is only captured inside the Tauri shell)";
    body.appendChild(row);
  }

  let shown = false;

  function append(line: string, muted = false) {
    const row = document.createElement("div");
    row.className = "lz-stderr__row" + (muted ? " lz-stderr__row--muted" : "");
    row.textContent = line;
    body.appendChild(row);
    while (body.childElementCount > MAX_ROWS) body.removeChild(body.firstChild!);
    if (shown) body.scrollTop = body.scrollHeight;
  }

  return {
    root,
    push(line: string) {
      append(line);
    },
    seed(text: string) {
      if (!text) return;
      for (const line of text.split("\n")) if (line) append(line, true);
    },
    toggle() {
      shown = !shown;
      root.style.display = shown ? "flex" : "none";
      if (shown) body.scrollTop = body.scrollHeight;
    },
    visible() {
      return shown;
    },
  };
}
