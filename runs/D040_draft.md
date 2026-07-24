# D-040 draft — cfly port Phase A VERDICT: the direct port is an honest NULL; pivot the optimizer to wrap the NATIVE stack

**2026-07-23 (evening).** Phase A executed per the SITREP §2.3 plan: reverted the port to the
sandbox-faithful law, gates green, ran the verdict. The answer is richer than either planned
branch — a decisive structural finding, not a tuning gap.

## What was done
Reverted 4 unfaithful edits in `guidance_cfly.c` (the SITREP named 3; review found a 4th):
1. max-Q cap 40 kPa → 58 kPa (sandbox value).
2. Min-throttle coast trap in the burn branch — removed.
3. Fuel-margin CEM cost term (`c -= 0.022*fuel_margin`) — removed.
4. h<60 lateral coast damping (latd 0.25) — removed (sandbox fin-steers full-authority).
Kept the legitimate adaptations (n_healthy engine count, main-tree fault enums, deck/target
sockets, final-state not-landed cost). Gates: selftest PASS · TERMINAL ×200 **byte-identical**.

## The verdict data (all seed 42)
| case | law | result | fuel left | t | maxq |
|---|---|---|---|---|---|
| compound run 0 | cfly faithful | CRASHED **FUEL** td_v 14.8 **lat 3.2** tilt 2.4° | 0 kg | 129.9 s | 45 kPa |
| compound runs 1-3 | cfly faithful | CRASHED FUEL ×3 (td_v 12-16, lat 0.8-10.6) | 0 kg | 133-149 s | 53-62 kPa |
| compound run 0 | hoverslam | CRASHED (OFF-PAD lat 35.1, td_v **2.4**, tilt 0.02°) | **3307 kg** | 116.8 s | 51 kPa |
| **CLEAN entry run 0** | **cfly faithful** | **CRASHED FUEL** td_v 14.2, lat 0.34 | **0 kg** | 142.3 s | 55 kPa |
| clean entry run 0 | hoverslam | HARD (td_v 2.7, lat 16.4) | **2111 kg** | 137.2 s | 54 kPa |

## The finding (three independent facts)
1. **The clean entry starves too** — this is NOT compound difficulty. The law structure itself
   (deep 3-engine entry burn → ballistic coast → 1-engine suicide) is fuel-infeasible on the
   main-tree plant, while the native hoverslam stack lands the same cases with 2-3.3 t margin.
2. **The CEM searches honestly and never finds feasibility**: t=0 big solve gbest = 11018
   (clean) / ~9019 (compound) — ABOVE the 8000 never-landed floor; elitism guarantees the
   sandbox's proven NOMINAL_TH was itself evaluated at t=0 — it does not land here. Replans
   grind to gbest ≈ 7300-7900 = land-hard-fuel-dead candidates. The optimizer keeps pushing
   VCUT 90→300-760 (cut the entry burn earlier to save fuel) and still starves.
3. **The two plants genuinely differ**: descent duration 99-106 s (sandbox) vs 130-149 s
   (main tree, ANY law) — ~40% more integrated drag/profile time. The sandbox 16/16 numbers
   do not transfer through θ; they certified the SEARCH ARCHITECTURE, not this law-on-this-plant.

## The pivot (recommended D-040 architecture)
Keep the AlphaZero thesis — search supplies the ±5% precision — but wrap it around the law
that is ALREADY fuel-feasible on this plant: **CEM-in-the-loop over the NATIVE reactive stack's
gains** (entry-divert KR/KV/bank re-auth of D-030, target-seek/damp gains, ignition margins),
per-scenario, warm-replanned every 10 s — exactly the machinery already built and working in
`cfly_replan` (deterministic sampler, candidate Sim-copy rollouts, cost arbitration; all
verified byte-clean default-off). The hoverslam compound failure mode is LATERAL (35 m off-pad,
soft, upright) — precisely the miss a per-scenario gain-search closes; and its EO recovery is
already 9-10/60 (D-030) with the same structural misses.

Effort estimate: bounded — a `double rtheta[K]` override block in Sim (default-off, byte-clean),
guidance reads overrides when armed, cfly_replan re-targeted at rtheta. The CEM/candidate/cost
plumbing is DONE and proven byte-clean; only the θ→law binding changes.

## THE PIVOT — BUILT AND FIRST-BLOOD (same evening)
**GM_RFLY** implemented per the design above: 10-D multiplier θ over the native stack's gains
(guidance_rfly.{c,h}; θ rides GuidanceCmd.rt — OpenMP-safe for candidates; identity ×1.0 is
IEEE-exact ⇒ byte-clean by construction). Sites: entry-divert KR/KV/bank (45° ceiling), A_DECEL,
T_LEAD, Kvel schedule, KVEL_NEAR, LANDING_IGNITE_MARGIN (both uses), Kv, + the RT_TGTLEAD
target-velocity lead ON THE SEEK (the D-038 redemption — identity 0). GM_RFLY block in sim.c =
the exact GM_HOVERSLAM pipeline (entry_supervisor + hoverslam_step + D-010 wind trim) + the
big-t=0/warm-10s CEM cadence. `--rfly` in run/headless/serve.

**Gates:** selftest PASS · TERMINAL ×200 byte-identical · hoverslam compound leak check
byte-exact (td_v 2.39 / lat 35.08 / fuel 3307 reproduced) — every gain site identity-clean.

**Run 0 (the canonical compound draw):**
```
t=0 big solve: gbest=324.7  (identity-hoverslam in-population at ~5583 ⇒ 17× improvement found)
warm replans:  324.7 → 279 → 232 → 174 → 78.7 → 77.3 → 62.8  (θ adapting through the flight)
RESULT: PERFECT  fault=none  td_v=1.72  lat=0.38  tilt=0.03°  fuel=3296 kg  t=113.4 s  maxq=50 kPa
```
vs hoverslam same draw: CRASHED off-pad lat 35.08 · vs GM_CFLY: CRASHED FUEL 0 kg ×4 · vs neural: 0%.
An engine-out at t=4, 15 m/s gust band, drifting deck — landed 38 cm from center with 3.3 t margin.
Notable θ story: gentler divert profile (ADEC 0.40 floor!), ~2× reversal lead, halved divert gain
(the D-012 lore), LATER ignition (IGN 0.40-0.50), TGT lead live at 0.2-0.5.

**In flight:** the 12+12 rate check (hoverslam baseline + rfly, identical draws) + determinism
pair → `runs/rfly_rate_s42.txt`. GO bar: rfly landed% ≫ hoverslam's, det-pair identical.

**Baseline (hoverslam ×12, identical compound draws): 2/12 survivable — 1 GOOD (r11, 8.7 m),
1 HARD (r7, 12.3 m), 3 LOC tumbles (r2/5/9), 1 FUEL starve (r3), 7 off-pad crashes (35-401 m).**

**N3 integration caveat (known, not blocking the ADR):** the warm replans cost seconds of CPU;
`--serve` paces real-time, so a live replan will hiccup the stream every 10 s. Interim: a smaller
live POP (e.g. 24×2) or async replan thread; the real fix is KESTREL's parked GPU CEM. Measure
during the showcase wiring.

## Preserved artifacts
`runs/cfly_rate_s42_faithful.txt` (0/4) · `runs/cfly_diag2.txt` (clean + compound full replan
traces) · the faithful-law `guidance_cfly.c` (uncommitted, worktree). The GM_CFLY plumbing
(flag, CflyState, CMake) is reusable as-is for the pivot.
