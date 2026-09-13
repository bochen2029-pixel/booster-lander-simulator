# D-055 — THE HEADLINE, VERIFIED ONCE ON A SEALED POOL.
#
# Seeds 42/7/99 informed a dozen conclusions across D-046..D-054 and seed 42 is directly
# contaminated (D-047's warm start was chosen BECAUSE a box-ceiling theta scored 13/60 there).
# They are development seeds now. SEALED_POOL.md seals 9200-9209, verified absent from the set of
# seeds ever flown. This is the ONE flight against them.
#
# THREE ARMS, paired on the identical 600 draws (10 seeds x 60), all zero-privilege:
#
#   identity            the old "reactive + D-030 baseline" -- 28/180 on the dev pool, and known
#                       NOT to be a baseline at all, just an unoptimised point. Carried as the
#                       control so the sealed ladder has its own floor rather than importing one.
#   constant theta      D-047's CEM winner -- 121/180 dev. Zero inference, 0.39 s/flight.
#   blind+event+1/8     D-054, THE DEPLOYABLE CONFIG -- 175/180 dev (97.2%) at 6.3 s/flight,
#                       ~0.5 s/replan against a 0.1 Hz outer loop. No privilege, no net, no
#                       teacher, no distillation.
#
# PRE-REGISTERED, before the run:
#   deployable within ~2 sigma of 97.2%  -> the headline is CONFIRMED on virgin seeds and may be
#                                           quoted without the "development number" qualifier.
#   materially below (say <93%)          -> the dev pool was optimistic; the honest figure is
#                                           whatever this returns, and D-052/D-054's numbers get
#                                           re-labelled in SCOREBOARD and PLAN.
#   materially above                     -> report it, but do NOT celebrate: an unexpected gain on
#                                           a fresh pool usually means the dev pool was hard, not
#                                           that the controller improved.
#
# Whatever it returns is the number. It is flown ONCE. No re-runs, no best-of.
#
# Farm-script law: verify every seed, never write DONE on silence, keep stderr, stay awake, and
# the monitor watches the DATA and the age of the newest output -- never a marker, never a bare
# process name.

$ErrorActionPreference = "Stop"
$exe  = "C:/Booster_Lander_Simulator/build2/bin/Release/booster-core.exe"
$dir  = "C:/Booster_Lander_Simulator/runs/d055"
$out  = "C:/Booster_Lander_Simulator/runs/d055_sealed_verify.txt"
$seeds = 9200..9209
New-Item -ItemType Directory -Force -Path $dir | Out-Null

Add-Type -Name Pwr -Namespace W55 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][W55.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

$IDENT = (@('1,1,1,1,1,1,1,1,0,1')) -join ''
$CONST = (Get-Content "C:/Booster_Lander_Simulator/runs/d047/best.json" -Raw | ConvertFrom-Json).theta -join ','

"D-055 SEALED-POOL VERIFICATION — seeds 9200-9209, flown ONCE" | Out-File $out
"started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append
"dev-pool references (42/7/99): identity 28/180 | constant 121/180 | blind+event+1/8 175/180 (97.2%)" | Out-File $out -Append
"pre-registered: deployable within ~2 sigma of 97.2% => headline CONFIRMED on virgin seeds" | Out-File $out -Append
"" | Out-File $out -Append

$arms = @(
  @{ name = 'identity';        args = @('--rfly-fixed', $IDENT) },
  @{ name = 'constant';        args = @('--rfly-fixed', $CONST) },
  @{ name = 'blind+event+1of8';args = @('--rfly-blind','--rfly-event-replan','--rfly-budget','0.125') }
)

$ok = $true
foreach ($arm in $arms) {
    $tot = 0; $n = 0
    "===== ARM: $($arm.name) =====" | Out-File $out -Append
    foreach ($s in $seeds) {
        $res = "$dir/$($arm.name)_$s.txt"; $err = "$dir/$($arm.name)_$s.err"
        if (-not ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet))) {
            & $exe --headless --scenario entry --seed $s --runs 60 --rfly @($arm.args) `
                   --engine-out random 1> $res 2> $err
        }
        if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
            "  seed $s FAILED — no LANDED line. NOT a zero result." | Out-File $out -Append
            if (Test-Path $err) { Get-Content $err -Tail 8 | ForEach-Object { "     $_" } | Out-File $out -Append }
            "D055-FAILED $($arm.name) seed $s" | Out-File $out -Append
            $ok = $false; break
        }
        $line = (Select-String -Path $res -Pattern "LANDED:").Line
        $l = [int]($line -replace '.*LANDED: (\d+)/.*','$1')
        $split = (Select-String -Path $res -Pattern "PERFECT").Line.Trim()
        $tot += $l; $n += 60
        "  seed $s : $l/60    $split" | Out-File $out -Append
    }
    if (-not $ok) { break }
    $pct = [math]::Round(100.0 * $tot / $n, 1)
    "  ---- $($arm.name) TOTAL: $tot/$n = $pct% ----" | Out-File $out -Append
    "" | Out-File $out -Append
}
if ($ok) { "D055-DONE  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append }
[void][W55.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
