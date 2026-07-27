# m4_aero_farm.ps1 — M4 data attempt: ~triple the AERO oracle coverage so theta-hat can train past
# the epoch-1 overfit (effective sample size = RUNS). Current AERO = 160 runs (80 clean + 80 gust);
# this adds ~288 more (seeds 6300-6317, GM_RFLY teacher, verdict-filtered downstream). Held-out law:
# seeds are disjoint from 42/7/99 AND from the existing farms (5xxx/6000-6202/7xxx). Deadline-paced.
param(
  [int]$SeedBase = 6300,
  [int]$Seeds    = 18,
  [int]$RunsPer  = 16,
  [string]$OutDir = "data\s0rf_m4aero",
  [string]$DeadlineLocal = "08:30",
  [string]$Exe = "build\bin\Release\booster-core.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot; Set-Location $root
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir "farm.log"
function Log($m){ $l="{0} {1}" -f (Get-Date -Format "HH:mm:ss"),$m; Write-Output $l; Add-Content $log $l }
$deadline=[DateTime]::Parse($DeadlineLocal); if($deadline -lt (Get-Date)){$deadline=$deadline.AddDays(1)}
Log "M4-AERO-FARM-START base=$SeedBase seeds=$Seeds runsPer=$RunsPer deadline=$deadline"
$done=0
for($i=0;$i -lt $Seeds;$i++){
  $seed=$SeedBase+$i
  if(@(42,7,99) -contains $seed){ Log "SKIP gate seed $seed"; continue }
  if((Get-Date) -ge $deadline){ Log "FARM-DEADLINE — stopping with $done banked"; break }
  # half clean AERO, half gusted AERO (the M4 metric is clean; gust adds robustness to the theta map)
  $gust = if($i % 2 -eq 0){ @() } else { $p=8+($i%5)*4; @("--gust","$p@5000:800","--gust-dir","$(($i*57)%360)") }
  $tag = if($i % 2 -eq 0){"clean"}else{"gust"}
  $bin="$OutDir\aero_${tag}_s$seed.bin"; $csv="$OutDir\aero_${tag}_s$seed.csv"; $err="$OutDir\aero_${tag}_s$seed.err"
  if((Test-Path $bin) -and (Test-Path $csv) -and ((Get-Content $csv | Measure-Object -Line).Lines -ge ($RunsPer+1))){ Log "SKIP seed=$seed (banked)"; $done++; continue }
  Log "FARM-SEED $tag seed=$seed runs=$RunsPer"
  $t0=Get-Date
  & $Exe --headless --scenario aero_offset --seed $seed --runs $RunsPer --rfly @gust `
      --policy-log $bin --out $csv 2>$err | Select-String "LANDED:" | ForEach-Object { Log "FARM-RATE seed=$seed $($_.Line)" }
  $mins=[math]::Round(((Get-Date)-$t0).TotalMinutes,1)
  if(Test-Path $bin){ Log "FARM-BANKED $tag seed=$seed $([math]::Round((Get-Item $bin).Length/1MB,1))MB ${mins}min"; $done++ }
  else{ Log "FARM-FAIL seed=$seed :: see $err" }
}
Log "M4-AERO-FARM-COMPLETE seeds_done=$done"
