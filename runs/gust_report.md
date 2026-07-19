# DIAL-A-GUST — deterministic discrete 1-cosine wind-shear injector

**Agent:** GUST (intercom `f0uij7rm`) · **Worktree:** `C:\Booster_Lander_Simulator\_gust_wt\` (integration-ready)
**Date:** 2026-07-18 night · **Canon:** §4.3 layer-3 discrete gust, §10.6 `INJECT_DISTURBANCE type=gust`

A robustness-test instrument (NOT a guidance change): throw a defined wind shear at the vehicle from
the CLI and watch the guidance re-solve. The gust is a **pure plant input** — it superposes on the
existing mean+Dryden wind and enters the dynamics through relative airspeed `v_rel = v − v_wind`
exactly like the existing wind. Guidance never reads it (canon §4.3 intact — see §3 below).

---

## 1. Flag spec

```
--gust <peak_mps>@<alt_m>[:<halfwidth_m>]     e.g.  --gust 12@3000:400
--gust-dir <deg>                              fixed horizontal bearing (0 => +x; optional)
```

- `peak_mps`  — pulse peak wind speed (m/s). **Absent or ≤0 => gust OFF => byte-identical to today.**
- `alt_m`     — band-center altitude (m), world Z (same frame as `S_RZ` and the mean profile).
- `halfwidth_m` — band half-width (m); optional, **default 300 m**. Full penetration distance `dm = 2·hw`.
- `--gust-dir` — bearing in degrees measured from +x toward +y. Default 0 (+x). Composes with the spec.

Available on both `--run` (single, with `--verbose` traces) and `--headless` (Monte-Carlo batches).
Both echo the armed band, e.g. `GUST: peak=12.0 m/s @ alt=3000 m  hw=400 m (band 2600..3400)  dir=0 deg`.

Usage examples:
```
booster-core --run --scenario terminal --seed 42 --run 1 --gust 12@1000:200 --verbose
booster-core --headless --scenario entry --seed 42 --runs 50 --mppi --gust 12@5000:800
booster-core --run --scenario aero_offset --seed 42 --run 1 --gust 20@2000:500 --gust-dir 90 --inject --nav-noisy
```

---

## 2. The 1-cosine math

As the vehicle penetrates the altitude band `[alt−hw, alt+hw]`, a horizontal wind pulse of magnitude

```
    w(h) = 0.5 · peak · ( 1 − cos( 2π·x / dm ) ),   x = h − (alt − hw),   dm = 2·hw
         = 0.5 · peak · ( 1 − cos( π·x / hw ) ),     x ∈ [0, 2·hw]
