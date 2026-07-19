//! Windows Job Object backstop against orphaned `booster-core.exe` servers.
//! (understory Tier-0 §0.2; ported verbatim from the proven Bonsai spine.)
//!
//! `kill_on_drop(true)` and the explicit kill on window-close handle the
//! *graceful* path. They do NOT cover a hard crash, a force-quit, or a
//! debugger stop of the shell — in those cases `booster-core.exe` is orphaned,
//! still bound to its WS port (and, once the cinematic layer lands, holding a
//! GPU context). That is the single most likely thing to make the app "feel
//! cursed": the next launch either can't bind the port or attaches to a ghost.
//!
//! Fix: at startup we put THIS process into a Job Object flagged
//! `KILL_ON_JOB_CLOSE`. Child processes (the core, spawned afterward) inherit
//! the job automatically. When this process dies for ANY reason the OS closes
//! its handles, the job's last handle closes, and the kernel terminates every
//! process still in the job — including the inherited core. This is
//! kernel-enforced, so it survives a crash a Drop impl would not.

/// Install the kill-on-close job on the current process. Call once at startup,
/// before spawning the core, so the child inherits the job.
#[cfg(windows)]
pub fn install_kill_on_close() -> anyhow::Result<()> {
    use anyhow::anyhow;
    use win32job::Job;

    let job = Job::create().map_err(|e| anyhow!("Job::create failed: {e}"))?;
    let mut info = job
        .query_extended_limit_info()
        .map_err(|e| anyhow!("query_extended_limit_info failed: {e}"))?;
    info.limit_kill_on_job_close();
    job.set_extended_limit_info(&mut info)
        .map_err(|e| anyhow!("set_extended_limit_info failed: {e}"))?;
    job.assign_current_process()
        .map_err(|e| anyhow!("assign_current_process failed: {e}"))?;

    // Intentionally leak the handle: only *process termination* should close
    // it, which is exactly when we want KILL_ON_JOB_CLOSE to fire on the
    // inherited children. Dropping it while we're alive would close the handle
    // early. The OS reclaims it on exit.
    std::mem::forget(job);
    Ok(())
}

#[cfg(not(windows))]
pub fn install_kill_on_close() -> anyhow::Result<()> {
    Ok(())
}
