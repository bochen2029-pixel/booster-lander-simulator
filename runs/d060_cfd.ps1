# D-060 — FluidX3D LBM/LES of the Kestrel-9 (legs stowed, base-first) vs the frozen aero table.
# GPU job (RTX 4070 Ti SUPER); independent of the CPU farms. Two points: alpha 0 (CA) and 8 deg (CN).
# Verify by the RESULT line in each log; DONE only when both verified.
$ErrorActionPreference = "Continue"
$exe = "C:/Booster_Lander_Simulator/assets/fluidx3d/FluidX3D-master/bin/FluidX3D.exe"
$out = "C:/Booster_Lander_Simulator/runs/d060"
New-Item -ItemType Directory -Force -Path "$out/frames" | Out-Null
$env:K9_VRAM_MB = "4000"
$env:K9_FLOWTHROUGHS = "5"
$env:K9_OUT = "$out/"
$env:K9_STL = "C:/Booster_Lander_Simulator/assets/kestrel9_gfx/exports/kestrel9_cfd_stowed_1-1_m.stl"
$ok = $true
foreach ($a in @("0", "8")) {
  $log = "$out/cfd_alpha$a.log"
  if ((Test-Path $log) -and (Select-String -Path $log -Pattern "K9 CFD RESULT" -Quiet)) { continue }
  $env:K9_ALPHA_DEG = $a
  $t0 = Get-Date
  Set-Location (Split-Path $exe)
  & $exe 1> $log 2>&1
  $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
  if (-not (Select-String -Path $log -Pattern "K9 CFD RESULT" -Quiet)) { "D060-FAILED alpha $a after $mins min" | Out-File "$out/status.txt" -Append; $ok = $false; break }
  $res = (Select-String -Path $log -Pattern "K9 CFD RESULT").Line
  "alpha $a : $mins min : $res" | Out-File "$out/status.txt" -Append
}
if ($ok) { "D060-DONE $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File "$out/status.txt" -Append }
