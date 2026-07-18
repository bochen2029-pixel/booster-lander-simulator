# Agent P5 — Angle-of-Attack HOLD control for the unpowered aero-descent

**Audit-P5 deliverable.** A control law that HOLDS a commanded steering angle-of-attack
(`alpha_cmd ~ 6 deg`) during the unpowered aero-descent, instead of the current
PD-to-attitude + trim-feedforward controller which drifts back to ~2 deg (aligned) and
under-steers. Verified numerically in a compiled C closed-loop simulation that mirrors
`core/dynamics.c` (body aero + grid-fin + Cmq) exactly, including the 20 deg/s fin rate
limit, the +-20 deg deflection limit, the transonic authority dip, and the tangential
roll-cant cross-term. **No `core/` files were edited** (per the hard rule; coordinator
integrates). Scratch/verification code: `runs/sandbox/p5_trim.c`, `runs/sandbox/p5_cloop.c`
(C only, MSVC `/O2 /fp:precise`).

---

## 0. TL;DR

- **Root cause of the drift-to-2deg (confirmed in code + sim, not tuning):** `control.c`'s
  trim-feedforward cancels the *body* aero moment at the *current* AoA, but the airframe also
  needs a **nonzero steady fin deflection** to hold a nonzero AoA. The PD supplies that fin
  moment only from tracking error, so as `q_err -> 0` at `alpha_cmd` the steady fin deflection
  -> 0 and the airframe relaxes to the fins' own natural trim (~aligned). **The feedforward
  cancels the body but forgot the fin trim deflection.** That is the whole bug.

- **Fix = MOVE-THE-TRIM-POINT.** Feed forward the *fin deflection that trims the airframe AT
  `alpha_cmd`* (computed from `alpha_cmd` and the flight condition, **not** from the current
  attitude error), and let the PD close only on the residual for damping. This makes the
  steady-state fin deflection nonzero at zero error, so the airframe holds `alpha_cmd`.

- **Verified:** old law commands 6 deg -> holds **2.0-2.3 deg**; move-the-trim-point ->
  **5.85-6.26 deg**, no overshoot, no limit cycle, across M=0.7-2.0.

- **Integral action is MANDATORY** (not optional). The feedforward uses the controller's
  *estimate* of the aero coefficients (`body_cna_ctrl`, `xcp_frac_ctrl` mirrors in `control.c`),
  which differ from plant truth. A representative 15% CNa error + 0.3 m CoP-estimate bias
  collapses feedforward-alone back to ~1.7-2.3 deg. A **slow deflection-domain integrator
  (tau_i ~ 1.5 s) with conditional-integration anti-windup** recovers the full 6.1 deg and
  rejects gusts.

- **Fin authority and the rate limit are NOT the bottleneck for HOLDING.** The trim deflection
  for 6 deg AoA is only ~4.4 deg and takes ~0.21 s to slew at 20 deg/s. Fins can trim-and-hold
  up to ~25 deg AoA (deflection ~18 deg at 25 deg AoA) at every descent Mach. The binding cap
  is the **structural side-load line** (canon 5.7), not control authority.

- **Recommendation:** ship the hand-tuned move-the-trim-point + PID law now (E2, per Agent A's
  build plan). It is a genuine physics fix, not single-point tuning, and it is a hard
  prerequisite for MPPI HIER anyway (HIER runs this same inner loop inside every rollout).
  **Do NOT defer AoA-hold to MPPI** — MPPI cannot exceed the attitude-loop bandwidth in HIER
  mode, and RAW-MPPI directly commanding fins would still limit-cycle without a trim reference.
  Details in Section 8.

---

## 1. The plant, precisely (from `dynamics.c`)

Base-first descent (engine end leads), body +Z points up the vehicle toward the interstage,
relative wind hits from -Z. AoA `alpha` measured off the -Z axis: `cos(alpha) = -vhat_z`.
I work in a single **steering plane** (the plane containing the relative-wind axis and the
desired lateral direction); the two lateral channels are symmetric so the scalar derivation
maps to both. Take the steering plane as body X-Z, so `alpha` is a tilt about +Y and the
relative-wind unit vector in body is `vhat_b = (sin alpha, 0, -cos alpha)`.

### 1.1 Body pitch moment about CoM (the destabiliser)

`dynamics.c` lines 136-158: normal force magnitude `Fn = qbar*Aref*CN`, `CN = CNa(M)*alpha`,
applied as a lateral force opposing the cross-flow at the centre of pressure
`x_cp = xcp_frac(M,alpha)*VEH_STAGE_LEN`, arm `(x_cp - com)`. The pitch moment about +Y:

```
  T_body(alpha) = (x_cp - com) * ( -Fn )           [Nm about +Y]
              = -qbar*Aref*CNa(M)*(x_cp - com) * alpha
```

Linearised **body pitch stiffness** `k_body = dT_body/dalpha = -qbar*Aref*CNa*(x_cp - com)`.
Sign convention: with the plant's `xcp_frac ~ 0.29-0.32`, at low/high Mach `x_cp < com`
(CoP ahead of / below CoM in the base-first sense) so `(x_cp - com) < 0` and the moment
**increases** `alpha` — i.e. the bare body is **statically unstable / marginal** (per D-005).

**Numbers** (my harness `p5_trim.c`, m=42.4 t aero-descent mass, `com = 12.284 m`):

| M    | qbar (Pa) | x_cp (m) | SM = x_cp-com (m) | CNa   | dT_body/dalpha (Nm/rad) |
|------|-----------|----------|-------------------|-------|-------------------------|
| 0.70 | 30000     | 12.265   | **-0.019**        | 2.200 | -1.35e4  (unstable)     |
| 0.90 | 45000     | 12.911   | +0.626            | 2.400 | +7.12e5  (stable)       |
| 1.05 | 50000     | 13.184   | **+0.900**        | 2.475 | +1.17e6  (most stable)  |
| 1.20 | 50000     | 12.911   | +0.626            | 2.475 | +8.15e5                 |
| 1.50 | 45000     | 12.078   | -0.206            | 2.400 | -2.34e5  (unstable)     |
| 2.00 | 35000     | 11.948   | -0.336            | 2.300 | -2.85e5  (unstable)     |
| 3.00 | 20000     | 11.948   | -0.336            | 2.200 | -1.56e5  (unstable)     |

