#!/usr/bin/env bash
# E8 FLY — Linux port of runs/e8_fly.ps1, generalised to any arm: fly ONE arm ONCE on the held-out
# pool 42/7/99 x60 (ENTRY --engine-out random), blind + event replan, plus the arm's own flags.
#
#   runs/e8_fly.sh EXE OUTDIR ARM [extra flags ...]
#     e.g. runs/e8_fly.sh build/bin/booster-core out R3 --rfly-rollouts 3
#          runs/e8_fly.sh build/bin/booster-core out D  --rfly-budget 0.03125 --rfly-critic c.w \
#                         --rfly-critic-confirm 2 --rfly-critic-confirm-every
#
# The three seeds fly in parallel, one process each, OMP_NUM_THREADS=1 (rollouts are independent
# and write their own slots, so the thread count never changes a byte). Receipts: OUTDIR/ARM_s<seed>.
# {txt,err}; OUTDIR/ARM_summary.txt. Farm-script law: ARM-FLY-DONE is written only when all three
# seeds carry a LANDED line in their own stdout; a seed that already has one is never re-flown.
set -u
EXE=$1; OUT=$2; ARM=$3; shift 3
mkdir -p "$OUT"
sumf="$OUT/${ARM}_summary.txt"
stamp(){ date '+%Y-%m-%d %H:%M:%S'; }
echo "E8 FLY $ARM exe=$EXE flags=[$*] started $(stamp)" > "$sumf"
for s in 42 7 99; do
  (
    res="$OUT/${ARM}_s$s.txt"; err="$OUT/${ARM}_s$s.err"
    if [ -f "$res" ] && grep -q "LANDED:" "$res"; then exit 0; fi
    t0=$(date +%s)
    OMP_NUM_THREADS=1 "$EXE" --headless --scenario entry --seed "$s" --runs 60 --rfly --rfly-blind \
        --rfly-event-replan "$@" --engine-out random > "$res" 2> "$err"
    echo "$(( $(date +%s) - t0 ))" > "$OUT/${ARM}_s$s.secs"
  ) &
done
wait
tot=0; P=0; ok=1
for s in 42 7 99; do
  res="$OUT/${ARM}_s$s.txt"
  if ! grep -q "LANDED:" "$res" 2>/dev/null; then echo "  arm $ARM seed $s FAILED — no LANDED line" >> "$sumf"; ok=0; continue; fi
  line=$(grep "LANDED:" "$res" | head -1 | sed 's/^ *//'); pl=$(grep "PERFECT" "$res" | head -1 | sed 's/^ *//')
  tot=$(( tot + $(echo "$line" | sed -E 's/.*LANDED: ([0-9]+)\/.*/\1/') ))
  P=$(( P + $(echo "$pl" | sed -E 's/.*PERFECT ([0-9]+).*/\1/') ))
  echo "  arm $ARM seed $s : $line | $pl | $(cat "$OUT/${ARM}_s$s.secs" 2>/dev/null || echo '?') s wall" >> "$sumf"
done
if [ "$ok" -eq 1 ]; then
  echo "==== arm $ARM TOTAL: $tot/180  PERFECT $P ====" >> "$sumf"
  echo "$ARM-FLY-DONE $(stamp)" >> "$sumf"
fi
