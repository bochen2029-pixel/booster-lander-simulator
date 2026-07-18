// main.rs — Booster Lander shell (canon §11.1, §10.1).
//
// The shell's ENTIRE job: spawn `booster-core --serve <port>` as a sidecar and
// supervise it (restart on crash, kill on window close). It does NOT relay
// telemetry — the webview connects directly to ws://127.0.0.1:<port> (canon §10.1:
// "no relay hop through Rust ... the shell only spawns and supervises"). Nothing
// in this process ever writes vehicle state.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tauri::Manager;
use tauri_plugin_shell::process::{CommandChild, CommandEvent};
use tauri_plugin_shell::ShellExt;
use std::sync::Mutex;

const CORE_PORT: u16 = 8787; // canon default ws://127.0.0.1:8787

/// Handle to the running sidecar so we can kill it on exit.
struct CoreProc(Mutex<Option<CommandChild>>);

fn spawn_core(app: &tauri::AppHandle) -> Result<CommandChild, String> {
    let sidecar = app
        .shell()
        .sidecar("booster-core")
        .map_err(|e| format!("sidecar lookup failed: {e}"))?
        .args(["--serve", &CORE_PORT.to_string()]);

    let (mut rx, child) = sidecar
        .spawn()
        .map_err(|e| format!("failed to spawn core: {e}"))?;

    // Drain the sidecar's stdout/stderr into the shell log (never blocks the UI).
    let app2 = app.clone();
    tauri::async_runtime::spawn(async move {
        while let Some(ev) = rx.recv().await {
            match ev {
                CommandEvent::Stdout(line) => {
                    println!("[core] {}", String::from_utf8_lossy(&line));
                }
                CommandEvent::Stderr(line) => {
                    eprintln!("[core:err] {}", String::from_utf8_lossy(&line));
                }
                CommandEvent::Terminated(payload) => {
                    eprintln!("[core] terminated: {:?}", payload.code);
                    // Supervise: a crash of the core must not take down the shell
                    // (directive 2). Restart once after a short delay.
                    let app3 = app2.clone();
                    tauri::async_runtime::spawn(async move {
                        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
                        if let Ok(child) = spawn_core(&app3) {
                            if let Some(state) = app3.try_state::<CoreProc>() {
                                *state.0.lock().unwrap() = Some(child);
                            }
                        }
                    });
                }
                _ => {}
            }
        }
    });

    Ok(child)
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .manage(CoreProc(Mutex::new(None)))
        .setup(|app| {
            let handle = app.handle().clone();
            let child = spawn_core(&handle)?;
            *app.state::<CoreProc>().0.lock().unwrap() = Some(child);
            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { .. } = event {
                // Kill the sidecar so it doesn't linger holding the socket.
                if let Some(state) = window.app_handle().try_state::<CoreProc>() {
                    if let Some(child) = state.0.lock().unwrap().take() {
                        let _ = child.kill();
                    }
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running Booster Lander shell");
}
