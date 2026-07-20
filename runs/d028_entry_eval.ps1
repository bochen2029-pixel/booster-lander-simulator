# D-028 ENTRY clean x60 --neural (NP_VERSION 6) three-seed held-out eval.
# Run DETACHED via Start-Process (a real OS process — NOT Start-Job, which dies
# with its parent session; that was the D-028 eval bug, 2026-07-19 22:45).
$exe = "C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"
$out = "C:\Booster_Lander_Simulator\runs\d028_entry_eval.txt"
"D-028 ENTRY clean x60 --neural (NP_VERSION 6) — $(Get-Date -Format 'HH:mm:ss')" | Out-File $out
foreach ($s in 42, 7, 99) {
    $r = & $exe --headless --scenario entry --seed $s --runs 60 --neural 2>&1 | Select-String "LANDED" | Select-Object -First 1
    "s${s}: $r" | Out-File $out -Append
}
"ENTRY-EVAL-DONE" | Out-File $out -Append
