# E8 — THE CANDIDATE FARM, full observation + designed candidates.
#
# The teacher whose JUDGMENT the critic must learn: blind, event replan, 1/32 budget (E6: 179/180
# dev at ~1 s/flight, sixteen plant rollouts per replan). At every replan the search's own
# population is logged with the rollout's cost and terminal summary, and a DESIGNED set of 41
# one-coordinate steps is evaluated and logged beside it (never enters the search: the flight is
# byte-identical to the deployed teacher). --policy-log is armed because it is what populates the
# run index in every row (s->tap.run).
#
# Row = 71 f64 (guidance_rfly.h RFLY_CAND_ROW): t, seed, run, big, iter, designed, obs39, mean10,
# cand10, cost, landed, td_v, td_lat, td_tilt, fuel_margin.
#
# Held-out law: 42/7/99 and the sealed bands 92xx/93xx are never farmed. Training seeds 7700-7723.
# Farm-script law: verify every seed by its LANDED line, never write DONE on silence, keep stderr,
# stay awake, and NEVER read a .cand while its writer is alive (E7's torn-file race).
param(
  [int]$SeedBase = 7700,
  [int]$Count    = 8,
  [string]$Exe   = "C:\bl_e1\build_e8\bin\Release\booster-core.exe",
  [string]$OutDir = "D:\bl_e1_data\e8"
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir ("farm_" + $SeedBase + ".log")
function Log($m){ ("[" + (Get-Date -Format 'HH:mm:ss') + "] " + $m) | Out-File $log -Append }

Add-Type -Name Pwr -Namespace WE8 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
[void][WE8.Pwr]::SetThreadExecutionState([uint32]([uint32]2147483648 -bor [uint32]1))

Log "E8 farm start base=$SeedBase count=$Count exe=$Exe"
for ($k = 0; $k -lt $Count; $k++) {
  $seed = $SeedBase + $k
  if (@(42, 7, 99) -contains $seed -or ($seed -ge 9200 -and $seed -le 9399)) { Log "FARM-SKIP seed=$seed held-out"; continue }
  $cand = Join-Path $OutDir "s$seed.cand"; $bin = Join-Path $OutDir "s$seed.bin"
  $res  = Join-Path $OutDir "s$seed.txt";  $err = Join-Path $OutDir "s$seed.err"
  if ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet)) { Log "RESUMED seed=$seed"; continue }
  if (Test-Path $cand) { Remove-Item $cand -Force }
  $t0 = Get-Date
  & $Exe --headless --scenario entry --seed $seed --runs 60 --rfly --rfly-blind --rfly-event-replan `
         --rfly-budget 0.03125 --rfly-cand-log $cand --rfly-cand-design --policy-log $bin `
         --engine-out random 1> $res 2> $err
  $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
  if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) { Log "FAILED seed=$seed after $mins min — no LANDED line"; continue }
  $rows = [math]::Floor((Get-Item $cand).Length / (71*8))
  Log ("DONE seed=$seed " + (Select-String -Path $res -Pattern "LANDED:").Line.Trim() + " | rows=$rows | $mins min")
}
Log "E8 farm END base=$SeedBase"
"E8-FARM-DONE $SeedBase" | Out-File (Join-Path $OutDir ("done_" + $SeedBase + ".txt"))
[void][WE8.Pwr]::SetThreadExecutionState([uint32]2147483648)
