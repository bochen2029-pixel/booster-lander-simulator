//! Owns the `booster-core.exe --serve` sidecar lifecycle (understory Tier-0).
//!
//! CANON (§10.1, §11.1): the shell SPAWNS AND SUPERVISES the core; it never
//! relays telemetry. The webview opens the 125 Hz WebSocket directly. So this
//! module deals only with the control plane: resolve the exe, pick a free port,
//! spawn under a kill-on-close Job Object, watch readiness + exit, and expose a
//! typed state the frontend can render.
//!
//! CORE IS A ONE-SHOT SERVER (measured 2026-07-18, `runs/fe_s0_report.md`):
//!   stderr:  ws: listening on 127.0.0.1:PORT — waiting for a client...   <- READY
//!            ws: client connected, handshake OK
//!            serve: scenario=... — streaming @125 Hz                      <- STREAMING
//!            serve: done — verdict=... emitted=N frames                   <- GRACEFUL END
//!   then the process EXITS.
//!
//! Therefore, unlike a persistent llama-server, a child exit is NORMAL once a
//! run has streamed. The watcher distinguishes:
//!   * exit AFTER `serve: done` / after any client streamed  -> ExitedComplete (no respawn)
//!   * exit BEFORE readiness / with no stream                 -> Crashed (one auto-respawn)
//! The picker / RELIGHT button drive fresh spawns explicitly.

use std::net::TcpListener;
use std::path::PathBuf;
use std::process::Stdio;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use anyhow::{anyhow, Result};
use tauri::{AppHandle, Emitter};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};

/// Canon default WS port (`ws://127.0.0.1:8787`). Overridable via `BOOSTER_PORT`
/// or auto-scanned if occupied.
pub const DEFAULT_PORT: u16 = 8787;

/// Readiness sentinel the core prints on stderr once its WS server is bound.
const READY_SENTINEL: &str = "ws: listening";
/// The core prints these once it actually has a client and is emitting frames.
const STREAM_SENTINEL: &str = "streaming @";
/// Graceful run-complete sentinel.
const DONE_SENTINEL: &str = "serve: done";
/// How long to wait for the readiness line before declaring a failed launch.
const READY_TIMEOUT: Duration = Duration::from_secs(20);
/// Rolling stderr tail kept for the observability panel (understory Tier-3).
const STDERR_TAIL_MAX: usize = 400;

/// The supervised lifecycle state, mirrored to the frontend as `core://state`.
/// (Kept in lock-step with `ui/src/shell/state.ts`'s `CoreState`.)
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Phase {
    Spawning,       // exe launched, awaiting the "ws: listening" line
    Ready,          // bound + waiting for a client (webview may now connect)
    Streaming,      // core reported it has a client and is emitting @125 Hz
    ExitedComplete, // ran a full serve and exited (verdict emitted) — NOT an error
    Crashed,        // exited before readiness / without streaming
    Failed,         // could not spawn or never became ready (readable reason)
    Respawning,     // the single automatic recovery attempt is in flight
}

impl Phase {
    pub fn as_str(self) -> &'static str {
        match self {
            Phase::Spawning => "SPAWNING",
            Phase::Ready => "READY",
            Phase::Streaming => "STREAMING",
            Phase::ExitedComplete => "EXITED_COMPLETE",
            Phase::Crashed => "CRASHED",
            Phase::Failed => "FAILED",
            Phase::Respawning => "RESPAWNING",
        }
    }
}

/// The launch parameters the picker sets. Validated on the JS side too, but the
/// capability arg-validators are the real gate (`shell/capabilities/default.json`).
///
/// v2 PLAY MENU (canon §10.6 "keep INJECT_DISTURBANCE one keystroke away in the
/// UI"; D-017/D-019): optional disturbance specs, passed through to the core CLI
/// verbatim when non-empty. Empty string = flag absent = byte-identical baseline.
///   gust       -> `--gust <peak_mps>@<alt_m>[:<halfwidth_m>]`   (BUILT, D-017)
///   gust_dir   -> `--gust-dir <deg>`                            (BUILT, D-017)
///   engine_out -> `--engine-out <k>@<t>`                        (N0 core; pre-N0 the
///   target     -> `--target <spec>`                              core rejects loudly
///                                                                and the stderr panel
///                                                                shows why)
#[derive(Clone, Debug)]
pub struct Launch {
    pub scenario: String,
    pub seed: i64,
    pub run: i64,
    pub gust: String,
    pub gust_dir: String,
    pub engine_out: String,
    pub target: String,
}

