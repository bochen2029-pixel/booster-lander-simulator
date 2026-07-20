# E2d GATE BATTERY for NP_VERSION 7 — run AFTER export_weights --np-version 7 + KAT ceremony + rebuild.
# Detached. Floors from D-028: AERO clean 46, gust-A 45, ENTRY clean >=57. EO baseline (D-030): neural 8/4/2.
$exe = "C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"
$out = "C:\Booster_Lander_Simulator\runs\e2_gates.txt"
function L($k,$a){ $r=(& $exe @a 2>&1 | Select-String "LANDED|RESULT|SELFTEST" | Select-Object -First 1); "$k : $r" | Out-File $out -Append }
"E2 GATES (NP_VERSION 7) - $(Get-Date -Format 'HH:mm:ss')" | Out-File $out
"== leak / byte anchors (must be byte-identical: selftest PASS, TERMINAL 194/200, run-1 2.63/10.48) ==" | Out-File $out -Append
L "selftest"     @("--selftest")
L "TERMINAL x200" @("--headless","--scenario","terminal","--seed","42","--runs","200")
L "MPPI run-1"   @("--run","--scenario","aero_offset","--seed","42","--run","1","--mppi")
"== no-regression floors (target: AERO>=46, gust-A>=45, ENTRY-clean>=57) ==" | Out-File $out -Append
L "AERO clean s42"  @("--headless","--scenario","aero_offset","--seed","42","--runs","60","--neural")
L "gust-A s42"      @("--headless","--scenario","aero_offset","--seed","42","--runs","60","--neural","--gust","12@5000:800")
L "ENTRY clean s42" @("--headless","--scenario","entry","--seed","42","--runs","60","--neural")
"== THE HEADLINE: EO recovery eval (target: beat D-030 neural 8/4/2, toward reactive 9-10) ==" | Out-File $out -Append
L "EO neural s42" @("--headless","--scenario","entry","--seed","42","--runs","60","--neural","--engine-out","random")
L "EO neural s7"  @("--headless","--scenario","entry","--seed","7","--runs","60","--neural","--engine-out","random")
L "EO neural s99" @("--headless","--scenario","entry","--seed","99","--runs","60","--neural","--engine-out","random")
L "EO neural s42 (determinism pair)" @("--headless","--scenario","entry","--seed","42","--runs","60","--neural","--engine-out","random")
"E2-GATES-DONE" | Out-File $out -Append
