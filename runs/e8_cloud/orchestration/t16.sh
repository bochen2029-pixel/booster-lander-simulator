#!/usr/bin/env bash
# LAST in the queue: the 1/32 teacher (two generations of 8 = the E6 cold arm) on 42/7/99 WITH per-draw
# csv -- the top of the control curve on this platform, and the cross-platform receipt for E6's
# 179/180 (60/59/60, 24 PERFECT on Windows).
SP=$SP; cd /home/user/booster-lander-simulator
until [ -f $SP/null/ladder_done.txt ]; do sleep 60; done
runs/e8_fly.sh $SP/bin/booster-core.e11 $SP/null T16 --rfly-budget 0.03125
