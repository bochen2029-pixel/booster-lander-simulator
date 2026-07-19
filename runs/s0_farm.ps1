# s0_farm.ps1 — the N1-S0 TEACHER DATA FARM (canon v2 §19.3-19.5; neural_policy_design §E).
# Runs sequential MPPI AERO batches with the --policy-log tap across TRAIN seeds,
# accumulating the S0 behavior-cloning dataset. Sequential on purpose: each batch
# saturates all cores via OpenMP; parallel batches would just contend.
#
# THE HELD-OUT LAW (canon §13.6.3, ABSOLUTE): gate seeds 42 / 7 / 99 are NEVER
# farmed. Train seeds live in the 1000+ range. The trainer additionally refuses
# gate-seed files in code — this script is the first fence, not the only one.
#
# Usage:  pwsh -File runs\s0_farm.ps1 [-Seeds 40] [-RunsPer 20] [-DeadlineLocal "09:15"]
# Output: data\s0\aero_s<seed>.bin (one per seed) + data\s0\farm.log (progress lines
#         — a Monitor tails this; every line is an event).

param(
  [int]$Seeds = 40,
  [int]$RunsPer = 20,
  [string]$DeadlineLocal = "09:15",
  [int]$SeedBase = 1000,
  [string]$TapFlag = "--policy-log",
  # D-023 DAgger: -Mode mppi  = round-0 (fly MPPI, log its own executed commands)
  #               -Mode neural = round-1+ (fly GM_NEURAL, log the SHADOW-MPPI teacher labels
  #               at the states the policy visits — neural_policy_design §B.1)
  [ValidateSet("mppi","neural")][string]$Mode = "mppi",
  [string]$OutDir = ""   # default: data\s0 for mppi, data\s0r1 for neural
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $repo "build\bin\Release\booster-core.exe"
$modeFlag = if ($Mode -eq "neural") { "--neural" } else { "--mppi" }
if ($OutDir -eq "") { $OutDir = if ($Mode -eq "neural") { "data\s0r1" } else { "data\s0" } }
$dataDir = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force $dataDir | Out-Null
$log = Join-Path $dataDir "farm.log"

function Log([string]$m) {
  $line = "$(Get-Date -Format 'HH:mm:ss') $m"
  Add-Content -Path $log -Value $line
  Write-Host $line
}

$deadline = Get-Date $DeadlineLocal
if ($deadline -lt (Get-Date)) { $deadline = $deadline.AddDays(1) }
Log "FARM-START seeds=$SeedBase..$($SeedBase+$Seeds-1) runsPer=$RunsPer deadline=$DeadlineLocal exe=$exe"

$done = 0
for ($i = 0; $i -lt $Seeds; $i++) {
  $seed = $SeedBase + $i
  if ($seed -in 42, 7, 99) { Log "FARM-SKIP seed=$seed (HELD-OUT LAW)"; continue }
  if ((Get-Date) -gt $deadline) { Log "FARM-DEADLINE reached after $done seeds"; break }
  $bin = Join-Path $dataDir "aero_s$seed.bin"
  $t0 = Get-Date
  & $exe --headless --scenario aero_offset --seed $seed --runs $RunsPer $modeFlag $TapFlag $bin 2>&1 |
    Select-String "LANDED:" | ForEach-Object { $_.Line } | Set-Variable -Name landedLine
  # Success = the tap file exists and is non-trivial. The headless EXIT CODE reflects the
  # LANDED rate, which is deliberately terrible for early DAgger rounds (-Mode neural flies
  # the not-yet-good policy to collect teacher labels) — it is NOT a farm failure signal.
  if (-not (Test-Path $bin) -or (Get-Item $bin).Length -lt 100000) {
    Log "FARM-BATCH-FAIL seed=$seed exit=$LASTEXITCODE binMissingOrTiny (continuing)"
    continue
  }
  $sz = (Get-Item $bin -ErrorAction SilentlyContinue).Length
  $dt = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
  $done++
  Log "FARM-SEED-DONE seed=$seed ${dt}min bytes=$sz  $landedLine"
}

$total = (Get-ChildItem $dataDir -Filter "aero_s*.bin" | Measure-Object -Property Length -Sum).Sum
Log "FARM-COMPLETE seeds_done=$done total_bytes=$total"
