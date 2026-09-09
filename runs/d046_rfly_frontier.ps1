# D-046 — THE FRONTIER BATCH: GM_RFLY on the D-027 engine-out draw.
#
# WHY THIS RUN EXISTS. D-019 §9.9 declared P(land | in-frontier) THE yardstick. D-027 measured
# the bound with runs/sandbox/ceiling_eo.c: the in-frontier fraction on `--engine-out random`
# is ~1.000 — essentially every draw is physically recoverable, so the claimable ceiling is
# ~59/60. Everything since has been scored against that bound EXCEPT the estate's best
# controller: GM_RFLY (the CEM search) has never been run on this batch. Its 36/36 is the
# compound SHOWCASE battery (3 seeds x 12 draws), a different and much smaller set.
#
# So the repo currently holds: a ~59/60 physical bound, a 1/60 MPPI floor (E0), ~8-10/60 from
# the reactive/neural stack (D-030), and NO number placing the search on the yardstick that
# defines "solved." This batch produces that number. It decides the next build:
#   high  (~45/60) => the remaining gap is small; build the reachability head (organ 1).
#   low   (~12/60) => the mission layer / site-reselect (organ 3) is the whole game.
#
# METHODOLOGY: identical to runs/eo_baseline_v6.ps1 (E0) in every respect except the guidance
# mode, so the comparison is like-for-like. Same exe, same scenario, same held-out seeds
# (42/7/99 — NEVER in training data), same 60 draws, same `--engine-out random`.
#
# COST: ~76 s/run measured => ~76 min/seed => ~3.8 h total. SEQUENTIAL BY DESIGN: the ROADMAP
# Phase-2 measurement showed concurrency is a null here (4 procs at OMP=4 = 159.6 s/run
# effective vs 150 s/run single-process-full-threads) — the CEM already saturates the box.
#
# WINDOWS DISCIPLINE (hard laws): detached via Start-Process (NEVER Start-Job); watch the
# DONE marker in the output file, NEVER a bare PID (Windows recycles them).
# NOTE: this batch HOLDS THE EXE — no C builds until D046-FRONTIER-DONE appears.

$exe = "C:/Booster_Lander_Simulator/build/bin/Release/booster-core.exe"
$out = "C:/Booster_Lander_Simulator/runs/d046_rfly_frontier.txt"

"D-046 GM_RFLY ENTRY --engine-out random x60, held-out seeds — started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out
"baselines on this exact batch: MPPI 1/60 (E0) | neural v6 1/0/0 (E0) | reactive+D-030 9-10/60 | D-027 in-frontier bound ~59/60" | Out-File $out -Append
"" | Out-File $out -Append

foreach ($s in 42, 7, 99) {
    $t0 = Get-Date
    # capture the WHOLE verdict block, not just LANDED — the PERFECT/GOOD/HARD/TIPPED/CRASHED
    # split and the crash-cause breakdown are what the frontier attribution needs.
    $raw = & $exe --headless --scenario entry --seed $s --runs 60 --rfly --engine-out random 2>&1
    $block = $raw | Select-String -Pattern "LANDED:|PERFECT|faults:|crash causes:|landed means:"
    $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
    "===== seed $s  ($mins min) =====" | Out-File $out -Append
    $block | ForEach-Object { $_.ToString() } | Out-File $out -Append
    "" | Out-File $out -Append
}

"D046-FRONTIER-DONE  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append
