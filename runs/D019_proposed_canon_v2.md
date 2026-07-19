# D-019 (PROPOSED) — Adopt CLAUDE_v2.md as canon: the perception-to-policy integration

> **STATUS: ADOPTED — operator-signed 2026-07-19 ("go, all defaults accepted").** The ADR
> block below was appended to `DECISIONS.md` as **D-019** and the adoption checklist
> executed (banner stripped + App-A inlined in CLAUDE_v2.md; RUN_STATE/README pointers
> flipped; N0 build launched same day). This file is retained as the authoring record.

---

## The ADR text (append to DECISIONS.md on signature)

## D-019 — Adopt CLAUDE_v2.md as canon: the perception-to-policy integration (2026-07-19)

CLAUDE_v2.md supersedes CLAUDE_v1.md (keep v1 for history; do not edit). The trigger: D-018
closed the M4 sampler branch and put the LEARNED NEURAL POLICY on the M4 critical path; the
design fleet delivered the four pillar docs (perception_to_policy_stack / neural_policy_design
/ perception_design / interplanetary_integration_design); the operator directed a holistic,
additive merge of the original architecture with the neural/showcase direction (engine-out ×
wind shear × moving target, simultaneously — recover whenever physics permits), with the
socket widened NOW for a future VLM target-acquisition front-end, and the UE-mesh + FluidX3D
CFD upgrade path scheduled. Key deltas, decided during v2 authoring:

- **Anchor stability:** every v1 section number keeps its meaning; v2 only ADDS
  (§4.5–4.7, §8.4, §9.8–9.9, §10.9, §13.6, §18–§20, App-G) and amends in place with tagged
  notes. Existing ADR/design-doc citations of v1 sections remain valid.
- **Directive 11 (new): "Precompute in, telemetry out — always"** — D-011's hard line
  promoted to a prime directive. Nothing nondeterministic (VLM, CFD, UE, trainer) ever
  closes a runtime loop; only frozen, versioned artifacts enter the gated loop. §20 is the
  artifact registry (weights, aero tables, acquisition traces, World params, bakes).
- **Directive 4/6 sharpenings:** learned/derived constants are versioned precompute
  artifacts consumed bit-exactly; directive 6 gains its measuring instrument — the
  reachability-frontier metric (§9.9), as a diagnostic OVERLAY that never softens a
  landed-rate gate (out-of-frontier "landings" are leak investigations).
- **The third maximum:** max-capable guidance joins max-true simulation and max-cinematic
  presentation; the §1 experience contract gains the compound-recovery finale (policy-driven
  engine-out + shear + moving-deck recovery, ALWAYS shown with the honest adjacent
  out-of-frontier failure), the talk-to-the-rocket beat, and the Mars epilogue.
- **The disturbance triad becomes canon plant modules, all default-off, byte-identity
  gated** (the D-017 pattern): `--gust` (BUILT), module ENGINE_OUT (§4.6 — rescinds v1
  §5.3's side-engine-torque neglect ONLY while armed; eng health is §4.3-legal chamber-P
  telemetry), module TARGET (§4.5 — target_xy(t) with five sources behind ONE schema:
  FIXED / SEEDED / BEACON / PERCEIVED-trace / DRAG-fenced).
- **THE WIDE SOCKET (§8.1 + App-G):** NavState gains TargetEstimate {xy, vxy, cov, deck_z,
  src, age, valid} + EngineHealth {eng_health[3], n_eng, relights_left} at nominal values
  from N0 — designed wide enough that the FUTURE VLM front-end drops in with zero
  re-architecture (source-blindness: guidance/policy cannot tell seeded from beacon from
  VLM). Byte-equality at nominal is the N0 gate. The §8.1 provenance rule binds every tier:
  never wind_world / wind_filt / truth-target.
- **§8.4 perception front-end contract:** sensor-camera is a PLANT sensor (deterministic
  raster, never the cinematic renderer); the VLM is an async precompute emitting an
  acquisition TRACE replayed bit-exactly; live vibe-instruct is FENCED; three-layer
  mis-grounding gate (confidence + innovation + rangefinder witness); bad perception =
  honest miss, never a clamp to truth. Beacon is the honest baseline. Implementation:
  runs/perception_design.md.
- **Guidance tier 3 (§9.8): GM_NEURAL** — π(legal_state)→GuidanceCmd; ~38k-param MLP;
  <10 µs; frozen fp64 weights header (NP_VERSION, regen = ADR); hand-rolled fixed-order C
  inference (no BLAS/atomics/threads); default OFF; --neural / SET_GUIDANCE_MODE 3;
  DISTILL (DAgger from MPPI, G-FOLD reach labels) → RL (SAC/PPO, warm-started) → optional
  RESIDUAL safety variant. §9.7 G-FOLD role extended to teacher/warm-start source.
- **§9.9 frontier metric:** BRS defined; recovery-rate = P(land | in-frontier) is THE
  number; realization-fraction + arrival-quality sub-metrics; ceiling.c generalized
  per-disturbance (engine-out authority debit; touchdown-relative target; per-world at N4).
- **§9.4 rescope recorded in canon** (per D-015): fp64 everywhere; the fp32-era 6 ms bar
  superseded by the measured 100 ms replan budget; goldens at K≈1024 sm_89.
