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