impl Default for Launch {
    fn default() -> Self {
        Launch {
            scenario: "entry".into(),
            seed: 42,
            run: 1,
            gust: String::new(),
            gust_dir: String::new(),
            engine_out: String::new(),
            target: String::new(),
        }
    }
}

/// Shared supervisor state (managed by Tauri, read by commands).
pub struct Supervisor {
    /// The live child, if any. `Some` only while a core we spawned is running.
    child: Mutex<Option<Child>>,
    /// Chosen port for the current/next spawn.
    pub port: AtomicU32,
    /// Current lifecycle phase (as `Phase as u8` is awkward; store the discriminant).
    phase: Mutex<Phase>,
    /// The most recent launch parameters (for RELIGHT / status echo).
    pub launch: Mutex<Launch>,
    /// Rolling stderr tail for the observability panel.
    stderr_tail: Mutex<Vec<String>>,
    /// Human-readable last failure reason (parsed from stderr where possible).
    pub last_error: Mutex<String>,
    /// True once the current child has streamed to at least one client — used to
    /// classify its eventual exit as graceful rather than a crash.
    streamed: Arc<AtomicBool>,
    /// Monotonic generation counter: bumped on every spawn so a stale watcher
    /// task from a killed child never mutates state for the current one.
    generation: Arc<AtomicU64>,
    /// True after we have already used our one automatic respawn for the current
    /// user-initiated launch; reset when the user relaunches/relights.
    auto_respawn_used: Arc<AtomicBool>,
}

impl Supervisor {
    pub fn new() -> Self {
        Supervisor {
            child: Mutex::new(None),
            port: AtomicU32::new(DEFAULT_PORT as u32),
            phase: Mutex::new(Phase::Spawning),
            launch: Mutex::new(Launch::default()),
            stderr_tail: Mutex::new(Vec::new()),
            last_error: Mutex::new(String::new()),
            streamed: Arc::new(AtomicBool::new(false)),
            generation: Arc::new(AtomicU64::new(0)),
            auto_respawn_used: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn phase(&self) -> Phase {
        *self.phase.lock().unwrap()
    }

    pub fn port(&self) -> u16 {
        self.port.load(Ordering::Relaxed) as u16
    }

    pub fn launch_snapshot(&self) -> Launch {
        self.launch.lock().unwrap().clone()
    }

    pub fn stderr_tail_joined(&self) -> String {
        self.stderr_tail.lock().unwrap().join("\n")
    }

    pub fn last_error_str(&self) -> String {
        self.last_error.lock().unwrap().clone()
    }

    fn push_stderr(&self, line: String) {
        let mut tail = self.stderr_tail.lock().unwrap();
        tail.push(line);
        let overflow = tail.len().saturating_sub(STDERR_TAIL_MAX);
        if overflow > 0 {
            tail.drain(0..overflow);
        }
    }

    fn set_phase(&self, app: &AppHandle, p: Phase) {
        *self.phase.lock().unwrap() = p;
        let l = self.launch_snapshot();
        let payload = serde_json::json!({
            "phase": p.as_str(),
            "port": self.port(),
            "launch": {
                "scenario": l.scenario, "seed": l.seed, "run": l.run,
                "gust": l.gust, "gustDir": l.gust_dir,
                "engineOut": l.engine_out, "target": l.target,
            },
            "error": self.last_error_str(),
        });
        let _ = app.emit("core://state", payload);
    }
}

/// Resolve `booster-core.exe`: env override → next to the shell exe → known
/// build dirs. (Bonsai's candidate-path pattern; the shell bundles the core as
/// a Tauri external-bin at `binaries/booster-core-<triple>.exe`, which lands next
/// to the app exe on install.)
pub fn core_path() -> Result<PathBuf> {
    if let Ok(p) = std::env::var("BOOSTER_CORE") {
        let pb = PathBuf::from(&p);
        if pb.exists() {
            return Ok(pb);
        }
    }
    let mut candidates: Vec<PathBuf> = Vec::new();
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            // Tauri sidecar sits next to the app exe under `binaries/` at dev time
            // and beside the exe once installed; try both plus a triple-stamped name.
            candidates.push(dir.join("booster-core.exe"));
            candidates.push(dir.join("binaries").join("booster-core.exe"));
            candidates
                .push(dir.join("binaries").join("booster-core-x86_64-pc-windows-msvc.exe"));
        }
    }
    // Repo build output (dev fallback).
    candidates.push(PathBuf::from(
        r"C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe",
    ));
    for c in &candidates {
        if c.exists() {
            return Ok(c.clone());
        }
    }
    Err(anyhow!(
        "booster-core.exe not found. Looked next to the shell exe, in binaries/, \
         and the repo Release dir. Set BOOSTER_CORE to override."
    ))
}

