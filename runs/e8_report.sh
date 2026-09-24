#!/usr/bin/env bash
# E8 cloud report: every pre-registered read in runs/E8_FINDINGS.md, from the receipts.
#   runs/e8_report.sh NULLDIR CHAINDIR
# NULLDIR holds the critic-free arms (R2, R3b, R5, R9, T16, rich_*), CHAINDIR the critic arms
# (B, D, De, De1) and the trainer's output. Missing arms are skipped, never guessed.
N=$1; C=$2
here=$(dirname "$0")
tot(){ grep -h "TOTAL" "$1/$2_summary.txt" 2>/dev/null | sed 's/=//g; s/^ *//'; }
echo "== totals =="
for a in R2 R3b R5 R9 T16 rich_event rich_t0event rich_periodic; do [ -f "$N/${a}_summary.txt" ] && echo "  $(tot "$N" "$a")"; done
for a in B D De De1; do [ -f "$C/${a}_summary.txt" ] && echo "  $(tot "$C" "$a")"; done
echo; echo "== the trainer =="; grep -h "EXPORTED\|dead input" "$C/train_c0.out" 2>/dev/null
echo; echo "== offline, per replan =="; cat "$C/stats_critic.txt" 2>/dev/null
pair(){ [ -f "$1/${2}_s42.csv" ] && [ -f "$3/${4}_s42.csv" ] && { echo; echo "== $2 vs $4 =="; python3 "$here/e8_paired.py" "$1" "$2" "$3" "$4"; }; }
pair "$N" R3b "$C" De        # the critic's worth as a proposer at three rollouts
pair "$N" R2  "$C" De1       # ... at two
pair "$C" D   "$C" De        # the missing elite floor
pair "$N" R3b "$N" rich_event
pair "$N" R3b "$N" rich_t0event
pair "$N" R3b "$N" rich_periodic
pair "$N" R3b "$N" T16
exit 0
