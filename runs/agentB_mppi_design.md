# Agent B — MPPI / CUDA controller design (M4 CPU → M5 CUDA)

Verified vs Williams T-RO 2018, SMPPI (arXiv:2112.09988), log-MPPI (2203.16599),
MPPI-Generic (2409.07563). Canon §9.2–9.6 already specifies this; below is the concrete plan.

## 1. Update law (per 50 Hz replan, horizon H = 200 × 25 ms = 5 s, K rollouts)
1. Sample noise `ε_t ~ N(0,Σ)` per channel, **colored (Ornstein-Uhlenbeck θ=0.15/step)** around
   the warm-started previous solution `ū` shifted one control step. (White chatters; OU flies.)
2. Cost with IS correction: `C_k = φ(x_T) + Σ_t [ q(x_t) + γ·u_tᵀ Σ⁻¹ ε_t ]`, `γ = λ(1−α)`, α=0.02.
3. Baseline `β = min_k C_k`; weights `w_k = exp(−(C_k−β)/λ)/η`, `η = Σ_k exp(...)`.
4. Update `ū_t += Σ_k w_k ε_t^k`; **Savitzky-Golay (win 9, order 3)**; clamp; execute first 20 ms; shift.
5. Adaptive λ: `ESS = 1/Σ w̄_k²`; servo λ (bisection) to hold ESS ∈ [2%,10%]·K.

Numbers: K=8192 default / 16384 quality; control step 25 ms; λ₀=1.0;
Σ_HIER=diag(σ_throttle=0.08, σ_acc=0.6 m/s²); Σ_RAW=diag(σ_gimbal=0.8°, σ_fin=3°).

## 2. Rollout integration — KEY resolved point
Rollout integrates **RK4 at the 25 ms control step, NOT the plant's 2 ms.** The plant runs 2 ms
fp64 for truth; the rollout is an fp32 planner. Stability fine (stiffest = gimbal 2nd-order
ωn=8 → ωn·h=0.2). Contact never integrated mid-rollout (event-terminated). 25 ms vs 2 ms is the
single biggest perf lever (12×). ~0.7 GFLOP/replan at K=16384.

## 3. Parameterizations — build HIER first (M4), RAW later (M5+)
- **HIER (default):** channels = throttle + 2 lateral-accel. The §8.3 attitude-PD + allocation
  run INSIDE the rollout as shared code, so MPPI searches only 3 slow channels → K=256 converges
  on CPU. Matches the sluggish/overdamped plant (ζ=1.1 needed).
- **RAW (purist):** throttle+gimbal[2]+fins[4] (7 ch). Needs SMPPI control-derivative lifting
  (sample du/dt, integrate, add action-variation cost) to suppress chatter.
- Delays/lags live inside the rollout by construction (same `dynamics_deriv`, directive 7). The
  20 ms transport delay: plan at 50 Hz, execute first 20 ms; rollout step 0 applies the previous
  in-flight control before the new plan takes effect.

## 4. Suicide-burn feasibility terminal cost (CRITICAL — 5 s horizon vs 15–33 s burn)
At each rollout's in-air end state x_H, ask "can this still be landed?" by running the §9.1
forward-shooting vertical-channel predictor (already in guidance_hoverslam.c) on x_H — the cheap
1-D `v_ref` profile with frozen a_design, drag, mass depletion. Soft one-sided barrier:
```
φ_feas(x_H) = w_fuel·max(0, m_needed − m_remaining)
            + w_vmargin·max(0, |v_ref(h_H)| − |vz_H|)²     // behind the min-fuel manifold
            + w_ctrl·max(0, a_req(x_H) − a_max(x_H))²        // needs more decel than thrust gives
```
Full terminal = canon `40|r_xy|²+60|v|²+800·tilt²+200|ω|²+0.02(m0−m)+crash·1e6` PLUS φ_feas at
the horizon (or the plain terminal at a ground-crossing).

## 5. CUDA layout (sm_89), determinism, parity
- H2D: x0 + warm-start ū (~2–4 KB).
- K1 `rollout_cost<<<K/128,128>>>`: 1 thread/rollout, ~26-elem fp32 state in registers. Philox-draw
  ε (lane=rollout id, ctr=(t,ch)) → HIER inner loop → RK4 25 ms → accumulate cost+IS. Event-
  terminate on z-crossing (bisect ≤1 substep, freeze — no underground integration). Writes C_k[K].
  Recompute ε from Philox in K2 (zero noise storage).
- Baseline β: D2H C_k + CPU min (beats GPU reduce for K~1e3–1e4; trivial determinism).
- K2 `weight_reduce`: w_k=exp(...); **fixed-topology pairwise tree** for η and each Σ_k w_k·ε.
  NO atomics → bit-stable sm_89. `-fmad=false` on K1/K2.
- Host: ū += num/η; SGF; clamp; ESS→λ; emit plan[64] (mean ghost line) + cloud[128] (terminal
  x,y + normalized w∈[0,1]); execute first knot 20 ms.
- Fat binary sm_89 + sm_90 + compute_120 PTX.
- Parity (§9.5): same K=64 fixed-seed rollout on CPU-ref (`__host__`) and GPU (`__device__`);
  per-step |Δr|<1e-3 m over 200 steps + identical event terminations. CPU ref MUST use the same
  pairwise-tree reduction. Two determinism layers: L1 plant fp64 memcmp (M1, green); L2 MPPI
  fixed-seed solve → control-sequence hash golden (sm_89-pinned).

## 6. Build/validation gates
- M4 (MPPI CPU, K=256, HIER): AeroOffset ≥90% landed; CPU reference frozen as parity oracle;
  test against runs/regression_worstcase.json (Agent C's 16 worst ICs).
- M5 (MPPI CUDA, K=16384): (1) p99 solve ≤6 ms end-to-end incl. transfers; (2) parity green;
  (3) Terminal ≥99%, AeroOffset ≥97% with wind+gusts; (4) MPPI hash golden frozen sm_89.

## Interfaces agreed with peers
- Entry burn = qbar/heat **supervisor** (Agent A predictive trigger); MPPI owns aero-descent +
  landing burn only. In-rollout, the entry constraint = running qbar-overshoot cost + STRUCT/
  THERMAL crash indicator, so rollouts diving too fast self-penalize.
- Reuse Agent A's fin allocation + body-AoA law verbatim inside HIER rollouts.
- Protocol (Agent D owns core/protocol.h): plan[] = rollout MEAN, fresh each 50 Hz; cloud[] =
  128 terminal (x,y)+normalized weight; solver telemetry (ESS/λ/costs/p99) in STATS@10 Hz not TLM.
