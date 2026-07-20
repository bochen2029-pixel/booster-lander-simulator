# v6 ENTRY engine-out baseline: how much EO recovery comes FREE from ENTRY
# competence alone (the policy has never trained on engine-out). Sets the bar the
# expert-iteration EO teachers must beat. Detached via Start-Process (NOT Start-Job).
$exe = "C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"
$out = "C:\Booster_Lander_Simulator\runs\eo_baseline_v6.txt"
"v6 ENTRY --engine-out random x60 (neural vs mppi, held-out) — $(Get-Date -Format 'HH:mm:ss')" | Out-File $out
foreach ($s in 42, 7, 99) {
    $n = & $exe --headless --scenario entry --seed $s --runs 60 --neural --engine-out random 2>&1 | Select-String "LANDED" | Select-Object -First 1
    "s${s} neural: $n" | Out-File $out -Append
}
$m = & $exe --headless --scenario entry --seed 42 --runs 60 --mppi --engine-out random 2>&1 | Select-String "LANDED" | Select-Object -First 1
"s42 mppi(teacher): $m" | Out-File $out -Append
"EO-BASELINE-DONE" | Out-File $out -Append
