# e7_cand_farm.ps1 — THE CANDIDATE-LOG FARM (experiment E7, 2026-09-15).
# The deployable teacher (blind + event replan + 1/8 budget) with --rfly-cand-log: every candidate
# the search evaluates is logged (t, seed, run, big, 12 legal features, theta, rollout cost). The
# policy tap is armed too, only so the run index is populated in the candidate rows.
# Held-out law: 42/7/99 never farmed. Wake-locked (the inline loops without it died mid-run).

param(
  [int]$SeedBase   = 7601,
  [int]$Seeds      = 1,
  [int]$Step       = 1,
  [int]$RunsPer    = 60,
  [double]$Budget  = 0.125,
  [string]$OutDir  = "D:\bl_e1_data\e7",
  [string]$Exe     = "D:\bl_e1_data\bin\booster-core-e7.exe"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir ("farm_" + $SeedBase + ".log")
function Log($m) { $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $m; Write-Output $line; Add-Content -Path $log -Value $line }

Add-Type -Name Pwr -Namespace E7 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][E7.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

if (-not (Test-Path $Exe)) { Log "FARM-ABORT no exe at $Exe"; exit 2 }
Log "FARM-START base=$SeedBase seeds=$Seeds step=$Step runsPer=$RunsPer budget=$Budget"
$done = 0
for ($i = 0; $i -lt $Seeds; $i++) {
  $seed = $SeedBase + $i * $Step
  if (@(42, 7, 99) -contains $seed) { Log "FARM-SKIP seed=$seed held-out"; continue }
  $cand = Join-Path $OutDir "s$seed.cand"; $bin = Join-Path $OutDir "s$seed.bin"
  $csv  = Join-Path $OutDir "s$seed.csv";  $err = Join-Path $OutDir "s$seed.err"
  if ((Test-Path $csv) -and ((Get-Content $csv | Measure-Object -Line).Lines -ge ($RunsPer + 1))) { Log "FARM-RESUME seed=$seed banked"; $done++; continue }
  Log "FARM-SEED seed=$seed"
  $t0 = Get-Date
  & $Exe --headless --scenario entry --seed $seed --runs $RunsPer --rfly `
      --rfly-blind --rfly-event-replan --rfly-budget $Budget --engine-out random `
      --rfly-cand-log $cand --policy-log $bin --out $csv 2> $err |
    Select-String "LANDED:" | ForEach-Object { Log "FARM-RATE seed=$seed $($_.Line)" }
  $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
  if ((Test-Path $cand) -and ((Get-Item $cand).Length -gt 0)) { Log "FARM-BANKED seed=$seed $([math]::Round((Get-Item $cand).Length/1MB,1))MB cand in ${mins}min"; $done++ }
  else { Log "FARM-FAIL seed=$seed no candidate log after ${mins}min" }
}
[void][E7.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
Log "FARM-COMPLETE seeds_done=$done"
