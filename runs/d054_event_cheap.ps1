# D-054 — EVENT-TRIGGERED REPLAN AT FULL BUDGET: is the stale plan the LOC mechanism?
#
# THE HYPOTHESIS, and it is mechanical rather than statistical. The replan cadence is PURELY
# PERIODIC: RFLY_REPLAN_DT = 10 s, replans at t = 0, 10, 20, ... The fault fires at t in [4,18] s.
# So a fault at t=11 leaves the vehicle flying a plan computed for THREE engines while running on
# two, for up to NINE SECONDS, during the entry burn.
#
# That predicts exactly what D-046 add.3 measured: the blind arm's dominant failure is LOC (13 of
# its 22 crashes at full budget), and the clairvoyant arm — whose plan was never stale, because it
# knew the fault was coming — produced ZERO faults of any kind across 180 flights.
#
# So the earlier reading, "less optimization against a misspecified model is better", may be a
# SYMPTOM rather than the cause. The cheap search wins not because searching badly is good, but
# because it never commits hard to a plan that is about to become wrong. If that is right, the
# repair is not a smaller budget — it is REPLANNING WHEN THE WORLD CHANGES.
#
# --rfly-event-replan re-solves the moment the LEGAL sensed engine count changes. n_eng is the
# same §4.3-legal quantity D-030 switches its bank cap on; eng_health rides the socket as
# OBS_EH0/EH1/EH2. Knowing the fault BEFORE it fires is privilege; noticing it AFTER is what every
# flight computer does.
#
# ARMS, all on the identical 180 faults of held-out 42/7/99, all blind (no privilege):
#   full budget, periodic replan only   -> 158/180, 119P/34G/5H, FUEL 5 LOC 13   (D-046 add.3)
#   full budget + event replan          -> THIS RUN
#   1/8 budget, periodic only           -> 166/180,  24P/121G/21H, FUEL 2 LOC 3  (D-051)
#
# PRE-REGISTERED READS, declared before the run:
#   LOC falls toward ~3 AND the PERFECT count stays near 119  -> the stale plan WAS the mechanism,
#       and this is the best controller on the disk: full-budget precision with cheap-search
#       robustness. The "less search is better" finding is then a symptom with a named cause.
#   LOC falls but PERFECT collapses  -> event replanning helps by keeping theta near the default,
#       i.e. the same shrinkage the budget cut buys, and the two are one mechanism, not two.
#   LOC unchanged (~13)  -> the stale plan is NOT the mechanism. Misspecification stands as the
#       explanation and the budget curve becomes the main line. A clean, cheap kill either way.
#
# Farm-script law: verify every seed, never write DONE on silence, keep stderr, stay awake, and
# the monitor watches the DATA not the marker.

$ErrorActionPreference = "Stop"
$exe = "C:/Booster_Lander_Simulator/build2/bin/Release/booster-core.exe"
$dir = "C:/Booster_Lander_Simulator/runs/d054"
$out = "C:/Booster_Lander_Simulator/runs/d054_event_replan.txt"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

Add-Type -Name Pwr -Namespace W52 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][W52.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

"D-054 EVENT-TRIGGERED REPLAN at FULL budget, blind — is the stale plan the LOC mechanism?" | Out-File $out
"started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append
"reference, same 180 faults: blind@full+periodic = 158/180 (119P/34G/5H, FUEL 5 LOC 13)" | Out-File $out -Append
"                            blind@1/8 +periodic = 166/180 ( 24P/121G/21H, FUEL 2 LOC 3)" | Out-File $out -Append
"pre-registered: LOC->~3 with PERFECT~119 => stale plan was the mechanism | LOC~13 => it was not" | Out-File $out -Append
"" | Out-File $out -Append

$ok = $true
foreach ($s in 42, 7, 99) {
    $res = "$dir/seed_$s.txt"; $err = "$dir/seed_$s.err"
    if ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
        "===== seed $s (RESUMED) =====" | Out-File $out -Append
        Select-String -Path $res -Pattern "LANDED:|PERFECT|faults:|crash causes:|landed means:" |
            ForEach-Object { $_.Line.Trim() } | Out-File $out -Append
        "" | Out-File $out -Append; continue
    }
    $t0 = Get-Date
    & $exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind `
           --rfly-event-replan --rfly-budget 0.125 --engine-out random 1> $res 2> $err
    $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
    if (-not (Test-Path $res) -or -not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
        "===== seed $s ($mins min) =====" | Out-File $out -Append
        "FAILED — no LANDED line. NOT a zero result." | Out-File $out -Append
        if (Test-Path $err) { Get-Content $err -Tail 10 | ForEach-Object { "   $_" } | Out-File $out -Append }
        "D054-FAILED seed $s" | Out-File $out -Append; $ok = $false; break
    }
    "===== seed $s ($mins min) =====" | Out-File $out -Append
    Select-String -Path $res -Pattern "LANDED:|PERFECT|faults:|crash causes:|landed means:" |
        ForEach-Object { $_.Line.Trim() } | Out-File $out -Append
    "" | Out-File $out -Append
}
if ($ok) { "D054-DONE  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append }
[void][W52.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
