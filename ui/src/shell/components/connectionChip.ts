// connectionChip.ts — the top-strip CONNECTION CHIP (brainstorm §2).
//
// Honest state machine readout: SPAWNING -> HEALTHY -> STREAMING -> LOST, plus
// COMPLETE (clean verdict) and FAILED. Shows the live launch identity, the v3
// verification tick, the port, a frame counter, a "last-good frame held" note
// during LOST (FIGUREHEAD ViewCache pattern), and a RELIGHT button that re-spawns
// the sidecar via the control plane. In a plain browser (no control plane) the
// RELIGHT button is hidden and the chip is a pure read-only observer.

import type { CockpitStateMachine, CockpitState, ConnState } from "../state";
import type { bridge as Bridge } from "../tauriBridge";

interface ChipStyle {
  dot: string;
  label: string;
}

const STYLES: Record<ConnState, ChipStyle> = {
  SPAWNING: { dot: "#c9a03a", label: "SPAWNING" },
  HEALTHY: { dot: "#3aa0c9", label: "HEALTHY" },
  STREAMING: { dot: "#4fd08a", label: "STREAMING" },
  LOST: { dot: "#d0574f", label: "LOST" },
  COMPLETE: { dot: "#8a8f9a", label: "COMPLETE" },
  FAILED: { dot: "#d0574f", label: "FAILED" },
};

export function createConnectionChip(
  machine: CockpitStateMachine,
  bridge: typeof Bridge
): HTMLElement {
  const root = document.createElement("div");
  root.className = "lz-chip";
  root.setAttribute("role", "status");

  const dot = document.createElement("span");
  dot.className = "lz-chip__dot";

  const label = document.createElement("span");
  label.className = "lz-chip__label";

  const ident = document.createElement("span");
  ident.className = "lz-chip__ident";

  const ver = document.createElement("span");
  ver.className = "lz-chip__ver";

  const note = document.createElement("span");
  note.className = "lz-chip__note";

  const relight = document.createElement("button");
  relight.className = "lz-chip__relight";
  relight.textContent = "RELIGHT";
  relight.title = "Re-spawn the sidecar for the current scenario/seed/run";
  relight.style.display = "none";
  relight.addEventListener("click", async () => {
    if (!bridge.available) return;
    relight.disabled = true;
    relight.textContent = "…";
    const s = machine.snapshot;
    machine.beginRelaunch(s.scenario, s.seed, s.run);
    await bridge.relight();
    // The supervisor's core://state events drive the chip from here; re-enable
    // shortly so the user can retry if the spawn itself fails.
    setTimeout(() => {
      relight.disabled = false;
      relight.textContent = "RELIGHT";
    }, 1200);
  });

  root.append(dot, label, ident, ver, note, relight);

  machine.subscribe((s: Readonly<CockpitState>) => {
    const st = STYLES[s.conn];
    dot.style.background = st.dot;
    dot.classList.toggle("lz-chip__dot--pulse", s.conn === "SPAWNING" || s.conn === "STREAMING");
    label.textContent = st.label;
    label.style.color = st.dot;

    ident.textContent = `${s.scenario} s${s.seed} r${s.run} · :${s.port}`;

    ver.textContent = s.helloVerified ? "v3 ✓" : "v3 —";
    ver.style.color = s.helloVerified ? "#4fd08a" : "#8a8f9a";

    // Honest degraded-mode note.
    if (s.conn === "LOST") {
      const held = s.lastTlmAt ? " · last-good frame held" : "";
      note.textContent = `stream lost${held}`;
      note.style.color = "#d0895f";
    } else if (s.conn === "FAILED") {
      note.textContent = s.error ? `— ${truncate(s.error, 80)}` : "— launch failed";
      note.style.color = "#d0574f";
    } else if (s.conn === "COMPLETE") {
      note.textContent = "run complete";
      note.style.color = "#8a8f9a";
    } else if (s.conn === "STREAMING") {
      note.textContent = `${s.frameCount} frames`;
      note.style.color = "#5a6472";
    } else {
      note.textContent = "";
    }

    // RELIGHT is offered only when we're in a recoverable dead state AND we have
    // a control plane to act through.
    const recoverable = s.conn === "LOST" || s.conn === "FAILED" || s.conn === "COMPLETE";
    relight.style.display = bridge.available && recoverable ? "inline-block" : "none";
    if (!recoverable) {
      relight.disabled = false;
      relight.textContent = "RELIGHT";
    }
  });

  return root;
}

function truncate(s: string, n: number): string {
  return s.length > n ? s.slice(0, n - 1) + "…" : s;
}
