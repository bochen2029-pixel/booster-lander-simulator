# E8 CHAIN — waits for all three farm processes, trains the critic, flies it ONCE on 42/7/99.
#
# Runs unattended. Every step verifies its own output before the next (farm-script law); the
# chain never writes its DONE marker on silence. Pre-registered reads for the flight, from
# ROADMAP_NN-FLIGHT_2026-09-15.md section 1.3 (unchanged here):
#     >= 160/180  the judgment transfers; the network flies the battery at reflex speed  -> phase 2
#     100..160    right where the search visited, wrong where its own search goes      -> phase 1.5
#     <  100      the one-shot cost critic cannot capture the rollout                    -> section 3
# Stated expectation before the number: 130-170, because the critic has never been trained at
# the states its own search reaches.
$ErrorActionPreference = "Continue"
$D   = "D:\bl_e1_data\e8"
$Exe = "C:\bl_e1\build_e8\bin\Release\booster-core.exe"
$log = Join-Path $D "chain.log"
function Log($m){ ("[" + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + "] " + $m) | Out-File $log -Append }

Add-Type -Name Pwr -Namespace WE8C -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
[void][WE8C.Pwr]::SetThreadExecutionState([uint32]([uint32]2147483648 -bor [uint32]1))

Log "chain start: waiting for done_7700/7708/7716"
$deadline = (Get-Date).AddHours(8)
while ($true) {
  $done = @(7700, 7708, 7716) | Where-Object { Test-Path (Join-Path $D "done_$_.txt") }
  if ($done.Count -eq 3) { break }
  if ((Get-Date) -gt $deadline) { Log "CHAIN-ABORT: farm not done in 8 h ($($done.Count)/3)"; exit 1 }
  Start-Sleep -Seconds 120
}
# never read a .cand while a writer is alive
while ((Get-CimInstance Win32_Process -Filter "Name='booster-core.exe'" | Where-Object { $_.CommandLine -match 'cand-design' }).Count -gt 0) { Start-Sleep -Seconds 10 }
$cands = Get-ChildItem (Join-Path $D "s77*.cand")
$rows = ($cands | ForEach-Object { [math]::Floor($_.Length / (71*8)) } | Measure-Object -Sum).Sum
Log "farm complete: $($cands.Count) seeds, $rows rows"
if ($cands.Count -lt 20 -or $rows -lt 200000) { Log "CHAIN-ABORT: too little data"; exit 1 }

Log "training critic_v1 (ranking loss, full observation)"
& python "C:\bl_e1\runs\e8_train_critic.py" --data $D --out (Join-Path $D "critic_v1.w") --epochs 30 --hidden 256 1> (Join-Path $D "train_v1.out") 2> (Join-Path $D "train_v1.err")
if (-not (Test-Path (Join-Path $D "critic_v1.w"))) { Log "CHAIN-ABORT: trainer produced no critic_v1.w"; Get-Content (Join-Path $D "train_v1.err") -Tail 8 | ForEach-Object { Log "   $_" }; exit 1 }
$last = Get-Content (Join-Path $D "train_v1.out") | Select-String "EXPORTED" | Select-Object -Last 1
Log ("trained: " + $last.Line)

Log "FLIGHT: critic-scored search, blind, event replan, budget 1.0, held-out 42/7/99 x60 -- flown ONCE"
$tot = 0; $ok = $true
foreach ($s in 42, 7, 99) {
  $res = Join-Path $D "critic_v1_s$s.txt"; $err = Join-Path $D "critic_v1_s$s.err"
  & $Exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind --rfly-event-replan `
         --rfly-critic (Join-Path $D "critic_v1.w") --engine-out random 1> $res 2> $err
  if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) { Log "FLIGHT FAILED seed $s -- no LANDED line"; $ok = $false; break }
  $line = (Select-String -Path $res -Pattern "LANDED:").Line.Trim(); $l = [int]($line -replace '.*LANDED: (\d+)/.*','$1'); $tot += $l
  Log ("  seed $s : $line | " + (Select-String -Path $res -Pattern "PERFECT").Line.Trim())
}
if ($ok) {
  $read = if ($tot -ge 160) { "TRANSFERS -> phase 2" } elseif ($tot -ge 100) { "partial -> phase 1.5 (expert iteration on the critic)" } else { "does not capture the rollout -> section 3 (terminal-state head)" }
  Log "E8 CRITIC v1 HELD-OUT: $tot/180  ($read)"
  "E8-CHAIN-DONE $tot/180 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File (Join-Path $D "chain_done.txt")
}
[void][WE8C.Pwr]::SetThreadExecutionState([uint32]2147483648)
