# D-057 — THE BUDGET SWEEP, POP AND ITERS SEPARATELY (PLAN.md Phase 0.3)
#
# WHY: two points exist on the budget curve — full (D-052: 177/180, 144 PERFECT, lat 0.41 m,
# ~76 s/flight) and 1/8 (D-054: 175/180, 27 PERFECT, lat 2.4 m, 6.3 s/flight) — with a surprising
# result between them: the rate barely moves while precision collapses. But --rfly-budget scales
# POP and ITERS TOGETHER and ITERS floors at 2, so at "1/8" the big solve is 24x2 = 48 evals (1/40
# of full) and the small replans 8x2 = 16 (1/12). The curve was confounded. D-057 adds
# --rfly-pop-scale and --rfly-iters-scale (byte-clean at 1.0, gates in runs/d057_gates.txt) and
# sweeps each axis alone, plus two joint points to fill the existing curve.
#
# THE GRID (all blind + event replan, dev pool 42/7/99 x 60, the identical 180 faults per arm):
#
#   arm          flags                     big POPxITERS  small POPxITERS   evals/flight (~1 big + 13 small)
#   joint_0.25   --rfly-budget 0.25          48x2 =  96      12x2 =  24     ~408
#   pop_0.0625   --rfly-pop-scale 0.0625     12x10= 120       8x4 =  32     ~536
#   pop_0.125    --rfly-pop-scale 0.125      24x10= 240       8x4 =  32     ~656
#   joint_0.5    --rfly-budget 0.5           96x5 = 480      24x2 =  48     ~1104   } matched
#   pop_0.25     --rfly-pop-scale 0.25       48x10= 480      12x4 =  48     ~1104   } pair A
#   iters_0.2    --rfly-iters-scale 0.2     192x2 = 384      48x2 =  96     ~1632
#   iters_0.3    --rfly-iters-scale 0.3     192x3 = 576      48x2 =  96     ~1824
#   pop_0.5      --rfly-pop-scale 0.5        96x10= 960      24x4 =  96     ~2208   } matched
#   iters_0.5    --rfly-iters-scale 0.5     192x5 = 960      48x2 =  96     ~2208   } pair B
#   (anchors already on disk: joint_1.0 = D-052, joint_0.125 = D-054)
#
# PRE-REGISTERED, before the first flight:
#   P1  the landed RATE is flat across the grid (every arm >= 172/180): the rate is carried by the
#       warm start + event replan, not by search volume. Falsified by any arm < 170.
#   P2  PERFECT count rises with evaluations per flight, and at MATCHED evaluations (pairs A and B)
#       the ITERS-preserving arm (pop_*) has MORE PERFECT than the ITERS-cut arm: refinement of the
#       CEM mean is what buys the bullseye, coverage is not. Falsified if the iters_* / joint_* arm
#       wins PERFECT in both matched pairs.
#   P3  there is a real-time configuration strictly better than D-054's at similar cost: some arm at
#       <= ~1100 evals/flight lands >= 175 AND has > 27 PERFECT. Falsified if none does.
#   Wall-clock per flight is recorded but ADVISORY: the box is shared tonight (UI gates, a Phase 2.2
#   build in another directory). Evaluations per flight are the exact cost axis.
#
# FARM-SCRIPT LAW: verify every unit by its LANDED: line, never write DONE on silence, keep stderr,
# hold the box awake, be resumable per unit, and the monitor watches the AGE OF THE NEWEST OUTPUT.

$ErrorActionPreference = "Stop"
$exe = "C:/Booster_Lander_Simulator/build2/bin/Release/booster-core.exe"
$dir = "C:/Booster_Lander_Simulator/runs/d057"
$out = "C:/Booster_Lander_Simulator/runs/d057_budget_sweep.txt"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

Add-Type -Name Pwr -Namespace W57 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
$ES_CONTINUOUS = [uint32]2147483648
[void][W57.Pwr]::SetThreadExecutionState([uint32]($ES_CONTINUOUS -bor [uint32]1))

