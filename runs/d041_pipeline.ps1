# d041_pipeline.ps1 — the ORACLE-DISTILL Phase 3 ceremony, end to end (D-041).
#
# Runs: guard -> verdict-filtered train -> export NP_VERSION N (Tier-A full action) -> rebuild ->
#       KAT re-pin FROM THE C PASS -> rebuild -> selftest -> the full gate battery -> compound eval.
#
# WHY A SCRIPT. Every step here is a house law, and the expensive failures in this project's ledger
# are process failures, not physics failures: a KAT pinned from numpy instead of the C pass (canon
# §13.5), a gate run against a stale exe after a failed relink (the LNK1104 trap, D-017), a farm
# built over (LNK1104 again). Encoding the ceremony makes those unrepeatable.
#
# The KAT re-pin is the step that was previously done by hand — insert a temporary printf, copy three
# numbers, strip the printf. It is now `--np-kat` (which prints at %.17g, the precision that actually
# round-trips a double) plus a checked substitution, and the proof it worked is that the rebuilt
# binary's own selftest passes.
#
# USAGE (after FARM-COMPLETE):
#   pwsh -NoProfile -File runs\d041_pipeline.ps1 -NpVersion 7
#   pwsh -NoProfile -File runs\d041_pipeline.ps1 -NpVersion 7 -Hidden 128 -SkipTrain   # re-gate only

