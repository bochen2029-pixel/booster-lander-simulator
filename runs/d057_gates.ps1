# D-057 gate battery for the --rfly-pop-scale / --rfly-iters-scale build (build2).
# Two-sided: OFF must be byte-identical (selftest, TERMINAL x200, MPPI anchor, and a GM_RFLY flight
# with the flags at 1.0 vs absent); ON must demonstrably differ (evaluation counts in stderr).
$ErrorActionPreference = "Continue"
$exe = "C:/Booster_Lander_Simulator/build2/bin/Release/booster-core.exe"
$out = "C:/Booster_Lander_Simulator/runs/d057_gates.txt"
Set-Location C:/Booster_Lander_Simulator
"D-057 GATES  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  exe=$exe" | Out-File $out
$st = & $exe --selftest 2>&1 | Select-String "SELFTEST"
"selftest      : $st" | Out-File $out -Append
& $exe --headless --scenario terminal --seed 42 --runs 200 2>&1 | Out-File -Encoding utf8 runs/d057_terminal.txt
$a = (Get-Content runs/n0main_terminal.txt) -join "`n"; $b = (Get-Content runs/d057_terminal.txt) -join "`n"
"TERMINAL x200 : " + $(if ($a -eq $b) { "BYTE-IDENTICAL" } else { "*** DIFF - STOP, DO NOT COMMIT ***" }) | Out-File $out -Append
"MPPI run-1    : " + (& $exe --run --scenario aero_offset --seed 42 --run 1 --mppi 2>&1 | Select-String "RESULT") | Out-File $out -Append
# GM_RFLY leak: flags at 1.0 vs absent, 2 flights at 1/8 budget (fast), stdout must be identical
& $exe --headless --scenario entry --seed 42 --runs 2 --rfly --rfly-blind --rfly-event-replan --rfly-budget 0.125 --engine-out random 1> runs/d057_leak_a.txt 2> runs/d057_leak_a.err
& $exe --headless --scenario entry --seed 42 --runs 2 --rfly --rfly-blind --rfly-event-replan --rfly-budget 0.125 --engine-out random --rfly-pop-scale 1.0 --rfly-iters-scale 1.0 1> runs/d057_leak_b.txt 2> runs/d057_leak_b.err
$la = (Get-Content runs/d057_leak_a.txt) -join "`n"; $lb = (Get-Content runs/d057_leak_b.txt) -join "`n"
"RFLY leak 1.0 : " + $(if ($la -eq $lb -and $la.Length -gt 100) { "BYTE-IDENTICAL (" + $la.Length + " chars)" } else { "*** DIFF or EMPTY ***" }) | Out-File $out -Append
$sa = (Get-Content runs/d057_leak_a.err) -join "`n"; $sb = (Get-Content runs/d057_leak_b.err) -join "`n"
"RFLY leak err : " + $(if ($sa -eq $sb) { "stderr identical (no [rfly_budget] line at 1.0)" } else { "*** stderr differs at 1.0 ***" }) | Out-File $out -Append
# functional ON: pop 0.5 and iters 0.5 must print the [rfly_budget] line with the expected counts
& $exe --headless --scenario entry --seed 42 --runs 1 --rfly --rfly-blind --rfly-event-replan --engine-out random --rfly-pop-scale 0.5 1> runs/d057_on_pop.txt 2> runs/d057_on_pop.err
& $exe --headless --scenario entry --seed 42 --runs 1 --rfly --rfly-blind --rfly-event-replan --engine-out random --rfly-iters-scale 0.5 1> runs/d057_on_iters.txt 2> runs/d057_on_iters.err
"ON pop 0.5    : " + (Select-String -Path runs/d057_on_pop.err -Pattern "rfly_budget" | Select-Object -First 1).Line.Trim() | Out-File $out -Append
"ON iters 0.5  : " + (Select-String -Path runs/d057_on_iters.err -Pattern "rfly_budget" | Select-Object -First 1).Line.Trim() | Out-File $out -Append
"ON pop result : " + (Select-String -Path runs/d057_on_pop.txt -Pattern "LANDED:").Line.Trim() | Out-File $out -Append
"ON iters res. : " + (Select-String -Path runs/d057_on_iters.txt -Pattern "LANDED:").Line.Trim() | Out-File $out -Append
"D057-GATES-DONE $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append
Get-Content $out
