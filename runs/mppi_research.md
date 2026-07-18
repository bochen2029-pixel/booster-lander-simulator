# MPPI scaling & variants — web-research brief for our lateral-only HIER MPPI

**Author:** lane `mppi-research` (opus-4.8, id hlmjyq7m) · **Date:** 2026-07-18 night · **Scope:** research only, NO code changes.
**Audience:** lanes `main`, `kprobe` (K-sweep), `cuda-mppi` (K=16384 port).

This brief distils the information-theoretic MPPI lineage (Williams/Theodorou), the mppi-generic GPU
library, and the recent theory/variant literature (2022–2026) into ranked, implementable recommendations
**for our specific controller** — not generic MPPI advice. Every rec is tagged
`[cheap | moderate | expensive]` (implementation cost) and `[CPU-now | CUDA-port | either]` (where it lands),
and the variant table (§C) additionally scores each on **determinism compatibility** (our sacred constraint:
no unordered FP reductions, fixed sample counts, seeded Philox streams only).

## 0. As-built recap (what we are scaling)
Lateral-only HIER MPPI: **K=256**, **H=200 knots × 25 ms = 5 s** horizon, **10 Hz** replan (decimated 50 Hz),
**2 lateral-accel channels** (throttle channel σ=0, hoverslam owns vertical). **OU-colored** per-channel noise,
stationary **σ_alat=1.5 m/s²** (small *because we perturb a good warm-start*, not zero), θ=0.15. Gamut clamp
**±3.2 m/s²**. Warm-start = hoverslam recipe forward-shot each replan (closed-loop; carried corrections REGRESSED
per D-009). Softmax with **ESS-servoed λ** (bisection, band 3–20 % of K, λ₀=30, normalized costs O(100–1000)).
ZEM terminal anchored at the ignition gate. OpenMP CPU, fixed **pairwise-tree reduction**, **Philox** counter RNG
(lane=rollout id k, counter=(replan, t·NCH+ch)). Current: **44/60 = 73.3 %** AERO s42 (best-ever); misses are
**off-pad reach**, not touchdown-quality — i.e. an **exploration/authority** shortfall, not an evaluation one.

The single most important theoretical takeaway up front: **the product λ·Σ·D governs everything** (D = cost
Hessian). Convergence, ESS, and suboptimality all key off this product — so K, σ, and λ must be reasoned about
*jointly*, never as independent knobs. Details below.

---

## (a) How should σ and λ scale as K grows 256 → 16384?

### Theory
**CoVO-MPC (Yi/Pan et al., L4DC 2024, arXiv 2401.07369)** is the definitive analysis. For a quadratic cost
`J(U)=UᵀDU+Uᵀd`, the MPPI update is a **contraction** toward the optimum with rate (Thm 4.1):

> `‖U_out − U*‖ / ‖U_in − U*‖ →_p ‖(2λΣD + I)⁻¹‖ < 1`   (N→∞)

and the cost bound `J(U_out)−J* ≤ (J(U_in)−J*)·‖(I + 2λD^½ΣD^½)⁻²‖`. Two consequences that dictate scaling:

1. **Smaller λ (or larger Σ) ⇒ faster contraction, but higher sample complexity** (variance on `U_out` grows).
   This is the core trade: contraction speed is *bought* with samples. So the natural use of a bigger K budget is
   to **afford a sharper λ / richer Σ** — not merely to average more draws at the current settings.
2. **Suboptimality is *second-order* in the exploration scale.** In the smooth/unconstrained regime the growth of
   input-trajectory suboptimality is **second-order (quadratic) in the MPPI exploration uncertainty**, and the
   optimal covariance (Cor. 4.4) is `Σ* ∝ D^(−1/2)` (det-constrained): **large variance along cost-smooth
   directions, small along sharp ones.** This is exactly the theoretical license for our *small* σ around a good
   warm-start: because the warm-start already sits near the valley floor, the residual curvature is high, so small
   σ is correct — and it is only second-order harmful to be slightly too small, but can be first-order harmful to
   rail beyond authority (which is precisely the A_LAT_GAMUT lesson we already learned by measurement in D-009).

**ESS ↔ K ↔ λ coupling (adaptive-cooling analysis, arXiv 2607.14245, 2026; kong1992 ESS):**
`ESS ≥ K · exp(−2·E[J]/λ)` and `log(ESS) ≤ H_M` (Shannon entropy of the weights). Since **ESS scales linearly in K
at fixed λ**, raising K from 256→16384 (×64) multiplies ESS by 64 at fixed λ. To hold ESS in the *same fractional
band* (our 3–20 %·K servo already does this in absolute-fraction terms), the servo will **naturally drive λ DOWN
(sharper)** as K rises — which is the theoretically desired move (item 1: spend samples on a sharper λ). **Our
ESS-band servo is the right mechanism; it auto-scales λ with K. Do NOT hard-code λ per K.**

