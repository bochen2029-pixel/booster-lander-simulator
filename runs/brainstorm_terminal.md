# Brainstorm mining: C:\TERMINAL → booster-lander 3D frontend

Read-only distillation of the TERMINAL project (Tauri + React CRT terminal), filtered for
what it contributes to a true-3D graphics GUI for the Booster Lander Simulator (deterministic
6-DOF sim, one-way binary WebSocket telemetry @125 Hz, renderer = pure observer, cinematic-
maximalism three.js/WebGPU target).

---

## 1. WHAT TERMINAL IS

- **Purpose:** a standalone desktop GUI chat client for local LLMs, rendering a 1980s–90s
  monochrome CRT-terminal aesthetic. Speaks OpenAI-compatible HTTP; ships pointed at LM Studio
  (`localhost:1234/v1`). Ultimate target was to be the face of "Vera" (a persona/harness), but
  it works backend-agnostic. Tagline: *the frontend is the durable artifact; the LLM/harness/
  persona are substrate-replaceable.*
- **Two documents, two ambitions.** `spec.md` (v1.0, ~7.6k words) + `CLAUDE.md` describe a 4–6
  week aspirational build: WebGL2 CRT-Royale shader pipeline, bitmap-font framebuffer, kernel-
  state WebSocket, capability tiers, bezel, discipline enforcement. `SCOPE.md` then *pivoted to
  a weekend MVP* delivering only "generic mode" — and that MVP is what actually exists.
- **Maturity — split brain:**
  - **What actually ships (real code):** a CSS-based CRT chat client. ~1,100 LOC of TS/React.
    Streaming OpenAI SSE, character-paced rendering, phosphor themes, settings modal, slash
    commands, localStorage persistence. **Works, coherent, polished.**
  - **What's vaporware (docs only):** the shader pipeline, bitmap framebuffer, WebSocket kernel
    state, tiers, bezel, discipline log. `src/render/shader/`, `src/render/text/`,
    `src/protocol/` (the WS side) are empty `.gitkeep` dirs. The spec's ambition was never built.
  - **The Rust/Tauri shell is stock boilerplate** — `src-tauri/src/lib.rs` is the default
    `greet()` command and nothing else. No custom IPC, no sidecar, no window logic in Rust.
- **Stack:** Tauri 2 · React 19 · TypeScript 5.8 (strict) · Vite 7 · Zustand 5 · Tailwind 3
  (installed, barely used) · `@fontsource/vt323` webfont. Vitest + ESLint flat config + clippy.

---

## 2. UI PATTERNS (the genuinely reusable layer)

The MVP is a **single full-bleed scrolling surface**, not a docked/panelled layout. Distinctive
pieces worth stealing for a HUD:

- **Theming via CSS custom properties + `data-` attributes on `<html>`.** `crt.css` defines
  `--phosphor-fg / -dim / -faint / -glow / -bg / --error-fg`. Presets (amber/green/white) are
  just attribute selectors (`[data-phosphor="green"]`). `App.tsx` sets
  `document.documentElement.dataset.phosphor = phosphor` in a `useEffect`. **This is exactly the
  right pattern for a telemetry HUD**: one CSS-var palette (nominal / caution / abort colors),
  swapped live by writing a data-attr — no re-render, no prop-drilling.
- **CRT identity is pure CSS** (the MVP's honest compromise vs. the spec's shader): scanlines +
  vignette as `::before`/`::after` overlays with `mix-blend-mode: multiply` and a
  `refresh-flicker` keyframe; `text-shadow` glow layered from the phosphor vars; optional
  "curvature" as a single `perspective()+rotateX()` transform on the content layer only (overlays
  stay rectangular). Cheap, GPU-composited, and directly layerable *over* a WebGL/WebGPU canvas
  as a "CRT-glass" post-look without touching the 3D pipeline.
- **Streaming-data display = a decoupled render clock.** The best idea in the codebase for
  telemetry. In `store.ts::sendMessage`: incoming SSE deltas are pushed char-by-char into a
  `charQueue`; a separate **recursive-`setTimeout` render loop** (`renderTick`) drains the queue
  at a configurable pace with a "burst" size, mutating store state. Ingest rate and display rate
  are fully separated. **Same shape you want for 125 Hz telemetry → a 60 fps render:** decouple
  the WS ingest callback (fill a ring/latest-sample buffer) from the `requestAnimationFrame` draw
  loop; never redraw per-packet. The spec even hard-forbids "buffer-and-burst" collapse (§14.9):
  render cadence must track source cadence, never converge to a fixed replay rate.
