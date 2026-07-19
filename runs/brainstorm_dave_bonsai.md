# Brainstorm distill: C:\DAVE + C:\Bonsai → a true-3D frontend for the Booster Lander Sim

Read-only research pass (2026-07-18). Purpose: mine two sibling Tauri apps for patterns
reusable by a cinematic 3D GUI over the booster sim's one-way 125 Hz WebSocket telemetry.
The load-bearing analogy: **`booster-core.exe --serve` is to a frontend shell exactly what
`llama-server.exe` is to DAVE/Bonsai** — a GPU-hungry child process that a Rust core spawns,
supervises, and streams to a pure-observer UI. Both apps are that template, already built.

---

## 1. WHAT EACH IS

### C:\DAVE  (product name "Tenancy"; default persona "Dave")
- **Purpose:** local-first substrate for a persistent, autotelic *persona-AI* — "a small finite
  presence in your machine." Not a chatbot; the metric is "does opening the app feel like checking
  on someone who lives there." Deep philosophical framing (`framework_v5/`, `docs/DAVE_SOUL.md`,
  a forthcoming book *Inside the Region*). The framework_v5 folder is a relational-coherence
  *diagnostic* + literate-programming spec — **irrelevant to the booster GUI**, ignore it.
- **Architecture:** Tauri 2 (Rust core + WebView) · React 18 + TS-strict + **Zustand** + Tailwind ·
  `llama-server` sidecar (system-spawned, OpenAI-compat) · **SQLite via rusqlite (bundled)** at
  `%APPDATA%/dave/dave.db` · EB Garamond bundled. Model: Qwen3.5-9B (any 9B–32B GGUF).
- **Maturity: HIGH / the parent app.** ~26 Rust modules (consolidation, discriminator, idle_worker,
  outreach, recall, memory_assembler, presence, time_awareness, chat_pacing, persistence…). Ships an
  NSIS/MSI installer. In daily personal use. This is the proven template the others descend from.

### C:\Bonsai
- **Purpose:** a small, fast, browser-free **native desktop chat** for the 1-bit Bonsai-27B model
  (~3.8 GB, 262K ctx) on the PrismML llama.cpp fork. Much narrower than DAVE — a focused chat client
  whose standout feature is a **live 3D "surprisal bonsai"** visualization.
- **Architecture:** same stack, slimmer. Tauri 2 · React + Vite + Tailwind + Zustand · 6 Rust
  modules only (`main / sidecar / llama_client / commands / job / think_strip`). Persistence is
  **localStorage** (deliberately minimal). **Three.js bundled** for the tree.
- **Maturity: MEDIUM, purpose-built 2026-07-17.** README states outright: *"Reuses the proven
  llama-server sidecar + streaming patterns from C:\DAVE, retargeted to the Bonsai stack."* It is
  DAVE's shell, stripped to essentials and given a WebGL organ. **This is the most directly useful
  reference for the booster frontend** — it is already "sidecar + stream + real-time 3D."

---

## 2. UI/UX PATTERNS

### Bonsai — the 3D piece is the crown jewel (see `src/lib/bonsaiScene.ts`, `components/TreeView.tsx`)
- **Full Three.js scene, and its aesthetic explicitly borrows from the booster sim.** Header comment:
  *"Aesthetic (AgX tone-mapping, emissive bloom, dark studio) borrows from the Booster-sim renderer
  notes."* So influence already flows booster→Bonsai; this closes the loop back.
- Concrete cinematic recipe worth lifting wholesale:
  - `WebGLRenderer({ antialias:true })`, `setPixelRatio(min(devicePixelRatio,1.5))` (perf cap),
    `toneMapping = AgXToneMapping`, exposure 1.0.
  - **EffectComposer post-stack:** `RenderPass` → `UnrealBloomPass(res, 0.62, 0.5, 0.82)` →
    `OutputPass`. Bloom is what makes emissive elements "smolder."
  - Dark studio: near-black bg `0x0a0d0b` + `THREE.Fog`, `HemisphereLight` + `DirectionalLight`
    key + blue rim light + point fill, ground `CircleGeometry` + faint `GridHelper` (the exact
    grid-plane look a launchpad wants).
  - `OrbitControls` with `enableDamping`, `autoRotate` (0.5 speed), min/max distance clamp.
  - **Raycaster hover→tooltip** reading back per-element data (`userData.token`) → screen-space HTML
    tooltip. Direct analog: hover the booster/plume/leg to read altitude, velocity, thrust.
  - **Live incremental rebuild while streaming:** rebuild geometry every ~4 new data points, dispose
    old geometry/materials each rebuild (leak-safe `traverse → geometry.dispose / material.dispose`).
  - **The geometry IS the signal, nothing is decoration** — color ramp (green→amber→red) +
    emissive-intensity glow both driven by a measured scalar (surprisal). *This is the exact design
    ethos the booster's "cinematic maximalism, renderer = pure observer" wants: telemetry drives
    every pixel; the frontend invents nothing.*