param(
  [int]$NpVersion  = 7,
  # BOTH corpora. Phase 2 (compound) alone cannot meet the no-regression floors — the pilot scored
  # AERO 0/60 purely because a compound-only corpus contains no AERO_OFFSET states, and the old
  # clean datasets are v1-width and correctly refused (D-041 addendum 6). Phase 2b supplies them.
  [string[]]$DataDir = @("data\s0rf", "data\s0rf_clean"),
  [string]$Ckpt    = "runs\s0rf.pt",
  [int]$Hidden     = 192,     # v6 was 128 (37k params). 192 -> ~82k, still trivial to evaluate in
                              # <10 us and to freeze as fp64 C. Raise only with evidence: capacity is
                              # a SUSPECTED limit, not a measured one.
  [int]$Epochs     = 300,
  [int]$ActionTier = 2,       # 2 = the net owns throttle (D-041). 1 reproduces the v6 arrangement.
  [switch]$SkipTrain,
  [switch]$SkipGates
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$exe  = "build\bin\Release\booster-core.exe"
$hdr  = "core\neural_policy_weights.h"
$out  = "runs\d041_pipeline.txt"
function L($m) { $line = "{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $m; Write-Output $line; Add-Content $out $line }

"D-041 PIPELINE :: $(Get-Date)" | Out-File $out

# ---------------------------------------------------------------------------------------------
# 0. GUARDS. Never build over a live farm (LNK1104: the link fails, and every gate below then
#    silently measures the OLD binary). Never train on a farm that is still writing.
# ---------------------------------------------------------------------------------------------
$live = Get-Process -Name booster-core -ErrorAction SilentlyContinue
if ($live) { L "ABORT: booster-core is running (pid $($live.Id -join ',')) — a build over a live farm is the LNK1104 trap"; exit 1 }
$bins = @(); $csvs = @()
foreach ($d in $DataDir) {
  $farmlog = Join-Path $d "farm.log"
  if (-not (Test-Path $farmlog)) { L "ABORT: no $farmlog — has that farm run?"; exit 1 }
  if (-not (Select-String -Path $farmlog -Pattern "FARM-COMPLETE|FARM-DEADLINE" -Quiet)) {
    L "ABORT: $d has not reached FARM-COMPLETE/FARM-DEADLINE — it is still banking seeds"; exit 1
  }
  $b = @(Get-ChildItem "$d\*.bin" -ErrorAction SilentlyContinue)
  if (-not $b) { L "ABORT: no .bin tap files in $d"; exit 1 }
  $bins += $b; $csvs += @(Get-ChildItem "$d\*.csv" -ErrorAction SilentlyContinue)
  L "GUARD-OK $d : $($b.Count) tap file(s)"
}
L "GUARD-OK total: $($bins.Count) tap file(s), $($csvs.Count) verdict manifest(s), $([math]::Round(($bins|Measure-Object Length -Sum).Sum/1GB,2)) GB across $($DataDir.Count) corpus dir(s)"

# ---------------------------------------------------------------------------------------------
# 1. TRAIN — verdict-filtered (the teacher is a SEARCH; its failures must never become labels)
#    and held-out-law enforced in code (train_s0.py refuses seeds 42/7/99 outright).
# ---------------------------------------------------------------------------------------------
if (-not $SkipTrain) {
  L "TRAIN: hidden=$Hidden epochs=$Epochs -> $Ckpt"
  python trainer\train_s0.py --data $DataDir --verdict-csv $DataDir --keep-verdicts 1,2 `
      --out $Ckpt --hidden $Hidden --epochs $Epochs 2>&1 | Tee-Object -Append $out
  if ($LASTEXITCODE -ne 0) { L "ABORT: trainer failed ($LASTEXITCODE)"; exit 1 }
}
if (-not (Test-Path $Ckpt)) { L "ABORT: no checkpoint at $Ckpt"; exit 1 }

# ---------------------------------------------------------------------------------------------
# 2-5. FREEZE + VERIFY — export, build, re-pin the KAT from the C pass, rebuild, selftest.
#      Factored into d041_export_build.ps1 so the DAgger loop (which repeats it every round) and
#      this one-shot path run the IDENTICAL ceremony. That sequence carries the house's sharpest
#      rule (the KAT is pinned from the C pass, never from numpy — canon §13.5); two copies of a
#      rule like that is one copy too many.
# ---------------------------------------------------------------------------------------------
pwsh -NoProfile -File runs\d041_export_build.ps1 -Ckpt $Ckpt -NpVersion $NpVersion -ActionTier $ActionTier -LogFile $out
if ($LASTEXITCODE -ne 0) { L "ABORT: the freeze ceremony failed"; exit 1 }

if ($SkipGates) { L "PIPELINE-DONE (gates skipped)"; exit 0 }

# ---------------------------------------------------------------------------------------------
# 6. THE GATE BATTERY. Leak anchors must be BYTE-IDENTICAL: GM_NEURAL is default-off, so a new
#    policy may not perturb hoverslam or MPPI by a single bit. Then the no-regression floors, then
#    the headline the whole arc is for.
# ---------------------------------------------------------------------------------------------
L "== LEAK ANCHORS (must be byte-identical) =="
& ".\$exe" --headless --scenario terminal --seed 42 --runs 200 2>&1 | Out-File -Encoding utf8 runs\d041_terminal.txt
$a = (Get-Content runs\n0main_terminal.txt) -join "`n"; $b = (Get-Content runs\d041_terminal.txt) -join "`n"
L ("TERMINAL x200 : " + $(if ($a -eq $b) { "BYTE-IDENTICAL" } else { "*** DIFF — STOP, DO NOT COMMIT ***" }))
L ("MPPI run-1    : " + (& ".\$exe" --run --scenario aero_offset --seed 42 --run 1 --mppi 2>&1 | Select-String "RESULT"))

L "== NO-REGRESSION FLOORS (AERO>=46, gust-A>=45, ENTRY-clean>=57) =="
foreach ($g in @(
    @{n = "AERO clean s42"; a = @("--headless", "--scenario", "aero_offset", "--seed", "42", "--runs", "60", "--neural") },
    @{n = "gust-A s42"; a = @("--headless", "--scenario", "aero_offset", "--seed", "42", "--runs", "60", "--neural", "--gust", "12@5000:800") },
    @{n = "ENTRY clean s42"; a = @("--headless", "--scenario", "entry", "--seed", "42", "--runs", "60", "--neural") })) {
  L ("$($g.n) : " + (& ".\$exe" @($g.a) 2>&1 | Select-String "LANDED"))
}

L "== THE HEADLINE: held-out COMPOUND, --neural vs the hoverslam 2/12 baseline =="
foreach ($s in 42, 7, 99) {
  L ("compound s$s : " + (& ".\$exe" --headless --scenario entry --seed $s --runs 12 --neural `
        --engine-out random --gust 15@6000:1000 --target circle:15:40 2>&1 | Select-String "LANDED"))
}
L "== M4 ATTEMPT (AERO >= 54/60 => M4 GREEN via GM_NEURAL) =="
foreach ($s in 7, 99) {
  L ("AERO s$s : " + (& ".\$exe" --headless --scenario aero_offset --seed $s --runs 60 --neural 2>&1 | Select-String "LANDED"))
}
L "PIPELINE-DONE"