/// Find a bindable TCP port. Honors `BOOSTER_PORT`; if that's taken or unset,
/// prefers `DEFAULT_PORT` then scans upward. Binding to 0 is not used here
/// because the core takes an explicit `--port` and we must know it up front to
/// hand it to the webview.
pub fn pick_port() -> u16 {
    if let Ok(s) = std::env::var("BOOSTER_PORT") {
        if let Ok(p) = s.parse::<u16>() {
            if TcpListener::bind(("127.0.0.1", p)).is_ok() {
                return p;
            }
            tracing::warn!("BOOSTER_PORT={p} is occupied; scanning for a free port");
        }
    }
    for p in DEFAULT_PORT..DEFAULT_PORT.saturating_add(60) {
        if TcpListener::bind(("127.0.0.1", p)).is_ok() {
            return p;
        }
    }
    DEFAULT_PORT
}

/// Spawn the core with the given launch params on a freshly-chosen free port,
/// install the readiness/exit watcher, and return once the child is *spawned*
/// (not yet ready — readiness arrives asynchronously via `core://state`).
///
/// `user_initiated` resets the one-automatic-respawn budget (a picker relaunch
/// or RELIGHT gets a fresh budget; the automatic recovery spawn does not).
pub async fn spawn(app: &AppHandle, sup: &Arc<Supervisor>, launch: Launch, user_initiated: bool) {
    // Kill any child we currently own before starting a new one (picker relaunch
    // during an active stream). The job object is the backstop; this is the tidy path.
    kill_current(sup).await;

    if user_initiated {
        sup.auto_respawn_used.store(false, Ordering::Relaxed);
    }
    sup.streamed.store(false, Ordering::Relaxed);
    *sup.launch.lock().unwrap() = launch.clone();
    *sup.last_error.lock().unwrap() = String::new();

    let port = pick_port();
    sup.port.store(port as u32, Ordering::Relaxed);
    sup.set_phase(app, Phase::Spawning);

    let exe = match core_path() {
        Ok(p) => p,
        Err(e) => {
            *sup.last_error.lock().unwrap() = e.to_string();
            sup.set_phase(app, Phase::Failed);
            return;
        }
    };

    let seed = launch.seed.to_string();
    let run = launch.run.to_string();
    let port_s = port.to_string();

    // Base args + the optional play-menu disturbance flags (empty = absent =
    // byte-identical baseline, the D-017 off-by-default pattern).
    let mut args: Vec<String> = vec![
        "--serve".into(),
        "--scenario".into(),
        launch.scenario.clone(),
        "--seed".into(),
        seed,
        "--run".into(),
        run,
        "--port".into(),
        port_s,
    ];
    if !launch.gust.is_empty() {
        args.push("--gust".into());
        args.push(launch.gust.clone());
    }
    if !launch.gust_dir.is_empty() {
        args.push("--gust-dir".into());
        args.push(launch.gust_dir.clone());
    }
    if !launch.engine_out.is_empty() {
        args.push("--engine-out".into());
        args.push(launch.engine_out.clone());
    }
    if !launch.target.is_empty() {
        args.push("--target".into());
        args.push(launch.target.clone());
    }

    tracing::info!("spawning core: {} {}", exe.display(), args.join(" "));

    let mut cmd = Command::new(&exe);
    cmd.args(&args)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .kill_on_drop(true);

    #[cfg(windows)]
    cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW — suppress console

    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(e) => {
            *sup.last_error.lock().unwrap() = format!("failed to spawn core: {e}");
            sup.set_phase(app, Phase::Failed);
            return;
        }
    };
    // The child inherits the kill-on-close Job Object installed at startup, so a
    // crash of THIS process can't orphan the core (understory §0.2).

    let generation = sup.generation.fetch_add(1, Ordering::SeqCst) + 1;

    // --- stdout drain (informational only) ---
    if let Some(stdout) = child.stdout.take() {
        tauri::async_runtime::spawn(async move {
            let mut lines = BufReader::new(stdout).lines();
            while let Ok(Some(line)) = lines.next_line().await {
                tracing::info!(target: "core", "{line}");
            }
        });
    }

    // --- stderr drain + readiness/stream parsing (the important pipe) ---
    if let Some(stderr) = child.stderr.take() {
        let app2 = app.clone();
        let sup2 = sup.clone();
        let streamed = sup.streamed.clone();
        tauri::async_runtime::spawn(async move {
            let mut lines = BufReader::new(stderr).lines();
            while let Ok(Some(line)) = lines.next_line().await {
                tracing::info!(target: "core", "{line}");
                // Emit raw stderr so the panel updates live, and keep the tail.
                sup2.push_stderr(line.clone());
                let _ = app2.emit("core://stderr", &line);

                // Ignore lines from a stale generation (a killed child racing).
                if sup2.generation.load(Ordering::SeqCst) != generation {
                    continue;
                }
                if line.contains(READY_SENTINEL) && sup2.phase() == Phase::Spawning {
                    sup2.set_phase(&app2, Phase::Ready);
                } else if line.contains(STREAM_SENTINEL) {
                    streamed.store(true, Ordering::Relaxed);
                    sup2.set_phase(&app2, Phase::Streaming);
                } else if line.contains(DONE_SENTINEL) {
                    // Full run finished; the process exit that follows is graceful.
                    streamed.store(true, Ordering::Relaxed);
                }
            }
        });
    }

    // --- readiness timeout watchdog ---
    {
        let app3 = app.clone();
        let sup3 = sup.clone();
        tauri::async_runtime::spawn(async move {
            let deadline = Instant::now() + READY_TIMEOUT;
            loop {
                if sup3.generation.load(Ordering::SeqCst) != generation {
                    return; // superseded
                }
                let ph = sup3.phase();
                if ph != Phase::Spawning {
                    return; // reached readiness (or beyond), or already failed
                }
                if Instant::now() > deadline {
                    *sup3.last_error.lock().unwrap() = format!(
                        "core did not report readiness within {}s (last stderr: {})",
                        READY_TIMEOUT.as_secs(),
                        sup3.stderr_tail
                            .lock()
                            .unwrap()
                            .last()
                            .cloned()
                            .unwrap_or_else(|| "<none>".into())
                    );
                    sup3.set_phase(&app3, Phase::Failed);
                    return;
                }
                tokio::time::sleep(Duration::from_millis(200)).await;
            }
        });
    }

    // --- exit watcher: classify graceful-complete vs crash, one auto-respawn ---
    {
        let app4 = app.clone();
        let sup4 = sup.clone();
        let streamed = sup.streamed.clone();
        let auto_used = sup.auto_respawn_used.clone();
        // Take the child out of `cmd`'s scope and into the supervisor so we can
        // both store a handle (for kill) and await it here. We can't do both with
        // one `Child`, so we store it and poll `try_wait` on the stored handle.
        *sup.child.lock().unwrap() = Some(child);
        tauri::async_runtime::spawn(async move {
            // Poll for exit on the stored handle so `kill_current` can also touch it.
            // IMPORTANT: the std MutexGuard around the Child is not `Send`, so the
            // lock+try_wait is done in a non-async helper that fully returns before
            // any `.await` — the guard never crosses an await point.
            let status = loop {
                if sup4.generation.load(Ordering::SeqCst) != generation {
                    return; // a newer spawn replaced us; that path owns the child
                }
                match poll_child_exit(&sup4) {
                    PollExit::Exited(st) => break st,
                    PollExit::Running => {}
                    PollExit::Gone => return, // handle taken by kill_current / error
                }
                tokio::time::sleep(Duration::from_millis(150)).await;
            };

            if sup4.generation.load(Ordering::SeqCst) != generation {
                return;
            }

            let code = status.code();
            let did_stream = streamed.load(Ordering::Relaxed);
            let _ = app4.emit(
                "core://exit",
                serde_json::json!({ "code": code, "streamed": did_stream }),
            );

            if did_stream {
                // Ran a full serve to completion → this exit is NORMAL, not a fault.
                sup4.set_phase(&app4, Phase::ExitedComplete);
                tracing::info!("core exited after streaming (code={:?}) — complete", code);
                return;
            }

            // Exited before streaming anything → treat as a crash.
            if sup4.last_error_str().is_empty() {
                *sup4.last_error.lock().unwrap() = format!(
                    "core exited before streaming (code={:?}). Last stderr: {}",
                    code,
                    sup4.stderr_tail
                        .lock()
                        .unwrap()
                        .last()
                        .cloned()
                        .unwrap_or_else(|| "<none>".into())
                );
            }

            if !auto_used.swap(true, Ordering::Relaxed) {
                // One automatic recovery attempt, then require a user action.
                tracing::warn!("core crashed pre-stream; one automatic respawn");
                sup4.set_phase(&app4, Phase::Respawning);
                let launch = sup4.launch_snapshot();
                tokio::time::sleep(Duration::from_millis(600)).await;
                // Not user-initiated → does NOT reset the respawn budget.
                // `spawn` recursing into itself makes its own future's `Send`-ness
                // impossible to infer (a cycle). We break the cycle by hopping
                // through `respawn_boxed`, which erases the future to a concrete
                // boxed type BEFORE the recursive `spawn` call is type-checked.
                respawn_boxed(app4.clone(), sup4.clone(), launch);
            } else {
                tracing::error!("core crashed and auto-respawn already used; awaiting user RELIGHT");
                sup4.set_phase(&app4, Phase::Crashed);
            }
        });
    }
}