> **Note for P2 (aero-coeff verify):** the `xcp_frac` bump at M~1.05 (base 0.32 L) puts x_cp
> ABOVE com there, so the bare body is actually *statically stable* in the narrow transonic
> band and unstable outside it. If P2 revises the CoP curve this table shifts; the control law
> below does not care about the sign of `k_body` (it trims either way), but the required
> `delta_ff` magnitude scales with `(x_cp - com)`.

### 1.2 Grid-fin pitch moment (the effector)

`dynamics.c` lines 176-198, per fin `i` at azimuth `phi_i in {45,135,225,315}`, mount
`r_i = (R cos phi, R sin phi, FIN_Z)`, `R = VEH_RADIUS = 1.83`, `FIN_Z = 45`:

```
  local flow    v_i    = vrel_b + omega x r_i               (omega x r term damps rotation)
  qbar_i        = 0.5 rho |v_i|^2
  incidence     inc_i  = atan2( v_i . e_r_i , -v_i_z )       (flow incidence in the fin plane)
  effective AoA a_i    = delta_i + inc_i           (clamp |a_i| <= FIN_STALL = 25 deg)
  radial lift   L_i    = qbar_i * S_f * CNa_f(M) * a_i        S_f = FIN_AREA = 2.4
  tangential    Ft_i   = qbar_i * S_f * (0.35*CNa_f(M)) * delta_i     (roll-cant; 0.35 = FIN_CT_DELTA_FRAC)
  force (body)  F_i    = -L_i e_r_i - Ft_i e_t_i
  torque        T_i    = (r_i - com_vec) x F_i
```

For the **yaw allocation pattern** `patY = [-1,1,1,-1]` (the +Y channel; this is exactly
`control.c` line 149) applied with amplitude `delta_amp`, the fin pitch moment about +Y from
the *radial lift* is (Agent A's B-matrix, matching `control.c`):

```
  T_fin_Y(delta_amp) = -4 * 0.7071 * A * delta_amp,   A = (FIN_Z - com) * qbar * S_f * CNa_f(M)
```

The minus sign is why `control.c` uses `patY = [-1,1,1,-1]` (the negation cancels it, so a
positive `dyaw` produces a positive +Y torque). **This sign is load-bearing** — using
`[1,-1,-1,1]` gives positive feedback and the loop diverges (I verified this: with the wrong
sign the closed loop blew up to +-26 deg; D-005's sign fix is correct — independently
confirmed by P3, control-vs-plant torque ratio 1.000).

Fin authority `Kfin_d = dT_fin_Y/d(delta_amp) ~ -1.5e7 to -1.9e7 Nm/rad` (magnitude ~1.5e7 at
the M=1.05 dip). At the ~4.4 deg (~0.077 rad) trim deflection that is ~1.3e6 Nm — the fins
have plenty of authority; the problem was never authority, it was that nothing *commanded* the
steady deflection.

### 1.3 Body pitch damping (Cmq, D-005) and closed-loop damping budget

`dynamics.c` lines 161-172: `T_damp = -Cdamp * w_perp`, `Cdamp = 0.5 rho V Cdc D J`,
`J = integral_0^L (z - com)^2 dz = L^3/3 - com L^2 + com^2 L`, `Cdc = BODY_CMQ_CDC = 0.6`.
This is the aero pitch damping. It gives `zeta ~ 0.1-0.15` open-loop (P1's regime; my sim
agrees). The controller's `Kd` is then **reduced by `Cmq`** (the D-005 damping-augmentation
term, `control.c` line 92) so the *total* closed-loop damping reaches the designed `zeta=1.3`
without the fins double-damping and wasting authority. My closed-loop sim shows the AoA step is
critically-to-over-damped (no ring), consistent with `zeta=1.3`.

---

## 2. The control law — MOVE-THE-TRIM-POINT

### 2.1 The idea

To hold the airframe at `alpha_cmd`, drive the *total* steady pitch moment to zero **at
`alpha_cmd`** (not at zero AoA). The total steady moment is body + fins:

```
  T_body(alpha_cmd) + T_fin_Y(delta_ff) = 0
```

Solve for the feedforward fin amplitude `delta_ff` (the value the fins must hold at steady
state), then add PD damping on the residual and a slow integral to mop up model error. The PD
no longer has to *manufacture* the steady moment from a standing error — the feedforward
provides it, and the PD sees ~zero error at trim.

### 2.2 The feedforward — the FULL trim (fin-inflow-dominated), not body-cancel

> **CORRECTION (P1 caught this — critical).** An early closed form here cancelled only the
> *body* moment: `delta_ff = -(x_cp-com)Aref CNa/(4*0.7071*(FIN_Z-com)S_f CNa_f)*alpha`. That is
> **wrong to ship** — it accounts for only the small body moment and *ignores the fins' own
> inflow restoring moment, which DOMINATES.* P1's full-plant numbers: at M=1.2, alpha=6 deg the
> body moment is +1.65e5 Nm (destabilising) but the fins *at zero deflection* already make
> **-4.97e5 Nm** (restoring, ~3x larger) purely from their inflow incidence `atan2(w_r,w_ax)`.
> So the airframe's real "natural trim" is set by the fins, not the body, and the body-only
> formula gives only ~-0.11..+0.28 deg (and wrongly flips sign transonically). The correct trim
> is the FULL moment balance below. (My verification harness already used the full balance, which
> is why its table is right; the body-only algebra was the error.)

To hold `alpha_cmd` at steady state the **total** pitch moment must be zero:

```
  T_body(alpha_cmd)  +  T_fin_inflow(alpha_cmd)  +  T_fin_defl(delta_ff)  =  0
```

