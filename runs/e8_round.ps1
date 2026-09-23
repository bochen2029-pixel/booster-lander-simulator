# E8 ROUND r — expert iteration for the critic (ROADMAP_NN-FLIGHT section 1.5), one command.
#
#   1. fly critic_v{r} on FRESH training seeds with --rfly-cand-log armed: every candidate the
#      critic's search proposes is rolled out on the plant and logged with the plant's cost
#      (designed=2 rows). The flight uses the critic; the log holds the plant's answer at exactly
#      the states the critic's own search reaches. Byte-identical to flying without the log.
#   2. retrain on the union: round-0 farm + every visited log so far -> critic_v{r+1}
#   3. fly critic_v{r+1} ONCE on held-out 42/7/99, arms B (1/32) and C (1/32 + confirm 2)
#
# Training seeds per round: 12 x 60 = 720 flights from a fresh band (7800 + 12r ...), so no round
# re-flies another's seeds. Held-out law: 42/7/99 and 92xx/93xx never appear here.
param(
  [Parameter(Mandatory=$true)][int]$Round,
  [int]$Seeds = 12,
  [string]$D = "D:\bl_e1_data\e8"
)
$ErrorActionPreference = "Continue"
$Exe = "C:\bl_e1\build_e9\bin\Release\booster-core.exe"
$log = Join-Path $D "round_$Round.log"
function Log($m){ ("[" + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + "] " + $m) | Out-File $log -Append }
Add-Type -Name Pwr -Namespace WE8R -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
[void][WE8R.Pwr]::SetThreadExecutionState([uint32]([uint32]2147483648 -bor [uint32]1))

$Wr  = Join-Path $D "critic_v$Round.w"
$Wn  = Join-Path $D "critic_v$($Round+1).w"
$vis = Join-Path $D "visited_$Round"
New-Item -ItemType Directory -Force $vis | Out-Null
if (-not (Test-Path $Wr)) { Log "ROUND-ABORT: no $Wr"; exit 1 }
Log "round $Round start: critic_v$Round -> visited_$Round -> critic_v$($Round+1)"

# 1. fly the critic on fresh training seeds, plant-labelled
$base = 7800 + 12 * ($Round - 1)
foreach ($k in 0..($Seeds-1)) {
  $s = $base + $k
  $cand = Join-Path $vis "s$s.cand"; $res = Join-Path $vis "s$s.txt"; $err = Join-Path $vis "s$s.err"
  if ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet)) { Log "  resumed s$s"; continue }
  if (Test-Path $cand) { Remove-Item $cand -Force }
  & $Exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind --rfly-event-replan `
         --rfly-budget 0.03125 --rfly-critic $Wr --rfly-cand-log $cand --policy-log (Join-Path $vis "s$s.bin") `
         --engine-out random 1> $res 2> $err
  if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) { Log "  FAILED s$s -- no LANDED line"; continue }
  Log ("  s$s " + (Select-String -Path $res -Pattern "LANDED:").Line.Trim() + " | rows " + [math]::Floor((Get-Item $cand).Length/(71*8)))
}
$n = @(Get-ChildItem (Join-Path $vis "s*.cand")).Count
if ($n -lt [math]::Max(4, $Seeds/2)) { Log "ROUND-ABORT: only $n visited logs"; exit 1 }

# 2. retrain on the union of the farm and every visited log so far
$dirs = @($D) + (1..$Round | ForEach-Object { Join-Path $D "visited_$_" })
Log ("  training critic_v$($Round+1) on: " + ($dirs -join ", "))
& python "C:\bl_e1\runs\e8_train_critic.py" --data ($dirs -join ",") --out $Wn --epochs 30 --hidden 256 `
    1> (Join-Path $D "train_v$($Round+1).out") 2> (Join-Path $D "train_v$($Round+1).err")
if (-not (Test-Path $Wn)) { Log "ROUND-ABORT: no $Wn produced"; Get-Content (Join-Path $D "train_v$($Round+1).err") -Tail 6 | ForEach-Object { Log "   $_" }; exit 1 }
Log ("  " + (Get-Content (Join-Path $D "train_v$($Round+1).out") | Select-String "EXPORTED" | Select-Object -Last 1).Line)

# 3. held-out, ONCE, arms B and C
$summary = @()
foreach ($arm in @(@{ n='B_budget1of32'; a=@('--rfly-budget','0.03125') }, @{ n='C_1of32_confirm2'; a=@('--rfly-budget','0.03125','--rfly-critic-confirm','2') })) {
  $tot = 0; $P = 0; $ok = $true
  foreach ($s in 42, 7, 99) {
    $res = Join-Path $D "critic_v$($Round+1)_$($arm.n)_s$s.txt"; $err = Join-Path $D "critic_v$($Round+1)_$($arm.n)_s$s.err"
    & $Exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind --rfly-event-replan --rfly-critic $Wn @($arm.a) --engine-out random 1> $res 2> $err
    if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) { Log "  FLIGHT FAILED $($arm.n) s$s"; $ok = $false; break }
    $line = (Select-String -Path $res -Pattern "LANDED:").Line.Trim(); $tot += [int]($line -replace '.*LANDED: (\d+)/.*','$1')
    $P += [int]((Select-String -Path $res -Pattern "PERFECT").Line -replace '.*PERFECT (\d+).*','$1')
    Log "  v$($Round+1) $($arm.n) s$s : $line"
  }
  if ($ok) { $summary += "$($arm.n)=$tot/180(P$P)" }
}
Log "ROUND $Round DONE: critic_v$($Round+1) held-out: $($summary -join ' | ')"
"E8-ROUND-DONE $Round $($summary -join ' | ') $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File (Join-Path $D "round_$($Round)_done.txt")
[void][WE8R.Pwr]::SetThreadExecutionState([uint32]2147483648)
