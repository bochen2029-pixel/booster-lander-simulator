// mount.ts — the single entry point that mounts the LZ-COCKPIT shell chrome.
//
// DESIGN: this module is self-contained. `main.ts` adds exactly two lines — an
// import and a `mountShell(...)` call — and feeds the shell its raw frames via
// the returned handle (frame tap for the wire log + HELLO identity gate + TLM
// liveness). Everything else (chip, picker, panels, supervisor wiring, dual-
// target degradation) lives here so sibling modules stay untouched.
//
// Control-plane vs telemetry split (canon §10.1): the shell chrome talks to the
// supervisor through Tauri invokes/events (lifecycle only); the vehicle stream
// stays on the direct WebSocket that main.ts already owns.

import { bridge } from "./tauriBridge";
import { CockpitStateMachine } from "./state";
import { verifyHello } from "./hello";
import { injectShellCss } from "./shellCss";
import { createConnectionChip } from "./components/connectionChip";
import { createPicker } from "./components/picker";
import { createWireLog } from "./components/wireLog";
import { createStderrPanel } from "./components/stderrPanel";
import { frameMagic, TLM_MAGIC, HELLO_MAGIC } from "../net/decode";

export interface ShellHandle {
  /** The fused connection state machine (chrome-only; owns no vehicle truth). */
  machine: CockpitStateMachine;
  /** Resolved telemetry port (supervisor-chosen in Tauri, default in browser). */
  port: number;
  /** The WS URL the telemetry client should open. */
  wsUrl: string;
  /** Feed EVERY raw frame here (pre-decode) — drives wire log + HELLO gate + liveness. */
  onFrame(buf: ArrayBuffer): void;
  /** Socket lifecycle hooks for the chip's fusion logic. */
  onSocketOpen(): void;
  onSocketClose(): void;
}

const DEFAULT_PORT = 8787;

/**
 * Mount the cockpit chrome and resolve the port to stream on.
 *
 * In Tauri: asks the supervisor for the chosen port (get_core_port), subscribes
 * to core://state / core://exit / core://stderr, seeds the stderr panel.
 * In a browser: uses the default port and shows read-only chrome.
 */
export async function mountShell(): Promise<ShellHandle> {
  injectShellCss();

  const hasControlPlane = bridge.available;

  // Resolve the port + initial launch identity from the supervisor if present.
  let port = DEFAULT_PORT;
  let scenario = "entry";
  let seed = 42;
  let run = 1;
  if (hasControlPlane) {
    const status = await bridge.getCoreStatus();
    if (status) {
      port = status.port || DEFAULT_PORT;
      scenario = status.launch.scenario;
      seed = status.launch.seed;
      run = status.launch.run;
    } else {
      const p = await bridge.getCorePort();
      if (p) port = p;
    }
  }

  const machine = new CockpitStateMachine({ port, hasControlPlane, scenario, seed, run });

  // --- build chrome ---
  const strip = document.createElement("div");
  strip.className = "lz-topstrip";

  const chip = createConnectionChip(machine, bridge);
  const picker = createPicker(machine, bridge);
  const spacer = document.createElement("div");
  spacer.className = "lz-spacer";

  const wireLog = createWireLog();
  const stderrPanel = createStderrPanel(bridge);

  const toggles = document.createElement("div");
  toggles.className = "lz-toggles";
  const wireBtn = toggleButton("WIRE", () => {
    wireLog.toggle();
    wireBtn.classList.toggle("lz-toggle--active", wireLog.visible());
  });
  const errBtn = toggleButton("STDERR", () => {
    stderrPanel.toggle();
    errBtn.classList.toggle("lz-toggle--active", stderrPanel.visible());
  });
  toggles.append(errBtn, wireBtn);

  strip.append(chip, picker, spacer, toggles);
  document.body.append(strip, wireLog.root, stderrPanel.root);

  // --- supervisor wiring (Tauri only) ---
  if (hasControlPlane) {
    // Sync any state that changed before our listener attached.
    const status = await bridge.getCoreStatus();
    if (status) machine.onSupervisorState(status);
    const tail = await bridge.getStderrTail();
    if (tail) stderrPanel.seed(tail);

    await bridge.onState((p) => machine.onSupervisorState(p));
    await bridge.onStderr((line) => stderrPanel.push(line));
    await bridge.onExit((p) => {
      // core://exit is corroborated by the socket close; the state machine's
      // onSocketClose handles the chip. Log for the transcript.
      console.info("[shell] core exited", p);
    });
  }

  const wsUrl = `ws://127.0.0.1:${port}`;

  return {
    machine,
    port,
    wsUrl,
    onFrame(buf: ArrayBuffer) {
      // 1) wire log (cheap no-op while hidden)
      wireLog.push(buf);
      // 2) identity gate on the first HELLO
      const magic = frameMagic(buf);
      if (magic === HELLO_MAGIC && !machine.snapshot.helloVerified) {
        const id = verifyHello(buf);
        if (id.ok) machine.onHelloVerified();
        else machine.onHelloRejected(id.reason ?? "unknown");
      }
      // 3) TLM liveness → STREAMING
      if (magic === TLM_MAGIC) machine.onTlm();
    },
    onSocketOpen() {
      machine.onSocketOpen();
    },
    onSocketClose() {
      machine.onSocketClose();
    },
  };
}

function toggleButton(label: string, onClick: () => void): HTMLButtonElement {
  const b = document.createElement("button");
  b.className = "lz-toggle";
  b.textContent = label;
  b.addEventListener("click", onClick);
  return b;
}
