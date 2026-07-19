# kprobe_analyze.ps1 — post-sweep analysis: rate + cause decomposition + run-indexed transition matrix.
# Per-run CSV schema (main.c): seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush
# verdict enum (state.h): 0=NONE 1=PERFECT 2=GOOD 3=HARD 4=TIPPED 5=CRASHED.  PAD_RADIUS=26.0  TD_V_HARD=6.0
# Cause taxonomy mirrors sim.c set_verdict + main.c: landed = verdict in {1,2,3}; among CRASHED/TIPPED (4,5):
#   fuel-out if fault==F_FUEL(1); else off-pad if td_lat>26; else too-hard if td_v>6; else other.
# Usage: powershell -NoProfile -File runs\kprobe_analyze.ps1 -Klist 512,1024   (256 optional if its CSV exists)
param([int[]]$Klist = @(512,1024))
$dir = "C:\Booster_Lander_Simulator\runs"
$PAD = 26.0; $TDVH = 6.0; $F_FUEL = 1

$data = @{}
foreach($K in $Klist){ $f="$dir\kprobe_aero_k$K.csv"; if(Test-Path $f){ $data[$K]=Import-Csv $f } else { Write-Host "MISSING per-run CSV for K=$K" } }

function IsLanded($r){ return ([int]$r.verdict -in 1,2,3) }
function OutClass($r){
    if([int]$r.verdict -in 1,2,3){ return 'LANDED' }
    if([int]$r.fault -eq 1){ return 'fuel' }           # F_FUEL
    if([double]$r.td_lat -gt 26.0){ return 'off-pad' }
    if([double]$r.td_v  -gt 6.0){ return 'too-hard' }
    return 'other'
}

Write-Host "===== RATE + CAUSE DECOMPOSITION ====="
Write-Host ("{0,-6} {1,-8} {2,-7} {3,-8} {4,-9} {5,-6} {6,-6} {7,-9} {8,-9}" -f "K","landed","rate%","off-pad","too-hard","fuel","other","td_v_mn","lat_mn")
foreach($K in $Klist){
    if(-not $data.ContainsKey($K)){ continue }
    $rows=$data[$K]; $n=$rows.Count
    $landed=@($rows | Where-Object { IsLanded $_ }).Count
    $off =@($rows | Where-Object { (OutClass $_) -eq 'off-pad' }).Count
    $hard=@($rows | Where-Object { (OutClass $_) -eq 'too-hard' }).Count
    $fuel=@($rows | Where-Object { (OutClass $_) -eq 'fuel' }).Count
    $oth =@($rows | Where-Object { (OutClass $_) -eq 'other' }).Count
    $ld =@($rows | Where-Object { IsLanded $_ })
    $tdv=if($ld.Count){ [math]::Round(($ld|Measure-Object td_v -Average).Average,3) }else{0}
    $lat=if($ld.Count){ [math]::Round(($ld|Measure-Object td_lat -Average).Average,3) }else{0}
    Write-Host ("{0,-6} {1,-8} {2,-7} {3,-8} {4,-9} {5,-6} {6,-6} {7,-9} {8,-9}" -f $K,"$landed/$n",[math]::Round(100.0*$landed/$n,1),$off,$hard,$fuel,$oth,$tdv,$lat)
}

for($i=0;$i -lt $Klist.Count-1;$i++){
    $lo=$Klist[$i]; $hi=$Klist[$i+1]
    if(-not ($data.ContainsKey($lo) -and $data.ContainsKey($hi))){ continue }
    Write-Host ""; Write-Host ("===== TRANSITION K=$lo -> K=$hi (run-indexed) =====")
    $loR=@{}; foreach($r in $data[$lo]){ $loR[[int]$r.run]=$r }
    $hiR=@{}; foreach($r in $data[$hi]){ $hiR[[int]$r.run]=$r }
    $trans=@{}; $flips=@()
    foreach($ri in ($loR.Keys | Sort-Object)){
        if(-not $hiR.ContainsKey($ri)){ continue }
        $a=OutClass $loR[$ri]; $b=OutClass $hiR[$ri]
        $k="$a -> $b"; if($trans.ContainsKey($k)){$trans[$k]++}else{$trans[$k]=1}
        if($a -ne $b){ $flips += [pscustomobject]@{run=$ri;from=$a;to=$b;tdv_lo=$loR[$ri].td_v;tdv_hi=$hiR[$ri].td_v;lat_lo=$loR[$ri].td_lat;lat_hi=$hiR[$ri].td_lat} }
    }
    Write-Host "-- class transitions --"; foreach($k in ($trans.Keys|Sort-Object)){ Write-Host ("  {0,-22} {1}" -f $k,$trans[$k]) }
    Write-Host ("-- {0} run(s) changed outcome class --" -f $flips.Count)
    if($flips.Count){ $flips | Sort-Object run | Format-Table run,from,to,tdv_lo,tdv_hi,lat_lo,lat_hi -AutoSize }
}
