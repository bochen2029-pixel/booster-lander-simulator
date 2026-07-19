# DECISIONS (append-only ADR log)

## D-001 — Adopt CLAUDE_v1.md as canon (2026-07-18)

CLAUDE_v1.md supersedes CLAUDE_v0.md. Key deltas, decided during spec authoring:

- Telemetry 125 Hz (500/4 integer decimation; v0's 120 Hz did not divide 500).
- Actuator dynamics (throttle lag/ramp, gimbal 2nd-order, fin rates, ignition sequence,
  20 ms guidance transport delay) are part of the plant and of rollout dynamics.
- Contact is dynamical: per-leg spring-damper + crush core + friction + settling +
  tipover; verdict graded after SETTLING (v0 froze on contact).
- US76 atmosphere replaces bare exponential; wind = mean profile + Dryden + 1-cosine
  gusts, all seeded; SRP aero-shielding blend during retropropulsion burns.
- Control architecture made explicit: nav layer → guidance (50 Hz) → quaternion-PD +
  allocation (500 Hz). MPPI default is HIER parameterization; RAW is a toggle.
- MPPI spec includes IS-correction term (γ=λ(1−α)), OU noise, SGF smoothing,
  event-terminated rollouts, suicide-burn-feasibility terminal cost, ESS-adaptive λ.
- G-FOLD demoted/promoted to offline optimality oracle in tools/ (not a flight mode).
- Renderer: WebGPU-first (three 0.185.1, RenderPipeline API, AgX), camera-relative
  floating origin, procedural booster built from HELLO geometry, physics-driven plume
  (Mach-cell spacing 0.67·D·√(p0/pa), altitude ballooning, SRP envelopment, TEA-TEB
  flash), audio with propagation delay + triple sonic boom.
- Command journal: every run (interactive included) replays bit-exactly.
- Core WS server embedded (RFC6455 subset, ~250 lines, single client); no Rust relay hop.
- Determinism scope: bit-identical same binary + same GPU arch; goldens pinned sm_89.
- House conventions adopted from ORRERY/DAVE: RUN_STATE.md ledger, this ADR log,
  goldens/, no fast-math, /MT static runtime, contract-first.

## D-002 — Hoverslam guidance that lands 99.8% (2026-07-18)

Built M0–M2. The plant (dynamics/integrator/contact/mass-props) landed on first correct
implementation; all effort went into tier-0 guidance, which passed through several wrong
formulations before converging. Recorded so M5 (MPPI) and future sessions inherit the
lessons rather than re-derive them:

- **Ignition = tracking.** Ignition trigger and the burn tracking law MUST use the same
  velocity-vs-altitude reference `v_ref(h) = -sqrt(vtd^2 + 2·a_design·hgo)`. A full-throttle
  stop-distance predictor paired with a gentler constant-decel tracker demands >full-throttle
  near the ground and lands hot (first failure: 93 m/s).
- **Freeze a_design at ignition.** Recomputing `a_design = k·(T/m − g)` each tick with
  *current* mass steepens the profile as propellant drains, dropping the vehicle behind it
  into a min-throttle hover that burns to depletion and tumbles (aero-unstable at min
  throttle). Freeze it from the actual ignition mass (`st->ada`).
- **cos(tilt) throttle compensation.** Vertical thrust is `T·cos(tilt)`; dividing the
  throttle demand by cos(tilt) removed the systematic hot-arrival for off-center runs.
- **First-order lateral, not ZEM/ZEV.** ZEM/ZEV nulled position but left inward velocity →
  overshoot off-pad. A law that commands an inward velocity ∝ offset (capped) then tracks it
  is first-order (no overshoot).
- **Gentle + overdamped.** The booster is control-sluggish (gimbal ±5°, low authority).
  Aggressive lateral commands saturate the gimbal and the attitude loop oscillates (tilt
  swinging 4°→17°). Gentle lateral gains + an OVERDAMPED attitude loop (zeta=1.1) fixed it.
- **Straighten before touchdown.** Fade lateral steering to zero below ~40 m so the vehicle
  is vertical at contact; a tilted booster contacts its low foot (9 m out) ~1 m early, where
  the profile speed is still 3–6 m/s → hard/tipped. Also aim the profile at the tilt-aware
  lowest-point height in the final metres.
- **TERMINAL lateral dispersion set to sigma=30 m** (final-approach realism); large diverts
  belong to AERO_OFFSET which has the altitude/time for them.

Result: seed 42 ×1000 = 99.8% landed (0 off-pad, 0 fuel-out); seed 7 ×2000 = 99.9%.
Determinism memcmp + all physics oracles still green.

## D-003 — Intercom fleet brainstorm + lateral fix → 100% TERMINAL (2026-07-18)

Per user request, fanned out 4 Opus subagents over the Intercom bus (C:\intercom): A=entry/
fins, B=MPPI/CUDA, C=robustness critic, D=renderer. They collaborated async (B took C's
regression suite; A solved C's AERO finding; D took protocol ownership from B) and returned
cross-validated designs. Key artifacts: runs/FAILURE_TAXONOMY_C.md, runs/regression_worstcase.json
(16 frozen worst-case ICs), runs/DISTURBANCE_MATRIX_C.md, runs/sandbox/agentA_fin_model.md,
core/protocol.h (compiled+static_asserted), ui/ + shell/ scaffold (26 vitest green).

Agent C's headline finding drove the fix: the residual HARD tail was **lateral, not vertical** —
the guidance faded its ENTIRE lateral command near the ground, opening the loop mid-oscillation
so residual sideways velocity flung the vehicle off-pad. Fix: fade only the position-SEEKING
velocity target; keep the velocity-NULL damping `Kvel·(−v_xy)` active to contact. Combined with
faster position closure (Kpos 0.12→0.20, protected from overshoot by the now-persistent damping):
**TERMINAL 100% landed across seeds 7/42/99, td_v max ≤2.8 m/s, ~95% GOOD+, 0 crashes.**

## D-004 — Grid-fin plant + allocation; aero-descent is M6/MPPI work (2026-07-18)

Implemented Agent A's grid-fin model in the plant (dynamics.c: per-fin lift + roll-cant at the
mount, mandatory ω×r, transonic dip, stall) and allocation (control.c: orthogonal pitch
[−1,−1,1,1] / yaw [1,−1,−1,1] / roll [1,1,1,1] patterns, regime-scheduled gains). Added a
passive-stability oracle to selftest (deployed fins DAMP a perturbation: ω 0.150→0.114) — the
fin force SIGN is confirmed stabilizing. TERMINAL unaffected (fins stowed there), determinism
green.

BUT: powered TERMINAL ≠ unpowered high-AoA aero-descent. Holding a booster at 6–12° AoA in
supersonic flow with rate-limited (20°/s) fins is a genuine control problem — the attitude loop
limit-cycles (tilt thrashing to ~25°) so net steering ≈ 0 and AERO_OFFSET (800–1400 m divert
from 12 km) lands only ~1%. Gain-scheduling + qbar-limited AoA cap made it *stable* but not yet
*effective*. This is exactly the M6 milestone Agents A+B+C scoped as needing the E2 aero-descent
guidance pass and, ultimately, MPPI (which re-optimizes the full trajectory rather than hand-
tuned PD). Deferred deliberately rather than sunk more single-point tuning into it. Fin plant +
allocation + passive-stability oracle are the durable E0/E1 deliverables; E2–E5 + entry
supervisor are the next session's work, fully designed in runs/sandbox/agentA_fin_model.md.

Also noted (D-000-style standing rule): **C/C++/CUDA only, no Python** (user pref, memory
[[prefer-c-over-python]]); tools/ to be C/C++.

## D-005 — Plant-physics correction + two fin-control sign bugs (2026-07-18)

A boosted-model review (shared by the user) correctly diagnosed that the AERO block was
**wrong/missing plant physics upstream of control**, not a tuning problem. Verified all three
claims against the code and implemented the fix package; in the process found two more bugs.

**Plant corrections (dynamics.c) — the sim is now MORE TRUE:**
1. **CoP was backwards.** `xcp_frac` returned 0.62–0.66 L (CoP ~13 m AFT of CoM) → a strongly
   self-stable bare body, contradicting CLAUDE_v1 §5.4/§6.3 (bare body UNSTABLE; fins provide
   stability) and reality (a flat-base-leading cylinder has CoP near the leading base). Corrected
   to ~0.29–0.32 L (marginal/slightly unstable). This "stole the fins' job" and was masking the
   real control problem.
2. **Missing body pitch/yaw damping (Cmq).** The plant had only the fins' ω×r damping (ζ≈0.03) —
   a 47 m body in a fast stream has strong distributed crossflow damping. Added strip-theory
   `τ = -0.5·ρ·V·Cdc·D·J·w_perp`, J = ∫(z−z_com)²dz, Cdc=0.6 (constant `BODY_CMQ_CDC`) → ζ≈0.1.
   Passive-fin oracle ω-decay improved 0.114→0.027.

**Two fin-allocation SIGN bugs found & fixed (control.c) — the actual AERO blocker:**
Worked out the plant fin torque analytically: `τ_x = A·0.707·(δ1+δ2−δ3−δ4)`,
`τ_z = −4C·droll`. The allocation patterns had the WRONG SIGN on BOTH pitch/yaw and roll →
**positive feedback**. Pitch/yaw drove the AoA to divergence (tilt → 37°, past fin stall); roll
spun the vehicle up (ω_z → 4.5 rad/s → LOC). Fixed: pitch `[1,1,-1,-1]`, yaw `[-1,1,1,-1]`,
`droll = −tau_cmd/4C`, unit-gain divisor `4·0.707·A`. Result: the vehicle went from tumbling/
spinning to fully STABLE (closed-loop oracle max|ω| = 0.061 rad/s). Added **trim feedforward**
(cancel the body aero moment so fins hold the commanded AoA) and **damping augmentation**
(subtract aero Cmq from the controller's Kd so it doesn't double-damp and waste authority).

**New oracles:** passive-fin damping (tightened) + a closed-loop aero-descent stability check
(full fin-controlled descent, assert max|ω|<1.5) — either would have caught the sign bugs.

**Results:** TERMINAL 98.5% (seed 42) / 99.8% (seed 7) — down slightly from the old 100%
because the controller was leaning on the (wrong) free aero stability; the plant is now honest.
AERO_OFFSET: went from 0% tumbling to STABLE, but still under-steers the 800–1400 m diverts
(holds ~2° AoA vs ~6° needed; grid fins in the transonic dip from a Mach-1.2 cold start give
~300–400 m aero divert — physics-limited). Genuinely needs the ENTRY burn (decelerate to
subsonic where fins bite + more time) and/or MPPI, per Agent A/B. The boosted model's "80–90%
in one pass" over-estimated the control side, but its plant diagnosis was correct and led to
the sign-bug discovery.

**META-LESSON (both times this project stalled, the block was upstream of control):** frozen
`a_design` (D-002), then a backwards CoP + missing Cmq + fin sign errors (here). When control
tuning stops converging, STOP TUNING and re-audit the plant. Plant-first, vindicated twice.

## D-006 — 5-agent audit round: bugs fixed + AoA-hold + AERO verdict (2026-07-18)

Fanned out 5 Opus agents (P1 math-verify, P2 aero-coeffs, P3 bug-hunt, P4 divert-ceiling,
P5 AoA-hold control) over intercom, all using compiled C (no Python), each with verification
harnesses in runs/sandbox/. They cross-validated everything (3 code paths on the key claims).

**D-005 verified CORRECT** (P1/P3, round-trip torque gain +1.0000 all 3 axes): the fin sign
fixes, Cmq form + ζ=0.13-0.16, damping-augmentation, gyroscopic terms, determinism (bit-exact
300×2, zero NaN/Inf).

**Bugs found & FIXED this round:**
- **Gimbal rate-state windup** (P3 BUG-1, integrator.c): clamped gimbal position but not the
  2nd-order rate state → rate wound to 22°/s at the ±5° stop, 0.55 s reversal lash on 10% of
  landing-burn steps. Fixed: zero the rate when it drives into the stop.
- **Zero passive roll damping** (P1/P3): roll axis had NO aero damping (only RCS) → unbounded
  spin if RCS saturates. Added fin roll damping in dynamics.c.
- **Transonic CoP bump overshot CoM** (P1/P2/P3, 3 paths): the +0.03 bump made the body wrongly
  STABLE in M0.9-1.2 (hybrid, not uniformly marginal). Reduced to +0.01, base 0.28 → marginally
  unstable at all Mach per canon. (Also noted, not yet changed: xcp_frac uses VEH_STAGE_LEN while
  com is total-L — a labeling caveat; absolute CoP is correct.)

**AoA-hold control implemented (P5 "move-the-trim-point"):** the old trim-FF cancelled only the
BODY moment; the fins' own inflow restoring moment (~15× larger) was unopposed, so at zero PD
error the fins relaxed and the airframe drifted back to ~2° AoA. Fix: feedforward the fin
deflection that TRIMS at the commanded AoA (K_ff≈0.73 rad/rad, ~Mach-invariant) + slow
deflection-domain integral with conditional-integration anti-windup. Result: the vehicle now
HOLDS ~6° AoA (was 2°). Full design in runs/agentP5_aoa_hold_control.md.

**Base-first lift-direction physics (found integrating P5):** a base-first body's aero lift
points OPPOSITE the tilt (unlike thrust vectoring) — tilting the top toward +x pushes toward −x.
So the unpowered aero-descent must tilt AWAY from the pad to translate toward it: negate the
steering demand when `aero_steer` (unpowered, fins deployed, qbar>200). Confirmed: with the
negation the lateral now DECREASES (was increasing).

**AERO_OFFSET verdict (P4, compiled C divert integrator):** even with a perfect AoA-hold the
physics ceiling is ~237-313 m aero + ~550 m burn ≈ 787-864 m; velocity-null eats ~1/3; the 3σ
tail (1550 m) is beyond reach WITHOUT an entry burn. The old 800/250 dispersion was not
well-posed. Retuned AERO_OFFSET to mean 500 / σ 150 and raised the AoA cap (P4: real STRUCT limit
15°, qbar only peaks 36 kPa, so the 3-8° cap was over-conservative → 6-12°).

**AERO_OFFSET STILL 0% land** despite all the above, because of a NEWLY-EXPOSED coupling: during
the powered landing burn at 5 km/300 m/s with fins deployed, the passive aero lift (opposing the
thrust-vector tilt) FIGHTS the gimbal steering and pushes the vehicle off-pad (lat grows during
the burn). This is fundamentally the **entry-burn problem** — the entry burn (E3, not yet built)
decelerates the vehicle so the landing burn happens at low qbar where thrust dominates. So AERO/
ENTRY remain M6 work: build the entry supervisor (E3) + the divert sequencing (P5 §5.1: hold max
AoA high → reverse to null lateral velocity → hand off with v_xy≈0), and handle the burn-phase
aero/thrust transition. TERMINAL stays ~97-98% (honest corrected plant), all oracles green.

## D-007 — Entry-burn supervisor (E3) + renderer --serve integration + burn-phase aero steering (2026-07-18)

Session (Opus 4.8, continuing D-006) ran 4-A (entry burn) and 4-C (renderer) CONCURRENTLY per
operator "go BOTH/all", after a cross-session intercom liaison handshake with the still-live prior
session: all calibration points (baseline, AERO/entry-burn blocker cause, recommended path) AGREED,
zero contradictions; confirmed deltas (AERO dispersion already 500/150, thermal path live+committed,
4-C elevated to co-primary). Baseline reconfirmed on the current binary: selftest 10 oracles PASS,
TERMINAL 97.0% (s42) / 98.6% (s7), determinism memcmp green.

**4-A — Entry-burn supervisor E3 (NEW; sim.c `entry_supervisor()` + guidance_hoverslam.c
`entry_predict_peak_qbar()`):** forward-shoot ballistic (no-thrust, drag CA≈1.0) vertical channel
predicts peak qbar to touchdown. Above hoverslam: PH_COAST → ignite 3-engine retrograde full-throttle
when predicted peak ≥ 72 kPa → cut to PH_AERO when predicted-remaining ≤ 68 kPa OR fuel ≤ 7 t floor.
Ignition latch amended so PH_ENTRY_BURN does NOT get forced to LANDING_BURN and does NOT freeze the
suicide-burn `ada`. Inert for TERMINAL/AERO (predict < ignite → gcmd/state untouched → TERMINAL
bit-identical, determinism preserved). **RESULT: ENTRY STRUCT_FAIL ELIMINATED (100/100 → 0);** peak
qbar 105→~39 kPa. This is the durable M6 E3 deliverable. **ENTRY still 0% land, now fuel-bound**
(92% fuel-out): the entry burn kills v to 156 m/s at 40 km (23 t), the vehicle RE-ACCELERATES to
605 m/s by 17 km (gravity beats drag in thin air — the deceleration is largely regained), and
hoverslam ignites a 57 s MIN-throttle landing burn from 17 km that runs dry. 45 t-prop diagnostic →
29% fuel-out + 71% off-pad, so fuel-efficiency AND the 3 km lateral divert both bind. Genuine
trajectory optimization = **MPPI-class (4-B)**; the E3 supervisor is the constraint layer MPPI sits
under. Constants (local, to consolidate): ENTRY_QBAR_IGNITE 72k, ENTRY_QBAR_CUT 68k, ENTRY_FUEL_FLOOR
7 t, ENTRY_PRED_CA 1.0.

**4-A — Burn-phase aero steering (control.c), the AERO_OFFSET fix:** instrumented `--run` verbose
with signed radial velocity `vrad` (+ = outbound) + qbar. Root cause of the AERO_OFFSET off-pad:
the OLD negation applied only when UNPOWERED; during the early landing burn at high qbar the aero
lift (opposes tilt) FIGHTS the gimbal (tilts toward pad) → net outbound push → the vehicle overshoots
inbound then drifts back off-pad. Fix: replaced the binary unpowered-only negation with a SMOOTH
signed authority factor `steer_sign = (thr − qbar·Aref·CNa)/max(thr, aero) ∈ [−1,1]` — +1 thrust-
dominant (tilt toward pad), −1 aero-dominant (tilt away), through 0 at the ~22 kPa crossover where a
body tilt makes no net lateral force. A HARD flip caused a tilt-reversal transient (body still tilted
the old way while the newly-dominant effector shoved it outbound); the smooth blend fixed it. Lowered
inward-velocity cap vlat_max 40→25. **RESULT: AERO_OFFSET now well-behaved** (lands ~150 m out vs
340; outbound vrad +4.7 vs +18.7; no wrong-way push) but **still 0% land** — residual is DIVERT-
AUTHORITY limited: the terminal divert is tilt-cap-limited (~2.1 m/s² at the 12° AoA cap) so it can't
close the last ~150 m before touchdown. Needs more aggressive aero-descent divert and/or MPPI.
TERMINAL unaffected (fins stowed → steer_sign ≡ 1; verified 97.0/98.6% both seeds).

**4-C — Renderer telemetry stream (INTEGRATED; was Agent D groundwork).** Built by a background
subagent in an ISOLATED source-tree copy (`_serve_wt/`, since this is not a git repo → no worktrees),
then merged into the real tree — files DISJOINT from 4-A's (`ws.{h,c}` new; `main.c` +`--serve`/
`--golden`; `constants.h` +PC_REF 9.7 MPa; `protocol.h` +BlHello(72B)/BlStats(48B); `CMakeLists`
+ws.c +ws2_32). `ws.c` = minimal RFC6455 subset (single client, self-contained SHA-1+base64
handshake, unmasked binary frames, PING/CLOSE), Win32 Winsock2. `--serve` waits for a client, sends
HELLO, streams TLM@125 Hz + EVT + STATS@10 Hz, real-time paced (wall-clock ONLY in the serve loop,
never integration — directive 3). Verified: selftest PASS + TERMINAL 97% byte-identical (determinism
intact), WS end-to-end 16/16 (RFC6455 published test vector validates the hand-rolled crypto),
goldens frozen `goldens/protocol/{hello,tlm,evt}.hex`. **M3 socket path is LIVE** — `pnpm -C ui dev`
against `booster-core --serve` can now render a live descent.

