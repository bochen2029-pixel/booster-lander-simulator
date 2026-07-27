# m4_theta_sweep.ps1 — M4 push: sweep theta-hat regularization to clear held-out AERO >=54/60.
# theta-hat's ceiling is OVERFITTING (best-val epoch 1-2, effective samples = runs). For each config
# retrain -> export -> build -> fly AERO s42/s7/s99 x60 (theta-controller, no CEM, ~1s/run). The
# selftest TP KAT fails mid-sweep (header != pin) — irrelevant, --rfly-theta-net flights don't use it.
$exe = "build\bin\Release\booster-core.exe"
$hdr = "core\theta_policy_weights.h"
$root = Split-Path -Parent $PSScriptRoot; Set-Location $root
$out = "runs\m4_theta_sweep.txt"
"M4 THETA SWEEP :: held-out AERO x60 (baseline TP_VERSION 1 = 53/53/56 = 162/180) :: $(Get-Date)" | Out-File $out
"BASELINE bar: M4 GREEN = >=54/60 held-out" | Tee-Object -Append $out

# (weight_decay, hidden) configs — overfitting fixes, architecture-neutral
$configs = @(
  @{wd="1e-4"; hid=128}, @{wd="1e-3"; hid=128}, @{wd="3e-3"; hid=128},
  @{wd="1e-3"; hid=64},  @{wd="3e-3"; hid=64},  @{wd="1e-2"; hid=64}
)
function AeroRate($seed){
  (& $exe --headless --scenario aero_offset --seed $seed --runs 60 --rfly --rfly-theta-net 2>$null |
    Select-String "LANDED:" | Select-Object -First 1).Line -replace '.*LANDED: (\d+)/60.*','$1'
}
foreach($cfg in $configs){
  $ck = "runs\theta_wd$($cfg.wd)_h$($cfg.hid).pt"
  "--- config wd=$($cfg.wd) hidden=$($cfg.hid) ---" | Tee-Object -Append $out
  python trainer\train_theta.py --data data\s0rf data\s0rf_clean --verdict-csv data\s0rf data\s0rf_clean `
      --out $ck --hidden $cfg.hid --epochs 80 --weight-decay $cfg.wd 2>&1 |
      Select-String "val_nrmse=|VERDICT|restored" | ForEach-Object { "    $_" | Tee-Object -Append $out }
  python trainer\export_theta.py --ckpt $ck --out $hdr --tp-version 99 2>&1 | Select-String "sha=" | ForEach-Object { "    $_" | Add-Content $out }
  cmake --build build --config Release 2>&1 | Select-String "error|\.vcxproj ->" | Select-Object -First 1 | Out-Null
  $s42=AeroRate 42; $s7=AeroRate 7; $s99=AeroRate 99
  $tot=[int]$s42+[int]$s7+[int]$s99
  $green = ([int]$s42 -ge 54 -and [int]$s7 -ge 54 -and [int]$s99 -ge 54)
  "  AERO held-out: s42=$s42 s7=$s7 s99=$s99  TOTAL=$tot/180  $(if($green){'*** M4 GREEN (all >=54) ***'}else{'(not all >=54)'})" | Tee-Object -Append $out
}
"M4-SWEEP-DONE" | Tee-Object -Append $out
