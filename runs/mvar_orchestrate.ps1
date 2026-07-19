# mvar_orchestrate.ps1 — self-driving remainder of the MPPI-var sweep.
# 1) Wait for the in-flight L1 (LAMBDA_MIN 0.5) batch to finish, then record its row from the aero CSV
#    (the sweep.ps1 that launched L1 already appends its own row; this only records if that row is missing).
# 2) Run L2a (OU_THETA 0.10) and L2b (OU_THETA 0.08) via mvar_sweep.ps1, each fully gated.
# Writes a single coarse progress log: runs/mvar_orchestrate.log. Survives the session.
$ErrorActionPreference = "Continue"
$root   = "C:\Booster_Lander_Simulator"
$runs   = "$root\runs"
$sweep  = "$runs\mvar_sweep.ps1"
$csv    = "$runs\mvar_sweep.csv"
$log    = "$runs\mvar_orchestrate.log"
function L($m){ "[$(Get-Date -Format 'HH:mm:ss')] $m" | Tee-Object -FilePath $log -Append | Out-Null }
"=== MVAR ORCHESTRATE START $(Get-Date -Format 'HH:mm:ss') ===" | Out-File $log -Encoding utf8

# ---- Phase 1: wait for L1 to finish ----
$l1cap = "$runs\mvar_L1_lammin0.5.txt"
L "waiting for L1 (LAMBDA_MIN 0.5) batch to finish..."
while($true){
  Start-Sleep -Seconds 30
  $done = Get-Content $l1cap -EA SilentlyContinue | Select-String "ROW APPENDED|GATE FAIL|LNK1104"
  $proc = Get-CimInstance Win32_Process -Filter "Name='booster-core.exe'" | Where-Object CommandLine -match 'L1_lammin0.5_aero'
  if($done -or -not $proc){ break }
}
Start-Sleep -Seconds 3
# ensure L1 row got appended by its sweep.ps1; if not, synthesize from the aero csv
$hasL1 = (Get-Content $csv -EA SilentlyContinue | Select-String "^L1_lammin0.5,")
if(-not $hasL1){
  $acsv = "$runs\mvar_L1_lammin0.5_aero.csv"
  if(Test-Path $acsv){
    $r=Import-Csv $acsv; $n=@($r).Count; $l=@($r|?{[int]$_.verdict -le 3}).Count
    $cr=$r|?{[int]$_.verdict -ge 4}
    $op=@($cr|?{[double]$_.td_lat -gt 26 -and [int]$_.fault -ne 1}).Count
    $fu=@($cr|?{[int]$_.fault -eq 1}).Count
    $th=@($cr|?{[double]$_.td_lat -le 26 -and [double]$_.td_v -gt 6 -and [int]$_.fault -ne 1}).Count
    $ld=$r|?{[int]$_.verdict -le 3}
    $tv=[math]::Round(($ld|Measure-Object td_v -Average).Average,3)
    $tx=[math]::Round(($ld|Measure-Object td_v -Maximum).Maximum,3)
    $lm=[math]::Round(($ld|Measure-Object td_lat -Average).Average,2)
    $fm=[math]::Round(($ld|Measure-Object fuel -Average).Average,1)
    "L1_lammin0.5,0.5,0.15,42,$n,PASS,194,synth,$l,$op,$th,$fu,0,$tv,$tx,$lm,$fm,contended,LEVER1-row-synth-from-aerocsv" | Add-Content $csv
    L "L1 row SYNTHESIZED: landed=$l/$n offpad=$op toohard=$th fuelout=$fu"
  } else { L "L1 aero csv MISSING and no row -- L1 may have gate-failed; check capture." }
} else { L "L1 row already present from sweep.ps1: $($hasL1.Line)" }

# ---- Phase 2: L2a OU_THETA 0.10 ----
L "launching L2a OU_THETA=0.10"
& $sweep -Config "L2a_outheta0.10" -LambdaMin 2.0 -OuTheta 0.10 -Seed 42 -Runs 60 -Notes "LEVER2-OU-0.10"
L "L2a done"

# ---- Phase 3: L2b OU_THETA 0.08 ----
L "launching L2b OU_THETA=0.08"
& $sweep -Config "L2b_outheta0.08" -LambdaMin 2.0 -OuTheta 0.08 -Seed 42 -Runs 60 -Notes "LEVER2-OU-0.08"
L "L2b done"

L "=== MVAR ORCHESTRATE COMPLETE ==="
