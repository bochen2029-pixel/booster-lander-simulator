# M5 CUDA MPPI — integration-ready REBASE onto protocol-v3 main (report)

**Agent:** cuda-mppi lane — **CUDA-3** (`vlbop5v9`, opus-4.8), successor to CUDA-2 (`mqfyhpon`) / port author (`fqkrauvv`).
**Date:** 2026-07-18 night. **Task:** rebase the validated M5 CUDA MPPI port from `_cuda_wt/` (branched at
D-012, protocol **v2**) onto the CURRENT main tree (D-013, protocol **v3**), with **CI-SAFE optional-CUDA
CMake** (the GitHub runner has no CUDA toolkit), fully re-gated.
**Rebased worktree:** `C:\Booster_Lander_Simulator\_cuda2_wt\` (gitignored; the main session integrates from here).
**Hardware:** RTX 4070 Ti SUPER (sm_89), CUDA 13.1.80, MSVC 19.44, VS2022 x64.

> STATUS: **ALL PRIMARY GATES GREEN.** Both build paths PROVEN (CUDA on this machine + CPU-only via
> `-DBL_CUDA=OFF`). selftest PASS (both builds); TERMINAL 194/200 (both builds); protocol v3 goldens MATCH
> (both builds); K=256 parity ≈1 ULP + top-64 100%; GPU run-twice bit-identical; **AERO `--mppi-cuda` 44/60
> LINE-FOR-LINE identical to CPU `--mppi` 44/60** (both = D-012 golden); determinism pair identical. K=16384:
> §7. (Landed-rate batches ran under peak fleet contention — CPU ~35 min, CUDA ~27 min wall; rates are
> contention-invariant by determinism.)

---

## 1. How the rebase was derived (exact-delta method)

`_cuda_wt/` is a plain (gitignored) copy, so its deltas were extracted against its own base commit, NOT
guessed. Established the base by markers: `_cuda_wt` has the D-012 KDIV overspeed schedule (`KDIV_SEEK/
BRAKE/VBLEND` in `guidance_hoverslam.h`) but NOT protocol v3 (`BL_PROTO_VERSION 2u`, no `pred_impact`/
`ignite_h`, no `bl_predict_ignite_h`, no `tools/`) → **base = `b3399b0` (D-012)** (the D-012 addendum
`0e33411` touched no `core/` file — verified). Extracted `git archive b3399b0` and diffed vs `_cuda_wt/core/`.

**Predecessor deltas (base b3399b0 → `_cuda_wt`), the exact set applied here:**

| file | kind | what (whitespace-insensitive real change) |
|---|---|---|
| `core/guidance_mppi_cuda.h`   | **NEW** | public C interface to the CUDA rollout (extern "C") |
| `core/guidance_mppi_cuda.cu`  | **NEW** | the port: unity-`#include`s plant `.c` (host+device), MPPI kernels |
| `core/guidance_mppi_rollout.cuh` | **NEW** | rollout-cost math extracted verbatim from `guidance_mppi.c`, `BL_HD` |
| `core/atmosphere.c` | qualifier-only | `BL_HD` on `atmo_eval`; 4 base tables → function-local `static const` |
| `core/contact.c`    | qualifier-only | `BL_HD` on `foot_body_pos` (the other 4 fns already `BL_HD` in v3) |
| `core/control.c`    | qualifier-only | `BL_HD` on `fin_dip_ctrl` / `body_cna_ctrl` / `xcp_frac_ctrl` |
| `core/dynamics.c`   | qualifier-only | `BL_HD` on `table_lookup`/`fin_dip`/`xcp_frac`; 3 aero tables → function-local in `dynamics_deriv` |
| `core/sim.c`        | additive | `g_mppi_use_cuda` global + GPU dispatch branch in the GM_MPPI replan |
| `core/main.c`       | additive | `--mppi-cuda` flag (in `cmd_run`+`cmd_headless`), `cmd_mppi_cuda_verify`/`cmd_mppi_cuda_bench`, `main()` dispatch, usage string |
| `core/CMakeLists.txt` (core) | rewrite | CUDA target: `.cu` owns plant `.c`, `-fmad=false`, sm_89, `BL_HAVE_CUDA=1` |
| `CMakeLists.txt` (top) | rewrite | `project(... C CUDA)`, static cudart |

**The ONLY file both D-013 (v3) and the predecessor touched is `main.c`** — verified by intersecting the two
change-sets:
- D-013 touched `core/{guidance_mppi.c(+4), guidance_mppi.h(+7), main.c(+17), protocol.h}` — the predecessor
  touched NONE of `guidance_mppi.c/h` or `protocol.h`, so those are taken **verbatim from current main** (v3).
