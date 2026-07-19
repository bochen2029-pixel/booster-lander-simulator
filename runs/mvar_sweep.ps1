# mvar_sweep.ps1 — measure ONE MPPI config (LAMBDA_MIN, OU_THETA) in _mvar_wt.
# Fresh guidance_mppi.c from main each call (only the two knobs differ), rebuild with SHA-change
# verification (stale-exe defense), gate (selftest + TERMINAL 194), then AERO s<seed> x<runs> --mppi.
# Appends one row to runs/mvar_sweep.csv. Records everything. NEVER touches the real tree.
#
# Usage: mvar_sweep.ps1 -Config "L1_lammin0.5" -LambdaMin 0.5 -OuTheta 0.15 -Seed 42 -Runs 60 [-Notes "..."]
param(
  [Parameter(Mandatory=$true)][string]$Config,
  [Parameter(Mandatory=$true)][double]$LambdaMin,
  [Parameter(Mandatory=$true)][double]$OuTheta,
  [int]$Seed = 42,
  [int]$Runs = 60,
  [string]$Notes = ""
)
$ErrorActionPreference = "Continue"
$wt   = "C:\Booster_Lander_Simulator\_mvar_wt"
$main = "C:\Booster_Lander_Simulator"
$csv  = "$main\runs\mvar_sweep.csv"
$exe  = "$wt\build\bin\Release\booster-core.exe"
$cap  = "$main\runs\mvar_$Config.txt"

function Log($m){ $line="[$((Get-Date).ToString('HH:mm:ss'))] $m"; Write-Host $line; $line | Out-File $cap -Append -Encoding utf8 }

"=== CONFIG $Config  LAMBDA_MIN=$LambdaMin  OU_THETA=$OuTheta  seed=$Seed runs=$Runs ===" | Out-File $cap -Encoding utf8

# 1. Fresh source from main, patch the two knobs -----------------------------------------
Copy-Item -Force "$main\core\guidance_mppi.c" "$wt\core\guidance_mppi.c"
$src = Get-Content "$wt\core\guidance_mppi.c" -Raw
# LAMBDA_MIN is a `static const double LAMBDA_MIN = 2.0;`
$src = $src -replace 'static const double LAMBDA_MIN\s*=\s*[\d.]+;', "static const double LAMBDA_MIN = $LambdaMin;"
# OU_THETA is a `#define OU_THETA   0.15  ...`
$src = $src -replace '#define OU_THETA\s+[\d.]+', "#define OU_THETA   $OuTheta"
Set-Content "$wt\core\guidance_mppi.c" $src
# Verify the patch actually took
$chk = Get-Content "$wt\core\guidance_mppi.c" -Raw
$lm_ok = $chk -match "static const double LAMBDA_MIN = $([regex]::Escape($LambdaMin.ToString()))"
$ou_ok = $chk -match "#define OU_THETA\s+$([regex]::Escape($OuTheta.ToString()))"
Log "PATCH lambda_min_ok=$lm_ok ou_theta_ok=$ou_ok"

# 2. Rebuild with SHA-change verification ------------------------------------------------
$shaBefore = if(Test-Path $exe){ (Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16) } else { "none" }
$bt0 = Get-Date
$bout = cmake --build "$wt\build" --config Release 2>&1
$bwall = [math]::Round(((Get-Date)-$bt0).TotalSeconds,1)
$compiled = ($bout | Select-String 'guidance_mppi.c') -ne $null
$linkfail = ($bout | Select-String 'LNK1104') -ne $null
if($linkfail){ Log "BUILD LNK1104 (exe locked by a running batch!) — ABORT config"; "$Config,$LambdaMin,$OuTheta,$Seed,$Runs,LOCKED,LOCKED,locked,-1,-1,-1,-1,-1,,,,,,$Notes exe-locked" | Add-Content $csv; return }
$shaAfter = (Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16)
Log "BUILD wall=${bwall}s compiled_guidance_mppi=$compiled sha $shaBefore -> $shaAfter"

# 3. Gates: selftest + TERMINAL 194 ------------------------------------------------------
$stOut = (& $exe --selftest 2>&1) -join "`n"
$selftest = if($stOut -match 'SELFTEST: PASS'){"PASS"}else{"FAIL"}
$tOut = (& $exe --headless --scenario terminal --seed 42 --runs 200 2>&1) -join "`n"
$term = if($tOut -match 'LANDED: (\d+)/200'){[int]$Matches[1]}else{-1}
Log "GATE selftest=$selftest terminal=$term/200"
if($selftest -ne "PASS" -or $term -ne 194){
  Log "GATE FAIL — recording and aborting measurement (leak or broken build)"
  "$Config,$LambdaMin,$OuTheta,$Seed,$Runs,$selftest,$term,$shaAfter,-1,-1,-1,-1,-1,,,,,,$Notes GATE-FAIL" | Add-Content $csv
  return
}

# 4. AERO s<seed> x<runs> --mppi ---------------------------------------------------------
$aoutCsv = "$main\runs\mvar_${Config}_aero.csv"
$at0 = Get-Date
$aOut = (& $exe --headless --scenario aero_offset --seed $Seed --runs $Runs --mppi --out $aoutCsv 2>&1) -join "`n"
$awall = ((Get-Date)-$at0).TotalSeconds
$perrun = [math]::Round($awall/$Runs,2)
$aOut | Out-File $cap -Append -Encoding utf8

# 5. Parse the summary -------------------------------------------------------------------
$land=-1;$op=-1;$th=-1;$fu=-1;$oth=-1;$tdvm="";$tdvx="";$latm="";$fuelm=""
if($aOut -match 'LANDED: (\d+)/'){ $land=[int]$Matches[1] }
if($aOut -match 'off-pad (\d+)\s+too-hard (\d+)\s+fuel-out (\d+)\s+other (\d+)'){
  $op=[int]$Matches[1];$th=[int]$Matches[2];$fu=[int]$Matches[3];$oth=[int]$Matches[4] }
if($aOut -match 'td_v=([\d.]+) m/s \(max ([\d.]+)\)\s+lat=([\d.]+) m'){
  $tdvm=$Matches[1];$tdvx=$Matches[2];$latm=$Matches[3] }
if($aOut -match 'fuel=([\d.]+) kg'){ $fuelm=$Matches[1] }
Log "RESULT landed=$land/$Runs off-pad=$op too-hard=$th fuel-out=$fu other=$oth td_v_mean=$tdvm wall/run=${perrun}s"

"$Config,$LambdaMin,$OuTheta,$Seed,$Runs,$selftest,$term,$shaAfter,$land,$op,$th,$fu,$oth,$tdvm,$tdvx,$latm,$fuelm,$perrun,$Notes" | Add-Content $csv
Log "ROW APPENDED to mvar_sweep.csv"
