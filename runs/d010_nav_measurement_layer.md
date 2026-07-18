# D-010 — NAV measurement layer (§8.1 "estimation honesty")

Agent NAVBUILD · 2026-07-18 · isolated tree `_nav_wt\` (proven `_serve_wt` pattern) ·
integration-ready for the main session.

## What this delivers

The canon §8.1 measurement layer the external review called for: guidance no longer reads
plant truth directly. A `NavState` is produced once per 50 Hz guidance tick from the true
`State`; **all** guidance layers (hoverslam, MPPI, entry supervisor + `entry_divert_step`,
and the sim.c wind-rejection integral) consume it instead of raw truth.

- **`NAV_TRUTH` (default):** pass-through. **BIT-IDENTICAL** to the pre-nav code — the hard
  acceptance gate (proof below).
- **`NAV_NOISY`** (`MOD_NAV_NOISY` bit / `--nav-noisy`): guidance sees a noisy estimate —
  position σ=[0.5,0.5,0.3] m, velocity σ=0.1 m/s, attitude σ=0.1°, gyro-bias random-walk;
  `INJECT_DISTURBANCE(sensor_bias)` acts here on the measurement (§8.1 "it matters"). All
  draws from the Philox `RNG_NAV` stream keyed (seed, step, run) → deterministic/replayable.

## Design decisions

1. **`State nav_view` copy, not a new type.** The nav layer emits a full `State` whose
   `r/v/q/w` are perturbed and whose every other field is truth. Guidance code compiles
   **unchanged** against `const State*` — zero edits to `guidance_hoverslam.c` /
   `guidance_mppi.c` / `control.c`. Least-invasive plumbing, per the brief.

2. **What is perturbed vs pass-through** (per §8.1 intent — the vehicle's own bookkeeping is
   known exactly; only its pose/motion through the world is *estimated*):
   - **Perturbed** (flat indices S_RX..S_WZ = 0..12): world position, world velocity,
     attitude quaternion (small-angle body-frame rotation), body angular rate (= truth +
     integrated gyro-bias random walk).
   - **Pass-through = truth:** masses, throttle/gimbal/fin actuator states, slosh, heat,
     `engine_on/n_eng/ign_timer/ada/relights_left`, `deploy_frac/deploy_cmd`, crush, N2,
     `wind_filt`, `t/step/phase/verdict/fault/fins_deployed`.

3. **Plant state machine stays on truth.** `entry_supervisor` now takes `(Sim*, const State*
   nav)`: all its **guidance readouts** (peak-qbar predictor inputs, the ZEM/ZEV divert, the
   t_go) read `nav`; every **plant write** (phase, engine latch/cut, ada freeze) stays on
   `s->st`. The ignition latch and ada freeze read truth (real ignition mass + pressure).

4. **`nav_resync` — the one subtlety.** `entry_supervisor`'s CUT flips
   `engine_on/phase/ign_timer` on truth *mid-tick*, then returns 0 so hoverslam runs in the
   same tick. The nav snapshot taken at tick-top would be stale for those fields, so
   `nav_resync(st, &nav)` refreshes the pass-through fields (preserving the perturbed
   kinematics) before hoverslam/MPPI consume it. **This bug bit ENTRY specifically** (0/100
   before the fix — the landing burn ignited at 40 km off a stale engine_on); with the
   resync ENTRY is exactly 50/100 again. TERMINAL/AERO were unaffected (they never take the
   entry-burn CUT path), which is what localized it.

5. **Timing (§8.2):** nav view built once per 50 Hz guidance tick (`is_gtick`), not at the
   500 Hz physics rate.

## Truth-parity proof (the hard gate — all EXACT)

Baselines measured in the pristine copy first, then reproduced with the nav layer compiled in:

| gate | baseline | nav-build TRUTH | result |
|---|---|---|---|
| `--selftest` | PASS | PASS | ✅ |
| TERMINAL s42 x200 | 194/200 | 194/200 | ✅ exact |
| ENTRY s42 x100 | 50/100 | 50/100 | ✅ exact |
| AERO s42 x300 | 181/300 | 181/300 | ✅ exact |
| **CSV SHA-256** (all 3) | — | — | ✅ **byte-identical** |
| MPPI TERMINAL x40 | 40/40 (td_v 1.94, lat 10.67, fuel 4173) | identical | ✅ exact |

CSV hashes matched exactly: term `4B4C07B7…`, entry `28536D46…`, aero `50178779…`.
NAV_NOISY verified deterministic (same seed → byte-identical CSV across two runs).

## NAV_NOISY degradation (honest numbers, GM_HOVERSLAM, seed 42)

| scenario | nominal | **nav-noisy** | inject | **noisy+inject** |
|---|---|---|---|---|
| TERMINAL x200 | 194/200 (97.0%) | 193/200 (96.5%) | 196/200 (98.0%) | 196/200 (98.0%) |
| ENTRY x100 | 50/100 (50.0%) | 48/100 (48.0%) | 44/100 (44.0%) | 40/100 (40.0%) |
| AERO x300 | 181/300 (60.3%) | 171/300 (57.0%) | 104/300 (34.7%) | 100/300 (33.3%) |

**Reading it:** TERMINAL is nav-immune (short, low-energy final approach; ±1 run = noise).
ENTRY loses ~2 runs. AERO is the nav-sensitive scenario (−10 runs, −3.3 pp): the pos/vel
noise leaks into the cross-range divert and ignition timing — off-pad crashes 73→76,
fuel-out 2→12 — the physically-correct failure mode. Landed-run QUALITY is unchanged
(AERO td_v 3.40→3.43, lat 15.56→14.81, fuel 4425→4428): noise nudges marginal runs over the
edge rather than degrading nominal landings. Plant disturbances (`--inject`) dominate over
sensor noise, as expected. MPPI absorbs the noise better (TERMINAL x40 stays 40/40) — it
replans every tick on the fresh estimate.

## Integration checklist (for the main session)

Copy from `_nav_wt\` into the live tree:

1. **NEW `core/nav.h`** + **`core/nav.c`** — copy verbatim.
2. **`core/CMakeLists.txt`** — add `nav.c` to the `booster-core` source list (one line,
   after `sim.c`).
3. **`core/sim.h`** — `#include "nav.h"`; add `NavState nav;` to the `Sim` struct.
4. **`core/sim.c`** (the only substantive diff):
   - `sim_init`: `nav_init(&s->nav, modules, seed, run_idx);`
   - `entry_supervisor(Sim*)` → `entry_supervisor(Sim* s, const State* nav)`; its reads use
     `nav->…`, its writes stay on `s->st`; `entry_divert_step(nav, …)`.
   - `sim_step`: build `int is_gtick`; `State nav; if(is_gtick) nav_measure(&s->nav, st,
     st->step, &nav);` route the HOVERSLAM block (`entry_supervisor(s,&nav)`; `nav_resync`
     then `hoverslam_step(&nav,…)`), the MPPI block (`nav_resync`; `mppi_step/execute(…,
     &nav,…)`), and the wind-integral (`nav.y[S_RX/RY]`) through `nav`.
5. **`core/main.c`** — `--nav-noisy` → `modules|=MOD_NAV_NOISY` in `cmd_headless`
   (mirrors `--inject`); also wired into `--run`/`--serve` + usage string.
6. **`state.h` unchanged** — `MOD_NAV_NOISY=4` already existed. **`rng.h` unchanged** —
   `RNG_NAV=4` already existed.

No changes to `guidance_hoverslam.c`, `guidance_mppi.c`, `control.c`, `dynamics.c`.
After merge, re-run the truth-parity gate (selftest + the three exact rates) to confirm the
paste is clean, then add a `--nav-noisy` selftest/golden if desired.