```

is added in the fixed direction `(cos dir, sin dir)`. This is the canonical MIL-F-8785C /
canon-§4.3 discrete 1-cosine gust `V = Vm/2·(1 − cos(πx/dm))` with penetration distance `dm = 2·hw`:

- `x = 0`   (band bottom edge, `h = alt−hw`): `w = 0`
- `x = hw`  (band center, `h = alt`):          `w = peak`   (crest)
- `x = 2·hw`(band top edge, `h = alt+hw`):      `w = 0`

`h` is the vehicle CoM world-Z (`st->y[S_RZ]`) — the same argument the mean/Dryden layers already
use. Pure function of altitude ⇒ **no RNG, fully replayable**.

---

## 3. Where it threads into the plant (file + line) — and why guidance can't cheat

**File:** `_gust_wt/core/sim.c`, function `wind_sample(Sim* s, double h, double out[3])`.
The gust block superposes on `out[]` **after** the mean profile and the Dryden turbulence
(`core/sim.c` ~line 60-76, immediately before the function closes). `wind_sample` is the single
sink that fills `s->env.wind_world` (called at `core/sim.c` ~line 348, once per physics step). The
plant integrates `v_rel = v − s->env.wind_world` in `dynamics.c`, so the gust reaches aero and fin
forces identically to the existing wind.

Config lives in a new `GustCfg` on the `Sim` struct (`_gust_wt/core/sim.h`), armed post-init by
`sim_set_gust(Sim*, peak, alt, hw, dir_deg)` (`core/sim.c`). `sim_init` memsets the Sim ⇒ `peak==0`
by default ⇒ the gust block is skipped entirely ⇒ byte-identical to the no-gust tree.

**Canon §4.3 (guidance never reads the wind) is structurally intact — verified in-code, not assumed:**

- The gust is written ONLY into `s->env.wind_world` (a plant field). No guidance/nav path reads it.
- The MPPI planner **zeroes the wind in its own rollouts**: `guidance_mppi.c` `rollout_cost()` does
  `EnvCtx env = *env0; env.wind_world[0..2] = 0.0;  /* nominal planner */`. So the controller plans
  against zero wind and re-solves purely against the **lateral state drift** the gust induces — it
  feels the shear only through state, exactly as §4.3 requires. The gust is invisible to guidance by
  construction; it cannot be "seen" and pre-compensated.
- The reactive law reads position/velocity error (the NAV estimate), never `wind_world`.

This is the honest wind-shear-**rejection** test: the vehicle must recover from a disturbance it
cannot observe directly.

---

## 4. Determinism proof

The gust is a pure function of altitude and a fixed direction — no RNG draw, and it is superposed
*after* the seeded Dryden step so it cannot perturb the turbulence stream.

| Check | Result |
|---|---|
| **(seed, gust) → bit-identical twice** (TERMINAL s42 x40, `--gust 12@1000:200`, CSV SHA256) | `8565E7BF…E02803` == `8565E7BF…E02803` ✅ |
| **gust OFF ≡ no-flag baseline** (TERMINAL s42 x40, `--gust 0@…` vs no flag, CSV SHA256) | identical ✅ (peak=0 disarms) |
| **`--selftest`** (with the flag wired in) | `SELFTEST: PASS` ✅ |
| **TERMINAL s42 x200, gust absent** — the sacred parity gate | **194/200 EXACTLY** ✅ (no leak) |
| **(seed, gust) under `--mppi`** (ENTRY s42 run3, `12@5000:800`) | RESULT bit-identical twice ✅ |

The flag is default-OFF and byte-transparent: the tree is unchanged unless `--gust` is passed with
a positive peak.

---

## 5. Demonstration — magnitude vs landing

All runs seed 42, full spec winds, gust superposed. Landed = PERFECT+GOOD+HARD.

### 5a. Single-run re-solve (TERMINAL s42 run 1 — lands GOOD today)

| Config | Verdict | td_v (m/s) | lat (m) | note |
|---|---|---|---|---|
| baseline (no gust)   | GOOD | 1.97 | 4.51 | clean landing |
| `--gust 12@1000:200` | GOOD | 1.98 | 4.58 | gust fires, guidance re-solves, still GOOD |

`--verbose` trace of `--gust 20@1200:400` (band 800..1600) shows the `wind` column climb from the
~6-7 m/s mean baseline up to **15-16 m/s** at band center (~1180 m) then decay back to ~6 m/s below
800 m — the 1-cosine crest — while `lat`/`vrad` deflect and then recover as the controller re-solves.

### 5b. TERMINAL magnitude sweep (s42 x50, gust `P@1000:200` and low flare-zone bands)

| peak (m/s) @ band | landed |
|---|---|
| 0 (baseline) | 96.0% (48/50) |
| 6  @1000:200 | 96.0% |
| 12 @1000:200 | 96.0% |
| 20 @1000:200 | 96.0% |
| 30 @300:150  | 96.0% |
| 50 @200:120  | 96.0% |
| 120 @120:80  | 96.0% |

TERMINAL is unmoved even by a 120 m/s gust: near the ground dynamic pressure is tiny (qbar ≈ 2-20 Pa
at h<3 m), so wind→aero force→lateral push is negligible regardless of wind speed; and at altitude
the fast (~180 m/s) descent gives brief exposure the controller easily re-solves. A gust bites where
**qbar is high** — see ENTRY below.

### 5c. ENTRY magnitude sweep — where the shear bites (s42, reactive)

ENTRY (62 km, ~1.5 km/s, highest qbar ~50 kPa, delicate multi-phase: entry burn → aero-descent
divert → landing burn). Gust placed during the aero-descent divert (nulling a 3 km offset):

| gust | landed (x20) | crash shift |
|---|---|---|
| none          | 90.0% (18/20) | — |
| **12@5000:800** | **70.0% (14/20)** | +too-hard | ← worst case: modest 12 m/s costs ~20 pts |
| 20@3000:600   | 75.0% (15/20) | +too-hard |
| 20@8000:1000  | 80.0% (16/20) | +too-hard/off-pad |
| 30@2000:500   | 85.0% (17/20) | +too-hard |

Confirmed at x25 (the batch): ENTRY reactive **baseline 92.0% (23/25) → 72.0% (18/25)** under
`12@5000:800` — a clean **−20 point** drop; crash mix shifts from off-pad toward too-hard
(td_v mean holds ~3.4-3.9 but more runs miss the landing-burn profile).

The honest robustness number: **a 12 m/s 1-cosine shear at 5 km costs ENTRY ~20 points of
landed-rate under the reactive controller** — the shear disrupts the committed divert enough that
the landing burn arrives off-profile. The MPPI trace below (§5d) rides the same crest — the `wind`
column climbs to ~37 m/s at band center while the vehicle is at qbar ~50 kPa nulling a 116 m
offset — and the closed-loop replanner banks 8-20° against the induced drift, closing lat 116→18 m
to a bit-deterministic HARD landing.

### 5d. Reactive vs `--mppi` — the closed-loop story (s42)

Landed-rate under the same gust, reactive law vs the MPPI replanner. (Fleet CPU contention on the
night of the run made the MPPI batches slow but they are bit-deterministic; see `runs/cmp_results.txt`.)

**AERO_OFFSET (x25), gust `20@2000:500` in the divert band:**

| config | reactive | `--mppi` |
|---|---|---|
| baseline           | 64.0% (16/25) | 72.0% (18/25) |
| `--gust 20@2000:500` | 68.0% (17/25) | (not completed — the MPPI batch was fleet-starved on the run
night to ~25% CPU; the load-bearing MPPI evidence is the deterministic single-run trace below) |

MPPI's baseline (72%) already leads reactive (64%). On AERO the 1-cosine shear is **near-neutral for
the reactive law** (64→68%) — the gust is placed at 2 km where the integral trim has ~10 s of descent
to absorb it before the landing burn commits. The full MPPI-under-gust landed-rate is a cheap re-run
for main to complete when the fleet is idle (`--headless --scenario aero_offset --seed 42 --runs 25
--mppi --gust 20@2000:500`); the mechanism is already demonstrated deterministically below.

**ENTRY MPPI — the single-run replan-through-shear trace** (the closed-loop story that matters most,
since ENTRY is where the shear bites reactive by −20 pts). `entry --mppi s42 run3 --gust 12@5000:800`,
band 4200..5800:

```
GUST: peak=12.0 m/s @ alt=5000 m  hw=800 m (band 4200..5800)  dir=0 deg
 t=95.0 h=6921 vz=-413 lat=116.5 qbar=51362 wind=34.5   (entering band, 116 m offset to null)
 t=99.5 h=5194 vz=-355 lat= 74.5 qbar=46193 wind=35.7   (near crest — replanner banks tilt 8→18°)
 t=100.0 h=5018 vz=-349 lat= 69.6 qbar=45532 wind=36.7   (crest ~37 m/s; lat closing hard)
 t=101.5 h=4508 vz=-332 lat= 59.0 qbar=43183 wind=28.6   (exiting band; drift absorbed)
 RESULT: HARD  td_v=1.86 m/s  lat=18.31 m  (lands — lat 116→18 m through the shear)