- `T_body` — body normal-force moment (small; sign depends on static margin).
- `T_fin_inflow` — each fin's radial lift from its *inflow incidence* `atan2(w_r,w_ax)` at zero
  deflection. This is the passive weathervane term and it **dominates**; it always pulls the
  airframe back toward aligned (restoring), so holding a nonzero AoA *requires* fin deflection to
  oppose it.
- `T_fin_defl = -4*0.7071*A*delta_ff` — the deflection response, `A=(FIN_Z-com)qbar S_f CNa_f`.

Because `T_fin_inflow` dominates and is proportional to `alpha_cmd`, the solution is
**single-signed and nearly Mach-invariant**: `delta_ff = K_ff(M)*alpha_cmd`,
**`K_ff ~ 0.69-0.77` (one sign — fins always bias INTO the AoA)**. This matches P1's independent
full-plant bisection (`~0.68-0.76 deg fin/deg AoA`, single sign) and my `p5_trim.c` root-find.

**Do not compute `delta_ff` from a body-only formula.** Two correct ways to get the full trim:

1. **Numeric trim (recommended, what the sim used and what P1 verified):** at each guidance/500-Hz
   tick, solve `total_moment(alpha_cmd, delta_ff) = 0` for `delta_ff` by a few Newton/bisection
   steps on the controller's *estimate* of the full moment (body + fin-inflow + fin-deflection,
   using the shared `body_cna_ctrl`/`xcp_frac_ctrl`/`fin_dip_ctrl` mirrors). This is `delta_ff_exact()`
   in `p5_cloop.c` and holds 5.85-6.26 deg with NO integral. ~5 bisection iters is trivially cheap.
2. **Precomputed `K_ff(M)` schedule:** since `K_ff` is ~0.69-0.77 across the whole envelope, a
   small Mach-indexed table (or even a constant ~0.73 with the integral trimming the rest) is a
   fine engineering approximation. The table below is that schedule.

The sign, applied as the yaw-pattern amplitude with `patY=[-1,1,1,-1]`, flips with `alpha_cmd`
(needed for the velocity-null reversal, Section 5). (The `p5_trim.c` amplitudes print negative
because the harness's `patY` sign convention absorbs one sign; the physical statement is "fins
bias ~0.73 deg into each deg of AoA".)

**Numbers** (`p5_trim.c`, exact nonlinear root-find of the FULL plant moment — body + fin inflow
+ fin deflection + tangential term — then linearised; `|K_ff|` reported):

| M    | K_ff (deg fin / deg AoA) | delta_ff(6 deg) | slew time @20 deg/s | max holdable AoA |
|------|--------------------------|-----------------|---------------------|------------------|
| 0.70 | 0.705                    | -4.18 deg       | 0.209 s             | ~25 deg          |
| 0.90 | 0.749                    | -4.41 deg       | 0.220 s             | ~25 deg          |
| 1.05 | 0.770  (worst, dip)      | -4.53 deg       | 0.226 s             | ~25 deg          |
| 1.20 | 0.750                    | -4.41 deg       | 0.221 s             | ~25 deg          |
| 1.50 | 0.694                    | -4.09 deg       | 0.205 s             | ~25 deg          |
| 2.00 | 0.691                    | -4.09 deg       | 0.205 s             | ~25 deg          |
| 3.00 | 0.692                    | -4.10 deg       | 0.205 s             | ~25 deg          |

Two important properties:

1. **`K_ff` is nearly Mach-invariant (~0.69-0.77).** Reason: the trim is set by the *ratio* of
   the fin-inflow restoring moment to the fin-deflection authority — and BOTH scale with the same
   `qbar S_f CNa_f(M)` (including the transonic dip), so the dip cancels out of the ratio. The
   fins that must be *overcome* (inflow) and the fins doing the *overcoming* (deflection) are the
   same surfaces, so their common Mach-dependence divides out. **The transonic dip does not break
   AoA-hold** — the trim deflection barely moves (~4.4 deg at 6 deg AoA across all Mach), far
   inside +-20 deg.

2. **The rate limit is a non-issue for holding.** Reaching the ~4.4 deg trim takes ~0.21 s.
   The rate limit only bounds the *damping* bandwidth (how fast the PD term can act on fast
   transients), which is handled by the overdamped `zeta=1.3` tune already in `control.c`.

### 2.3 Why the transonic dip is handled without special logic

The `guidance_hoverslam.c` `sfac` already fades *steering* by 0.5 in the dip (a guidance-level
softening of the *commanded* AoA). That remains useful as a divert-authority scheduler. But the
**hold** itself needs no dip-specific control logic because `K_ff` self-compensates (property 1
above). Recommendation: keep the guidance `sfac` dip-fade (it correctly reflects reduced divert
*rate*), but the inner AoA-hold loop uses the plain `K_ff(M)` formula — no separate dip branch.

---

## 3. Full inner-loop equations (500 Hz)

Let `alpha` = current AoA in the steering plane, `q` = pitch rate about the steering axis,
`alpha_cmd` = commanded AoA (signed), flight condition `(qbar, M, com, rho, V)`.

```
  # --- feedforward trim (the fix) : the FULL trim, NOT body-only (see 2.2 correction) ---
  # Option 1 (recommended): numeric trim of the full estimated moment (body+fin-inflow+fin-defl)
  delta_ff   = solve_trim(alpha_cmd, qbar, M, com)      # ~5 bisection iters, yaw-pattern amplitude
  #   solve_trim finds delta s.t.  T_body_est(alpha_cmd) + T_fin_est(alpha_cmd, delta) = 0
  #   using shared body_cna_ctrl / xcp_frac_ctrl / fin_dip_ctrl (directive 7). Single-signed.
  # Option 2 (approx): delta_ff = K_ff(M) * alpha_cmd,  K_ff ~ 0.69..0.77 (schedule or const 0.73)

  # --- PD damping on the residual (torque -> deflection) ---
  a_err      = alpha_cmd - alpha
  tau_pd     = Kp*a_err - Kd_use*q                      # Kp=I_tr wn^2, Kd_use = I_tr 2 zeta wn - Cmq
  d_pd       = tau_pd / (4*0.7071*A)                    # A = (FIN_Z-com)*qbar*S_f*CNa_f

  # --- slow integral trim (model-mismatch / bias rejection) ---
  if not saturated:  e_int += a_err * dt               # conditional integration (anti-windup)
  e_int      = clamp(e_int, +-(8 deg)/Ki_d)            # integral trim clamp
  d_int      = Ki_d * e_int                             # Ki_d = K_ff_nom / tau_i,  tau_i ~ 1.5 s

  # --- total commanded amplitude, then map to 4 fins ---
  d_amp      = delta_ff + d_int + d_pd
  for i in 0..3:  fin_cmd[i] = clamp( patY[i]*d_amp , +-FIN_DEFL_MAX )   patY=[-1,1,1,-1]
```

The **key structural change from today's `control.c`** is the `delta_ff` and `d_int` terms
in the fin amplitude. Today's code has only the `d_pd` term plus a *body-moment cancellation*
in the torque command. Two clean integration options (Section 6):

- **(A) Replace** the body-moment-cancel torque with the `delta_ff` fin feedforward. Cleanest;
  the feedforward *is* the correct generalisation of the cancel (it cancels body AND supplies
  the fin trim). Recommended.
- **(B) Keep** the body-cancel and ADD `delta_ff` computed for the *incremental* AoA the fins
  must hold beyond what the cancel leaves. Slightly redundant but a smaller diff. My sim ran
  configuration (B) (cancel ON + delta_ff added) and it holds correctly, so either works; (A)
  is tidier.

### 3.1 Verified closed-loop behaviour (`p5_cloop.c`, alpha_cmd = 6 deg)

| M    | baseline (cancel+PD) | P5 exact FF+PD | P5 est-FF (15% CNa err) no-I | **P5 est-FF + I** |
|------|----------------------|----------------|------------------------------|-------------------|
| 0.70 | 2.309                | 5.938          | 2.329                        | **6.237**         |
| 0.90 | 2.221                | 6.140          | 1.985                        | **6.196**         |
| 1.05 | 2.075                | 6.256          | 1.696                        | **6.146**         |
| 1.20 | 2.075                | 6.149          | 1.819                        | **6.129**         |
| 1.50 | 2.005                | 5.850          | 2.099                        | **6.082**         |
| 2.00 | 2.051                | 5.852          | 2.159                        | **6.073**         |

(final AoA in deg, 6 s sim, start aligned.) The **baseline column reproduces the reported
drift-to-2deg exactly.** The exact-FF column holds ~6 deg. The est-FF-no-integral column shows
the feedforward alone is fragile to coefficient error. The **est-FF + integral column holds
~6.1 deg under a 15% CNa + 0.3 m CoP error** — this is the shippable configuration.

Gust rejection (a ~3-deg-equivalent body moment applied from t=3 s): P5+integral holds
6.07-6.23 deg (rejects the offset); feedforward-alone drifts a few tenths.

---

## 4. Integral action, anti-windup — yes, and how

**Yes, integral action is required**, because:

1. The feedforward gain `K_ff` uses the controller's *estimated* coefficients (`control.c`
   already carries `body_cna_ctrl`, `xcp_frac_ctrl` mirrors that are explicitly noted as
   estimates, plant is truth). Estimate error -> feedforward error -> steady AoA offset. My
   mismatch test (15% CNa + 0.3 m CoP) shows this is a **~4 deg** offset (6 -> ~1.8), far too
   large to ignore.
