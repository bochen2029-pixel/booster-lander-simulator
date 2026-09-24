# e1_legal_farm.ps1 — THE LEGAL TEACHER FARM (experiment E1, 2026-09-15).
#
# Farms the DEPLOYABLE controller (blind + event replan + 1/8 budget, the D-054/D-055 row) on the
# ENTRY engine-out battery and taps every guidance tick, so the gain labels a student sees are
# (a) legal (no future fault in the candidate rollouts), (b) event-locked (the gains jump when the
# sensed engine count changes), (c) at the budget the deployed controller actually runs.
# The July corpus (data/s0rf) came from the clairvoyant full-budget search; see
# C:\ONE\ONE_BOOSTER_NN-ROOT-CAUSE_2026-09-15_S3.md for why those labels were unlearnable.
#
# Held-out law: seeds 42/7/99 never farmed (refused here and in the trainer).
# Corpus lives on D: (C: has ~3 GB free). Sequential by design: the CEM saturates the box.
# Resumable: a seed whose .csv already carries RunsPer verdict lines is skipped.

param(
  [int]$SeedBase   = 7100,
  [int]$Seeds      = 12,
  [int]$RunsPer    = 60,
  [double]$Budget  = 0.125,
  [string]$OutDir  = "D:\bl_e1_data\s1_legal",
  [string]$Exe     = "C:\bl_e1\build\bin\Release\booster-core.exe"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir "farm.log"

function Log($m) {
  $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $m
  Write-Output $line
  Add-Content -Path $log -Value $line
}

# hold the box awake (the D-055 idiom; the 0x80000000 literal parses signed and must not be used)
Add-Type -Name Pwr -Namespace E1 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][E1.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

if (-not (Test-Path $Exe)) { Log "FARM-ABORT no exe at $Exe"; exit 2 }
Log "FARM-START base=$SeedBase seeds=$Seeds runsPer=$RunsPer budget=$Budget out=$OutDir exe=$Exe"

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

[void][E1.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
Log "FARM-COMPLETE seeds_done=$done"