### Reported practice
- Temperature is *the* sensitivity knob; "generally lower λ works better" once ESS is protected (pytorch_mppi
  guidance; emergentmind MPPI topic). Practitioners subtract `min_k` (or a `ρ=min(J−λ·IS)` baseline) before
  exponentiating for conditioning — **we already do** `beta=min_k C_k`.
- **Sampling σ is usually held FIXED as K grows** (survey arXiv 2511.08019: fixed-Σ is what keeps the softmax update
  *concave*; jointly optimizing mean+Σ loses concavity and needs iteration). So the default correct answer is:
  **hold σ fixed, let the ESS servo retune λ.** Adapting Σ is a separate opt-in (§C, CoVO/CMA rec).

### → Recommendations
- **A1 `[cheap][either]` Keep σ fixed at 1.5; do NOT scale σ with K.** Theory says σ is already near-right for a
  warm-start-anchored problem, and fixed-Σ preserves update concavity. If anything, a bigger K budget should first
  be spent on λ (via the servo), not σ.
- **A2 `[cheap][either]` Trust the ESS-servo to lower λ as K grows — but widen LAMBDA headroom checks.** At K=16384
  the servo can and should reach a *smaller* λ than at K=256 (ESS ×64 headroom). Verify λ is not pinned at
  `LAMBDA_MIN=2.0` at large K; if it pins, **lower LAMBDA_MIN** (e.g. to 0.5) so the servo can exploit the sample
  budget. This is the single highest-leverage large-K tuning action and it is free.
- **A3 `[cheap][CUDA-port]` Re-express the ESS band as absolute counts, not just fractions, and sanity-check it.**
  3 %·256 ≈ 8 effective samples is already thin; 3 %·16384 ≈ 490 is luxurious. Consider *narrowing* the fractional
  band at large K (e.g. 1–5 %·K) so λ sharpens more aggressively and the plan tracks the best rollouts harder —
  this is where extra K converts to extra *precision* (tighter off-pad reach), which is our actual bottleneck.
- **A4 `[moderate][either]` A slightly *larger* σ is worth a probe once K is large** — but only along the
  cost-smooth (cross-range-reach) direction. Our misses are off-pad *reach*; CoVO says put variance where the cost
  is smooth. A σ_alat probe at {1.5, 2.0, 2.5} **at K≥1024** may extend reach where K=256 cannot afford the
  exploration. (This partially contradicts the "keep σ small" default — the resolution is that the default holds at
  *small* K; a large K budget re-opens the exploration/σ trade. Rank *below* A2.) **Compat note:** this is exactly
  the sweep `kprobe` should append once the K-axis is mapped — cross K × σ, not σ alone.

---

## (b) Does vanilla MPPI saturate with K? What breaks first?

**Yes — vanilla MPPI rate saturates, and it saturates *early* relative to K=16384.** The evidence is consistent:

- **Empirical:** autonomous-racing MPPI shows **diminishing returns past ~1920 samples per gradient, with only
  small gains beyond** (racing study surfaced via the diminishing-returns search). Deployed drone-racing MPPI runs
  at **K=1024–2048 @ ~50 Hz** and treats that as sufficient (arXiv 2509.14726, RA-L Feedback-MPPI 10218402). The
  Georgia-Tech AutoRally stack itself races real hardware at low-thousands samples.
- **Theory of *why* it saturates:** MPPI is a **zeroth-order** importance-sampling estimator. Adding samples reduces
  the *Monte-Carlo variance* of the weighted-mean estimate as ~1/√K, but it **cannot improve the estimate beyond the
  support of the proposal**. If the good trajectory is not *in* the sampled set — because σ is too small, the noise
  is white-and-jittery, or the warm-start is biased — more samples just estimate the *wrong* mean more precisely.
  The survey (2511.08019 §2.4.4) makes the mechanism explicit: sample complexity rises when λ is small, the prior is
  tight (low exploration), or the cost is steep — "because these factors interact it is hard to predict K a priori;
  in practice use more samples when feasible or **monitor ESS and adjust**."

**What breaks first — exploration, not evaluation.** For our controller specifically:
- Our **evaluation** side is healthy: rollouts use the shared EOM at RK4 25 ms, the cost is normalized and well-
  conditioned (ESS stays in-band), and touchdown quality is good (td_v mean 2.95). Adding K sharpens an *already-
  good* estimator — modest gains.