```

The `wind` column rides the 1-cosine crest to ~37 m/s at band center while the vehicle is at qbar
~50 kPa; the closed-loop replanner banks 8-20° against the induced drift and closes the 116 m offset
to an 18 m HARD landing — **bit-identical on a repeat run**. This is the guidance visibly re-solving
through a shear it cannot see.

---

## 6. What this proves about wind-shear rejection

The DIAL-A-GUST instrument confirms that the guidance solves the landing against a disturbance it
**cannot observe** — the gust enters only through `wind_world`, which no guidance/nav path reads and
which the MPPI planner explicitly zeroes in its rollouts (§3). The vehicle recovers purely from the
lateral state drift the shear induces. Three things fall out of the sweeps, and all three are the
*physics* talking, not tuning: (1) **where a shear bites is set by dynamic pressure, not wind speed**
— TERMINAL is unmoved even by a 120 m/s gust because near the ground qbar ≈ 2-20 Pa makes the aero
force negligible, while a modest 12 m/s gust at 5 km costs ENTRY ~20 points of landed-rate because it
lands on a qbar ~50 kPa committed divert; (2) **time-to-react is the second axis** — a shear placed
high (with descent left to re-solve) is absorbed, one placed on the final committed maneuver is not,
which is exactly why the ENTRY 5 km band is the worst case; and (3) **closed-loop replanning is the
right tool for shear rejection** — the ENTRY MPPI trace rides a 37 m/s crest and banks 8-20° to close
a 116 m offset to an 18 m landing, and MPPI's baseline AERO rate (72%) already leads the reactive law
(64%). The whole event is deterministic and replayable: `(seed, gust)` reproduces bit-for-bit, so any
shear that breaks a landing becomes a permanent regression fixture. Net: the controller is genuinely
solving the problem — throw a defined shear at it and it re-plans through it or fails gracefully
(crashes shift from off-pad toward too-hard, never a scripted-animation shatter), which is the honest
robustness bar canon §10.6 asks the `INJECT_DISTURBANCE` gust to prove.

---

## 7. Composition with `--inject` / `--nav-noisy`

All three disturbance instruments stack cleanly and independently:
- `--gust` is a **plant wind** superposition in `wind_sample` (deterministic, altitude-keyed).
- `--inject` (MOD_INJECT) perturbs thrust/Isp/CoM in `EnvCtx` (seeded per-run).
- `--nav-noisy` (MOD_NAV_NOISY) corrupts the guidance's *measurement* of state in the nav layer.

They touch disjoint parts of the pipeline (wind vs actuator/mass vs sensor), so
`--gust 20@2000:500 --gust-dir 90 --inject --nav-noisy` composes a directed shear + actuator faults
+ sensor noise in one replayable run — the combined-tail robustness matrix (roadmap item D).

---

## Integration notes (for main to fold in)

- Touched files: `core/sim.h` (GustCfg + field + `sim_set_gust` decl), `core/sim.c` (gust block in
  `wind_sample` + `sim_set_gust`), `core/main.c` (`parse_gust_flag` + wiring into `--run`/`--headless`
  + `wind` verbose column + usage string). No other files changed. No new deps.
- Zero impact when the flag is absent (memset default + peak==0 skip). CI selftest + TERMINAL-194
  unaffected; goldens untouched (the wire protocol already carries `wind_local` from `wind_world`,
  so a served gust run streams the pulse to the renderer with no protocol change).
- Optional future: expose the gust via the `--serve` command channel as a live `INJECT_DISTURBANCE`
  (§10.6) so the operator can throw it mid-descent in the renderer — one keystroke, as canon asks.
