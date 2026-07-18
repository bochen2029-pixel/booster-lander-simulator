# C15 graze-band grid: KI_WIND x trim-fade x EINT_CAP interaction with the D-012 burn-brake.
# The C14 trim (Ki 0.012 / fade (h-40)/160 / cap 2.0) was tuned BEFORE the overspeed brake
# existed; ENTRY's op-5 graze band (three runs within 2-6 m of the 26 m line) is residual wind
# drift the faded trim doesn't finish nulling. Patches sim.c in the _adapt_wt worktree.
$ErrorActionPreference = "Continue"
$wt  = "C:\Booster_Lander_Simulator\_adapt_wt"
$csv = "C:\Booster_Lander_Simulator\runs\d012_sweep3.csv"

# Recreate the worktree from the main tree (post-D-012-push source)
if(Test-Path $wt){ Remove-Item -Recurse -Force $wt -Confirm:$false }
New-Item -ItemType Directory -Force $wt | Out-Null
Copy-Item C:\Booster_Lander_Simulator\CMakeLists.txt $wt\
Copy-Item -Recurse C:\Booster_Lander_Simulator\core $wt\core
cmake -S $wt -B $wt\build -G "Visual Studio 17 2022" -A x64 2>&1 | Out-Null

"ki,fade,cap,selftest,terminal,e_land,e_op,e_th,e_fuel,a_land,a_op,a_th,a_fuel,score" | Set-Content $csv

function Parse-Batch([string]$out){
    $land=-1;$op=-1;$th=-1;$fu=-1
    if($out -match 'LANDED: (\d+)/'){ $land=[int]$Matches[1] }
    if($out -match 'off-pad (\d+)\s+too-hard (\d+)\s+fuel-out (\d+)'){
        $op=[int]$Matches[1];$th=[int]$Matches[2];$fu=[int]$Matches[3] }
    ,@($land,$op,$th,$fu)
}

# rows: (KI_WIND, fade_denom, EINT_CAP). Row 1 = shipped config (pipeline determinism check).
$grid = @(
    @(0.012,160,2.0),
    @(0.016,160,2.0),
    @(0.020,160,2.0),
    @(0.012,240,2.0),
    @(0.016,240,2.0),
    @(0.012,160,3.0)
)
foreach($g in $grid){
    $ki=$g[0]; $fd=$g[1]; $cap=$g[2]
    $c = Get-Content "$wt\core\sim.c" -Raw
    $c = $c -replace 'const double KI_WIND=[\d.]+, EINT_CAP=[\d.]+;', "const double KI_WIND=$ki, EINT_CAP=$cap;"
    $c = $c -replace 'double wfade=\(h_feet_w-40\.0\)/[\d.]+;', "double wfade=(h_feet_w-40.0)/$fd.0;"
    Set-Content "$wt\core\sim.c" $c

    cmake --build "$wt\build" --config Release 2>&1 | Out-Null
    $exe = "$wt\build\bin\Release\booster-core.exe"

    $stOut = (& $exe --selftest 2>&1) -join "`n"
    $selftest = if($stOut -match 'SELFTEST: PASS'){"PASS"}else{"FAIL"}
    $tOut = (& $exe --headless --scenario terminal --seed 42 --runs 200 2>&1) -join "`n"
    $term = if($tOut -match 'LANDED: (\d+)/200'){[int]$Matches[1]}else{-1}
    if($selftest -ne "PASS" -or $term -ne 194){
        "$ki,$fd,$cap,$selftest,$term,GATE,GATE,GATE,GATE,GATE,GATE,GATE,GATE,0" | Add-Content $csv
        continue
    }
    $eOut = (& $exe --headless --scenario entry --seed 42 --runs 100 2>&1) -join "`n"
    $e = Parse-Batch $eOut
    $aOut = (& $exe --headless --scenario aero_offset --seed 42 --runs 300 2>&1) -join "`n"
    $a = Parse-Batch $aOut
    $score = $e[0] + [math]::Round($a[0]*100.0/300.0,1)
    "$ki,$fd,$cap,$selftest,$term,$($e[0]),$($e[1]),$($e[2]),$($e[3]),$($a[0]),$($a[1]),$($a[2]),$($a[3]),$score" | Add-Content $csv
}
"DONE" | Add-Content $csv
