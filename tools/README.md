# Booster Lander analysis tools — `mcdiff` + `tracestat`

Two single-file C tools for evaluating Monte-Carlo sweeps and diagnosing single-run
failures. C only, **zero external dependencies**, MSVC 2022 buildable, clean at `/W4`.

Built by the TOOLSMITH lane in `_tools_wt\` (gitignored). The main session integrates
the sources into `tools/` later — **do not edit the real tree from here.**

---

## Build incantation (MSVC, VS2022)

From any shell, run the VS2022 x64 dev environment then `cl.exe`:

```powershell
# vcvars64.bat lives under your VS install; on this machine:
$vc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "call `"$vc`" && cd /d C:\Booster_Lander_Simulator\_tools_wt && cl.exe /O2 /W4 /nologo mcdiff.c && cl.exe /O2 /W4 /nologo tracestat.c"
```

Both compile with **zero warnings** at `/O2 /W4`. `_CRT_SECURE_NO_WARNINGS` is defined
at the top of each file (the `strcpy`/`strncpy` uses are hand-bounds-checked) so MSVC's
C4996 deprecation nags are suppressed without masking real diagnostics.

No CMake needed — each tool is one translation unit. To rebuild just one, run the single
`cl.exe` line for it.

---

## Tool 1 — `mcdiff`  (Monte-Carlo CSV differ)

```
mcdiff base.csv cand.csv [--pad 26] [--json]
```

Joins two per-run CSVs on the `run` index and reports what changed from a baseline
sweep to a candidate sweep.

**Options**
- `--pad P` — on-pad lateral threshold in metres (`td_lat <= P` is on-pad). Default `26.0`.
- `--json` — machine-readable JSON instead of the text report.

**Input** — the per-run CSV emitted by `booster-core --headless --out f.csv`:
```
seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush
```
`verdict`: 0–3 landed grades (PERFECT/GOOD/HARD/…), 4 = TIPPED, 5 = CRASHED.
`fault`: 0 = none, 1 = FUEL (STRUCT/THERMAL/LOC also exist).

**Cause buckets** (project taxonomy; a run is classified once, in this order):
| bucket    | rule |
|-----------|------|
| `landed`  | `verdict ∈ {0,1,2,3}` |
| `fuel`    | not landed AND `fault == 1` |
| `off-pad` | not landed, `fault != 1`, `td_lat > pad` |
| `too-hard`| not landed, `fault != 1`, `td_lat <= pad`, `td_v > 6` |
| `other`   | any remaining non-landed (e.g. TIPPED, soft on-pad crash) |

**Output** (text default):
1. **verdict-flip lists** — every joined run that converted `landed↔crashed`, with its
   `td_v` and `td_lat` before→after.
2. **cause-transition matrix** — rows = base bucket, cols = cand bucket (+ row/col sums).
3. **landed-cohort distribution** — mean / p50 / p95 of `td_v` and `td_lat` for the landed
   runs on each side, plus the delta. (Computed over each file's whole landed cohort.)
4. **one-line SUMMARY** — `base X/N -> cand Y/N landed | +A crashed->landed, -B landed->crashed | net ±(A-B)`.

**Join semantics** — the report covers the **intersection** of run indices in both files.
If the run sets differ, a `WARNING` names the count and example run ids dropped from each
side (join proceeds on the intersection). A scenario mismatch (e.g. entry vs aero_offset)
also warns.

**JSON schema** (top-level keys):
`base`,`cand` (`{path,rows,skipped,scenario,seed,landed}`), `pad`, `joined`, `only_base`,
`only_cand`, `buckets` (bucket-name order), `matrix` (5×5, row=base bucket), `flips_to_landed`,
`flips_to_crashed` (arrays of `{run,from,to,base_td_v,base_td_lat,cand_td_v,cand_td_lat}`),
`landed_dist` (`td_v`/`td_lat` each with base/cand `{n,mean,p50,p95}` + `d_mean/d_p50/d_p95`),
`summary`. String values are escaped (valid JSON on Windows paths).

**Exit codes** — `0` OK, `2` usage error / unreadable file.
Malformed data lines are **skipped and counted** (surfaced as `skipped` in both formats).

---

## Tool 2 — `tracestat`  (verbose-trace feature extractor)

```
tracestat trace.txt [--json]
```

Parses a single-run `--verbose` dump (0.5 s cadence telemetry) and surfaces the events an
analyst looks for by hand.

**Options** — `--json` for machine-readable output.

**Input** — the dump from `booster-core --run … --verbose`. Line 1 is a header
(`scenario=… seed=… run=… h0=… vz0=…`); telemetry lines are `key=value` tokens:
```
  t= 12.00 h= 45791.0 vz= -992.8 thr=1.00 tilt= 2.08 lat= 2359.0 vrad= -50.1 qbar= 869 wperp= 0.00 m= 45468 ph=2
```
The parser locates each field **by key**, so it is insensitive to column spacing and to
extra/reordered keys. Non-telemetry lines (header, the trailing `RESULT:` line) are skipped
and counted.

**Features emitted**
| feature | meaning |
|---------|---------|
| `ignition` | first sample where `thr` jumps `>0.5` from ~0 while `ph >= 4` (the landing burn — the entry-deorbit burn at ph 2 is deliberately excluded). Reports `t,h,vz,lat,vrad,thr,ph`. |
| `arrest` | first `vz >= 0` at/after ignition. Reports `t,h`. |
| `max_climb_after_arrest` | `max(h)` at/after arrest minus arrest `h`, plus peak positive `vz`. A large value is auto-flagged as the **min-throttle climb-trap signature** (arrest near the pad → balloon back up on min throttle → burn dry). |
| `fuel` | last `m` (dry-frozen mass proxy). Detects a terminal mass freeze and classifies it: frozen **airborne** (`h > 10 m`) ⇒ **FUEL-OUT**; frozen near ground ⇒ normal post-landing engine cut. |
| `final_approach` | over the last 5 s: mean `|vrad|`, and lat trend (last−first) — converging (`<0`) or diverging. |
| `phases` | every `ph` transition with `t,h,vz`. |

**JSON schema** — `file`, `header`, `samples`, `skipped`, `t_end`, `ignition`, `arrest`,
`max_climb_after_arrest`, `fuel` (`{m_final,frozen,freeze_t,freeze_h,frozen_samples,fuel_out}`),
`final_approach`, `phases`. `null` where a feature was not reached. Strings escaped.

**Exit codes** — `0` OK, `2` usage error / unreadable file / no telemetry parsed.

---

## Constants (top of each source, easy to retune)

`mcdiff`: `TD_V_HARD 6.0`, default pad `26.0`, `MAX_ROWS 200000`.
`tracestat`: `IGN_DTHR 0.5`, `IGN_THR_LOW 0.05`, `PH_LANDING 4`, `FINAL_WINDOW 5.0 s`,
`FREEZE_GND_H 10.0 m`, `CLIMB_TRAP_M 50.0 m`, `MAX_SAMP 20000`.
