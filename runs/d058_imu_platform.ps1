# D-058 — THE ATTITUDE REFERENCE CAN NOW BE LOST (PLAN.md Phase 2.2): what does a gimbaled
# platform cost the deployable controller, and where does the servo limit start to bite?
#
# THE PLANT CHANGE (core/imu.c, default OFF, gates in runs/d058_gates.txt): a three-gimbal inertial
# platform in the Apollo Block II order, the operator's asset's servo loop (2nd-order, accel- and
# RATE-limited, resolver chain with sec(MGA) on the outer axis), floats with +-3 deg stops. When the
# case rotates faster than the gimbals can follow, the stable member is dragged; at the float stops
# the reference is LOST and the platform's belief of the attitude is wrong by the accumulated drift
# for the rest of the flight. With --imu-platform on, the 500 Hz attitude controller AND the 50 Hz
# nav view fly the platform's belief, never truth.
#
# ARMS: the deployable config (blind + event replan + 1/8 budget = D-054, 175/180 on this pool,
# LOC 1) with the platform at three servo rate limits, dev pool 42/7/99 x 60, identical 180 faults:
#   imu180   --imu-platform        the asset default (180 deg/s)   gate flight: 0/2 lost, peak demand 107 deg/s
#   imu90    --imu-platform 90
#   imu45    --imu-platform 45     gate flight at 30 deg/s: 1/2 lost (53 deg platform error), crashed
#
# PRE-REGISTERED, before the first flight:
#   H1  at 180 deg/s the platform is transparent on this pool: <= 1 reference lost across 180 draws
#       and landed within 175 +- 2. Falsified by >= 3 losses or landed <= 171.
#   H2  the limit bites between 90 and 45: at 45 deg/s >= 5 references are lost and landed < 170 —
#       the attitude-reference axis of the ceiling becomes BINDING and the number goes DOWN, which is
#       the deliberate decision PLAN.md 2.2 names. Falsified if 45 deg/s loses < 3 or lands >= 173.
#   H3  where a reference is lost on a flight that then crashes, the loss PRECEDES the crash by
#       more than a replan interval (10 s) in the majority of cases — cause, not symptom. Read from
#       the [imu] journal lines (t of loss) against the flight's end time. Falsified if the majority
#       of losses happen within the last 10 s.
#   Wall clock is irrelevant here (the box also runs D-057).
#
# FARM-SCRIPT LAW: verify by the LANDED: line, DONE only when every unit verified, per-unit stderr,
# box awake, resumable; the monitor names its own job (--imu-platform) and watches output age.

$ErrorActionPreference = "Stop"
$exe = "C:/Booster_Lander_Simulator/build3/bin/Release/booster-core.exe"
$dir = "C:/Booster_Lander_Simulator/runs/d058"
$out = "C:/Booster_Lander_Simulator/runs/d058_imu_platform.txt"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

Add-Type -Name Pwr -Namespace W58 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][W58.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

$arms = @(
  @{ name = 'imu180'; args = @('--imu-platform') },
  @{ name = 'imu90';  args = @('--imu-platform', '90') },
  @{ name = 'imu45';  args = @('--imu-platform', '45') }
)
$seeds = @(42, 7, 99)

if (-not (Test-Path $out)) {
  "D-058 IMU PLATFORM — the attitude reference can be lost; deployable config, dev pool 42/7/99 x60" | Out-File $out
  "started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  exe=$exe" | Out-File $out -Append
  "reference (platform OFF, same 180 faults): D-054 = 175/180, 27 PERFECT, lat 2.4 m, LOC 1" | Out-File $out -Append
  "pre-registered: H1 180 deg/s transparent (<=1 lost, landed 175+-2) | H2 45 deg/s binds (>=5 lost, landed <170) | H3 loss precedes crash by >10 s in the majority" | Out-File $out -Append
  "" | Out-File $out -Append
}

$ok = $true
foreach ($arm in $arms) {
  foreach ($s in $seeds) {
    $res = "$dir/$($arm.name)_$s.txt"; $err = "$dir/$($arm.name)_$s.err"
    $done = (Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet)
    if ($done) {
      if (-not (Select-String -Path $out -Pattern "^\[$($arm.name) s$s\]" -Quiet)) {
        $line = (Select-String -Path $res -Pattern "LANDED:").Line.Trim()
        $q = (Select-String -Path $res -Pattern "PERFECT").Line.Trim()
        $im = (Select-String -Path $res -Pattern "imu:").Line.Trim()
        "[$($arm.name) s$s] (resumed) $line | $q | $im" | Out-File $out -Append
      }
      continue
    }
    $t0 = Get-Date
    & $exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind --rfly-event-replan `
           --rfly-budget 0.125 --engine-out random @($arm.args) 1> $res 2> $err
    $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
    if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
      "[$($arm.name) s$s] FAILED after $mins min — no LANDED line. NOT a zero result." | Out-File $out -Append
      if (Test-Path $err) { Get-Content $err -Tail 8 | ForEach-Object { "     $_" } | Out-File $out -Append }
      "D058-FAILED $($arm.name) seed $s" | Out-File $out -Append
      $ok = $false; break
    }
    $line = (Select-String -Path $res -Pattern "LANDED:").Line.Trim()
    $q = (Select-String -Path $res -Pattern "PERFECT").Line.Trim()
    $f = (Select-String -Path $res -Pattern "faults:").Line.Trim()
    $m = (Select-String -Path $res -Pattern "landed means").Line.Trim()
    $im = (Select-String -Path $res -Pattern "imu:").Line.Trim()
    $nl = @(Select-String -Path $err -Pattern "\[imu\] reference LOST").Count
    "[$($arm.name) s$s] $mins min | $line | $q | $f | $m | $im | journal: $nl loss lines" | Out-File $out -Append
  }
  if (-not $ok) { break }
  $tot = 0; $P = 0; $lost = 0; $n = 0
  foreach ($s in $seeds) {
    $res = "$dir/$($arm.name)_$s.txt"
    if (-not ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet))) { $n = -1; break }
    $tot += [int]((Select-String -Path $res -Pattern "LANDED:").Line -replace '.*LANDED: (\d+)/.*', '$1')
    $P += [int]((Select-String -Path $res -Pattern "PERFECT").Line -replace '.*PERFECT (\d+).*', '$1')
    $lost += [int]((Select-String -Path $res -Pattern "imu:").Line -replace '.*LOST in (\d+)/.*', '$1')
    $n += 60
  }
  if ($n -eq 180) { "==== $($arm.name) TOTAL: $tot/180 = $([math]::Round(100.0*$tot/180,1))%  PERFECT $P  references lost $lost ====" | Out-File $out -Append; "" | Out-File $out -Append }
}
if ($ok) { "D058-DONE  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append }
[void][W58.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
