# D-035 SEA deck — leak anchor #4 + heaving-deck landing rate (honest, new capability) + determinism pair.
# Guidance is DECK-BLIND in 1b (hoverslam targets z=0, not deck_z(t), per §1.2/§A.4) — the rate is the
# honest floor that motivates deck-aware vertical guidance (the 1b-follow-up). deck_vz leg-load coupling
# (§A.2) IS wired. TERMINAL scenario = clean vertical landing, so --sea isolates the heave effect.
$exe = "C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"
$out = "C:\Booster_Lander_Simulator\runs\d035_sea.txt"
function L($k,$a){ $r=(& $exe @a 2>&1 | Select-String "LANDED|RESULT|SELFTEST" | Select-Object -First 1); "$k : $r" | Out-File $out -Append }
"D-035 SEA - $(Get-Date -Format 'HH:mm:ss')" | Out-File $out
"== leak anchor #4 (must be byte-identical: AERO --mppi x60 = 44/60) ==" | Out-File $out -Append
L "AERO mppi x60" @("--headless","--scenario","aero_offset","--seed","42","--runs","60","--mppi")
"== heaving-deck landing rate — HOVERSLAM (tier-0, deck-blind), Hs=3 (canon rough ~+-1.5m) ==" | Out-File $out -Append
L "SEA hoverslam s42 Hs3" @("--headless","--scenario","terminal","--seed","42","--runs","60","--sea","3")
L "SEA hoverslam s7  Hs3" @("--headless","--scenario","terminal","--seed","7","--runs","60","--sea","3")
L "SEA hoverslam s99 Hs3" @("--headless","--scenario","terminal","--seed","99","--runs","60","--sea","3")
"== heaving-deck landing rate — HOVERSLAM, Hs=1.5 (moderate ~+-0.75m) ==" | Out-File $out -Append
L "SEA hoverslam s42 Hs1.5" @("--headless","--scenario","terminal","--seed","42","--runs","60","--sea","1.5")
L "SEA hoverslam s7  Hs1.5" @("--headless","--scenario","terminal","--seed","7","--runs","60","--sea","1.5")
L "SEA hoverslam s99 Hs1.5" @("--headless","--scenario","terminal","--seed","99","--runs","60","--sea","1.5")
"== heaving-deck landing rate — NEURAL (learned policy, also deck-blind), Hs=1.5 ==" | Out-File $out -Append
L "SEA neural s42 Hs1.5" @("--headless","--scenario","terminal","--seed","42","--runs","60","--neural","--sea","1.5")
L "SEA neural s7  Hs1.5" @("--headless","--scenario","terminal","--seed","7","--runs","60","--neural","--sea","1.5")
L "SEA neural s99 Hs1.5" @("--headless","--scenario","terminal","--seed","99","--runs","60","--neural","--sea","1.5")
"== calm-sea sanity: Hs=0.05 (floor) should ~= dry TERMINAL 194/200-class ==" | Out-File $out -Append
L "SEA hoverslam s42 Hs0.05" @("--headless","--scenario","terminal","--seed","42","--runs","60","--sea","0.05")
"== SEA determinism pair (run twice; LANDED must be identical) ==" | Out-File $out -Append
L "SEA det A" @("--headless","--scenario","terminal","--seed","42","--runs","60","--sea","1.5")
L "SEA det B" @("--headless","--scenario","terminal","--seed","42","--runs","60","--sea","1.5")
"D-035-SEA-DONE" | Out-File $out -Append
