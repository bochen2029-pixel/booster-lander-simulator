# kprobe_drive2.ps1 — KPROBE-2 MPPI K-capacity sweep driver (D-013 probe).
# Successor to kprobe_sweep.ps1. Differences: (1) K=256 captured as row 1 in the pristine
# worktree (canonical bit-determinism repro + full cause breakdown), (2) EVERY stdout captured
# to runs/kprobe_k<K>_{selftest,term,aero}.txt so nothing is lost on interrupt, (3) exe-SHA
# stale-trap guard: refuse to trust an AERO batch unless the exe SHA changed after rebuild
# (K=256 skips the rebuild, it is the pristine tree), (4) records exe SHA per row.
#
# For each K: regex-patch _k1024_wt/core/guidance_mppi.h MPPI_K -> K, rebuild Release,
# confirm exe SHA changed (stale-trap), gate (selftest PASS + TERMINAL s42/200 == 194 EXACT),
# then AERO_OFFSET s42 x60 --mppi (per-run --out CSV), parse, append one row to kprobe_sweep.csv.
# Self-contained + survives the session.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File runs\kprobe_drive2.ps1 -Klist 256,512,1024
param(
    [int[]]$Klist = @(256,512,1024),
    [double]$BudgetMinutes = 240   # skip a K if elapsed > this at its start (wall-time guard)
)
$ErrorActionPreference = "Stop"
$wt   = "C:\Booster_Lander_Simulator\_k1024_wt"
$hdr  = "$wt\core\guidance_mppi.h"
$exe  = "$wt\build\bin\Release\booster-core.exe"
$csv  = "C:\Booster_Lander_Simulator\runs\kprobe_sweep.csv"
$log  = "C:\Booster_Lander_Simulator\runs\kprobe_drive2.log"
$outd = "C:\Booster_Lander_Simulator\runs"

function Log($m){
    $ts = (Get-Date -Format "HH:mm:ss")
    "$ts  $m" | Tee-Object -FilePath $log -Append | Out-Host
}

# CSV header (frozen column order). One row per K.
if(-not (Test-Path $csv)){
    "K,gate,selftest,terminal_194,landed,off_pad,too_hard,fuel_out,other,td_v_mean,td_v_max,lat_mean,fuel_mean,wall_s_total,wall_s_per_run,exe_sha8,notes" | Set-Content $csv
    Log "created $csv with header"
}

$sweepStart = Get-Date

