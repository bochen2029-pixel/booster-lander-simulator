#!/bin/bash
cd "C:/Booster_Lander_CFLY"
OUT=runs/rfly_rate_s7_s99.txt
echo "D-040 held-out rate-check s7+s99 :: $(date)" > $OUT
for SEED in 7 99; do
  echo "=== GM_RFLY seed $SEED runs 0-11 ===" >> $OUT
  for r in 0 1 2 3 4 5 6 7 8 9 10 11; do
    R=$(./build/bin/Release/booster-core.exe --run --scenario entry --engine-out random --gust 15@6000:1000 --target circle:15:40 --seed $SEED --run $r --rfly 2>/dev/null | grep RESULT)
    echo "rfly s$SEED run $r :: $R" >> $OUT
  done
done
echo "HELDOUT-DONE :: $(date)" >> $OUT
