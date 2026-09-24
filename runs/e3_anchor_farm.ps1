# e3_anchor_farm.ps1 — THE ANCHORED LEGAL TEACHER FARM (experiment E3, 2026-09-15).
#
# E1 (legal + event-locked labels) and E2 (plus a constant sampler stream) both left the search's
# pick spread across runs by ~1.0 of the corpus width at the same state, so the student's regression
# target averaged to identity, the one theta that crashes. E3 makes the pick smooth by construction:
# the search warm-starts at a theta that lands on its own (D-047's constant, 67% held-out) and
# tie-breaks toward it, so every label is "that constant plus the smallest consistent correction".
# Flags: blind + event replan + 1/8 budget (the deployable row) + --rfly-det-stream + --rfly-anchor
# + --rfly-anchor-w. Held-out law: seeds 42/7/99 never farmed. Corpus on D:. Resumable per seed.

param(
  [int]$SeedBase   = 7300,
  [int]$Seeds      = 12,
  [int]$RunsPer    = 60,
  [double]$Budget  = 0.125,
  [double]$AnchorW = 60,
  [string]$Anchor  = "3.9486512123718245,3.6070627977252863,3.0,1.1034248624801957,0.25,0.4,0.6043122432336955,0.5579795297922319,0.33269879919580125,0.7624253416827261",
  [string]$OutDir  = "D:\bl_e1_data\s3_anchor",
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

Add-Type -Name Pwr -Namespace E3 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][E3.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

if (-not (Test-Path $Exe)) { Log "FARM-ABORT no exe at $Exe"; exit 2 }
Log "FARM-START base=$SeedBase seeds=$Seeds runsPer=$RunsPer budget=$Budget anchorW=$AnchorW anchor=$Anchor det-stream=on out=$OutDir"

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
      --rfly-det-stream --rfly-anchor $Anchor --rfly-anchor-w $AnchorW `
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

[void][E3.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
Log "FARM-COMPLETE seeds_done=$done"
