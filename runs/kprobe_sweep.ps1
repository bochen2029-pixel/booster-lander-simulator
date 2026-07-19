# kprobe_sweep.ps1 - self-driving MPPI K-capacity sweep (KPROBE lane, D-013 probe).
# For each K: regex-patch _k1024_wt/core/guidance_mppi.h MPPI_K, rebuild Release, gate
# (selftest PASS + TERMINAL s42/200 == 194), run AERO_OFFSET s42 x60 --mppi, append one CSV row.
# Self-contained + survives the session. Writes runs/kprobe_sweep.csv (header if absent).
# Usage: powershell -File runs\kprobe_sweep.ps1 -Klist 512,1024   (256 row captured separately)
param(
    [int[]]$Klist = @(512,1024),
    [double]$BudgetMinutes = 210   # trim scope: skip a K if elapsed > this at its start
)
$ErrorActionPreference = "Stop"
$wt   = "C:\Booster_Lander_Simulator\_k1024_wt"
$hdr  = "$wt\core\guidance_mppi.h"
$exe  = "$wt\build\bin\Release\booster-core.exe"
$csv  = "C:\Booster_Lander_Simulator\runs\kprobe_sweep.csv"
$log  = "C:\Booster_Lander_Simulator\runs\kprobe_sweep.log"
$percsv_dir = "C:\Booster_Lander_Simulator\runs"

function Log($m){
    $ts = (Get-Date -Format "HH:mm:ss")
    "$ts  $m" | Tee-Object -FilePath $log -Append | Out-Host
}

# CSV header (frozen column order). One row per K.
if(-not (Test-Path $csv)){
    "K,gate,selftest,terminal_194,landed,off_pad,too_hard,fuel_out,other,td_v_mean,td_v_max,lat_mean,fuel_mean,wall_s_total,wall_s_per_run,notes" | Set-Content $csv
}

$sweepStart = Get-Date

foreach($K in $Klist){
    $elapsedMin = ((Get-Date) - $sweepStart).TotalMinutes
    if($elapsedMin -gt $BudgetMinutes){
        Log "BUDGET EXCEEDED ($([math]::Round($elapsedMin,1)) min > $BudgetMinutes) - skipping K=$K and beyond"
        break
    }
    Log "===== K=$K  (elapsed $([math]::Round($elapsedMin,1)) min) ====="

    # 1. Patch MPPI_K in the worktree header (regex, in-place).
    $content = Get-Content $hdr -Raw
    $patched = [regex]::Replace($content, '(#define\s+MPPI_K\s+)\d+', "`${1}$K")
    Set-Content -Path $hdr -Value $patched -NoNewline
    $verify = Select-String -Path $hdr -Pattern "define\s+MPPI_K\s+(\d+)" | ForEach-Object { $_.Matches[0].Groups[1].Value }
    if($verify -ne "$K"){ Log "PATCH FAILED: header shows MPPI_K=$verify expected $K - SKIP"; continue }
    Log "patched MPPI_K -> $verify"

    # 2. Rebuild Release (touch the header forces recompile of the MPPI TU).
    (Get-Item $hdr).LastWriteTime = Get-Date
    $bt0 = Get-Date
    $buildOut = cmake --build "$wt\build" --config Release 2>&1
    $bt1 = Get-Date
    $exeLine = $buildOut | Select-String "booster-core.exe"
    if(-not (Test-Path $exe)){ Log "BUILD FAILED (no exe) K=$K"; $buildOut | Select-Object -Last 15 | Out-Host; continue }
    Log ("build OK {0:N1}s  ({1})" -f ($bt1-$bt0).TotalSeconds, ($exeLine -replace '.*->\s*',''))

    # 3. GATE: selftest PASS + TERMINAL s42 x200 == 194 EXACTLY.
    $stOut = & $exe --selftest 2>&1
    $selftest = if($stOut -match "SELFTEST:\s*PASS"){ "PASS" } else { "FAIL" }
    $termOut = & $exe --headless --scenario terminal --seed 42 --runs 200 2>&1
    $termLine = ($termOut | Select-String "LANDED:").ToString()
    $term194 = if($termLine -match "LANDED:\s*194/200"){ "194" } else { ($termLine -replace '.*LANDED:\s*(\d+)/200.*','$1') }
    $gate = if($selftest -eq "PASS" -and $term194 -eq "194"){ "PASS" } else { "FAIL" }
    Log "GATE: selftest=$selftest terminal=$term194/200 -> $gate"
    if($gate -ne "PASS"){
        Log "GATE FAILED K=$K - recording row with GATE=FAIL, NOT trusting batch numbers"
        "$K,FAIL,$selftest,$term194,,,,,,,,,,,,gate-fail-leak-check" | Add-Content $csv
        continue
    }

    # 4. AERO_OFFSET s42 x60 --mppi (the measurement). Capture summary + per-run CSV.
    $percsv = "$percsv_dir\kprobe_aero_k$K.csv"
    $rt0 = Get-Date
    $aeroOut = & $exe --headless --scenario aero_offset --seed 42 --runs 60 --mppi --out $percsv 2>&1
    $rt1 = Get-Date
    $wallTotal = ($rt1-$rt0).TotalSeconds
    $wallPer   = $wallTotal/60.0

    # 5. Parse the summary block.
    $landedLine = ($aeroOut | Select-String "LANDED:").ToString()
    $causeLine  = ($aeroOut | Select-String "crash causes:").ToString()
    $meanLine   = ($aeroOut | Select-String "landed means:").ToString()

    $landed = if($landedLine -match "LANDED:\s*(\d+)/60"){ $Matches[1] } else { "?" }
    $offpad = if($causeLine -match "off-pad\s+(\d+)"){ $Matches[1] } else { "?" }
    $toohard= if($causeLine -match "too-hard\s+(\d+)"){ $Matches[1] } else { "?" }
    $fuelout= if($causeLine -match "fuel-out\s+(\d+)"){ $Matches[1] } else { "?" }
    $other  = if($causeLine -match "other\s+(\d+)"){ $Matches[1] } else { "?" }
    $tdvMean= if($meanLine -match "td_v=([\d.]+)"){ $Matches[1] } else { "?" }
    $tdvMax = if($meanLine -match "max\s+([\d.]+)"){ $Matches[1] } else { "?" }
    $latMean= if($meanLine -match "lat=([\d.]+)"){ $Matches[1] } else { "?" }
    $fuelMean=if($meanLine -match "fuel=([\d.]+)"){ $Matches[1] } else { "?" }

    $row = "$K,$gate,$selftest,$term194,$landed,$offpad,$toohard,$fuelout,$other,$tdvMean,$tdvMax,$latMean,$fuelMean,$([math]::Round($wallTotal,1)),$([math]::Round($wallPer,2)),ok"
    $row | Add-Content $csv
    Log "ROW: K=$K landed=$landed/60 off-pad=$offpad too-hard=$toohard fuel=$fuelout td_v=$tdvMean wall=$([math]::Round($wallTotal,1))s per_run=$([math]::Round($wallPer,2))s"
}

Log "===== SWEEP COMPLETE. Total $([math]::Round(((Get-Date)-$sweepStart).TotalMinutes,1)) min ====="
