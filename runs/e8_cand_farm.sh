#!/usr/bin/env bash
# E8 — THE CANDIDATE FARM, Linux port of runs/e8_cand_farm.ps1: same flags, same 71-column rows,
# same laws. One process per seed with OMP_NUM_THREADS=1 — the rollouts are independent and
# write their own slots, so the thread count never changes a byte, and N seeds fill N cores
# without the idle tail an 8- or 41-wide parallel-for leaves inside every replan.
#
#   runs/e8_cand_farm.sh EXE OUTDIR SEED [SEED ...]
#
# Held-out law: 42/7/99 and the sealed bands 92xx/93xx are refused. Farm-script law: a seed is
# done only when its own stdout carries a LANDED line; FARM-DONE is written only when EVERY seed
# has one; stderr is kept; a finished seed is never re-flown (resume = rerun the same command).
set -u
EXE=$1; OUT=$2; shift 2
mkdir -p "$OUT"
log="$OUT/farm.log"
stamp(){ date '+%Y-%m-%d %H:%M:%S'; }
echo "[$(stamp)] E8 farm start exe=$EXE seeds=$*" >> "$log"
pids=()
for seed in "$@"; do
  case " 42 7 99 " in *" $seed "*) echo "[$(stamp)] FARM-SKIP seed=$seed held-out" >> "$log"; continue;; esac
  if [ "$seed" -ge 9200 ] && [ "$seed" -le 9399 ]; then echo "[$(stamp)] FARM-SKIP seed=$seed sealed band" >> "$log"; continue; fi
  (
    res="$OUT/s$seed.txt"; err="$OUT/s$seed.err"; cand="$OUT/s$seed.cand"; bin="$OUT/s$seed.bin"
    if [ -f "$res" ] && grep -q "LANDED:" "$res"; then echo "[$(stamp)] RESUMED seed=$seed" >> "$log"; exit 0; fi
    rm -f "$cand"
    t0=$(date +%s)
    OMP_NUM_THREADS=1 "$EXE" --headless --scenario entry --seed "$seed" --runs 60 --rfly --rfly-blind \
        --rfly-event-replan --rfly-budget 0.03125 --rfly-cand-log "$cand" --rfly-cand-design \
        --policy-log "$bin" --engine-out random > "$res" 2> "$err"
    rc=$?; mins=$(( ($(date +%s) - t0) / 60 ))
    if ! grep -q "LANDED:" "$res"; then echo "[$(stamp)] FAILED seed=$seed rc=$rc after ${mins} min — no LANDED line" >> "$log"; exit 1; fi
    rows=$(( $(stat -c %s "$cand") / (71*8) ))
    echo "[$(stamp)] DONE seed=$seed $(grep 'LANDED:' "$res" | head -1 | sed 's/^ *//') | rows=$rows | ${mins} min" >> "$log"
  ) &
  pids+=($!)
done
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
for seed in "$@"; do
  case " 42 7 99 " in *" $seed "*) continue;; esac
  grep -q "LANDED:" "$OUT/s$seed.txt" 2>/dev/null || fail=1
done
if [ "$fail" -eq 0 ]; then echo "E8-FARM-DONE $* $(stamp)" > "$OUT/farm_done.txt"; echo "[$(stamp)] E8 farm END (all seeds landed a LANDED line)" >> "$log"
else echo "[$(stamp)] E8 farm END WITH FAILURES — no FARM-DONE" >> "$log"; exit 1; fi
