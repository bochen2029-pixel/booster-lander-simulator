# kprobe_launcher.ps1 - wait for the exe lock (K=256 validation batch) to release, then
# run the self-driving sweep for K=512,1024. Fully autonomous; survives the session.
$exe = "C:\Booster_Lander_Simulator\_k1024_wt\build\bin\Release\booster-core.exe"
$log = "C:\Booster_Lander_Simulator\runs\kprobe_sweep.log"

function CanWrite($path){
    # exe is LOCKED while a batch runs; try to open for write to detect release.
    try { $fs=[System.IO.File]::Open($path,'Open','Write','None'); $fs.Close(); return $true }
    catch { return $false }
}

"$(Get-Date -Format HH:mm:ss)  LAUNCHER: waiting for exe lock to release (K=256 validation batch)" | Tee-Object -FilePath $log -Append | Out-Host
$waited=0
while(-not (CanWrite $exe)){
    Start-Sleep -Seconds 15
    $waited += 15
    if($waited % 120 -eq 0){ "$(Get-Date -Format HH:mm:ss)  LAUNCHER: still locked after $($waited)s" | Tee-Object -FilePath $log -Append | Out-Host }
    if($waited -gt 3600){ "$(Get-Date -Format HH:mm:ss)  LAUNCHER: timed out waiting 60 min, aborting" | Tee-Object -FilePath $log -Append | Out-Host; exit 1 }
}
"$(Get-Date -Format HH:mm:ss)  LAUNCHER: exe unlocked after $($waited)s, starting sweep K=512,1024" | Tee-Object -FilePath $log -Append | Out-Host

& powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Booster_Lander_Simulator\runs\kprobe_sweep.ps1" -Klist 512,1024
