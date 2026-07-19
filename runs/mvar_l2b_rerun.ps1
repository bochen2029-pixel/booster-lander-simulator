# mvar_l2b_rerun.ps1 — re-run ONLY the L2b (OU_THETA 0.08) AERO batch with the already-built
# _mvar_wt exe (SHA 3D1AD948, gates already PASSED at 20:17:27). The orchestrator was killed
# mid-L2b; the exe + source are correct (OU_THETA 0.08, LAMBDA_MIN 2.0), so no rebuild is needed.
# Re-verifies gates for safety, runs AERO s42 x60 --mppi, appends the sweep row.
$ErrorActionPreference = "Continue"
$root="C:\Booster_Lander_Simulator"; $runs="$root\runs"
$exe="$root\_mvar_wt\build\bin\Release\booster-core.exe"
$csv="$runs\mvar_sweep.csv"
$cap="$runs\mvar_L2b_outheta0.08.txt"
$acsv="$runs\mvar_L2b_outheta0.08_aero.csv"
function Log($m){ "[$(Get-Date -Format 'HH:mm:ss')] $m" | Tee-Object -FilePath $cap -Append | Out-Null }
"=== L2b RERUN OU_THETA=0.08 (exe SHA $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))) ===" | Out-File $cap -Encoding utf8

# re-verify gates (exe already built; cheap insurance the killed run didn't corrupt anything)
$sha=(Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16)
$stOut=(& $exe --selftest 2>&1) -join "`n"
$selftest= if($stOut -match 'SELFTEST: PASS'){"PASS"}else{"FAIL"}
$tOut=(& $exe --headless --scenario terminal --seed 42 --runs 200 2>&1) -join "`n"
$term= if($tOut -match 'LANDED: (\d+)/200'){[int]$Matches[1]}else{-1}
Log "GATE selftest=$selftest terminal=$term/200 sha=$sha"
if($selftest -ne "PASS" -or $term -ne 194){ Log "GATE FAIL - abort"; "L2b_outheta0.08,2.0,0.08,42,60,$selftest,$term,$sha,-1,-1,-1,-1,-1,,,,,,LEVER2-0.08-GATE-FAIL-rerun" | Add-Content $csv; return }

# AERO batch
$t0=Get-Date
$aOut=(& $exe --headless --scenario aero_offset --seed 42 --runs 60 --mppi --out $acsv 2>&1) -join "`n"
$awall=((Get-Date)-$t0).TotalSeconds
$perrun=[math]::Round($awall/60,2)
$aOut | Out-File $cap -Append -Encoding utf8

# parse
$land=-1;$op=-1;$th=-1;$fu=-1;$oth=-1;$tdvm="";$tdvx="";$latm="";$fuelm=""
if($aOut -match 'LANDED: (\d+)/'){ $land=[int]$Matches[1] }
if($aOut -match 'off-pad (\d+)\s+too-hard (\d+)\s+fuel-out (\d+)\s+other (\d+)'){ $op=[int]$Matches[1];$th=[int]$Matches[2];$fu=[int]$Matches[3];$oth=[int]$Matches[4] }
if($aOut -match 'td_v=([\d.]+) m/s \(max ([\d.]+)\)\s+lat=([\d.]+) m'){ $tdvm=$Matches[1];$tdvx=$Matches[2];$latm=$Matches[3] }
if($aOut -match 'fuel=([\d.]+) kg'){ $fuelm=$Matches[1] }
Log "RESULT landed=$land/60 off-pad=$op too-hard=$th fuel-out=$fu other=$oth td_v_mean=$tdvm wall/run=${perrun}s"
"L2b_outheta0.08,2.0,0.08,42,60,$selftest,$term,$sha,$land,$op,$th,$fu,$oth,$tdvm,$tdvx,$latm,$fuelm,$perrun,LEVER2-OU-0.08-rerun" | Add-Content $csv
Log "ROW APPENDED to mvar_sweep.csv"