- **Command palette = slash commands in the input bar.** No modal palette. `InputBar` detects a
  leading `/`; `store.runCommand` parses `/name args`, dispatches from a `switch`, echoes the
  command as a user msg and the result as a dim `system` msg. Commands include a **live ASCII
  progress bar** rendered from backend JSON (`/context` builds `█████·····` from a ratio) — a
  neat pattern for a text telemetry readout (fuel, throttle) inside a mostly-graphical UI.
- **Status bar** (`StatusBar.tsx`): fixed bottom strip, `justify-content: space-between`,
  model · host · state · keybind-legend. State label has status-driven classes
  (`is-streaming` blinks, `is-error`/`is-disconnected` go red). Directly maps to a booster
  telemetry footer (phase · vehicle · MET · connection).
- **Keyboard model:** dead simple — one global `window.keydown` listener in `App.tsx`. `F2`
  settings, `F11` fullscreen, `Esc` cancels/closes. No chords, no vim modes. Input auto-refocuses
  when it leaves the disabled state. Legend is always shown in the status bar.
- **Boot sequence** (`BootSequence.tsx` + CSS): a timed CRT warm-up (sync-flutter → expanding
  warm-up line → "READY.") on a fixed 2.4 s timer. Pure set-dressing, but the *pattern* (a gated
  intro overlay before the live surface mounts) suits a "range safety / ignition sequence" cold-
  open for the launch cinematic.
- **Modal convention:** backdrop click-to-close + `stopPropagation` on the panel; bordered box
  with double-glow `box-shadow`; option rows are toggle-button groups with an `is-active` class,
  not native selects. Everything is custom-drawn to stay inside the aesthetic (the spec forbids
  native OS dialogs entirely, §14.3).

---

## 3. TAURI SHELL PATTERNS

Reality check: **TERMINAL's Tauri shell is nearly empty**, so most reusable shell knowledge is in
the config + the *spec's plan*, not in shipped Rust.

