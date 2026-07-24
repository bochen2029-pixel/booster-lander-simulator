#!/bin/bash
cd "C:/Booster_Lander_CFLY"
OUT=runs/rfly_rate_s42.txt
CMD="./build/bin/Release/booster-core.exe --run --scenario entry --engine-out random --gust 15@6000:1000 --target circle:15:40 --seed 42"
echo "D-040 pivot rate-check seed 42 :: $(date)" > $OUT
echo "=== HOVERSLAM baseline runs 0-11 ===" >> $OUT
for r in 0 1 2 3 4 5 6 7 8 9 10 11; do
  R=$($CMD --run $r 2>/dev/null | grep RESULT)
  echo "hs  run $r :: $R" >> $OUT
done
echo "=== GM_RFLY runs 0-11 ===" >> $OUT
for r in 0 1 2 3 4 5 6 7 8 9 10 11; do
  R=$($CMD --run $r --rfly 2>/dev/null | grep RESULT)
  echo "rfly run $r :: $R" >> $OUT
done
echo "=== DETERMINISM PAIR (rfly run 0 again; must match the first) ===" >> $OUT
R=$($CMD --run 0 --rfly 2>/dev/null | grep RESULT)
echo "rfly run 0 (pair) :: $R" >> $OUT
echo "RATE-CHECK-DONE :: $(date)" >> $OUT