- Our **exploration** side is the binding constraint: misses are **off-pad reach**, i.e. the sampled set does not
  contain a trajectory that both reaches the far seed *and* arrives nulled. That is a *proposal-support* problem —
  fixed by richer/better-directed noise and/or a sharper λ that commits to the best reaching rollout, **not** by K
  alone. This is why (per §C) a *variant* (colored/lifted noise, or CoVO covariance, or an extra iteration) is
  likely to beat "K=256 → K=16384 vanilla" at converting compute to landings.

**What practitioners change at large K:** (i) they let λ sharpen (ESS servo), (ii) they switch to
**colored/correlated noise** so each of the many samples is a *coherent maneuver* rather than a jitter (Vlahov;
iCEM), (iii) they **adapt Σ** (CoVO, CMA in MPOPI), and/or (iv) they spend the budget on **multiple update
iterations per replan** rather than one giant batch (MPOPI: 10 samples × 3 iters beat 30 × 1).

### → Recommendations
- **B1 `[cheap][either]` Set expectations: vanilla K 256→16384 will likely give a *small* rate bump (a few points),
  not a step change** — because our bottleneck is exploration/reach, which K does not directly fix. `kprobe`'s
  256/512/1024(/2048) curve should be read as *"where does vanilla flatten"*; expect flattening by ~1–2 k samples
  consistent with the racing literature. **This is the load-bearing message for the CUDA value proposition:** the
  win from 16384 is real but is *precision/robustness under dispersion*, not a large mean-rate jump — so pair the
  port with a variant (§C) to actually move AERO toward 90 %.
- **B2 `[cheap][either]` Instrument the saturation directly.** Log per-replan `ESS`, `cspread=cmax−beta`, and the
  fraction of replans where the *executed* knot came from a rollout that beat the warm-start baseline cost. If ESS
  is already comfortably in-band at K=256 and cspread is modest, that is positive proof the estimator is
  proposal-limited (exploration), and K alone will saturate — quantifying this de-risks the whole capacity thesis.
  (We already emit ESS/beta/cspread under `MPPI_DBG`; just aggregate them across a batch.)
- **B3 `[moderate][CUDA-port]` At large K, prefer "more iterations" over "one huge batch" if rate plateaus** — see
  §C MPOPI rec (C4). This is the highest-value *structural* use of a 16384-sample GPU budget.

---

## (c) Variants that beat vanilla at FIXED K for tight-constraint problems — RANKED

Our problem is a **tight-constraint, disturbance-driven** landing (off-pad = death, wind is the loss driver).
Ranked by **(implementation cost) × (expected gain for us) × (determinism compatibility)**. Determinism scoring:
✅ = fixed sample count, seeded streams, deterministic reduction preserved as-is; ⚠️ = needs care (extra reduction
or per-replan solve must be made order-fixed); ❌ = introduces data-dependent iteration/adaptation that is hard to
make bit-exact.

| Rank | Variant | What it changes | Expected gain for US | Cost | Where | Determinism |
|---|---|---|---|---|---|---|
| **1** | **Colored/OU noise — we already have it; *tune it*** | Low-freq-biased (PSD∝1/f^γ) noise so each sample is a coherent maneuver | **High** — our misses are reach; coherent low-freq cross-range pushes are exactly what explores far-seed reversals | cheap | either | ✅ (we already OU; γ-tuning is param-only) |
| **2** | **CoVO optimal covariance (offline/diagonal)** | Set Σ ∝ D^(−1/2), variance along cost-smooth dirs | **High** — 43–54 % cost cut reported; puts exploration on the reach axis | moderate | either | ⚠️ (Σ from a fixed per-replan Hessian proxy; keep it order-fixed) |
| **3** | **MPOPI: L iterations/replan (CEM+CMA flavor)** | Re-solve the update L× per replan, same total samples | **High at large K** — "10×3 beats 30×1"; converts GPU budget to precision | moderate | CUDA-port | ⚠️ (L fixed, each iter deterministic → OK; just fix L) |
| **4** | **Tube-/Robust-MPPI (RMPPI) ancillary tracker** | Nominal MPPI + tracking controller rejects disturbance; free-energy-bounded | **Medium** — wind is our loss driver; but we *already* reject via 10 Hz replan + C14 integral + hoverslam blend | expensive | either | ⚠️ (adds a 2nd system dim z; ancillary must be deterministic) |
| **5** | **log-MPPI (normal·log-normal noise)** | Heavier-tailed proposal → fewer infeasible-cluster collapses | **Low-Med** — helps when *all* samples land in high-cost regions; our warm-start already avoids that | cheap | either | ✅ (per-sample scale draw, seeded) |
| **6** | **Smooth/lifted MPPI (S-MPPI)** | Sample in a lifted (derivative) space → smooth actions | **Low** — we already smooth (Savitzky-Golay) + OU; S-MPPI *degrades* at few samples/long horizon | moderate | either | ⚠️ (extra integrator state per rollout) |
| **7** | **CEM-elite (hard elite set) hybrid** | Top-e elites, unweighted mean | **Low/negative** — MPPI's soft weighting ⊃ CEM; elite-set memory across steps = our regressed persistent-correction | cheap | either | ✅ but ❌-fit (elite carry ≈ ucorr, regressed D-009) |

