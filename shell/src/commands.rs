//! Tauri commands — the CONTROL PLANE only (canon §10.1, §10.6).
//!
//! Telemetry never passes through here: the webview opens the 125 Hz WebSocket
//! to `ws://127.0.0.1:<port>` directly. These invokes exist solely for
//! lifecycle: report the current supervised state, hand the webview its port,
//! relaunch a scenario/seed/run (picker), RELIGHT after a crash, and surface the
//! sidecar stderr tail for the observability panel.

use std::sync::Arc;

use serde::Serialize;
use tauri::{AppHandle, State};

use crate::supervisor::{self, Launch, Supervisor};

/// Snapshot handed to the frontend on demand (mirrors `core://state` payload).
#[derive(Serialize)]
pub struct CoreStatus {
    pub phase: String,
    pub port: u16,
    pub scenario: String,
    pub seed: i64,
    pub run: i64,
    pub error: String,
}

/// Current supervised lifecycle snapshot (the chip polls this on mount to sync
/// with any state changes that fired before its listener attached).
#[tauri::command]
pub fn get_core_status(sup: State<'_, Arc<Supervisor>>) -> CoreStatus {
    let l = sup.launch_snapshot();
    CoreStatus {
        phase: sup.phase().as_str().to_string(),
        port: sup.port(),
        scenario: l.scenario,
        seed: l.seed,
        run: l.run,
        error: sup.last_error_str(),
    }
}

/// The port the webview should open its telemetry WebSocket to. Canon §11.1:
/// the shell chooses the port and hands it to the webview via this invoke.
#[tauri::command]
pub fn get_core_port(sup: State<'_, Arc<Supervisor>>) -> u16 {
    sup.port()
}

/// Rolling sidecar stderr tail for the observability panel (understory Tier-3).
#[tauri::command]
pub fn get_stderr_tail(sup: State<'_, Arc<Supervisor>>) -> String {
    sup.stderr_tail_joined()
}

/// PICKER: relaunch the core for a new scenario/seed/run. Kills the current
/// child (if any) and spawns a fresh one on a fresh free port. User-initiated,
/// so the one-automatic-respawn budget resets.
#[tauri::command]
pub async fn relaunch_core(
    app: AppHandle,
    sup: State<'_, Arc<Supervisor>>,
    scenario: String,
    seed: i64,
    run: i64,
) -> Result<u16, String> {
    // Defensive server-side validation (the capability arg-validators are the
    // real gate, but fail fast with a readable message here too).
    let scenario = scenario.trim().to_string();
    if scenario.is_empty() || scenario.len() > 32 || !scenario.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-') {
        return Err(format!("invalid scenario '{scenario}'"));
    }
    if !(0..=1_000_000).contains(&seed) {
        return Err(format!("seed {seed} out of range 0..=1000000"));
    }
    if !(0..=1_000_000).contains(&run) {
        return Err(format!("run {run} out of range 0..=1000000"));
    }
    let sup = sup.inner().clone();
    supervisor::spawn(&app, &sup, Launch { scenario, seed, run }, true).await;
    Ok(sup.port())
}

/// RELIGHT: user-initiated recovery after a crash/complete. Re-spawns the last
/// launch params with a fresh respawn budget.
#[tauri::command]
pub async fn relight_core(
    app: AppHandle,
    sup: State<'_, Arc<Supervisor>>,
) -> Result<u16, String> {
    let sup = sup.inner().clone();
    let launch = sup.launch_snapshot();
    supervisor::spawn(&app, &sup, launch, true).await;
    Ok(sup.port())
}
