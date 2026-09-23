# E8 FLY — fly any exported critic ONCE on held-out 42/7/99 x60, arms B and C. Reusable.
#   B  blind + event replan + 1/32 budget, critic-only        (the critic's own quality)
#   C  same + --rfly-critic-confirm 2                          (the plant confirms the top 2 at events)
# Writes D:\bl_e1_data\e8\fly_<Tag>\{arm}_s<seed>.{txt,err} and summary.txt; FLY-DONE only after
# every seed has a LANDED line (farm-script law: never a marker on silence).
param(
  [Parameter(Mandatory=$true)][string]$Critic,
  [Parameter(Mandatory=$true)][string]$Tag,
  [string]$Exe = "C:\bl_e1\build_e9\bin\Release\booster-core.exe",
  [string[]]$Arms = @('B','C')
)
$ErrorActionPreference = "Continue"
$D = "D:\bl_e1_data\e8\fly_$Tag"; New-Item -ItemType Directory -Force $D | Out-Null
$S = Join-Path $D "summary.txt"
Add-Type -Name Pwr -Namespace WE8F -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
[void][WE8F.Pwr]::SetThreadExecutionState([uint32]([uint32]2147483648 -bor [uint32]1))
if (-not (Test-Path $Critic)) { "FLY-ABORT: no critic at $Critic" | Out-File $S; exit 1 }
"E8 FLY $Tag  critic=$Critic  started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $S
$def = @{ B = @('--rfly-budget','0.03125'); C = @('--rfly-budget','0.03125','--rfly-critic-confirm','2') }
$okAll = $true
foreach ($a in $Arms) {
  $tot = 0; $P = 0; $ok = $true
  foreach ($s in 42, 7, 99) {
    $res = Join-Path $D "${a}_s$s.txt"; $err = Join-Path $D "${a}_s$s.err"
    & $Exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind --rfly-event-replan --rfly-critic $Critic @($def[$a]) --engine-out random 1> $res 2> $err
    if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) { "  arm $a seed $s FAILED — no LANDED line" | Out-File $S -Append; $ok = $false; $okAll = $false; break }
    $line = (Select-String -Path $res -Pattern "LANDED:").Line.Trim(); $tot += [int]($line -replace '.*LANDED: (\d+)/.*','$1')
    $pl = (Select-String -Path $res -Pattern "PERFECT").Line.Trim(); $P += [int]($pl -replace '.*PERFECT (\d+).*','$1')
    "  arm $a seed $s : $line | $pl" | Out-File $S -Append
  }
  if ($ok) { "==== arm $a TOTAL: $tot/180  PERFECT $P ====" | Out-File $S -Append }
}
if ($okAll) { "FLY-DONE $Tag $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $S -Append }
[void][WE8F.Pwr]::SetThreadExecutionState([uint32]2147483648)
