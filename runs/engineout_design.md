# ENGINE-OUT DESIGN — a seeded engine failure in the 3-engine entry burn, the thrust centroid shifts, the loop and guidance re-solve

**Author:** ENGINEOUT (Opus 4.8) · **Date:** 2026-07-18 (night, post-D-016 / continuity) · **Status:** design + complete static trace of the offset-substitution and recovery path, ready to implement
**Operator ask:** during the multi-engine burn, a seed/CLI-selected engine fails; the surviving-engine thrust centroid shifts OFF-AXIS; that induces a torque; the control loop nulls it by gimballing the survivors back to center; guidance re-times the now-reduced-authority burn (the Apollo-13 / F9-CRS-1 "burn longer and recover" behavior). The single most dramatic "watch it re-solve" beat in the play menu.
**Builds on:** §5.3 (the per-engine model, `n_eng ∈ {1,3}`, and the explicit "side-engine offset torque neglected — recorded assumption" this design RELAXES), §6.2 ("thrust applied at the gimbal pivot; the pivot-to-CoM offset is the entire source of pitch/yaw authority"), §10.6 (`INJECT_DISTURBANCE` — the closed anti-cheat command enum that already reserves *thrust deficit / CoM offset / RCS pod out*), §10.8 (the command journal — the determinism template for a time-triggered event), the D-007 E3 entry supervisor, the D-016 ENTRY-under-MPPI result, the existing `--inject` plant-disturbance channel.
**Canon it must respect:** directive 2 (determinism sacred — seeded, replayable, memcmp-oracle-clean), directive 3 (if guidance can't solve it, the vehicle crashes — no assist toward the pad), directive 5 (renderer is a pure observer), directive 7 (one dynamics source — the MPPI rollout must see the same failed-engine authority), directive 9 (TERMINAL byte-identical), §4.3 (the "what may guidance legally know" line — the engine-out analog is chamber-pressure health, which IS legal, unlike hidden wind).

> **DESIGN + STATIC ANALYSIS ONLY. Nothing in `core/` was edited; no cmake/binary was run against the real tree.** Every code injection below is specified by **file + function + line-neighborhood** so a build agent implements without re-deriving. The central claim — that an engine-out is an *effective lateral shift of the thrust application point*, and the existing `arm_thr × Fthr` EOM turns that shift into the induced torque with **no new torque machinery** — is established by a **complete static trace** of `dynamics.c` (§B, cited line-by-line). A read-only `_eo_wt/` centroid→arm→torque probe is specified in §F-S0 as the *byte-equality + torque-sanity* proof; it is not needed to establish the trace.

---

## 0. Executive verdict

**Engine-out is an AUTHORITY change that decomposes into two scalars the plant ALREADY consumes — a thrust deficit and a lateral thrust-application offset — plus one honest new question: may guidance know the engine died? The whole feature rides on existing math; the only genuinely new code is a seeded, time-triggered event that sets those two scalars mid-burn, and a guidance-legal engine-health flag.**

1. **The torque machinery already exists, and it is the SAME lever that makes the gimbal work.** `dynamics.c:128-138` applies thrust as ONE aggregate gimballed vector `Fthr` at the base and computes its moment about the CoM through `arm_thr = (−com_offset[0], −com_offset[1], −com)` (`dynamics.c:137`), `Tthr = arm_thr × Fthr` (`dynamics.c:138`). Canon §6.2 states it exactly: *"thrust applied at the gimbal pivot (base center); the pivot-to-CoM offset is the entire source of pitch/yaw authority."* An engine-out shifts the *effective application point* of the surviving cluster laterally toward the survivor centroid — i.e. it adds a lateral component to `arm_thr` — and the existing cross product produces the induced torque. **No new "off-center thrust" term. The build is a two-scalar substitution into an EOM that is already force×lever-arm.** (§B.)

2. **Both scalars are ALREADY plant inputs on the `INJECT_DISTURBANCE`/`--inject` channel.** `EnvCtx` carries `thrust_scale` (`dynamics.h:26`, multiplicative thrust deficit, consumed at `dynamics.c:106,111`) and `com_offset[2]` (`dynamics.h:28`, the lateral thrust-application offset, consumed at `dynamics.c:137`). `--inject` (`sim.c:65-76`) already seeds both per-run. **Engine-out reuses exactly these two fields**, but drives them from a *time-triggered, engine-geometry-derived* value instead of a per-run random dispersion. Canon §10.6's `INJECT_DISTURBANCE` enum already reserves "thrust deficit ±%, ... CoM offset, ... RCS pod out" — an effector-failure family; engine-out is the canonical member of it, not a new channel.

3. **Directive-7 is FREE for the physics.** The MPPI rollout copies the real `EnvCtx` verbatim — `EnvCtx env = *env0;` (`guidance_mppi.c:350`, and the warm-start at `:555`) — so a `com_offset`/`thrust_scale` set on the live env is seen inside every rollout's `dynamics_deriv` automatically. The plant and the rollouts share the EOM *including the disturbance*, exactly the way `--inject` already composes. (The one non-free directive-7 item is the *entry-burn n_eng reduction*, because the rollout models only the 1-engine landing burn — §C.3 handles it honestly.)

4. **The reactive catch needs NO engine-health signal; the "burn longer" make-up DOES.** `control.c:152-153` closes the attitude PD on body-rate `w` and attitude error `errb` from *state feedback alone* — an induced torque perturbs `w`/attitude and the loop gimbals to null it with zero knowledge that an engine failed (the "blind catch"). But the loop's gimbal *allocation* divides by the current thrust (`denom = mp.com*thr`, `control.c:188`, with `thr = act->n_eng*engine_thrust(...)`, `:132`), so if `n_eng` drops the same `tau_cmd` correctly demands a larger gimbal — the allocation self-adjusts **iff `n_eng` is updated**. And the *guidance* arrest profile (`a_max_now = Tfull/m − G0`, `hoverslam_step` `:115`; the E3 divert `a_burn = 3.0*engine_thrust(...)/m`, `sim.c:154`) is authority-sized — a mid-burn engine-out changes the profile, and to "burn longer" honestly guidance must know the reduced authority. **Chamber-pressure engine health is a legal NavState input** (a real vehicle reads chamber P; unlike wind it is a *measured on-board quantity*, not a hidden disturbance — the §4.3 line is satisfied). So I recommend guidance gets a per-engine health flag and derive BOTH a "blind" behavior (feedback-only; the loop catches the torque but guidance mis-sizes the burn) and an "informed" behavior (guidance re-sizes n_eng authority → the true re-solve).

5. **Where it is survivable, and where it is correctly NOT.** The 3-engine ENTRY burn has redundancy (lose 1 of 3 → 2 survive → torque + 33% thrust loss, recoverable). The 1-engine LANDING burn has none — losing it is pure thrust loss to zero, unrecoverable, and that is **correct, not a gap** (F9 de-risks exactly this way with the 3-engine phase; directive 3 says an unsolvable state crashes). The demo therefore lives in the entry burn. The honest failure mode when a survivable engine-out is *too* big (or lands in the wrong phase) is `F_LOC` — the induced torque outruns the gimbal, `wmag` exceeds 2 rad/s for >3 s, `sim.c:369` terminates. That LOC path is the truthful "it could not re-solve it" outcome, and it must be reachable (directive 3).

6. **Determinism: default OFF → byte-identical to today.** No `--engine-out` flag ⇒ `thrust_scale=1`, `com_offset=0`, health all-nominal ⇒ every read is a no-op the compiler folds identically ⇒ **TERMINAL 194/200, ENTRY `--mppi` 95/100, AERO 44/60 reproduce byte-exact** (the hard gate). The event itself is a *seeded closed form* (a fixed `(k, t_fail)` or a seeded pick from the existing RNG streams), no runtime RNG, fully replayable — a `{step, INJECT_DISTURBANCE}` journal record in the §10.8 sense.

**Recommended build order (each stage independently gated):** **S0** — add the per-engine geometry + surviving-centroid math with NO failure active (prove byte-equality: all-engines-firing centroid is on-axis, `arm_thr` unchanged). **S1** — the seeded `--engine-out k@t` injector: reduced thrust + induced torque + the legal health flag; measure ENTRY landing-rate with a mid-entry-burn engine-out at a few `(k,t)` under reactive AND `--mppi` (expect MPPI ≥ reactive). **S2** — the optional structural side-load gate (opt-in, off by default → goldens unperturbed). **Ship S0+S1 into the honest tree; keep S2 behind its own flag.**

---

## 1. The plant today — the engine model, and why "engine-out" is a centroid question

Read directly from `core/dynamics.c`, `core/control.c`, `core/state.h`, `core/sim.c`, `core/scenario.c`, and canon §5.3/§6.2.

### 1.1 The engine is a lumped, symmetric, single-gimbal cluster

`state.h:30` — `int n_eng; /* engines this burn (1 or 3) */` is a real hybrid state var. `dynamics.c:110-112`:
```c
double ramp   = st->engine_on ? ignition_ramp(st->ign_timer) : 0.0;
double thr_per= engine_thrust(y[S_THR], atm.p) * ramp * tscale;   // per-engine, with the --inject deficit
double thrust = st->n_eng * thr_per;                              // dynamics.c:112 — LINEAR in n_eng
```
Total thrust is `n_eng × per-engine`, applied along ONE gimbal-deflected axis `tdir = (sin g0, sin g1, ·)` (`dynamics.c:129-133`) at the base. Canon §5.3:292 records the modelling assumption exactly:

> *"3-engine burns: symmetric side pair assumed thrust-aligned through CoM (side-engine offset torque **neglected — recorded assumption**). Thrust multiplier ×3, same gimbal on all."*

So today the cluster has **no per-engine geometry**: all engines share one gimbal, and their thrust sums to a single centered vector. **This design relaxes precisely that recorded assumption** — the minimal upgrade is a fixed per-engine ring geometry, enough to compute the surviving-engine centroid when one drops.

### 1.2 The torque of a base-mounted thrust vector is ALREADY computed through a lever arm

`dynamics.c:128-138` — the whole mechanism, verbatim:
```c
double g0=y[S_G0], g1=y[S_G1];
double tdir[3]={ sin(g0), sin(g1), 0.0 };
double tz2 = 1.0 - tdir[0]*tdir[0] - tdir[1]*tdir[1];
tdir[2] = (tz2>0.0)? sqrt(tz2) : 0.0;
double Fthr[3]; v3_scale(Fthr,tdir,thrust);
v3_add(Fb,Fb,Fthr);
/* CoM laterally offset by the disturbance → the base-mounted thrust gains a moment arm. */
double arm_thr[3]={-env->com_offset[0],-env->com_offset[1],-com};   // dynamics.c:137  r_gimbal − r_com
double Tthr[3]; v3_cross(Tthr,arm_thr,Fthr); v3_add(Tb,Tb,Tthr);     // dynamics.c:138  induced torque
```
The comment at `dynamics.c:135-136` calls it "thrust misalignment": a lateral offset between the application point `r_gimbal = (−com_offset, 0)` and the CoM `(0,0,com)` gives `arm_thr` a lateral component, and `Tthr = arm_thr × Fthr` is a genuine moment about the CoM. Canon §6.2:366-368 states the general principle: *"Thrust applied at the gimbal pivot (base center). The pivot-to-CoM offset is the entire source of pitch/yaw authority."* **The same lever that lets the gimbal steer is the lever that turns an engine-out into a torque.** `com_offset` defaults to `(0,0)` (`sim.c:67`), read as nominal, so the term is zero unless perturbed.

**The physical identity that makes this exact (not an approximation):** a thrust `F` produced by the surviving cluster whose geometric centroid is at body-lateral position `c = (cx, cy)` (offset from the axis) applies its resultant at `(cx, cy, 0)`, giving a moment `(c − r_com) × F` about the CoM. That is *identically* what `dynamics.c:138` computes if we set the application-point lateral offset to `−c` (the sign convention of `arm_thr = r_gimbal − r_com` with `r_gimbal = −com_offset`). **So injecting the surviving-centroid lateral position as a `com_offset`-shaped term is not a model of the torque — it IS the torque, evaluated by the plant's own force×lever-arm code.** (The subtlety that `com_offset` also feeds the aero CoP is addressed in §B.2 — it does NOT; the aero arm is separate.)

### 1.3 The two `--inject` scalars are the engine-out's two physical effects

`dynamics.h:20-29` (the `EnvCtx` a physics step reads):
```c
double thrust_scale;     /* multiplicative thrust deficit (1.0 nominal; 0 → treated as 1.0) */   // dynamics.h:26
double isp_scale;        /* multiplicative Isp deficit    (1.0 nominal; 0 → treated as 1.0) */
double com_offset[2];    /* lateral CoM offset [m] → thrust-misalignment disturbance torque   */   // dynamics.h:28
```
`sim.c:65-76` seeds them under `MOD_INJECT`: up to −8% thrust, −1% Isp, 2 cm lateral offset, per `(seed, run_idx)`, nominal `(1,1,0)` otherwise. The engine-out's two effects map ONE-TO-ONE:
- **thrust loss** — the surviving cluster produces `(n_survivors / n_eng)` of the commanded total → a multiplicative deficit, exactly `thrust_scale`'s job (though engine-out is better expressed as an `n_eng` decrement; §B.1 chooses).
- **thrust-application offset** — the surviving centroid moves laterally → exactly `com_offset`'s job.

**The engine-out injector is therefore a `com_offset`/`thrust_scale` writer, driven by engine geometry + a failure event, rather than by a per-run random draw.** This is the minimal, honest, gate-clean shape — and it is the shape canon §10.6 already reserves.

### 1.4 The 3-engine entry burn vs the 1-engine landing burn — where redundancy lives

- **Entry burn** (`sim.c:198,208`): `s->gcmd.n_eng=3`, full throttle, latched at `sim.c:240` (`st->n_eng = s->gcmd.n_eng`). Divert authority is 3-engine: `a_burn = 3.0*engine_thrust(1.0,atm.p)/mp.m` (`sim.c:154`). **This burn has redundancy** — losing 1 of 3 leaves 2 firing. This is the survivable engine-out.
- **Landing burn** (`hoverslam_step:87`): `g->n_eng=1`. `Tfull = g->n_eng*engine_thrust(1.0,atm.p)` (`:93`); `a_max_now = Tfull/m − G0` (`:115`). **No redundancy** — losing the single engine is pure thrust loss to zero.
- **Relights** (`scenario.c:76`): `relights_left=2`, spent by the entry burn and the landing burn. There is no third igniter to relight a failed engine — an engine-out is a *permanent* loss for the remainder of that burn (a failed Merlin does not restart mid-burn; F9-CRS-1's engine stayed out). This is physically correct and requires no relight-budget change.

### 1.5 The failure sinks that make engine-out honest

- **`F_LOC`** — `sim.c:368-369`: `wmag = |ω| > 2.0 rad/s` sustained >3 s → `F_LOC`, `PH_LOC`, terminate. **This is the truthful "the torque outran the gimbal" outcome.** A survivable-but-mishandled or too-large engine-out ends here (directive 3).
- **`F_STRUCT`** — `sim.c:362`: `qbar > QBAR_MAX` (80 kPa, `constants.h:97`) sustained >2 s. The *only* structural sink today (qbar-based). §D adds an *opt-in* asymmetric-thrust / counter-gimbal side-load gate here.
- **Verdict** — `set_verdict` (`sim.c:86-105`): off-pad or `td_v > TD_V_HARD` → `V_CRASHED`. A burn that recovers attitude but exhausts its authority making up Δv lands off-pad or hot → the honest degraded grade.

---

## 2. The engine-model decision (Part A) — minimal realistic geometry

### 2.1 The layout: center engine + symmetric side pair (F9-faithful, canon-consistent)

**Recommendation: model the 3-engine cluster as ONE center engine on the axis + TWO side engines on a ring of radius `R_eng`, diametrically opposed.** This is:
- **F9-faithful.** The Falcon 9 octaweb lights the *center* engine for the single-engine landing burn and adds a symmetric pair (or the full set) for the higher-authority phases; the landing engine is the center one. Canon §5.3:292 names exactly this geometry ("symmetric side pair") and §11.4:758 describes the render model as "octaweb + 9 bells (center bell articulated by gimbal_act)" — the *center* bell is the gimballed lander.
- **The minimal geometry that answers the centroid question.** Three positions — `p0 = (0,0)` (center), `p1 = (+R_eng, 0)`, `p2 = (−R_eng, 0)` in the body lateral plane (azimuth is arbitrary; pick the body-X axis, or seed it — §E) — are enough to compute the surviving centroid for any single failure. No per-engine plumes, no per-engine Isp, no per-engine gimbal (canon: "same gimbal on all"). **Do NOT over-model.**
- **`R_eng` basis.** The octaweb outer ring sits near the vehicle radius; a defensible value is `R_eng ≈ 0.6·VEH_RADIUS ≈ 1.1 m` (`VEH_RADIUS = 1.83 m`, `constants.h:42`) — the outer-ring Merlins are roughly two-thirds of the way out. Tag it `[chosen, representative]` in the canon style; it is the single new geometry constant and the design's sensitivity to it is linear (§B.3). Put it in `constants.h` beside the engine block (`ENG_*`, ~`constants.h:25-28`) as `ENG_RING_R`.

### 2.2 The decisive consequence: center loss ≠ side loss

This is the physics the demo turns on, and it falls straight out of the geometry:

| failed engine | survivors | survivor centroid `c` | thrust factor | induced torque |
|---|---|---|---|---|
| **center** (`p0`) | side pair (`p1,p2`) | `(p1+p2)/2 = (0,0)` — **still on-axis** | 2/3 | **ZERO** (symmetric pair) — pure thrust loss |
| **one side** (`p1`) | center + other side (`p0,p2`) | `(p0+p2)/2 = (−R_eng/2, 0)` — **off-axis** | 2/3 | **NONZERO** — `F·R_eng/2` about CoM |
| other side (`p2`) | center + `p1` | `(+R_eng/2, 0)` | 2/3 | mirror of above |

- **Center-engine loss in the entry burn**: the surviving pair is symmetric → centroid stays on-axis → **no induced torque, only a 33% thrust deficit**. Recoverable by "burn longer" alone (no attitude upset). *But* note: in the LANDING burn the center IS the only engine — its loss there is total and unrecoverable (§G).
- **Side-engine loss in the entry burn**: centroid shifts to `−R_eng/2` (halfway between the two survivors) → **induced torque `τ = F_survivors × R_eng/2` about the CoM** → the dramatic attitude upset the demo wants, plus the 33% thrust loss.

**So the "money shot" engine-out is a SIDE engine failing during the 3-engine entry burn.** The center-out case is the honest "quieter" variant (thrust loss, no yaw) worth offering as a contrast. State both; default the demo to a side-out.

### 2.3 The surviving-centroid formula (closed form, the injector's core)

For a set `S` of surviving engine body-lateral positions `{p_i}` (each `p_i = (xi, yi)`):
```
c_survivor = (1/|S|) · Σ_{i∈S} p_i            // surviving-cluster geometric centroid, body lateral
n_survivors = |S|
thrust_factor = n_survivors / n_eng_nominal   // e.g. 2/3
```
With the center+pair layout and one side out, `c_survivor = (−R_eng/2, 0)`; center out, `c_survivor = (0,0)`. **This `c_survivor` is the lateral thrust-application offset the plant needs.** (Body frame; the plant's `com_offset` is body-lateral too — `dynamics.c:137` uses it before the `q_rot` to world at `:226`, so no frame conversion is needed. Verify in §B.2.)

---

## 3. The physics (Part B) — the exact substitution into `arm_thr`

**Claim:** injecting `c_survivor` as a `com_offset`-shaped thrust-application offset, plus the `2/3` thrust factor, reproduces the true reduced-authority + induced-torque dynamics through the *existing* EOM, with a handful of mechanical edits and no new torque term. Established by the static trace below.

### 3.1 The cleanest injection: a dedicated `thrust_offset[2]` on `EnvCtx`, added at `dynamics.c:137`

There are two candidate ways to feed `c_survivor` in. I recommend **(a)**, a NEW field, over **(b)**, reusing `com_offset`, for a clean-separation reason:

- **(a) RECOMMENDED — add `double thrust_offset[2]` to `EnvCtx`** (`dynamics.h`, beside `com_offset` at `:28`), and change `dynamics.c:137` to:
  ```c
  double arm_thr[3]={ -(env->com_offset[0]+env->thrust_offset[0]),
                      -(env->com_offset[1]+env->thrust_offset[1]),
                      -com };                                   // dynamics.c:137, engine-out extends the arm
  ```
  `thrust_offset` defaults to `(0,0)` → read as nominal → byte-identical when absent. **Why a separate field, not just `com_offset`:** `com_offset` is documented as a *CoM* dispersion (a mass-property offset). A survivor centroid is a *thrust-application-point* offset — physically the same lever *for the thrust torque* but semantically distinct, and (critically) a CoM offset would ALSO (correctly) move where gravity and inertia act, whereas an engine-out does NOT move the CoM. Today `com_offset` only feeds the thrust arm (`dynamics.c:137`) and nothing else (verified §B.2), so reusing it would *happen* to work — but keeping `thrust_offset` separate keeps the semantics honest and future-proofs against `com_offset` ever being wired to gravity/inertia. It also lets `--inject` (a small CoM wobble) and `--engine-out` (a big application-point shift) co-exist and *sum* naturally in the arm.
- **(b) reuse `com_offset`** — set `s->env.com_offset = c_survivor` during the failure. Fewer new fields, and it works TODAY because `com_offset` is thrust-arm-only. **Rejected as the primary** for the semantic reason above, but noted as the zero-new-field fallback if a build agent wants the absolute minimum diff. (If chosen, `--inject` + `--engine-out` together would need the injector to *add* to the existing `com_offset` rather than overwrite it.)

### 3.2 Static trace — where `com_offset`/`thrust_offset` flows, and where it does NOT

Grep + read confirm `com_offset` is consumed at **exactly one site**: `dynamics.c:137`. It does **not** touch:
- the aero CoP arm — that is `arm_cp = (0,0, xcp−com)` (`dynamics.c:173`), a pure axial lever from the aero force at the CoP; independent of `com_offset`.
- the mass properties — `mass_props` (`dynamics.c:56-86`) takes only propellant masses; `com` is the axial CoM height, no lateral term.
- gravity — `a[2] = Fw[2]/m − g_h` (`dynamics.c:228`); gravity acts at CoM, no lateral arm.
- the fin/RCS arms — `arm = (rm[0],rm[1],rm[2]−com)` (`dynamics.c:213`) and `act->rcs_torque` (`:223`); independent.

**Therefore adding `thrust_offset` into `arm_thr` at `dynamics.c:137` affects ONLY the thrust moment — precisely the engine-out's torque — and nothing else.** This is why the substitution is exact and side-effect-free. (Frame check: `arm_thr` and `Fthr` are both **body frame** at `:133-138`; the resultant `Tthr` is body torque, summed into `Tb` and used by the rotational EOM at `:230-236` before any world rotation. `com_offset`/`thrust_offset` are body-lateral by construction — no `q_rot` needed. ✓)

### 3.3 The thrust deficit: decrement `n_eng`, don't scale (the honest choice)

The 33% thrust loss has two equivalent encodings:
- **(preferred) decrement `n_eng`**: at failure, `st->n_eng = n_survivors` (e.g. 3→2). Then `thrust = st->n_eng * thr_per` (`dynamics.c:112`) is automatically `2/3` of nominal, `mdot` (`:116-118`) drops proportionally (correct — a dead engine burns no propellant), and — crucially — `control.c:132` and `:188` see the reduced `n_eng` so the gimbal allocation self-adjusts (§C.1). **This is the physically honest encoding**: two engines really are firing.
- **(rejected) `thrust_scale = 2/3`**: scales thrust but leaves `n_eng=3`, so `control.c` still allocates against 3-engine authority (over-gimbals) and `mdot` is right but for the wrong reason. Use `thrust_scale` only for a *partial* deficit (a degraded, not-dead engine) if ever wanted; for a clean engine-OUT, decrement `n_eng`.

**So the injector's two writes at the failure step are: `st->n_eng = n_survivors` (thrust + allocation + mdot) and `env.thrust_offset = c_survivor` (induced torque).** Both are closed-form from geometry; both default to no-op.

### 3.4 Magnitude sanity (the numbers a build agent should expect)

At a representative entry-burn point (`sim.c:154` gives `a_burn ≈ 50 m/s²` at 3-engine near-vacuum, `m ≈ 30-40 t`):
- Per-engine thrust `T ≈ 850-930 kN` (`constants.h:25`, `ENG_T_VAC = 932 kN` vac). Two survivors: `F_survivors ≈ 1.7-1.86 MN`.
- Side-out centroid offset `|c| = R_eng/2 ≈ 0.55 m` (with `R_eng ≈ 1.1 m`).
- Induced torque `τ = F_survivors × |c| ≈ 1.8e6 × 0.55 ≈ 1.0e6 N·m`.
- Transverse inertia `I_tr` (from `mass_props`, order `~2-4e6 kg·m²` for a ~40 t booster with `VEH_LEN=47.7 m`) → **angular accel `α = τ/I_tr ≈ 0.25-0.5 rad/s²`**. Left uncorrected for ~4-8 s that would breach the `wmag>2` LOC line — so the gimbal has a real fight, but the timescale (seconds) is well within the 15°/s gimbal rate (`constants.h:283`) and the ±5° range (`ENG_GIMBAL_MAX`, `constants.h:28`). This is the *recoverable-but-dramatic* regime the demo wants.
- **The counter-gimbal cost:** to null `τ`, the survivors gimbal to produce an equal-and-opposite base moment. The gimbal moment available is `com·thrust·sin(g)` (`control.c:187-190`); with `com ≈ 10-14 m`, `thrust ≈ 1.8 MN`, `sin(5°)=0.087`, max gimbal moment `≈ 10×1.8e6×0.087 ≈ 1.6e6 N·m` > the `1.0e6` induced — **so the 2-engine gimbal CAN hold a side-out**, with margin (~60% of gimbal authority spent on the trim), and the leftover steers/decelerates. The cos-loss: a 5° trim gimbal costs `1−cos5° ≈ 0.4%` of vertical thrust — negligible; the binding cost is the *gimbal-authority budget* (fraction of the ±5° cone consumed holding the trim), not the cos vertical loss. Quantify both in S1 telemetry.

**Sanity of the "burn longer" driver:** thrust drops 33% while gravity is unchanged, so net decel authority `a = n_eng·T/m − g` drops from `~50` to `~32 m/s²` (the −g bites harder at 2 engines). A given Δv now takes ~1.5× the burn time → more propellant, deeper into the reserve → the make-up the guidance must find. That is the physical "burn longer," and it is exactly what an authority-aware `a_design` (§C.2) re-solves.

---

## 4. The recovery (Part C) — both guidance modes

### 4.1 The reactive catch is state-feedback-only (no health signal needed) — trace

`control.c:127-153`:
```c
double zbody_w[3]; double zb[3]={0,0,1}; q_rot(zbody_w,q,zb);
double errw[3]; v3_cross(errw,zbody_w,zdes);   // attitude error, world  control.c:128
double errb[3]; q_rot_inv(errb,q,errw);        // → body                 control.c:129
...
tau_cmd[0] = Kp*errb[0] - Kd*w[0];             // control.c:152 — PD on error AND rate
tau_cmd[1] = Kp*errb[1] - Kd*w[1];
```
When the induced torque `Tthr` starts rotating the body, `w` grows and the attitude drifts off `zdes`; the PD immediately commands a counter `tau_cmd` **purely from the sensed `w` and attitude — it never needs to know an engine died.** The gimbal allocation then realizes it:
```c
double thr = act->n_eng * engine_thrust(...) * ramp;   // control.c:132
double denom = mp.com*thr;                              // control.c:188
double sg1 = tau_cmd[0]/denom;  double sg0 = -tau_cmd[1]/denom;   // :189-190 → gimbal angles
```
**The critical subtlety:** `denom` uses `act->n_eng`. If the injector decrements `st->n_eng` → `g->n_eng` → `act->n_eng` (via the latch and `control.c:43`), then `denom` correctly halves-ish and the SAME `tau_cmd` produces a LARGER gimbal deflection — the allocation self-corrects for the weaker cluster. **If `n_eng` were NOT updated in the control path, the allocation would under-gimbal** (it thinks it has 3-engine leverage) and the catch would be sluggish. So the reactive catch is *feedback-driven* (no health flag) BUT depends on `n_eng` reaching `control_step` — which the §B.3 "decrement `n_eng`" encoding guarantees, because the plant latch and the control read the same `n_eng`. **Verdict: the gimbal catches the torque from state feedback alone; the only thing it must "know" is the reduced `n_eng`, which is a *plant* fact (how many engines are firing), not privileged wind-like information.** Directive-3-clean: no pad-seeking, just attitude regulation.

**Roll note:** the induced torque from a side-out is a *pitch/yaw* moment (lateral arm × axial-ish thrust) — the gimbal handles pitch/yaw (`control.c:187`). "Gimbal cannot roll" (`control.c:195`) is not a problem here: the engine-out torque has negligible roll component (the thrust is near-axial; `arm_thr × Fthr` with `Fthr ≈ (·,·,thrust)` and `arm_thr` lateral gives a pure pitch/yaw couple). RCS still owns roll as today.

### 4.2 What guidance may legally know: engine health via chamber pressure (the §4.3 analog)

The target-sandbox design established the doctrinal test (its §2, §A.4): guidance may consume *measured on-board quantities* (deck pose is transmitted/surveyed — legal; wind is a hidden disturbance — illegal, §4.3). **Engine health is the legal kind.** A real engine has a chamber-pressure transducer; loss of chamber pressure IS how the flight computer detects an engine-out (F9's fault detection is chamber-pressure/turbopump based). So:

> **Guidance is ALLOWED to know which engines are firing (a chamber-pressure health flag), because that is a sensed on-board quantity, not privileged knowledge of a hidden disturbance.** This is the §4.3-legal analog of the deck pose, and it is *more* clearly legal than the deck pose (it is the vehicle's own subsystem, not an external transmission).

**Recommendation: guidance gets a per-engine health flag (equivalently, the current `n_eng`).** Then derive both behaviors:
- **Blind behavior (health withheld — a diagnostic/ablation, not the ship default):** guidance keeps commanding as if 3 engines; the *control loop* still catches the torque (§C.1, feedback-only) IF `n_eng` reaches control, but guidance's arrest profile `a_design` is sized for 3-engine authority (`hoverslam_step:115`, `sim.c:154`) → it under-decelerates → arrives hot / off-profile. This is the honest "it caught the attitude but mis-timed the burn" partial. Useful to *show* why the informed version matters.
- **Informed behavior (health known — the ship default):** guidance re-sizes its authority to `n_survivors` engines → `a_design`/`a_burn`/`ignite_h` reflect the weaker cluster → it "burns longer," re-times, and re-solves. §C.2 wires it.

### 4.3 The "burn longer" Δv make-up — where n_eng must feed guidance

The authority-sized guidance terms that a mid-burn engine-out changes:
- **Entry divert authority** — `sim.c:154`: `double a_burn = 3.0*engine_thrust(1.0, atm.p)/mp.m;` **hardcodes `3.0`.** With a side-out this should be `st->n_eng` (=2). `amax = a_burn*sin(15°)` (`:155`) bounds the ZEM/ZEV divert (`entry_divert_step:159-162`) — a 2-engine burn has ~2/3 the divert authority, so the far-offset seeds that were marginal at 3 engines may now be un-divertable (honest — directive 3). **Injection: replace the literal `3.0` at `sim.c:154` with `(double)st->n_eng`.** (Guidance-legal: `n_eng` is the sensed firing count.)
- **Entry cut timing** — the E3 cut binds on predicted peak qbar and the fuel floor (`sim.c:183,206`). With less thrust the burn decelerates less per second, so `entry_predict_peak_qbar` (a *ballistic* no-thrust shoot, `guidance_hoverslam.c:20-35`) is unchanged, but the vehicle stays faster/higher longer → it may need to burn nearer the fuel floor. The cut logic already handles this (it cuts on qbar/fuel, not on a fixed time) — **no change needed**, but the burn naturally "lasts longer" in wall-time, which IS the demo beat.
- **Landing-burn arrest profile** — `hoverslam_step:93,115`: `Tfull = g->n_eng*engine_thrust(1.0,atm.p)`; `a_max_now = Tfull/m − G0`. `g->n_eng` is set to 1 here (`:87`), so if the engine-out is a *side engine during the entry burn*, by the time the landing burn starts (1 engine, center) the survivor count is irrelevant — the landing burn always uses the center engine, and if THAT is the failed one the landing is impossible (§G). So `hoverslam_step` needs no engine-out change for the entry-burn demo; it only matters if we ever model a partial/degraded landing engine (out of scope).
- **The ada freeze** — `sim.c:248-250`: `Tf = st->n_eng*engine_thrust(1.0,atm.p); ... st->ada = frac*(Tf/mp.m − G0)`. This ALREADY uses `st->n_eng`, so the frozen design decel correctly reflects the survivor count at the *landing-burn* ignition. ✓ (No change; it is already n_eng-aware.)

**Summary of guidance edits for the informed "burn longer": exactly one literal — the `3.0` → `st->n_eng` at `sim.c:154`.** Everything else is already `n_eng`-sourced or unaffected. This is the design's cleanest surprise: the plant was built `n_eng`-parametric (directive 7 foresight), so making guidance authority-aware of an engine-out is nearly free.

### 4.4 MPPI — re-solves the reduced-authority burn natively (the strong story), with one honest caveat

**The physics is directive-7-free for the rollout:** `rollout_cost` and `warm_start_nominal` copy the live env — `EnvCtx env = *env0;` (`guidance_mppi.c:350`, `:555`) — so the `thrust_offset` (induced torque) is seen by every rollout's `dynamics_deriv` at `guidance_mppi.c:399` automatically. When the engine fails, MPPI replans on the *disturbed* nav state (`mppi_step` every `MPPI_REPLAN_DECIM` ticks, `sim.c:265`) and its rollouts fly the *same* off-axis-thrust dynamics the plant flies → it re-solves the recovery natively, exactly as D-016 showed it re-solves wind-disturbed descents. **This is the "watch it re-plan" money story: the ghost line writhes as the torque hits, the rollouts that gimbal-trim-and-decelerate win, and pred_impact re-converges.**

**The one honest caveat (state it plainly in the ADR):** MPPI's rollout thrust model is the **1-engine landing burn** — `cmd_from_u_lean` sets `g->n_eng=1` and `Tfull=engine_thrust(1.0,...)` (`guidance_mppi.c:271,275`). The rollout does NOT model the 3-engine entry burn (that is the E3 supervisor's job, *outside* the rollout — the rollout starts from the post-CUT `nav` state via `mppi_step`, `sim.c:262-273`). So:
- If the engine-out happens **during the ENTRY burn**, the *plant* flies 2-engine dynamics (via `n_eng` + `thrust_offset`), the *E3 supervisor* (not MPPI) commands the entry divert, and MPPI only takes over at the CUT for the aero-descent + landing burn. The MPPI rollouts inherit the disturbed *state* (position/velocity the 2-engine burn produced) but model the 1-engine landing correctly. **This is fine and honest** — MPPI re-solves the landing from wherever the degraded entry burn left the vehicle. The entry-burn recovery itself is the E3 + control-loop's job (§C.1, §C.3), not MPPI's.
- If we ever wanted MPPI to *also* model a 3-engine entry burn with an engine-out inside the rollout, `cmd_from_u_lean` would need an `n_eng` input and the entry-burn thrust model — a larger change, out of scope for this demo. **Recommendation: do NOT extend the rollout to the entry burn; the disturbed-state handoff is sufficient and keeps the rollout lean.** The `thrust_offset` in env is still seen by the rollout, so if any residual off-axis effect persists past the CUT (it shouldn't — the entry engines are shut down and the landing engine is the center, on-axis), the rollout would model it correctly.

**Directive-7 invariance check (mandatory, §F):** with `--engine-out` absent, the MPPI single-run invariance line (`--run --scenario aero_offset --seed 42 --run 1 --mppi`, the `td_v 2.63 / lat 10.48` reference from continuity §0) must be **byte-identical** — proving the new `thrust_offset` field + the `3.0→n_eng` edit leaked nothing when inactive.

---

## 5. Structural (Part D) — the asymmetric-thrust / counter-gimbal side-load gate (OPT-IN)

### 5.1 Today's only structural sink

`sim.c:361-363`: `qbar > QBAR_MAX (80 kPa) sustained > 2 s → F_STRUCT`. Pure dynamic-pressure. There is no thrust-side-load or moment-based structural check.

### 5.2 The added path (opt-in, off by default)

A sustained asymmetric-thrust moment, and the counter-gimbal side-load it demands, load the thrust structure and the interstage laterally. An honest optional gate:

- **The load metric.** The lateral base shear from the counter-gimbal is `F_lat = thrust·sin(g_trim)` where `g_trim` is the steady gimbal deflection holding the engine-out trim (`control.c:194` `g0/g1`). Equivalently, the un-nulled asymmetric moment `τ_resid = |Tthr + gimbal_moment|` (the net the structure carries during the transient). Either is a proxy for the side-load the airframe feels.
- **The gate.** Add, adjacent to the qbar check (`sim.c:361-363`), guarded by an opt-in flag:
  ```c
  if(g_engine_out_struct_gate){                                  // opt-in only
      double glat = s->diag.thrust * fabs(sin(gimbal_trim_est)); // lateral thrust side-load [N]
      if(glat > ENG_SIDELOAD_MAX){ s->sideload_over_timer += DT;
          if(s->sideload_over_timer > 1.5){ st->fault=F_STRUCT; st->phase=PH_STRUCT_FAIL; s->done=1; return 0; } }
      else s->sideload_over_timer = 0;
  }
  ```
  (`gimbal_trim_est` from the actual `y[S_G0]/y[S_G1]` gimbal state; `s->sideload_over_timer` a new sibling of `qbar_over_timer` in `Sim`, `sim.h:24`, zeroed by the `memset` in `sim_init`.)
- **A plausible limit basis.** The thrust structure carries `n_eng·T` axially (~2.8 MN at 3 engines); a lateral side-load limit of order **10-15% of single-engine thrust** (`ENG_SIDELOAD_MAX ≈ 0.12·ENG_T_VAC ≈ 110 kN`) is a defensible "the thrust puck / gimbal actuators are rated for the normal ±5° gimbal side-load but not a sustained hard-over" number — tag `[chosen, representative]`. A 5° trim at 1.8 MN is `sin5°×1.8e6 ≈ 157 kN`, so this limit would flag a *sustained* near-max-gimbal engine-out trim (the marginal cases) but pass a modest one — appropriately selective.

### 5.3 Why opt-in

**Default OFF so it cannot perturb existing goldens** (directive 9). With the flag off, `sim.c` is byte-identical (the new block is compiled/branched out). It arms only under `--engine-out ... --struct-gate` (or a module bit). When on, it makes the "too big an engine-out tears the vehicle apart" outcome available as a *second* honest failure sink beside LOC — a richer demo (some engine-outs tumble → LOC; some hold attitude but exceed side-load → STRUCT). Record it as its own ADR sub-item; freeze a *separate* golden for the gated runs, never re-baseline the ungated goldens.

---

## 6. Determinism + injector (Part E) — the seeded, gate-clean plant event

### 6.1 The CLI: `--engine-out <k>@<t>` (deterministic) or `--engine-out random` (seeded)

Mirror the `--inject`/`--mppi` parse pattern (`main.c:204,246` for `--inject`; `main.c:206,248` for `--mppi`; mode dispatch `main.c:766-775`). Add to `cmd_run` and `cmd_headless` (and optionally `cmd_serve`):
```c
else if(!strcmp(argv[i],"--engine-out") && i+1<argc){
    parse_engine_out(argv[++i], &eo_engine, &eo_time);   // "k@t"  e.g. "1@8.5"; or "random"
    modules |= MOD_ENGINE_OUT;                            // new module bit (state.h:65 enum)
}
```
- **`k`** = which engine (0=center, 1/2=sides; or a name). **`@t`** = sim-time of failure [s]. **`random`** = seed a `(k, t)` from an existing RNG stream (see §E.3).
- Add `MOD_ENGINE_OUT` to the module enum (`state.h:65`, next free bit = 64) and a small `Sim`-level struct `{int eo_engine; double eo_time; int eo_fired;}` (or reuse a compact encoding), zeroed by `sim_init`'s `memset` (`sim.c:60`).

### 6.2 The event: a seeded closed-form plant write at the failure step (§10.8-style)

In `sim_step`, before guidance/control (beside the `--inject` seeding, but *time-triggered* not init-time):
```c
if((s->modules & MOD_ENGINE_OUT) && !s->eo_fired && st->t >= s->eo_time
    && st->engine_on && st->n_eng > 1){          // only a MULTI-engine burn can survive/absorb it
    s->eo_fired = 1;
    int n_surv = st->n_eng - 1;
    double c[2]; survivor_centroid(st->n_eng, s->eo_engine, c);  // §A.3 closed form (body lateral)
    st->n_eng = n_surv;                          // thrust + mdot + allocation drop (§B.3)
    s->env.thrust_offset[0] = c[0]; s->env.thrust_offset[1] = c[1];  // induced torque (§B.1)
    s->eng_health[s->eo_engine] = 0;             // the legal chamber-P flag guidance may read (§C.2)
    /* §10.8 journal: this is a {step, INJECT_DISTURBANCE(engine_out,k)} record, replayable */
}
```
- **No runtime RNG** for the deterministic `k@t` form → trivially replayable. The `random` form draws once from a seeded stream (§E.3) → also replayable. Either way it is a *closed-form* event: given `(seed, run, k, t)` the run is bit-identical.
- **Gate on `n_eng > 1`**: a failure commanded during the 1-engine landing burn is ignored by this survivability path (there is nothing to survive) — OR, for the honest unrecoverable demo, it sets `n_eng=0`/`thrust_scale=0` (total thrust loss) and lets the vehicle crash (directive 3). Recommend: **allow it, set thrust to zero, let it crash** — the "you cannot survive a landing-engine-out" beat is itself instructive. Make it a sub-mode (`--engine-out 0@t` during the landing burn → total loss).
- **`eng_health[3]`** — a new `Sim` array (the sensed chamber-pressure flags), `1`=firing, `0`=failed. This is what guidance reads (§C.2); it is the §4.3-legal signal. In NAV_TRUTH it is exact; under `--nav-noisy` a future refinement could add a (tiny) chance of a mis-detected engine-out, but default it is a clean measured flag.

### 6.3 The `random` seeding (replayable)

Reuse the `--inject`-style keyed draw (`sim.c:69-71` shows the pattern `rng_u01((uint32_t)(seed+run_idx*2654435761u+SALT))`):
```c
if(eo_random){
    double u1=rng_u01((uint32_t)(seed+run_idx*2654435761u+404u));   // which engine
    double u2=rng_u01((uint32_t)(seed+run_idx*2654435761u+505u));   // failure time within the entry burn
    s->eo_engine = (u1<0.5)? 1 : 2;              // a SIDE engine (the dramatic case); center = separate opt
    s->eo_time   = eo_burn_t0 + u2*eo_burn_window;  // seeded within the entry-burn window
}
```
Fixed salts (404/505) keep it out of the `RNG_WIND`/`RNG_DISPERSION` streams' way (they use different salts, `sim.c:69-71` uses 101/202/303; `scenario.c` fields 1-11). **No new RNG stream needed** — the existing `rng_u01` keyed by `(seed,run)` suffices, and it composes with `--inject` (different salts → independent).

### 6.4 Directive-7 for the injector

The `thrust_offset` lives in `EnvCtx`, which the MPPI rollout copies (`guidance_mppi.c:350,555`) → the rollout models the same off-axis thrust (§C.4). The `n_eng` decrement is on `st` (the plant); the rollout latches its own `n_eng` from `g.n_eng` (`guidance_mppi.c:390`) and models the landing burn — correct per §C.4 (the entry-burn engine-out is pre-CUT, the rollout is post-CUT). **The invariance gate (flag absent → byte-identical) is the proof this is clean.**

### 6.5 The hard determinism gate (default OFF ⇒ byte-identical)

With no `--engine-out`: `MOD_ENGINE_OUT` unset → the `sim_step` block never fires → `thrust_offset` stays `(0,0)` → `dynamics.c:137` reads `com_offset+0` (identical to today) → `n_eng` untouched → `sim.c:154`'s `st->n_eng` equals the `3.0` it replaced during the entry burn (so even the `3.0→n_eng` edit is a no-op when no engine is out, *because the entry burn sets n_eng=3*). **Reproduce EXACTLY:** TERMINAL 194/200; ENTRY `--mppi` 95/100; AERO 44/60; the MPPI single-run invariance line. Any drift = the substitution is not algebraically neutral → fix before proceeding.

> **One caution on the `sim.c:154` edit:** replacing `3.0` with `(double)st->n_eng` is byte-identical ONLY if `st->n_eng==3` throughout the entry burn in the no-engine-out case. It is (`sim.c:208,240` set it to 3 and nothing decrements it without the injector). But a build agent MUST verify with the determinism pair — a literal-to-variable swap is exactly the kind of "obviously equal" change that can surprise if `n_eng` is ever transiently different. If any ENTRY number moves, gate the edit behind `MOD_ENGINE_OUT` (use `3.0` when the module is off) to guarantee equality.

---

## 7. Staged build plan (Part F) — each stage independently gated

**All in a `_eo_wt\` worktree copy (CMakeLists + core; VS2022 x64 configure), gitignored. Never edit/build the real tree until a stage's gate is green.** Gates after EVERY build (HANDOFF §1.7): `--selftest` = PASS; `--headless --scenario terminal --seed 42 --runs 200` = **exactly 194/200**; a determinism pair on the changed scenario (same `--run` twice, RESULT lines byte-match); and — since this touches the plant EOM (`arm_thr`) and (via `sim.c:154`) a guidance authority term — the **MPPI single-run invariance check** (`--run --scenario aero_offset --seed 42 --run 1 --mppi` vs the `td_v 2.63 / lat 10.48` reference, continuity §0). If TERMINAL moves, the change leaked past its gate — stop, fix.

### S0 — Per-engine geometry + surviving-centroid math, NO failure active (byte-equality proof)

**Build:** add `ENG_RING_R` (`constants.h`), the 3-position layout + `survivor_centroid()` helper (a pure function; §A.3), and the `thrust_offset[2]` field on `EnvCtx` (`dynamics.h:28`) wired into `arm_thr` (`dynamics.c:137`). **Do NOT fire any failure** — `thrust_offset` stays `(0,0)`, `n_eng` untouched. Add the `--engine-out` parse but leave the event un-triggered (or `MOD_ENGINE_OUT` off).

- **The byte-equality proof:** with `thrust_offset=(0,0)` and no `n_eng` change, EVERY baseline reproduces EXACTLY (subtracting zero is a compiler-folded no-op):
  - TERMINAL `--headless --seed 42 --runs 200` = **194/200 byte-exact**.
  - ENTRY `--headless --seed 42 --runs 100 --mppi` = **95/100** (D-016).
  - AERO `--headless --seed 42 --runs 60 --mppi` = **44/60**.
  - MPPI single-run invariance = the `(1.5,3)` reference line, byte-identical.
  - Determinism pair on each.
- **The centroid-math sanity (optional, `_eo_wt/eo_probe.c`):** a read-only unit that, for `n_eng=3` all-firing, computes `survivor_centroid` over the *full* set = `(0,0)` (on-axis, `arm_thr` unchanged — the S0 invariant), and for each single-out prints `c` and the resulting `τ = F×|c|` at a representative thrust — confirming the §B.4 magnitudes (center-out τ≈0, side-out τ≈1e6 N·m). This is the "the claim is real" artifact; the real tree is never touched.
- **Decision rule:** proceed to S1 iff all baselines reproduce byte-exact AND the centroid probe shows center-out τ≈0 / side-out τ>0. Hard equalities; no tuning.

### S1 — The seeded `--engine-out k@t` injector: reduced thrust + induced torque + legal health flag

**Build:** the §E injector (time-triggered event: `n_eng` decrement + `thrust_offset = c_survivor` + `eng_health`), the `sim.c:154` `3.0→st->n_eng` edit (gated behind `MOD_ENGINE_OUT` if the pair shows any drift), and guidance reading `eng_health`/`n_eng` for the informed behavior (§C.2-C.3). Add the `BL_EVT_*` engine-out event (mirror `BL_EVT_IGNITION_CMD` which already emits `n_eng`, `main.c:493`) for the renderer.

- **MPPI invariance FIRST** (before any batch): `--run --scenario aero_offset --seed 42 --run 1 --mppi` vs the reference line — **byte-identical** (engine-out inactive on AERO tier-0; proves the `thrust_offset` field + `sim.c` edit leaked nothing). If it moved, fix.
- **The recovery-rate demo (the core measurement):** ENTRY under a **mid-entry-burn side-engine-out** at a few `(k, t)` — e.g. `--engine-out 1@t` for `t ∈ {entry-burn early, mid, late}` — batched `--headless --scenario entry --seed 42 --runs 100`, under **reactive** (default GM_HOVERSLAM) AND **`--mppi`**:
  - Report: landed-rate vs `t_fail`, the LOC-fail count (the un-recoverable tail), td_v/td_lat of the landers, and the gimbal-authority fraction spent on the trim (a new telemetry/CSV column).
  - **Expected shape:** early/mid engine-outs recoverable (the E3 divert + control catch, then the 1-engine landing proceeds nominally since the failed *side* engine is irrelevant to the center-engine landing burn); late engine-outs (near the fuel floor) may LOC or land off-pad (no authority left to both trim and divert). **MPPI ≥ reactive** on the landers that reach the landing burn (D-016's replan advantage), though the *entry-burn* recovery is E3+control (§C.4) so the reactive/MPPI gap is smaller than for pure landing-burn scenarios — state this honestly.
  - **Center-out contrast:** `--engine-out 0@t` during the entry burn (thrust loss, no torque) — should land at a HIGHER rate than side-out (no attitude fight, just "burn longer") — the honest quieter variant.
- **Decision rule:** S1 "works" iff (a) the injector is byte-clean when absent, (b) a mid-burn side-out is *survivable at a meaningful rate* under both modes (demonstrating recovery), AND (c) a large/late engine-out honestly reaches LOC/off-pad (directive 3 — it must be able to fail). Record all numbers, including the LOC tail, in an ADR.

### S2 — Optional structural side-load gate (opt-in, goldens unperturbed)

**Build:** the §D gate behind `--struct-gate` (or a module bit), `sideload_over_timer` in `Sim`, `ENG_SIDELOAD_MAX` in `constants.h`.
- **Gate:** with `--struct-gate` OFF, ALL S1 numbers + the baselines reproduce byte-exact (the block is branched out). With it ON, re-run the S1 engine-out batches and report how many recoverable-by-LOC-standards runs now STRUCT-fail instead (the marginal near-max-gimbal trims).
- **Decision rule:** ship S2 as a labeled option iff it is byte-transparent when off and produces a *sensible* selective failure when on (flags sustained hard-over trims, passes modest ones). Freeze a *separate* `*_engineout_structgate_baseline.txt`; never touch the ungated goldens.

### Batch sizes / cadence
- Singles (~9 s MPPI / instant hoverslam) for wiring + the invariance check + first look.
- 100-run ENTRY batches (~1-2 min) per `(k, t, mode)` cell for the recovery-rate grid → background, one CSV row per cell, analyze/doc while waiting (NEVER block on a monitor — the idle-wait trap).
- Cross-seed s7/s99 on any headline rate before believing it (D-009/D-012 noise-scale discipline).

---

## 8. The demo / money shot + risks (Part G)

### 8.1 The money shot (a side engine fails during the 3-engine entry burn)

The visible beat, frame by frame:
1. **Nominal 3-engine entry burn** — three bells lit (the render model's octaweb, §11.4:758), plume, decelerating retrograde.
2. **`--engine-out 1@t` fires** — one side bell goes dark. The surviving centroid jumps to `−R_eng/2`; `Tthr` (`dynamics.c:138`) kicks the stack into a **yaw/pitch** (the stack visibly cants over — `wmag` spikes).
3. **The gimbals snap to catch it** — the two survivors gimbal hard (`control.c:189-194`, up to ~60% of the ±5° cone) to null the torque from pure rate feedback (§C.1). The attitude arrests and re-centers on `zdes`. `pred_impact` (protocol offset 220, streaming since D-013) **lurches off** as the disturbed state propagates, then **re-settles** as the loop recovers and (informed) guidance re-aims.
4. **The burn stretches** — with 33% less thrust, `a_burn` drops (`sim.c:154`, now `n_eng=2`), the ZEM/ZEV divert has less authority, and the burn runs closer to the fuel floor / lasts longer in wall-time before the E3 CUT. Under `--mppi` the ghost/plan line (§11.11) writhes on the disturbance and the vehicle chases the re-solved plan.
5. **Handoff + land** — at CUT the (2-engine, now shut down) entry burn hands to the aero-descent; the **1-engine center landing burn proceeds nominally** (the failed *side* engine never mattered to the center-engine landing) — the vehicle lands, having "burned longer and recovered." The HUD's `dist_pad` (offset 216) counts down to the honest miss.

**The whole thesis in one gesture:** a human kills an engine mid-burn, the stack lurches, the survivors catch it, and the guidance re-solves the now-harder problem in real time — no scripted recovery, no assist toward the pad.

### 8.2 Risks and honesty

- **The 1-engine landing burn genuinely cannot recover an engine-out — and that is CORRECT, not a gap.** Losing the single center landing engine is total thrust loss; directive 3 says the vehicle crashes. F9 de-risks exactly this way (the 3-engine phase provides the redundancy; the final single-engine burn is short and committed). State this in the ADR as a *design invariant*, not a TODO. The demo's engine-out therefore lives in the entry burn by physical necessity.
- **How big an engine-out is survivable — tie it to the authority margins.** The gimbal can hold a side-out iff the counter-gimbal moment (`~1.6e6 N·m` at 2 engines, §B.4) exceeds the induced `τ (~1.0e6)` — it does, with ~60% margin, so a *nominal* side-out is recoverable. It becomes UN-survivable when: (a) it lands late in the burn near the fuel floor (no authority left to both trim and divert — the far-offset seeds), or (b) `R_eng` is larger than assumed (bigger arm → bigger torque; the design is linear in `R_eng`, §B.3 — a build agent can sweep it), or (c) combined with a large `--inject` CoM offset that adds to the arm. Beyond the survivable envelope the honest outcome is `F_LOC` (`sim.c:369`) — reachable and correct. The `D_phys` divert ceiling (the aero-divert reach, `runs/sandbox/ceiling.c`, ~1107 m AERO / wider ENTRY) is the *lateral* ceiling; the engine-out adds a *temporal/authority* ceiling on top (less thrust → less divert → some far seeds that landed at 3 engines won't at 2). Both are physical, both honest.
- **Interaction with the wind-trim integral (C14) and the D-012 brake.** The C14 wind integral (`sim.c:300-323`) is `PH_LANDING_BURN` + fins gated — the *entry-burn* engine-out is over (CUT) before the landing burn, so the integral does not interact with the entry-burn recovery. The D-012 overspeed brake (`guidance_hoverslam.c:163-171`) is a *lateral* divert-gain schedule in the fins-deployed burn — also post-CUT — so it too does not fight the entry-burn engine-out trim. **The one interaction to WATCH:** if a build agent ever models an engine-out *during the landing burn* (not recommended — it is unrecoverable), the induced torque would collide with the C14 integral and the brake; for the entry-burn demo (the recommended scope) they are temporally separated. Note it, don't build it.
- **Determinism if random-seeded.** The `random` form draws `(k,t)` once from `rng_u01((seed,run,salt))` (§E.3) — fully replayable, a `{step, INJECT_DISTURBANCE}` journal record (§10.8). No runtime RNG in the plant path. **The gate is the same as `--inject`'s:** disturbed runs replay bit-exact; default-off runs are byte-identical to today.
- **The MPPI rollout does not model the entry burn (the §C.4 caveat).** Stated plainly so nobody over-claims: MPPI's *native* re-solve of the engine-out is the *landing-burn-and-after* re-solve from the disturbed state; the *entry-burn* attitude recovery is the E3 supervisor + the control loop (§C.1). This is honest and sufficient — the entry burn's job is deceleration + coarse divert, which E3 owns; MPPI's job is the terminal solve, which it does natively on whatever state the degraded entry burn produced.

---

## 9. Implementation checklist (for the build agent, no re-derivation needed)

1. **`constants.h`:** add `ENG_RING_R` (~`0.6·VEH_RADIUS ≈ 1.1 m`, `[chosen, representative]`), and (S2) `ENG_SIDELOAD_MAX` (~`0.12·ENG_T_VAC ≈ 110 kN`).
2. **`dynamics.h` (`EnvCtx`, `:28`):** add `double thrust_offset[2];` beside `com_offset`. Defaults `(0,0)` → nominal.
3. **`dynamics.c:137`:** `arm_thr = (−(com_offset[0]+thrust_offset[0]), −(com_offset[1]+thrust_offset[1]), −com)`. **The only EOM edit** — affects thrust torque only (§B.2).
4. **A `survivor_centroid(n_eng, failed_k, out c[2])` helper** (pure, §A.3): center+pair layout `{(0,0),(+R,0),(−R,0)}`, returns the mean of the survivors. Put it in `dynamics.c`/`sim.c` (host; no rollout-hot-path concern — it fires once).
5. **`state.h:65`:** add `MOD_ENGINE_OUT=64` to the module enum. **`sim.h` (`Sim`):** add `{int eo_engine; double eo_time; int eo_fired; int eng_health[3];}` and (S2) `double sideload_over_timer;` — zeroed by `sim_init`'s `memset` (`sim.c:60`).
6. **`main.c` (`cmd_run` ~`:204`, `cmd_headless` ~`:246`):** parse `--engine-out k@t | random` → `MOD_ENGINE_OUT` + `s->eo_engine/eo_time` (or seeded, §E.3). Update the usage string (`main.c:775`).
7. **`sim.c` (`sim_step`, before guidance):** the §E.2 time-triggered event (`n_eng` decrement + `thrust_offset = c_survivor` + `eng_health[k]=0`), gated on `n_eng>1` (or the landing-burn total-loss sub-mode). Emit a `BL_EVT` engine-out (mirror `main.c:493`).
8. **`sim.c:154`:** `a_burn = st->n_eng*engine_thrust(1.0,atm.p)/mp.m` (was literal `3.0`). **Gate behind `MOD_ENGINE_OUT` (use `3.0` when off) if the determinism pair shows ANY ENTRY drift** (§E.5 caution).
9. **Guidance health read (informed behavior, §C.2-C.3):** guidance/E3 reads `st->n_eng` (already the survivor count post-event) for authority — this is the §4.3-legal chamber-pressure flag. No hidden-state read.
10. **(S2) `sim.c:361`-adjacent:** the opt-in side-load STRUCT gate (§D), behind `--struct-gate`. Separate golden.
11. **Gates:** `--selftest`, TERMINAL 194, determinism pair, **MPPI single-run invariance** (the leak check after touching `arm_thr`/`sim.c:154` — non-negotiable, HANDOFF §1.4). Directive-7: confirm the rollout sees `thrust_offset` via `EnvCtx env=*env0` (`guidance_mppi.c:350,555`).
12. **Validation:** S0 (byte-equality + centroid probe) → S1 (the `(k,t,mode)` recovery-rate grid, reactive AND `--mppi`, cross-seeded) → S2 (opt-in struct gate). Decision rule per stage above.
13. **ADR:** append a `DECISIONS.md` entry — the geometry decision (center+pair, `R_eng` basis), the `arm_thr` reuse, the two-scalar injection, the recovery-rate table (WITH the LOC tail and off-pad honesty), the "1-engine landing is correctly unrecoverable" invariant, the MPPI-rollout-is-post-CUT caveat, and the determinism gate. Freeze `*_engineout_*_baseline.txt` if it ships.

---

## Appendix A — canon compliance restated

- **§6.2 (thrust at the pivot; pivot-to-CoM offset is the entire source of pitch/yaw authority):** SATISFIED and REUSED — the engine-out torque IS a pivot-offset moment, computed by the same `arm_thr × Fthr` at `dynamics.c:138` that produces gimbal authority. No bare torque injected (canon §6.2 forbids that; we go through the lever arm).
- **§5.3:292 (side-engine offset torque "neglected — recorded assumption"):** this design RELAXES exactly that recorded assumption, with a minimal per-engine geometry, as the canon anticipated ("recorded" = a knowingly-deferred simplification).
- **§10.6 (`INJECT_DISTURBANCE` closed enum — the anti-cheat contract):** SATISFIED — engine-out is a member of the reserved effector-failure family ("thrust deficit, ... CoM offset, ... RCS pod out"); it writes only *plant inputs* (`n_eng`, `thrust_offset`), never renderer/derived state; the enumeration stays closed.
- **§10.8 (command journal — reproducible interactive runs):** SATISFIED — the event is a `{step, command}` record; deterministic `k@t` or seeded `random`, replayable bit-exact.
- **directive 2 (determinism):** SATISFIED — seeded closed-form event, no runtime RNG in the plant path; default-off byte-identical (the hard gate: TERMINAL 194 / ENTRY-mppi 95 / AERO 44|60).
- **directive 3 (unsolvable → crash):** SATISFIED — a too-large/too-late engine-out reaches `F_LOC` (`sim.c:369`) or off-pad `V_CRASHED`; no assist toward the pad. The 1-engine landing-engine-out is correctly unrecoverable.
- **directive 5 (renderer pure observer):** SATISFIED — the injector is a plant input (like `--inject`); the renderer only *draws* the resulting attitude/plume/pred_impact from telemetry. A `BL_EVT` engine-out is a read-only notification, not a feedback path.
- **directive 7 (one dynamics source):** SATISFIED for the physics — the MPPI rollout copies `EnvCtx` (`guidance_mppi.c:350,555`) so it sees `thrust_offset` natively; the entry-burn n_eng reduction is pre-CUT (outside the rollout, which models the landing burn) — stated honestly, not hidden.
- **directive 9 (TERMINAL byte-identical):** SATISFIED — engine-out only fires under `MOD_ENGINE_OUT` during a multi-engine burn; TERMINAL (`n_eng=1`, no entry burn) never triggers it; `thrust_offset=0` folds identically.

## Appendix B — the one-line-per-fact code map (verify these before building)

| fact | file:line |
|---|---|
| thrust linear in `n_eng` | `dynamics.c:112` |
| thrust as one aggregate vector `Fthr` at base | `dynamics.c:128-134` |
| `arm_thr = r_gimbal − r_com` (the lever) | `dynamics.c:137` |
| induced torque `Tthr = arm_thr × Fthr` | `dynamics.c:138` |
| `thrust_scale` / `com_offset` fields | `dynamics.h:26,28` |
| `--inject` seeds both (the pattern) | `sim.c:65-76` |
| `com_offset` consumed at exactly one site | `dynamics.c:137` (grep-confirmed) |
| control PD closes on body-rate `w` (blind catch) | `control.c:152-153` |
| gimbal allocation divides by `n_eng·thrust` | `control.c:132,188` |
| "gimbal cannot roll" (RCS owns roll) | `control.c:195` |
| entry burn sets `n_eng=3`, full throttle | `sim.c:198,208` |
| entry-burn latch `st->n_eng = gcmd.n_eng` | `sim.c:240,277` |
| entry divert authority hardcodes `3.0` | `sim.c:154` (the one guidance edit) |
| ada freeze already `n_eng`-aware | `sim.c:248-250` |
| landing burn `n_eng=1`, `Tfull`, `a_max_now` | `guidance_hoverslam.c:87,93,115` |
| LOC fail `wmag>2` sustained >3 s | `sim.c:368-369` |
| STRUCT fail qbar>80kPa >2 s (only sink) | `sim.c:361-363` |
| MPPI rollout copies `EnvCtx` (directive-7) | `guidance_mppi.c:350,555` |
| MPPI rollout models 1-engine landing burn | `guidance_mppi.c:271,275` |
| `relights_left=2` (no third igniter) | `scenario.c:76` |
| CLI parse pattern (`--inject`/`--mppi`) | `main.c:204,206,246,248,766-775` |
| `BL_EVT_IGNITION_CMD` emits `n_eng` (event template) | `main.c:493` |
| canon: side-engine torque "neglected — recorded" | `CLAUDE_v1.md:292` |
| canon: thrust at pivot, pivot-to-CoM = authority | `CLAUDE_v1.md:366-368` |
| canon: `INJECT_DISTURBANCE` reserved enum | `CLAUDE_v1.md:669` |
| canon: engine-out demos reserved (M8) | `CLAUDE_v1.md:957` |
| geometry constants (`VEH_RADIUS`, `ENG_T_VAC`, gimbal) | `constants.h:25,28,42,283` |