- D-013 did **not** touch `sim.c/atmosphere.c/contact.c/control.c/dynamics.c` (`sim.c` byte-identical to
  b3399b0), so the predecessor's edits to those apply onto the v3 files **cleanly, no conflict**.
- `main.c`: the v3 change is a self-contained ~17-line block inside `fill_tlm()` (writes `pred_impact[2]`+
  `ignite_h`); the predecessor's additions are in the includes, `cmd_run`/`cmd_headless` arg loops, a harness
  block after `cmd_golden`, and `main()` dispatch — **disjoint regions**. Merged by applying the predecessor's
  additions onto the current-main `main.c` (keeping 100% of the v3 block). Verified coexisting (§4).

**Fidelity check:** after applying, `_cuda2_wt/core/{atmosphere,contact,control,dynamics}.c` are byte-identical
to `_cuda_wt/core/` (the whitespace-insensitive real-diff is empty), and the 3 new CUDA files are byte-exact
copies (same size). So the rebased CUDA path is the *same* validated port, now on the v3 plant/protocol.

---

## 2. CI-SAFE conditional-CUDA CMake design (the central new engineering)

The predecessor's CMake declared `project(BoosterLander C CUDA)` and set CUDA runtime **unconditionally** —
that **hard-fails `cmake` configure** on any machine without a CUDA toolkit (the GitHub CI runner), which
would break the per-push selftest+TERMINAL CI. The rebase makes CUDA fully optional:

**Top-level `CMakeLists.txt`:**
- `project(BoosterLander C)` — **C only**; CUDA is NEVER in `project()` (that is the thing that fails configure).
- `option(BL_CUDA "..." ON)` — default ON, auto-detecting.
- If `BL_CUDA`: `include(CheckLanguage)` → `check_language(CUDA)` (probes for a usable `nvcc` WITHOUT failing) →
  only if `CMAKE_CUDA_COMPILER` is set do we `enable_language(CUDA)` and set `BL_CUDA_ENABLED ON`. Otherwise a
  `message(STATUS ...)` explains the CPU-only fallback. `-DBL_CUDA=OFF` forces CPU-only.

**`core/CMakeLists.txt`** — two source layouts keyed on `BL_CUDA_ENABLED`:
- **CUDA ON:** target = driver/guidance `.c` + `guidance_mppi_cuda.cu`; the `.cu` unity-`#include`s the plant
  `.c` (`atmosphere/dynamics/integrator/contact/control`) so those are **excluded from the C target**;
  `BL_HAVE_CUDA=1`; `CUDA_ARCHITECTURES 89`, static cudart, per-language `-fmad=false`.
- **CUDA OFF (CI):** target = **all** `.c` incl. plant; **no** `.cu`; `BL_HAVE_CUDA` undefined. The `BL_HD`
  qualifiers in the plant `.c` expand to nothing under a non-nvcc compiler (defined in `vmath.h` as
  `__host__ __device__` only under `__CUDACC__`), so the CPU exe is byte-identical to plain v3.

**Runtime graceful-degradation (no-CUDA build):** `main.c` guards the CUDA harness bodies and the `sim.c`
dispatch on `BL_HAVE_CUDA`; `--mppi-cuda`, `--mppi-cuda-verify`, `--mppi-cuda-bench` print a clear
"this build has no CUDA support (configure with -DBL_CUDA=ON …)" and exit **4** — never a link error against
absent `mppi_cuda_*` symbols, never a silent wrong result. `g_mppi_use_cuda` exists in BOTH builds (so the
flag parse compiles) but is unreachable-to-set in the CPU-only build.

### Both-path build PROOF (this machine)

| path | configure | build | selftest |
|---|---|---|---|
| **(a) CUDA on** (default `BL_CUDA=ON`, auto-detect) | `CUDA toolkit found (…/v13.1/bin/nvcc.exe) → GPU path`; `core: CUDA MPPI rollout ENABLED (sm_89, -fmad=false)`; Configuring done | nvcc `arch=compute_89,code=[compute_89,sm_89] -fmad=false -cudart static -DBL_HAVE_CUDA=1` → `booster-core.exe` **CLEAN (no errors/warnings)** | **PASS** |
| **(b) CPU-only** (`-DBL_CUDA=OFF`) | `BL_CUDA=OFF → CPU-only build`; `core: CPU-only build (no CUDA)`; **no CUDA language engaged**; Configuring done (40.8s) | `booster-core.exe` **CLEAN** | **PASS** |

CPU-only runtime graceful-degradation verified: `--mppi-cuda` → `error: --mppi-cuda: this build has no CUDA
support …` exit 4; `--mppi-cuda-verify` → same, exit 4. (Simulates the CI runner exactly.)

---

