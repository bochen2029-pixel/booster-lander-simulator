# D-046 (1b) — THE BLIND TEACHER: how much of 180/180 is clairvoyance?
#
# ============================================================================================
# REWRITTEN 2026-09-11 AFTER THIS SCRIPT LIED.
#
# The 2026-09-09 run died when the kernel power manager initiated a shutdown transition at
# 04:44 (System event 109; the box stayed down until 09-10 10:26). It was killed ~31 min into
# seed 42 — about 24 of 60 draws — and seeds 7 and 99 then failed in ~6 s each as the power
# transition tore things down.
#
# That part was the machine. THIS part was the script: it wrote D046B-BLIND-DONE
# UNCONDITIONALLY, so a batch that produced ZERO data reported success, and the monitor
# watching for that marker duly announced "COMPLETE". A gate that can only say yes is not a
# gate. The estate's own law — "found nothing" and "broke" must never be indistinguishable —
# was violated by the harness meant to enforce it.
#
# Four defects fixed, in the order they bit:
#   1. NO VERIFICATION   -> every seed must yield a LANDED line or the seed is marked FAILED
#                           and the run STOPS. DONE is written only if all seeds verified.
#   2. THE BOX SLEPT     -> SetThreadExecutionState(ES_CONTINUOUS|ES_SYSTEM_REQUIRED) holds the
#                           system awake for the life of the script, and releases on exit.
#   3. NOT RESUMABLE     -> per-seed result files. A seed already holding a verified LANDED line
#                           is skipped, so a killed run resumes instead of restarting 3.8 h.
#   4. STDERR DISCARDED  -> per-seed .err files. When it dies again, the tail says why.
# ============================================================================================
#
# WHAT IT MEASURES. GM_RFLY is a PRIVILEGED oracle: rfly_eval_candidate copies the Sim including
# eo_engine/eo_time, so candidates fly the TRUE realization and the search knows which engine
# fails and when, before it happens. That is what makes its 180/180 (D-046) an upper bound, and
# what makes its labels unlearnable by construction — they are conditioned on information the
# 39-D observation cannot contain.
#
# --rfly-blind hides only UNFIRED faults. An already-fired one keeps propagating (eo_fired is
# latched at sim.c:392, n_eng decremented, eng_health set) because a vehicle that has lost an
# engine legitimately knows it: OBS_EH0/EH1/EH2 carry per-engine chamber-pressure health, and
# App-G v2 added the self-sensed sf/wdot channel (the sf_z 52->37 signature, D-041). What is
# removed is ONLY the future — which, drawn from a seeded RNG with no observable precursor, is
# not merely unobserved but IRREDUCIBLY UNPREDICTABLE. No adaptation module can recover a coin
# flip that has not landed. That is why this run is not downstream of a privileged-input
# ablation: it IS the measurement of the only part of the latent that is actually hidden.
#
# PRE-REGISTERED READS (declared in D-046 before any of this ran):
#   ~45/60 => clairvoyance is worth ~15 draws; the teacher holds large TRANSFERABLE content and
#             Phase 3's 0/12 becomes a puzzle worth re-attacking with a LEGAL teacher.
#   ~6/60  => the 180/180 is MADE of clairvoyance; the teacher has almost nothing a deployed
#             system could receive, and Phase 3's null was STRUCTURAL, not a training failure.
# Anything between is reported as the interpolation it is, never rounded toward a story.
#
# METHODOLOGY: identical to runs/d046_rfly_frontier.ps1 except for --rfly-blind. Same exe, same
# scenario, same held-out seeds (42/7/99 — never in training data), same 60 draws, same
# --engine-out random. The faults are therefore the SAME sixty per seed that the clairvoyant arm
# and the MPPI control already flew. The only variable is what the SEARCH was allowed to see.
#
# COST ~76 s/run => ~76 min/seed => ~3.8 h. SEQUENTIAL: concurrency is a measured null here
# (ROADMAP Phase 2 — 4 procs at OMP=4 = 159.6 s/run effective vs 150 s/run single-process).
# HOLDS THE EXE: no C builds until the DONE or FAILED marker appears.

$ErrorActionPreference = "Stop"
$exe = "C:/Booster_Lander_Simulator/build/bin/Release/booster-core.exe"
$dir = "C:/Booster_Lander_Simulator/runs/d046b"
$out = "C:/Booster_Lander_Simulator/runs/d046b_blind_teacher.txt"
$seeds = @(42, 7, 99)

New-Item -ItemType Directory -Force -Path $dir | Out-Null

# ---- defect 2: hold the box awake. ES_CONTINUOUS(0x80000000) | ES_SYSTEM_REQUIRED(0x00000001)
Add-Type -Name Pwr -Namespace W32 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
# NOTE: PowerShell parses 0x80000000 as a SIGNED Int32, so `0x80000000 -bor 0x1` evaluates to
# -2147483647 and the P/Invoke refuses it ("too large or too small for a UInt32"). This killed the
# first launch silently, into a hidden window, with no stderr captured — the same class of defect
# this rewrite exists to remove. Type the constants explicitly.
$ES_CONTINUOUS      = [uint32]2147483648   # 0x80000000
$ES_SYSTEM_REQUIRED = [uint32]1            # 0x00000001
[void][W32.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED))

"D-046 1b BLIND TEACHER — GM_RFLY --rfly-blind, ENTRY --engine-out random x60, held-out seeds" | Out-File $out
"started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  (rewritten after the 09-09 false green)" | Out-File $out -Append
"clairvoyant arm, same faults (D-046): 60/60 s42 | 60/60 s7 | 60/60 s99 = 180/180" | Out-File $out -Append
"same-binary MPPI control on this batch: 4/60 (s42)   |   D-027 in-frontier bound ~59/60" | Out-File $out -Append
"pre-registered: ~45/60 => transferable content is large | ~6/60 => 180/180 is made of clairvoyance" | Out-File $out -Append
"" | Out-File $out -Append

$allok = $true
foreach ($s in $seeds) {
    $res = "$dir/seed_$s.txt"
    $err = "$dir/seed_$s.err"

    # ---- defect 3: resume. A seed that already verified is not re-flown.
    if ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
        "===== seed $s  (RESUMED — already complete) =====" | Out-File $out -Append
        Select-String -Path $res -Pattern "LANDED:|PERFECT|faults:|crash causes:|landed means:" |
            ForEach-Object { $_.Line.Trim() } | Out-File $out -Append
        "" | Out-File $out -Append
        continue
    }

    $t0 = Get-Date
    & $exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind --engine-out random `
        1> $res 2> $err
    $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)

    # ---- defect 1: VERIFY. Silence is not success.
    if (-not (Test-Path $res) -or -not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
        "===== seed $s  ($mins min) =====" | Out-File $out -Append
        "FAILED — no LANDED line. The run did not complete; this is NOT a zero-landing result." | Out-File $out -Append
        "stderr tail:" | Out-File $out -Append
        if (Test-Path $err) { Get-Content $err -Tail 12 | ForEach-Object { "   $_" } | Out-File $out -Append }
        "" | Out-File $out -Append
        "D046B-BLIND-FAILED  seed $s  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append
        $allok = $false
        break
    }

    "===== seed $s  ($mins min) =====" | Out-File $out -Append
    Select-String -Path $res -Pattern "LANDED:|PERFECT|faults:|crash causes:|landed means:" |
        ForEach-Object { $_.Line.Trim() } | Out-File $out -Append
    "" | Out-File $out -Append
}

if ($allok) { "D046B-BLIND-DONE  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append }

[void][W32.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)   # release the wake lock
