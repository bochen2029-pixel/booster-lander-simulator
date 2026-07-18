# G-FOLD / Convex Powered-Descent Guidance — implementation brief for the Booster Lander Simulator

**Author:** gfold-research lane (intercom e5xqqhp2), 2026-07-18. **Status:** research only, no code touched.
**Audience:** an engineer who will decide whether to build an onboard convex-optimal guidance layer for
THIS sim, and if so, how. Written to be actionable, not a lit review. Every symbol is defined; caveats
are honest; the "should we?" verdict is in §C.

**Reader's 60-second version.** The convex-guidance literature (G-FOLD / lossless convexification, and
its 6-DoF successor SCvx) is real, flight-demonstrated, and solves in tens of ms onboard — but its
clean, globally-optimal form (LCvx) is valid **only for drag-free / affine dynamics**, i.e. the
vacuum-ish terminal problem we have **already solved to 97%**. Our open problem — the wind-disturbed
lateral divert during an *aero-dominant* fall from 12 km — is exactly the regime where LCvx provably
breaks and you must fall back to successive convexification (SCvx: linearize-the-aero + trust-region +
iterate), paying 3-10× the compute and losing the global-optimality guarantee. Our own `ceiling.c`
oracle already computes the true physical divert ceiling (**D_phys ≈ 1107 m** at v_lat≤30 m/s) that any
such solver could target, and MPPI-at-K256 already realizes **~0.70·D_phys ≈ 775 m**. So the real
question in §C is not "convex vs MPPI" in the abstract; it is "can an onboard SCvx solve close the
0.70→0.90+ realization gap on a velocity-capped ceiling better than MPPI-at-capacity can?" My ranked
answer: **build the G-FOLD solver as an offline/oracle tool first (it will pay for itself as a ceiling
and a warm-start generator), and if it goes onboard, run it as a convex REFERENCE that MPPI tracks —
not as a replacement for MPPI.** Reasoning and rankings below.

---

## 0. Scope, and how our plant maps onto the canonical problem

Our vehicle (from `constants.h` / `ceiling.c` parity block): single engine, `ENG_T_SL≈845 kN`
(`T_vac≈933 kN`, so ~850 kN class), throttle range **0.40–1.0** (the non-convex "annulus"), `Isp`
282 s SL / 311 s vac, dry mass 25.6 t, aero ref area 10.52 m², US76 atmosphere, frozen `CA/CN(M)`
tables. Fins-deployed tilt cap **≈15°** (`control.c` flat 15° fins-deployed qcap; TERMINAL 12°).

The three scenarios and where convex guidance could even apply:

| Scenario | Regime | Solved? | Convex-guidance relevance |
|---|---|---|---|
| TERMINAL (2 km) | near-vacuum vertical, small lateral | **97% (194/200)** — sacred | This is the LCvx sweet spot, and it is *already solved* by the reactive hoverslam. Convex adds nothing here. |
| ENTRY (62 km) | hypersonic entry + entry burn + divert | 88% | Entry burn is a qbar/heat *supervisor* problem; the powered divert is the same lateral problem as AERO. |
| AERO_OFFSET (12 km, ~500 m offset, winds) | **aero-dominant fall → aero-aware ignition → landing burn** | 73.3% | **The open problem.** This is the *hardest* case for LCvx and the natural home for SCvx-with-aero. |

Nominal AERO descent (verified by running `runs/sandbox/ceiling.c` this session):
`T_touchdown = 49.54 s`; ignition at `t = 27.49 s`, `h = 3120 m`, `Mach 0.89`. So the descent is
**~27.5 s unpowered aero fall, then ~22 s powered burn.** The divert authority is aero (lift ∝ q̄) in
the first phase and thrust-vector (∝ sin(tilt), faded near ground) in the second — a time-varying,
regime-switching authority that the reactive law and MPPI both wrestle with.

---

## A. The canonical minimum-fuel powered-descent SOCP, adapted to our plant

### A.1 The lossless-convexification (LCvx / G-FOLD) formulation — the drag-free base case

This is the Açıkmeşe–Ploen / Blackmore–Açıkmeşe result [1][2][3]. States: position **r**∈ℝ³,
velocity **v**∈ℝ³, mass m. Continuous dynamics with constant gravity **g** and thrust vector **T_c**:

```
ṙ = v
v̇ = T_c / m − g          (+ F_aero/m  — DROPPED in LCvx; see the break, §A.3)
ṁ = −‖T_c‖ / (Isp·g0) = −α‖T_c‖,     α ≜ 1/(Isp·g0)
```

