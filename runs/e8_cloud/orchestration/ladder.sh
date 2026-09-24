#!/usr/bin/env bash
# the critic-free control ladder, R = 3 first (arm D's rollout count), then 2, 5, 9
cd /home/user/booster-lander-simulator
for R in 3 2 5 9; do runs/e8_fly.sh $SP/bin/booster-core.e11 $SP/null R$R --rfly-rollouts $R; done
echo "LADDER-DONE $(date '+%Y-%m-%d %H:%M:%S')" > $SP/null/ladder_done.txt
