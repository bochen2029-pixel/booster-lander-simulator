// main.rs — Booster Lander LZ-COCKPIT shell (canon §10.1, §11.1; understory Tier-0).
//
// The shell's ENTIRE job: spawn `booster-core.exe --serve --scenario S --seed N
// --run R --port P` as a sidecar and SUPERVISE it with hardening discipline —
// Windows Job Object kill-on-close, single instance, free-port selection, stderr
// capture + readiness parse, child-exit classification, one auto-respawn.
//
// It does NOT relay telemetry. The webview connects DIRECTLY to
// ws://127.0.0.1:<port> (canon §10.1: "no relay hop through Rust ... the shell
// only spawns and supervises"). Nothing in this process ever writes vehicle
// state. Tauri events carry control/lifecycle only:
//   core://state   { phase, port, launch, error }   — lifecycle state machine
//   core://exit    { code, streamed }                — child exited
//   core://stderr  "<line>"                          — live sidecar stderr
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod job;
mod supervisor;

use std::sync::Arc;

use tauri::{Manager, WindowEvent};
use tracing_subscriber::EnvFilter;

use supervisor::{Launch, Supervisor};

fn main() {
    let _ = tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| EnvFilter::new("booster_shell=info,core=info")),
        )
        .try_init();

    tauri::Builder::default()
        // Single-instance MUST be registered first (understory §0.3): a second
        // launch focuses the existing window instead of spawning a duplicate
        // shell + a second core fighting for the same port.
        .plugin(tauri_plugin_single_instance::init(|app, _argv, _cwd| {
            if let Some(w) = app.get_webview_window("main") {
                let _ = w.unminimize();
                let _ = w.show();
                let _ = w.set_focus();
            }
        }))
        .manage(Arc::new(Supervisor::new()))
        .setup(|app| {
            // Install the kill-on-close job BEFORE any child spawns, so the core
            // inherits it and can't be orphaned by a crash (understory §0.2).
            if let Err(e) = job::install_kill_on_close() {
                tracing::warn!("kill-on-close job not installed (orphan backstop disabled): {e}");
            }

            let handle = app.handle().clone();
            let sup = app.state::<Arc<Supervisor>>().inner().clone();
            // Spawn the default run on startup (entry s42 r1). The webview will
            // connect once it receives READY via core://state (or by polling
            // get_core_status on mount).
            tauri::async_runtime::spawn(async move {
                supervisor::spawn(&handle, &sup, Launch::default(), true).await;
            });
            Ok(())
        })
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { .. } = event {
                // Graceful kill of the sidecar so it doesn't linger holding the
                // socket. The inherited kill-on-close job is the kernel-enforced
                // backstop for any path that skips this (crash / force-quit).
                if let Some(sup) = window.app_handle().try_state::<Arc<Supervisor>>() {
                    let sup = sup.inner().clone();
                    tauri::async_runtime::block_on(async move {
                        supervisor::kill_current(&sup).await;
                    });
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::get_core_status,
            commands::get_core_port,
            commands::get_stderr_tail,
            commands::relaunch_core,
            commands::relight_core,
        ])
        .run(tauri::generate_context!())
        .expect("error while running Booster Lander shell");
}