**Also:** repo pushed to public GitHub (github.com/bochen2029-pixel/booster-lander-simulator, MIT,
README) via a background subagent (build/ + _serve_wt/ excluded, secret-scanned clean).

**Net:** TERMINAL 97-98% (unchanged, both seeds); ENTRY STRUCT eliminated (entry burn = durable E3);
ENTRY-land + AERO_OFFSET-land are authority/fuel-limited → MPPI (4-B); renderer streaming live (M3).
Determinism + 10 oracles green throughout. **META (plant-first, 4th time):** instrumenting `vrad`
turned an opaque "drifts off-pad" into a precise crossover sign-flip diagnosis — measure the plant,
don't guess the controller.

**D-007 addendum (same session, continued autonomous push):** Added the **aero-aware suicide-burn
ignition** (guidance_hoverslam.c `suicide_burn_margin()` — a THRUST-ONLY forward-shoot, since the
retropropulsive landing burn is SRP-drag-shielded; crediting drag ignited too late and slammed in at
157 m/s): the fins-deployed landing burn now COASTS (free aero decel) until a full-thrust burn would
just arrest above the ground, then lights HARD (ada 0.85) and low. This **eliminated ENTRY fuel-out
(92%→0)** and cut the AERO residual (coast now closes 843→119 m before ignition; more within 200 m).
Both scenarios are now cleanly **DIVERT-LIMITED** (soft, off-pad, zero fuel/STRUCT). CONFIRMED
tier-0 AERO/ENTRY landing is UNACHIEVABLE: the fin-rate-limited attitude reversal (~56 m of travel
per reversal > the 26 m pad) + the aero/thrust crossover dead zone cannot null the cross-range
velocity at the pad — a clean 500 m offset (run 0) overshoots to 610 m. Launched **two parallel
Opus MPPI-HIER CPU builds** (tournament, on intercom lanes mppi-build/mppi-build2) — MPPI's
forward-planning of the bang-bang reversal is the only path. TERMINAL 97-98% / determinism / 10
oracles green throughout. Also fixed the landing-burn a_design to a hard 0.85 for fins-deployed
handoffs (short burn, less gravity loss). Tuning constants (local, consolidate later):
LANDING_IGNITE_MARGIN 150 m, fins-deployed base_frac/ada 0.85, divert A_DECEL 2.5 / vlat 35.

