# r2b_warmstart.ps1 — R2b: does theta-hat WARM-STARTING the CEM cut the compute to reach the basin?
# The AlphaZero move: the search still runs; theta-hat just centres it, so a reduced budget can still
# land. Logs each trial as it finishes (identity-warm vs theta-warm across shrinking --rfly-budget).
$exe = "build\bin\Release\booster-core.exe"
$root = Split-Path -Parent $PSScriptRoot; Set-Location $root
$out = "runs\r2b_warmstart.txt"
"R2b WARM-START SWEEP :: compound s42 x12 :: $(Get-Date)" | Out-File $out
$c = "--headless","--scenario","entry","--seed","42","--runs","12","--rfly","--engine-out","random","--gust","15@6000:1000","--target","circle:15:40"
function Trial($label, $extra) {
  $t0 = Get-Date
  $line = (& $exe @c @extra 2>$null | Select-String "LANDED:" | Select-Object -First 1).Line.Trim()
  $dt = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
  $m = "{0,-24} {1,-42} {2,7}s" -f $label, $line, $dt
  $m | Tee-Object -Append $out
}
foreach ($b in "1.0", "0.5", "0.25", "0.125") {
  Trial "identity budget=$b" @("--rfly-budget", $b)
  Trial "theta-warm budget=$b" @("--rfly-warm-net", "--rfly-budget", $b)
}
"R2B-SWEEP-DONE" | Tee-Object -Append $out