$arms = @(
  @{ name = 'joint_0.25'; args = @('--rfly-budget', '0.25') },
  @{ name = 'pop_0.0625'; args = @('--rfly-pop-scale', '0.0625') },
  @{ name = 'pop_0.125';  args = @('--rfly-pop-scale', '0.125') },
  @{ name = 'joint_0.5';  args = @('--rfly-budget', '0.5') },
  @{ name = 'pop_0.25';   args = @('--rfly-pop-scale', '0.25') },
  @{ name = 'iters_0.2';  args = @('--rfly-iters-scale', '0.2') },
  @{ name = 'iters_0.3';  args = @('--rfly-iters-scale', '0.3') },
  @{ name = 'pop_0.5';    args = @('--rfly-pop-scale', '0.5') },
  @{ name = 'iters_0.5';  args = @('--rfly-iters-scale', '0.5') }
)
$seeds = @(42, 7, 99)

if (-not (Test-Path $out)) {
  "D-057 BUDGET SWEEP — POP and ITERS separately, blind + event replan, dev pool 42/7/99 x60" | Out-File $out
  "started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  exe=$exe" | Out-File $out -Append
  "anchors: joint_1.0 = 177/180 144P lat 0.41 (D-052) | joint_0.125 = 175/180 27P lat 2.4 (D-054)" | Out-File $out -Append
  "pre-registered: P1 rate flat (>=172 every arm) | P2 pop_* beats iters_*/joint_* on PERFECT at matched evals | P3 an arm <=~1100 evals lands >=175 with >27 PERFECT" | Out-File $out -Append
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
        $m = (Select-String -Path $res -Pattern "landed means").Line.Trim()
        "[$($arm.name) s$s] (resumed) $line | $q | $m" | Out-File $out -Append
      }
      continue
    }
    $t0 = Get-Date
    & $exe --headless --scenario entry --seed $s --runs 60 --rfly --rfly-blind --rfly-event-replan `
           --engine-out random @($arm.args) 1> $res 2> $err
    $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
    if (-not (Select-String -Path $res -Pattern "LANDED:" -Quiet)) {
      "[$($arm.name) s$s] FAILED after $mins min — no LANDED line. NOT a zero result." | Out-File $out -Append
      if (Test-Path $err) { Get-Content $err -Tail 8 | ForEach-Object { "     $_" } | Out-File $out -Append }
      "D057-FAILED $($arm.name) seed $s" | Out-File $out -Append
      $ok = $false; break
    }
    $line = (Select-String -Path $res -Pattern "LANDED:").Line.Trim()
    $q = (Select-String -Path $res -Pattern "PERFECT").Line.Trim()
    $f = (Select-String -Path $res -Pattern "faults:").Line.Trim()
    $m = (Select-String -Path $res -Pattern "landed means").Line.Trim()
    $b = (Select-String -Path $err -Pattern "rfly_budget" | Select-Object -First 1)
    $bl = if ($b) { $b.Line.Trim() } else { "(budget line: none — joint arm)" }
    "[$($arm.name) s$s] $mins min ($([math]::Round($mins*60/60,1)) s/flight, advisory) $line | $q | $f | $m | $bl" | Out-File $out -Append
  }
  if (-not $ok) { break }
  # arm total, only when all three seeds verified
  $tot = 0; $P = 0; $n = 0
  foreach ($s in $seeds) {
    $res = "$dir/$($arm.name)_$s.txt"
    if (-not ((Test-Path $res) -and (Select-String -Path $res -Pattern "LANDED:" -Quiet))) { $n = -1; break }
    $tot += [int]((Select-String -Path $res -Pattern "LANDED:").Line -replace '.*LANDED: (\d+)/.*', '$1')
    $P += [int]((Select-String -Path $res -Pattern "PERFECT").Line -replace '.*PERFECT (\d+).*', '$1')
    $n += 60
  }
  if ($n -eq 180) { "==== $($arm.name) TOTAL: $tot/180 = $([math]::Round(100.0*$tot/180,1))%  PERFECT $P ====" | Out-File $out -Append; "" | Out-File $out -Append }
}
if ($ok) { "D057-DONE  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $out -Append }
[void][W57.Pwr]::SetThreadExecutionState($ES_CONTINUOUS)
