#!/usr/bin/env bash
# the control ladder, resumed: R2 as soon as R3 is done; R5 and R9 after the critic chain
set -u
SP=$SP; cd /home/user/booster-lander-simulator
until grep -q "R3-FLY-DONE" $SP/null/R3_summary.txt 2>/dev/null; do sleep 30; done
runs/e8_fly.sh $SP/bin/booster-core.e11 $SP/null R2 --rfly-rollouts 2
until [ -f $SP/chain/chain_done.txt ] || grep -q "CHAIN-ABORT" $SP/chain/chain.log 2>/dev/null; do sleep 60; done
for R in 5 9; do runs/e8_fly.sh $SP/bin/booster-core.e11 $SP/null R$R --rfly-rollouts $R; done
echo "LADDER-DONE $(date '+%Y-%m-%d %H:%M:%S')" > $SP/null/ladder_done.txt
