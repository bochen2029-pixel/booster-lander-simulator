# D-062 — D-057's iters_0.2 row (180/180 dev, 104 PERFECT) flown ONCE on the sealed band 9300-9302.
#
# The unreconciled pair: E6's cold joint-1/32 (179/180 dev, 24 PERFECT, ~1 s/flight) and D-057's
# iters_0.2 (180/180 dev, 104 PERFECT, ~54 s/flight). Same rate, four times the precision, fifty
# times the compute. D-061 seals the first; this seals the second on THREE of the same ten seeds
# (2.7 h at 54 s/flight) so the two can finally be read side by side on identical faults.
# --rfly-iters-scale 0.2 => big POP 192 x ITERS 2, small POP 48 x ITERS 2 (D-057 header).
$ErrorActionPreference = "Stop"
$exe = "C:/Booster_Lander_Simulator/build4/bin/Release/booster-core.exe"
$dir = "C:/Booster_Lander_Simulator/runs/d062"
$out = "C:/Booster_Lander_Simulator/runs/d062_sealed_iters02.txt"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Add-Type -Name Pwr -Namespace W62 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
[void][W62.Pwr]::SetThreadExecutionState([uint32]([uint32]2147483648 -bor [uint32]1))
"D-062 SEALED 9300-9302, flown ONCE: D-057 iters_0.2 + blind + event replan" | Out-File $out
"started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append
"dev reference: iters_0.2 = 180/180, PERFECT 104 | D-061 seals the 1/32 row on the same seeds" | Out-File $out -Append
"" | Out-File $out -Append
$tot = 0; $ok = $true
foreach ($s in 9300, 9301, 9302) {
  $res = "$dir/iters02_$s.txt"; $err = "$dir/iters02_$s.err"
  if (-not ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet))) {
    $t0 = Get-Date
    & $exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind --rfly-event-replan `
           --rfly-iters-scale 0.2 --engine-out random 1> $res 2> $err
    $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
  } else { $mins = "resumed" }
  if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) { "seed $s FAILED — no LANDED line ($mins)" | Out-File $out -Append; "D062-FAILED $s" | Out-File $out -Append; $ok = $false; break }
  $line = (Select-String -Path $res -Pattern "LANDED:").Line.Trim(); $l = [int]($line -replace '.*LANDED: (\d+)/.*','$1'); $tot += $l
  "seed $s ($mins min): $line | $((Select-String -Path $res -Pattern 'PERFECT').Line.Trim())" | Out-File $out -Append
}
if ($ok) { "---- iters_0.2 TOTAL: $tot/180 ----" | Out-File $out -Append; "D062-DONE $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append }
[void][W62.Pwr]::SetThreadExecutionState([uint32]2147483648)
