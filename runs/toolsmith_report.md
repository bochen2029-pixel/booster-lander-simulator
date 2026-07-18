# TOOLSMITH report — `mcdiff` + `tracestat` analysis tools

**Lane:** toolsmith (`pugv4u6n`) · **Date:** 2026-07-18 night · **Status:** both tools built, verified, demoed.
**Sources & binaries:** `_tools_wt\mcdiff.c`, `_tools_wt\tracestat.c`, `_tools_wt\mcdiff.exe`, `_tools_wt\tracestat.exe`, `_tools_wt\README.md`.

C only, zero external dependencies, MSVC 2022, **clean at `/O2 /W4` (zero warnings)**. Built in the
gitignored `_tools_wt\` worktree — the real tree was **not** touched (read-only use of the main exe
to generate one demo trace).

---

## Build incantation (documented + verified)

```powershell
$vc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "call `"$vc`" && cd /d C:\Booster_Lander_Simulator\_tools_wt && cl.exe /O2 /W4 /nologo mcdiff.c && cl.exe /O2 /W4 /nologo tracestat.c"
```

Each tool is one translation unit — no CMake. `_CRT_SECURE_NO_WARNINGS` is defined at the top of
each source (the handful of `strcpy`/`strncpy` calls are hand-bounds-checked), so the build is
genuinely warning-free rather than noisy with C4996.

---

## Tool contracts (short form; full schema in `_tools_wt\README.md`)

### `mcdiff base.csv cand.csv [--pad 26] [--json]`
Joins two per-run CSVs on the `run` index and reports:
- **verdict-flip lists** — runs that converted `landed↔crashed`, with `td_v`/`td_lat` before→after.
- **cause-transition matrix** — rows = base bucket, cols = cand bucket, buckets `{landed, off-pad, too-hard, fuel, other}` (+ sums).
- **landed-cohort distribution** — mean/p50/p95 of `td_v` and `td_lat` per side + delta.
- **one-line SUMMARY** — `base X/N -> cand Y/N | +A -B | net ±`.

Buckets: `landed`=verdict∈{0,1,2,3}; then `fuel`=fault==1; `off-pad`=td_lat>pad; `too-hard`=on-pad & td_v>6; else `other`.
Differing run counts → join on intersection **with a warning** naming dropped run ids. Missing files → exit 2. Malformed lines skipped + counted. `--json` emits valid (escaped) JSON.

### `tracestat trace.txt [--json]`
Parses a `--verbose` dump and emits per-run features: **ignition** (landing burn, ph≥4 thr-jump), **arrest** (first vz≥0 after ignition), **max climb after arrest** (auto-flags the min-throttle climb-trap), **fuel margin** (last `m`; classifies an airborne freeze as **FUEL-OUT** vs a near-ground freeze as normal post-landing cut), **final approach** (last 5 s mean |vrad| + lat trend), **phase timeline** (ph transitions). Robust to whitespace-variable, ANSI-free `key=value` lines; header and trailing `RESULT:` line skipped + counted. `--json` for machine use.

---

## Demo 1 — `mcdiff` on two real ENTRY sweeps

`d009_entry_fix.csv` (an early build — all off-pad) vs `d012_entry_v4.csv` (the shipped v4 factorial
config). Both `entry`, seed 42, 100 runs — a clean full join. The transition-matrix off-pad row
**reproduces the exact v4 failure anatomy from the handoff** (op 6, th 9, fuel 1):

```
=== mcdiff ===
base: ...\d009_entry_fix.csv  (100 runs, scenario=entry seed=42, landed=0, 0 skipped)
cand: ...\d012_entry_v4.csv   (100 runs, scenario=entry seed=42, landed=84, 0 skipped)
pad radius (on-pad if td_lat <= P): 26.00 m

-- verdict flips (joined runs) --
crashed -> landed : 84
   run 1      off-pad  -> landed     td_v  4.310-> 4.604  td_lat  122.402->   6.483
   ... (83 more) ...
landed -> crashed : 0

-- cause-transition matrix (rows=base, cols=cand) --
base\cand    landed  off-pad too-hard     fuel    other     |sum
landed            0        0        0        0        0        0
off-pad          84        6        9        1        0      100
too-hard          0        0        0        0        0        0
fuel              0        0        0        0        0        0
other             0        0        0        0        0        0
|sum             84        6        9        1        0      100

-- landed-cohort distribution (whole-file) --
metric   side         mean        p50        p95
td_v     cand       3.6500     3.5910     5.7760
td_lat   cand      12.5374    13.2640    22.6060
(base landed n=0, cand landed n=84)

SUMMARY: base 0/100 -> cand 84/100 landed  |  +84 crashed->landed, -0 landed->crashed  |  net +84
```

### Demo 1b — mismatched-count warning path

`mcdiff d009_aero_fix.csv wt_aero.csv` (300 vs 100 runs) exercises the intersection warning:

```
WARNING: run sets differ; joining on intersection of 100.
         200 run(s) only in base (e.g. 101 102 103 104 105 106 107 108 ...)
```

The malformed-line path was also verified (a CSV with a comma-free garbage line and a
non-numeric row → those 2 rows reported as `skipped`, good rows joined normally, including a
demonstrated `landed->crashed` flip and matrix cell — all correct). `--json` output was validated
as parseable by a strict JSON parser (Windows-path backslashes escaped).

---

## Demo 2 — `tracestat` on the known min-throttle climb-trap seed

Generated read-only from the main exe:
`booster-core --run --scenario entry --seed 42 --run 14 --verbose`. The tool **automatically
surfaces the arrest-at-ground → climb-to-550 m → burn-dry → freefall signature** the handoff
describes as the ENTRY fuel-out failure mode:

