# D-051 — IS THE "DEPLOYABLE" CLAIM ACTUALLY TRUE? Fly blind AND at reduced budget, together.
#
# The claim made on 2026-09-11 was: blind GM_RFLY lands 158/180 with no privilege, and R2b showed
# the budget drops 8x with no rate loss (~0.4 s/replan against a 0.1 Hz outer loop), therefore a
# legal real-time controller exists.
#
# THAT COMPOSITION WAS NEVER FLOWN. 1b ran at FULL budget. R2b's 8x cut was measured on the
# CLAIRVOYANT search, months before --rfly-blind existed, and on the COMPOUND battery rather than
# this one. Multiplying two separately-measured factors and calling the product a result is
# exactly the error this repo keeps catching in other forms. Flagged by outside review; it is
# correct and it is the load-bearing claim of the night, so it gets measured rather than argued.
#
# ARMS, all on the identical 180 faults of held-out seeds 42/7/99:
#   blind @ budget 1.0    -> 158/180, already flown (D-046 add.3), the reference
#   blind @ budget 0.125  -> THIS RUN. If the rate holds, the deployable claim is real.
#
# PRE-REGISTERED READS (before the run):
#   holds within ~1 sigma of 158/180  -> the claim stands: legal AND real-time, measured end to end.
#   degrades to ~120-140/180          -> blindness and budget interact; the honest claim becomes
#                                        "legal at full budget, and a latency/rate trade below it",
#                                        and the trade curve has to be published rather than a point.
#   collapses below the constant's 121/180 -> the composition fails outright and the deployable
#                                        headline is withdrawn; an optimized constant would then
#                                        dominate it at 0.39 s/run.
#
# Farm-script law applies: verify every seed, never write DONE on silence, keep stderr, stay awake.

$ErrorActionPreference = "Stop"
$exe = "C:/Booster_Lander_Simulator/build2/bin/Release/booster-core.exe"
$dir = "C:/Booster_Lander_Simulator/runs/d051"
$out = "C:/Booster_Lander_Simulator/runs/d051_blind_realtime.txt"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

Add-Type -Name Pwr -Namespace W51 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][W51.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

"D-051 BLIND + REDUCED BUDGET — the deployable claim, measured end to end" | Out-File $out
"started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append
"reference, same 180 faults: blind@1.0 = 158/180 (87.8%) | constant = 121/180 | clairvoyant = 180/180" | Out-File $out -Append
"" | Out-File $out -Append

$ok = $true
foreach ($s in 42, 7, 99) {
    $res = "$dir/seed_$s.txt"; $err = "$dir/seed_$s.err"
    if ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
        "===== seed $s (RESUMED) =====" | Out-File $out -Append
        Select-String -Path $res -Pattern "LANDED:|PERFECT|faults:|landed means:" |
            ForEach-Object { $_.Line.Trim() } | Out-File $out -Append
        "" | Out-File $out -Append; continue
    }
    $t0 = Get-Date
    & $exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind `
           --rfly-budget 0.125 --engine-out random 1> $res 2> $err
    $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
    if (-not (Test-Path $res) -or -not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
        "===== seed $s ($mins min) =====" | Out-File $out -Append
        "FAILED — no LANDED line. NOT a zero result." | Out-File $out -Append
        if (Test-Path $err) { Get-Content $err -Tail 10 | ForEach-Object { "   $_" } | Out-File $out -Append }
        "D051-FAILED seed $s" | Out-File $out -Append; $ok = $false; break
    }
    "===== seed $s ($mins min) =====" | Out-File $out -Append
    Select-String -Path $res -Pattern "LANDED:|PERFECT|faults:|landed means:" |
        ForEach-Object { $_.Line.Trim() } | Out-File $out -Append
    "" | Out-File $out -Append
}
if ($ok) { "D051-DONE  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append }
[void][W51.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
