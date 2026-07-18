# WINDTHINK DESIGN — the NAV-legal wind estimator + feedforward

**Author:** WINDTHINK (Opus 4.8) · **Date:** 2026-07-18 (night, post-D-012) · **Status:** design + plant-parity observability probe, ready to implement
**Probe:** `_wind_wt/windobs.c` → built `cl /O2 /fp:precise` (gitignored scratch; plant-parity mirror of `core/dynamics.c` aero + `core/control.c` trim)
**Builds on:** D-009 addendum 2/3 (the wind-floor mechanism + the FAILED feather/hold/late-integral fixes), D-010 C14 integral, D-012 overspeed brake + graze anatomy, HANDOFF_2026-07-18_NIGHT §3.
**Canon it must respect:** §4.3 (guidance NEVER reads `v_wind`), §8.1 (guidance consumes the nav view), directive 2 (determinism sacred), directive 7 (one dynamics source: any law the plant sees, the MPPI rollout mirror must see), directive 9 (TERMINAL byte-identical).

> **DESIGN + ANALYSIS ONLY.** Nothing in `core/` was edited; no cmake/binary was run against the real tree. The only artefacts are this file and the gitignored `_wind_wt/` probe. Every code injection below is specified by **file + function + line-neighborhood** so a build agent implements without re-deriving.

---

## 0. Executive verdict

**The estimator is real, NAV-legal, and cheaply observable — but it is NOT free money, and by itself it can be WORSE than the C14 integral. The honest recommendation is a NARROW, one-shot, gated feedforward, validated estimator-first, and it is the C14 integral's *complement*, not its replacement.**

Five load-bearing findings (all measured in `windobs.c`, plant-parity):

1. **The math closes exactly.** At trimmed aero-descent flight the airframe weathervanes so body −Z tracks the *relative*-wind vector; inverting that geometry with nav's attitude quaternion + inertial velocity recovers the horizontal mean wind to **<0.2 m/s (≈0.5%)**. This is a direct algebraic inversion of the `dynamics.c` aero force law, not a heuristic. **It reads only attitude + inertial v (both in the nav view) — canon §4.3 is satisfied by construction: `v_wind` is never read; it is *inferred*.**

2. **The dominant error is attitude offset, and it is brutal: ~4.5 m/s of wind error PER DEGREE of AoA** at |vrel|~260 m/s (error ≈ |vrel|·sin Δα). A naive "body −Z = wind direction" estimator is poisoned the instant the vehicle holds a divert AoA. **Fix: subtract the KNOWN commanded AoA** (guidance itself commanded `a_lat` → a commanded tilt) — the OBS-B estimator below. This is what makes the estimator trustworthy during an active divert.

3. **The turbulence noise floor is LOW but SLOW.** Dryden w20=30 gives a raw per-tick apparent-wind gust of only **~0.6–0.9 m/s** — but with correlation length Lu~240 m at V=60 the gust is *slowly* correlated (~4 s), so a short-τ low-pass barely attenuates it. It doesn't need to: the amplitude is already inside the feedforward budget, and the **mean wind (13–28 m/s) is quasi-DC** so any τ passes it unbiased. **τ = 4–6 s** is the sweet spot.

4. **Observability collapses in the burn — usefully.** The SRP shield cuts aero to 5% once CT>3, which happens as qbar drops toward the pad. So the estimator's clean window IS the unpowered aero-descent; it must **FREEZE at ignition and coast its last pre-burn value** — which is *exactly* what a pre-ignition feedforward wants (capture the wind before the burn, apply it as the burn fights that same wind).

5. **Why this differs from the D-009 failures, honestly:** the feather/hold/late-integral fixes all failed because they were *blind* — they straightened or damped without knowing which way the wind blew, so they either handed the wind a free window (the hold) or fought a direction they couldn't see (feather can't be simultaneously unloaded AND upright in a crosswind). This estimator gives guidance the **wind vector**, so the feedforward is *aimed*. **But** — and this is the honest catch — the same "aimed" property is what makes double-counting dangerous, and a latched-gust or transonic-garbage estimate injected as a pre-bias could push a clean lander off-pad. The verdict (§7) is: ship it **only** as a gated, one-shot, magnitude-capped divert-aim pre-bias, behind an estimator-only logging gate, and keep the C14 integral doing the terminal steady-state trim it already does well.

**Recommended architecture:** estimator state lives in the **`Sim` struct** (sibling to `lat_eint[2]`), updated once per guidance tick from the nav view inside `sim.c` (NOT inside `hoverslam_step`, which must stay stateless/byte-identical for TERMINAL and for the MPPI rollout). The feedforward's **ranked #1 injection** is an **upwind ignition pre-bias on the divert aim-point** (shift the target the predicted burn-window drift upwind). #2 (warm-start the C14 `eint`) is **conditionally recommended with a double-count guard**. #3 (vdes profile shift) is **rejected as redundant** with #1. MPPI gets the estimate **only** if a leak-audited rollout mirror is added — default is MPPI-exempt exactly like the C14 integral (D-010 lesson: reactive medicine poisons a replanner).

