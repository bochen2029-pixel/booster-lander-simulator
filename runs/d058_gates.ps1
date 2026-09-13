# D-058 gate battery for the IMU-platform build (build3). OFF must be byte-identical — including
# against the build2 exe the sweep is flying — and ON must demonstrably differ.
$ErrorActionPreference = "Continue"
$exe = "C:/Booster_Lander_Simulator/build3/bin/Release/booster-core.exe"
$exe2 = "C:/Booster_Lander_Simulator/build2/bin/Release/booster-core.exe"
$out = "C:/Booster_Lander_Simulator/runs/d058_gates.txt"
Set-Location C:/Booster_Lander_Simulator
"D-058 GATES  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  exe=$exe" | Out-File $out
$st = & $exe --selftest 2>&1
($st | Select-String "IMU platform|FAIL|SELFTEST") | ForEach-Object { "selftest      : " + $_.Line.Trim() } | Out-File $out -Append
& $exe --headless --scenario terminal --seed 42 --runs 200 2>&1 | Out-File -Encoding utf8 runs/d058_terminal.txt
$a = (Get-Content runs/n0main_terminal.txt) -join "`n"; $b = (Get-Content runs/d058_terminal.txt) -join "`n"
"TERMINAL x200 : " + $(if ($a -eq $b) { "BYTE-IDENTICAL" } else { "*** DIFF - STOP, DO NOT COMMIT ***" }) | Out-File $out -Append
"MPPI run-1    : " + (& $exe --run --scenario aero_offset --seed 42 --run 1 --mppi 2>&1 | Select-String "RESULT") | Out-File $out -Append
# cross-build leak: the deployable config, module absent, build3 vs build2 (2 flights, 1/8 budget)
& $exe  --headless --scenario entry --seed 7 --runs 2 --rfly --rfly-blind --rfly-event-replan --rfly-budget 0.125 --engine-out random 1> runs/d058_leak_b3.txt 2> runs/d058_leak_b3.err
& $exe2 --headless --scenario entry --seed 7 --runs 2 --rfly --rfly-blind --rfly-event-replan --rfly-budget 0.125 --engine-out random 1> runs/d058_leak_b2.txt 2> runs/d058_leak_b2.err
$la = (Get-Content runs/d058_leak_b3.txt) -join "`n"; $lb = (Get-Content runs/d058_leak_b2.txt) -join "`n"
"RFLY x-build  : " + $(if ($la -eq $lb -and $la.Length -gt 100) { "BYTE-IDENTICAL to build2 (" + $la.Length + " chars)" } else { "*** DIFF or EMPTY ***" }) | Out-File $out -Append
# ON: the asset default (180 deg/s) and a tight limit (30 deg/s), same 2 flights
& $exe --headless --scenario entry --seed 7 --runs 2 --rfly --rfly-blind --rfly-event-replan --rfly-budget 0.125 --engine-out random --imu-platform 1> runs/d058_on180.txt 2> runs/d058_on180.err
& $exe --headless --scenario entry --seed 7 --runs 2 --rfly --rfly-blind --rfly-event-replan --rfly-budget 0.125 --engine-out random --imu-platform 30 1> runs/d058_on30.txt 2> runs/d058_on30.err
"ON 180 deg/s  : " + ((Select-String -Path runs/d058_on180.txt -Pattern "LANDED:|imu:") | ForEach-Object { $_.Line.Trim() }) -join " | " | Out-File $out -Append
"ON  30 deg/s  : " + ((Select-String -Path runs/d058_on30.txt -Pattern "LANDED:|imu:") | ForEach-Object { $_.Line.Trim() }) -join " | " | Out-File $out -Append
$lo = (Get-Content runs/d058_on180.txt) -join "`n"
"ON differs    : " + $(if ($lo -ne $la) { "yes — the belief changes the flight (stdout differs from OFF)" } else { "*** NO — ON is identical to OFF: the belief is not reaching the controller ***" }) | Out-File $out -Append
"D058-GATES-DONE $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append
Get-Content $out