- **Protocol:** v3 as-built canonized (288 B, pred_impact/ignite_h, D-013); **v4
  pre-authorized (§10.9)** as ONE validated unit at N0: TLM += TargetEstimate view +
  eng_health/n_eng + np_ver; HELLO += module bits + World hash + NP_VERSION; EVT unchanged
  (FAULT + TARGET_CHANGED suffice). TS mirror + goldens re-frozen together.
- **§13.6 N-track gate battery:** the named leak check (present-but-off byte-identity);
  determinism-on pairs; THE HELD-OUT LAW (s42/s7/s99 never trained on, plus held-out
  severe-tail conditions); per-rung parity/quality gates; the full existing battery always.
- **§14 N-track milestones** (parallel to M7/M8), mapped onto the pillar docs' P/S
  numbering: N0 widen-once (socket + v4 + capabilities-built-but-off; byte-equality gate) →
  N1 distilled GM_NEURAL ships (AERO ≥42/60, bit-deterministic, ~1000×) → N2 RL beats MPPI
  on ≥1 axis (recovery-vs-frontier, held-out) → N3 THE SHOWCASE + the M4 attempt (compound
  batch ≥ MPPI; AERO ≥54/60 ⇒ M4 GREEN via GM_NEURAL, else the honest 0.70·D_phys plateau
  verdict routes M4 to the plant-authority ADR — either outcome decisive) → N4 perception
  live + worlds. M-statuses truthed (M2/M3/M5/M6 ✓ with scopes; M4 open-redirected; M8's
  "engine-out demos" = N3's showcase rendered). Build order per neural_policy_design §H.0
  (canonical): widen once, capabilities early-but-off, easy-physics-first, distill-single /
  RL-joint, curriculum-ramp.
- **§19 training pipeline:** the gym IS the plant (thin C ABI over sim_step, bit-for-bit);
  trainer is PyTorch-offline precompute (local RTX for N1/N2, H200 fleet for N3+) — the
  language reconciliation codified with the §2 tooling rule (project code + tools/ =
  C/C++/CUDA only; v1's tools/-Python lines superseded); domain randomization incl.
  target_cov/staleness so the policy LEARNS to use the uncertainty socket; DAgger; the
  terminal-dominated potential-shaped reward; freeze/export/NP_VERSION ceremony.
- **§11.13 multi-client + mesh doctrine (folding D-011):** clients are added, never
  migrated (WebGPU forever-fast-view; UE = IMAX on the same stream); the high-fidelity mesh
  is observer garnish UNTIL the §20 CFD event, when the SAME STL feeds FluidX3D to
  regenerate the aero tables — mesh + tables + vehicle-hash + re-golden as ONE ADR event
  (one geometry source via the precompute gate). CFD/UE never close a runtime loop.
- **§16/§17 additions:** sim-overfit, reward hacking, gate dilution, interface churn, VLM
  mis-grounding, the G0 double-hat, teacher cost, the stale-exe trap; anti-patterns incl.
  training-on-gate-seeds and unversioned artifact regeneration.

Pre-authorizations carried in this ADR: the protocol-v4 golden re-freeze at N0; the N0
byte-equality re-verification run; NP_VERSION regime for policy weights; the future
mesh+CFD single-event re-baseline (§20) when the operator schedules it.

---

## Adoption checklist (executes on operator signature, one commit)

1. Append the ADR block above to `DECISIONS.md` (append-only; verbatim).
2. Remove the DRAFT banner from `CLAUDE_v2.md`; physically copy v1's App-A tables into
   App-A (they are byte-authoritative; v2 changes no value).
3. `RUN_STATE.md` header: canon pointer `CLAUDE_v1.md` → `CLAUDE_v2.md` (+ one-line D-019
   note). `README.md`: repo-layout line + status pointer.
4. Session memory: update the project memory (canon = v2; N-track live).
5. Commit + push (single commit: CLAUDE_v2.md, DECISIONS.md, RUN_STATE.md, README.md,
   runs/D019_proposed_canon_v2.md retained as the authoring record).
6. First build session thereafter: N0 per §14 (worktree-first; §13.6 gates).

## Open decision points for the operator (defaults are what the draft encodes)

- **D1 — supersede vs amend:** draft = full v2 supersede (D-001 precedent). Alternative:
  bolt-on amendments to v1 (rejected: cold-start readability, accumulated staleness).
- **D2 — new directive 11:** draft = yes (one new directive, precompute-in/telemetry-out).
  Alternative: leave it a D-011 note (weaker than its actual load-bearing role).
- **D3 — N-track naming:** draft = N0–N4 with an explicit N↔P↔S mapping table (P/S labels in
  the pillar docs stay valid). Alternative: reuse P0–P6 as milestones directly.
- **D4 — socket width:** draft = 28-feature App-G socket incl. target_vxy/cov/age/valid +
  deck_z + eng_health (VLM-ready). Alternative: minimal (xy + health only) — rejected:
  re-architecture tax later, per §H.0.
- **D5 — beacon at N0 or N4:** draft = beacon optional at N0 (it is trivial and proves the
  acquisition→guidance path), REQUIRED by N4. Pull earlier if the perception story should
  demo sooner.
- **D6 — the M4 gate framing:** draft = M4 stays ≥90 landed-rate; N3 is its designated
  vehicle; frontier metric is overlay-only. (Anti-dilution — recommend keeping.)
