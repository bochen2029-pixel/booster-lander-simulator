# D-010 TAIL-SWEEP — 16-config parameter sweep

Agent: TAIL-SWEEP (opus-4.8). Tree: `_sweep_wt` (isolated). Started 2026-07-18 15:0x.

## Knobs (2x2x2x2 = 16)
1. **KI_WIND** (sim.c wind-trim): {0.004, 0.012}
2. **WFADE** (near-ground fade of applied trim): {OFF, ON}
3. **IGN_MARGIN** (LANDING_IGNITE_MARGIN, guidance_hoverslam.c): {150.0, 190.0}
4. **KVEL** (Kvel fins-deployed value, guidance_hoverslam.c): {1.2, 0.9}

## Hard invariants per config
- `--selftest` => `SELFTEST: PASS`
- terminal s42 x200 => `LANDED: 194/200` EXACTLY (else TERMINAL leak => INVALID)

## Measurements
- ENTRY s42 x100, AERO_OFFSET s42 x300. Score = ENTRY% + AERO%.

## BASELINE (zero-knob 0.004/OFF/150/1.2) — verified in _sweep_wt
- selftest PASS; terminal 194/200
- ENTRY 50/100 = 50.0% | crash: off-pad 38 too-hard 10 fuel-out 2 other 0 | landed td_v=3.67 lat=15.27
- AERO 181/300 = 60.3% | crash: off-pad 73 too-hard 40 fuel-out 2 other 4 | landed td_v=3.40 lat=15.56
- **SCORE 110.3** (baseline to beat)

## RESULTS TABLE
| # | KI_WIND | WFADE | IGN | KVEL | selftest | term | ENTRY% | AERO% | SCORE | E:offpad/hard/fuel | A:offpad/hard/fuel | notes |
|---|---------|-------|-----|------|----------|------|--------|-------|-------|--------------------|--------------------|-------|
| B | 0.004 | OFF | 150 | 1.2 | PASS | 194 | 50.0 | 60.3 | 110.3 | 38/10/2 | 73/40/2 | baseline |