---

## 1. The plant: where the wind actually enters, and what the vehicle does about it

Read directly from `core/dynamics.c` (aero block, lines 138–174) and `core/control.c` (trim feedforward, lines 158–180; fin trim-point mover, 207–234).

### 1.1 The wind and the relative-wind vector
`sim.c wind_sample` (lines 20–46) builds the **world** wind:
- **Mean:** `sp = u_ref · scale(h)` in a FIXED per-run direction `wind_az`. Power-law `scale=(h/10)^0.14`, jet-amplified `×(1+1.2t)` above 1 km, `×0.4` above 25 km. For AERO (`u_ref=6`) this is **13–28 m/s** through the aero-descent band; for ENTRY (`u_ref=8`) larger.
- **Turbulence:** Dryden `wind_filt[k]` — a first-order Markov (AR(1)) process driven by the `RNG_WIND` Philox stream, `wind_filt[k] = (1−β)wind_filt[k] + su√(2β)η`, added to the mean on the two horizontal axes.
- **KEY (scenario.c:67,81):** `wind_az` is set to the *same* random azimuth as the initial attitude tilt, fixed per `(seed,run)`. The wind is a fixed-direction mean + slow gusts. **This is deterministic and replayable** — the estimator built on it is replay-safe.

`dynamics.c:139–140` forms the relative wind the aero actually sees:
```
vrel_w = v_world − wind_world              // world relative-wind
vrel_b = q_rot_inv(q, vrel_w)              // body frame
speed  = |vrel_b|,  qbar = ½ρ·speed²
```
The aero **normal force** (`:158–169`) acts normal to the body axis, in the direction of the *lateral component of the relative-wind unit vector* `vhat = vrel_b/|vrel_b|`, magnitude `Fn = qbar·Aref·CN`, `CN = CNα(M)·α·srp_shield`, where the AoA `α = acos(−vhat_z)` (`:160–161`). The CoP is **aft of CoM** (`xcp_frac ≈ 0.29–0.32·L`, destabilizing — `:30–41`).

### 1.2 What the vehicle does: it trims INTO the relative wind
`control.c:158–180` **cancels the body aero moment** (the trim feedforward), and `control.c:207–234` (the P5 fin trim-point mover) drives the fins to **hold the commanded AoA at zero PD error**. The net closed-loop behavior:

> **In the unpowered aero-descent, with the divert steering feathered (no `a_lat` command), the airframe weathervanes: the body −Z axis aligns with the relative-wind vector `vrel_w`, to within the small residual trim AoA.** With a divert command, it holds `body −Z` at the commanded AoA off `vrel_w`.

This is the observability hook D-009 flagged ("tilt−vertical vs v_inertial encodes the relative-wind direction"), now grounded in the exact plant: **attitude tells you `vrel_w` direction; nav's inertial `v` gives you `v_world`; the difference is `wind_world`.**

---

## 2. THE ESTIMATOR

### 2.1 The math (derived from §1, two observers)

Let `zb2w = q_rot(q, [0,0,1])` be the body +Z axis expressed in world (from the nav quaternion). Let `v = (vx,vy,vz)` be the nav inertial velocity.

**OBS-A (weathervane / zero-AoA approximation)** — valid when steering is feathered:
```
vrel_dir  ≈ −zb2w                          // body base points into the wind
|vrel|    ≈ |v|                            // wind ≪ v, so magnitudes ~equal
vrel_w    = |v| · (−zb2w)
w_est_xy  = v_xy − vrel_w_xy               // the horizontal wind estimate
```
Probe result (§(1)): recovers the mean wind to **<0.2 m/s** at 1.5–8 km. The residual is the small |vrel|≠|v| coupling from the horizontal velocity; negligible at the pad-relevant scale.

**OBS-B (AoA-corrected)** — REQUIRED whenever the vehicle holds a divert AoA:
The airframe is NOT weathervaned when guidance commands `a_lat`; it holds a commanded AoA `α_cmd`. But **guidance KNOWS `α_cmd`** — it is the tilt implied by the `a_lat`/`a_vert_ref` mapping in `control.c:120–125` (`zdes = normalize([a_lat, a_vert_ref])`, tilt = angle of `zdes` from vertical). Two equivalent corrections:
- **(B1, geometric):** rotate the estimated `vrel_dir` by `−α_cmd` in the steering plane (remove the commanded tilt before treating `−zb2w` as the wind direction). i.e. reconstruct the *aero-relative* body axis = the attitude the body WOULD hold if it were weathervaned = `zb2w` de-rotated by the commanded AoA about the commanded steering axis.
- **(B2, force-inversion, higher fidelity):** solve `α` from the plant CN law using the realized lateral specific force. Not needed given B1's accuracy and its lower complexity; documented as a fallback.

Probe result (§(2)): **without** the AoA correction, a 1° attitude offset = **4.5 m/s** wind error; **with** B1 it returns to the <0.2 m/s floor. **This is the single most important estimator design decision** — OBS-B, not OBS-A, is what ships.