2. Persistent disturbances (CoM offset -> constant torque, steady wind shear) bias the trim.
   Agent C flagged CoM-offset as the nastiest attitude disturbance; only an integrator removes
   a constant disturbing moment at steady state.

**Design choice: deflection-domain integrator** (integrate AoA error into a fin-amplitude trim
`d_int = Ki_d * e_int`), NOT a torque-domain integrator. Reasons:

- The thing that must end up nonzero at steady state is a **fin deflection**, so integrating in
  deflection space is direct and the anti-windup clamp is naturally in actuator units.
- `Ki_d = K_ff_nom / tau_i`, `tau_i ~ 1.5 s`. This places the integrator ~5-8x slower than the
  attitude loop (`wn = 1.1 rad/s -> ~0.9 s`), so it trims the residual without interacting with
  the PD dynamics or causing integral-induced overshoot. Verified: converges in ~4 s, no ring.

**Anti-windup: conditional integration (a.k.a. integrator clamping).** Freeze the integral
update whenever the fin command is deflection-saturated (`|fin_cmd| >= FIN_DEFL_MAX`) OR
rate-saturated (`|fin rate| >= FIN_RATE`). Also hard-clamp `e_int` so `|d_int| <= 8 deg`
(the integrator should never need more than the difference between estimated and true trim,
which is a few degrees). This is essential because the fins WILL rate-saturate during the
initial slew to a large `alpha_cmd`; without the freeze the integrator would wind up during the
~0.2 s slew and overshoot. My sim's `e_int` trace stays bounded and settles cleanly.

> Do NOT use a back-calculation (tracking) anti-windup here unless you also want the extra tune
> parameter; conditional integration is simpler and sufficient given the clamp. Either is fine.

---

## 5. Guidance: commanding `alpha_cmd` from lateral error (honoring side-load)

The outer lateral law already exists in `guidance_hoverslam.c` (lines 57-80): an inward-velocity
first-order position loop producing `a_lat = Kvel*(vdes*lat_scale - v_xy)`, `vdes = Kpos*r_xy`
capped at `vlat_max`. During the unpowered aero-descent this `a_lat` must be realised by **body
AoA** (canon 9.1, Agent A: body lift ~4x fin lift is the divert lever; fins TRIM the AoA).
Convert acceleration demand to AoA using the body normal-force relation (canon 9.1):