The killer non-convexity is the **thrust annulus** `0 < T_min ≤ ‖T_c‖ ≤ T_max` (for us
`T_min = 0.40·T(p)`, `T_max = 1.0·T(p)`, so `‖T_c‖ ≥ T_min` carves out a non-convex hole around the
origin). LCvx removes it with two moves:

**(1) Change of variables to mass-normalized control** (makes the annulus and the `1/m` bilinearity
tractable). Define
```
u ≜ T_c / m        (specific thrust, i.e. commanded acceleration, m/s²)
σ ≜ Γ / m          (a SLACK scalar, the "thrust-magnitude budget")
z ≜ ln m           (log-mass)
```
Then `v̇ = u − g`, and the mass dynamics become **linear** in σ: `ż = −α σ`.

**(2) Relax the annulus** by introducing σ as an *upper bound on the control norm* rather than the
control norm itself:
```
‖u‖ ≤ σ                                            (SOC — convex!)
μ_min(z) ≤ σ ≤ μ_max(z),   with μ_min = ρ_min·e^{−z},  μ_max = ρ_max·e^{−z}
```
where `ρ_min = T_min`, `ρ_max = T_max`. The upper bound `σ ≤ ρ_max e^{−z}` is convex directly. The
**lower** bound `σ ≥ ρ_min e^{−z}` is *concave* in z, so it is replaced by its **first-order Taylor
expansion about the current iterate z₀** (this is the one linearization LCvx needs, and it is exact at
convergence because z is a scalar with monotone dynamics):
```
σ ≥ ρ_min e^{−z₀} · [ 1 − (z − z₀) + ½(z − z₀)² ]      (2nd-order lower-bounding, still convex)
```

**Thrust-pointing / tilt cone** (our ≈15° fins-deployed cap). With **n̂** the desired thrust axis
(vertical/up), the cone `n̂·T_c ≥ cos(θ_max)·‖T_c‖` becomes, in the normalized variables, the **linear**
constraint
```
n̂ · u ≥ σ · cos(θ_max),     θ_max = 15°   (for us; fins-deployed)
```
This is the single cleanest win of the change of variables: a non-convex pointing constraint becomes a
half-space in (u, σ).

**Glideslope** (keep the vehicle inside an inverted cone above the pad, avoid sub-surface / shallow
approach). With pad at origin and `e_z` up:
```
‖ E r ‖ ≤ (1/tan γ_gs) · (e_z · r),     E = [I₂ 0] picks the horizontal components
```
γ_gs is the glideslope half-angle from horizontal. (For a booster you'd set γ_gs steep, ~80-85°,
because we come in nearly vertical; a shallow γ_gs would over-constrain the divert.)

**Optional velocity bound** (structural / small-AoA validity): `‖v‖ ≤ V_max` — convex SOC. Blackmore's
"maximum-divert with state constraints" extension [4] proves LCvx stays lossless even when this bound
is active, which matters for us because our divert is explicitly velocity-capped (see §C).

