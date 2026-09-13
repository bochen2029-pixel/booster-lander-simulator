# D-058 monitor — checks the DATA, names its own job (--imu-platform), alarms on output age.
# exit 0 = progressing, 2 = done, 3 = STALLED/DEAD, 4 = FAILED.
$dir = "C:/Booster_Lander_Simulator/runs/d058"
$out = "C:/Booster_Lander_Simulator/runs/d058_imu_platform.txt"
$stallMin = 90   # a unit's stderr is written by pwsh only at unit end; CPU progress is the live signal
$now = Get-Date
$units = @(Get-ChildItem "$dir/*.txt" -ErrorAction SilentlyContinue | Where-Object { Select-String -Path $_.FullName -Pattern "LANDED:" -Quiet })
$newest = Get-ChildItem "$dir/*" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$age = if ($newest) { [math]::Round(($now - $newest.LastWriteTime).TotalMinutes, 1) } else { -1 }
$core = @(Get-CimInstance Win32_Process -Filter "Name = 'booster-core.exe'" | Where-Object { $_.CommandLine -match "--imu-platform" })
$cpu0 = ($core | Measure-Object -Property UserModeTime -Sum).Sum
Start-Sleep -Seconds 10
$core2 = @(Get-CimInstance Win32_Process -Filter "Name = 'booster-core.exe'" | Where-Object { $_.ProcessId -in $core.ProcessId })
$cpu1 = ($core2 | Measure-Object -Property UserModeTime -Sum).Sum
$cpuGain = [math]::Round(($cpu1 - $cpu0) / 1e7, 1)   # seconds of user CPU in the 10 s sample
$done = (Test-Path $out) -and (Select-String -Path $out -Pattern "^D058-DONE" -Quiet)
$failed = (Test-Path $out) -and (Select-String -Path $out -Pattern "^D058-FAILED" -Quiet)
$cur = if ($core.Count -gt 0) { ($core[0].CommandLine -replace '.*--seed (\d+).*--engine-out random (.*)$', 'seed $1 $2') } else { "no imu core process" }
$line = "$($now.ToString('HH:mm:ss')) cpu +$cpuGain s/10 s | units verified $($units.Count)/9 | newest output $($newest.Name) age $age min | $cur"
if ($failed) { "FAILED  $line"; exit 4 }
if ($done) { "DONE    $line"; exit 2 }
if ($core.Count -eq 0 -or $cpuGain -lt 1.0 -or $age -gt $stallMin) { "STALLED $line"; exit 3 }
"OK      $line"; exit 0