- **Frameless window done right** (`tauri.conf.json`): `"decorations": false`, 1280×800,
  minWidth 800×600, resizable, `"csp": null`. The missing title bar is replaced by a 24 px
  `.drag-handle` div carrying **`data-tauri-drag-region`** (CSS `-webkit-app-region`-style drag
  via Tauri's built-in attribute — no Rust needed). This is the single most directly-copyable
  shell fact for a borderless cinematic booster window.
- **Fullscreen toggling from the webview** (`App.tsx::toggleFullscreen`): dynamic
  `import("@tauri-apps/api/window")` → `getCurrentWindow().setFullscreen(!fs)`. The dynamic
  import is deliberate so the same bundle **also runs in a plain browser** (`npm run dev` at
  `localhost:1420`) where Tauri APIs are absent — the import throws and is swallowed. Good dual-
  target dev ergonomics: build the booster UI so it runs in-browser against a WS mock and in
  Tauri for the shipped app.
- **Vite/Tauri wiring** (`vite.config.ts`): fixed `strictPort: 1420`, `clearScreen: false` (don't
  hide Rust errors), `watch.ignored: ['**/src-tauri/**']`, `@ → ./src` alias. Standard, works.
- **IPC:** only the stock `#[tauri::command] greet`. **No real Rust↔web IPC exists to copy.** For
  the booster, note: with **one-way binary telemetry, you likely don't need Rust IPC at all** —
  the webview can open the `ws://` socket directly (native `WebSocket` + `arraybuffer`), exactly
  as TERMINAL's frontend hits HTTP directly. Rust only earns its place if you want it to *own* the
  socket/process.
- **Sidecar / process supervision: ABSENT in code, but the spec designs the contract.** The
  intended shape (spec §5, §9) is worth lifting: frontend probes a backend, subscribes to a
  push socket, degrades visibly on socket loss with exponential backoff (2/4/8…30 s), and re-
  probes on reconnect. If you want Tauri to *launch the sim binary* and stream its telemetry,
  that's a Tauri **sidecar** (`tauri.conf.json > bundle > externalBin` + Rust `Command` spawn) —
  TERMINAL doesn't implement it, so treat that as net-new.
- **Packaging:** `bundle.targets: "all"`, icon set present, `beforeBuildCommand: npm run build`,
  `frontendDist: ../dist`. Nothing bespoke; the default `tauri build` installer path.

---

## 4. HOUSE-STYLE SIGNALS (how this operator builds frontends)

Strong, consistent signals across spec + code + doctrine:

- **Spec-first, contracts-first, terminology-policed.** `spec.md` mandates a glossary with
  *forbidden synonyms* ("bezel" not "frame"; "operator" not "user") and TypeScript/Rust interface
  contracts written *before* implementation (`src/contracts/`). Modules declare explicit
  dependency edges. Expect the same rigor demanded of the booster frontend: name things once,
  precisely, and freeze the telemetry-packet contract as the load-bearing interface.
- **"Substrate honesty" / renderer-is-observer instinct.** TERMINAL insists the frontend never
  mutates backend truth (kernel-state writes are *architecturally forbidden*; the WS is push-only,
  frontend never sends). **This is the identical philosophy to the booster's "renderer = pure
  observer, one-way telemetry"** — the operator already thinks this way and will expect the 3D UI
  to hold it.
- **Determinism & anti-drift discipline.** `CLAUDE.md` + spec §14 ("What Will Kill This Build")
  enumerate failure modes with named defenses: no hidden state, no silent default changes (any
  default change = major version bump + migration), regression tests over visual presets,
  versioned protocol contracts. The operator codifies "don't let this rot" rules up front.
- **Performance budgets are explicit and hard.** Spec §13: <2 ms shader frame @4K (60 fps hard
  ceiling), zero memory-leak tolerance over 8 h sessions, RAM/CPU/GPU/bundle ceilings tabulated.
  A cinematic booster UI will be held to concrete frame-time and long-run-stability numbers.
- **Stack preferences:** Tauri (Rust core) over Electron; React functional + hooks, **Zustand**
  for state (single `create` store with `persist` middleware, selectors, module-scoped mutable
  refs for the render loop), strict TS with `noUncheckedIndexedAccess`-grade care, CSS custom
  properties for theming, webfont bundled (offline). ESLint flat + clippy `deny`,
  `#![forbid(unsafe_code)]`. Aesthetic maximalism is a *first-class requirement*, not polish —
  but it is disciplined by toggles, presets, and a flat/default baseline.
- **Pragmatic scope-cutting.** `SCOPE.md` shows the operator will consciously ship a "weekend
  MVP first, rewrite later" pass, explicitly listing what's OUT. They value a working spine over a
  complete cathedral — relevant if the booster 3D UI should land an observable-telemetry MVP
  before the full cinematic pipeline.

---

## 5. VERDICT — what TERMINAL contributes to the booster 3D frontend

**Contribution: a handful of concrete frontend patterns + one philosophy match. NOT a shell to
fork, NOT a panel system, NOT any 3D code.**

Take these, leave the rest:

1. **The decoupled ingest→render clock** (`charQueue` + `renderTick`). Reshape it: WS binary
   telemetry callback fills a latest-sample / ring buffer; a `requestAnimationFrame` loop reads
   it and drives the three.js scene + HUD. This is the single most valuable transferable idea, and
   it's already the operator's own pattern. Honor spec §14.9 (no buffer-and-burst).
2. **CSS-var + `data-attr` theming** for the HUD palette (nominal/caution/abort), swapped live.
3. **The CSS "CRT-glass" overlay stack** (scanlines/vignette/flicker/glow as blend-mode layers +
   optional perspective warp) as a cheap cinematic post-look *layered over* the WebGPU canvas —
   independent of, and complementary to, real 3D post-processing.
4. **Frameless-window shell facts:** `decorations:false` + `data-tauri-drag-region`, webview-side
   fullscreen via dynamic `@tauri-apps/api/window` import, dual browser/Tauri run targets, fixed
   Vite port. Copy the *config*, not any Rust (there is none to copy).
5. **Status-footer + slash-command + ASCII-gauge idioms** for the textual telemetry layer that
   coexists with the graphics (MET, phase, throttle bar, event log).
6. **The push-socket resilience contract from the spec** (visible degrade on loss, exponential
   backoff, re-probe on reconnect, `SYNC LOSS` indicator) — implement it; TERMINAL only specced it.

**Deliberately do NOT take:** the (nonexistent) shader pipeline, bitmap-framebuffer, capability-
tier machinery, LLM/OpenAI protocol code, discipline log, or the Rust shell (stock boilerplate).
None address the booster's real problem — a high-rate binary telemetry feed driving a 3D scene.

**Strongest strategic parallel:** TERMINAL's kernel-state model — a backend pushes a fixed-schema
state struct at a steady rate (~1 Hz PULSE) and the frontend *smoothly interpolates that state
into continuous visual modulation, never abruptly* (spec §5.4.2). Swap 1 Hz kernel-state for
125 Hz 6-DOF telemetry and that is precisely the booster HUD/scene-driver architecture: a
push-only state stream, interpolated (not snapped) onto a smooth render. The operator has already
designed this once — reuse the mental model, not the code.
