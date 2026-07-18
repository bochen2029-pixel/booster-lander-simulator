# D-012 adaptive-divert-gain sweep (fleet worktree _adapt_wt, self-driving).
# Grid over KDIV_BRAKE x KDIV_VBLEND on the v4 structure (powered-only overspeed brake).
# One CSV row per config; selftest + TERMINAL-194 gate recorded per row. Survives the session.
$ErrorActionPreference = "Continue"
$wt  = "C:\Booster_Lander_Simulator\_adapt_wt"
$csv = "C:\Booster_Lander_Simulator\runs\d012_sweep.csv"

# Fresh source from the main tree (v4, verified selftest+TERMINAL green before launch)
Copy-Item -Force C:\Booster_Lander_Simulator\core\guidance_hoverslam.c $wt\core\
Copy-Item -Force C:\Booster_Lander_Simulator\core\guidance_hoverslam.h $wt\core\
Copy-Item -Force C:\Booster_Lander_Simulator\core\guidance_mppi.c      $wt\core\

"brake,vblend,selftest,terminal,e_land,e_op,e_th,e_fuel,a_land,a_op,a_th,a_fuel,score" | Set-Content $csv

function Parse-Batch([string]$out){
    $land=-1;$op=-1;$th=-1;$fu=-1
    if($out -match 'LANDED: (\d+)/'){ $land=[int]$Matches[1] }
    if($out -match 'off-pad (\d+)\s+too-hard (\d+)\s+fuel-out (\d+)'){
        $op=[int]$Matches[1];$th=[int]$Matches[2];$fu=[int]$Matches[3] }
    ,@($land,$op,$th,$fu)
}

$grid = @(
    @(1.05,3),@(1.05,6),@(1.05,10),
    @(1.2,3),          @(1.2,10),
    @(1.35,3),@(1.35,6),@(1.35,10),
    @(1.5,3), @(1.5,6), @(1.5,10)
)
foreach($g in $grid){
    $brake=$g[0]; $vb=$g[1]
    $h = Get-Content "$wt\core\guidance_hoverslam.h" -Raw
    $h = $h -replace '#define KDIV_BRAKE\s+[\d.]+',  "#define KDIV_BRAKE   $brake"
    $h = $h -replace '#define KDIV_VBLEND\s+[\d.]+', "#define KDIV_VBLEND  $vb"
    Set-Content "$wt\core\guidance_hoverslam.h" $h

    cmake --build "$wt\build" --config Release 2>&1 | Out-Null
    $exe = "$wt\build\bin\Release\booster-core.exe"

    $stOut = (& $exe --selftest 2>&1) -join "`n"
    $selftest = if($stOut -match 'SELFTEST: PASS'){"PASS"}else{"FAIL"}

    $tOut = (& $exe --headless --scenario terminal --seed 42 --runs 200 2>&1) -join "`n"
    $term = if($tOut -match 'LANDED: (\d+)/200'){[int]$Matches[1]}else{-1}

    if($selftest -ne "PASS" -or $term -ne 194){
        "$brake,$vb,$selftest,$term,GATE,GATE,GATE,GATE,GATE,GATE,GATE,GATE,0" | Add-Content $csv
        continue
    }

    $eOut = (& $exe --headless --scenario entry --seed 42 --runs 100 2>&1) -join "`n"
    $e = Parse-Batch $eOut
    $aOut = (& $exe --headless --scenario aero_offset --seed 42 --runs 300 2>&1) -join "`n"
    $a = Parse-Batch $aOut

    $score = $e[0] + [math]::Round($a[0]*100.0/300.0,1)
    "$brake,$vb,$selftest,$term,$($e[0]),$($e[1]),$($e[2]),$($e[3]),$($a[0]),$($a[1]),$($a[2]),$($a[3]),$score" | Add-Content $csv
}
"DONE" | Add-Content $csv