**Objective — minimum fuel = maximum final mass = minimum total impulse:**
```
minimize   ∫₀^{tf} σ dt          (≡ −z(tf) is maximized ≡ min propellant)
```
This is **linear** in σ → the whole thing is a Second-Order Cone Program (SOCP): linear objective,
linear equalities (discretized dynamics), SOC constraints (`‖u‖≤σ`, glideslope), and linear
inequalities (pointing, Taylor'd thrust bounds).

**The LCvx theorem (why the relaxation is "lossless").** At the optimum of the *relaxed* SOCP, the
inequality `‖u‖ ≤ σ` holds with **equality** (`‖u*‖ = σ*`), so the relaxed solution is feasible and
optimal for the *original* non-convex annulus problem. Conditions (Açıkmeşe–Blackmore [2][3]):
(i) the system is controllable, (ii) the thrust bounds are the *only* control non-convexity,
(iii) — critically — **the dynamics are affine in the state** and there is no state-dependent forcing
that couples into the control. Drag violates (iii). See §A.3.

Discretization: N knots, zero-order or first-order hold on u and σ, dynamics as linear equalities
(exact ZOH matrix exponential, or RK). Flight practice uses **N ≈ 15–60** (G-FOLD flight tests ran as
few as ~16; Szmuk 6-DoF ~30; the 2025 Uzun/Açıkmeşe SPLICE paper uses K=15 [10]). Free-final-time is
handled by a **golden-section / bisection line search over tf** wrapping the SOCP (each tf is one
fixed-time SOCP solve), or by the SCvx time-dilation trick.

### A.2 What changes for a single-engine booster (us) vs the multi-thruster Mars lander

- Mars-lander LCvx assumes a throttleable pointable thrust *vector* (gimbal). We have one engine with a
  gimbal → identical math, `θ_max` = gimbal + tilt authority. Our fins-deployed 15° cap IS the pointing
  cone. Fine.
- Our `T(p) = T_vac − A_e·p` is **pressure-dependent** (ambient pressure drops through descent). LCvx
  assumes constant T_min/T_max. Fix: make ρ_min(t), ρ_max(t) time-varying along the (known) altitude
  profile — still convex, since p(h(t)) is data, not a variable. `ceiling.c` already does exactly this.
- Mass ratio: we burn ~a few tonnes of a 35 t vehicle, so `e^{−z}` varies modestly → the Taylor
  lower-bound is very accurate (one linearization suffices; LCvx stays essentially one-shot).

### A.3 Where lossless convexification BREAKS for us, and what SCvx costs

**The break.** Our AERO divert is *aero-dominant for the first 27.5 s.* The specific force is
```
v̇ = u − g + a_aero,   a_aero = −(½ρ‖v‖ / m)·S_A·[CA(M)·v̂_axial + CN(M,α)·(lift terms)]
```
`a_aero` depends on **v** (quadratically, via q̄=½ρ‖v‖²), on Mach M=‖v‖/a(h), on angle-of-attack α
(i.e. on attitude, which in a lift-generating divert is the control), and on altitude via ρ(h). This is
a **non-affine, state-and-control-coupled forcing term.** It violates LCvx theorem condition (iii): the
`‖u*‖=σ*` tightness proof no longer holds, and worse, the *lift* that does most of our first-phase
divert (a_max = q̄·A_ref·CN·α/m in `ceiling.c`) is **not even in the u channel** — it is generated by
tilting the body into the airflow, so the "thrust-pointing cone" abstraction does not capture the
control authority at all. LCvx has nothing to say about the aero phase. This is not a tuning issue; it
is structural. The 2023 survey [8] states it plainly: *"aerodynamic forces were generally neglected in
the development of SOCP-based PDG methods"* precisely because including them destroys the one-shot
convex structure.

**What the literature does instead — three named strategies, with who does what:**

1. **Ignore aero** (classic G-FOLD / Mars): valid only in thin-atmosphere or terminal regimes.
   Açıkmeşe–Ploen [1], Blackmore [2][3], Dueri [6]. **Applies to our TERMINAL, not AERO.**

2. **Treat aero as a bounded disturbance + reserve control margin** (robust/stochastic PDG). Ridderhof
   & Tsiotras [11] show min-fuel optima *rail the throttle at T_min or T_max*, leaving **no feedback
   authority**; they derive stochastic throttle margins so the feed-forward optimum stays a bounded
   distance from the rails, preserving authority to reject the disturbance. **This is the single most
   relevant caveat for us** (see §C): our D-012 "overspeed brake" is exactly a hand-tuned throttle
   margin, and Ridderhof/Tsiotras is the principled version.

3. **Successively convexify the aero** (SCvx / the only method that actually models the lift):
   linearize a_aero about the current trajectory iterate, add a **trust region** (‖x−x̄‖≤ρ_trust) to
   keep the linearization valid, add **virtual control / slack** so every subproblem is feasible, and
   **iterate** the SOCP to convergence. Szmuk–Reynolds–Açıkmeşe 6-DoF [5][9]; Chen et al. [7] (the
   FIRST onboard atmospheric PDG — LCvx for the thrust bound *and* SCvx for the aero, together);
   Uzun–Açıkmeşe–Carson 2025 [10] (models `a_aero = −½ρ‖v‖ S_A C_A v`, successively linearized, K=15,
   NOT lossless, exact-penalty SCvx). Reusable-launcher work [survey ref 108] uses a **6-DoF model but
   only 3-DoF in the OCP under a perfect-attitude-control assumption** — which is *exactly* our HIER
   abstraction (attitude/allocation handled by an inner loop, guidance solves the translational
   problem). That parallel is worth internalizing: the aerospace SCvx community independently converged
   on the same "guidance solves 3-DoF translation, inner loop owns attitude" split that our MPPI HIER
   parameterization already uses.

**The cost of SCvx over LCvx (concrete, for our sizing):**
- No global-optimality guarantee — only *local* optimality (converges to a KKT point of the non-convex
  problem). Superlinear/local convergence in **3–10 SOCP solves** per guidance call.
- ~3–10× the per-call compute of a single SOCP (one SOCP × iterations).
- Requires trust-region + penalty-weight tuning (the SCvx analog of MPPI's λ/Σ tuning — no free lunch).
- **State-triggered constraints** (Szmuk/Reynolds [5][9]) let you impose a *velocity-triggered
  angle-of-attack cap* — "when ‖v‖ > v_thresh, limit α ≤ α_max(v)" — continuously, with no
  integer variables, to bound aero loads exactly the way our ENTRY qbar supervisor and 15° cap do by
  hand. This is the cleanest thing SCvx offers us that the reactive law approximates crudely.

---

## B. Onboard feasibility — problem sizes, solve times, and what building the oracle costs

### B.1 Reported solve times (the evidence, with sources)

| Solver / study | Problem | Nodes N | Solve time | Hardware | Ref |
|---|---|---|---|---|---|
| BSOCP (JPL flight solver) | G-FOLD min-fuel + ref-track | ~16 | **tens of ms** | Xombie flight computer | [6][fd] |
| Dueri custom IPM | 3-DoF PDG SOCP | 11–25 | **2–3 orders faster** than generic ECOS | flight-class | [6] |
| SeCO (Kamath/Elango) | rocket soft-landing SOCP | 16 | **~13.7 ms** | desktop-class | [12] |
| PIPG (first-order) | 3-DoF PDG w/ LCvx | ~25 | **<70 ms; beats BSOCP**, meets NASA rate | embedded + **GPU** | [12][13] |
| Chen et al. (SCvx + LCvx, **with aero**) | atmospheric PDG | — | **~0.6 s** (9× faster than public solvers) | **rad-hard flight proc** | [7] |
| Jang & Lee custom IPM | Mars landing SOCP | 40 (120 vars) | **~2.5 ms** | ARM Cortex-A9 650 MHz | [14] |
| Jang & Lee custom IPM | same | 40 | **sub-ms** | desktop Intel i5-13600K | [14] |
| ECOS (generic, C, open) | general SOCP | any | baseline; ~2× slower than custom | any | [ec] |

**Read-through for us.** A *single* SOCP of our size (N≈20–40, states (r,v,z)=7 + control (u,σ)=4 per
knot → ~200–450 variables) solves in **single-digit ms on a desktop core** with a custom IPM, or ~sub-ms
to a few ms with ECOS/QOCO. A **10 Hz replan (100 ms budget) on one desktop core is comfortably
realistic for LCvx** — you have ~30–100× headroom. **The aero case (SCvx, 3–10 iterations) lands at
~10–60 ms/call on a desktop core** → **10 Hz still fits, 20-30 Hz plausible.** Chen's 0.6 s is on a
*rad-hard* processor (100× slower than our desktop); on this PC's CPU it would be single-digit ms per
iteration. So **compute is NOT the blocker** for an onboard convex layer here — determinism,
validation, and the aero-model fidelity are.

Note for the **cuda-mppi lane**: PIPG is *matrix-inverse-free and GPU-native* [13]. There is a published
"library-free GPU framework for 6-DoF PDG using PIPG within SCP" for large Monte-Carlo. If a convex
oracle ever wants the same K=16384-class throughput you are building for MPPI, PIPG-on-GPU is the
convex-side analog and would share your reduction/determinism infrastructure. Worth a cross-lane look.

### B.2 Scoping the `tools/` G-FOLD oracle build (C/C++, per house rules)

The repo's planned "G-FOLD oracle" in `tools/` — here is what building it actually takes, in two tiers:

**Tier 1 — offline optimal-divert / min-fuel ORACLE (recommended first; ~1–2 k LOC, ~2–4 days).**
- Purpose: a *ground-truth* trajectory generator, not onboard. Answers "what is the fuel-optimal
  landing trajectory from this IC, and does one exist?" — the thing you validate MPPI *against* and
  warm-start *from*.
- **Solver choice: ECOS** (open-source, ANSI-C, ~small, no deps, SOCP-native, deterministic exit) —
  vendor it into `tools/gfold/`. Alternative: **QOCO/QOCOGEN** [15] (open-source C code-gen SOCP with
  quadratic objective; Chari & Açıkmeşe; generates dynamic-memory-free C — better if you later go
  onboard). Do NOT hand-roll an IPM for Tier 1; that is Tier-2 work.
- LCvx formulation from §A.1, N≈30, free-tf via golden-section over ~10 tf samples. Aero: for the
  *terminal/vacuum* comparison, ignore it (validates against TERMINAL). For AERO, add the §A.3 SCvx
  loop (linearize a_aero about the previous iterate; trust region; 5–8 iterations).
- **Validation is already half-built:** `runs/sandbox/ceiling.c` is an *independent* optimal-divert
  oracle (bang-bang / velocity-capped rest-to-rest under the true time-varying a_max(t)) that reports
  **D_phys = 1107 m** for the nominal AERO IC. The G-FOLD SOCP oracle's max-divert answer MUST match
  ceiling.c's D_phys to within discretization error — that is your acceptance test, and it is a genuine
  cross-check (two totally different methods: convex SOCP vs analytic bang-bang). Parity with
  `ceiling.c`'s parity block (US76 atmo, frozen CA/CN, engine model) is mandatory (directive 7 spirit).
- LOC estimate: ECOS vendored (~15 k LOC, but it's a dependency you don't write) + our layer: problem
  setup / matrix assembly ~500, LCvx variable transforms ~150, SCvx loop + linearization ~400,
  free-tf search ~100, ceiling.c-parity harness + I/O ~300. **≈ 1.5 k LOC of our code.**

**Tier 2 — onboard real-time solver (only if §C says go onboard; ~3–6 k LOC, ~2–3 weeks).**
- Custom fixed-topology IPM or PIPG in C, dynamic-memory-free, fixed iteration count, **bit-deterministic**
  (the hard part — matches non-negotiable #2). This is where QOCOGEN's generated code or a PIPG port
  earns its keep. Determinism: fixed-order reductions, no unordered FP, pinned iteration budget.
  Two determinism layers like the MPPI plan (fp64 memcmp of the plant; solver control-sequence hash).
- Must run inside the 2 ms sim-step budget as a replan every ~10 Hz (i.e. one solve per ~5 sim steps) —
  feasible per §B.1, but the *determinism + parity* work dominates the schedule, not the solve time.

---

## C. The honest comparison FOR US: convex vs MPPI-at-capacity for the wind-disturbed divert

**The framing that matters.** Our vertical channel is solved (97% TERMINAL). The open problem is the
**wind-disturbed lateral divert + arrival quality** from 12 km. The *physical* ceiling of that problem
is known (ceiling.c: **D_phys ≈ 1107 m** at v_lat≤30 m/s; the free bang-bang upper bound is 2492 m but
is unphysical — it builds 125 m/s cross-range that violates the small-AoA lift model). **MPPI at K=256
already realizes ~0.70·D_phys ≈ 775 m** (the observed 400-500 m land radius against a mean-500 offset).
So the entire value-of-convex question reduces to: **can an onboard convex solve realize a larger
fraction of the 1107 m velocity-capped ceiling than MPPI-at-capacity can, on wind-dispersed ICs?**

Four honest observations before the ranking:

1. **LCvx is the wrong tool for the aero divert** (proven, §A.3). Anyone who proposes "just run G-FOLD"
   is implicitly proposing SCvx-with-aero, with its local-optimality and iteration cost — or is
   proposing to run LCvx *ignoring* the aero, which throws away the lift that does most of the
   first-phase divert. Don't let the G-FOLD brand name paper over this.

2. **The min-fuel optimum rails the throttle** (Ridderhof/Tsiotras [11]). A pure min-fuel convex
   solution sits at T_min or T_max with *zero* margin for the wind it didn't see — the same pathology
   our D-012 overspeed-brake exists to fix. So an *open-loop* convex reference is fragile under exactly
   our disturbance; it needs either (a) stochastic throttle margins baked in, or (b) a tracking
   controller with authority reserved — which is what MPPI already is.

3. **MPPI's misses are off-pad reach, not cost-machinery failure** (per the handoff: 44/60 best-ever,
   healthy softmax, misses are "off-pad reach"). That means MPPI is *authority/capacity*-limited, and
   the fleet's K→1024→16384 CUDA push is the direct lever. A convex solve doesn't add authority the
   plant doesn't have — the 1107 m ceiling is the same for both. Convex can only help if it *allocates*
   the available authority better (earlier, smoother, less wasteful reversal) than MPPI's sampled search
   does, closing the 0.70→~0.90 realization gap. That is plausible (convex finds the true optimum of its
   model; MPPI approximates it by sampling) but it is **not guaranteed** and it competes with simply
   giving MPPI more samples.

4. **Determinism tax.** Every onboard option must be bit-deterministic (non-negotiable #2). MPPI already
   cleared this bar (Philox, fixed reductions). A convex IPM/PIPG must re-clear it (fixed iteration
   count, fixed-order arithmetic) — real work, and a reason to prefer the *offline* oracle first.

### Ranked options (my recommendation)

**Rank 1 — Build the G-FOLD oracle as an OFFLINE tool (Tier 1), NOT onboard (yet). [do this]**
Highest value / lowest risk. It (a) gives a second, independent optimal ceiling to cross-check
ceiling.c and validate that MPPI-at-capacity is genuinely near the physical limit (if MPPI hits ~0.9·
D_phys, the divert problem is *closed* and further guidance work is wasted — the answer is dispersion
retune, per ceiling.c's own recommendation of mean 300-350/σ100); (b) produces fuel-optimal reference
trajectories usable as **MPPI warm-starts** (our coordinator already found warm-starting MPPI with the
hoverslam recipe was "the big unlock" — a *convex-optimal* warm-start is strictly better than the
hand-tuned sqrt-profile warm-start, and would likely lift MPPI's realized fraction toward the ceiling);
(c) costs ~1.5 k LOC and validates against an oracle we already have. No determinism tax (offline).
**This is the move.**

**Rank 2 — Convex REFERENCE + MPPI TRACKING (if going onboard). [the right onboard architecture]**
Do NOT replace MPPI with a convex solve. Instead: run an SCvx-with-aero solve at a slow rate (~1–5 Hz)
to produce a fuel-optimal *nominal* divert trajectory, and let **MPPI track it** at 10 Hz with the
authority-reserving, disturbance-rejecting behavior it already has. This is the textbook robust
architecture (open-loop-optimal feed-forward + feedback that owns the wind), it directly answers the
Ridderhof/Tsiotras rail-the-throttle problem, and it reuses everything: MPPI's cost machinery, its
determinism, its HIER 3-DoF-translation abstraction (which *matches* the reusable-launcher SCvx
"3-DoF-in-OCP-under-perfect-attitude" convention [survey 108]). The convex layer supplies the *global*
structure MPPI's local sampling can miss; MPPI supplies the robustness the open-loop optimum lacks.

**Rank 3 — Pure onboard convex (SCvx) REPLACING MPPI. [not recommended]**
Wrong tool as a *replacement*. It loses the sampled robustness to un-modeled wind, rails the throttle,
carries a local-optimality-only guarantee in the aero phase, and its main claimed advantage
(finds-the-optimum) is undercut by the fact that the optimum is a *fuel* optimum, whereas our objective
is *landing probability under dispersion* — a different cost that MPPI's terminal-penalty formulation
targets directly. Only reconsider if MPPI-at-K16384 plateaus *below* the ceiling AND the oracle shows
convex realizes materially more of D_phys on the *same* ICs.

**Rank 4 — LCvx-ignoring-aero onboard. [no]**
Throws away first-phase lift authority; would underperform even the current reactive law on AERO. Only
valid for the already-solved TERMINAL, where it adds nothing.

**Bottom line for lanes main + kprobe + cuda-mppi.** The divert problem is *capacity-limited against a
known 1107 m ceiling that MPPI already realizes ~70% of*, so the fleet's instinct (K→capacity, CUDA) is
correct and is the shortest path to both gates. Convex guidance's best contribution here is **as an
offline oracle + warm-start generator (Rank 1)**, and if it ever goes onboard, **as a reference MPPI
tracks (Rank 2)** — never as an MPPI replacement. Build the Tier-1 oracle; it pays for itself as a
ceiling cross-check and a superior warm-start regardless of whether anything convex ever flies.

---

## D. Citations — papers, flight demos, open-source, worth reading

**Foundational lossless convexification / G-FOLD**
- [1] Açıkmeşe, Ploen. "Convex Programming Approach to Powered Descent Guidance for Mars Landing."
  JGCD 30(5), 2007. — the origin of LCvx for the thrust annulus.
- [2] Blackmore, Açıkmeşe, Scharf. "Minimum-Landing-Error Powered-Descent Guidance for Mars Landing
  Using Convex Optimization." JGCD 33(4), 2010. — min-landing-error objective; the "land-or-min-miss".
- [3] Açıkmeşe, Carson, Blackmore. "Lossless Convexification of Nonconvex Control Bound and Pointing
  Constraints in the Soft Landing Optimal Control Problem." IEEE TCST 21(6), 2013.
  (larsblackmore.com/iee_tcst13.pdf; ACC'11 precursor: CarsonAcikmeseBlackmoreACC11.pdf) — the pointing
  cone convexification.
- [4] Harris, Açıkmeşe et al. — maximum-divert PDG with linear/quadratic state constraints (velocity
  bounds active); LCvx-with-state-constraints theory. See survey [8] refs [77][78].

**Successive convexification (6-DoF, aero-capable)**
- [5] Szmuk, Reynolds, Açıkmeşe. "Successive Convexification for Real-Time 6-DoF Powered Descent
  Guidance with State-Triggered Constraints." arXiv:1811.10803, 2018. — velocity-triggered AoA to cap
  aero loads; the state-triggered-constraint mechanism.
- [9] Szmuk, Reynolds, Açıkmeşe, Mesbahi, Carson. "Successive Convexification for 6-DoF Powered Descent
  Guidance with Compound State-Triggered Constraints." arXiv:1901.02181 / AIAA SciTech 2019-0926.
- Szmuk, Açıkmeşe. "Successive Convexification for 6-DoF Mars Rocket Powered Landing with
  Free-Final-Time." arXiv:1802.03827, 2018. — the free-tf time-dilation SCvx.
- [10] Uzun, Açıkmeşe, Carson III. "Sequential Convex Programming for 6-DoF PDG with Continuous-Time
  Compound State-Triggered Constraints." arXiv:2510.09610, 2025. — models aero drag explicitly
  (½ρ‖v‖ S_A C_A v), exact-penalty SCvx, K=15, ECOS/MOSEK, two-phase thrust; the current SOTA formulation.

**Atmospheric / Earth-booster aero PDG (the regime that matches AERO_OFFSET)**
- [7] Chen, Yang, Wang, Gan, Chen, Xu. "A Fast Algorithm for Onboard Atmospheric Powered Descent
  Guidance." IEEE TAES, 2023. arXiv:2209.04157. — FIRST onboard aero PDG: LCvx (thrust) + SCvx (aero);
  0.6 s on rad-hard proc, 9× faster than public solvers, structure-exploiting IPM + warm-start.
- [survey ref 108] SCvx for reusable launchers over extended envelope: 6-DoF model, 3-DoF-in-OCP under
  perfect-attitude-control — the abstraction that matches our HIER. (Via [8].)
- [survey ref 85] 2-D reusable-rocket-return-to-Earth coordinating thrust + aero as controls. (Via [8].)

**ZEM/ZEV (feedback alternative — cheap, closed-form, but no hard constraints)**
- Guo, Hawkins, Wie. "Applications of Generalized ZEM/ZEV Guidance..." — the base ZEM/ZEV law.
- Zhang, Guo et al. "Improved ZEM/ZEV Feedback Guidance for Mars Powered Descent Phase." Adv. Space
  Res. 54(11), 2014. — collision-avoidance / glideslope-aware ZEM/ZEV.
- "Two-Phase Zero-Effort-Miss/Zero-Effort-Velocity Guidance for Mars Landing." JGCD (10.2514/1.G005242).
  — accommodates thrust-magnitude limits and abnormal ICs. NOTE: ZEM/ZEV is optimal *only in uniform
  gravity* and does not natively enforce the thrust annulus or aero — it is the cheap feedback cousin,
  and is essentially what our entry_divert_step ZEM/ZEV bank already is (KR 2.0/KV 3.5 in sim.c).

**Robust / stochastic (the wind caveat — READ [11])**
- [11] Ridderhof, Tsiotras. "Minimum-Fuel Closed-Loop Powered Descent Guidance with Stochastically
  Derived Throttle Margins." JGCD 44(3), 2021. — WHY min-fuel rails the throttle and how to reserve
  feedback authority; the principled version of our D-012 overspeed brake.
- Ridderhof, Tsiotras. "Minimum-Fuel Powered Descent in the Presence of Random Disturbances." AIAA
  SciTech 2019-0646. (dcsl.gatech.edu/papers/aiaa18.pdf).

**Embedded solvers (Tier-2 candidates)**
- [6] Dueri, Açıkmeşe, Scharf, Harris. "Customized Real-Time Interior-Point Methods for Onboard
  Powered-Descent Guidance." JGCD 40(2), 2017, pp.197-212. — custom SOCP IPM, 2-3 orders faster than
  generic; the onboard IPM reference. (Foundation: Dueri, Zhang, Açıkmeşe, automated SOCP code-gen, 2014.)
- [12] Kamath, Elango, Açıkmeşe et al. "A Customized First-Order Solver for Real-Time Powered-Descent
  Guidance." AIAA SciTech 2022-0951. — PIPG; SeCO ~13.7 ms/16 nodes.
- [13] Yu, Elango, Kamath, Açıkmeşe. PIPG / "Proportional-Integral Projected Gradient Method for Conic
  Optimization" + GPU-native 6-DoF PDG SCP framework. — matrix-inverse-free, GPU, beats BSOCP, meets
  NASA update rate. **Cross-lane relevance to cuda-mppi.** (Repo: github.com/UW-ACL/pipg-demo.)
- [14] Jang, Lee (KAIST). "Customized Interior-Point Methods Solver for Embedded Real-Time Convex
  Optimization." arXiv:2505.14973, 2025. — static-alloc C IPM; Mars landing ~2.5 ms on ARM A9, sub-ms
  desktop; N=40.
- [15] Chari, Açıkmeşe. "QOCO: A Quadratic Objective Conic Optimizer with Custom Solver Generation."
  Math. Prog. Comp., 2026. — open-source C SOCP + QOCOGEN dynamic-memory-free code-gen. Good Tier-1/2
  solver. (github QOCO.)
- [ec] Domahidi, Chu, Boyd. "ECOS: An SOCP solver for embedded systems." ECC 2013. — the default
  vendor-in solver for Tier 1; ANSI-C, tiny, deterministic.

**Surveys / tutorials (start here for depth)**
- [8] Wang (Zhenbo). "A Survey on Convex Optimization for Guidance and Control of Vehicular Systems."
  arXiv:2311.05115, 2023. — the aero-handling taxonomy (§A.3 quotes it); best single map of the field.
- Malyuta, Reynolds, Szmuk, Lew, Bonalli, Pavone, Açıkmeşe. "Convex Optimization for Trajectory
  Generation." arXiv:2106.09125, 2022 (IEEE CSM). — 68-pp tutorial with the full LCvx/SCvx/GuSTO
  derivations and worked rocket-landing example; the implementer's bible. SCP Toolbox: malyuta.name.
- Malyuta, Reynolds, Szmuk et al. "Advances in Trajectory Optimization for Space Vehicle Control."
  arXiv:2108.02335. Annual Reviews in Control 2021.

**Flight demonstrations**
- [fd] Açıkmeşe, Aung, Casoliva, Mohan, Johnson, Scharf, Masten et al. "Flight Testing of Trajectories
  Computed by G-FOLD: Fuel Optimal Large Divert Guidance Algorithm for Planetary Landing." AAS/AIAA
  2013 (Xombie). — 800 m divert initiated at 290 m altitude moving crosswise; BSOCP solved onboard in
  tens of ms. NASA/JPL–Masten Xombie campaign (nasa.gov gfold_tests). **The proof it flies.**
- Onboard Dual Quaternion Guidance for Rocket Landing (Kamath et al., arXiv:2508.10439, 2025) — NASA
  SPLICE / Descent-and-Landing-Computer HIL; PIPG/SeCO; **note: aero NOT modeled** — the modern
  onboard stack is still vacuum-ish for the powered phase.

**Open-source implementations worth reading**
- Ibrassow/soft_landing_mpc (GitHub, MIT) — Python/CVXPY LCvx soft-landing MPC, fuel/landing-error/
  prioritized variants; readable reference for the §A.1 formulation.
- UW-ACL/pipg-demo (GitHub) — PIPG trajectory-optimization demo.
- QOCO / QOCOGEN (github, open-source C) — production-grade SOCP + code-gen.
- SCP Toolbox (malyuta.name) — the SCvx/GuSTO/PTR reference implementations from tutorial [Malyuta 2022].

**Our own artifacts this brief leans on (cite in the oracle build)**
- `runs/sandbox/ceiling.c` — independent optimal-divert oracle; D_phys=1107 m, MPPI realizes ~0.70·D_phys.
  The G-FOLD SOCP oracle's max-divert MUST match this. Parity block (US76/CA-CN/engine) is the template.
- `runs/agentB_mppi_design.md` §5 — the MPPI CUDA/PIPG-adjacent determinism+parity plan.
- `HANDOFF_2026-07-18_NIGHT.md` — current state (ENTRY 88, AERO 73.3, TERMINAL 97), non-negotiables.
