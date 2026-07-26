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
# 2. EXPORT the frozen fp64 header. Back the old one up first — it is a committed artifact and a
#    rejected NP_VERSION must be revertible without touching git (the D-031/D-032 revert pattern).
# ---------------------------------------------------------------------------------------------
Copy-Item $hdr "$hdr.bak" -Force
L "EXPORT: NP_VERSION=$NpVersion action-tier=$ActionTier (backup at $hdr.bak)"
python trainer\export_weights.py --ckpt $Ckpt --out $hdr --np-version $NpVersion --action-tier $ActionTier 2>&1 | Tee-Object -Append $out
if ($LASTEXITCODE -ne 0) { L "ABORT: exporter failed"; Copy-Item "$hdr.bak" $hdr -Force; exit 1 }

# ---------------------------------------------------------------------------------------------
# 3. REBUILD. The KAT will FAIL here — expected: the header changed but the pinned expectation in
#    main.c still describes the old policy. That failure is the point; it is what makes a silent
#    weights swap impossible.
# ---------------------------------------------------------------------------------------------
L "BUILD (1/2): expect the NP KAT to fail — the pin still describes the previous policy"
cmake --build build --config Release 2>&1 | Select-String -Pattern "error|\.vcxproj ->" | ForEach-Object { L "  $_" }
if ($LASTEXITCODE -ne 0) { L "ABORT: build failed"; exit 1 }

# ---------------------------------------------------------------------------------------------
# 4. RE-PIN THE KAT **FROM THE C PASS** (canon §13.5 — never from numpy: the accumulation order
#    differs, so a numpy-derived expectation is wrong in the low bits and the bit-exact gate is
#    silently defeated). --np-kat prints at %.17g, which round-trips a double exactly.
# ---------------------------------------------------------------------------------------------
$kat = & ".\$exe" --np-kat
L "KAT dump from THIS binary:"; $kat | ForEach-Object { L "  $_" }
$e0 = ($kat | Select-String 'EXP0 = (\S+);').Matches.Groups[1].Value
$e1 = ($kat | Select-String 'EXP1 = (\S+);').Matches.Groups[1].Value
$e2 = ($kat | Select-String 'EXP2 = (\S+);').Matches.Groups[1].Value
if (-not ($e0 -and $e1 -and $e2)) { L "ABORT: could not parse the KAT dump"; exit 1 }

$src = Get-Content core\main.c -Raw
# Target the REAL-WEIGHTS branch only: it writes three separate `const double EXPn = ...;` lines,
# while the NP_VERSION==0 placeholder branch puts all three on one line without spaces around '='.
$new = $src -replace '(?m)^(\s*)const double EXP0 = [^;]+;', "`${1}const double EXP0 = $e0;" `
            -replace '(?m)^(\s*)const double EXP1 = [^;]+;', "`${1}const double EXP1 = $e1;" `
            -replace '(?m)^(\s*)const double EXP2 = [^;]+;', "`${1}const double EXP2 = $e2;"
if ($new -eq $src) { L "ABORT: KAT substitution matched nothing — main.c layout changed, re-pin by hand"; exit 1 }
Set-Content core\main.c -Value $new -NoNewline
L "KAT re-pinned in core/main.c"

# ---------------------------------------------------------------------------------------------
# 5. REBUILD + SELFTEST. The selftest passing is the PROOF the re-pin was correct — the binary is
#    checking its own forward pass against the value it just reported.
# ---------------------------------------------------------------------------------------------
L "BUILD (2/2)"
cmake --build build --config Release 2>&1 | Select-String -Pattern "error|\.vcxproj ->" | ForEach-Object { L "  $_" }
if ($LASTEXITCODE -ne 0) { L "ABORT: rebuild failed"; exit 1 }
$st = & ".\$exe" --selftest 2>&1
$st | Select-String "policy KAT|SELFTEST" | ForEach-Object { L "  $_" }
if (-not ($st | Select-String "SELFTEST: PASS")) { L "ABORT: selftest FAILED after the re-pin"; exit 1 }

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