foreach($K in $Klist){
    $elapsedMin = ((Get-Date) - $sweepStart).TotalMinutes
    if($elapsedMin -gt $BudgetMinutes){
        Log "BUDGET EXCEEDED ($([math]::Round($elapsedMin,1)) min > $BudgetMinutes) - skipping K=$K and beyond"
        break
    }
    Log "===== K=$K  (elapsed $([math]::Round($elapsedMin,1)) min) ====="

    # Skip if this K already has a real (gated-PASS, landed present) row — idempotent restart.
    $existing = Get-Content $csv | Select-String "^$K,"
    if($existing -and ($existing -match "^$K,PASS,.*,\d+,")){
        Log "K=$K already recorded with a landed count - SKIP (idempotent)"; continue
    }

    $shaBefore = if(Test-Path $exe){ (Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,8) } else { "none" }

    # 1. Patch MPPI_K in the worktree header (regex, in-place).
    $content = Get-Content $hdr -Raw
    $patched = [regex]::Replace($content, '(#define\s+MPPI_K\s+)\d+', "`${1}$K")
    Set-Content -Path $hdr -Value $patched -NoNewline
    $verify = (Select-String -Path $hdr -Pattern "define\s+MPPI_K\s+(\d+)").Matches[0].Groups[1].Value
    if($verify -ne "$K"){ Log "PATCH FAILED: header shows MPPI_K=$verify expected $K - SKIP"; continue }
    Log "patched MPPI_K -> $verify (exe SHA before build = $shaBefore)"

    # 2. Rebuild Release. Touch the header to force recompile of the MPPI TU.
    (Get-Item $hdr).LastWriteTime = Get-Date
    $bt0 = Get-Date
    $buildOut = cmake --build "$wt\build" --config Release 2>&1
    $bt1 = Get-Date
    $buildOut | Set-Content "$outd\kprobe_k${K}_build.txt"
    if(-not (Test-Path $exe)){ Log "BUILD FAILED (no exe) K=$K"; $buildOut | Select-Object -Last 15 | Out-Host; continue }
    $recompiledMppi = [bool]($buildOut | Select-String "guidance_mppi")
    $shaAfter = (Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,8)
    Log ("build OK {0:N1}s  mppi_recompiled={1}  exe SHA {2} -> {3}" -f ($bt1-$bt0).TotalSeconds, $recompiledMppi, $shaBefore, $shaAfter)

    # STALE-TRAP GUARD (killed 2 agents tonight): for K != 256 the exe MUST change vs before.
    # For K=256 in the pristine tree the SHA legitimately may equal main's (no source change),
    # so we only hard-require a change when we actually altered MPPI_K away from the built value.
    if($K -ne 256 -and $shaAfter -eq $shaBefore){
        Log "STALE-EXE TRAP: exe SHA did not change after MPPI_K=$K rebuild ($shaAfter). Forcing clean rebuild of the MPPI TU."
        # Force: delete the MPPI object + exe, rebuild.
        Remove-Item "$wt\build\core\booster-core.dir\Release\guidance_mppi.obj" -ErrorAction SilentlyContinue
        Remove-Item $exe -ErrorAction SilentlyContinue
        $buildOut2 = cmake --build "$wt\build" --config Release 2>&1
        $buildOut2 | Add-Content "$outd\kprobe_k${K}_build.txt"
        if(-not (Test-Path $exe)){ Log "FORCED BUILD FAILED K=$K"; continue }
        $shaAfter = (Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,8)
        Log "forced rebuild exe SHA now $shaAfter"
        if($shaAfter -eq $shaBefore){ Log "STILL STALE K=$K - recording GATE=FAIL, refusing batch"; "$K,FAIL,,,,,,,,,,,,,,$shaAfter,stale-exe-unresolved" | Add-Content $csv; continue }
    }

    # 3. GATE: selftest PASS + TERMINAL s42 x200 == 194 EXACTLY. Capture both.
    $stOut = & $exe --selftest 2>&1
    $stOut | Set-Content "$outd\kprobe_k${K}_selftest.txt"
    $selftest = if($stOut -match "SELFTEST:\s*PASS"){ "PASS" } else { "FAIL" }
    $termOut = & $exe --headless --scenario terminal --seed 42 --runs 200 2>&1
    $termOut | Set-Content "$outd\kprobe_k${K}_term.txt"
    $termLineM = $termOut | Select-String "LANDED:"
    $termLine = if($termLineM){ $termLineM.ToString() } else { "NO-LANDED-LINE" }
    $term194 = if($termLine -match "LANDED:\s*194/200"){ "194" } elseif($termLine -match "LANDED:\s*(\d+)/200"){ $Matches[1] } else { "?" }
    $gate = if($selftest -eq "PASS" -and $term194 -eq "194"){ "PASS" } else { "FAIL" }
    Log "GATE: selftest=$selftest terminal=$term194/200 -> $gate"
    if($gate -ne "PASS"){
        Log "GATE FAILED K=$K - recording row GATE=FAIL, NOT trusting batch numbers"
        "$K,FAIL,$selftest,$term194,,,,,,,,,,,,$shaAfter,gate-fail-STOP-diagnose" | Add-Content $csv
        Log "STOP per mission: gate failure at K=$K needs diagnosis before proceeding"
        break
    }

    # 4. AERO_OFFSET s42 x60 --mppi (the measurement). Per-run CSV via --out; summary to stdout.
    $percsv = "$outd\kprobe_aero_k$K.csv"
    $rt0 = Get-Date
    $aeroOut = & $exe --headless --scenario aero_offset --seed 42 --runs 60 --mppi --out $percsv 2>&1
    $rt1 = Get-Date
    $aeroOut | Set-Content "$outd\kprobe_k${K}_aero.txt"
    $wallTotal = ($rt1-$rt0).TotalSeconds
    $wallPer   = $wallTotal/60.0

    # 5. Parse the summary block (main.c lines 281/285/286).
    $landedLine = ($aeroOut | Select-String "LANDED:").ToString()
    $causeLine  = ($aeroOut | Select-String "crash causes:").ToString()
    $meanLineM  = ($aeroOut | Select-String "landed means:")
    $meanLine   = if($meanLineM){ $meanLineM.ToString() } else { "" }

    $landed = if($landedLine -match "LANDED:\s*(\d+)/60"){ $Matches[1] } else { "?" }
    $offpad = if($causeLine -match "off-pad\s+(\d+)"){ $Matches[1] } else { "?" }
    $toohard= if($causeLine -match "too-hard\s+(\d+)"){ $Matches[1] } else { "?" }
    $fuelout= if($causeLine -match "fuel-out\s+(\d+)"){ $Matches[1] } else { "?" }
    $other  = if($causeLine -match "other\s+(\d+)"){ $Matches[1] } else { "?" }
    $tdvMean= if($meanLine -match "td_v=([\d.]+)"){ $Matches[1] } else { "" }
    $tdvMax = if($meanLine -match "max\s+([\d.]+)"){ $Matches[1] } else { "" }
    $latMean= if($meanLine -match "lat=([\d.]+)"){ $Matches[1] } else { "" }
    $fuelMean=if($meanLine -match "fuel=([\d.]+)"){ $Matches[1] } else { "" }

    $row = "$K,$gate,$selftest,$term194,$landed,$offpad,$toohard,$fuelout,$other,$tdvMean,$tdvMax,$latMean,$fuelMean,$([math]::Round($wallTotal,1)),$([math]::Round($wallPer,2)),$shaAfter,ok"
    $row | Add-Content $csv
    Log "ROW: K=$K landed=$landed/60 off-pad=$offpad too-hard=$toohard fuel=$fuelout td_v=$tdvMean wall=$([math]::Round($wallTotal,1))s per_run=$([math]::Round($wallPer,2))s"
}

Log "===== KPROBE-2 SWEEP COMPLETE. Total $([math]::Round(((Get-Date)-$sweepStart).TotalMinutes,1)) min ====="
