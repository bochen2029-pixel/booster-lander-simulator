# d041_export_build.ps1 — THE WEIGHTS-FREEZE CEREMONY, factored out so there is exactly one copy.
#
# export -> build -> dump the KAT from the C pass -> re-pin it -> rebuild -> selftest.
#
# WHY IT LIVES ALONE. Both the one-shot pipeline (d041_pipeline.ps1) and the DAgger driver
# (d041_dagger.ps1, which repeats it every round) need this exact sequence, and it is the part with
# the house's sharpest rule attached: the KAT expectation is pinned FROM THE C PASS, never from
# numpy, whose accumulation order differs (canon §13.5). Two copies of a rule like that is one copy
# too many. The selftest passing at the end is the PROOF the re-pin was correct — the binary is
# checking its own forward pass against the value it just reported.
#
# Returns exit 0 on success, non-zero on any failure. Restores the previous header on export failure.

param(
  [Parameter(Mandatory = $true)][string]$Ckpt,
  [Parameter(Mandatory = $true)][int]$NpVersion,
  [int]$ActionTier = 2,
  [string]$BuildDir = "build",
  [string]$LogFile = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$hdr = "core\neural_policy_weights.h"
$exe = "$BuildDir\bin\Release\booster-core.exe"
function L($m) {
  $line = "{0} [freeze] {1}" -f (Get-Date -Format "HH:mm:ss"), $m
  Write-Output $line
  if ($LogFile) { Add-Content $LogFile $line }
}

if (-not (Test-Path $Ckpt)) { L "ABORT: no checkpoint at $Ckpt"; exit 1 }

Copy-Item $hdr "$hdr.bak" -Force
L "export NP_VERSION=$NpVersion action-tier=$ActionTier from $Ckpt"
python trainer\export_weights.py --ckpt $Ckpt --out $hdr --np-version $NpVersion --action-tier $ActionTier 2>&1 |
  ForEach-Object { L "  $_" }
if ($LASTEXITCODE -ne 0) { L "ABORT: exporter failed; restoring the previous header"; Copy-Item "$hdr.bak" $hdr -Force; exit 1 }

# Build 1. The KAT is EXPECTED to fail here: the header changed but the pin in main.c still describes
# the previous policy. That failure is the feature — it is what makes a silent weights swap impossible.
L "build 1/2 (the NP KAT should fail — the pin still describes the previous policy)"
cmake --build $BuildDir --config Release 2>&1 | Select-String -Pattern "error|\.vcxproj ->" | ForEach-Object { L "  $_" }
if ($LASTEXITCODE -ne 0) { L "ABORT: build failed"; exit 1 }

# Re-pin FROM THE C PASS. --np-kat prints at %.17g, the precision that actually round-trips a double;
# CHECKF's failure text prints %.9g and would be silently wrong in the low bits.
$kat = & ".\$exe" --np-kat
$e0 = ($kat | Select-String 'EXP0 = (\S+);').Matches.Groups[1].Value
$e1 = ($kat | Select-String 'EXP1 = (\S+);').Matches.Groups[1].Value
$e2 = ($kat | Select-String 'EXP2 = (\S+);').Matches.Groups[1].Value
if (-not ($e0 -and $e1 -and $e2)) { L "ABORT: could not parse --np-kat output"; exit 1 }
L "KAT from the C pass: $e0 | $e1 | $e2"

$src = Get-Content core\main.c -Raw
# Targets the REAL-WEIGHTS branch only: it writes three separate `const double EXPn = ...;` lines,
# while the NP_VERSION==0 placeholder branch puts all three on one line without spaces around '='.
#
# Verify each pattern MATCHES — do not infer success from the text having CHANGED. Those differ, and
# the difference bit once already: re-exporting the same checkpoint at a different --action-tier
# produces identical KAT values (the forward pass is unchanged; only whether a[2] is applied
# differs), so the substitution matched perfectly and produced byte-identical text, which a
# changed-text test reads as "matched nothing". Same for any idempotent re-run of the ceremony.
foreach ($p in '(?m)^\s*const double EXP0 = [^;]+;',
               '(?m)^\s*const double EXP1 = [^;]+;',
               '(?m)^\s*const double EXP2 = [^;]+;') {
  if ($src -notmatch $p) { L "ABORT: KAT pin pattern not found in core/main.c ($p) — layout changed, re-pin by hand"; exit 1 }
}
$new = $src -replace '(?m)^(\s*)const double EXP0 = [^;]+;', "`${1}const double EXP0 = $e0;" `
            -replace '(?m)^(\s*)const double EXP1 = [^;]+;', "`${1}const double EXP1 = $e1;" `
            -replace '(?m)^(\s*)const double EXP2 = [^;]+;', "`${1}const double EXP2 = $e2;"
if ($new -eq $src) { L "KAT pin already correct for this policy (unchanged) — proceeding" }
Set-Content core\main.c -Value $new -NoNewline

L "build 2/2"
cmake --build $BuildDir --config Release 2>&1 | Select-String -Pattern "error|\.vcxproj ->" | ForEach-Object { L "  $_" }
if ($LASTEXITCODE -ne 0) { L "ABORT: rebuild failed"; exit 1 }

$st = & ".\$exe" --selftest 2>&1
$st | Select-String "policy KAT|SELFTEST" | ForEach-Object { L "  $_" }
if (-not ($st | Select-String "SELFTEST: PASS")) { L "ABORT: selftest FAILED after the re-pin"; exit 1 }
L "frozen + verified: NP_VERSION $NpVersion is live in $BuildDir"
exit 0
