# e2_det_farm.ps1 — THE DE-RANDOMISED LEGAL TEACHER FARM (experiment E2, 2026-09-15).
#
# E1's farm (e1_legal_farm.ps1) made the labels legal and event-locked; its pilot corpus still showed
# the t=0 gain vector spread across runs at ~0.9 of the corpus width, i.e. the search picks an
# arbitrary point in a wide basin. E2 adds the two levers built for that (build_det, gates green):
#   --rfly-det-stream   the CEM sampler stream depends on the tick, not the run
#   --rfly-anchor-w W   a tie-break toward identity in the candidate cost
# Everything else is the deployable row: blind + event replan + 1/8 budget, ENTRY engine-out.
# Held-out law: seeds 42/7/99 never farmed. Corpus on D:. Resumable per seed.

param(
  [int]$SeedBase   = 7200,
  [int]$Seeds      = 12,
  [int]$RunsPer    = 60,
  [double]$Budget  = 0.125,
  [double]$AnchorW = 30,
  [string]$OutDir  = "D:\bl_e1_data\s2_det",
  [string]$Exe     = "C:\bl_e1\build_det\bin\Release\booster-core.exe"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir "farm.log"

function Log($m) {
  $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $m
  Write-Output $line
  Add-Content -Path $log -Value $line
}

Add-Type -Name Pwr -Namespace E2 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][E2.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

if (-not (Test-Path $Exe)) { Log "FARM-ABORT no exe at $Exe"; exit 2 }
Log "FARM-START base=$SeedBase seeds=$Seeds runsPer=$RunsPer budget=$Budget anchorW=$AnchorW det-stream=on out=$OutDir exe=$Exe"

$done = 0
for ($i = 0; $i -lt $Seeds; $i++) {
  $seed = $SeedBase + $i
  if (@(42, 7, 99) -contains $seed) { Log "FARM-SKIP seed=$seed held-out gate seed"; continue }
  $bin = Join-Path $OutDir "s$seed.bin"
  $csv = Join-Path $OutDir "s$seed.csv"
  $err = Join-Path $OutDir "s$seed.err"
  if ((Test-Path $csv) -and (Test-Path $bin) -and ((Get-Content $csv | Measure-Object -Line).Lines -ge ($RunsPer + 1))) {
    Log "FARM-RESUME seed=$seed already banked, skipping"; $done++; continue
  }
  Log "FARM-SEED seed=$seed runs=$RunsPer"
  $t0 = Get-Date
  & $Exe --headless --scenario entry --seed $seed --runs $RunsPer --rfly `
      --rfly-blind --rfly-event-replan --rfly-budget $Budget `
      --rfly-det-stream --rfly-anchor-w $AnchorW `
      --engine-out random --policy-log $bin --out $csv 2> $err |
    Select-String "LANDED:" | ForEach-Object { Log "FARM-RATE seed=$seed $($_.Line)" }
  $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
  if ((Test-Path $bin) -and ((Get-Item $bin).Length -gt 0)) {
    $mb = [math]::Round((Get-Item $bin).Length / 1MB, 1)
    Log "FARM-BANKED seed=$seed ${mb}MB in ${mins}min"
    $done++
  } else {
    Log "FARM-FAIL seed=$seed no tap file after ${mins}min (see $err)"
  }
}

[void][E2.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
Log "FARM-COMPLETE seeds_done=$done"
