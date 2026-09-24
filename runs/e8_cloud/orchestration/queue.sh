#!/usr/bin/env bash
# post-chain queue, in priority order: the width decomposition, then R5/R9, then T16 (the teacher
# with csv receipts = the E6 cross-platform check). Two arms at a time (6 procs on 4 cores).
set -u
SP=$SP; E=$SP/bin/booster-core.e12; O=$SP/null; cd /home/user/booster-lander-simulator
until [ -f $SP/chain/chain_done.txt ] || grep -q "CHAIN-ABORT" $SP/chain/chain.log 2>/dev/null; do sleep 60; done
runs/e8_fly.sh $E $O rich_event     --rfly-budget 0.03125 --rfly-rollouts 3 --rfly-rollouts-at t0,periodic &
runs/e8_fly.sh $E $O rich_t0event   --rfly-budget 0.03125 --rfly-rollouts 3 --rfly-rollouts-at periodic &
wait
runs/e8_fly.sh $E $O rich_periodic  --rfly-budget 0.03125 --rfly-rollouts 3 --rfly-rollouts-at t0,event &
runs/e8_fly.sh $E $O R5 --rfly-rollouts 5 &
wait
runs/e8_fly.sh $E $O R9 --rfly-rollouts 9 &
runs/e8_fly.sh $E $O T16 --rfly-budget 0.03125 &
wait
echo "QUEUE-DONE $(date '+%Y-%m-%d %H:%M:%S')" > $O/queue_done.txt
