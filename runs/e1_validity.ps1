# E1 VALIDITY TABLES (D-029 deliverable) — the composite operator must beat the student AND the
# teacher head-to-head on the engine-out axis BEFORE it may teach (expert_iteration_design.md §3).
# ENTRY --engine-out random x60 x3 held-out seeds. Frontier ~1.000 (D-027) => recovery ~ landed/60.
# Determinism already confirmed bit-identical on the composite single run (2026-07-20). Detached via
# Start-Process. Ordered as per-seed TRIOS so the decisive s42 comparison lands first (~80 min).
$exe = "C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"
$out = "C:\Booster_Lander_Simulator\runs\e1_validity.txt"
function RunCell($label, $a) {
    $full = & $exe @a 2>&1
    $landed = ($full | Select-String "LANDED" | Select-Object -First 1)
    $means  = ($full | Select-String "landed means|off-pad|too-hard|fuel" | Select-Object -First 4)
    "$label : $landed" | Out-File $out -Append
    if ($means) { $means | ForEach-Object { "        $_" } | Out-File $out -Append }
    return $full
}
"E1 VALIDITY TABLES - ENTRY --engine-out random x60 - $(Get-Date -Format 'HH:mm:ss')" | Out-File $out
"composite=--mppi-warm-neural  student=--neural  teacher=--mppi ; frontier~1.000 (D-027) => recovery~landed/60" | Out-File $out -Append
foreach ($s in 42,7,99) {
    "" | Out-File $out -Append
    "== SEED $s ==" | Out-File $out -Append
    $c = RunCell "composite s$s" @("--headless","--scenario","entry","--seed","$s","--runs","60","--mppi-warm-neural","--engine-out","random")
    if ($s -eq 42) { $c | Out-File "C:\Booster_Lander_Simulator\runs\e1_comp_s42_full.txt" }  # cause anatomy
    RunCell "student  s$s" @("--headless","--scenario","entry","--seed","$s","--runs","60","--neural","--engine-out","random") | Out-Null
    RunCell "teacher  s$s" @("--headless","--scenario","entry","--seed","$s","--runs","60","--mppi","--engine-out","random") | Out-Null
    "SEED-$s-DONE" | Out-File $out -Append
}
"E1-VALIDITY-DONE" | Out-File $out -Append
