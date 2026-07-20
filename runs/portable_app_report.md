# Portable app — booster-shell.exe (the one-shot LZ-COCKPIT)

**What it is:** the Tauri desktop shell that launches the WHOLE thing in one shot — it spawns
`booster-core.exe --serve --interactive` as a supervised sidecar and opens a webview with the (embedded)
three.js cockpit UI, which connects directly to `ws://127.0.0.1:<port>`. No install, no dev server, no
Node — double-click and it flies.

## Contents of the portable folder
```
BoosterLander-portable\
  booster-shell.exe                              <- the app (UI embedded); double-click this
  booster-core-x86_64-pc-windows-msvc.exe        <- the sim sidecar (the shell spawns it)
```
The shell resolves the core next to itself (or in `binaries\`, or via the `BOOSTER_CORE` env var), so the
two files just need to sit together. Copy the folder anywhere — it's fully portable (needs only Windows +
WebView2, which ships with Windows 11).

> Not a single `.exe`: the UI is embedded IN the shell, but the sim is a separate sidecar exe (Tauri's
> external-bin model). A true single-file (embed the core as a resource + extract-and-run at startup) is a
> small follow-up if wanted.

## How it was built
1. `pnpm -C ui build`  → `ui/dist` (embedded into the shell by `generate_context!`).
2. copy `build/bin/Release/booster-core.exe` → `shell/binaries/booster-core-x86_64-pc-windows-msvc.exe`.
3. `cargo build --release --manifest-path shell/Cargo.toml` → `shell/target/release/booster-shell.exe`.
4. stage both exes into `dist/BoosterLander-portable/`.

(`cargo tauri build` would additionally produce an NSIS *installer* — not portable — so we ship the raw
release exe instead.)

## Using it
- Launches the default run (scenario `entry`, seed 42). The cockpit streams the live descent.
- **Failure-mode buttons (bottom-center):** the shell spawns the core with `--interactive`, so
  **WIND GUST** injects a random 15–30 m/s shear at the vehicle's current altitude, and **ENGINE OUT**
  fails a side engine (fires on the next multi-engine burn). Every injection is journaled in the core's
  stderr (visible in the shell's stderr panel) as `[INJECT] t=.. ..`.
- **Moving deck:** launch with a SEA scenario / `--sea` to see the Gerstner ocean + the heaving/drifting
  ASDS droneship (the deck is posed from the streamed `deck_z`/`target_xy`).

## Notes
- Single-instance: a second launch focuses the existing window (no duplicate core fighting for the port).
- Kill-on-close Job Object: closing the window (or a shell crash) kills the sidecar — no orphans.
- Determinism: absent any injection the run is deterministic; pressing a failure button waives determinism
  for that run (by design), and the `[INJECT]` journal records exactly what/when so it can be replayed.