**Rationale per rank (the parts that matter for us):**

- **#1 Colored/OU — validate and tune γ, don't just keep it. `[cheap][either]`** Vlahov/Gibson/Theodorou (*Low
  Frequency Sampling in MPPI*, arXiv 2404.03094) show colored sampling is **transformative for high-control-lag
  systems**: on a lagged off-road vehicle, white-Gaussian success **0 %** → colored **83.3 %**; double-integrator
  cost 13 815 → 11 081 at σ=1.5; quadrotor success 24 %→43 %. Grid-fin attitude reversal *is* a lagged actuator, so
  this regime is ours. Our OU(θ=0.15) already imposes correlation, but **OU ≠ tuned colored noise** — OU is a
  fixed-time-constant red-ish process, whereas the paper samples an explicit PSD∝1/f^γ and finds the *right γ*
  matters (γ=2 red gave the wins above). **Key implementation nuance they provide:** their variance-normalization
  `ζ` **decouples smoothness (γ) from variance (σ)** so time-domain std stays σ regardless of γ or horizon — our OU
  `drive=σ√(2θ−θ²)` does the analogous stationary-variance normalization, so we are structurally aligned. **Rec:**
  treat θ (equivalently the OU correlation length) as a first-class tuning axis alongside σ; probe a *longer*
  correlation length (smaller θ, e.g. 0.08–0.10) so cross-range pushes persist over more of the 5 s horizon — this
  is the cheapest credible lever on off-pad reach. Overhead is negligible (paper: +0.12 ms on RTX 3080), and it is
  **fully determinism-safe** (still one Philox draw per (k,t,c); OU recursion is order-independent).
  ⚠️ *If* you ever move to true FFT-based colored noise on GPU, the iFFT is a per-rollout deterministic transform
  (fine), but prefer the AR/OU recursion we already use to avoid an FFT in the bit-exact path.

- **#2 CoVO covariance. `[moderate][either]`** Σ ∝ D^(−1/2) (Cor. 4.4) puts exploration on cost-smooth directions —
  for us that is the cross-range/reach axis, precisely where we are short. Reported **43–54 %** cost reduction on
  cartpole/quadrotor incl. real hardware. **How to adopt determinism-safely:** compute a *cheap per-replan diagonal*
  Hessian proxy (e.g. from the running-cost weights Q_VXYERR/Q_RXY around the warm-start), form a **fixed diagonal
  Σ per replan** (2×2, our two lateral channels), and sample with per-channel σ_x≠σ_y. No unordered reduction, no
  extra iteration — just a state-dependent-but-deterministic σ. This is the theory-optimal version of A4 and is the
  most principled single upgrade. Rank below #1 only because it is more code and needs a Hessian proxy design.

