# CoVO diagonal-covariance MPPI — design, sweep, verdict (D-017)

**Lane:** `covo` (opus-4.8, id jrvibezo) · **Date:** 2026-07-18 night → 07-19 · **Worktree:** `_covo_wt` (CPU-only, gitignored)
**Blueprint:** `runs/mppi_research.md` #2 CoVO covariance (L170-176) + CoVO analysis (L32-63).
**Target:** M4 gate — AERO_OFFSET `--mppi` ≥ 90% (baseline 44/60 = 73.3% s42). AERO misses are **off-pad reach**.
**VERDICT: NULL — do not adopt** (best ρ=1.5 = 44/60, dead flat; ρ≥2 harmful). Full diagnosis §4.

---

## 1. Design — the rotation and the diagonal Σ

### 1.1 The idea (CoVO Cor 4.4)
CoVO-MPC (Yi/Pan, L4DC 2024) proves the det-constrained optimal sampling covariance is
`Σ* ∝ D^(−1/2)` (D = cost Hessian): **large variance along cost-smooth directions, small along
sharp ones**, and suboptimality is only *second-order* in the exploration scale. For our landing,
the cost-smooth direction is the **radial / reach axis** (toward-or-away the pad): that is exactly
where the off-pad misses live (the sampled set lacks a trajectory that reaches the far seed *and*
arrives nulled). The cross-track (tangential) direction is comparatively sharp — the converging
profile pins v_tangential tightly. So CoVO says: **sample MORE radially, LESS tangentially.**

### 1.2 The frame (deterministic per replan)
At each replan we read the vehicle's current cross-range vector `r_xy = (r_x, r_y)` and form:
- **radial** unit vector `r̂ = r_xy / |r_xy|` (points outward from pad to vehicle — the reach axis),
- **tangential** unit vector `t̂ = R₉₀·r̂ = (−r̂_y, r̂_x)` (cross-track).

`r̂` is a pure function of the plant state read **once per replan** (in `compute_covo_frame`),
identical for all K rollouts → bit-deterministic. Degenerate `|r_xy| < 1e-3` (never in a divert)
falls back to world-x so the transform is always defined.

### 1.3 The det-preserving scale (isolates SHAPE from magnitude)
To test the CoVO *shape* and not merely "more σ", we hold `det(Σ)` fixed (CoVO's det-constrained
optimum) via the ratio `ρ = σ_rad / σ_tan`:

```
σ_rad = SIG_ALAT · √ρ            (radial, reach axis — LARGER)
σ_tan = SIG_ALAT / √ρ            (tangential, cross-track — smaller)
⇒ σ_rad · σ_tan = SIG_ALAT²  (det preserved; ρ=1 ⇒ isotropic baseline, bit-exact)
```

With SIG_ALAT = 1.5 m/s²: ρ=1.5 → (1.84, 1.22); ρ=2 → (2.12, 1.06); ρ=3 → (2.60, 0.87).
All keep the trace-equivalent "budget" comparable, so a rate change is attributable to the
**directional reallocation**, not a variance increase. (A follow-up magnitude sweep is noted §5.)

### 1.4 Ratio provenance — swept, not derived
The mission offered two options: derive ρ from the cost curvature proxy (Q_RXY/Q_VXYERR around the
warm-start) OR sweep {1.5, 2, 3}. **We SWEEP** — chosen because the "cost-smooth axis" here is a
composite of many running/terminal terms (Q_VXYERR tracks the *radial* converging profile, Q_VOUT
penalizes *outward radial* velocity asymmetrically, Q_RXY is a gentle radial pull, TD_RXY/T_ZEM are
radial-dominant), so a single closed-form Q-ratio Hessian proxy would be a guess; a 3-point sweep
directly measures where the reach payoff peaks and whether it is monotone. Documented per mission.

### 1.5 Why NO separate directive-7 mirror is needed
The two lateral channels `u[1], u[2]` map **directly to world-frame acceleration** `a_lat[0], a_lat[1]`
(guidance.h: `a_lat[2]` = "desired **world** lateral accel"). We draw white `z_rad, z_tan` per (k,t)
(same Philox counters as before), OU-color each in-frame, then **rotate the colored frame noise into
world XY** before storing in `EPS`:

```
eps_world_x = e_rad·r̂_x + e_tan·t̂_x = e_rad·r̂_x + e_tan·(−r̂_y)
eps_world_y = e_rad·r̂_y + e_tan·t̂_y = e_rad·r̂_y + e_tan·( r̂_x)
```

`EPS[k][t][{1,2}]` is therefore **world-frame accel noise**, exactly as the isotropic path stored —
so the rollout (`u = ubar + eps` → `a_lat` world), the softmax reduction (`ubar += Σ w_k eps_k`),
the Savitzky-Golay smooth, the ±A_LAT_GAMUT clamp, and `mppi_execute` all consume correctly-shaped
world noise with **no separate mirror** (the shaping is invisible downstream — only the *distribution*
of the world noise changed). This is the clean property the mission flagged to check for.

### 1.6 The importance-sampling term (kept exact)
The noise is now anisotropic in world XY: `Σ_world = R·diag(σ_rad², σ_tan²)·Rᵀ` (a full symmetric
2×2). The IS correction `γ·uᵀΣ⁻¹ε` therefore uses the **exact world-frame inverse-covariance**
`Σ⁻¹ = R·diag(1/σ_rad², 1/σ_tan²)·Rᵀ = [[isXX, isXY],[isXY, isYY]]`, precomputed once per replan:

```
isXX = ir·r̂x² + it·t̂x²,  isXY = ir·r̂x·r̂y + it·t̂x·t̂y,  isYY = ir·r̂y² + it·t̂y²
    (ir = 1/σ_rad², it = 1/σ_tan²)
isc = isXX·u₁·ε₁ + isYY·u₂·ε₂ + isXY·(u₁·ε₂ + u₂·ε₁)
```

This keeps the IS likelihood-ratio consistent with the actual (rotated, anisotropic) proposal — not
an isotropic approximation. (The IS term is a small tie-breaker, GAMMA_IS=1.0, but making it exact
costs nothing and avoids any theoretical inconsistency.)

### 1.7 Determinism ledger
- `r̂`, σ_rad, σ_tan, Σ⁻¹ computed once per replan from `st->y` → identical for all K rollouts.
- One Philox draw per (k, t, c), **same counters** `(replan, t·NCH+c, k)`, stream RNG_MPPI.
- OU recursion order-independent; rotation is a fixed 2×2 per (t); reduction unchanged (pairwise tree).
- **ρ=1 (COVO_ON=0) is a bit-exact no-op**: `σ_rad=σ_tan=SIG_ALAT` (√1=1 exactly), guarded so the
  isotropic code path (no rotation, diagonal IS) is compiled verbatim → reproduces 44/60 byte-for-byte.

---

## 2. Implementation (files touched, `_covo_wt` only — real tree never touched)

- **`core/guidance_mppi.h`** — added to `MppiState`: `rhx, rhy` (radial basis), `sig_rad, sig_tan`,
  `isXX, isXY, isYY` (world Σ⁻¹). All per-replan.