- **Overlay/HUD idiom (`TreeView.tsx`):** fullscreen `fixed inset-0`, canvas underneath, absolutely-
  positioned Tailwind panels on top — title block, a **gradient legend bar** (`linear-gradient(90deg,
  green,amber,red)`), a live mono stat readout (`peak … nats · mean … · growing…`), Esc-to-close,
  a bottom "caption" strip re-rendering the answer text colored by the same ramp. This HUD-over-canvas
  layout is a ready-made template for a mission-control overlay (altitude/vel/fuel/throttle bars).

### DAVE — restraint, "felt presence," cadence (relevant even without a 3D view)
- **Backend-paced streaming** (`src/streaming/pacedRenderer.ts` + Rust `chat_pacing.rs`): the *Rust*
  side owns visual pacing, sleeping between per-char emits; the frontend is a dumb queue that just
  renders on arrival. Single-source-of-truth pacing. **Directly transferable:** the booster core can
  likewise own frame cadence and the frontend just draws what arrives at 125 Hz — no client-side
  interpolation guesswork unless you want it.
- **Presence/status vocabulary as pure CSS** (`styles/globals.css`): `.presence-dot` with
  `streaming` (1.4s pulse) vs `waiting` (4.2s slow pulse) keyframes; a **Telegram-style two-check
  delivery/read indicator** whose states are gated on *real* backend facts (llama reachable =
  delivered, triage done = read) — "real, not fake." Lesson for the booster HUD: bind every indicator
  to a measured telemetry fact, never a cosmetic timer.
- **Typography-forward theming:** EB Garamond serif body, Inter for chrome/labels, warm dark palette
  via CSS custom props (`--bg-base:#1a1714 … --accent:#c9a876`). Bonsai's is a **green** darkroom
  (`radial-gradient` vignette + `#0b0e0c`, `color-scheme:dark`, Inter). Both: `overflow:hidden`,
  custom thin scrollbars, `user-select:none` on chrome / `text` on content — the "native app, not a
  web page" polish the booster's native-feel target wants.
- **Markdown + fenced code w/ copy button, collapsible live "Reasoning" panel, adjustable-sampling
  settings drawer.** Chat-specific; low relevance to a telemetry HUD except as component-craft
  reference.

---

## 3. SHELL + SIDECAR PATTERNS  ← the highest-value takeaway

This is the reusable spine. Bonsai's `src-tauri/` is the tightest expression; DAVE's is the
battle-tested superset. **Both are a working implementation of "Rust core supervises a GPU child and
streams it to an observer UI" — which is precisely the booster shell.**

### Sidecar lifecycle (`Bonsai/src-tauri/src/sidecar.rs`, `main.rs`)
- **Locate the child by candidate-path search** with an env override first
  (`BONSAI_LLAMA_SERVER` / `DAVE_LLAMA_SERVER`), then known dirs, then next-to-exe. → booster:
  resolve `booster-core.exe` the same way with a `BOOSTER_CORE` override.
- **`spawn_llama_server`**: `tokio::process::Command`, args, `stdout/stderr = piped`,
  `kill_on_drop(true)`, `creation_flags(0x08000000 = CREATE_NO_WINDOW)` to suppress the console.
  Pipes are pumped line-by-line into `tracing` and the **last stderr lines are retained so a failed
  launch surfaces the real cause** (CUDA OOM, model-not-found).