**D-007 addendum 2 (autonomous push cont'd — FIRST AERO LANDING + MPPI coaching):**
- **DIVERT RECIPE → first AERO_OFFSET landing ever.** (i) tilt cap raised 12°→15° for the aero divert
  (gated `env->fins_deployed`; TERMINAL keeps 12°) — the STRUCT check is qbar>80 kPa and aero qbar
  peaks ~36 kPa, so 15° AoA is safe and buys ~3.2 m/s² lateral (12° gave only ~2.5, too weak to null
  v_xy at the pad). (ii) decelerating profile `vdes=√(2·A_DECEL·r_pred)`, A_DECEL 1.5 conservative,
  with a **velocity LEAD** `r_pred=r+v_rad·2s` (crude pre-emptive reversal to beat the ~2 s fin-rate
  attitude-reversal lag). Result: AERO_OFFSET td_lat median 328→136 m, min 111→**9 m**, and **1-2/300
  LANDED cross-seed** (s42/7/99), soft (td_v 3.4-3.8). Tier-0 is now VARIANCE-limited (IC sensitivity
  the reactive law can't beat — the run that lands vs the median differ only by initial attitude azimuth
  vs offset direction). **This is MPPI's job** and confirms the physics is landable.
- **ENTRY entry-burn banking = CATASTROPHIC without null-out.** Banking the 3-engine burn toward the
  pad (a_lat≠0) sent ENTRY from ~2 km to ~17 km off-pad: the huge-authority burn builds ~100 m/s
  cross-range that the descent never nulls. Reverted to pure retrograde. ENTRY's 3 km offset needs the
  full P5 §5.1 bank-then-REVERSE sequencing (MPPI/4-B), not a naive bank.
- **INJECT_DISTURBANCE (F4) implemented.** EnvCtx `thrust_scale/isp_scale/com_offset` (0 read as
  nominal → oracles bit-identical), applied in dynamics.c (thrust, Isp, thrust-misalignment torque),
  seeded per (seed,run) in sim_init under `MOD_INJECT` (`--inject`). Deterministic/replayable. Verified
  **TERMINAL passes Tier-B** (thrust/Isp/CoM): LANDED 97.3-98.1%, GOOD+ 88.9-89.4%, td_v p95 3.5/p99
  ≤4.5 across s42/7/99 — meets Agent C's bar. (Full Tier-B also wants NAV_NOISY + 12 m/s gust; not yet.)
- **MPPI tournament coaching (main session = intercom coordinator d3mc3czv).** Two Opus MPPI-HIER CPU
  builds (mppi-build zzskby1w, mppi-build2 t8oy4jpy) hit the exact tier-0 wall, then the classic MPPI
  softmax-collapse (costs 1e7 from |r|²+CRASH=1e6 → ESS=1, λ railed). Coached the fixes that unlocked
  them (both confirmed): **WARM-START the mean with the hoverslam recipe** (search corrections, not
  discover from noise); LINEAR |r_xy| + BOUNDED/saturated crash cost; hard |v_xy| + asymmetric
  outward-vrad penalty; σ_alat≈1.5-2.5 clamped to the ±3.2 gamut (big σ tumbles rollouts); small fixed
  GAMMA_IS tie-breaker (servoed λ swamps state cost); and a **lateral terminal cost** (propagate
  r_xy+v_xy to predicted touchdown via the forward-shoot / ZEM `r+v·t_go`) so the 5 s horizon "sees"
  the burn-phase drift. Both now null to ~7 m; integrating the winner's guidance_mppi.c when it clears
  AERO_OFFSET ≥90%. Baselines frozen: goldens/mc/{terminal_s42_baseline, aero_pre_mppi_s42}.txt.

## D-008 — MPPI HIER controller integrated into the real tree (2026-07-18, ~10am CST)

Both Opus MPPI subagents (D-007) converged on the same architecture after coaching, then STALLED and
were killed by the harness watchdog mid-batch — but **MPPI-2's 614-line `guidance_mppi.c` was preserved
on disk** with every piece implemented. The main session **took over and integrated it** rather than
rebuild.

**Architecture (lateral-only HIER MPPI — MPPI-2's key simplification):** the proven hoverslam suicide
burn owns the VERTICAL (throttle/ignition, 97% TERMINAL); MPPI owns ONLY the cross-range a_lat (2
world-lateral channels; throttle σ=0). Every rollout is vertically-correct so the cost is dominated by
the lateral outcome MPPI optimizes. Rollout = `control_step` + `dynamics_deriv` (shared plant) at RK4
25 ms, warm-started on the hoverslam `a_lat` (sqrt-decel + velocity-lead), K=256, OU σ_alat=1.5. Cost
(NORMALIZED so the softmax stays healthy): converging-velocity tracking + asymmetric outward penalty +
**ZEM foresight terminal** `T_ZEM·|r_xy+v_xy·t_go|²` (sees touchdown past the 5 s horizon) + strong
touchdown `TD_RXY/TD_VXY` + BOUNDED/clipped crash (800/20000) + adaptive λ (ESS-servoed). Replan @10 Hz,
cheap knot-emit between; lean in-rollout vertical model → ~9 s/run with OpenMP.

**Integration (real tree):** copied `guidance_mppi.{c,h}`; GM_MPPI block in sim.c (E3 `entry_supervisor`
ABOVE MPPI → `mppi_step`/`mppi_execute` → the SAME ignition latch — guarded by `GM_MPPI` so GM_HOVERSLAM
is byte-for-byte untouched); `MppiState mppi` in Sim; `--mppi` flag (headless+run); `guidance_mppi.c` +
`find_package(OpenMP)` in CMakeLists. Plus the **flat-15° qcap** in control.c (raises the divert ceiling
MPPI-2 found limiting the far seeds; sim STRUCT is qbar>80 kPa NOT AoA, aero-descent peaks ~36 kPa → 15°
safe). MPPI reuses my `control_step`/`hoverslam_step`, inheriting the recipe (aero-aware ignition, 15°
cap, smooth crossover steer).

**Verified:** build clean (OpenMP on); **`--selftest` PASS → GM_HOVERSLAM byte-identical, determinism
intact**; single AERO run under MPPI diverts run 1 (843 m far seed) → **88 m** vs tier-0's 158 m.
AERO_OFFSET landed-rate batch under `--mppi` in flight (headline → RUN_STATE). **Physics caveat (MPPI-2,
confirmed):** the aero-divert ceiling from 12 km is ~500-600 m even with 15° flat + optimal control, and
AERO_OFFSET is mean 500 σ150, so the far tail (>~600 m init) may be genuinely unlandable regardless of
controller — the honest rate may cap below 90% (a physics ceiling, cf. the 800→500 retune in D-006),
still a massive 0%→landing win. Open lever: raise the conservative warm-start (A_DECEL=1.3/VLAT=30) for
the far seeds — MPPI's TD_VXY nulls the resulting overshoot. Run: `--headless --scenario aero_offset --mppi`.

## D-009 — Post-batch truth-up, session archaeology, and the AERO landing plan (2026-07-18, afternoon)

**Truth-up of D-008/RUN_STATE (both were written while the first MPPI batch was still in flight and
never updated):** the 60-run AERO_OFFSET `--mppi` batch finished **0/60 LANDED** (59 off-pad, 1
too-hard; only on-pad run43 at td_v=7.12). Two tuning experiments followed and were REVERTED:
(a) aggressive warm-start A_DECEL 1.3→2.2 / VLAT_MAX 30→48 → reaches 46-103 m but HARD td_v 6.25-8.60
(divert tilt near the ground compromises the vertical arrest); (b) execution fade h/400→h/150 →
td_v 11.1-12.0, WORSE — the fade must be HIGHER, not lower. File restored to MPPI-2's baseline
(A_DECEL=1.3/VLAT=30/fade=h/400) + a NOTE block; integration verified (selftest PASS, TERMINAL 97.0%,
MPPI run-twice bit-identical). Until now HANDOFF_2026-07-18.md §4 was the only record of this;
appended here as the honest ledger entry (append-only correction; D-008's text is left as-written).

**Also for the record (was transcript-only):** a SECOND entry-burn banking experiment ran ~08:34-08:37
and was reverted: the control.c PH_ENTRY_BURN `a_vert_ref` override (use the true ~40 m/s² burn accel
so a_lat→tilt maps correctly; without it a_lat over-produces lateral ~3.4×) PLUS a velocity-nulling
sqrt-decel bank (A_CROSS=4, 4 s lead, Kvel=0.5, vdes cap 250) → ENTRY td_lat median **2363 m — WORSE
than pure retrograde's 2050 m** (reduced retrograde decel + un-nulled v_xy at the cut). So the
a_vert_ref fix alone does NOT rescue banking; timing the null to the burn CUT is the coupled hard part
(MPPI-class), as the sim.c comment records. Also was-transcript-only: ENTRY_PRED_CA 1.0→1.5 made ENTRY
fuel-out WORSE (92→96%) — **a bigger entry burn is more fuel-efficient than a smaller one** (an earlier
cut forces more low-altitude arresting where gravity/Isp losses dominate).

**Session archaeology (this session; 2 fleets × 5 Opus readers over intercom):** both prior sessions'
exported transcripts were chunked (C:\chunker) and reconstructed line-level. Founding session
01:42-06:10 (lane wlmxyeow: D-001..D-006, liaison handshake, intercom v0.2.1 lane fix + LP-1).
MPPI-push session 05:47-11:21 (lane d3mc3czv: D-007/D-008/HANDOFF), died on context ("Prompt is too
long") seconds after the HANDOFF Write; its transcript has a **2h11m stream-idle-timeout blind spot
08:41→10:52** recoverable only from the intercom broadcast.db. MPPI-1 (`mppi-build` zzskby1w,
`_mppi_wt`) was still editing at 11:14:29; its 6× serial-stability verdict, NaN-hardening, and
|r_xy|+ZEM belt-and-suspenders edit are being adjudicated this session (background agent).

**Diagnosis — why the last session circled (D-009 core):**
1. **The divert ceiling was never measured.** MPPI-2's ~400-500 m vs P4's 787-864 m disagree ~2×
   (candidate causes: the execution fade, the late aero-aware ignition shrinking the burn-divert
   window, cap differences, velocity-null accounting). mean-500 σ150 is well-posed under one estimate
   and ill-posed under the other. A compiled-C oracle bang-bang study (`runs/sandbox/ceiling.c`) is
   measuring it now; the dispersion will be retuned to the measured number (ADR), like D-006's 800→500.
2. **"Arrive CENTERED (r≈0 AND v_xy≈0) by ~400 m" was articulated but never implemented** — only
   warm-start scalars and the fade were tuned; the cost function that would enforce it was untouched.
3. **The D-003 lesson repeated at the MPPI layer:** the execution fade `s=(h/400)²` scales the ENTIRE
   MPPI a_lat to zero near the ground — including velocity damping — so residual v_xy rides to
   touchdown. That is why near-seeds hit dead-center (lat=0) yet crashed at td_v 6-8, and why every
   reach-vs-arrest tuning point failed on one horn or the other. D-003's fix ("fade only the
   position-seeking term; keep the velocity-null damping active to contact") applies verbatim.

**The plan (executing now):** (i) fade becomes a BLEND in both execution and rollout (directive 7):
`a_lat = s·u + (1−s)·(−KV_RES·v_xy)`, gamut-clamped — position-seek fades, damping persists to
contact (the control.c altitude tilt cap keeps it touchdown-safe exactly as for tier-0). (ii)
Front-load cost: altitude-ramped running |v_xy| penalty + a one-shot GATE cost at h=400 m on |r_xy|
and |v_xy| (the centered-by-400 m objective, placed where MPPI can act on it). (iii) Warm-start
parity with the PROVEN tier-0 landing recipe (A_DECEL 1.3→1.5, VLAT_MAX 30→35). (iv) Scenario
dispersion retune per the measured ceiling. Every step gated by: selftest PASS, TERMINAL 97.0%
(s42/200) unchanged, MPPI run-twice bit-identical.

**D-009 addendum (same session — ceiling measured, MPPI-1 adjudicated, first MPPI batch landings):**
- **CEILING RESOLVED** (`runs/sandbox/ceiling.c`, compiled-C optimal divert; dt-converged 5 sig figs,
  solver self-tests 0.00% vs closed forms): the PHYSICAL max landable offset from 12 km at the 15°
  cap WITH the execution fade is **D_phys ≈ 1107 m** (velocity-capped at vcap=30 m/s; 3×3 dispersion
  worst corner 1033); no-fade 1291; centered-by-400 profile 1054; 12° cap 1062. The free bang-bang
  "ceiling" (2492 m) is a math artifact (builds 125 m/s cross-range — violates the small-AoA lift
  model and the guidance's velocity cap). **D_phys is ~LINEAR in the inward-speed cap** (vcap
  15/20/25/30/40/50 → 603/781/949/1107/1396/1647 m): the load-bearing lever is VLAT_MAX plus how
  hard the profile decelerates — NOT raw tilt authority. RECONCILIATION of the old 2× disagreement:
  MPPI-2's ~400-500 = the as-built controller realizing ~60-80% of D_phys (correct for the
  implementation); P4's 787-864 = optimistic sum of non-additive terms (fixed 4.6 km ignition + an
  un-faded separately-budgeted 550 m burn divert). **Scenario verdict: AERO_OFFSET mean-500 σ150
  (p90=692 m) IS physically well-posed** (0.85·D_phys = 941 m) — the honest fix is pushing the
  controller toward D_phys, not shrinking the scenario. Retune to ~350/σ100 ONLY if the controller
  plateaus below the dispersion (decision after the config batches).
- **MPPI-1 adjudicated** (read-only agent over `_mppi_wt` + the task store + the bus): KEEP the
  integrated MPPI-2. MPPI-1 is an independent re-implementation that CONVERGED on the same
  architecture (validating the design) but never reached the pad (best 100-155 m off, soft); its 6×
  serial stability hammer NEVER completed (task killed after the header — no 6/6 verdict exists);
  the "memory fault, not NaN" conclusion is UNVERIFIED; its one concrete crash signal was
  OpenMP-linked in ITS OWN build. Ported its free hardening: NaN-safe `clampd` on the ubar clamp,
  `!isfinite` cost sanitize (→COST_CLIP), and the linear |r_xy| terminal anchor (T_RXYD=3) alongside
  ZEM. Open follow-up: a 6× serial-vs-OpenMP stability A/B on the integrated tree (it has 70+ clean
  runs; risk low but unproven).
- **Gap window recovered from the bus:** 08:41→10:52 contains ZERO messages — genuine fleet silence,
  not lost capture. MPPI-2's last pre-gap word (#1052) "aero-divert ceiling ~400 m" is now superseded
  by the measured 1107.
- **Package results (every step: selftest PASS, TERMINAL 97.0% s42/200 unchanged, MPPI run-twice
  bit-identical):**
  - **pkg1** (fade→BLEND into the tier-0 arrest + gate@400 G60/60 + Q_VLOW 6 + warm-start 1.5/35 +
    NaN guards): **2/60 = 3.3% LANDED — the first MPPI batch landings ever** (both HARD, td_v
    4.47/4.55, lat ≤10 m). Too-hard crash cause 1→0: ALL 58 misses are purely off-pad (miss median
    127 m, 37/60 within 150 m). The soft-arrest horn is CLOSED; what remains is centering/reach.
  - **#4 persistent-corrections experiment: REGRESSED and REVERTED** (landing seed 15→91 m). Root
    cause: the warm-start baseline is CLOSED-LOOP (recomputed from the corrected state each replan),
    so a correction carried across replans double-counts — accumulation happens through the PLANT,
    not the plan. One-shot correction + strong per-replan cost gradients is the correct pairing for
    a closed-loop baseline (recorded in-code at the warm-start call).
  - **pkg3** (pkg1 + G90/90 + Q_VLOW 12 + T_RXYD 3 + reach 1.8/40): singles mixed (2 better/4 worse
    — single-seed noise near the pad); 60-batch comparison vs an oracle-informed 2.2/40 config in
    flight. Config selection by batch rate + miss distribution, then the dispersion decision above.

**D-009 addendum 2 — the floor is WIND; ENTRY's divert is SOLVED in still air (same session, cont.):**
- **MPPI cost-inertness chain resolved:** pkg3 batched 0/60 (the running Q_VLOW absolute-|v_xy|
  penalty fights the cruise phase of the optimal trapezoid — ZEROED); gate-at-400m and gate-at-
  ignition cost variants were byte-inert because **event gates inside the 5 s horizon never fire
  until the final seconds** (only analytic horizon-end projections steer all-descent); the
  touchdown-anchored ZEM **rewards ballistic collision courses that drift through the ignition/
  crossover dead zone still carrying v_xy** → re-anchored at the ignition gate above it (config D);
  and the ±8 ubar clamp **railed the executed plan beyond the ±3.2 physical gamut for the first
  ~11 s** (every rollout saturating identically at the plant = zero softmax gradient; MPPI_DBG
  measured alat=-8.00 for 110 replans) → clamped to A_LAT_GAMUT=3.2 at the baseline AND the update.
- **ENTRY ZEM/ZEV collision-course bank IMPLEMENTED** (design runs/d009_entry_divert_design.md;
  `entry_divert_step` in sim.c + the gated PH_ENTRY_BURN a_vert_ref fix in control.c): closes the
  3 km divert 2050 m→**~150 m median (12×)**, soft, fuel-positive, STRUCT 0. Kgain journey: uniform
  Kg=6 overshoots (crosses r=0 at ~21 km still −29 m/s, med 148 tight); Kg=1 preserves timing but
  under-drives far seeds (med 536, **FIRST ENTRY LANDING EVER: 1/100, HARD 4.33 @ 14.4 m**); split
  OVERDAMPED KR=2/KV=3.5 (the D-002 ζ lesson) = tight med 145 without the wide tail. **SHIPPED.**
- **THE UNIVERSAL ~140 m FLOOR IS MEAN-WIND DRIFT — proven by falsification.** With uref
  temporarily 0 (spec winds restored immediately after): **ENTRY 32% LANDED, median miss 5 m,
  49/50 inside the pad; AERO tier-0 71% LANDED, median 6 m, 93/100 on-pad.** Every guidance layer
  works nearly perfectly in still air. With spec winds (6-8 m/s uref → ~13-18 m/s at altitude) the
  wind's aero side-force (~1.6 m/s² at 20-40 kPa) + the thrust×(relative-wind-trim tilt) component
  at light-up (~1.7 m/s²) leak through the ignition/crossover windows as a systematic drift.
- **Fixes tried, floor unmoved (~147):** ignition-attitude FEATHER (straighten before light —
  fails because "zero AoA" in crosswind = aligned with RELATIVE wind = ~3.7° from vertical; the
  body cannot be simultaneously unloaded and upright), 2 s post-ignition steering hold (gives the
  wind a free window), PH_LANDING_BURN integral trim Ki=0.004 (gated after the hold — too late,
  and muted through the crossover band). A dedicated root-cause agent is decomposing the transient
  (thrust×tilt vs shielded-aero side-force vs steer_sign zero-band) with instrumented traces; top
  candidate fixes: upwind pre-bias via the NAV-LEGAL attitude-based wind inference (tilt−vertical
  vs v_inertial encodes the relative-wind direction), damping-through-ignition, earlier ignition
  margin, ignition-transient feedforward (counter-tilt = −current-tilt × ramping thrust).
- **Gates held throughout:** selftest PASS and TERMINAL 97.0% (s42/200) byte-stable across every
  build in this arc; ENTRY/AERO changes are fins-deployed/PH-gated by construction.

**D-009 addendum 3 — THE WALL FALLS: un-shielded grid fins were the wind floor (plant correction):**
- **Root-cause chain (instrumented probe agent + trace arithmetic):** the ignition-transient probe
  (runs/ign_probe.c linking the frozen Release objects) measured the landing burn commanding
  **−84 m/s** of lateral correction while the vehicle realized **−8%** of it, split into (Stage 1) an
  un-shielded crosswind aero seed of +3.5-4.6 m/s during the thrust ramp (the SRP C_T blend does not
  engage until ~0.75 s after light) and (Stage 2) an authority collapse: control.c's steer_sign
  crossover compared thrust against UN-shielded aero authority (830 vs 790 kN → sign ~0 → steering
  muzzled all burn) and a_vert_ref=G0+2 clamped burn amax to 3.16 m/s². Fixes #1 (shield-weight the
  steer_sign aero term by the plant's own C_T blend), #2 (true-specific-force a_vert_ref for the
  fins-deployed landing burn), #3 (damp-through-ignition replacing the zero-hold) landed — but only
  moved the median 147→140: the mid-burn outward push persisted at ~1.3-1.6 m/s².
- **The remaining force was arithmetically too large for shielded body aero + wind → grep confirmed
  a CANON VIOLATION in the plant: dynamics.c applied the §6.3 SRP shield ONLY to body CA/CN; the
  grid-fin block computed full per-fin forces from the wind-inclusive flow with NO shielding —
  despite the fins being the farthest-DOWNSTREAM surfaces in the plume-enveloped wake. Worse, the
  un-shielded fin side-force acts at FIN_Z=45 m — a huge arm that re-trims the vehicle downwind
  against the attitude loop, so the 830 kN thrust follows it. Canon §6.3 says "aero forces blend out
  with C_T", not "body only". FIXED: srp_shield hoisted and applied to fin lift + tangential force.**
- **RESULT (full spec winds, all gates green — selftest PASS, TERMINAL 97.0% byte-stable):**
  - **ENTRY 62 km→pad: 50/100 = 50.0% LANDED** (49 HARD 1 GOOD; miss min 1.3 / med 23 / p90 33 m;
    99/100 within 50 m; STRUCT 0, fuel-out 2). Was 0% since inception; first landings ever were
    1/100 earlier this same session. The 38 "off-pad" misses sit at 26-33 m — grazing the pad line.
  - **AERO_OFFSET tier-0: 181/300 = 60.3% LANDED** (miss med 19 m, 225/300 inside the pad;
    off-pad 73, too-hard 40, fuel 2). All-time best before this: 1/300 (0.3%).
  - The ENTRY stack that delivers this: E3 supervisor (D-007) → ZEM/ZEV overdamped collision-course
    bank (this session) → 87 s coast drift → aero-descent trim → aero-aware thrust-only ignition →
    feather + damp-through-ignition → fin-shielded burn with true-a_vert_ref steering + wind-trim
    integral. Meta-lesson (plant-first, SIXTH vindication — and the first where the plant was too
    PESSIMISTIC): when control refuses to converge, audit the plant; this time the missing physics
    was suppressing a real capability, not faking one.
- **Remaining tails (next session's tuning, not architecture):** ENTRY/AERO too-hard tail (td_v ~6-8
  on runs arriving uncentered — KV_RES/arrest interplay), the 26-33 m grazing band (wind-trim gain +
  Kvel tuning), MPPI re-batch + cross-seeds in flight; then M4 gate ≥90% assessment vs the mean-500
  dispersion (the measured D_phys≈1107 m says mean-500 σ150 is physically well-posed).

**D-009 addendum 4 — MPPI takes the lead; tail probes reverted; review-driven repo upgrades:**
- **Cross-seed validation of the fin-shield baseline:** ENTRY 41%/45% (s7/s99, vs 50% s42); AERO
  tier-0 55% (s7, vs 60.3% s42); ENTRY + MPPI determinism pairs bit-identical. All real, no seed luck.
- **MPPI warm-start parity retune (A_DECEL 2.2/40 → 1.5/35, matching the tier-0 landing recipe on
  the now-honest plant): 38/60 = 63.3% — MPPI beats tier-0 (60.3%) for the first time** (PERFECT 1,
  GOOD 6, off-pad only 15). MPPI golden refreshed (goldens/mc/aero_mppi_s42_d009_baseline.txt;
  supersedes the interim 29/60 measured with the pre-parity warm start the same day).
- **Tail-tuning probes REVERTED to the golden baseline** after honest measurement: cycle 1 (Ki
  0.012 + 1 s engage + LANDING_IGNITE_MARGIN 150→220) traded off-pad for too-hard/fuel (ENTRY 51%,
  AERO 52%); cycle 2 (+near-ground trim fade, margin back to 150) = ENTRY 49% / AERO 57.3%. Both net
  ≤ baseline (50/60.3): the grazing-band and td_v tails INTERACT — they need a systematic parameter
  sweep (or the M5 CUDA MPPI's capacity), not single-knob probes. Values + reasoning recorded in-code.
- **External-review-driven repo upgrades (all shipped this push):** README gains the DISPERSION
  TABLE beside the Wilson CIs (the reviewer's single-highest-value edit), an explicit determinism
  SCOPE (fp64 CPU plant bit-identical everywhere; CPU MPPI bit-identical under OpenMP; future GPU
  rollouts per-arch bit-stable with toleranced CPU↔GPU parity — never a cross-device bit claim), an
  honest-idealization paragraph (guidance reads truth state; NAV_NOISY specified-not-built; what
  --inject already disperses blind), and a public-mirror provenance note. NEW: `.github/workflows/
  ci.yml` — third-party attestation (clean windows-latest runner: build, 10-oracle selftest incl.
  the determinism memcmp, TERMINAL 200-run ≥90% gate, ENTRY/AERO informational spots). Moved
  `_events.xlsx` → `data/reference/events.xlsx` (canon layout). Answered the reviewer's
  quantization question with data: the TERMINAL hard tail is LATERAL-coupled (corr(td_lat,td_v)
  ≈ +0.64, Agent C) — the 50 Hz ignition-quantization bound is ~0.2 m/s, an order below the tail.
  .gitignore hardened: session-transcript exports, chunker output, agent worktrees never publish.

## D-010 — Graphics-review deltas adopted into canon (2026-07-18, evening)

An external graphics review (operator-relayed) independently re-derived ~80% of canon §11–§12 —
plume-as-hero with HDR/AgX rolloff, propagation-honest audio, ground-effect, soot states, director
cameras, guidance-mind visualization, and the ASDS deck-motion-must-live-in-the-plant flag (already
satisfied by the core-side SEA module §4.4 + HELLO-exported spectrum §11.9). Convergence noted as
validation; the DELTAS are hereby adopted as canon amendments (the §0 epigraph mechanism):

1. **Predicted-impact-point diegetic marker** (the review's best idea, now cheap): stream the
   guidance's ZEM/ZEV impact projection + t_go + ignite_h (all computed since D-009) and draw a
   marker that converges onto the pad as the solve tightens — "it actually solved it," visible from
   62 km. **Protocol plan (deferred to the M7-prep session so the TS mirror + goldens are updated
   as one validated unit):** add `pred_impact[2] (f32)`, `ignite_h (f32)` to BlTlmFixed, bump
   BL_PROTO_VERSION, update static asserts + decode.ts + offset tests, re-freeze
   goldens/protocol/*.hex (this ADR pre-authorizes that re-baseline).
2. **LOX frost band + GOX vent wisps**, telemetry-honest: the frost line IS m_lox through the HELLO
   tank geometry (§5.2 column model) — directive-8 legal, added to §11.5's detail states.
3. **Grid-fin entry glow** keyed to the streamed qdot_heat (titanium fins; real F9 behavior) —
   added to §11.5. (Bells still never glow — regen-cooled rule stands.)
4. **M7 "first light" internal ordering = the review's 80/20:** plume → propagation-delayed sound →
   ONE volumetric cloud-deck punch-through at 2-5 km (promoted from §11.8 weather-preset garnish to
   a first-class beat: the strongest visceral altitude cue in the 70 km→1 m shot) → one great
   long-lens ground camera. Reference target unchanged: indistinguishable from LZ-1 tracking footage.
5. **Shadows scoped to a near-ground cascade** so the floating-origin rebase never fights the
   shadow frusta (practical note; composes with §11.12's budget).
6. Noted for the record: the SRP plume envelopment (§11.6) and the D-009 fin-shield physics are the
   SAME phenomenon — the renderer drawing the envelopment correctly will be visualizing the exact
   mechanism that hid the landing for three sessions.

M7 remains gated behind M6 (ENTRY ≥90%) per directive 10; the ui/ scaffold (decode/frame/interp
vitest green) and `--serve` are ready, and `ui/src/fx/plume.ts` already holds the typecheck-clean
raymarched plume node. First light requires a real WebGPU browser (headless preview hangs).

**D-010 addendum — fleet results composed: ENTRY 85%, AERO 71.7%, MPPI 68.3%, NAV layer live:**
- **16-config sweep** (runs/d010_sweep.csv; gates held on every row — selftest PASS + TERMINAL
  exactly 194/200): Kvel(fins) 0.9 dominates every pairing; winner **C14** = KI_WIND 0.012 + trim
  output-fade + margin 150 + Kvel 0.9 → ENTRY 80/AERO 69.3. The interactions the single-knob
  probes couldn't see: the strong trim only wins when its near-ground output FADES and Kvel is 0.9
  (identical trim at Kvel 1.2 = C13, 109 vs C14's 149).
- **KVEL agent** cross-seed-validated 0.9 (ENTRY 69/76/60 s42/7/99) and mapped the value sweep;
  **TOOHARD agent** instrumented the too-hard cohort — 100% LATERAL (|vz|≈2.4 soft in 63/63;
  ignition-margin exonerated), diagnosed the one-gain-two-jobs conflict, and delivered the tested
  **HEIGHT-SPLIT null gain** (divert 0.9; −v_xy damping ramps to 1.6 below 250 m). Two independent
  methods + the MPPI batch converged on the same mechanism (predicted 17→23→30 too-hard trend
  measured exactly).
- **COMPOSED (real tree): ENTRY 85/100 s42 (77 s7 / 76 s99; off-pad 5, fuel 3, STRUCT 0) · AERO
  tier-0 215/300=71.7 (75.3 s7) · TERMINAL 194/200 byte-exact · selftest PASS · deterministic.**
- **NAV layer integrated** (NAVBUILD agent, canon §8.1; new core/nav.{h,c} + routed sim.c —
  guidance now consumes a NavState view; NAV_TRUTH proven BIT-TRANSPARENT on the composed baseline
  (85/215/194 reproduced exactly; agent's CSVs SHA-256-identical pre-merge). Honest `--nav-noisy`
  degradation (pos σ .5/.5/.3 m, vel σ .1, att σ .1°, gyro walk): ENTRY 85→73, AERO 71.7→70.3,
  TERMINAL 97.0→96.5. NAVBUILD also found+fixed a real stale-snapshot bug at the entry-burn CUT
  (nav_resync) — ENTRY would have been 0% under nav without it.
- **MPPI under the composition: two directive-7 lessons.** The batch fell 63.3→40% when the split
  landed: (a) the rollout lean-model's fixed 0.6 damping no longer matched the split execution —
  mirrored the Kvd ramp into cmd_from_u_lean; (b) the REAL culprit: the sim-level wind-trim
  integral double-corrects a replanning controller — **gated to GM_HOVERSLAM** → **MPPI 41/60 =
  68.3%, its best and softest batch ever** (too-hard 2, landed td_v mean 2.97, run 1 GOOD @7.4 m).
  Lesson: reactive-law medicine (integral trim) poisons a replanning controller; MPPI's wind
  rejection IS its replan loop.
- **Goldens re-frozen** (entry/aero_t0/aero_mppi *_d010_baseline.txt). Gate status: M6 ENTRY ≥90%
  now 5 points away (misses graze at 26-33 m); M4 AERO ≥90% needs the next lever (state-adaptive
  divert gain per TOOHARD, or MPPI capacity at M5-CUDA).

## D-011 — Audio observer, presentation ladder, and the UE endgame (noted-for-later; 2026-07-18)

Second external review (operator-relayed; the reviewer verified OUR claims — their own claims about
post-cutoff tooling are tagged [reviewer, verify-at-build-time]). Adopted-in-principle roadmap;
nothing here is scheduled before the M6/M7 gates. Recorded so the M7+/M8+ sessions inherit it whole.

**1. AUDIO = A THIRD PURE OBSERVER (the doctrinal extension).** Not part of the renderer — its own
client on the same one-way TLM stream. Two tiers:
- **Tier A (in the existing ui/, cheap, transforms the current renderer):** Web Audio —
  THREE.AudioListener + PositionalAudio over PannerNode (HRTF), ConvolverNode IR reverb,
  AudioWorklet for sample-level synth DSP; all works in WebView2. Honest ceiling: binaural over
  headphones is genuinely excellent; no true object-based speaker-array output from a browser.
- **Tier B (native C sidecar, fits the zero-deps ethos):** subscribe to TLM like any observer;
  **Windows Spatial Audio (`ISpatialAudioClient`)** for real object-based output (engines, RCS,
  booms as OS-mixed 3D objects → Windows Sonic / Dolby Atmos / DTS:X abstracted away) +
  **Steam Audio** (Valve, Apache-2, C API) for HRTF, physics-based occlusion/reflection/pathing
  from scene geometry, convolution reverb (better than parametric outdoors), ambisonics.
  [reviewer]. FMOD/Wwise remain the middleware alternates if authoring tools are ever wanted.
- **Synthesis doctrine = canon §12, sharpened: causally derived, never loops.** Source layers per
  engine object (turbulent-mixing bed keyed to throttle/chamber count; the CRACKLE layer as
  Poisson-distributed steep asymmetric shocklets, density ∝ thrust — canon already pins the
  measured positive-skewness signature; ignition overpressure thump; shutdown pop; RCS hiss per
  mask bit; grid-fin actuator whine on close cams; leg-deploy bang; touchdown clang + die-off).
  Propagation computed from streamed state: retarded-time arrival (distance/c — the silent
  touchdown then the sound wall; canon's triple boom stands), 1/r spreading, **frequency-dependent
  atmospheric absorption driven by OUR OWN US76 model** (NEW vs canon's "distance strips highs":
  at 20 km slant range the rocket is pure infrasonic rumble and crackle fades in as range closes —
  the spectral reveal IS the F9-footage sound), Doppler with supersonic handling, ground-reflection
  comb near the pad, slow turbulence amplitude flutter on distant sources; then a real mastering
  bus (compressor/limiter/LFE management) for cinematic dynamic range.

**2. PRESENTATION LADDER.** WebGPU/TSL path ≈ 90% of the wow: compute-shader GPU particles for the
plume (no CPU roundtrip), BAKED volumetrics (EmberGen / Blender pyro → flipbooks / 3D textures
rather than real-time volumes), Bruneton precomputed sky, the §11.1 post stack. Credible second
engine: Unity HDRP + VFX Graph; Godot/Bevy trail for this use case. The deep point: **the protocol
means never choosing** — renderers are swappable clients; the WebGPU one remains the fast,
agent-iterable, always-works observer even after UE exists.

**3. UE 5.8 ENDGAME — UE as just another pure observer.** [reviewer: UE 5.8 shipped 2026-06, last
major UE5 before UE6; Epic's official "Unreal MCP" plugin embeds an MCP server in the editor
(local HTTP: spawn actors, lighting, material instances, automation tests; experimental);
community layers: ClaudeUnreal; Blender MCP exists incl. unified Blender+UE servers — verify all
at build time.] Wiring that keeps it OUR project: a thin UE plugin whose ONLY job is decoding
`BlTlm` (a THIRD static-asserted mirror of protocol.h) and driving actor transforms. UE
contributes: Nanite for a TurboSquid-class booster hull (static hull ideal; fins/legs as separate
articulated components), Lumen/MegaLights for plume-as-light at night, Niagara pyro, the path
tracer for offline money shots, **MetaSounds as the native home for the telemetry-driven synth**
(with OS-level Atmos output free), and double-precision Large World Coordinates (the 70 km→1 m
shot without floating-origin gymnastics).

**4. FluidX3D (OpenCL lattice-Boltzmann DNS; free non-commercial; free-surface/temperature/
Smagorinsky-Lilly LES; moving-geometry voxelization; STL import) — enters twice, BOTH OFFLINE:**
(a) visuals: sim the plume + ground-effect impingement around the actual mesh → baked vector
fields / VDB volumes driving Niagara; (b) **the deeper one: regenerate the plant's aero data** —
CA/CNα tables, CoP vs Mach and α, fin effectiveness — replacing hand-modeled coefficients with a
deterministic, auditable precompute artifact (ledger-grade provenance). Directly answers the
D-006/P2 transonic-CoP amplitude and the VEH_STAGE_LEN-vs-VEH_LEN caveat.

**5. THE HARD LINE (contract, canon-grade):** CFD and UE must NEVER close a runtime loop into
dynamics. The moment the pretty half feeds state back, determinism, the memcmp oracle, and the
anti-cheat thesis die. **Precompute in, telemetry out, always.** (Directives 2/7 extended to all
future observers and all offline tooling.)

**6. Build order adopted (each rung independently shippable; the core never learns):** finish
`--serve` renderer first-light → Tier-A Web Audio synth → UE plugin decoding the same stream →
Blender-MCP model prep + Nanite import → Niagara plume with FluidX3D bakes → MetaSounds port →
**aero-table regeneration as the final physics payoff.**

**D-011 addendum — the "don't pay the cinematic layer twice" sharpening (same reviewer, follow-up):**
Revises §6's ordering with a cleaner split, ADOPTED:
- **Web client = renderer-AGNOSTIC validation only, then STOP.** Wire `--serve` to a live tracked
  descent; prove interpolation/replay, camera logic, basic vehicle/pad/exhaust sprite, HUD, and a
  Tier-A audio SKETCH (not the full production). Target: "credible documentary view, not AAA
  polish." This validates everything that transfers to any client — the protocol, the observer
  model, the timing — and most of it is already built (vitest-green decode/frame/interp).
- **UE spike EARLY and CHEAP:** the thin plugin — decode HELLO/TLM, drive a placeholder mesh
  transform at 60 fps with Large World Coordinates — is DAYS (MCP-assisted), not weeks. Run it as
  soon as the wire is proven; it de-risks the engine decision with a tiny artifact instead of
  speculation.
- **The cinematic maximalism (plume, volumetrics, full audio production) is built ONCE, in the
  winner** — realistically UE (Niagara + Lumen + MetaSounds is exactly the layer we'd otherwise
  hand-build in TSL/WebAudio). The expensive layer is never paid twice.
- **Reframe (doctrine): this is never a migration — we ADD CLIENTS.** The web renderer stays
  forever as the fast, agent-iterable debug view (shareable in a browser / embeddable on
  wholemachine.org); UE becomes the IMAX theater. Same stream, and the core never knows.
- **DO NOT DEFER THE TELEMETRY SCHEMA (supersedes D-010 item 1's "defer to M7-prep"):** the
  protocol is the real coupling point — renderers are disposable, the contract is not. Any channel
  the cinematic layer will need gets into protocol.h EARLY, while ONE cheap client consumes it:
  the D-010 predictor internals (`pred_impact[2]`, `ignite_h` — the ghost/impact marker), and an
  audit of per-engine state + event coverage for ignition/boom timing (note: the EVT channel
  §10.4 already carries IGNITION_CMD/GREEN_FLASH/MACH1_CROSS with emission position — likely
  sufficient; the audit confirms rather than assumes). Schedule: the FIRST renderer-touching
  session does the schema extension as its opening act (one version bump, TS mirror + goldens
  re-frozen as one validated unit, per D-010's pre-authorization).
- Unchanged and re-affirmed: none of this blocks the core roadmap — the guidance gates (M4/M6)
  remain the highest-value work in the repo, and all of the above stays behind them.

## D-012 — State-adaptive divert gain: the powered-burn overspeed brake; ENTRY 88 (2026-07-18, night)

**The question (posed by TOOHARD/D-010):** with the height-split protecting the deck null, the
divert Kvel is a free knob — and the TH grid measured ENTRY/AERO wanting OPPOSITE values (0.9 vs
0.7). Schedule it from STATE (guidance cannot see the scenario; per-scenario gains are overfitting).

**The schedule that shipped:** `Kvel = KDIV_SEEK + sat((|v_xy| - vdes_mag)/KDIV_VBLEND) *
(KDIV_BRAKE - KDIV_SEEK)`, **fins-deployed POWERED BURN only** — SEEK 0.9, BRAKE 1.5, VBLEND 3
(guidance_hoverslam.h, shared with the MPPI rollout mirror). PROFILE OVERSPEED (|v_xy| above the
sqrt-decel vdes) is the state that separates "carrying speed the profile can't null" from
"tracking/seeking"; every divert transits it on the decel leg, so the schedule stiffens exactly
the deceleration/null part of the trapezoid while the cruise/seek keeps the C14-package 0.9.

**The v1→v4 structural factorial (each step gated: selftest PASS + TERMINAL exactly 194/200 +
determinism pair; s42 ENTRY/AERO below):**
- v1 SEEK .7/BRAKE .9 both phases: **69 / 66.3** — the TH-tree 0.7-seek optimum does NOT transfer
  under the C14 trim (AERO th→0 and GOOD 46→83 showed the soft-arrival upside; off-pad +31 killed
  it). DO NOT RETRY seek<0.9 anywhere while the C14 trim stands.
- v2 SEEK .9/BRAKE 1.2 both phases: **77 / 78.7** — AERO off-pad 58→37, ENTRY th 7→12 fuel 3→5.
- v3 brake UNPOWERED-only: **77 / 72.7** — the clean isolation: ALL of AERO's off-pad win lives in
  the burn-phase brake (v3 lost it), ALL of ENTRY's damage lives in the unpowered brake (v3 kept
  it), and burn-braking costs ENTRY nothing (v2==v3 on ENTRY). The unpowered AoA episodes disturb
  trim/ignition with zero off-pad payoff. **This also re-reads D-010's "Kvel 1.2 over-drove tilt":
  uniform-1.2's damage was the unpowered phase all along — the burn WANTS overspeed braking.**
- v4 brake POWERED-only: **84 / 76.3** (sum 160.3) — structure correct; constants to the grid.

**The 11-config BRAKE×VBLEND grid** (runs/d012_sweep.csv; `_adapt_wt` self-driving loop; every row
selftest+TERMINAL-194 gated): ENTRY's too-hard responds to brake ONLY at sharp onset (vblend 3:
th 9→7→6→5 across brake 1.05→1.5; flat ~8-9 at vblend 6/10) — the hot tail must be braked HARD and
IMMEDIATELY or not at all. AERO wants moderate braking (op 36 @1.2/3 vs 47 @1.5/3 — over-braking
mid-burn costs deep-divert reach). Winner for the M6 push: **(1.5, 3) = ENTRY 88 / AERO 73.3**;
co-leaders (1.2,3)=E86/A77.3 and (1.5,6)=E87/A76.3 stand as fallbacks if priorities flip toward M4.

**Deck-null follow-up grid** (runs/d012_sweep2.csv, KVEL_NEAR 1.5-1.8 × SPLIT 250/350 at (1.5,3)):
**ENTRY pinned at 88 on every row** — the deck null is SATURATED; the residual th-5 cohort is
time-below-the-split limited, not damping limited (extreme-vxy seeds, e.g. run 39: td_v 12.7 at
lat 4.6, dead-center and hot — the TOOHARD "deepest cross-range" tail). AERO lead noted for a
future session: (NEAR 1.7, SPLIT 350) = 75.0% s42 with th 24 (lowest seen) — noise-scale (+5/300),
NOT cross-validated, NOT shipped.

**Shipped state (config SEEK .9/BRAKE 1.5/VBLEND 3, NEAR 1.6/SPLIT 250) — all spec winds:**
- **ENTRY 88/100 s42 (op 5, th 5, fuel 2) · 79 s7 · 78 s99** — every seed +2-3 over D-010's
  85/77/76; M6 gate now 2 points away at s42.
- **AERO tier-0 220/300 = 73.3% s42 · 73.3% s7** (baseline 71.7/75.3 — s42 up, s7 down; net flat.
  M4's path remains MPPI capacity/M5-CUDA, not tier-0 tuning).
- **MPPI 43/60 at v4 constants → 44/60 = 73.3% at the final config — new MPPI best twice over**
  (baseline 41/60; final: off-pad 13, th 2, td_v mean 2.95). The execution blend inherits the
  burn-brake through hoverslam_step and the re-mirrored rollout ranks consistently — the brake
  helps every guidance mode.
- **NAV-NOISY honesty: ENTRY 74 (was 73), AERO 72.3 (was 70.3), TERMINAL 96.5 (unchanged).**
- **TERMINAL 194/200 byte-stable across every build of the arc** (~20 builds incl. both sweep
  worktree pipelines — the worktree rows bit-match main-tree reruns of the same config).

**Directive-7 note (the leak that was caught):** GM_MPPI does NOT call hoverslam_step for its
lateral — but `mppi_execute` DOES (vertical channel + the (1-s) blend into hoverslam's own a_lat),
and the warm-start forward-shoots the plant under hoverslam guidance. The first single-run MPPI
invariance check (run 1: td_v 2.52→2.70) caught the leak before any batch was trusted; the fix is
the header-shared KDIV_* constants + the same overspeed schedule computed in `cmd_from_u_lean`
from the rollout state (converging_vdes is profile-exact: A_DECEL/VLAT_MAX/T_LEAD parity).

**ENTRY fuel-out anatomy (the A2 trace, runs/d012_entry_v4.csv + run-14 verbose):** the fuel tail
is the MIN-THROTTLE CLIMB TRAP — a deep-offset, longest-flight seed (165 s) reaches the landing
burn fuel-marginal; as the tank drains, min-throttle (40%) TWR crosses 1, the vehicle arrests
~250 m up, CLIMBS to 560 m at +40 m/s unable to shut down (GM_HOVERSLAM has no mid-burn cut;
relights budgeted), burns dry, freefalls in at 96 m/s. NOT fixed this epoch: ENTRY_FUEL_FLOOR
medicine is counter-indicated by D-009 (an earlier entry-burn cut costs MORE total fuel), and the
cohort is 1-2 runs. Recorded as a latent with a named mechanism; candidate future fix is a
terminal-phase engine-cut rule (canon-compatible: guidance may command engine_cmd=0), taken ONLY
with a dedicated study.

**Do-not-retry additions:** seek<0.9 anywhere under the C14 trim (v1); unpowered-phase overspeed
braking (v3); VBLEND≥6 for the ENTRY th tail (flat); deck-null NEAR beyond 1.6 for ENTRY (flat
1.5-1.8); expecting the TH-tree isolated-config optima to transfer into the composed tree.

**Goldens re-frozen:** entry_s42_d012_baseline.txt, aero_t0_s42_d012_baseline.txt,
aero_mppi_s42_d012_baseline.txt (supersede _d010_; re-baseline pre-authorized by the gate protocol).

**M6 standing:** 88/100 s42 — remaining anatomy: op 5 (three graze 26-32 m within ~2-6 m of the
line), th 5 (extreme-vxy, structure-saturated), fuel 2 (min-throttle trap). The credible next
levers, in order: (a) the graze band via wind-trim/KI interaction with the new brake (a C15-style
mini-grid), (b) an engine-cut terminal rule for the fuel pair, (c) MPPI capacity (K 256→1024 CPU
probe, then M5 CUDA) which subsumes the th tail. AERO/M4 waits on (c) regardless.

**D-012 addendum — both remaining reactive levers CLOSED by measurement (same session):**
- **Lever (a) trim grid: NULL-TO-NEGATIVE** (runs/d012_sweep3.csv; KI_WIND 0.012/0.016/0.020 ×
  fade 160/240 × EINT_CAP 2/3 at the shipped brake; every row selftest+TERMINAL-194 gated; row 1
  = shipped config reproduced 88 bit-exact). Stronger/longer/deeper trim DOES convert grazes
  (op 5→4→2 at ki.016/fade240) but pays for every one in the same coin the C14 sweep found: trim
  tilt near the deck turns soft grazes into on-pad TOO-HARDS (th 5→7-8) and burns fuel margin
  (fuel 2→3, AERO fuel up to 5). Net ENTRY ≤88 and net AERO ≤ shipped on EVERY variant. The C14
  package (0.012/160/2.0) is Pareto-saturated under the brake. DO NOT RETRY trim strengthening
  for the graze band.
- **Lever (b) engine-cut rule: DEAD ON ARRIVAL with the current relight budget.**
  `relights_left=2` at scenario init (scenario.c:76); ENTRY spends both (entry burn + landing
  burn), so a high-arrest cut can never relight exactly where the trap lives — cut-without-
  relight from ~250 m is a 70 m/s crash, no better than the 96 m/s trap. The rule requires
  relights 3 (defensible vs real F9 3-burn profiles but it is a PLANT/scenario relaxation —
  directive-3 adjacent, needs its own ADR + study of cut/relight oscillation guards). Parked.
- **Graze anatomy (final-config verbose traces):** the op-5 band is TWO mechanisms. Runs like 3:
  the TRIM-RESIDUAL miss — a flawless landing (settles upright, tilt 0.4°, vz≈0) 33 m downwind;
  the steady-state residual of the faded/capped trim (this is what lever (a) targeted and cannot
  convert without the th/fuel payback). Runs like 73: the OVERSHOOT TAIL — crosses pad center at
  h=54 m still at 38 m/s down, slides outbound, contacts at 26.4 m with vrad +7 (a th-mechanism
  run that happens to end 0.4 m over the line; brake-saturated). Also noted: v4's run 18 graze
  (27.8 m) was CONVERTED by the final (1.5,3) brake — it now lands at 23.9 m.
- **Conclusion: ENTRY 88 is the measured plateau of the reactive structure.** All three residual
  mechanisms are saturated (trim: this addendum; brake/deck-null: the D-012 grids; fuel: relight-
  blocked). The path to M6 ≥90 is MPPI capacity (K 256→1024 CPU probe → M5 CUDA), which attacks
  the th tail and the overshoot-op tail by replanning — and is the M4 path anyway. Next session:
  start at Roadmap B.

## D-013 — Telemetry protocol v3: the predicted-impact marker fields (2026-07-18, night)

The D-010-item-1 / D-011-addendum pre-authorized schema extension, executed as ONE validated unit
by a fleet worktree agent (lane `proto`, `_proto_wt`) and integrated with full gates. The renderer
remains a PURE OBSERVER: both fields are computed by a pure read of sim/guidance state in the
telemetry writer (`fill_tlm`); guidance reads nothing new; no feedback path exists.

- **`BlTlmFixed` += `float pred_impact[2]` (world XY, offsets 220/224) and `float ignite_h`
  (offset 228)**, placed in the guidance-derived group after `dist_pad`; tail fields shift +12;
  `sizeof 276→288`; **`BL_PROTO_VERSION 2→3`** (the TS decoder rejects mismatched `ver` — old
  clients fail loud, intended). C static asserts updated and passing (compile-time layout proof).
- **`pred_impact` v1 formula:** `r_xy + v_xy·clamp(t_go,0,60)` — the kinematic coast point, one
  consistent semantic for GM_HOVERSLAM and GM_MPPI; it converges onto the pad as the solve
  tightens (the D-010 diegetic "it actually solved it" marker). LIMITATION: no wind/steering/aero
  — a v2 could stream the planner's own terminal projection. **`ignite_h`:** the existing
  state-pure `compute_ignite_h` bisection (MPPI's per-replan precompute; matches hoverslam's
  trigger), exposed via `bl_predict_ignite_h()`.
- **Validation:** worktree-first (C asserts + selftest PASS + TERMINAL 194/200 exact + vitest
  26/26 + `--golden` re-emit == frozen hex + a raw-WS live-wire probe decoding HELLO/TLM v3 with
  the formula reproducing wire bytes), then REPEATED on the main tree post-integration: selftest
  PASS, TERMINAL 194/200 byte-exact, **MPPI run-1 invariance pair byte-identical** (HARD
  td_v 2.63 / lat 10.48 twice — behavior-neutral; note this run-1 line is the correct
  (1.5,3)-config reference, superseding the v2-constants 2.52/8.58 reference), vitest 26/26,
  all three protocol goldens MATCH.
- **`goldens/protocol/*.hex` re-frozen** (tlm 276→288 B; hello ver→3; evt unchanged) — the
  re-baseline pre-authorized by D-010/D-011. `runs/ws_probe.mjs` added (stdlib-Node raw-RFC6455
  smoke reader). Full field/offset tables: `runs/proto_report.md`.

**Also recorded this push — the fleet's completed research + tooling artifacts (no core changes):**
- `runs/mppi_research.md` (lane mppi-research): vanilla MPPI saturates ~1-2k samples; our misses
  are exploration-limited → pair capacity with a variant. Keep σ fixed, let the ESS-servo sharpen
  λ (lower LAMBDA_MIN 2.0→~0.5); tune OU θ 0.15→0.08-0.10; CoVO covariance and MPOPI
  iterate-per-replan (4096×4 ≥ 16384×1) are the structural upgrades; **fp64 on sm_89 is ~1/64
  fp32 throughput — the CUDA p99≤6ms@K16384 gate likely needs fp32 rollouts with §9.5 toleranced
  parity**; keep H=5 s + projection terminals (the literature endorses our structure).
- `runs/gfold_research.md` (lane gfold-research): LCvx/G-FOLD is provably valid only where we
  don't need it (drag-free TERMINAL); the aero-dominant divert needs SCvx (local optimality,
  3-10× compute). Onboard solve times are affordable but the honest role is **offline oracle +
  fuel-optimal MPPI warm-starts**, not an onboard replacement; PIPG-on-GPU noted. MPPI@K256
  already realizes ~0.70·D_phys ≈ 775 m of the 1107 m ceiling.
- `runs/windthink_design.md` (lane windthink): the NAV-legal wind estimator closes to <0.2 m/s in
  a plant-parity probe IF the commanded-AoA correction is applied (~4.5 m/s error per degree of
  AoA otherwise); τ 4-6 s; freeze at ignition (the SRP shield kills the aero signal in-burn);
  feedforward ranked upwind-aim-pre-bias first, C14-eint warm-start as an unstackable fallback;
  staged validation with explicit falsification bars. Build in flight (lane windbuild).
- `runs/toolsmith_report.md` (lane toolsmith): compiled-C analysis tools `mcdiff` (per-run CSV
  A/B: verdict flips, cause-transition matrix, distribution deltas) and `tracestat` (--verbose
  trace features: ignition/arrest/climb/fuel-margin; auto-flags the min-throttle climb trap).
  Sources in `_tools_wt\` pending a tools/ fold-in; contracts posted to all lanes.

**D-013 addendum — two more levers CLOSED by measurement (fleet build lanes, same night):**
- **RELIGHT (relights 2→3 + high-arrest engine cut): NULL — REJECTED** (runs/relight_report.md,
  560 runs across 5 batches, every one summary-identical to baseline; per-run diff shows EXACTLY
  the two fuel-out rows changed). The cut works mechanically (run 14: cut at h=44.8/vz=+11 →
  refall → relight → still burns dry → 55.3 m/s vs 96.0; run 88: 130→97.8) and is
  directive-3-clean (engine-off only, no padward term — and the tell is it does NOT rescue the
  runs), but **the min-throttle climb trap is a symptom: the fuel-marginal seeds lack total
  propellant for ANY strategy, relight included.** The D-012 "+2 s42/+5 s7 potential" is measured
  NOT REALIZED. DO-NOT-RETRY cut/relight strategies at the current fuel budget; the upstream
  levers (entry-burn economics, scenario fuel load) are plant-ADR territory bounded by D-009's
  "bigger entry burn is cheaper" finding. The `_rl3_wt` implementation is preserved in the report
  should the fuel budget ever change. (Side finding kept: relights 2→3 alone is a defensible
  physical correction — real F9 flies 3-burn descents — but it is pointless without a consumer.)
- **WINDBUILD (the windthink estimator, Stage 0): FALSIFIED — SHELVED** (runs/windbuild_report.md).
  Byte-transparency PROVEN (estimator scaffolding reproduces every baseline bit-exact: 88/220/194/
  MPPI-invariant), but the estimator-error ship bar (<2 m/s) FAILED BY ~10×: mean |error| at
  ignition 21.7 m/s ENTRY / 18.5 m/s AERO — anti-informative (error ≥ the 16-22 m/s true wind).
  Root cause, measured from truth: the composed vehicle holds 9-11° mean AoA (transients to 53°)
  through the whole fins-deployed descent — it is continuously DIVERTING, so the weathervane
  premise (AoA≈0) never holds; at 4.5 m/s per degree that is ~40 m/s of error, and the commanded-
  AoA correction cannot remove the UNCOMMANDED fin-rate oscillation. The windthink probe's
  <0.2 m/s was an artifact (it modeled attitude ≡ −vrel_dir by construction) — the isolated-model
  -vs-composed-tree lesson (§4) claims another scalp. Stage 1 (aim pre-bias) was correctly NOT
  built (injecting an 18-22 m/s wrong-direction estimate is the design's own named poison).
- **Convergence: every reactive/estimation lever for the M6 gap is now measured-closed** (D-012:
  brake/deck-null/trim saturated; this addendum: relight null, wind-inference falsified). The
  remaining path is MPPI capacity + variants (lanes kprobe/cuda-mppi/mppi-var, in flight), exactly
  as the research trilogy independently concluded.

## D-014 — NAV-legal wind estimator: BUILT and FALSIFIED at Stage 0 (2026-07-18, night)

The windbuild lane (the "Build in flight" noted under D-013's research artifacts) landed. Recorded
per directive 8 — failed experiments get recorded WITH numbers. Full record: runs/windbuild_report.md
(design: runs/windthink_design.md). NUMBERING NOTE: the report and the `_wind2_wt/` code comments
call this "the D-013 estimator" — they were authored before the telemetry-protocol entry above
claimed D-013. A compact cross-lane summary of the same result also lands in the D-013 addendum
above (appended by the concurrent fleet-integration push); this D-014 entry is the full dedicated
record.

**What was built (worktree `_wind2_wt/` ONLY — the real tree was never touched):** strictly per the
windthink design — the OBS-B AoA-corrected wind estimator in the `Sim` struct (`wind_est[2]`,
`wind_est_valid`, `wind_est_frozen[2]`, `prev_engine_on`), updated at the 50 Hz guidance tick under
the observability gate (fins_deployed && !engine_on && qbar>500 && |v|>30 && tilt<20°),
GM_HOVERSLAM-gated (MPPI-exempt): de-rotate the KNOWN commanded steering tilt out of the nav
attitude, `w = v_horiz − |v|·(−zb2w_aero)_horiz`, τ=5 s low-pass, freeze latched on the ignition
edge. Canon §4.3 clean — reads ONLY nav attitude + inertial v, never wind_world/wind_filt. Plus an
off-by-default WIND_DBG stderr tap (env BL_WIND_DBG, separate build_dbg/) and winderr_batch.ps1.
Stage 1 (`GuidanceCmd.aim_bias` upwind pre-bias) was NOT built — correctly, see below.

**Stage 0, transparency half — PROVEN (the estimator computes but injects nothing):** vs the D-012
baselines captured on the unmodified worktree copy: `--selftest` PASS; TERMINAL s42 ×200 = 194/200,
ENTRY s42 ×100 = 88/100 (op5 th5 fuel2), AERO s42 ×300 = 220/300 (op47 th30 fuel1) — all
byte-exact; MPPI run-1 invariance byte-identical (HARD td_v 2.63 / lat 10.48); ENTRY run 3 (the
canonical 31.8 m graze) and the ENTRY/AERO run-twice determinism pairs reproduce exactly. Zero
hidden coupling: the Sim-struct placement + fins/GM gating are correct, and TERMINAL (fins stowed)
never touches the estimator (directive 9).

**Stage 0, estimator-error half — FAILED the bar by ~10×:** mean |estimate − true mean wind| at the
ignition freeze (the value Stage 1 would consume) = **21.7 m/s ENTRY / 18.5 m/s AERO** (n=20 each,
s42, DBG tap) against the design's **<2 m/s** ship bar (probe idealized <0.2). True winds at
ignition are 16–22 m/s, so **the error ≥ the signal itself — the estimate is anti-informative**
(its direction essentially uncorrelated with truth). Every one of the 40 runs freezes 7–36 m/s
wrong; none approaches 2. The OBS-B commanded-AoA correction DOES help (est 18–22 vs raw OBS-A
21–32) but cannot remove the uncommanded oscillation, and the τ=5 s filter makes it WORSE
(37–38 m/s): the error is NOT zero-mean in wind space (sin nonlinearity of the AoA→error map + the
sustained divert bias), so no averaging recovers the mean. Clean-tick census: 0/1399, 115/3301
(3.5%), 14/1418 observable ticks under 3 m/s across probed runs — transient AoA-zero-crossings,
scattered, unidentifiable in flight (isolating them requires knowing the true wind). Not
salvageable by tighter gating.

**Root cause — the body never weathervanes (measured from TRUTH, independent of any estimator):**
the vehicle holds **mean true AoA 9–11° (transients 20–53°, swinging −26°..+7°)** through the
entire fins-deployed aero-descent, because it is continuously diverting to null the 500 m–3 km
cross-range offset and the fin-rate-limited (20°/s) attitude loop oscillates around the commanded
divert AoA (control.c:138: "tilt swinging 2..24 deg"). The estimator's foundational premise — the
airframe weathervanes so body −Z tracks the relative wind (AoA≈0) — never holds in the composed
sim. At |v_rel|~250 m/s the probe's own measured sensitivity (4.5 m/s of wind error per degree of
AoA) turns 9–11° into ~40 m/s of raw error — exactly the OBS-A number measured. The WINDTHINK
probe (windobs.c) reached <0.2 m/s only by MODELING the attitude as −vrel_dir by construction
(windobs.c:182) — correct math, wrong operating point: the isolated-model-vs-composed-tree lesson
(HANDOFF §4) again. The probe measured its own failure mode (its finding 2) without modeling the
flight condition that triggers it; the design's staged in-sim truth-up existed precisely to catch
this, and did — small cost, decisive kill.

**Why Stage 1 was correctly NOT built:** the design's decision rule ("do not proceed to Stage 1
until Stage 0 passes; if the bar fails, record the falsification and STOP") was applied as written.
Injecting an 18–22 m/s wrong-direction estimate as a ≤40 m upwind aim shift is the design's own §6
named failure mode ("a garbage estimate injected as a pre-bias moves the aim-point
deterministically wrong… can actively degrade a clean lander"): it would convert currently-on-pad
runs into off-pad grazes — strictly worse than the reactive faded C14 integral, which can only
ever leave a residual, never push a good trajectory off the pad. Bitter symmetry: ENTRY run 3's
31.8 m trim-residual graze (a flawless landing settling downwind) is exactly what a CORRECT upwind
pre-bias would fix — and exactly what a wrong one manufactures on runs that today land.

**Convergence with the D-012 addendum:** D-012 closed the trim grid (null-to-negative) and the
engine-cut rule (relight-blocked); this closes the wind-estimator-feedforward lever too — not at
the §6 Pareto test but UPSTREAM of it, at observability (the input cannot be observed in the
flight condition where it is needed). **ENTRY 88 / AERO 73.3 stand as the plateau of the
reactive/open-loop wind-rejection structure. The path to M6 ≥90 AND M4 ≥90 is Roadmap A — MPPI
capacity (K 256→1024 CPU probe → M5 CUDA) — which rejects wind by replanning on the fresh nav
state every tick; closed-loop, no wind estimate needed.**

**DO NOT RETRY:** an attitude-only wind estimator during offset-nulling ENTRY/AERO descents
(measured: 18–22 m/s freeze error, mean true AoA 9–11°, no identifiable clean window; OBS-B2
force-inversion assessed and not a small fix — the oscillation is fin-rate-limit/slosh-driven, not
cleanly invertible, and the ~5° residual at ignition poisons it even feathered). Prerequisite for
any future revisit: a flight regime where the body genuinely weathervanes (feathered,
attitude-settled) at ignition — which the current ENTRY/AERO scenarios never provide.

**Artifacts preserved:** `_wind2_wt/` is the settling artifact — the estimator (core/sim.{h,c}),
the WIND_DBG tap, clean `build/` + `build_dbg/`, and `winderr_batch.ps1`; runs/windbuild_report.md
holds the full record incl. repro commands. Honesty note kept with it: the tap's first version
called `wind_sample()` for the truth column — which MUTATES the plant's Dryden state and broke
determinism (ENTRY run 3 td_v 5.48→5.92); fixed by computing the true MEAN wind inline, after
which the DBG exe reproduces the baseline byte-exact. Instrument the plant without touching it —
measure-the-measurer applies to debug taps too.

## D-015 — M5 CUDA MPPI port INTEGRATED; the fp64/latency rescope; capacity-saturation evidence (2026-07-19, small hours)

**The port (fleet lanes cuda-mppi ×3, `_cuda_wt` → `_cuda2_wt` rebase; full record
runs/cuda_mppi_report.md + runs/cuda_rebase_report.md):** `--mppi-cuda` runs the MPPI rollout
sweep on the GPU (RTX 4070 Ti SUPER, sm_89, CUDA 13.1, `-fmad=false`, no fast-math, no atomics,
on-device Philox with the CPU counter scheme, fixed power-of-two pairwise reductions).

- **Precision architecture: fp64 EVERYWHERE — the directive-7 decision.** The `.cu` unity-includes
  the SAME plant `.c` files (BL_HD qualifiers only; no-ops in CPU builds), so nvcc compiles
  byte-identical dynamics source. An fp32 rollout would be a second, divergent plant — rejected on
  principle before performance was even measured.
- **Parity + determinism, measured at K=256 AND K=16384:** CPU-ref vs GPU max relative Δcost
  8.7e-16..1.3e-15 (≈1 ULP, the known MSVC↔CUDA libm divergence), top-64 rollout rank agreement
  100%; same-GPU run-twice **bit-identical (max|Δ|=0) at both K** — per-arch determinism is the
  hard gate and it holds at the design-target K.
- **Behavioral identity:** AERO s42 ×60 `--mppi-cuda` = **44/60, line-for-line identical to the
  CPU batch and the D-012 golden** (every bucket, every landed mean) — not one flipped outcome in
  60 dispersed runs. Single-run + determinism pair re-verified on the integrated main tree, plus
  selftest PASS, TERMINAL 194/200, protocol-v3 goldens MATCH.
- **The honest latency miss and the RESCOPE:** fp64 on consumer sm_89 (~1/64 fp32 throughput)
  misses the design's p99≤6 ms at every K — measured GPU-event floor: rollout+reduction p99
  ≈46 ms @K256 → ≈299 ms @K16384 (255-reg occupancy-capped; GPU-bound 85-94%). BUT the controller
  replans at 10 Hz = a **100 ms budget**, against which fp64 is real-time-viable to ~K1024 — and
  that is exactly where vanilla MPPI saturates (below). **The 6 ms number was an fp32-era
  hard-real-time bar; re-scoped by measurement to the 100 ms replan budget.** Tighter-latency
  paths if ever gated: the reduction kernel, MPOPI 4096×4 iterate-per-replan.
- **CI-safe wiring (proven both paths):** CUDA is NEVER in `project()`; `option(BL_CUDA ON)` +
  `check_language(CUDA)` → clean CPU-only build when no toolkit (the GitHub runner); `--mppi-cuda*`
  flags refuse with exit 4 in CPU-only builds. Verified: full CUDA build green AND `-DBL_CUDA=OFF`
  build green with graceful refusal; all gates green under BOTH builds. `.github/workflows/ci.yml`
  unchanged — this push is the live CI test of the toolkit-less path.

**Capacity-saturation evidence (the D-013 research prediction, now measured):** K=512 AERO s42
×60 = **44/60 FLAT vs K=256** (identical failure anatomy; runs/kprobe_sweep.csv, lane kprobe;
K=1024 confirmation in flight, verdict pre-registered as locked regardless). The cheap variant
levers are ALSO null-to-negative so far (lane mppi-var interim: LAMBDA_MIN 2.0→0.5 = 42/60;
OU_THETA 0.15→0.10 = 42/60; θ 0.08 in flight). Vanilla capacity alone does not move the rate —
consistent with runs/mppi_research.md: exploration-limited, pair capacity with STRUCTURAL
variants (CoVO covariance / MPOPI iterate-per-replan).

**Deck-null cross-validation VERDICT (lane decknull, runs/decknull_report.md): REJECT (1.7,350).**
The strict pre-registered rule caught it: AERO tier-0 +12/600 (real; reproduces sweep2) and ENTRY
0/+1/+1 PASS, but **MPPI 44→41 (−3, all off-pad reach) FAILS rule 3** — the stronger deck damping,
correctly mirrored into the rollout (directive 7), over-damps the replanner's reach. Trading the
roadmap's M4/M6 vehicle for tier-0 gain is backwards; main stays (250, 1.6). Flagged for a future
ADR study ONLY: a deliberate directive-7-exception decoupling (hoverslam 350/1.7, MPPI 250/1.6).

**Fleet-method note for the record:** eight of ~14 build agents died to the idle-wait trap ("I'll
wait for the notification"); successor agents with poll-heartbeat discipline + main-session
harvest of orphan batches recovered every lane with zero work lost. The worktree pipeline's
bit-determinism (worktree rows == main-tree reruns, verified ~6× tonight) is what made the
successions cheap.