- **`core/guidance_mppi.c`**:
  - `#define COVO_ON` (int preprocessor switch, 0/1) + `#define COVO_RATIO` (double ρ). Defaults 0/1.0
    → identical to the shipped controller. (Int switch because the C `#if` can't compare a float.)
  - `compute_covo_frame(M, st)` — the §1.2–1.6 math; called once in `mppi_step` after `warm_start_nominal`.
  - Sampling loop: `#if !COVO_ON` = verbatim baseline; `#else` = frame-draw + OU-color + rotate-to-world.
  - IS term: `#if !COVO_ON` = diagonal `invSig`; `#else` = anisotropic 2×2 (isXX/isXY/isYY).
  - `drive[]` uses `M->sig_rad/M->sig_tan` (ρ=1 ⇒ bit-identical to `SIG_ALAT·√(2θ−θ²)`).

Sweep driver: `_covo_wt/covo_sweep_lean.ps1` (regex-patch the two #defines → rebuild `guidance_mppi.c`
only → gate selftest+TERMINAL-194 → AERO s42 x30 screen → one CSV row; det no-op already proven so ρ=1
row dropped). Output: `_covo_wt/covo_screen.csv` (copied to `runs/covo_screen.csv`). The winning ratio
(1.5) was then confirmed at x60. (The original x60 4-config `covo_sweep.ps1` was abandoned mid-run —
fleet CPU contention made x60-per-config ~30+ min each; the x30 screen + x60 winner-confirm is the same
signal at a fraction of the contended wall-time.)

---

## 3. Results

### 3.1 Gates (per build) — ALL PASS
| check | requirement | result |
|---|---|---|
| selftest | PASS | PASS (every build) |
| TERMINAL s42 x200 | == 194/200 | **194/200 every row** (leak-clean; the change is fins-deployed-only) |
| AERO s42 x60 --mppi (pristine copy) | == 44/60 | **44/60** (bit-matches `goldens/mc/aero_mppi_s42_d012_baseline.txt`) |
| MPPI single-run invariance (COVO_ON=0) | td_v 2.63 / lat 10.48 | **td_v 2.63 / lat 10.48 — bit-identical** → my edits are a true no-op at ρ=1 |

The COVO_ON=0 guarded path reproduces the shipped controller byte-for-byte (single-run bit-match +
pristine x60 = 44/60). Determinism is preserved.

### 3.2 CoVO ratio sweep — AERO_OFFSET s42 --mppi (all TERMINAL 194 = leak check, all selftest PASS)
Screen at **x30** (fleet CPU heavily contended); winning ratio confirmed at **x60**. The det-preserving
scale means ρ=1 IS the isotropic baseline. Data: `_covo_wt/covo_screen.csv`.

| ρ (σ_rad/σ_tan) | σ_rad | σ_tan | runs | AERO landed | rate | Δ vs baseline | off-pad | too-hard | td_v | lat |
|---|---|---|---|---|---|---|---|---|---|---|
| **1.0 baseline (iso)** | 1.50 | 1.50 | x30 | **22/30** | **73.3%** | — | 6 | 2 | 2.77 | 14.69 |
| 1.5 | 1.84 | 1.22 | x30 | 22/30 | 73.3% | **0** | 7 | 1 | 2.85 | 15.05 |
| 2.0 | 2.12 | 1.06 | x30 | 21/30 | 70.0% | **−1** | 7 | 2 | 2.81 | 17.25 |
| 3.0 | 2.60 | 0.87 | x30 | 19/30 | 63.3% | **−3** | 8 | 2 | 2.93 | 16.05 |
| **1.0 baseline (iso)** | 1.50 | 1.50 | **x60** | **44/60** | **73.3%** | — | 13 | 2 | 2.95 | 14.37 |
| **1.5 (best) CONFIRM** | 1.84 | 1.22 | **x60** | **44/60** | **73.3%** | **0** | **15** | 1 | 2.99 | 15.28 |

### 3.3 Reading
- **ρ=1.5 (mildest anisotropy) is exactly FLAT** — 22/30 at screen, **44/60 at confirm = baseline rate**.
- **ρ=2.0, 3.0 monotonically LOSE landings** (−1, −3) as anisotropy strengthens.
- The loss appears **as MORE off-pad crashes** (6→7→8 at x30; and ρ=1.5's x60 shifts 13→15 off-pad) and
  **worse lat mean** (14.7→17.3) — the *opposite* of the CoVO thesis. Reallocating variance onto the
  radial reach axis does **not** convert far-seed misses; and starving the tangential (cross-track)
  channel degrades the cross-range null the converging profile depends on.
- No ratio clears the +3 gate, so the `if it clears +3` follow-ups (s7 x60, determinism pair, ENTRY
  s42 x100 protection) are **moot** — nothing to adopt, ENTRY is untouched.

---

## 4. Verdict — **NULL (do not adopt).** Recorded with numbers.

**CoVO diagonal covariance in a radial/tangential frame does not move AERO** on our lateral-only HIER
MPPI: best case ρ=1.5 = 44/60 (dead flat vs the 44/60 baseline), and it is monotonically harmful with
stronger anisotropy (ρ=3.0 = −3). The M4 gate (AERO ≥90) is **not** advanced by this variant.

**Why the theory-optimal upgrade is inert here (diagnosis):** CoVO's `Σ*∝D^(−1/2)` win requires that
the binding constraint be *proposal support along the cost-smooth axis* — i.e. that better-directed
radial exploration would *discover* reaching trajectories the isotropic proposal misses. Ours does not:
1. The **warm-start already sits on the radial converging profile** (`converging_vdes` = the sqrt-decel
   reach recipe), so radial noise around it perturbs an already-radial-optimal mean — it re-samples the
   same reach envelope, not a new one. The off-pad misses are the **aero/thrust crossover dead-zone reach
   CEILING** (D-009's ~100–150 m residual, ~22 kPa), a *plant-authority* limit, not a sampling-support gap.
2. This is the **same wall** K 256→1024 hit (kprobe: 44/44/42, 0/14 off-pads converted) and isotropic
   OU-θ hit (MPPIVAR: −2/−3). Exploration *shaping* — more K, longer θ, or now anisotropic Σ — cannot
   manufacture reach the tilt-capped plant does not have in the dead zone.
3. Crushing σ_tan to feed σ_rad (det-preserving) actively **hurts** because the cross-track null is a
   real, tight cost direction (Q_VXYERR tracks it) — CoVO mis-identifies it as "sharp ⇒ needs no
   exploration", but at fixed budget the tangential channel still needs enough variance to correct wind
   cross-track, and starving it loses landings.

**Answer to the "anisotropic where isotropic failed?" question (mission dir 4):** NO. MPPIVAR falsified
isotropic OU-θ retuning; anisotropic reach-axis σ does **not** rescue it — ρ=1.5 flat, ρ≥2 worse. The
anisotropy hypothesis is falsified on the same evidence: our bottleneck is plant reach authority in the
crossover dead zone, which no proposal reshaping (isotropic or anisotropic) addresses. **AERO-90 needs a
capacity/authority lever the sampler cannot supply** (consistent with the fleet's kprobe/MPPIVAR nulls);
MPOPI iterate-per-replan (peer lane) attacks proposal support differently and is the remaining structural
candidate.

**No integration patch is proposed** (NULL). The `_covo_wt` implementation is complete, gated, and
determinism-safe; it is preserved as the decisive artifact behind the `COVO_ON` compile switch (default 0
= shipped controller, byte-identical). If a future regime ever makes reach a genuine proposal-support
problem (e.g. a higher-authority plant, or ASDS cross-range), the code is ready to re-probe.

---

## 5. Notes / follow-ups
- **Anisotropic-OU question — ANSWERED (null):** see §4. Anisotropic reach-axis σ does not help where
  isotropic OU-θ (MPPIVAR) didn't; ρ=1.5 flat, ρ≥2 harmful. Same falsification, same cause.
- **Magnitude sweep (a considered next step, NOT pursued):** these rows hold det(Σ) fixed to isolate
  *shape*. Research A4 said a larger reach-axis σ (lift the trace, not just the ratio) is worth a probe
  *once exploration is the bottleneck*. But this screen shows exploration shaping is inert on our reach
  ceiling (det-preserving ρ null; and ρ=3.0 with σ_rad=2.60 — a de-facto radial-trace lift — was the
  WORST row). A pure-magnitude σ_rad lift at fixed σ_tan is very unlikely to differ, and the failure
  mode (off-pad, not touchdown) is a plant-authority ceiling, not a σ gap. Not run — the det-preserving
  sweep already covers a σ_rad range of 1.22→2.60 with a monotone-null-to-harmful signal. Left as a
  one-line note for the CUDA/K≥1024 line if ever wanted.
- **Interaction with K:** research A4 hinted a σ probe "at K≥1024". This screen is K=256 (CPU). kprobe
  already showed K 256→1024 is flat (44/44/42), so a σ_rad×K cross is low-priority; but if the CUDA
  K≥1024 path is ever the harness, re-running ρ=1.5 there is cheap. Expectation: still null (the ceiling
  is authority, not samples).
