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

