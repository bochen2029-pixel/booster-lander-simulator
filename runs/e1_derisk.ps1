# E1 DE-RISK — engine-out recovery anatomy on the CURRENT v6 exe (NP_VERSION 6).
# Farm-free, read-only measurement. Detached via Start-Process (NEVER Start-Job).
# Answers two questions before the composite build:
#  [1] Does the plain REACTIVE law (GM_HOVERSLAM, entry-divert-only) beat 1/60 on EO?
#      (isolates the mode-independent entry divert from the guidance mode)
#  [2] Is the --neural EO off-pad miss distribution near ~119 m (fine-closure = composite-fixable)
#      or ~1800 m (gross = entry-divert-phase, composite cannot help)?
$exe = "C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"
$out = "C:\Booster_Lander_Simulator\runs\e1_derisk.txt"
"E1 DE-RISK - engine-out recovery anatomy (v6 exe, NP_VERSION 6) - $(Get-Date -Format 'HH:mm:ss')" | Out-File $out
"" | Out-File $out -Append
"== [1] REACTIVE (GM_HOVERSLAM) --engine-out random x60 (the UNTESTED cell) ==" | Out-File $out -Append
foreach ($s in 42,7,99) {
    $r = & $exe --headless --scenario entry --seed $s --runs 60 --engine-out random 2>&1 | Select-String "LANDED" | Select-Object -First 1
    "  reactive s${s}: $r" | Out-File $out -Append
}
"" | Out-File $out -Append
"== [2] --neural --engine-out random x60 s42 (baseline recheck, aggregate) ==" | Out-File $out -Append
$rn = & $exe --headless --scenario entry --seed 42 --runs 60 --neural --engine-out random 2>&1 | Select-String "LANDED" | Select-Object -First 1
"  neural s42: $rn" | Out-File $out -Append
"" | Out-File $out -Append
"== [3] --neural EO s42 PER-RUN RESULT (off-pad miss distribution = fixable-fraction anatomy) ==" | Out-File $out -Append
for ($run=0; $run -lt 60; $run++) {
    $line = & $exe --run --scenario entry --seed 42 --run $run --neural --engine-out random 2>&1 | Select-String "RESULT" | Select-Object -First 1
    "  run ${run}: $line" | Out-File $out -Append
}
"EO-DERISK-DONE" | Out-File $out -Append