/// Fire the single automatic recovery spawn on its own task, owning its args.
/// This exists purely to break the `spawn`→watcher→`spawn` async recursion cycle
/// so the compiler can see a concrete boxed future type at the recursion site.
fn respawn_boxed(app: AppHandle, sup: Arc<Supervisor>, launch: Launch) {
    tauri::async_runtime::spawn(async move {
        spawn(&app, &sup, launch, false).await;
    });
}

/// Kill the currently-owned child (graceful path; the job object is the backstop).
pub async fn kill_current(sup: &Arc<Supervisor>) {
    // Bump generation so the stale exit-watcher/stderr tasks stop mutating state.
    sup.generation.fetch_add(1, Ordering::SeqCst);
    let child = sup.child.lock().unwrap().take();
    if let Some(mut c) = child {
        let _ = c.start_kill();
        // Best-effort reap so we don't leave a zombie; ignore result.
        let _ = c.wait().await;
    }
}

/// Result of a single non-blocking check for child exit.
enum PollExit {
    Exited(std::process::ExitStatus),
    Running,
    /// Handle taken by `kill_current`, or a `try_wait` error — stop watching.
    Gone,
}

/// Synchronously check whether the stored child has exited, reaping it if so.
/// The std `MutexGuard` lives only within this function (never across an await),
/// which keeps the calling async task `Send`.
fn poll_child_exit(sup: &Arc<Supervisor>) -> PollExit {
    let mut guard = sup.child.lock().unwrap();
    match guard.as_mut() {
        Some(c) => match c.try_wait() {
            Ok(Some(st)) => {
                *guard = None; // consume so we don't reap twice
                PollExit::Exited(st)
            }
            Ok(None) => PollExit::Running,
            Err(e) => {
                tracing::warn!("try_wait error: {e}");
                PollExit::Gone
            }
        },
        None => PollExit::Gone,
    }
}