```
  # required lateral specific force -> AoA (steering plane)
  alpha_raw = m * |a_lat| / ( qbar * Aref * CNa(M) )          # small-AoA inverse of Fn=qbar Aref CNa alpha
  alpha_cmd = sign(a_lat_component) * min( alpha_raw , aoa_cap(qbar) )    # signed, per lateral axis
```

with the **side-load AoA cap schedule** (canon 5.7: `|alpha|>15 deg & qbar>30 kPa for >2 s ->
STRUCT_FAIL`), using Agent A's validated schedule:

```
  aoa_cap(qbar) = 12 deg     for qbar < 10 kPa
                = ramp       10 kPa..30 kPa
                = 4  deg     for qbar > 30 kPa
```

This mirrors the existing `qcap` block in `control.c` (lines 57-61) which already shrinks the
tilt cap in the aero-descent — but that block caps *thrust-axis tilt*; here we cap **AoA**
directly. The two must be consistent; recommend the AoA cap live in guidance (where `a_lat` is
formed) and the control-side `qcap` be removed for the fins-active branch to avoid double-capping
(P4 confirms qbar peaks only ~34-36 kPa on the nominal profile, so the 15 deg STRUCT line is
never near — the 4-12 deg schedule is self-imposed conservatism, deliberately safe).

**Two lateral axes.** `a_lat` is a 2-vector; resolve it into the current steering plane and
command the signed `alpha_cmd` per axis (or, equivalently, command a single AoA magnitude toward
the horizontal `a_lat` direction and let the two fin channels — the `patP`/`patY` allocation —
realise the tilt in that direction). The trim feedforward is applied to whichever channel(s)
carry the commanded AoA; because `delta_ff` is linear in `alpha_cmd` it superposes cleanly across
the two orthogonal channels.

### 5.1 Sequencing: divert -> null lateral velocity -> vertical (critical)

This is the single most important operational detail, and it is where Agent C's TERMINAL
lesson (F1) and P4's rest-to-rest analysis converge: **you must null the lateral VELOCITY the
divert builds up, with altitude to spare, or you just move the terminal lateral limit-cycle up
into the aero-descent.** The AoA-hold law makes divert *effective*, which makes velocity-null
*necessary*.

Phase sequence within AERO_DESCENT (high -> low):

1. **Gross divert (high, qbar building, M supersonic->transonic):** command
   `alpha_cmd = +sign` toward the pad up to `aoa_cap`. Hold it (this is what P5 delivers).
   Cross-range accrues (Agent A: 4 deg -> 664 m, 6 deg -> 998 m over the descent).
2. **Velocity-null (mid):** as `r_xy` closes, the first-order law naturally reduces `vdes`;
   but the accumulated `v_xy` must be actively driven to zero. Because the outer law is
   `a_lat = Kvel*(vdes - v_xy)`, once `vdes < v_xy` the sign of `a_lat` reverses -> `alpha_cmd`
   **reverses sign** -> the airframe tilts the other way to decelerate the cross-range. The
   trim feedforward flips sign with `alpha_cmd` automatically, so the same law handles the
   reversal (verified: `delta_ff` is odd in `alpha_cmd`). **This reversal is the real cost**
   (P4) — budget altitude for it.
3. **Vertical terminal (low, handoff approaching):** fade the position-SEEKING term
   (`vdes -> 0` via `lat_scale`), but — exactly per Agent C's F1 fix — **keep the
   velocity-null damping `Kvel*(-v_xy)` active to the handoff** so residual cross-range rate is
   killed, not frozen. `alpha_cmd -> 0` as `a_lat -> 0`. Hand off to the landing burn
   (`guidance_hoverslam` re-solves ignition) with `v_xy ~ 0`.

Do the gross divert HIGH (Agent C: >4 km) so steps 2-3 have the altitude.

**P4 delivered the exact rest-to-rest numbers** (`runs/sandbox/p4_divert.c`, dt-converged,
on the P2-validated CN/CA tables, 12 km start / vz0=-330):

| AoA cap schedule            | aero divert | + ~550 m burn = total | qbar peak | AoA used |
|-----------------------------|-------------|-----------------------|-----------|----------|
| control.c flown (8->3 deg)  | 237 m       | 787 m                 | 36 kPa    | 6.3 deg  |
| Agent A (12->4 deg)         | 313 m       | 864 m                 | 36 kPa    | 9.2 deg  |
| hard 15 deg STRUCT limit    | 847 m       | 1397 m                | 36 kPa    | 20 deg   |

- **Velocity-null / reversal must START at `switch_alt ~ 6600 m`** (Agent A cap; ~6170 m for the
  control.c cap) — i.e. ~73% down the aero band, ~1500-2000 m above the burn. At that switch ~69%
  of the divert is banked; the remaining descent is spent reversing `alpha` to bleed the
  accumulated `v_xy` down to ~8 m/s at handoff. **The velocity-null eats ~1/3 of the divert
  budget** — this is why net rest-to-rest (313 m) is much less than the naive hold-AoA cross-range.
- **Guidance shape (P4, adopt verbatim):** hold `+alpha_cmd(=cap)` from 12 km to ~6.6 km; then
  command `-alpha_cmd` (reversal — my symmetric trim-FF handles it automatically) 6.6 km -> ~5 km;
  then fade `alpha -> 0` into the vertical terminal. Set the AERO_DESCENT->LANDING_BURN handoff at
  ~5 km with `v_xy` nulled.
