# rfly_farm_clean.ps1 — THE CLEAN-REGIME ORACLE FARM (oracle-distill Phase 2b, 2026-07-25).
#
# WHY THIS EXISTS (D-041 addendum 6). The pilot flight test scored AERO clean 0/60 — not because the
# architecture failed, but because a compound-only corpus contains no AERO_OFFSET states at all. The
# pre-existing clean datasets (data\s0, s0r1, s0g_*, s0e_*) are v1-width (36 col) and CANNOT be mixed
# with v2 rows; rowformat.read_rows refuses them by name, correctly. So the standing no-regression
# floors — AERO>=46, gust-A>=45, ENTRY-clean>=57 — are unreachable BY CONSTRUCTION for NP_VERSION 7
# unless those regimes are re-farmed in the v2 format. This farm does that.
#
# ONE TEACHER, ONE LABELLING CONVENTION. These regimes could be farmed far more cheaply with --mppi,
# but MPPI clamps its a_lat to its own +-3.2 sampler gamut while the reactive/RFLY stack does not
# (D-041 addendum 1). Mixing them would put two different label conventions in one corpus for no
# gain, so GM_RFLY teaches here too. It is also simply a better teacher on these regimes: measured
# on the probe, AERO clean landed 0.12 m from centre and ENTRY clean 0.03 m, against the 46/60 and
# 57/60 baselines those floors were set from.
#
# COST (measured 2026-07-25, sharing the box with the compound farm): AERO ~48 s/run solo, ENTRY
# clean ~152 s/run solo — roughly double each while the compound farm runs concurrently. Ordered by
# gate priority (AERO clean first: it is the M4 metric), and DEADLINE-paced, so whatever the window
# allows is banked cleanly rather than lost.
#
# Windows discipline: launch with Start-Process (never Start-Job); watch the marker, never a PID.

param(
  [string]$OutDir        = "data\s0rf_clean",
  [string]$DeadlineLocal = "07:30",
  [string]$Exe           = "build\bin\Release\booster-core.exe"
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

$deadline = [DateTime]::Parse($DeadlineLocal)
if ($deadline -lt (Get-Date)) { $deadline = $deadline.AddDays(1) }

# The work list, in GATE PRIORITY order. Seeds 6000+ are disjoint from the gate seeds {42,7,99} AND
# from the compound farm's 5000-5011, so the two farms can never collide in the merged corpus.
$jobs = @()
foreach ($i in 0..4) { $jobs += @{ tag = "aeroclean"; seed = 6000 + $i; scen = "aero_offset"; runs = 16; extra = @() } }
foreach ($i in 0..4) {
  $peak = 8 + ($i % 5) * 4                    # 8,12,16,20,24 m/s — spans the gust-A gate's 12 m/s
  $jobs += @{ tag = "aerogust"; seed = 6100 + $i; scen = "aero_offset"; runs = 16;
              extra = @("--gust", "$peak@5000:800", "--gust-dir", "$(($i*73)%360)") }
}
foreach ($i in 0..2) { $jobs += @{ tag = "entryclean"; seed = 6200 + $i; scen = "entry"; runs = 12; extra = @() } }

Log "FARM-START clean-regime oracle farm: $($jobs.Count) job(s), out=$OutDir, deadline=$deadline"

$done = 0
foreach ($j in $jobs) {
  if (@(42, 7, 99) -contains $j.seed) { Log "FARM-SKIP seed=$($j.seed) :: HELD-OUT GATE SEED"; continue }
  if ((Get-Date) -ge $deadline) {
    Log "FARM-DEADLINE reached — stopping cleanly with $done job(s) banked"
    break
  }
  $bin = Join-Path $OutDir "$($j.tag)_s$($j.seed).bin"
  $csv = Join-Path $OutDir "$($j.tag)_s$($j.seed).csv"
  $err = Join-Path $OutDir "$($j.tag)_s$($j.seed).err"
  Log "FARM-SEED $($j.tag) seed=$($j.seed) scen=$($j.scen) runs=$($j.runs) extra='$($j.extra -join ' ')'"
  $t0 = Get-Date
  & $Exe --headless --scenario $j.scen --seed $j.seed --runs $j.runs --rfly @($j.extra) `
      --policy-log $bin --out $csv 2> $err | Select-String "LANDED:" | ForEach-Object { Log "FARM-RATE $($j.tag) seed=$($j.seed) $($_.Line)" }
  $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
  if (Test-Path $bin) {
    Log "FARM-BANKED $($j.tag) seed=$($j.seed) $([math]::Round((Get-Item $bin).Length/1MB,1))MB in ${mins}min"
    $done++
  } else {
    Log "FARM-FAIL $($j.tag) seed=$($j.seed) :: no tap file after ${mins}min — see $err"
  }
}

Log "FARM-COMPLETE jobs_done=$done"
