// tauriBridge.ts — dual-target Tauri access (canon §11.1 "dual browser/Tauri bundle").
//
// The SAME bundle must run in two homes:
//   1. Inside the Tauri webview  → control plane available (invoke/listen): the
//      picker relaunches the sidecar, the chip reflects supervised state, etc.
//   2. A plain browser (`pnpm dev` against a hand-started `core --serve`) → no
//      Tauri; the picker + supervised-state UI degrade to READ-ONLY gracefully.
//
// We therefore DYNAMICALLY import `@tauri-apps/api` (never a static import that
// would hard-fail the bundle in a browser) and detect presence via the injected
// `__TAURI_INTERNALS__` global. Everything below returns null / no-ops when not
// in Tauri, so callers can `if (bridge.available) …` and otherwise show the
// browser fallback.

export type CorePhase =
  | "SPAWNING"
  | "READY"
  | "STREAMING"
  | "EXITED_COMPLETE"
  | "CRASHED"
  | "FAILED"
  | "RESPAWNING";

export interface CoreLaunch {
  scenario: string;
  seed: number;
  run: number;
  /** v2 play-menu disturbance specs (canon §10.6/D-019). Empty/absent = off. */
  gust?: string;
  gustDir?: string;
  engineOut?: string;
  target?: string;
}

/** The optional play-menu disturbances the picker can arm on a relaunch. */
export interface DisturbSpec {
  gust?: string;
  gustDir?: string;
  engineOut?: string;
  target?: string;
}

export interface CoreStatePayload {
  phase: CorePhase;
  port: number;
  launch: CoreLaunch;
  error: string;
}

export interface CoreExitPayload {
  code: number | null;
  streamed: boolean;
}

type UnlistenFn = () => void;

// Minimal structural types for the two @tauri-apps/api surfaces we touch. We do
// NOT `import` the package (it is intentionally absent from the browser bundle
// and the ui package.json), so we declare just enough shape here and load it via
// a variable-specifier dynamic import (which TS types as `any`, and `@vite-ignore`
// keeps out of the browser build graph). This keeps the module dependency-free
// and dual-target without a compile-time coupling to Tauri.
interface TauriCoreApi {
  invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T>;
}
interface TauriEventApi {
  listen<T>(
    event: string,
    handler: (e: { payload: T }) => void
  ): Promise<UnlistenFn>;
}

/** True when running inside a Tauri webview (control plane present). */
export function inTauri(): boolean {
  return typeof (globalThis as Record<string, unknown>).__TAURI_INTERNALS__ !== "undefined";
}

// Lazily-resolved handles to the dynamically imported API surfaces.
let coreApi: TauriCoreApi | null = null;
let eventApi: TauriEventApi | null = null;
let loadPromise: Promise<void> | null = null;

async function ensureLoaded(): Promise<boolean> {
  if (!inTauri()) return false;
  if (coreApi && eventApi) return true;
  if (!loadPromise) {
    loadPromise = (async () => {
      // Variable specifiers so TS won't demand the module at compile time and
      // Vite (with @vite-ignore) won't pre-bundle it in browser mode, where the
      // dep is intentionally absent from the graph at runtime.
      const coreSpec = "@tauri-apps/api/core";
      const eventSpec = "@tauri-apps/api/event";
      coreApi = (await import(/* @vite-ignore */ coreSpec)) as TauriCoreApi;
      eventApi = (await import(/* @vite-ignore */ eventSpec)) as TauriEventApi;
    })().catch((e) => {
      console.warn("[shell] Tauri api import failed; degrading to read-only:", e);
      coreApi = null;
      eventApi = null;
    });
  }
  await loadPromise;
  return !!(coreApi && eventApi);
}

/** Invoke a control-plane command. Returns null (and warns) when not in Tauri. */
export async function invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T | null> {
  if (!(await ensureLoaded()) || !coreApi) return null;
  try {
    return await coreApi.invoke<T>(cmd, args);
  } catch (e) {
    console.warn(`[shell] invoke ${cmd} failed:`, e);
    return null;
  }
}

/** Listen for a lifecycle event. Returns a no-op unlisten when not in Tauri. */
export async function listen<T>(
  event: string,
  handler: (payload: T) => void
): Promise<UnlistenFn> {
  if (!(await ensureLoaded()) || !eventApi) return () => {};
  const un = await eventApi.listen<T>(event, (e) => handler(e.payload));
  return un;
}

/** One-call convenience surface for the shell UI. */
export const bridge = {
  get available() {
    return inTauri();
  },
  getCoreStatus: () => invoke<CoreStatePayload>("get_core_status"),
  getCorePort: () => invoke<number>("get_core_port"),
  getStderrTail: () => invoke<string>("get_stderr_tail"),
  relaunch: (scenario: string, seed: number, run: number, disturb: DisturbSpec = {}) =>
    invoke<number>("relaunch_core", {
      scenario,
      seed,
      run,
      // Tauri v2 maps camelCase invoke args onto snake_case command params.
      gust: disturb.gust ?? "",
      gustDir: disturb.gustDir ?? "",
      engineOut: disturb.engineOut ?? "",
      target: disturb.target ?? "",
    }),
  relight: () => invoke<number>("relight_core"),
  onState: (h: (p: CoreStatePayload) => void) => listen<CoreStatePayload>("core://state", h),
  onExit: (h: (p: CoreExitPayload) => void) => listen<CoreExitPayload>("core://exit", h),
  onStderr: (h: (line: string) => void) => listen<string>("core://stderr", h),
};