- **Health-gate before use:** poll `GET /health` (400 ms interval, 180 s deadline) until 200, then
  flip a `ready` flag and emit a ready event. → booster: poll the WS/HTTP endpoint until the sim's
  serve-loop answers, hold the UI in a "loading" state (DAVE deliberately shows an empty
  slow-pulsing dot for 10–30 s during model load — matches the booster's "warming up" idea).
- **Port hygiene:** `pick_free_port` scans a range via `TcpListener::bind`; if `:8080` is already
  healthy, do a **server-identity check** (`GET /v1/models` / `/props`, substring-match the model)
  before attaching, else spawn our own on a free port and *repoint the client's base URL at runtime*
  (`Arc<RwLock<String>>`). → booster: if the WS port is occupied, verify it's actually *our* core
  (some handshake / version string) before attaching; otherwise pick another port and tell the
  frontend via Tauri state. This is the single most robustness-buying idea in either repo.

### Crash-safety: the Windows Job Object (`Bonsai/src-tauri/src/job.rs`) — COPY THIS VERBATIM
- At startup, **before any child spawns**, put *this* process into a Job Object flagged
  `KILL_ON_JOB_CLOSE` (`win32job` crate). Children inherit it; when the parent dies for ANY reason
  (crash, force-quit, debugger stop) the kernel terminates the whole job — no orphaned process
  holding VRAM. `std::mem::forget(job)` so only process-exit closes the handle.
- `kill_on_drop(true)` + explicit `child.start_kill()` on `WindowEvent::CloseRequested` cover the
  graceful path; the job object is the kernel-enforced backstop. → **A runaway `booster-core.exe`
  pinning a GPU is the identical failure mode; this is a drop-in fix.**

### IPC / event streaming to the frontend (`commands.rs`, `lib/ipc.ts`, `state/store.ts`)
- **Two directions:** `#[tauri::command]` request/response for control (`send_message`,
  `stop_generation`, `get_status`) **+** fire-and-forget `app.emit("bonsai://…", json)` events for
  the live stream (`…/token`, `…/reasoning`, `…/done`, `…/error`, `…/ready`, `…/status`). Events are
  keyed by a `request_id` so the UI routes deltas to the right message.
- Frontend `ipc.ts` wraps `invoke` + `listen` into a typed surface; Zustand `store.ts` binds
  listeners once (`listenersBound` guard) and reduces events into state. **Cancellation** is an
  atomic epoch counter (`stop_epoch.fetch_add`) polled between stream chunks — clean "Stop" button.
- **Direct booster mapping:** the sim already emits binary telemetry at 125 Hz over WebSocket, so the
  frontend can connect to that WS *directly* (no need to marshal every frame through Tauri IPC — WS
  in the WebView is fine and lower-overhead). Use **Tauri commands/events only for control-plane +
  lifecycle** (start/stop/reset the sim, "core ready", "core died, here's the stderr tail", chosen
  port). That hybrid — WS for the hot telemetry path, Tauri events for supervision — is the clean
  synthesis of these two apps' patterns for a one-way-stream sim.

### Persistence
- **DAVE = SQLite/rusqlite (bundled)** — the mature choice; schema-driven, searchable, survives
  WebView resets. **Bonsai = localStorage** with a telling detail: it **drops the heavy per-token
  array from disk** (`partialize`, localStorage cap) and keeps only small summary scalars. → booster:
  if you ever record runs/replays, follow DAVE to SQLite; never put a full 125 Hz trace in
  localStorage. For a live-only observer, no persistence is needed at all.

---

## 4. HOUSE-STYLE SIGNALS + SIBLING-LINEAGE MAP

**House style (consistent across all three apps + the booster's own pins):**
- Tauri 2 + React + Vite + TS-strict + Tailwind; **Zustand** for state; **pnpm**; Windows-first,
  CUDA, RTX 4070 Ti SUPER 16 GB as the reference box.
- Rust core spawns a **system-installed GPU sidecar** (never bundled/rebuilt); locate-by-candidate-
  path + env override; health-gate; **Job Object kill-on-close**; single-instance plugin.
- Dark, cinematic, native-feel UI (`overflow:hidden`, custom scrollbars, no browser chrome). Every
  indicator bound to a **measured fact, "real not fake."** three.js 0.185 is the shared 3D dep
  (booster `ui/` already pins `@types/three@0.185`, matching MEMORY.md).
- Heavy inline "why" comments; explicit honesty about limits.

**Lineage (who begat whom):**
```
        C:\DAVE  (Tenancy) ── the proven parent: full Rust supervision, SQLite,
             │                 personas, paced streaming, installer. HIGH maturity.
             │  "reuses proven sidecar+streaming patterns from C:\DAVE"
             ▼
        C:\Bonsai ── DAVE's shell stripped to a lean chat client + a live Three.js
             │        surprisal-tree; localStorage; PrismML 1-bit sidecar. 2026-07-17.
             │
             ▼  (Understory hardening spec: "inherited from a proven sibling app")
     Understory (bochen2029-pixel/Understory) ── Tauri v2 + React/Vite/Tailwind,
                Rust core supervising a PrismML llama-server for Bonsai-27B-Q1_0.
                Its Tier-0 checklist (Job Object, single-instance, identity check,
                supervised recovery, SQLite-out-of-localStorage) is LITERALLY the DAVE/
                Bonsai patterns written up as a spec. Its "generative tree visualization"
                = Bonsai's surprisal bonsai. So Understory ≈ productized Bonsai.
```
The operator's **proven frontend template** is therefore: *Tauri-2 shell + Rust sidecar supervisor
(Job Object + health-gate + identity check + single-instance) + React/Vite/Tailwind/Zustand +
event-stream-into-store + a Three.js AgX/bloom canvas whose geometry is driven by measured data.*
The Understory spec is the checklist form of it; Bonsai is the smallest working code form of it.

---

## 5. VERDICT — what these contribute to the booster 3D frontend

**Strong yes. Bonsai in particular is a near-perfect skeleton to fork.** Concretely:

1. **The sidecar-supervision template → take it wholesale.** `sidecar.rs` + `job.rs` + `main.rs`'s
   spawn/health-gate/identity-check/single-instance flow map 1:1 onto supervising
   `booster-core.exe --serve`. The **Windows Job Object kill-on-close is the single highest-value
   copy** — it kills the "runaway core pinning the GPU" failure class outright. Health-gate +
   stderr-tail-on-failure + free-port-scan + "is this actually our process on the port" are all
   directly reusable.

2. **The shell skeleton → yes.** Tauri-2 + React/Vite/Tailwind/Zustand + typed `ipc.ts`(invoke) +
   `events`(listen) + listeners-reduced-into-store + atomic-epoch cancellation is a clean, proven
   frame for the booster's control plane. **Refinement for a one-way sim:** connect the WebView
   straight to the 125 Hz telemetry WebSocket for the hot path; reserve Tauri commands/events for
   lifecycle + control (start/stop/reset, "core ready/died", chosen port). DAVE's `chat_pacing`
   single-source-of-truth pacing validates letting the core own cadence.

3. **The 3D/cinematic recipe → yes, this is the closest thing to a style guide you have.**
   `bonsaiScene.ts` already encodes exactly the booster's target (AgX tone-mapping, UnrealBloom,
   dark studio + fog + grid ground, OrbitControls w/ damping, raycaster-hover→HTML-tooltip,
   perf-capped pixel ratio, leak-safe live geometry rebuild, **"geometry IS the signal"**). Lift the
   renderer/composer/lighting setup and the HUD-overlay-on-canvas idiom (legend bar, live mono
   readouts, Esc-close) as the starting frame for the launchpad + booster + plume view. Note the
   influence was *already* booster→Bonsai per its own comments, so this reconverges cleanly.

4. **Persona / "mission-control commentator" voice → optional, and there's real material.** DAVE's
   persona bundle concept (`personas/*.txt`, `prompts.rs`) is a rich, opinionated system-prompt craft
   for a distinct voice that "doesn't perform helpfulness." If the booster ever wants an
   LLM-driven flight-commentator/CAPCOM narrating a descent ("throttle up… coming in hot… touchdown"),
   the whole DAVE stack (llama sidecar + persona + backend-paced streaming into a text panel) drops
   in — and you'd supervise *that* llama-server with the very same Job Object machinery. Nice-to-have,
   not core to a graphics frontend. Feed it live telemetry as context and it becomes a real commentator.

5. **What contributes nothing:** DAVE's `framework_v5/` (relational-coherence philosophy/diagnostic),
   the memory/consolidation/outreach/discriminator subsystems, persona memory inspector, journal —
   all chat/persona-specific, no bearing on a telemetry renderer. Skip them.

**Bottom line:** clone Bonsai's `src-tauri/` supervision spine and its `bonsaiScene.ts` cinematic
setup; wire the hot telemetry path as a direct WS into the WebView; keep DAVE in your pocket only if
you later want an LLM mission-control voice. The operator has, in effect, already built the booster
frontend's chassis twice — Bonsai is the copy to start from.
