// state.ts — the cockpit CONNECTION state machine (brainstorm §2 top-strip,
// §1 "supervised stream recovery"; FIGUREHEAD ViewCache degraded-mode).
//
// Two truth sources fuse here:
//   * The SUPERVISOR (Rust, via core://state) — did the sidecar spawn / bind /
//     stream / exit? Only present in Tauri.
//   * The SOCKET (webview WebSocket) — did WE connect, verify HELLO v3, and are
//     TLM frames arriving? Always present (this is the browser-only truth too).
//
// The displayed ConnState is a fusion, deliberately biased toward the SOCKET for
// the live states (the socket is what the renderer actually consumes) and toward
// the SUPERVISOR for lifecycle framing (SPAWNING / crashed / complete).
//
//   SPAWNING  — sidecar launching, or socket not yet open
//   HEALTHY   — socket open + HELLO v3 verified, but no TLM flowing yet
//   STREAMING — TLM frames arriving (the good state)
//   LOST      — socket dropped / sidecar exited unexpectedly; last-good frame held
//   COMPLETE  — sidecar ran a full serve to a verdict and exited (NOT an error)
//   FAILED    — sidecar couldn't spawn / never became ready (readable error)
//
// Renderer-owns-no-truth still holds: this machine drives only CHROME (the chip,
// the RELIGHT affordance). It never touches vehicle state.

import type { CorePhase, CoreStatePayload } from "./tauriBridge";

export type ConnState =
  | "SPAWNING"
  | "HEALTHY"
  | "STREAMING"
  | "LOST"
  | "COMPLETE"
  | "FAILED";

export interface CockpitState {
  conn: ConnState;
  /** Last supervised phase from Rust (null in a plain browser). */
  supPhase: CorePhase | null;
  /** True once a HELLO v3 was verified on the current socket. */
  helloVerified: boolean;
  /** Wall-clock ms of the last TLM frame seen (for the "last-good-frame hold" note). */
  lastTlmAt: number | null;
  /** Current telemetry port (from the supervisor, or the default). */
  port: number;
  /** Current launch identity for the chip label. */
  scenario: string;
  seed: number;
  run: number;
  /** Readable error, when FAILED/LOST. */
  error: string;
  /** True when the control plane (Tauri) is present. */
  hasControlPlane: boolean;
  /** Frames counted on the current socket (chip + wire-log corroboration). */
  frameCount: number;
}

export type StateListener = (s: Readonly<CockpitState>) => void;

/** How long without a TLM frame before a STREAMING socket is considered stalled. */
const TLM_STALL_MS = 800;

export class CockpitStateMachine {
  private s: CockpitState;
  private listeners = new Set<StateListener>();
  private stallTimer: number | null = null;

  constructor(opts: { port: number; hasControlPlane: boolean; scenario: string; seed: number; run: number }) {
    this.s = {
      conn: "SPAWNING",
      supPhase: null,
      helloVerified: false,
      lastTlmAt: null,
      port: opts.port,
      scenario: opts.scenario,
      seed: opts.seed,
      run: opts.run,
      error: "",
      hasControlPlane: opts.hasControlPlane,
      frameCount: 0,
    };
  }

  get snapshot(): Readonly<CockpitState> {
    return this.s;
  }

  subscribe(fn: StateListener): () => void {
    this.listeners.add(fn);
    fn(this.s); // fire immediately with current state
    return () => this.listeners.delete(fn);
  }

  private emit(): void {
    for (const fn of this.listeners) fn(this.s);
  }

  private set(patch: Partial<CockpitState>): void {
    const next = { ...this.s, ...patch };
    // Only emit on a meaningful change to avoid chip thrash.
    const changed = (Object.keys(patch) as (keyof CockpitState)[]).some((k) => this.s[k] !== next[k]);
    this.s = next;
    if (changed) this.emit();
  }

  // --- SUPERVISOR side (Tauri core://state) -------------------------------
  onSupervisorState(p: CoreStatePayload): void {
    const patch: Partial<CockpitState> = {
      supPhase: p.phase,
      port: p.port,
      scenario: p.launch.scenario,
      seed: p.launch.seed,
      run: p.launch.run,
      error: p.error || this.s.error,
    };
    // Map supervisor lifecycle to the chip where the socket can't speak for it.
    switch (p.phase) {
      case "SPAWNING":
      case "RESPAWNING":
        // A fresh spawn invalidates the previous socket's HELLO.
        patch.conn = "SPAWNING";
        patch.helloVerified = false;
        patch.error = "";
        break;
      case "READY":
        // Bound + waiting for us. If the socket hasn't opened yet, stay SPAWNING;
        // the socket callbacks will advance to HEALTHY/STREAMING.
        if (!this.s.helloVerified) patch.conn = "SPAWNING";
        break;
      case "STREAMING":
        // Supervisor sees a client; our own socket state is authoritative for the
        // chip, so don't force it here (the socket will report STREAMING).
        break;
      case "EXITED_COMPLETE":
        patch.conn = "COMPLETE";
        break;
      case "CRASHED":
        patch.conn = "LOST";
        break;
      case "FAILED":
        patch.conn = "FAILED";
        break;
    }
    this.set(patch);
  }

  // --- SOCKET side (webview WebSocket) ------------------------------------
  onSocketOpen(): void {
    // Open but not yet trusted — wait for HELLO v3 before HEALTHY.
    this.set({ error: "" });
  }

  onHelloVerified(): void {
    this.set({ helloVerified: true, conn: this.s.frameCount > 0 ? "STREAMING" : "HEALTHY" });
  }

  onHelloRejected(reason: string): void {
    // Identity gate failed — do not trust the stream.
    this.set({ conn: "FAILED", helloVerified: false, error: `identity gate: ${reason}` });
  }

  onTlm(): void {
    const now = Date.now();
    this.set({
      lastTlmAt: now,
      frameCount: this.s.frameCount + 1,
      conn: this.s.helloVerified ? "STREAMING" : this.s.conn,
    });
    this.armStallTimer();
  }

  onSocketClose(): void {
    // Socket dropped. If the supervisor already told us the run completed, keep
    // COMPLETE; otherwise this is a LOST (hold last-good frame, offer RELIGHT).
    if (this.s.supPhase === "EXITED_COMPLETE") {
      this.set({ conn: "COMPLETE" });
    } else if (this.s.conn !== "FAILED") {
      this.set({ conn: "LOST" });
    }
    this.clearStallTimer();
  }

  /** Reset for a fresh launch (picker/relight) initiated from the webview. */
  beginRelaunch(scenario: string, seed: number, run: number): void {
    this.set({
      conn: "SPAWNING",
      helloVerified: false,
      lastTlmAt: null,
      frameCount: 0,
      error: "",
      scenario,
      seed,
      run,
    });
  }

  private armStallTimer(): void {
    this.clearStallTimer();
    this.stallTimer = setTimeout(() => {
      // No TLM for TLM_STALL_MS while we think we're streaming → degrade to LOST
      // (unless the supervisor says the run completed cleanly).
      if (this.s.conn === "STREAMING") {
        if (this.s.supPhase === "EXITED_COMPLETE") this.set({ conn: "COMPLETE" });
        else this.set({ conn: "LOST" });
      }
    }, TLM_STALL_MS) as unknown as number;
  }

  private clearStallTimer(): void {
    if (this.stallTimer !== null) {
      clearTimeout(this.stallTimer);
      this.stallTimer = null;
    }
  }
}
