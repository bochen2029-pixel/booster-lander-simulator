#!/bin/bash
cd "C:/Booster_Lander_CFLY"
OUT=runs/cfly_rate_s42.txt
echo "cfly compound rate-check seed 42 runs 0-11 :: $(date)" > $OUT
for r in 0 1 2 3 4 5 6 7 8 9 10 11; do
  R=$(./build/bin/Release/booster-core.exe --run --scenario entry --engine-out random --gust 15@6000:1000 --target circle:15:40 --seed 42 --run $r --cfly 2>/dev/null | grep RESULT)
  echo "run $r :: $R" >> $OUT
done
echo "DONE :: $(date)" >> $OUT
