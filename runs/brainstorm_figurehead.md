# Brainstorm distillate — FIGUREHEAD, read for the Booster 3D frontend

**Purpose of this note:** the operator is designing a true-3D cinematic frontend for the
Booster Lander Simulator (deterministic 6-DOF Falcon-9 sim; one-way binary telemetry over
WebSocket @125 Hz; renderer is a **pure observer**; a three.js/WebGPU `ui/` scaffold + a Tauri
`shell/` already exist). This distills `C:\FIGUREHEAD` **only** for what maps onto that job.
Read on 2026-07-18. Nothing outside this file was modified.

---

## 1 · What FIGUREHEAD is

FIGUREHEAD is a **Rust workspace** (19 crates, ~93 tests, 0 warnings, clippy-clean, all offline)
that is *"KEEL's face genome"* — a Windows-native, non-browser UI framework whose thesis is:
**a specialized application is a `face.toml` manifest + a theme, never a fork of the shell.** It is
the UI counterpart of KEEL (the operator's harness genome). Its whole design axis is a
**view-model bus**: a Rust core owns *all* authoritative state and emits `ViewModel`s (core→renderer);
a dumb renderer draws them and emits `Intent`s back (renderer→core). Everything else — surfaces,
manifests, capability scoping, the reversibility gate — hangs off that one contract.

Architecturally it is a strict **layered crate DAG** (`fh-contracts` F0 ← `fh-bus` F1 ← surfaces F4
← `fh-shell`/`fh-tauri` bins). The layer law *is* the dependency graph: a crate may only import
downward, and an up-import is a compile error, not a review note. F0 (`fh-contracts`) is serde-only
and depends on nothing. Two binaries wire it: `fh-shell` (headless console) and `fh-tauri` (a real
WebView2 window). It ships 11 composable "surfaces" (Converse, Approve, Ledger, Meters, Sensorium,
Palette, Board, Tiles, Canvas, Capture, ManifestEditor), each a **headless reducer** on one `Surface`
trait — pure `ViewModel → projection` folds, unit-testable with no GUI.

**Maturity:** genuinely built and green (the operator's docs call it *"the one sibling that escaped
the spec-only trap"*). But it is a **conversational-AI / trust-rendering UI** — chat turns, cost
receipts, approval gates, data-class chips. Deferred items all need a human/GUI/live daemon: the
actual Tauri window draw was never run headless, Windows Hello signing, and live KEEL attach.

## 2 · Its frontend / rendering approach

- **Windowing/GPU:** Tauri 2.x + **WebView2** (the OS webview *component*, never a browser tab).
  Explicitly **not** a GPU 3D engine — it renders DOM: text turns, chips, cards, forms. No wgpu,
  no canvas 3D, no scene graph anywhere in the tree.
- **Renderer stack:** a **zero-Node, zero-bundler vanilla `ui/`** (`index.html` + `main.js` +
  `style.css`) via Tauri's `withGlobalTauri`. `main.js` (158 lines) is the entire frontend: it
  `listen`s for `fh://vm` events, routes each ViewModel by `surface`, and reconciles DOM nodes keyed
  by `vm.node`. It emits Intents as Tauri **commands** (`invoke("submit"…)`, `invoke("approve"…)`).
- **egui/TUI:** an `egui` native renderer is *specced behind the same bus but deferred* ("build the
  seam now; build the second renderer never-or-later"). `fh-shell` is a headless console (stdout
  transcript), used as the offline test harness for render paths — not a TUI product.
- **Core patterns worth naming:** (a) **one render path** — every output (user, model, driver,
  oracle, system) flows through a single `fh_bus::deliver(bus, surfaces, vm)` free fn, killing the
  "works for X not Y" divergent-path bug class; (b) a **presentation firewall** in that path drops
  leaked control-vocab blocks regardless of origin; (c) **seq-stamped** ViewModels a renderer applies
  in monotonic order; (d) `op` verbs `Set|Patch|Append|Stream|Remove` for incremental DOM updates.

## 3 · Reusable assets for the booster 3D frontend

The booster frontend is *"a native shell hosting a GPU renderer fed by a real-time binary stream,
renderer as pure observer."* FIGUREHEAD is a **native shell hosting a DOM renderer fed by an
in-process view-model bus.** Same shape at the shell layer; different at the data layer. What ports:

- **Sidecar supervision / lifecycle (the strongest, and already adopted).** The booster's own
  `shell/src/main.rs` is *the same Tauri-sidecar-supervisor pattern* FIGUREHEAD proves: spawn the
  compute core as a child, drain its stdout/stderr into the shell log, **restart-on-crash**, and
  **kill-on-window-close** so it can't linger holding the socket. FIGUREHEAD's `fh-tauri` + the
  attach-resolver rungs (embed→daemon→sidecar→remote, §9) are the mature reference for hardening this
  (fall-through on failure, a "waiting"/degraded chip instead of a blank window).
- **"Core owns all state; renderer is a dumb projection" (ADR-1).** This is *exactly* the booster's
  "renderer is a pure observer" law, arrived at independently. FIGUREHEAD's §19 kill-list ("state
  leaking into the renderer re-creates front/back drift as a class") is a ready-made rationale for
  keeping the three.js side stateless w.r.t. vehicle truth — it only integrates/interpolates what the
  stream sends, authors no physics.
- **Island / degraded mode (§13, `fh-island` + `ViewCache`).** When the source drops, don't blank:
  keep the **last-good projection** drawable, show an honest "disconnected; retrying" chip, and
  replay on recovery. The booster's `TelemetryClient` already has `onClose → retry`; FIGUREHEAD's
  ViewCache pattern is the recipe for *what to keep on screen* during the gap (freeze the last frame /
  ghost the booster) rather than freezing the whole window.
- **Reflex / deliberate split (§10) → reframed as the interp/render split.** FIGUREHEAD's lesson is
  *"latency is presence: never block the visible layer on the slow producer."* The booster's
  render-one-packet-in-the-past interpolation is the same instinct; FIGUREHEAD is prior art that this
  separation is a first-class law, not a hack.
- **Capability-filtered fan-out (`fh-bus::admits`, filter-before-serialize).** *If* the booster ever
  wants multiple concurrent views (a director view + a debug telemetry view + a remote spectator) off
  one stream, FIGUREHEAD's bus shows one publisher fanning filtered clones to N subscribers, with the
  filter applied **before** serialization (a denied payload never becomes bytes). Not needed for v1.
- **Manifest-driven configuration (`face.toml`).** The idea that "chrome density / which panels mount
  / theme" are **data, not forks** maps cleanly onto director-mode presets, HUD on/off, camera-rig
  choice — a `view.toml` that selects cinematic-vs-debug without branching the renderer.
- **The diegetic predicted-impact marker** has *no* FIGUREHEAD analogue — it is 3D-scene content the
  DOM framework never contemplates. Build it in three.js.

## 4 · House-style signals (how the operator likes software built)

Strongly evidenced across FIGUREHEAD's docs and code — and mirrored in the booster tree:

- **Contracts frozen at the bottom, enforced by the compiler.** A tiny serde-only F0 that *nothing*
  imports up into; "get the contracts right and every renderer/surface is swappable." Layer law = the
  crate DAG, made executable (a test asserts F0 depends on nothing).
- **Goldens as the conformance layer.** `GOLDEN_FACE` is an operator-ratified, agent-frozen table of
  input→expected behaviors. (The booster has a `goldens/` dir — same discipline.)
- **Determinism + offline + provable-build.** *Every* build/test runs `--offline`, "treat any download
  as an immediate abort"; a **build-provenance / stale-binary check** ("the sensors weren't in the
  shipped exe" bug promoted to a shell law). Reproducibility is sacred.
- **Zero-deps / minimal renderer.** No Node, no npm, no bundler in `fh-tauri` — a vanilla JS file.
  Keep the machinery small and legible; a per-module token budget is CI-enforced.
- **Falsifier-gated, staged builds.** Each stage is independently useful and carries an explicit
  *falsifier* ("if X, the boundary is wrong — stop"). Overnight work is one green commit per item.
- **"Ship the thing that grades against reality; don't gold-plate the satisfying part."** The
  operator's own inverted anti-pattern warning: FIGUREHEAD shipped, so *stop adding a 6th surface* and
  go do the reality-graded spike. Bias toward the running artifact over the spec.
- **Atmosphere/presence taken seriously as engineering** (DAVE lineage: pacing, single render path,
  harness-invisibility) — relevant spirit for "cinematic maximalism," even if the mechanism differs.

## 5 · Verdict — what role FIGUREHEAD should play

**Reference and pattern-donor, not a dependency. Do not build the booster frontend on FIGUREHEAD,
and do not port its bus.** The domains diverge exactly where it matters:

- FIGUREHEAD renders **DOM** (chat/receipts/forms) via a **Rust-relayed** IPC event bus. The booster
  renders a **GPU 3D scene** from a **125 Hz binary WebSocket** the webview reads **directly** — the
  booster canon (§10.1) *deliberately forbids the relay hop through Rust* that is the entire premise of
  FIGUREHEAD's bus. Pushing 125 Hz binary frames through Tauri `emit`/`invoke` (JSON, main-thread) is
  precisely the latency/GC anti-pattern FIGUREHEAD's own §10 warns against. The booster already made
  the correct opposite call.
- The booster's cinematic goals (plume-as-hero, HDR, director cameras, WebGPU) live entirely in a
  space FIGUREHEAD has *no* code for. Its 11 surfaces are all trust/dialogue widgets; none touch a
  scene graph, a shader, or a camera rig.

**What to actually take:** three transferable *patterns*, already partly in the booster tree —
(1) the **Tauri sidecar-supervisor** lifecycle (spawn/supervise/restart/kill-on-close); the booster's
`shell/main.rs` is a lean version of it, and `fh-tauri` + the attach-resolver is the hardening
reference; (2) **island/ViewCache degraded-mode** discipline (keep last-good frame + honest reconnect
chip, never blank); (3) the **house-style scaffolding** — a small frozen telemetry-protocol contract
(`core/protocol.h` is the booster's F0-equivalent), goldens, determinism, provable-build, a `view.toml`
manifest for director/HUD presets. Also worth a skim for *spirit*: the "renderer owns no truth" law and
the "latency is presence" law are the two philosophical anchors the booster frontend should adopt
verbatim, even though the booster satisfies them with a direct WS + interp buffer rather than a bus.

**One-line answer:** FIGUREHEAD is the wrong *engine* (DOM/relay-bus vs GPU/direct-binary-stream) but
the right *philosophy and lifecycle donor*; borrow its supervision, degraded-mode, and contract/golden
discipline — the operator has, in fact, already started doing exactly that in `shell/` and `goldens/`.
