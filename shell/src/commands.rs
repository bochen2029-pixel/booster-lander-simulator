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

/// Snapshot handed to the frontend on demand — the SAME nested shape as the
/// `core://state` event payload (`{ phase, port, launch: {...}, error }`), so
/// `mount.ts`/`state.ts` consume one shape from both sources. (The previous flat
/// struct diverged from the event payload and broke the shell chrome's mount-time
/// sync path — `status.launch.scenario` on a flat object.)
#[derive(Serialize)]
pub struct CoreLaunchOut {
    pub scenario: String,
    pub seed: i64,
    pub run: i64,
    pub gust: String,
    #[serde(rename = "gustDir")]
    pub gust_dir: String,
    #[serde(rename = "engineOut")]
    pub engine_out: String,
    pub target: String,
}

#[derive(Serialize)]
pub struct CoreStatus {
    pub phase: String,
    pub port: u16,
    pub launch: CoreLaunchOut,
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
        launch: CoreLaunchOut {
            scenario: l.scenario,
            seed: l.seed,
            run: l.run,
            gust: l.gust,
            gust_dir: l.gust_dir,
            engine_out: l.engine_out,
            target: l.target,
        },
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

/// PICKER: relaunch the core for a new scenario/seed/run (+ optional play-menu
/// disturbances, canon §10.6/D-019). Kills the current child (if any) and spawns
/// a fresh one on a fresh free port. User-initiated, so the one-automatic-respawn
/// budget resets.
#[tauri::command]
pub async fn relaunch_core(
    app: AppHandle,
    sup: State<'_, Arc<Supervisor>>,
    scenario: String,
    seed: i64,
    run: i64,
    gust: Option<String>,
    gust_dir: Option<String>,
    engine_out: Option<String>,
    target: Option<String>,
    sea: Option<bool>,
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
    // Disturbance specs pass through to the core CLI VERBATIM — the core's parser
    // is the semantic gate (it rejects loudly; the stderr panel surfaces why).
    // Here we only enforce a tight character set + length so nothing shell-hostile
    // rides an arg (no spaces, no dashes-as-new-flags, no quoting games).
    let gust = validate_spec("gust", gust, 32, "0123456789.@:")?;
    let gust_dir = validate_spec("gust-dir", gust_dir, 8, "0123456789.-")?;
    let engine_out = validate_spec("engine-out", engine_out, 32, "0123456789.@")?;
    let target = validate_spec_alnum("target", target, 48)?;
    let sup = sup.inner().clone();
    supervisor::spawn(
        &app,
        &sup,
        Launch { scenario, seed, run, gust, gust_dir, engine_out, target, sea: sea.unwrap_or(true) },
        true,
    )
    .await;
    Ok(sup.port())
}

/// Trim + validate an optional disturbance spec against an allowed char set.
/// None/empty -> "" (flag absent). A leading '-' is always rejected so a spec can
/// never masquerade as a new CLI flag.
fn validate_spec(
    name: &str,
    v: Option<String>,
    max_len: usize,
    allowed: &str,
) -> Result<String, String> {
    let s = v.unwrap_or_default().trim().to_string();
    if s.is_empty() {
        return Ok(s);
    }
    if s.len() > max_len {
        return Err(format!("{name} spec too long (max {max_len})"));
    }
    if s.starts_with('-') {
        return Err(format!("{name} spec must not start with '-'"));
    }
    if !s.chars().all(|c| allowed.contains(c)) {
        return Err(format!("{name} spec '{s}' has characters outside [{allowed}]"));
    }
    Ok(s)
}

/// Like `validate_spec` but for the target spec (mode words + params, e.g.
/// "drift:2" or "sea"), so ascii-alphanumerics plus a small punctuation set.
fn validate_spec_alnum(name: &str, v: Option<String>, max_len: usize) -> Result<String, String> {
    let s = v.unwrap_or_default().trim().to_string();
    if s.is_empty() {
        return Ok(s);
    }
    if s.len() > max_len {
        return Err(format!("{name} spec too long (max {max_len})"));
    }
    if s.starts_with('-') {
        return Err(format!("{name} spec must not start with '-'"));
    }
    if !s
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || "_.:@,+".contains(c))
    {
        return Err(format!("{name} spec '{s}' has invalid characters"));
    }
    Ok(s)
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
