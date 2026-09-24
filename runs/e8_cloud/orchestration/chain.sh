#!/usr/bin/env bash
# E8 cloud chain: wait for the farm, train critic_c0 (the v0c recipe: 6 seeds, 15 epochs, batch 32,
# hidden 256, pair_weight 4.0), score it offline, fly it once on 42/7/99 on four arms.
set -u
SP=$SP; D=$SP/e8data; C=$SP/chain; W=$C/critic_c0.w; EXE=$SP/bin/booster-core.e11
cd /home/user/booster-lander-simulator
log=$C/chain.log; stamp(){ date '+%Y-%m-%d %H:%M:%S'; }
echo "[$(stamp)] chain start: waiting for $D/farm_done.txt" >> $log
until [ -f $D/farm_done.txt ]; do sleep 60; done
while pgrep -f "booster-core.e8farm --headless" > /dev/null; do sleep 10; done   # never read a .cand while its writer is alive
rows=0; nf=0; for f in $D/s77*.cand; do rows=$(( rows + $(stat -c %s $f) / 568 )); nf=$((nf+1)); done
echo "[$(stamp)] farm complete: $nf seeds, $rows rows" >> $log
if [ $nf -lt 6 ] || [ $rows -lt 150000 ]; then echo "[$(stamp)] CHAIN-ABORT: too little data" >> $log; exit 1; fi
python3 runs/e8_cand_stats.py $D > $C/stats_teacher.txt 2>&1
python3 runs/e8_train_critic.py --data $D --out $W --epochs 15 --hidden 256 --batch 32 --pair_weight 4.0 > $C/train_c0.out 2> $C/train_c0.err
if [ ! -f $W ]; then echo "[$(stamp)] CHAIN-ABORT: no critic produced" >> $log; tail -5 $C/train_c0.err >> $log; exit 1; fi
echo "[$(stamp)] trained: $(grep EXPORTED $C/train_c0.out)" >> $log
python3 runs/e8_cand_stats.py $D --critic $W > $C/stats_critic.txt 2>&1
B="--rfly-budget 0.03125 --rfly-critic $W"
runs/e8_fly.sh $EXE $C De $B --rfly-critic-confirm 2 --rfly-critic-confirm-every --rfly-critic-confirm-elite &
runs/e8_fly.sh $EXE $C D  $B --rfly-critic-confirm 2 --rfly-critic-confirm-every &
wait
runs/e8_fly.sh $EXE $C B  $B &
runs/e8_fly.sh $EXE $C De1 $B --rfly-critic-confirm 1 --rfly-critic-confirm-every --rfly-critic-confirm-elite &
wait
ok=1; for a in De D B De1; do grep -q "$a-FLY-DONE" $C/${a}_summary.txt || ok=0; grep TOTAL $C/${a}_summary.txt >> $log; done
[ $ok -eq 1 ] && echo "E8-CLOUD-CHAIN-DONE $(stamp)" > $C/chain_done.txt && echo "[$(stamp)] CHAIN DONE" >> $log