**Observability gate (when to update the estimate at all):**
```
observable = fins_deployed
          && !engine_on                    // unpowered — shield=1, full aero signal
          && qbar > QBAR_OBS_FLOOR         // ~500 Pa (probe: signal full above 300 Pa; margin)
          && |vrel| > V_OBS_FLOOR          // ~30 m/s (AoA geometry well-defined)
          && tilt   < TILT_OBS_MAX         // ~20° (past this the small-AoA CN model breaks; == the
                                           //       guidance stabilize-first cutoff, hoverslam.c:200)
```
Transonic (0.8<M<1.2): the CN table and `fin_dip` are still valid but the body aero is noisier and the fin authority dips; **de-weight** (freeze or raise τ) through the dip rather than trusting a fast update.

### 2.2 The filter (mean vs gust)
A first-order low-pass on the per-axis estimate, at the 50 Hz guidance tick:
```
w_filt[k] += (dt_tick / (TAU + dt_tick)) · (w_est[k] − w_filt[k])
```
Probe result (§(3)): the Dryden gust is **low amplitude (~0.6–0.9 m/s) but slow (~4 s correlation)**, so τ trades gust-rejection against nothing useful (the mean is DC). **τ = 4–6 s**: leaked gust ~0.35–0.5 m/s, mean unbiased, and the settling lag is irrelevant because the mean wind barely changes over the ~20 s descent. **Do NOT chase a short τ** to "track gusts" — that re-imports the exact noise the estimator exists to reject, and gusts are not the floor (the mean is).

### 2.3 Where the state lives (placement argument)
**Verdict: in the `Sim` struct, as a sibling to `lat_eint[2]`. Updated in `sim.c` on guidance ticks from the nav view. NOT in `hoverslam_step`, NOT in the NAV layer.**