## 3. Parity + determinism (CUDA build, rebased tree) — reproduce the predecessor EXACTLY

`--mppi-cuda-verify` on the rebased CUDA exe, AERO_OFFSET seed=42 run=3 @step 6000
(capture: h=7804.9 m, vz=−310.8, lat=553.4 m, ignite_h=3499.1, replan=120, m0=35686 kg — identical to the
predecessor's §3 capture):

| metric | K=256 (rebased) | predecessor `_cuda_wt` §3 | verdict |
|---|---|---|---|
| cost range (cpu-ref min) | 4920.1206 | 4920.12 | identical |
| max \|Δcost\| | **4.54747e-12** | 4.55e-12 | identical |
| mean \|Δcost\| | 5.43565e-13 | 5.44e-13 | identical |
| max relative Δ | **8.721e-16** (≈1 ULP) | 8.72e-16 | identical |
| top-16 rank agreement | **100% (16/16)** | 100% | identical |
| top-64 rank agreement | **100% (64/64)** | 100% | identical |
| **GPU run-twice bit-identity** | **YES (max\|Δ\|=0)** | YES | identical |

**PARITY GREEN + DETERMINISM GREEN at K=256** — the port is byte-for-byte the same faithful fp64 GPU image of
the CPU controller after the v3 rebase. (K=16384 build + gate: §7, via a dedicated `-DMPPI_K=16384` build.)

---

## 4. Protocol v3 goldens — the rebase's v3-integrity check — MATCH

`--golden` (a deterministic TERMINAL/GM_HOVERSLAM run through `fill_tlm`, which writes the v3 `pred_impact`+
`ignite_h`) vs the frozen `goldens/protocol/*.hex`, byte-for-byte:

| golden | bytes | CUDA build | CPU-only build |
|---|---|---|---|
| `hello.hex` | 72 (144 hex) | **MATCH** | **MATCH** |
| `tlm.hex`   | 288 (576 hex) | **MATCH** | **MATCH** |
| `evt.hex`   | 48 (96 hex) | **MATCH** | **MATCH** |

Both builds also produce byte-identical protocol output to each other. → the merged `protocol.h` + `fill_tlm`
reproduce the D-013 v3 goldens exactly; the CUDA rebase did not perturb the telemetry schema. **v3 GREEN.**

---

## 5. Full gate scorecard (rebased `_cuda2_wt`)

| Gate | Requirement | Result |
|---|---|---|
| CI-safe build (a) CUDA on | clean full build, this machine | **PASS** — nvcc sm_89 `-fmad=false`, exe linked clean |
| CI-safe build (b) CPU-only `-DBL_CUDA=OFF` | clean CPU-only build (CI runner sim) | **PASS** — no CUDA engaged; `--mppi-cuda` errors gracefully (exit 4) |
| `--selftest` (CUDA) | `SELFTEST: PASS` | **PASS** |
| `--selftest` (CPU-only) | `SELFTEST: PASS` | **PASS** |
| TERMINAL s42 x200 (CPU-only) | EXACTLY 194/200 | **194/200 = 97.0%** (td_v 1.93, lat 4.60, tilt 1.90, fuel 4183 — matches D-012/main headline) |
| TERMINAL s42 x200 (CUDA build) | EXACTLY 194/200 | **194/200 = 97.0%** (td_v 1.93, lat 4.60, tilt 1.90, fuel 4183 — CUDA-build CPU path byte-identical; the `.cu` unity-include did not perturb the plant) |
| Protocol goldens `--golden` (both builds) | MATCH `goldens/protocol/*.hex` (v3) | **MATCH** (hello/tlm/evt, both builds) |
| Parity CPU-ref vs GPU @K=256 | ≈1 ULP, top-64 100% | **max rel 8.72e-16, top-64 100%** |
| GPU determinism run-twice @K=256 | bit-identical | **YES (max\|Δ\|=0)** |
| AERO `--mppi` s42 x60 (CPU) | 44/60 (D-012 golden) | **44/60 = 73.3%** (GOOD 12 / HARD 32 / CRASHED 16; off-pad 13 / too-hard 2 / fuel 1; td_v 2.95, lat 14.37, tilt 2.64, fuel 4553) |
| AERO `--mppi-cuda` s42 x60 (GPU) | 44/60, line-for-line vs CPU | **44/60 = 73.3% — LINE-FOR-LINE IDENTICAL to CPU** (same GOOD 12/HARD 32/CRASHED 16, off-pad 13/too-hard 2/fuel 1, td_v 2.95, lat 14.37, tilt 2.64, fuel 4553) |
| Determinism pair (CUDA `--run` twice) | identical RESULT lines | **IDENTICAL** (td_v 2.08, lat 17.31, tilt 0.10, fuel 4590, t 55.1, maxq 36777 — both runs) |
| GPU determinism / parity @K=16384 (`-DMPPI_K=16384` build) | bit-identical + ≈1 ULP | **PASS** — selftest PASS; max rel Δ **1.280e-15** (≈1 ULP), top-64 **100%**, run-twice **bit-identical YES** (§7) |

---

## 6. Integration file list (for the main session)

Integrate from `C:\Booster_Lander_Simulator\_cuda2_wt\` onto main:

**NEW files (copy verbatim):**
- `core/guidance_mppi_cuda.h`
- `core/guidance_mppi_cuda.cu`
- `core/guidance_mppi_rollout.cuh`

**MODIFIED files (take the `_cuda2_wt` version — already the v3 file + the CUDA delta merged):**
- `CMakeLists.txt` (top: CI-safe optional CUDA)
- `core/CMakeLists.txt` (conditional CUDA target / CPU-only)
- `core/main.c` (v3 `fill_tlm` PRESERVED + `--mppi-cuda`/harness/dispatch added)
- `core/sim.c` (`g_mppi_use_cuda` + `BL_HAVE_CUDA`-guarded GPU dispatch)
- `core/atmosphere.c`, `core/contact.c`, `core/control.c`, `core/dynamics.c` (`BL_HD` qualifier-only; value-identical, TERMINAL-194 proves it)

**UNCHANGED from main (do NOT overwrite):** `core/protocol.h`, `core/guidance_mppi.c`, `core/guidance_mppi.h`
(these are the D-013 v3 files; the predecessor never touched them), and every other `core/` file.

**ADR:** carry over the predecessor's `DECISIONS.md` recommendation (fp64-everywhere under directive 7; the
6 ms gate re-scoped to the 10 Hz/100 ms operating budget) PLUS this rebase's CI-safe-CUDA decision
(`option(BL_CUDA)` + `check_language`/`enable_language`, graceful runtime refusal when absent).

**CI note:** the existing `.github/workflows/ci.yml` (selftest + TERMINAL) will keep working unchanged — the
default `BL_CUDA=ON` auto-detects the absent toolkit on the runner and silently builds CPU-only. No CI edit
required; optionally add an explicit `-DBL_CUDA=OFF` for clarity.

---

## 7. K=16384 (the design's target K) — parity + determinism GREEN, rebased tree

A dedicated `-DMPPI_K=16384` build (`_cuda2_wt/build_k16384/`, CUDA enabled, sm_89 `-fmad=false`) — **selftest
PASS** — then `--mppi-cuda-verify` (same AERO s42 r3 @step 6000 capture) at K=16384:

| metric | K=16384 (rebased) | predecessor `_cuda_wt` §3/§4 | verdict |
|---|---|---|---|
| cost range (cpu-ref min) | 4663.0526 | 4663.05 | identical |
| max \|Δcost\| | **6.36646e-12** | 6.37e-12 | identical |
| mean \|Δcost\| | 5.98133e-13 | 5.98e-13 | identical |
| max relative Δ | **1.280e-15** (≈1 ULP) | 1.28e-15 | identical |
| top-16 / top-64 rank agreement | **100% / 100%** | 100% / 100% | identical |
| **GPU run-twice bit-identity** | **YES (max\|Δ\|=0)** | YES | identical |

**K=16384 PARITY + DETERMINISM GREEN** at the design's target K — reproducing the predecessor to every
reported digit. The sm_89 determinism golden can be frozen at K=16384 with confidence (2¹⁴ = a perfectly
balanced tree for the fixed pairwise reduction). Latency at K=16384 was NOT re-measured here (the fp64/sm_89
6 ms-gate MISS and its 10 Hz/100 ms re-scope are the predecessor's central finding — `runs/cuda_mppi_report.md`
§5 — a documented property of the port, unchanged by the rebase; the verify's per-K latency sweep was cut
short after the parity/determinism block printed, to free the GPU for the fleet).

---

## 8. Bottom line

The validated M5 CUDA MPPI port is now **integration-ready on protocol-v3 main**, with **CI-safe optional
CUDA** proven on both paths. Every gate that certified the original port (`runs/cuda_mppi_report.md`) is
re-certified here on the v3 tree — parity, per-arch determinism (K=256 and K=16384), TERMINAL 194, and the
line-for-line CPU↔GPU landing-outcome match (44/60) — PLUS the two v3-specific gates (protocol goldens MATCH)
and the two CI-safety gates (CPU-only `-DBL_CUDA=OFF` build + graceful `--mppi-cuda` refusal). The main session
integrates from `_cuda2_wt/` using the §6 file list; no change to the real tree was made by this agent.
