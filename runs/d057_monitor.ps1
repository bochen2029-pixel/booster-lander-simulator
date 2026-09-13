# D-057 monitor — checks the DATA, never a marker, and alarms on the AGE OF THE NEWEST OUTPUT.
# One-shot: prints a status line; exit 0 = progressing, 2 = done, 3 = STALLED/DEAD, 4 = FAILED.
$dir = "C:/Booster_Lander_Simulator/runs/d057"
$out = "C:/Booster_Lander_Simulator/runs/d057_budget_sweep.txt"
$stallMin = 25
$now = Get-Date
$units = @(Get-ChildItem "$dir/*.txt" -ErrorAction SilentlyContinue | Where-Object { Select-String -Path $_.FullName -Pattern "LANDED:" -Quiet })
$newest = Get-ChildItem "$dir/*" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$age = if ($newest) { [math]::Round(($now - $newest.LastWriteTime).TotalMinutes, 1) } else { -1 }
$core = @(Get-CimInstance Win32_Process -Filter "Name = 'booster-core.exe'" | Where-Object { $_.CommandLine -match "--rfly-blind --rfly-event-replan" })
$done = (Test-Path $out) -and (Select-String -Path $out -Pattern "^D057-DONE" -Quiet)
$failed = (Test-Path $out) -and (Select-String -Path $out -Pattern "^D057-FAILED" -Quiet)
$cur = if ($core.Count -gt 0) { ($core[0].CommandLine -replace '.*--seed (\d+).*--engine-out random (.*)$', 'seed $1 $2') } else { "no sweep core process" }
$line = "$($now.ToString('HH:mm:ss')) units verified $($units.Count)/27 | newest output $($newest.Name) age $age min | $cur"
if ($failed) { "FAILED  $line"; exit 4 }
if ($done) { "DONE    $line"; exit 2 }
if ($age -lt 0 -or $age -gt $stallMin -or $core.Count -eq 0) { "STALLED $line"; exit 3 }
"OK      $line"; exit 0
