# d041_dagger.ps1 — THE ORACLE-DAGGER LOOP (oracle-distill Phase 3, D-041).
#
# Each round: fly the CURRENT student with --shadow-rfly (the oracle re-solves theta at the states
# the student actually visits and labels them), append those rows to the corpus, retrain, freeze,
# and measure. Repeat.
#
# WHY THIS LOOP IS THE POINT. The pilot flight test measured imitation R^2 0.9966 on held-out RUNS
# and 0/12 landings (D-041 addendum 6). Behaviour cloning learns the teacher's map almost perfectly
# and still cannot fly it, because a law 99.7% right per tick drifts off the teacher's state
# distribution and is then evaluated where the corpus is silent. DAgger closes exactly that gap, and
# it is the one thing D-031/D-032 never had — their teachers were ~10% controllers, so relabelling
# the student's states with them added nothing. This teacher sweeps the compound 36/36.
#
# WHAT TO WATCH. Two numbers, printed every round:
#   * the LANDED rate on the held-out compound — the thing we are actually buying;
#   * the mean shadow gbest — the oracle's best achievable cost from the states the student reaches.
#     That second number IS the covariate shift. On the pilot it ROSE 37.8 -> 495.5 within a single
#     flight. Rounds that are working drive it DOWN; a round where the rate stalls but gbest keeps
#     falling is still progress, and a round where gbest stops moving means the student has stopped
#     visiting new states and more rounds will not help.
#
# LODESTAR saw 2/8 -> 4 -> 5 -> 8/8 across three rounds on the same structure. Expect monotone but
# not fast, and do NOT read a weak round 1 as a failure of the architecture.
#
# COST: one shadow flight ~= one RFLY flight (the CEM dominates), so a round of R runs costs about
# what farming R oracle runs costs. Budget accordingly; the loop is deadline-paced.
#
# USAGE (after both farms have landed, with the exe free):
#   pwsh -NoProfile -File runs\d041_dagger.ps1 -Rounds 3 -NpBase 7

param(
  [int]$Rounds        = 3,
  [int]$NpBase        = 7,        # round k exports NP_VERSION (NpBase + k - 1)
  [string[]]$BaseData = @("data\s0rf", "data\s0rf_clean"),
  [int]$SeedBase      = 7000,     # disjoint from gate seeds {42,7,99}, the farms (5000s/6000s)
  [int]$RunsPerSeed   = 8,
  [int]$SeedsPerRound = 3,
  [int]$Hidden        = 192,
  [int]$Epochs        = 400,
  [string]$DeadlineLocal = "",
  [string]$Exe        = "build\bin\Release\booster-core.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$out = "runs\d041_dagger.txt"
function L($m) { $line = "{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $m; Write-Output $line; Add-Content $out $line }

"D-041 ORACLE-DAGGER :: $(Get-Date)" | Out-File $out

$deadline = $null
if ($DeadlineLocal -ne "") {
  $deadline = [DateTime]::Parse($DeadlineLocal)
  if ($deadline -lt (Get-Date)) { $deadline = $deadline.AddDays(1) }
}

# The held-out compound eval: the SAME canonical draw D-040 was measured on, so the numbers are
# directly comparable to hoverslam 2/12, GM_CFLY 0/12 and the GM_RFLY oracle 12/12.
function Measure-Compound($tag) {
  $line = & ".\$Exe" --headless --scenario entry --seed 42 --runs 12 --neural `
      --engine-out random --gust 15@6000:1000 --target circle:15:40 2>&1 | Select-String "LANDED"
  L "RATE $tag :: compound s42 x12 -> $line"
}

$corpora = @($BaseData)

for ($k = 1; $k -le $Rounds; $k++) {
  if ($deadline -and (Get-Date) -ge $deadline) { L "DEADLINE reached before round $k — stopping cleanly"; break }
  $npv = $NpBase + $k - 1
  $dir = "data\s0rf_dag$k"
  New-Item -ItemType Directory -Force $dir | Out-Null
  L "=== ROUND $k / $Rounds  (NP_VERSION $npv, collecting into $dir) ==="

  # ---- 1. COLLECT. Fly the CURRENT student; the oracle labels the states it visits. The compound
  # recipe is varied per seed so the round covers the disturbance product, not one corner of it.
  $gb = @()
  for ($i = 0; $i -lt $SeedsPerRound; $i++) {
    $seed = $SeedBase + $k * 100 + $i
    if (@(42, 7, 99) -contains $seed) { L "SKIP held-out gate seed $seed"; continue }
    $peak = 12 + ($i % 3) * 6                     # 12,18,24 m/s
    $tr = 15 + ($i % 3) * 5                       # circle radius 15,20,25 m
    $bin = "$dir\dag_s$seed.bin"; $csv = "$dir\dag_s$seed.csv"; $err = "$dir\dag_s$seed.err"
    L "collect seed=$seed gust=$peak@6000:1000 target=circle:${tr}:40 runs=$RunsPerSeed"
    & ".\$Exe" --headless --scenario entry --seed $seed --runs $RunsPerSeed --neural --shadow-rfly `
        --engine-out random --gust "$peak@6000:1000" --target "circle:${tr}:40" --sea 1.5 --sea-wander 3 `
        --policy-log $bin --out $csv 2> $err | Select-String "LANDED" | ForEach-Object { L "  student rate seed=$seed $($_.Line)" }
    # the covariate-shift readout: what cost could the oracle still achieve from where the student went?
    $g = Select-String -Path $err -Pattern 'gbest=([0-9.]+)' -AllMatches |
         ForEach-Object { $_.Matches } | ForEach-Object { [double]$_.Groups[1].Value }
    if ($g) { $gb += $g; L "  shadow gbest seed=$seed : n=$($g.Count) mean=$([math]::Round(($g | Measure-Object -Average).Average,1)) max=$([math]::Round(($g | Measure-Object -Maximum).Maximum,1))" }
  }
  if ($gb.Count) { L "ROUND $k COVARIATE-SHIFT: mean shadow gbest = $([math]::Round(($gb | Measure-Object -Average).Average,1)) over $($gb.Count) replans" }
  $corpora += $dir

  # ---- 2. RETRAIN on every corpus so far (BC rows + all DAgger rounds). Verdict-filtered and
  # parked-tail-trimmed as always; the auto-gamut re-derives the output range from the merged data.
  $ckpt = "runs\s0rf_dag$k.pt"
  L "retrain on $($corpora.Count) corpus dir(s) -> $ckpt"
  python trainer\train_s0.py --data $corpora --verdict-csv $corpora --keep-verdicts 1,2 `
      --out $ckpt --hidden $Hidden --epochs $Epochs 2>&1 |
    Select-String "verdict filter|parked-tail|a_lat output|throttle mask|runs:|metrics" | ForEach-Object { L "  $_" }
  if ($LASTEXITCODE -ne 0) { L "ABORT: trainer failed in round $k"; exit 1 }

  # ---- 3. FREEZE + verify (the one shared ceremony), then measure.
  pwsh -NoProfile -File runs\d041_export_build.ps1 -Ckpt $ckpt -NpVersion $npv -ActionTier 2 -LogFile $out
  if ($LASTEXITCODE -ne 0) { L "ABORT: freeze ceremony failed in round $k"; exit 1 }
  Measure-Compound "round$k"
}

L "=== DAGGER LOOP DONE — the rate + gbest table above is the result, monotone or not ==="
L "Next: if the rate is climbing, run more rounds; if the rate stalls while gbest keeps falling,"
L "the student is still improving where it matters; if gbest has stopped moving, it has stopped"
L "visiting new states and more rounds will not help — go to capacity or the KILL criterion."
