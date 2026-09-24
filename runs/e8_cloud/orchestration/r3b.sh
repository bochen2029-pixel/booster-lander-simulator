#!/usr/bin/env bash
# R3 re-flown WITH per-draw csv receipts (the first R3 predates --out in e8_fly.sh). Doubles as a
# determinism receipt: its stdout summaries must be byte-identical to R3's.
SP=$SP; cd /home/user/booster-lander-simulator
until grep -q "R3-FLY-DONE" $SP/null/R3_summary.txt 2>/dev/null; do sleep 30; done
runs/e8_fly.sh $SP/bin/booster-core.e11 $SP/null R3b --rfly-rollouts 3
for s in 42 7 99; do cmp -s $SP/null/R3_s$s.txt $SP/null/R3b_s$s.txt && echo "s$s R3b stdout BYTE-IDENTICAL to R3" || echo "s$s R3b DIFFERS from R3"; done > $SP/null/R3b_determinism.txt