- **NOT the NAV layer.** `nav.{h,c}` is a pure *measurement* layer — it perturbs/passes-through the true state and is contractually bit-transparent in NAV_TRUTH (the memcmp gate). Adding an *estimator* (a stateful inference) there would either break that transparency or bloat the layer's contract. The nav view is the estimator's *input*, not its home. (It is correct that `wind_filt` appears in the nav pass-through set — but that is the *plant's* Dryden integrator, truth bookkeeping, NOT a legal guidance wind estimate; the estimator must never read it, or §4.3 is violated.)
- **NOT `hoverslam_step`.** That function is `BL_HD`, stateless-per-tick, and byte-shared with the MPPI rollout via `mppi_execute → hoverslam_step` (directive 7). Any persistent state there leaks into the rollout and breaks determinism/parity. It must stay pure.
- **YES the `Sim` struct.** `lat_eint[2]` already establishes the precedent: guidance-adjacent persistent state that (a) is deterministic (updated from the nav view with no wall-clock, no unordered FP), (b) replays bit-identically, (c) is naturally exempt from MPPI unless explicitly mirrored. Add:
  ```c
  double wind_est[2];      // filtered horizontal mean-wind estimate (world) [m/s]
  int    wind_est_valid;   // 0 until the observability gate first fires
  double wind_est_frozen[2]; // value latched at ignition (coast through the shielded burn)
  ```
  Zero-initialized by the existing `memset(s,0,...)` in `sim_init` (sim.c:49) — no new init path. **Determinism:** the update reads only `nav.*` and `s->wind_est*`; identical `(seed,run,step)` ⇒ identical estimate ⇒ replay-safe, exactly like `lat_eint`. **Gate on TERMINAL:** the whole estimator is `fins_deployed`-gated, so TERMINAL (fins stowed) never updates it and never reads it — byte-identical, directive 9 satisfied.

### 2.4 Estimator update — exact injection point
`sim.c`, on the guidance tick, **after** `nav_measure`/`nav_resync` and **before or after** guidance (order-independent since it only writes `s->wind_est*`), ideally right beside the C14 integral block (sim.c:282–305). Pseudocode:
```c
if (is_gtick) {
    // --- WIND ESTIMATOR (NAV-legal; reads nav attitude + inertial v, never wind_world) ---
    // observability gate (unpowered aero-descent = the clean window; freeze in the burn)
    double zb2w[3]; { double zb[3]={0,0,1}; q_rot(zb2w,&nav.y[S_QX],zb); }
    double v[3]={nav.y[S_VX],nav.y[S_VY],nav.y[S_VZ]};
    double vmag=v3_norm(v);
    AtmoOut atmE; atmo_eval(nav.y[S_RZ],&atmE);
    double qbarE=0.5*atmE.rho*vmag*vmag;               // NB: uses inertial v; wind<<v so ~vrel
    double tiltE=sim_body_tilt(&nav);
    int observable = st->fins_deployed && !st->engine_on
                  && qbarE>QBAR_OBS_FLOOR && vmag>V_OBS_FLOOR && tiltE<TILT_OBS_MAX;
    if (observable) {
        // OBS-B: de-rotate the KNOWN commanded AoA out of the attitude before inverting.
        // commanded tilt axis/mag from the last gcmd.a_lat (guidance already computed it):
        //   zdes = normalize([gcmd.a_lat[0], gcmd.a_lat[1], a_vert_ref]);  aoa_cmd = angle(zdes,+Z)
        // remove aoa_cmd from zb2w in the steering plane -> zb2w_aero (weathervane-equivalent axis)
        double vrel_w[3] = { -zb2w_aero[0]*vmag, -zb2w_aero[1]*vmag, -zb2w_aero[2]*vmag };
        double w_meas[2] = { v[0]-vrel_w[0], v[1]-vrel_w[1] };
        double a = GUIDANCE_DT/(TAU_WIND+GUIDANCE_DT);   // dt_tick = GUIDANCE_DT (50 Hz)
        if (!s->wind_est_valid) { s->wind_est[0]=w_meas[0]; s->wind_est[1]=w_meas[1]; s->wind_est_valid=1; }
        else { s->wind_est[0]+=a*(w_meas[0]-s->wind_est[0]); s->wind_est[1]+=a*(w_meas[1]-s->wind_est[1]); }
    }
    // latch at the moment of ignition so the burn feedforward uses the pre-shield value:
    if (st->engine_on && !prev_engine_on_flag) { s->wind_est_frozen[0]=s->wind_est[0]; s->wind_est_frozen[1]=s->wind_est[1]; }
}
```
(The `a_vert_ref` and `aoa_cmd` reconstruction mirror `control.c:82–125`; share the arithmetic or recompute — it is cheap. `prev_engine_on_flag` is the existing ignition-edge the latch in sim.c:228 already detects; reuse it.)

**Constants (proposed, to be swept):** `QBAR_OBS_FLOOR 500`, `V_OBS_FLOOR 30`, `TILT_OBS_MAX 20°`, `TAU_WIND 5.0`.

---

## 3. THE FEEDFORWARD — ranked injection points

The wind estimate is a *world horizontal vector* `w = s->wind_est_frozen` (post-ignition) or `s->wind_est` (pre-ignition). Each injection is analyzed against the **known D-012 failure anatomy**: the op-5 graze band is (a) the **trim-residual miss** — a flawless landing that settles upright ~33 m *downwind* (the steady-state the faded/capped C14 trim can't finish, DECISIONS D-012 addendum "runs like 3"), and (b) the **overshoot tail** — crosses pad center still hot, slides outbound to ~26 m (a th-mechanism run, DECISIONS "runs like 73"); plus s7's fuel-outs from long corrections.

### #1 (RANKED FIRST) — Upwind ignition pre-bias on the divert aim-point
**Mechanism.** During the landing burn the vehicle drifts downwind by a predictable amount before it can null it. Shift the divert **target** upwind by that predicted drift, so the vehicle aims at a point that the wind then carries onto the pad.

**The drift model (this is the `f(wind, burn duration, authority)` the mission asks for):**
```
drift ≈ (a_wind / 2) · t_burn²      capped by authority, where
  a_wind  = the residual downwind specific force the burn can't immediately cancel
          ≈ (shielded body-aero side force + thrust×trim-tilt leak)/m  — D-009 measured ~1.3–1.7 m/s²
  t_burn  = predicted burn duration ≈ |vz_ignite| / a_design  (a few seconds; hoverslam has this)
```
A conservative closed form the build agent can start from: the C14 addendum says the trim-residual settles at **~33 m** at the shipped gains. That IS the measured steady-state drift for the nominal wind — so the *simplest* well-posed pre-bias is **`aim_shift = −k_ff · w_unit · drift_pred`**, with `drift_pred` from the model above and `k_ff∈[0,1]` a single gain calibrated so the mean-wind case lands the 33 m residual on center. Direction is `−w_unit` (upwind).

**Injection point.** `guidance_hoverslam.c hoverslam_step`, the lateral block (lines 84, 132–176). The cleanest, most surgical shift is on the **position error fed to the divert law**: replace `r_xy` with `r_xy_biased = r_xy − aim_shift` where `aim_shift` points upwind. Because the divert law is `vdes = f(r_pred)` and `r_pred = r_mag + v_rad·T_LEAD` (`:147–148`), biasing the target moves the whole sqrt-decel profile's aim-point — the vehicle drives to the upwind point and the wind carries it in. **BUT** `hoverslam_step` is stateless and byte-shared with MPPI, so it cannot read `s->wind_est`. Two clean options:
- **(1a, preferred) pass the bias in via `GuidanceCmd`.** Add `double aim_bias[2]` to `GuidanceCmd` (guidance.h); `sim.c` fills it from `s->wind_est_frozen` on the tick (GM_HOVERSLAM only, zeroed otherwise); `hoverslam_step` subtracts it from `r_xy` at the top of the lateral block. This keeps `hoverslam_step` a pure function of its inputs (determinism/parity preserved: MPPI passes `aim_bias=0` unless explicitly wired), and mirrors how `a_lat` already flows.
- **(1b) apply the bias in `sim.c` as a post-hoc `a_lat` nudge** (like the C14 integral does). Less clean — it fights the divert law's own velocity-null rather than re-aiming it — and it double-counts more easily. **Prefer 1a.**

**Expected effect on the anatomy.** Directly attacks the **trim-residual miss (a)**: instead of the C14 integral slowly winding up to trim a wind it discovers reactively (and fading out before it finishes → the 33 m residual), the pre-bias aims upwind from `t=0` of the burn so the steady-state settle lands on center. It does NOT worsen the overshoot tail (b) if magnitude-capped (it only moves the aim-point a few tens of metres). It should **reduce** s7 fuel-outs by shortening the terminal correction (the vehicle is already aimed right, so less late clawing).

**Gating.** `fins_deployed` only; applied from ignition using the frozen estimate; **magnitude-capped** at ~`AIM_BIAS_MAX` (e.g. 40 m — the graze band scale) so a latched-gust or garbage estimate can never fling a clean lander off-pad. TERMINAL: `aim_bias=0` always (fins stowed) → byte-identical.

### #2 (CONDITIONAL) — Warm-start the C14 integral `eint` with the estimator's equivalent trim
**Mechanism.** The C14 integral (`sim.c:293–302`) spends its first ~1–2 s winding up from zero to the trim value that cancels the wind. Warm-start `s->lat_eint[ax]` at the moment the integral engages (ign_timer≥2) with the **estimator's equivalent steady-state trim** — the `eint` value that produces the counter-`a_lat` matching the estimated wind's side-force — killing the spin-up lag.

**The equivalent trim.** The integral applies `a_lat −= lat_eint·wfade`. The steady-state `lat_eint` that trims wind `w` is the one whose `a_lat` cancels the residual downwind specific force `a_wind` (§#1). So `lat_eint_ff[ax] ≈ (a_wind_vector[ax])` (units already m/s², matching the `a_lat` subtraction; the `wfade` handles the near-ground taper). Seed `s->lat_eint` with `lat_eint_ff` at engage.

**Double-counting analysis (the D-010 MPPI lesson, applied honestly).** D-010 proved that layering a reactive integral on a *replanner* (MPPI) double-corrects, because the replanner already compensates through the fresh state each tick → the integral fights the plan through the plant. **Does #2 double-count?**
- Against **#1 (the aim pre-bias):** YES, partially — #1 re-aims the target upwind, and the C14 integral then *also* winds up on the (now smaller) residual position error. This is the dangerous interaction. **Resolution:** #1 and #2 are **alternatives, not a stack.** If #1 ships, the C14 integral keeps doing ONLY the terminal steady-state trim it does today (it already fades near ground); do NOT also warm-start it, or the two corrections sum. If #1 does NOT clear the graze band, try #2 *instead* (revert #1), warm-starting the existing integral.
- Against the MPPI replanner: #2 is **GM_HOVERSLAM-only** exactly as the C14 integral already is (`sim.c:282`) — MPPI never sees it. Safe by the existing gate.
- **Verdict on #2:** recommended **only as the fallback** if #1 underperforms, and **never stacked with #1.** The warm-start itself is sound (it is feedforward into a reactive term the vehicle already trusts), but two aimed corrections on the same error is precisely the poison D-010 named.

**Injection point.** `sim.c:293–302`, at the `else { s->lat_eint[...]=0; }` → engage transition: on the first tick the gate opens, set `s->lat_eint[ax] = clamp(lat_eint_ff[ax], ±EINT_CAP)` before the normal integration resumes.

### #3 (REJECTED) — vdes profile shift
Aim the sqrt-decel `vdes` profile (`hoverslam.c:136–149`) at the drift-compensated point. **Rejected as redundant with #1:** #1 already re-aims the profile by biasing the position error `r_xy` that feeds `r_pred` → `vdes`. Shifting `vdes` *separately* would be a second, differently-parameterized aim change on the same law — more knobs, same effect, higher double-count surface. #1 subsumes it. (If a build agent finds #1's position-bias couples badly with `T_LEAD`, the fallback is to bias `r_pred` specifically rather than `r_xy` — but that is a tuning detail of #1, not a distinct injection.)

### Feedforward summary table
| # | injection | file · function | attacks | double-count risk | gate |
|---|---|---|---|---|---|
| **1** | upwind aim pre-bias (via `GuidanceCmd.aim_bias`) | `guidance_hoverslam.c hoverslam_step` (r_xy bias), filled in `sim.c` | trim-residual miss (33 m), s7 fuel-outs | low if capped + not stacked with #2 | fins_deployed; capped ≤40 m; TERMINAL=0 |
| **2** | warm-start `lat_eint` | `sim.c` C14 block (:293) | integral spin-up lag | **HIGH vs #1** (never stack); safe vs MPPI (GM_HOVERSLAM gate) | fins+PH_LANDING_BURN+ign≥2 |
| ~~3~~ | vdes profile shift | — | (redundant) | subsumed by #1 | rejected |
| MPPI | rollout-mirrored aim bias | `guidance_mppi.c cmd_from_u_lean` | AERO off-pad | needs leak audit (directive 7) | **default: exempt** |

**MPPI note.** Default is **MPPI-exempt** (pass `aim_bias=0`), exactly like the C14 integral, per the D-010 verdict that MPPI's wind rejection IS its replan loop. If a build agent later wants MPPI to use the estimate, it must be mirrored into `cmd_from_u_lean` AND leak-audited with the single-run invariance check (HANDOFF §1.4/§5) — treat as a separate, later experiment.

---

## 4. VALIDATION PLAN (the staged experiment sequence a build agent runs)

**All in a `_wind_wt\` worktree copy (CMakeLists + core; VS2022 x64 configure), gitignored. Never edit/build the real tree. Gates after EVERY build:** `--selftest` = PASS, `--headless --scenario terminal --seed 42 --runs 200` = **exactly 194/200**, and a determinism pair on the changed scenario (same `--run` twice, RESULT lines byte-match). If TERMINAL moves, the change leaked past its fins-deployed gate — stop, fix.

### Stage 0 — Estimator-only logging pass (NO feedforward; the observability truth-up)
Build the estimator (§2) writing `s->wind_est` but **feeding it into nothing** (`aim_bias` unused). Add a **worktree-only debug tap** (a `#ifdef WIND_DBG` printf, or a spare CSV column) emitting per guidance tick: `t, h, qbar, tilt, engine_on, w_est[2], w_true[2]` where `w_true = wind_sample(h)` (the tap may read truth — it's diagnostic, not guidance).
- **Batch:** ENTRY s42 ×20 runs + AERO s42 ×20 runs, dump the tap.
- **Decision rule / PASS criteria:**
  - mean |w_est − w_true| over the observable window **< 2 m/s** (probe predicts <0.5; 2 is the ship bar);
  - the estimate is **stable** (no latching/divergence) through the transonic dip and up to ignition;
  - the frozen value at ignition is within **2 m/s** of the true mean wind at the ignition altitude.
- If it fails: the OBS-B AoA correction is wrong, or the gate is admitting garbage (transonic, low-qbar). Fix the estimator BEFORE any feedforward. **Do not proceed to Stage 1 until Stage 0 passes** — a feedforward on a bad estimate is worse than no feedforward.
- **Determinism check:** the estimator must be byte-transparent when its output is unused — i.e. ENTRY/AERO/TERMINAL rates must reproduce the D-012 baselines EXACTLY (88/220/194 s42) with the estimator computing-but-not-injecting. If any rate moves, the estimator has a hidden coupling (it must not) — fix before proceeding.

### Stage 1 — Feedforward #1, alone (the aim pre-bias)
Wire `aim_bias` (§#1, option 1a). Start `k_ff` conservative (0.5) and `AIM_BIAS_MAX 40`.
- **MPPI invariance FIRST** (before any batch): `--run --scenario aero_offset --seed 42 --run 1 --mppi` vs the prior RESULT line — must be **byte-identical** (MPPI passes `aim_bias=0`; this proves the wiring didn't leak into the rollout). If it moved, the `GuidanceCmd` plumbing leaked — fix.
- **Batch:** ENTRY s42 ×100, AERO s42 ×300 (the standard gate batches).
- **Decision rule:** ship `k_ff`/cap iff **ENTRY ≥ 88 AND AERO ≥ 73.3** (no regression) AND the op-graze cohort shrinks (target the 3 trim-residual grazes at 26–32 m → on-pad). Cross-seed on s7/s99 before believing a >1-pt move (D-009/D-012 noise-scale discipline).
- **Mini-grid** if the single point is promising: `k_ff ∈ {0.4,0.6,0.8,1.0}` × `AIM_BIAS_MAX ∈ {25,40,60}` — the `_adapt_wt\sweep.ps1` regex-patch-#define pattern (HANDOFF §2), one CSV row per config, per-row selftest+TERMINAL-194 gate. Pick by ENTRY rate + graze-band count + AERO not regressing.
- **Honesty gate:** if AERO *regresses* (the aim bias over-corrects the tier-0 divert), the pre-bias is fighting the divert's own velocity-null — reduce `k_ff`, or gate the bias to ENTRY-scale winds only. Record the number either way.

### Stage 2 — Feedforward #2, alone (ONLY if #1 fails to clear the graze band)
Revert #1. Warm-start `lat_eint` (§#2). Same MPPI-invariance-first, same batches, same decision rule. **Never run #1 and #2 together** (§#2 double-count analysis).

### Stage 3 — Cross-seed + nav-noisy + robustness
For whichever of {#1, #2} shipped: ENTRY/AERO on s7 + s99; `--nav-noisy` (the estimator now runs on the *noisy* attitude/velocity — this is the real test of whether the 0.1° attitude noise, ×4.5 m/s/deg, degrades the estimate: predict ~0.5 m/s added wind error, tolerable, but MEASURE); confirm TERMINAL 96.5 nav-noisy unchanged. Freeze goldens (`*_wind_baseline.txt`) as an ADR event.

### Batch sizes / cadence
- Singles (~9 s MPPI / instant hoverslam) for wiring + invariance + first look.
- 100/300-run gate batches for decisions (ENTRY 1–2 min, AERO 2–3 min uncontended).
- Long grids → background, one CSV row per config, analyze/doc while waiting (never block on a monitor).

---

## 5. INTERACTIONS & RISKS (the interaction map)

- **#1 × C14 integral:** the intended division of labor — #1 handles the *transient* (aim upwind from ignition), the C14 integral handles the *terminal steady-state* trim it already does (faded near ground). They attack the SAME residual, so **watch for over-correction**: if the mean-wind case starts landing *upwind* of center, #1's `k_ff` is too high OR it is stacking with the integral's own wind-up. Stage-1 mini-grid tunes this; the cap bounds the worst case.
- **#1 × the D-012 overspeed brake:** the brake stiffens the divert on profile-overspeed (hot arrival). Re-aiming upwind changes the arrival geometry slightly; the brake is state-adaptive so it self-adjusts, but confirm the AERO too-hard cohort (op→th conversion frontier, D-012) doesn't grow.
- **#1 × the height-split deck null:** below KVEL_SPLIT_H the null gain ramps up; the aim bias is a position-target shift, orthogonal to the velocity-null, so low interaction — but the bias should probably **fade with the same lat_scale** the seek term uses (hoverslam.c:177) so it doesn't drag the vehicle off a good terminal null. (Design choice for the build agent: fade `aim_bias` by `lat_scale` too.)
- **Estimator × transonic:** the CN model degrades in 0.8<M<1.2; the gate de-weights but a bad transonic sample could still bias the filter. τ=5 s smooths it, but WATCH the Stage-0 tap through the dip.
- **Estimator × nav-noise:** 0.1° attitude σ × 4.5 m/s/deg ≈ 0.45 m/s per-sample; τ=5 s averages it down; predicted tolerable but Stage 3 measures it.

---

## 6. HONESTY — what could make this WORSE, and the failure modes

**What makes it worse than the C14 integral alone:**
1. **A latched gust or a garbage estimate injected as a pre-bias moves the aim-point deterministically wrong.** The C14 integral is reactive — it can only respond to the position error it actually sees, and it fades near the ground, so its worst case is a *residual* (the 33 m miss), never an *active push off a good trajectory*. A feedforward that AIMS can actively degrade a clean lander. **Mitigation:** the magnitude cap (≤40 m), the observability gate (freeze in the burn, reject low-qbar/transonic/high-tilt), and the one-shot-frozen-at-ignition design (no live gust chasing during the burn). But the residual risk is real and is why Stage 0 (estimator-only) MUST pass first.
2. **Double-counting if #1 and #2 are stacked** (§#2) — two aimed corrections on the same error, the D-010 poison. Mitigation: the design forbids stacking.
3. **Overfitting the drift model.** `drift ≈ ½·a_wind·t_burn²` is a linearized approximation; the real leak is the shielded-aero + thrust×tilt coupling through the ignition transient, which is nonlinear. If `k_ff` is tuned to the nominal wind and the dispersion has fat tails, it could help the median and hurt the tails. Mitigation: cross-seed (Stage 3), cap, and the decision rule requires *no regression* on ENTRY/AERO, not just a median win.

**The failure modes, enumerated:**
- **Estimator latches a gust:** low risk given τ=5 s + low gust amplitude, but a transonic outlier could bias it. Caught by Stage 0's stability criterion.
- **Transonic garbage:** the CN/fin model degrades 0.8<M<1.2; the gate de-weights, but verify on the tap.
- **Shielded-regime staleness:** the frozen estimate is captured pre-ignition; if the true wind changes materially during the ~few-second burn (it won't — the mean is quasi-DC, gusts are ~0.6 m/s), the frozen value is stale. Bounded by the gust amplitude — negligible.
- **The `wind_filt` temptation:** a build agent might "shortcut" by reading `nav.wind_filt` (it's in the pass-through!) — this is a **canon §4.3 violation** (that's the plant's truth wind). The estimator MUST infer from attitude+v only. Flag loudly in-code.

**Why the D-009 feather/hold/late-integral fixes failed — and whether this is genuinely different (argued both ways):**
- **They failed because they were BLIND.** Feather straightens toward vertical, but "zero AoA in a crosswind = aligned with the *relative* wind = ~3.7° from vertical" (D-009 addendum 2) — you cannot be simultaneously unloaded and upright, so feathering to vertical *creates* AoA. The 2 s hold gave the wind a free unactuated window. The late integral engaged after the crossover, too late and muted. None of them knew the wind vector.
- **The case that THIS IS DIFFERENT:** the estimator supplies the wind *vector*, so the feedforward is *aimed* — it pre-biases upwind by a computed amount, from `t=0` of the burn, exactly countering the drift the blind fixes couldn't see. The observability is real and measured (§2), the injection is surgical and capped, and it complements (not replaces) the C14 integral that already works.
- **The case that IT ISN'T DIFFERENT (the devil's advocate):** the D-012 addendum already proved the reactive structure is **Pareto-saturated** — the trim grid (sweep3) showed stronger/longer/deeper trim converts grazes ONLY by paying in too-hard + fuel. If the feedforward's aim-shift ALSO just trades the trim-residual miss for a too-hard (by arriving with residual velocity from the re-aim), it buys nothing — same Pareto wall, different knob. And the estimator adds complexity + a new failure surface (latching) that the robust-but-limited integral doesn't have.
- **COMMITTED VERDICT:** **Build it, but as a NARROW complement gated behind Stage 0, and treat it as a hypothesis to be FALSIFIED, not a fix to be shipped.** The physics says the aim pre-bias attacks a mechanism (the transient downwind drift) that the reactive trim structurally *cannot* (the integral is inherently lagged + faded). That is a real, distinct lever — **the first feedforward in the stack, vs a family of reactive laws.** But the D-012 Pareto result is a genuine warning: if Stage 1 shows the aim-shift converts trim-residual grazes into too-hards (op→th, no net gain), then the estimator is confirmed to be hitting the same wall reactively-shaped medicine hit, and it should be **shelved in favor of MPPI capacity** (the HANDOFF's Roadmap A/B), with the Stage-0 estimator-vs-truth data preserved as the artifact that settles the question. The estimator is cheap to build, cheap to falsify, and either way produces a decisive measurement. That asymmetry — small cost, decisive result — is why it's worth building now.

---

## 7. Implementation checklist (for the build agent, no re-derivation needed)

1. **`Sim` struct (sim.h):** add `wind_est[2]`, `wind_est_valid`, `wind_est_frozen[2]` beside `lat_eint`. Zeroed by existing `memset`.
2. **`GuidanceCmd` (guidance.h):** add `double aim_bias[2]` (for #1, option 1a).
3. **Estimator update (sim.c):** the §2.4 block on `is_gtick`, beside the C14 integral. Constants `QBAR_OBS_FLOOR 500`, `V_OBS_FLOOR 30`, `TILT_OBS_MAX 20°`, `TAU_WIND 5.0`. Reads nav attitude + v ONLY — never `wind_world`/`wind_filt`.
4. **Debug tap (worktree-only, `#ifdef WIND_DBG`):** emit `t,h,qbar,tilt,engine_on,w_est[2],w_true[2]`.
5. **Feedforward #1 (guidance_hoverslam.c + sim.c):** `sim.c` fills `gcmd.aim_bias` from `wind_est_frozen` (GM_HOVERSLAM, fins, capped ≤40 m, faded by `lat_scale`); `hoverslam_step` subtracts it from `r_xy` at the top of the lateral block. `aim_bias=0` for MPPI + TERMINAL.
6. **Gates:** selftest, TERMINAL 194, determinism pair, and **MPPI single-run invariance** after touching `hoverslam_step`/`GuidanceCmd` (the leak check — non-negotiable, HANDOFF §1.4).
7. **Validation:** Stage 0 (estimator-only, must pass the <2 m/s + byte-transparency bar) → Stage 1 (#1 alone) → Stage 2 (#2 alone, only if #1 fails) → Stage 3 (cross-seed + nav-noisy). Decision rule: no ENTRY/AERO regression + graze-band shrink.
8. **ADR:** append a `DECISIONS.md` D-013 entry (estimator design + whichever feedforward measured, WITH numbers — including a null result if it hits the D-012 Pareto wall). Freeze `*_wind_baseline.txt` goldens if it ships.

---

## Appendix A — probe reference
`_wind_wt/windobs.c` (plant-parity, `cl /O2 /fp:precise`), sections:
- (1) mean-wind recovery at trim: **err <0.2 m/s (0.5%)** across 1.5–8 km.
- (2) error vs residual AoA: **~4.5 m/s per degree** → OBS-B (AoA correction) is mandatory.
- (3) Dryden gust floor: raw **0.6–0.9 m/s**, slow (Lu~240 m, ~4 s corr); τ=4–6 s → residual 0.35–0.5 m/s; mean unbiased.
- (4) observability gate: shield=1 unpowered (full aero, qbar 0.3–60 kPa); shield→0.05 in-burn as CT>3 near the pad → **freeze estimator at ignition**.

## Appendix B — canon compliance restated
- **§4.3 (guidance never reads v_wind):** SATISFIED — the estimator *infers* wind from attitude + inertial v (both nav-view kinematics); it never reads `wind_world` or `wind_filt`. The inference is an on-board NAV computation, exactly like a real flight computer estimating winds aloft from its own INS + air-data.
- **§8.1 (guidance consumes the nav view):** SATISFIED — the estimator's inputs are `nav.y[S_QX..S_QW]` and `nav.y[S_VX..S_VZ]`, the perturbed kinematic estimate; under `--nav-noisy` it correctly degrades on noisy inputs (Stage 3).
- **directive 2 (determinism):** SATISFIED — state in `Sim`, updated from the nav view with no wall-clock/unordered-FP; replay-identical like `lat_eint`.
- **directive 7 (one dynamics source):** the feedforward is `GuidanceCmd`-plumbed so MPPI is exempt by default (`aim_bias=0`); if ever wired to MPPI, the rollout mirror + invariance check are mandatory.
- **directive 9 (TERMINAL byte-identical):** the whole estimator + feedforward is `fins_deployed`-gated → TERMINAL never touches it.
