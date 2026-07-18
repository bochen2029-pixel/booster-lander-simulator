# P2 — Aerodynamic Coefficient Verification (audit fleet)

Agent P2 (opus-4.8-hcmwkotz). Read-only audit. All calcs in C (MSVC), no Python.
Harnesses: `runs/audit_P2/*.c` (aero_verify, cop_sign, cop_physics2, stability_sim,
quat_alpha, crossover, fin_cna). Literature cited inline.

## Bottom line

The AERO tables are **physically sound and should mostly be KEPT**. Two concrete
issues found: (1) a **units/frame inconsistency** (`xcp_frac × VEH_STAGE_LEN` while
CoM is total-L referenced) that shifts CoP ~2 m, and (2) the **transonic CoP bump is
slightly too large**, flipping the bare body to *stable* in M0.75–1.35, contradicting
the "bare body unstable" canon. D-005's CoP correction is otherwise **physically
justified** (the old 0.62 L was strongly stable, contradicting canon and reality).

## Question-by-question

### 1. CoP / static stability (D-005) — DIRECTION CORRECT; two refinements
- Empty/descent CoM = **12.27 m from base = 0.298 Lstage = 0.257 total-L** (bottom-heavy,
  engines dominate — correct for an empty F9 booster).
- Stability sign (base-first): **CoP aft/high-z of CoM (xcp>com) = STABLE** (CoP
  downstream of CoM). Triple-verified: `cop_sign.c`, forward-time integration
  `stability_sim.c`, and quaternion-exact `quat_alpha.c`. Independently reproduced by
  P1 and P3 (3 code paths agree).
- **The code is a HYBRID**: UNSTABLE at M<0.8 and M>1.4, STABLE only in the transonic
  bump M0.9–1.2 (CoP moves aft there and overshoots CoM). Static margins (near-empty):
  M0.3 −0.40 m, M0.7 −0.09 m, M0.9 +0.56 m, M1.05 +0.83 m, M1.2 +0.56 m, M1.5 −0.27 m,
  M2.0 −0.40 m (i.e. ±0.1–0.25 caliber — genuinely marginal).
- **Literature**: finless F9 booster is "inherently unstable during reentry"; grid
  fins (even stowed, at the top) provide the weathercock stability
  (spaceandscience.fr; alexcortiella.com F9-recovery). Allen-Perkins crossflow
  (`cop_physics2.c`): bare CoP for a base-first blunt cylinder sits between ~0.5 L
  (base loading negligible) and ~0.18 L (blunt base dominates); crosses into unstable
  when the flat leading base carries normal force ≳0.9× the barrel crossflow, which is
  plausible for a bluff flat base. So ~0.30 Lstage is defensible.
- **OLD 0.62 L**: SM ≈ +3.6…+4.1 calibers = strongly stable, ~15× restoring torque →
  flatly contradicts canon 5.4/6.3. D-005 was right to change it.

**Recommended table changes:**
- Fix the frame: either multiply `xcp_frac` by `VEH_LEN` and re-baseline, or (simpler)
  document that the fraction is of STAGE length and reconcile the "0.29 L" label.
- Reduce the transonic bump so it does **not** cross CoM: `0.03 → 0.01`
  (peak → 0.30 Lstage ≈ 12.36 m ≈ CoM, stays marginally unstable through transonic).
  OR lower baseline `0.29 → 0.267`. Keep the bump *direction* (aft is correct).
- Fix D-005 wording: "marginally unstable" is only true sub/supersonic; add the
  transonic-stable window.

### 2. Body CNα 2.0–2.5 /rad — CORRECT (linearized slope)
- Slender-body potential theory: **CNα(α→0) = 2.0 /rad exactly** referenced to base
  area (= Aref). Textbook (Allen-Perkins NACA 1048 / Jorgensen NASA TR R-474).
- Adding viscous crossflow (η=0.65, Cdc=1.2), the secant slope CN/α rises 2.2→5.2 /rad
  over α=1→15° (`aero_verify.c`). So **2.0–2.5 is a fair single-slope linearization for
  the <~8° divert regime**; it under-predicts normal force above ~10° (but that's above
  the side-load cap). KEEP.

### 3. CA 0.85→1.40→0.95 — REASONABLE (subsonic maybe slightly low)
- Base-first ⇒ base drag dominates. Flat-base subsonic base-pressure **Cp_base ≈ −0.6…−0.78**
  (measured, bluff-body eddy shedding) ⇒ CA_base ≈ 0.6–0.78; + forebody/skin ~0.1–0.2
  ⇒ **CA_sub ≈ 0.8–0.95**. Sim's 0.85 is at the low edge but OK.
- Transonic 1.40 peak — correct (wave drag on leading flat face + base-drag rise).
- **Optional**: nudge subsonic CA0 0.85 → 0.90. Negligible effect on lateral divert.

### 4. Transonic CoP bump direction — CORRECT (do not flip)
- Literature: CoP moves **AFT** transonically/supersonically (shock formation, AC→50%
  chord; Mach-tuck mechanism). Base-first, aft = higher z = more stable. Sim's `+0.03·exp`
  raises xcp toward the nose = correct stabilizing direction. Only the *amplitude* is
  too large (see Q1). The grid-fin transonic authority dip (0.55×) — see Q5.

### 5. Grid-fin CNα 3.0 /rad + fin_dip — dip EXCELLENT; CNα slightly high
- `fin_cna.c`, sources: Hiroshima & Tatsumi ICAS-2004 (MELCO tri-sonic WT); ARL
  GOVPUB-D101 (Simpson 1997 WT + FLUENT). Both reference CN to **body cross-section
  area**; fin lifting-area/body-area = 2.40.
- Converted to fin-area basis (what the sim uses): subsonic **~2.6 /rad**; spread across
  sources ~1.4–2.9 /rad. **Sim's 3.0 /rad is at/just above the high end** — defensible
  as clean-flow isolated-fin, but slightly optimistic. **Recommend 3.0 → 2.6** for
  mid-range grounding (won't change P5's "not authority-limited" or P4's ceiling much).
- **Transonic dip 0.55** (0.8<M<1.2): **spot-on**. ICAS measured trough = 0.64 @M0.8,
  0.55 @M1.2. Choked-cell/normal-shock mechanism confirmed everywhere.
- Nit: sim recovers to only 0.80 for M>2, but ICAS shows ~1.05 by M2.0 → sim is ~25%
  conservative supersonically (harmless; optionally raise M>2 dip 0.80 → 1.0).
- Fin area 2.4 m² / 1.2×2.0 m Ti panel — matches canon 5.4, realistic.

## Peer contributions folded in
- **P1** (me6gq2nq): independently reproduced the hybrid stability picture (3rd code
  path); confirmed the frame bug shifts xcp 2.07 m; confirmed fin-torque signs correct.
- **P3** (fgkflf16): initially had the stability labels inverted, then re-ran a clean
  weathervane test on `dynamics_deriv` and confirmed P2's picture exactly. Also found
  the 35% fin pitch→yaw cross-coupling and a gimbal rate-windup bug (separate lanes).
- **P4** (f1gvkjvl): consumed the "keep the tables" verdict; his divert ceiling stands
  on CNα~2.4. Concludes AERO_OFFSET's 3σ tail is beyond the aero+burn physics ceiling.
- **P5** (wewkahm9): proved control can deliver the commanded AoA (fins hold ≤25°), so
  side-load, not fin authority, is the binding cap.