- **#3 MPOPI (iterate the update). `[moderate][CUDA-port]`** *Model Predictive Optimized Path Integral* (arXiv
  2203.16633; legged-robot deployment arXiv 2508.11917) fuses MPPI with CEM + **CMA covariance adaptation** and runs
  **L update cycles per timestep**: "**MPPI 30 samples (1 shot) vs MPOPI 10 samples × 3 cycles = same budget, MPOPI
  wins**," and it is an **anytime** strategy (each cycle improves). For our CUDA port this is the *right* way to
  spend 16384 samples if the vanilla curve plateaus: e.g. K=4096 × 4 iterations rather than K=16384 × 1 — each
  iteration re-centers the proposal on the previous weighted mean, so late iterations explore *around a better
  point* (attacks the proposal-support problem §b directly). **Determinism:** L is a fixed compile-time count; each
  iteration is our existing deterministic solve; only requirement is to fix L and keep per-iteration Philox counters
  distinct (e.g. fold the iteration index into the counter). ⚠️ CMA's covariance *adaptation* across iterations is
  the ❌ part (data-dependent) — adopt the **iterate-and-recenter** idea but keep Σ fixed (or use the CoVO closed
  form #2) to stay bit-exact.

- **#4 Tube/Robust-MPPI. `[expensive][either]`** Williams' Tube-MPPI (nominal MPPI + iLQG ancillary tracking a tube)
  and Gandhi/Vlahov/Theodorou **RMPPI** (arXiv 2102.09027) derive a **bound on free-energy growth** as a function of
  (constraint-satisfaction level, ancillary-tracker performance, sampling error) and pick the nominal initial state
  by constrained opt to cap cost growth under disturbance. RMPPI **beats both MPPI and Tube-MPPI** on off-road
  robustness/agility. **Relevance:** our losses *are* wind-disturbance-driven, so on paper this is a fit. **But** we
  *already* implement the practical essence of a tube: 10 Hz replanning + the C14 wind-trim integral + the
  hoverslam-blend that damps residual v_xy to contact. A full RMPPI (augmented nominal/actual state, ancillary
  controller, safety logic for nominal propagation) is a large architectural addition with a doubled system
  dimension (the mppi-generic z-axis exists for exactly this). **Rec:** do NOT build full RMPPI now; instead **steal
  one idea cheaply** — RMPPI's "propagate the nominal from a *constrained* initial state so disturbance can't blow up
  cost" ≈ our warm-start already re-anchoring on the corrected closed-loop state each replan. If robustness under the
  `--inject`/gust tails (D main's Tier-B: AERO 50 % under inject) becomes the gate, revisit RMPPI as a deliberate
  ADR'd unit. Determinism ⚠️: the ancillary tracker and nominal-state opt must both be deterministic.

- **#5 log-MPPI. `[cheap][either]`** Mohamed et al. (arXiv 2203.16599) sample from a **normal·log-normal (NLN)
  mixture** so the proposal is heavier-tailed and "significantly improves trajectory feasibility" when standard MPPI
  would concentrate all samples in high-cost/infeasible regions. **For us the gain is modest** because our
  warm-start already keeps rollouts clustered near a *feasible* landing trajectory — we rarely suffer the
  "all-samples-infeasible collapse" log-MPPI cures. It is nearly free and determinism-safe (one extra seeded
  log-normal scale per sample), so it is a fine *cheap add-on* to try in the same sweep as #1, but do not expect
  much. **Compat:** heavier tails partially overlap with A4/#2 (more exploration) — don't stack all three blindly.

- **#6 Smooth/lifted S-MPPI. `[moderate][either]`** Lifts sampling into a derivative/integrator space for smooth
  actions — but the literature explicitly warns **S-MPPI degrades with fewer samples or longer horizons** (our
  H=200 is long). We already get smoothness from **Savitzky-Golay(9,3) + OU**, so the marginal value is low and the
  failure mode (long horizon) is ours. **Skip** unless chatter becomes a measured problem.

- **#7 CEM-elite hybrid. `[cheap][either]` but poor fit.** CEM (elite set, unweighted mean) is a *special case*
  MPPI's soft exponential weighting already generalizes (survey: "MPPI = CEM with softmax weights instead of a hard
  elite set"). iCEM's real wins come from (a) **colored noise** — which is our #1 already — and (b) **carrying
  elites across steps** — which for our *closed-loop* warm-start is equivalent to the persistent-correction (ucorr)
  we **measured as a regression in D-009** (baseline is recomputed from the corrected state, so carried elites
  double-count). **Rec:** do NOT pursue elite-carry; harvest iCEM's colored-noise lesson via #1 instead.

---

## (d) GPU MPPI implementation practice — for the CUDA port

Primary source: **MPPI-Generic (Vlahov et al., arXiv 2409.07563)**, the Georgia-Tech header-only C++/CUDA library
that is the reference implementation, plus the AutoRally lineage.

- **Kernel structure — use the COMBINED (fused) kernel for our sizes. `[moderate][CUDA-port]`** MPPI-Generic ships
  **two** designs and auto-selects via `chooseAppropriateKernel()`: (i) **split** dynamics-kernel + cost-kernel
  (each writes intermediate state to global memory), and (ii) **combined**, which runs dynamics+cost sequentially in
  time and **keeps intermediate state in *shared memory*, avoiding global-memory round-trips.** For a *cheap* plant
  like ours (our lean vertical proxy + 2 lateral channels + RK4, all fp64) the combined kernel is the winner — the
  physics is register/shared-memory-resident and the bottleneck is arithmetic, not global bandwidth. **Thread map:**
  `sample = threadIdx.x + blockDim.x·blockIdx.x` (x-dim over samples/rollouts), block-x also striding time when the
  1024-thread block cap is hit; **z-dim reserved for multiple systems** (this is the Tube/RMPPI hook if #4 ever
  lands). **Rec:** one rollout per thread (matches our "rollouts are independent" structure exactly), combined
  kernel, block-x a **multiple of 32** so a warp does identical work.
- **Memory layout — SoA, vectorized loads. `[moderate][CUDA-port]`** MPPI-Generic uses `float*` device buffers with
  **vectorized 32/64/128-bit accesses** and recommends output dims **divisible by 2 or 4** for coalescing, and
  **small thread-blocks**. Our state is naturally struct-of-arrays across rollouts (the `EPS[k][t][c]` and `C[k]`
  buffers already are). **Rec:** keep per-rollout state (pos/vel/quat/mass/timers) in **registers** (AutoRally
  stores "position, velocity, actuator state in thread-local registers, each thread independent"); lay the
  cross-rollout arrays out **SoA** (contiguous over k for a given (t,c)) so warps coalesce; avoid AoS-per-rollout in
  global memory. **fp64 caveat:** we are BL_HD fp64 (cuda-mppi confirmed the physics chain is already fp64); on
  sm_89 (RTX 4070 Ti SUPER) fp64 throughput is 1/64 of fp32, so fp64 rollouts are ALU-bound — the combined kernel's
  shared-memory residency matters *more* for us than for the fp32 library. Budget accordingly.
- **Per-rollout RNG — counter-based (Philox) is BEST PRACTICE; we already use it. `[cheap][CUDA-port]`** The
  literature note: MPPI-Generic does not pin its RNG method, and AutoRally generates noise **on the CPU and copies
  it to the GPU as a contiguous array** — which is a *determinism liability* (host RNG + transfer) and a bandwidth
  cost. **Our design is strictly better and is the modern best practice:** Philox is a **counter-based** stream RNG,
  so each (rollout k, step t, channel c) draw is an *independent stateless function of its address* — generate it
  **on-device inside the kernel** with zero cross-thread state and **bit-identical results regardless of thread
  scheduling.** This is exactly what makes a K=16384 GPU rollout reproducible. **Rec:** keep Philox keyed
  identically to the CPU path (seed, RNG_MPPI, lane=k, counter=(replan, t·NCH+c)); if MPOPI (#3) lands, fold the
  iteration index into the counter's hi word so iterations get disjoint streams. Do **not** adopt AutoRally's
  host-generate-and-copy pattern.
- **Reduction determinism — the one place to be careful. `[moderate][CUDA-port]`** MPPI-Generic exponentiates costs
  on the GPU but computes the normalizer η **back on the CPU**, and the paper **does not guarantee a deterministic
  reduction.** Our sacred constraint forbids unordered FP reductions, so **do not use a naive `atomicAdd`
  tree or cub::DeviceReduce with run-to-run-variable ordering.** **Rec:** replicate our CPU **fixed-topology
  power-of-two pairwise fold** on the device — K=16384 is 2¹⁴, a perfect balanced tree, so a shared-memory
  segmented pairwise reduction (log₂K=14 steps, fixed pairing) gives a **bit-identical** sum independent of warp
  scheduling, matching the host oracle within the §9.5 host/device tolerance. The `min_k` baseline (`beta`) and η
  are both reductions — do both with the same fixed-pairwise scheme. This is the crux of passing the determinism
  gate at K=16384 and is already the documented plan (agentB_mppi_design §5); the literature confirms *no library
  gives this for free*, so it is correctly a bespoke kernel.
- **Reported K and latency.** MPPI-Generic **benchmarks up to K=16384 samples** (sweep 128/256/512/1024/2048/4096/
  6144/8192/16384, each run 10 000× for timing) and demonstrates **real-time GPU performance across multiple RTX
  cards** — i.e. **K=16384 is an established, real-time-feasible operating point on desktop RTX hardware** (the exact
  per-K ms table lives only in the paper's figures and was not extractable via text fetch; the qualitative,
  load-bearing result — 16384 demonstrated real-time — is firm). Field deployments corroborate the *lower* end:
  AutoRally races at low-thousands; drone racing at **K=1024–2048 @ 50 Hz**. **Implication for our p99 ≤ 6 ms gate:**
  the library shows 16384 is reachable in real-time on RTX-class silicon *for fp32 plants*; our **fp64** rollouts
  will be slower per-sample, so **measure early** — if fp64@16384 blows the 6 ms budget, the MPOPI route (#3,
  e.g. 4096×4) or a mixed-precision rollout (fp64 only where the EOM needs it) is the fallback. This is the concrete
  number `kprobe`'s wall-time-vs-K CPU curve is meant to calibrate against for the speedup target.

---

## (e) The horizon question — 5 s + analytic terminal projection

Our design: **H=5 s** with **analytic horizon-end projections** (ZEM anchored at the ignition gate; event gates
*inside* the horizon measured byte-inert in D-009 — they don't fire until the final seconds so moving them had zero
effect on outcomes). Two literature threads bear on whether to lengthen H at higher K, or keep projection terminals.

- **Cost/samples grow with horizon; longer H is degraded by long-horizon prediction uncertainty.** Multiple sources
  (horizon-tradeoff search; Neural-Horizon MPC arXiv 2408.09781): "computational cost and **sample requirements grow
  with the prediction horizon**," and "long-horizon MPC is significantly more demanding and its performance is
  **degraded by uncertainties in long-term predictions**." For a *sampling* method this is doubly true: covering a
  longer horizon well needs *more* samples (the proposal must span a higher-dimensional control sequence), so
  **lengthening H spends the K budget on horizon coverage rather than reach precision** — the opposite of what our
  bottleneck wants.
- **Projection/value-function terminals are the *endorsed* way to get long-horizon foresight cheaply — our ZEM is
  exactly this.** The RRT#-MPPI interface (horizon-tradeoff search) proposes using a planner's **cost-to-go as the
  terminal cost of each rollout** to "**extend the horizon of MPC without proportionally increasing computational
  burden**." *The Value of Planning for Infinite-Horizon MPC* (arXiv 2104.02863) and terminal-value-function MPC
  make the same point: a good **terminal cost buys effective horizon for free.** **Our analytic ZEM projection
  (predicted landing offset `r + v·t_go` past the 5 s window) is precisely a hand-designed cost-to-go / value
  proxy** — it gives us the foresight of a ~50 s time-to-go through a 5 s rollout. This is the *recommended* pattern,
  and D-009's finding that in-horizon event gates are byte-inert is direct empirical confirmation that **the terminal
  projection, not the horizon length, is carrying the foresight.**

### → Recommendations
- **E1 `[cheap][either]` KEEP the 5 s horizon; do NOT lengthen H at higher K.** The literature is consistent that a
  longer H costs samples and imports long-horizon prediction error, while a good terminal cost gives the foresight
  for free — and we *have* that terminal (ZEM). Spending 16384 samples on more knots is strictly worse than spending
  them on reach precision / a variant (§C) at H=200. This directly answers the mission question: **the literature
  does NOT support longer H at higher K for our structure; it supports projection-based terminals like ours.**
- **E2 `[cheap][either]` Our ZEM-at-ignition-gate anchor is the textbook move — keep and *refine* it, don't
  replace it with horizon extension.** The one caveat from the value-function literature: a *biased* terminal proxy
  caps achievable quality. If AERO stalls after K/σ/variant work, the highest-value horizon-side action is to
  **improve the terminal projection fidelity** (e.g. a better `t_go`/`predict_tgo` model, or folding a cheap
  suicide-burn-feasibility-aware landing-offset estimate into ZEM) — *not* more knots. This is a terminal-cost
  design task, cheap, and determinism-neutral.
- **E3 `[moderate][either]` If you ever want a *longer effective* horizon, do it via the terminal, not the rollout.**
  The RRT#/value-function pattern (learn or precompute a cost-to-go and use it as the rollout terminal) is the
  scalable path; but for our single-goal landing the analytic ZEM already fills that role, so this is a
  low-priority, only-if-needed note.

---

## Top-5 actionable, in priority order (the TL;DR for main/kprobe/cuda-mppi)
1. **Tune the noise correlation, not just σ** (#1, `cheap`): probe longer OU correlation (θ ≈ 0.08–0.10) and, at
   K≥1024, a σ_alat sweep {1.5,2.0,2.5} on the *reach* axis. Colored/coherent low-freq pushes attack off-pad reach —
   our actual bottleneck — and are the cheapest credible lever. Determinism-safe.
2. **Let the ESS servo sharpen λ at large K; lower LAMBDA_MIN so it can** (A2, `cheap`): ESS ∝ K, so 16384 earns a
   much sharper λ than 256 — free precision, but only if λ isn't pinned at the floor. Verify and un-pin.
3. **Set the value-proposition expectation** (B1, `cheap`): vanilla 256→16384 gives *precision/robustness under
   dispersion*, not a big mean-rate jump — vanilla saturates by ~1–2 k samples (racing lit). Pair the CUDA port with
   a variant to actually move AERO→90 %. Instrument ESS/cspread to *prove* the exploration-limited regime (B2).
4. **CUDA port specifics** (§d, `moderate`): combined/fused kernel, SoA + vectorized loads, per-rollout state in
   registers, **on-device Philox** (we already have the best-practice RNG — do NOT copy AutoRally's host-generate),
   and a **fixed power-of-two pairwise reduction** on-device (K=16384=2¹⁴ is a perfect tree) — the one place the
   determinism gate is won, and no library gives it for free.
5. **Keep the 5 s horizon + ZEM terminal** (E1/E2, `cheap`): literature endorses projection terminals over long
   horizons; refine ZEM fidelity, don't add knots. If capacity plateaus, the two strongest *structural* upgrades are
   **CoVO diagonal covariance** (#2, theory-optimal reach-axis exploration, 43–54 % reported) and **MPOPI iterate-
   per-replan** (#3, e.g. 4096×4 beats 16384×1 at fixed budget) — both determinism-compatible if Σ/L are held fixed.

## Sources
- Williams, Aldrich, Theodorou — *Information-Theoretic MPC: Theory & Applications to Autonomous Driving* (arXiv 1707.02342); *MPPI: From Theory to Parallel Computation* (JGCD 2017). GT ACDS MPPI page: https://sites.gatech.edu/acds/mppi/
- Yi/Pan et al. — *CoVO-MPC: Theoretical Analysis of Sampling-based MPC and Optimal Covariance Design*, L4DC 2024 (arXiv 2401.07369; https://arxiv.org/html/2401.07369v1). **[core: λΣD contraction, second-order suboptimality, Σ*∝D^(−1/2)]**
- *Model Predictive Control via Probabilistic Inference: A Tutorial and Survey* (arXiv 2511.08019). **[taxonomy, fixed-Σ concavity, sample-complexity factors, leptokurtic/colored priors]**
- Vlahov, Gibson, Theodorou — *Low Frequency Sampling in MPPI* (colored noise; arXiv 2404.03094; https://arxiv.org/html/2404.03094). **[γ, coherent-maneuver exploration, variance-decoupling ζ, +0.12 ms]**
- Mohamed et al. — *log-MPPI: Autonomous Navigation in Unknown Cluttered Environments* (arXiv 2203.16599; code github.com/IhabMohamed/log-MPPI_ros). **[NLN mixture, feasibility]**
- Williams et al. Tube-MPPI; Gandhi/Vlahov/Theodorou — *Robust MPPI (RMPPI): Analysis and Performance Guarantees* (arXiv 2102.09027). **[free-energy growth bound, ancillary tracker, disturbance]**
- *Model Predictive Optimized Path Integral Strategies (MPOPI)* (arXiv 2203.16633); *Control of Legged Robots using MPOPI* (arXiv 2508.11917). **[CEM+CMA, L iterations/replan, 10×3 > 30×1]**
- Vlahov et al. — *MPPI-Generic: A CUDA Library for Stochastic Trajectory Optimization* (arXiv 2409.07563; https://arxiv.org/html/2409.07563; site acdslab.github.io/mppi-generic-website). **[combined vs split kernel, SoA/vectorized, K up to 16384 real-time, CPU-side η]**
- *Information-Theoretic Adaptive Cooling for Deterministic MPPI via Entropy Feedback* (arXiv 2607.14245, 2026). **[ESS≥K·e^(−2E[J]/λ), entropy-driven λ cooling ≈ our ESS servo]**
- iCEM — Pinneri et al., *Sample-efficient CEM for Real-time Planning* (arXiv 2008.06389; github.com/martius-lab/iCEM). **[colored noise + elite memory; MPPI ⊃ CEM]**
- Horizon/terminal: *The Value of Planning for Infinite-Horizon MPC* (arXiv 2104.02863); RRT#-MPPI cost-to-go terminal; *Neural Horizon MPC* (arXiv 2408.09781). **[terminal value buys effective horizon; long-H prediction error]**
- Deployment corroboration: reference-free MPPI drone racing (arXiv 2509.14726); Feedback-MPPI (RA-L, discovery.ucl.ac.uk/id/eprint/10218402); AutoRally. **[K=1024–2048 @ 50 Hz field practice; ~1920-sample diminishing returns]**
