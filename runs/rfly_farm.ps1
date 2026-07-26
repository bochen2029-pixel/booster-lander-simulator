# rfly_farm.ps1 — THE ORACLE TEACHER FARM (oracle-distill Phase 2, 2026-07-25).
#
# Fans GM_RFLY (the D-040 optimizer-in-the-loop that sweeps the compound 36/36) across the
# DISTURBANCE PRODUCT and taps every guidance tick, producing the near-optimal (o, a*) corpus the
# whole solver-then-distill program was waiting for. One process per seed => one .bin (tap rows) +
# one .csv (the per-run verdict manifest); the trainer joins them on (seed, run) and keeps only the
# runs that actually landed (train_s0.py --verdict-csv).
#
# HELD-OUT LAW (canon §13.6.3, ABSOLUTE): gate seeds 42/7/99 are refused here as defence in depth —
# the trainer refuses them too, but a farm that never writes them cannot create the temptation.
#
# COST (measured 2026-07-25 on the i9-9900K, 8C/16T): ~150 s per compound solve. Running N processes
# concurrently does NOT help — 4 concurrent x OMP=4 measured 159.6 s/run effective vs 150 s/run for
# one process at full threads, i.e. the CEM already saturates the box. So this farm is SEQUENTIAL by
# design and paced by a wall-clock deadline instead of a run count.
#
# Windows discipline (house law): launch me with Start-Process (never Start-Job); watch the
# FARM-COMPLETE marker or the per-seed artifacts (never a bare PID — Windows recycles them).

param(
  [int]$SeedBase      = 5000,
  [int]$Seeds         = 12,
  [int]$RunsPer       = 16,
  [string]$OutDir     = "data\s0rf",
  [string]$DeadlineLocal = "",     # e.g. "07:30" — stop starting new seeds after this local time
  [string]$Exe        = "build\bin\Release\booster-core.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir "farm.log"

function Log($m) {
  $line = "{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $m
  Write-Output $line
  Add-Content -Path $log -Value $line
}

$deadline = $null
if ($DeadlineLocal -ne "") {
  $deadline = [DateTime]::Parse($DeadlineLocal)
  if ($deadline -lt (Get-Date)) { $deadline = $deadline.AddDays(1) }   # a time already past means tomorrow
}

Log "FARM-START base=$SeedBase seeds=$Seeds runsPer=$RunsPer out=$OutDir deadline=$(if($deadline){$deadline}else{'none'})"

$done = 0
for ($i = 0; $i -lt $Seeds; $i++) {
  $seed = $SeedBase + $i
  if (@(42, 7, 99) -contains $seed) {
    Log "FARM-SKIP seed=$seed :: HELD-OUT GATE SEED (canon 13.6.3) — never farmed"
    continue
  }
  if ($deadline -and (Get-Date) -ge $deadline) {
    Log "FARM-DEADLINE reached before seed=$seed — stopping cleanly with $done seed(s) banked"
    break
  }

  # ---- the compound recipe for this seed: a distinct point in the disturbance PRODUCT.
  # Varying per SEED (not per run) is deliberate — the engine-out draw, the sea phase and the
  # target phase are already re-seeded per RUN inside the sim, so each seed contributes RunsPer
  # independent draws of ITS recipe, and the 12 recipes span the product.
  $gPeak = 8 + ($i % 5) * 4              # 8,12,16,20,24 m/s
  $gAlt  = 4000 + ($i % 3) * 1500        # 4000,5500,7000 m
  $gDir  = ($i * 47) % 360               # spread the bearing
  $tR    = 10 + ($i % 4) * 5             # circle radius 10,15,20,25 m
  $tT    = 30 + ($i % 3) * 15            # circle period 30,45,60 s
  $hs    = 1.0 + ($i % 3) * 0.75         # sea state 1.0,1.75,2.5 m

  $bin = Join-Path $OutDir "s$seed.bin"
  $csv = Join-Path $OutDir "s$seed.csv"
  $err = Join-Path $OutDir "s$seed.err"

  Log "FARM-SEED seed=$seed gust=$gPeak@${gAlt}:1000 dir=$gDir target=circle:${tR}:${tT} sea=$hs runs=$RunsPer"
  $t0 = Get-Date
  & $Exe --headless --scenario entry --seed $seed --runs $RunsPer --rfly `
      --engine-out random --gust "$gPeak@${gAlt}:1000" --gust-dir $gDir `
      --target "circle:${tR}:${tT}" --sea $hs --sea-wander 3 `
      --policy-log $bin --out $csv 2> $err | Select-String "LANDED:" | ForEach-Object { Log "FARM-RATE seed=$seed $($_.Line)" }
  $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)

  if (Test-Path $bin) {
    $mb = [math]::Round((Get-Item $bin).Length / 1MB, 1)
    Log "FARM-BANKED seed=$seed ${mb}MB in ${mins}min"
    $done++
  } else {
    Log "FARM-FAIL seed=$seed :: no tap file after ${mins}min — see $err"
  }
}

Log "FARM-COMPLETE seeds_done=$done"