```
=== tracestat: ...\trace_entry_s42_r14.txt ===
scenario=entry seed=42 run=14  h0=61085 m  vz0=-1482.9 m/s
samples: 329  (t_end=164.50s, 1 line(s) skipped)

-- phase timeline --
   ph=2  @ t=   0.50   h=  60329.7  vz= -1487.7
   ph=3  @ t=  26.00   h=  37626.7  vz=  -137.9
   ph=4  @ t= 112.00   h=   2592.2  vz=  -287.8

-- landing ignition --
   t=112.00  h=2592.2  vz=-287.8  lat=19.1  vrad=4.6  (thr 0.76, ph=4)

-- arrest (first vz>=0 after ignition) --
   t=131.00  h=1.4  vz=1.3

-- max climb after arrest --
   peak h=550.8 @ t=153.50   climb=549.4 m above arrest   peak vz(+)=47.7
   ** large re-ascent after arrest -- min-throttle climb-trap signature **

-- fuel margin --
   final m=25600.0   FUEL-OUT: mass frozen from t=150.00 at h=480.3 (30 samples airborne on dry tanks)

-- final approach (last 5s) --
   mean|vrad|=7.864 m/s   lat 85.4 -> 124.7 (trend +39.3, diverging)
```

Read the story straight off the report: the landing burn lights at 2.6 km, the vehicle arrests
essentially **at the pad** (h=1.4 m) but — stuck at min throttle with TWR>1 — balloons back up
**549 m**, exhausts the tanks at h=480 m (frozen mass, airborne ⇒ FUEL-OUT), and diverges off-pad on
the way down. The `climb-trap` banner and the FUEL-OUT classification both fire automatically.

### Demo 2b — control: a normal landing does NOT false-positive

`tracestat` on a `terminal` seed-42 run-1 landing (`--verbose`):

```
-- max climb after arrest --
   peak h=1.1 @ t=21.50   climb=0.0 m above arrest   peak vz(+)=0.3
-- fuel margin --
   final m=29598.0   frozen from t=21.50 at h=1.1 (engine cut near ground -- normal post-landing, not fuel-out)
```

No climb-trap banner; the ground-level mass freeze is correctly labeled a normal post-landing engine
cut (not fuel-out) — the h-at-freeze test (`FREEZE_GND_H 10 m`) discriminates the two.

Both tools' `--json` output was validated as strict-parseable in every demo.

---

## Verification summary

| check | result |
|-------|--------|
| MSVC `/O2 /W4` clean build, both tools | **PASS, 0 warnings** |
| `mcdiff` full join (matching counts) | PASS — matrix + flips + deltas correct, matches v4 anatomy |
| `mcdiff` intersection warning (300 vs 100) | PASS |
| `mcdiff` malformed lines skipped + counted | PASS |
| `mcdiff` `--json` strict-parseable | PASS |
| `mcdiff` missing file → exit 2 | PASS |
| `tracestat` climb-trap auto-detection (entry s42 r14) | PASS — arrest+549 m climb+FUEL-OUT surfaced |
| `tracestat` normal landing (no false trap, not-fuel-out) | PASS |
| `tracestat` `--json` strict-parseable | PASS |
| `tracestat` missing file / RESULT-line skip | PASS |

---

## Limitations & notes (honest)

1. **`mcdiff` distribution deltas are whole-file, not paired.** The landed-cohort mean/p50/p95 are
   computed over each file's entire landed cohort (standard for comparing sweep quality), not over
   the paired subset of runs landed in *both*. This is the right statistic for "did the candidate's
   landings get softer" but is not a per-run paired test. The flip lists *are* per-run.
2. **Percentiles use nearest-rank** (no interpolation) on the sorted cohort. For n≈40–200 landed
   runs this is fine; for very small cohorts p95 is coarse by construction.
3. **`mcdiff` join key is `run` only**, not `seed+scenario+run`. If two files with different seeds
   share run indices it will join them and (if scenarios differ) warn, but it will not refuse. Feed
   it comparable sweeps. Scenario mismatch warns but does not abort.
4. **Bucket order is fixed** (`fuel` is tested before `off-pad`/`too-hard`): a fuel-fault crash that
   also happens to be off-pad is counted as `fuel`. This matches the project's crash-cause bucketing
   in the handoff. `other` absorbs TIPPED (verdict 4) and on-pad-but-soft-crash rows.
5. **`tracestat` ignition detection is ph-gated to the landing burn (ph≥4).** The entry-deorbit burn
   (ph 2, thr→1 at the top) is intentionally *not* reported as ignition. If a scenario lights the
   landing burn before `ph` reaches 4, a fallback takes the first ph≥4 sample with thrust — check the
   reported `ph` if a trace has unusual phase wiring.
6. **`tracestat` "arrest" is the first vz≥0 after ignition.** In a healthy landing this is the
   touchdown settle; in the climb-trap it fires low and the re-ascent is captured by *max-climb*.
   A run that never arrests (straight-in crash) reports arrest=null and no climb figure — by design.
7. **Fuel = last `m` (dry-frozen proxy), not a propellant integral.** The `fuel` column of the CSV
   (used by `mcdiff` only indirectly) is authoritative for remaining kg; `tracestat`'s mass-freeze
   signal is a *timing/altitude* diagnostic, not a mass-balance.
8. **Static array caps** (`MAX_ROWS 200000`, `MAX_SAMP 20000`) — generous for current sweeps/traces;
   the tools warn and truncate rather than overflow if exceeded.
9. **`--verbose` cadence is 0.5 s.** Event timestamps (ignition/arrest/freeze) are quantised to that
   grid; sub-0.5 s precision is not available from the trace.

Tunable constants live at the top of each source (documented in `README.md`) for easy re-baselining.
