# D-012 follow-up: deck-null grid (KVEL_NEAR x KVEL_SPLIT_H) at the winning BRAKE/VBLEND.
# The BRAKE sweep showed too-hard is brake-insensitive (~9 ENTRY / ~30 AERO flat) -> the deck
# null is the credible lever, and its 1.6/250 tuning predates the burn-brake. Patch the .c.
# Usage: pwsh -File sweep2.ps1 -Brake 1.2 -Vblend 6
param([double]$Brake = 1.2, [double]$Vblend = 6)
$ErrorActionPreference = "Continue"
$wt  = "C:\Booster_Lander_Simulator\_adapt_wt"
$csv = "C:\Booster_Lander_Simulator\runs\d012_sweep2.csv"

# Fresh source from the main tree, then pin the winning BRAKE/VBLEND
Copy-Item -Force C:\Booster_Lander_Simulator\core\guidance_hoverslam.c $wt\core\
Copy-Item -Force C:\Booster_Lander_Simulator\core\guidance_hoverslam.h $wt\core\
Copy-Item -Force C:\Booster_Lander_Simulator\core\guidance_mppi.c      $wt\core\
$h = Get-Content "$wt\core\guidance_hoverslam.h" -Raw
$h = $h -replace '#define KDIV_BRAKE\s+[\d.]+',  "#define KDIV_BRAKE   $Brake"
$h = $h -replace '#define KDIV_VBLEND\s+[\d.]+', "#define KDIV_VBLEND  $Vblend"
Set-Content "$wt\core\guidance_hoverslam.h" $h

"near,split,selftest,terminal,e_land,e_op,e_th,e_fuel,a_land,a_op,a_th,a_fuel,score" | Set-Content $csv

function Parse-Batch([string]$out){
    $land=-1;$op=-1;$th=-1;$fu=-1
    if($out -match 'LANDED: (\d+)/'){ $land=[int]$Matches[1] }
    if($out -match 'off-pad (\d+)\s+too-hard (\d+)\s+fuel-out (\d+)'){
        $op=[int]$Matches[1];$th=[int]$Matches[2];$fu=[int]$Matches[3] }
    ,@($land,$op,$th,$fu)
}

# NOTE: KVEL_NEAR/KVEL_SPLIT_H live in guidance_hoverslam.c; the MPPI rollout mirror hardcodes
# the same 250/1.6 ramp in guidance_mppi.c (house pattern) — patch BOTH (directive 7).
$grid = @(
    @(1.6,250),@(1.7,250),@(1.8,250),@(1.5,250),
    @(1.6,350),@(1.7,350)
)
foreach($g in $grid){
    $near=$g[0]; $split=$g[1]
    $c = Get-Content "$wt\core\guidance_hoverslam.c" -Raw
    $c = $c -replace '#define KVEL_SPLIT_H\s+[\d.]+', "#define KVEL_SPLIT_H  $split.0"
    $c = $c -replace '#define KVEL_NEAR\s+[\d.]+',    "#define KVEL_NEAR     $near"
    Set-Content "$wt\core\guidance_hoverslam.c" $c
    $m = Get-Content "$wt\core\guidance_mppi.c" -Raw
    $m = $m -replace 'double bk=\(250\.0-h_feet\)/250\.0', "double bk=($split.0-h_feet)/$split.0"
    $m = $m -replace 'double bk=\([\d.]+-h_feet\)/[\d.]+', "double bk=($split.0-h_feet)/$split.0"
    $m = $m -replace 'double kvd=kbase \+ bk\*\([\d.]+-kbase\)', "double kvd=kbase + bk*($near-kbase)"
    Set-Content "$wt\core\guidance_mppi.c" $m

    cmake --build "$wt\build" --config Release 2>&1 | Out-Null
    $exe = "$wt\build\bin\Release\booster-core.exe"

    $stOut = (& $exe --selftest 2>&1) -join "`n"
    $selftest = if($stOut -match 'SELFTEST: PASS'){"PASS"}else{"FAIL"}
    $tOut = (& $exe --headless --scenario terminal --seed 42 --runs 200 2>&1) -join "`n"
    $term = if($tOut -match 'LANDED: (\d+)/200'){[int]$Matches[1]}else{-1}
    if($selftest -ne "PASS" -or $term -ne 194){
        "$near,$split,$selftest,$term,GATE,GATE,GATE,GATE,GATE,GATE,GATE,GATE,0" | Add-Content $csv
        continue
    }
    $eOut = (& $exe --headless --scenario entry --seed 42 --runs 100 2>&1) -join "`n"
    $e = Parse-Batch $eOut
    $aOut = (& $exe --headless --scenario aero_offset --seed 42 --runs 300 2>&1) -join "`n"
    $a = Parse-Batch $aOut
    $score = $e[0] + [math]::Round($a[0]*100.0/300.0,1)
    "$near,$split,$selftest,$term,$($e[0]),$($e[1]),$($e[2]),$($e[3]),$($a[0]),$($a[1]),$($a[2]),$($a[3]),$score" | Add-Content $csv
}
"DONE" | Add-Content $csv
