# D-030 mode-independence: MPPI-with-D-030 on EO (also the E2 DAgger teacher assessment). Detached.
$exe = "C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"
$out = "C:\Booster_Lander_Simulator\runs\d030_mppi.txt"
"D-030 MPPI EO (teacher assessment) - $(Get-Date -Format 'HH:mm:ss')" | Out-File $out
foreach ($s in 42, 7) {
    $r = & $exe --headless --scenario entry --seed $s --runs 60 --mppi --engine-out random 2>&1 | Select-String "LANDED" | Select-Object -First 1
    "mppi s${s}: $r" | Out-File $out -Append
}
"D030-MPPI-DONE" | Out-File $out -Append