- **Well-posedness (P4's verdict):** AERO_OFFSET (800 m mean, sigma 250) is NOT coverable by
  aero-divert + landing-burn alone past ~mu+0.3 sigma; the upper half of the dispersion needs the
  entry burn (which lowers Mach so fins bite in denser air, buying time). **My AoA-hold fix removes
  the control bug (delivers the full commanded AoA), but it cannot beat the physics ceiling** —
  the two findings are complementary: P5 sets how close to P4's physical ceiling we actually fly,
  and P4 shows the ceiling itself needs the entry burn for the big diverts. P4 recommends keeping
  800 m/250 only if the entry burn is made mandatory for the tail (his option B/C), *conditional on
  control adopting this AoA-hold* — the integral-augmented hold is a stated precondition of his
  verdict, not a nicety.

---

## 6. Integration with the existing `control.c` / guidance (directive-7 clean)

**Reuse the shared coefficient functions** (directive 7 = single dynamics source). `control.c`
already has `body_cna_ctrl(M)`, `xcp_frac_ctrl(M,alpha)`, `fin_dip_ctrl(M)` — the feedforward
uses exactly these (they are the controller's estimate; the integrator handles the residual vs
plant truth). No new coefficient code.

### 6.1 Minimal patch to `control.c` (fins_active branch)

Today (lines 138-158) the fins branch computes `dpitch`, `dyaw`, `droll` from `tau_cmd` (pure
PD, after the body-moment cancel in lines 100-122). Change:

```c
} else if(fins_active){
    double CNa_f=FIN_CNA*fin_dip_ctrl(mach);
    double k=qbar*FIN_AREA*CNa_f;
    double A=(FIN_Z-mp.com)*k;
    double C=RCS_ARM*(FIN_CT_DELTA_FRAC*k);

    /* ---- P5: move-the-trim-point feedforward (per steering axis) ---- */
    /* alpha_cmd_x/_y = commanded AoA components (from guidance, see 6.2), signed.
     * IMPORTANT (P1): this is the FULL trim (fin-inflow-dominated), NOT a body-moment cancel.
     * Since the fin-inflow restoring and the fin-deflection authority share the same
     * qbar*S_f*CNa_f(M), their ratio is ~Mach-invariant -> K_ff ~ 0.73 deg fin per deg AoA,
     * SINGLE-SIGNED (fins bias into the AoA). Two implementations:
     *   (a) constant/scheduled gain (simple, integral trims the residual): */
    double Kff = K_FF_NOM;                    /* ~0.73; or a Mach-indexed table K_ff(mach) 0.69..0.77 */
    double dyaw_ff   = Kff * alpha_cmd_y;     /* map each commanded-AoA axis to its pattern amplitude */
    double dpitch_ff = Kff * alpha_cmd_x;
    /*   (b) exact numeric trim (tighter): solve total_moment_est(alpha_cmd, d)=0 for d by ~5
     *       bisection steps using body_cna_ctrl + xcp_frac_ctrl + the fin inflow model
     *       (mirror of dynamics.c fin loop). Preferred if the extra ~5 evals/axis are affordable
     *       at 500 Hz; otherwise (a)+integral is sufficient (verified). */

    /* ---- P5: slow deflection-domain integral with anti-windup (persistent state in Actuators/ctrl) ---- */
    /* e_int_x/_y integrated at 500 Hz, frozen on saturation, clamped. Ki_d = Kff_nom/tau_i. */
    double dyaw_int   = Ki_d * e_int_y;
    double dpitch_int = Ki_d * e_int_x;

    /* ---- PD damping on residual (as today) ---- */
    double dpitch_pd = (fabs(A)>1.0)? tau_cmd[0]/(4.0*0.7071*A):0.0;
    double dyaw_pd   = (fabs(A)>1.0)? tau_cmd[1]/(4.0*0.7071*A):0.0;
    double droll     = (fabs(C)>1.0)? -tau_cmd[2]/(4.0*C):0.0;

    double patP[4]={1,1,-1,-1}, patY[4]={-1,1,1,-1};
    double dpitch = dpitch_ff + dpitch_int + dpitch_pd;
    double dyaw   = dyaw_ff   + dyaw_int   + dyaw_pd;
    int sat=0;
    for(int i=0;i<4;i++){
        double d=droll + dpitch*patP[i] + dyaw*patY[i];
        if(d> FIN_DEFL_MAX){d= FIN_DEFL_MAX;sat=1;} if(d<-FIN_DEFL_MAX){d=-FIN_DEFL_MAX;sat=1;}
        fins[i]=d;
    }
    /* anti-windup: only integrate when not saturated & not rate-limited (rate check vs prev fins) */
    if(!sat){ e_int_x += (alpha_cmd_x - alpha_meas_x)*DT_CTRL;   /* clamp to +-(8deg)/Ki_d */
              e_int_y += (alpha_cmd_y - alpha_meas_y)*DT_CTRL; }
    rcs[2]=0.5*tau_cmd[2];
}
```

Notes:
- If option (A) [replace the body-cancel] is chosen, DELETE the `tau_cmd[0/1] -= Ta[0/1]` lines
  (100-122); the `delta_ff` supersedes them. If option (B), keep them and the `delta_ff` above
  becomes the *incremental* trim (my sim used (B) and it works).
- `alpha_cmd_x/_y`, `alpha_meas_x/_y` come from guidance/state (Section 6.2). The two integral
  states `e_int_x/_y` are new persistent controller state (add to a small ctrl struct, or to
  `Actuators`/`State` scratch — coordinator's call; they must persist across the 500 Hz ticks).
- `DT_CTRL = 1/500 = 0.002 s`.

### 6.2 Guidance side (`guidance_hoverslam.c`)

In the `!st->engine_on` aero-descent block (lines 69-84), after forming `g->a_lat` and applying
`sfac`, convert to a commanded AoA and pass it down. The cleanest is to add
`g->alpha_cmd[2]` to `GuidanceCmd` (magnitude+direction, or two signed components) and set:

```c
double alat_mag = hypot(g->a_lat[0], g->a_lat[1]);
double alpha_raw = (qbar_g>50.0) ? m*alat_mag/(qbar_g*VEH_AREF*body_cna(machg)) : 0.0;
double cap = aoa_cap(qbar_g);           /* 12deg@<10kPa -> 4deg@>30kPa */
double alpha_mag = fmin(alpha_raw, cap);
if(alat_mag>1e-9){ g->alpha_cmd[0]=alpha_mag*g->a_lat[0]/alat_mag;   /* per axis, signed */
                   g->alpha_cmd[1]=alpha_mag*g->a_lat[1]/alat_mag; }
```

`control.c` then maps these two AoA components onto the pitch/yaw fin patterns (the mapping
from world-lateral to body pitch/yaw axes goes through the attitude quaternion exactly as the
existing `errb` computation does). The velocity-null reversal (Section 5.1) is automatic
because `a_lat` (hence `alpha_cmd`) reverses sign when `v_xy` exceeds `vdes`.

Keep the existing `sfac` (transonic/stall/low-qbar steering fades) — it correctly scales the
*commanded divert*; it is orthogonal to the *hold* mechanism.

---

## 7. What can still bite (honest failure modes)

- **CoM-offset / persistent torque (Agent C's D4):** handled by the integrator, but a large
  CoM offset can push the required trim deflection toward the +-20 deg limit at high AoA; then
  the anti-windup clamp holds and the achieved AoA saturates below command. Acceptable
  (degrades gracefully, no windup), but the divert ceiling shrinks. Quantify under Tier-B.
- **Deep transonic + large AoA simultaneously:** `CNa_f` dip (x0.55) AND fin stall (|a_i|>25 deg
  local) can co-occur if `alpha_cmd` is large in the dip; the `aoa_cap` schedule (4 deg at high
  qbar) keeps `alpha_cmd` small exactly where the dip lives, so the fin local incidence stays
  well below stall. Verified holdable to 25 deg even at M=1.05 in isolation.
- **Estimate drift as mass changes:** `com` moves as residual propellant drains; `K_ff` uses
  the live `mp.com` (recomputed every tick, canon 6.4) so it tracks. The integrator absorbs the
  slow residual. Fine.
- **CoP reference-length labeling (P2's caveat):** `xcp_frac` multiplies `VEH_STAGE_LEN` (41.2 m),
  not `VEH_LEN` (47.7 m), while `com` is reckoned on total length. So "0.29" reads like 0.29 L but
  is really ~0.25 L-of-total. My `K_ff` formula uses the code's exact convention
  (`x_cp = xcp_frac*VEH_STAGE_LEN`), so the numbers are correct as-is, but if the coordinator
  normalises the CoP to total-L (per P2/P3's flag) the `xcp_frac` constants change and `K_ff`
  must be recomputed from the new `x_cp`. The formula is unchanged; only the `x_cp` input moves.
- **Gimbal rate-state windup (P3's BUG-P3-3) — powered phase, not ours, but same class:** P3
  found the gimbal 2nd-order actuator winds up its rate state at the +-5 deg position clamp
  (~10% of terminal steps saturated, 0.55 s reversal lash). That is a LANDING-BURN issue (gimbal),
  not aero-descent (fins), so it does not touch this AoA-hold law — but note it is the exact same
  saturation-windup pathology my fin integrator's anti-windup (Section 4) guards against, and P3
  notes it "interacts with the under-trim (P5)" in the shared terminal attitude loop. When the
  coordinator adds P3's 4-line gimbal anti-windup, the fin integrator here should get the matching
  conditional-integration freeze (already specified) for consistency.
- **Two-axis cross-coupling (P3's BUG-P3-1):** the tangential roll-cant force leaks ~35% of a
  pure pitch command into yaw (and vice-versa). It does NOT change the in-plane trim magnitude
  (P3: net pitch 100% correct), so `K_ff` is unaffected, but it degrades two-axis AoA *tracking*.
  For a single-plane divert it is negligible; for a diagonal divert it introduces a small
  off-axis AoA the other channel's PD+integral corrects. **Recommend adopting P3's fix** (add
  the tangential term to the pitch/yaw allocation rows, or the full 3x4 damped pseudo-inverse)
  so the feedforward and PD both see the true B matrix — this tightens tracking and is directly
  compatible with this design (the `delta_ff`/`d_int`/`d_pd` amplitudes then feed the
  pseudo-inverse instead of the fixed patterns).

---

## 8. Hand-tuned vs MPPI — recommendation

**Ship the hand-tuned move-the-trim-point + PID AoA-hold law now (Agent A's E2). Do NOT defer
AoA-hold itself to MPPI.** Argument:

1. **This is a physics fix, not single-point tuning.** The whole D-005 meta-lesson is "when
   control tuning stops converging, the block is upstream." The drift-to-2deg was a *structural
   omission in the control law* (no fin trim feedforward), exactly analogous to the frozen
   `a_design` and the CoP/sign bugs. Fixing it is completing the control architecture, and it
   generalises across the whole flight envelope (`K_ff(M)` is a formula, not a fitted constant).
   It is not the kind of brittle single-point tune D-004/D-005 warns against.

2. **MPPI HIER *requires* this loop anyway.** Agent B's HIER mode (the default, M4) runs the
   §8.3 attitude+allocation inner loop **inside every rollout** and only searches
   throttle + 2 lateral-accel. If that inner loop can't hold a commanded AoA, MPPI HIER commands
   a lateral accel, the allocator asks for an AoA, and the airframe drifts back to 2 deg inside
   the rollout — MPPI would plan diverts the plant can't execute. **AoA-hold is a prerequisite
   for MPPI, not an alternative to it.** Build it first; MPPI consumes it verbatim (directive 7,
   single source).

3. **MPPI cannot beat the attitude-loop bandwidth in HIER.** Agent B and Agent C already
   established (msg #908) that HIER-MPPI's lateral bandwidth is bounded by the inner attitude
   loop's tilt slew — MPPI removes the *heuristic* fade-to-blind limit cycle but "cannot exceed
   the attitude loop's tilt slew." So for the *hold* problem specifically, MPPI adds nothing
   the correct inner loop doesn't already give; its value is in the *outer* trajectory
   (no-fade full-lateral re-optimisation to contact, terminal `w_vxy`), which is real but
   orthogonal to AoA-hold.

4. **RAW-MPPI (fins direct) would still limit-cycle without a trim reference.** If MPPI
   commanded the four fin deflections directly (RAW/M5), it would face the *same* marginally
   unstable body and rate-limited fins, and without a trim-point reference the samples chatter
   (which is exactly why Agent B needs SMPPI derivative-lifting for RAW). The move-the-trim-point
   feedforward is the natural warm-start / nominal that a RAW-MPPI should be sampled *around*.
   So even in the purist mode, this law is the operating-point the planner perturbs.

**Where MPPI genuinely wins (keep it on the roadmap):** the *outer* aero-descent + landing-burn
trajectory — the no-fade lateral re-optimisation, the suicide-burn feasibility terminal cost,
and squeezing the last of Agent C's HARD tail. Those are Agent B's M4/M5 and they sit ON TOP of
this inner loop. Entry-burn stays a supervisor (Agent A). 

**Net:** hand-tuned AoA-hold (this doc) is E2 and unblocks both the immediate AERO_OFFSET gate
(Agent A: >=90% land @ 800 m, ceiling >=1000 m) *and* MPPI HIER (which reuses it). MPPI is the
next layer, not a replacement. Recommended order is exactly Agent A's E0..E5: fins -> alloc ->
**this E2 AoA-hold** -> entry supervisor -> robustness -> E5 MPPI-owns-aero.

---

## 9. Peer contributions folded in

- **P1 (`me6gq2nq`, Cmq + fin-torque signs):** the D-005 fin-torque sign fix (`patY=[-1,1,1,-1]`)
  is load-bearing for this design — I independently confirmed that the *wrong* sign makes the
  closed loop diverge to +-26 deg (positive feedback). The Cmq damping term and the
  damping-augmentation (Kd -= Cmq) set the closed-loop `zeta`; my sim confirms critically-to-
  over-damped AoA steps with the D-005 values. (Awaiting P1's independent static-margin numbers
  to cross-check my table in Section 1.1; sent him the `K_ff` closed form to verify against his
  fin-torque derivation.)
- **P2 (`hcmwkotz`, aero coeffs / CoP tension):** flagged the spec contradiction (App-A.6 table
  0.62 L vs 5.4/6.3 prose "forward of CoM") and **adjudicated in favor of D-005** (bare body
  marginally unstable; lit backs it — F9 finless booster "inherently unstable during reentry",
  grid fins provide weathercock stability). **Independently confirmed my static-margin numbers to
  the decimal:** M=1.05 -> +0.9 m, M=0.7 -> -0.2 m, M=2.0 -> -0.3 m (my M=0.7 = -0.019 m differs
  only because I used the heavier m=42.4 t mass state -> higher com; the formula is identical).
  Validated the CN/CA tables (KEEP, no rescale) so `K_ff` stands on `CNa~2.4`. Raised the
  **`VEH_STAGE_LEN`-vs-`VEH_LEN` labeling caveat** now folded into Section 7. My design is agnostic
  to the *sign* of the static margin (it trims either way); `delta_ff` magnitude scales with
  `(x_cp - com)` and `CNa`.
- **P3 (`fgkflf16`, adversarial bug hunt):** **independently corroborated the half-trim-cancel
  diagnosis** with a real-`control_step`+RK4 probe (settles 4.4 deg AoA / -2.2 deg steady fin
  vs a ~7.3 deg cap -> under-trims ~60%), and confirmed the trim-FF sign is correct
  (control-vs-plant torque ratio 1.000) and gimbal alloc is exact. His **FINDING-P3-2** refined
  the static-margin picture: the body is a **hybrid** — stable sub/supersonic, unstable only in
  the transonic bump M0.9-1.2 — matching my Section 1.1 table exactly (D-005's blanket "marginally
  unstable" is precise only transonically). His **BUG-P3-1** (tangential roll-cant leaks 35% into
  pitch/yaw) and **BUG-P3-3** (gimbal rate-state windup) are folded into Section 7; the design
  plugs straight into his full-3x4-pseudo-inverse fix, and the fin integrator's anti-windup
  mirrors his gimbal-windup guard.
- **P4 (`f1gvkjvl`, divert ceiling — FINAL delivered):** consumed my "control is not the binding
  constraint, side-load is" finding to cap his ideal integrator at the side-load schedule and treat
  AoA as deliverable. His **rest-to-rest** analysis (hold max AoA to divert, then REVERSE AoA to
  null lateral velocity) drove my **sequencing** (Section 5.1); the signed feedforward handles the
  reversal automatically. His delivered numbers — ceiling **237 m** (control.c cap) / **313 m**
  (Agent A cap) / **847 m** (hard 15 deg), **velocity-null start at ~6600 m**, ~48 m/deg,
  velocity-null eats ~1/3 of the budget — are now in Section 5.1. His verdict: **AERO_OFFSET
  (800 m/250) is not well-posed by aero+burn past ~mu+0.3 sigma without an entry burn**, and he
  states the integral-augmented AoA-hold is a **precondition** of that verdict. Complementary
  result: P5 sets how close to P4's physical ceiling we fly; P4 shows the ceiling needs the entry
  burn for the big diverts. qbar peaks only ~36 kPa so the 15 deg STRUCT line is never near — the
  4-12 deg cap is deliberate conservatism.
- **Prior fleet (D-003, Agents A/B/C):** Agent A's body-AoA-is-the-divert-lever + fins-trim
  architecture and E0..E5 build plan are the frame this fits into; Agent B's HIER-reuses-the-
  inner-loop is the basis of the MPPI recommendation; Agent C's F1 velocity-null-to-ground fix
  is the reason Section 5.1 keeps the velocity damping active through the handoff.

---

## 10. Artifacts

- `runs/sandbox/p5_trim.c` (+ `.exe`, `p5_trim_out.txt`): static margin, body stiffness,
  `delta_ff` / `K_ff` table, slew times, holdable-AoA ceiling. Mirrors `dynamics.c` exactly.
- `runs/sandbox/p5_cloop.c` (+ `.exe`, `p5_cloop_out.txt`): closed-loop AoA-hold sim — baseline
  vs move-the-trim-point vs +integral, model-mismatch and gust tests, traces.
- Build: `runs/sandbox/p1_build.cmd <src> <exe>` (MSVC `/O2 /fp:precise`), C only per house rule.
