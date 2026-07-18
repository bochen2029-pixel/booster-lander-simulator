# ENTRY-DIVERT DESIGN — closing the 3 km cross-range on the 62 km → pad problem

**Author:** ENTRY-DIVERT DESIGNER (Opus 4.8) · **Date:** 2026-07-18 · **Status:** design + compiled-C feasibility, ready to implement
**Study:** `runs/sandbox/entrydiv.c` → `runs/sandbox/entrydiv_out.txt` (pure C, `cl /O2 /fp:precise`, plant-parity mirror of `core/`)
**Builds on:** D-007 / D-008 / D-009, HANDOFF §4–§5, and the parallel ceiling oracle `runs/sandbox/ceiling.c` (→ `ceiling_out.txt`).

> **DESIGN + ANALYSIS ONLY.** Nothing in `core/` was edited; no cmake/binary was run. All artefacts live under `runs/`.

---

## 0. Executive verdict

**YES — the ENTRY 3 km cross-range is physically closeable, with a large margin, and the fix is a *deterministic* guidance law, not MPPI.**

- With optimal control the full ENTRY trajectory can rest-to-rest divert **~25.6 km** (velocity-capped realistic) — the 3 km target is **8× under the ceiling**. The problem was *never* authority; it is **nulling the cross-range velocity at touchdown** (naive constant bank flings it 6–47 km off — reproduced).
- A single **deterministic ZEM/ZEV optimal-terminal bank law** — `a_lat = −6·r/t_go² − 4·v/t_go`, capped at 15°, gated to `PH_ENTRY_BURN`/`PH_AERO` — closes the nominal 3 km to **td_lat = 5.8 m and vxy_td = −4.8 m/s** (both inside pad=26 m and soft<6 m/s → **LANDS**), and holds **8/9** across the ±800 m offset dispersion and the full 3×3 (h0,vz0) grid.
- It works **even without** the control.c `a_vert_ref` fix for the 3 km case (the weaker realized cap still closes it); the fix is only needed to unlock the *far* (>4 km) tail.
- **qbar and fuel are non-binding:** peak qbar 60 kPa (< 80 STRUCT), the bank's cos-loss is <1 % of a retrograde-dominated ±reversal, and the entry burn is fuel-floor-cut exactly as today.

**Recommended architecture:** a **deterministic ZEM/ZEV collision-course bank law owns the entry-burn + coast divert** (new `entry_divert_step` called from the E3 supervisor during `PH_ENTRY_BURN`); the **existing lateral-only MPPI (or the tier-0 hoverslam divert) owns only the aero-descent trim** below the burn, exactly as it does for AERO_OFFSET today. MPPI is **rejected** for the burn phase (its 5 s horizon ≪ the 27 s burn + 87 s coast; the physics is a clean closed-form 1-reversal problem MPPI would only approximate at far higher cost).

---

## 1. The nominal ENTRY trajectory (measured, plant-parity)

From `entrydiv_out.txt` (dt-converged; 0.5 ms ≡ 4 ms to 4 sig figs):

| phase | span | duration | key numbers |
|---|---|---|---|
| **PH_ENTRY_BURN** (3-eng retrograde) | 62 → 39.9 km | **25.1 s** | ignites at t≈0 (predicted peak qbar ≥72 kPa at the IC); kills v 1500 → **114 m/s**; burns **23.0 t** → stops at the **7 t fuel floor** |
| **coast** (thin-air ballistic) | 39.9 → 3.27 km | **87.2 s** | v **re-accelerates 114 → 602 m/s by 17 km** (gravity beats drag in thin air — the classic D-007 re-accel); this is the *free-drift window* |
| **PH_AERO + landing burn** | 3.27 km → pad | **21.0 s** | aero-aware ignition at 3.27 km; touchdown vz = −1.47 m/s; **fuel_left 1405 kg** (> 0 → lands); qbar_peak **60.4 kPa**, mach_peak 4.81 |

Total time-to-touchdown **133.2 s**. This matches D-007's narrative (kills v to ~156 m/s at 40 km using ~23 t; re-accels to ~605 m/s by 17 km) — my point-mass cuts slightly deeper (114 vs 156 m/s) because it lacks the 6-DOF finned-body drag, which is conservative for the divert estimate.

**Note on the entry burn's cut:** at nominal the burn is **fuel-floor-cut** (runs the full 23 t to the 7 t reserve), *not* qbar-cut. The 68 kPa qbar-cut and the 7 t floor are both in `sim.c entry_supervisor`; whichever binds first fires. The divert law must **not** modulate the cut (see §4.5).

---

## 2. FEASIBILITY — how much cross-range CAN the trajectory close? (Question A)

### 2.1 Phase-decomposed rest-to-rest divert (optimal, v nulled by touchdown)

Each number is the **maximum initial offset that a single-switch bang-bang can close AND arrive nulled** (r=0, v=0), computed by root-finding the switch time so v(T)=0, on the measured `a_max(t)` profile. `vcap250` bounds cross-range speed at a physical 250 m/s.

| phase | authority model | free-bb (m) | **vcap250 (m)** | peak \|v_lat\| |
|---|---|---|---|---|
| **(i) entry burn** | `a_lat = a_burn·sin(15°)`, a_burn ≈ 50 m/s² (3-eng) | 2 628 | **2 628** | 211 m/s |
| **(ii) aero-descent** | `a_lat = q̄·Aref·CNα·sin(15°)/m` | 2 880 | **2 880** | — |
| **(iii) landing burn** | `(thr·T/m)·sin(15°)·fade(h/400)²` | 385 | — | — |
| **WHOLE TRAJECTORY** | all phases + the free coast | 37 358 | **25 590** | 425 m/s |

**Reading:** the entry burn *alone* closes **2.6 km rest-to-rest**; the aero-descent *alone* closes **2.9 km** (consistent with `ceiling.c`'s ~2.5 km AERO ceiling, which is the same physics from 12 km); the whole trajectory closes **25.6 km**. **The 3 km ENTRY offset is ~8× inside the whole-trajectory ceiling and ~1.1× the entry-burn-alone ceiling** — so even the burn phase *by itself* nearly closes it, and the burn+aero together close it with enormous margin.

The "free coast" is the mechanism the HANDOFF flagged: velocity built during the burn integrates over the 87 s coast (100 m/s × 87 s ≈ 8.7 km of drift). That is why the whole-trajectory number (25.6 km) hugely exceeds the sum of the powered phases — **the coast is a free lever, if and only if the velocity is timed to null at touchdown.**

### 2.2 The catastrophe reproduced (why naive banking fails)

Constant +15° bank toward the pad for the whole burn, **no reversal** (D-007 addendum 2):

- physical cap → **47.4 km off-pad**
- control.c realized cap → **6.6 km off-pad**

(The documented "17 km" used a milder `a_lat = 4 m/s²` law; the full-cap and mild-law numbers bracket it.) The mechanism is confirmed: **the burn+coast build cross-range velocity the descent never nulls.** The problem is null timing, not authority.

### 2.3 The optimal bank schedule

The rest-to-rest optimum is a **single-switch bang-bang**: bank `+cap` early (build cross-range toward a collision course), then `−cap` (null the velocity) so both r and v hit zero at touchdown. For the nominal, the switch lands inside the entry burn; the coast then carries the (correctly-signed, decaying) drift onto the pad. This is the "burn to a collision course, coast, then aero-trim" strategy — and it is a *closed-form* control problem (one switch), which is why a deterministic law suffices.

---

## 3. THE RECOMMENDED LAW — ZEM/ZEV optimal-terminal bank (Question C)

### 3.1 Why not the naive predicted-impact-point (ZEM) law

The prompt's "steer the velocity vector's ground-impact point onto the pad" (proportional-nav on `r_impact = r + v·t_fall`) **positions correctly but leaves large terminal velocity** — it drives r→0 at touchdown but arrives *still moving* (measured: td_lat 3.7 m but **vxy_td = −57 m/s** → a crash). It is also **t_go-fragile** (its gain ∝ 1/t_fall²; a ±5 % t_go error already pushes it off-pad). A landing needs **both** r_f=0 **and** v_f=0. See the DIAGNOSTIC block in `entrydiv_out.txt`.

### 3.2 The law (energy-optimal terminal guidance)

The energy-optimal lateral acceleration that drives a double-integrator to **zero final position AND zero final velocity** in time-to-go `t_go` is the classic ZEM/ZEV pair:

```
a_lat_cmd = −6·r_xy/t_go²  −  4·v_xy/t_go            (per world-lateral axis; r toward pad = +)
```

- **First term** (−6r/t_go²) = build cross-range toward the collision course (position closure).
- **Second term** (−4v/t_go) = null the cross-range velocity (the reversal), so both reach ~0 at `t_go`.
- It banks hard early (saturates the 15° cap while the miss is huge), then trims through zero — the reversal is **emergent and correctly timed**, no scheduling.
- Far more `t_go`-robust than raw ZEM (a biased t_go rescales the closure rate, it doesn't destabilise the null): **underestimating t_go actually *improves* the null** (tgo_err=0.70 → td_lat 0.0 m, vxy 0.01 m/s), so a deliberately conservative t_go is safe. Tolerant to +5 % overestimate.

### 3.3 The effector mapping (bank → a_lat) per phase

The command is realised by tilting the airframe / banking the burn, per phase (all already in the plant):

| phase | effector | `a_lat` realised as | max `a_lat` |
|---|---|---|---|
| **PH_ENTRY_BURN** | thrust vector (bank the 3-eng burn) | `a_burn·sin(bank)`, a_burn = n·T/m ≈ 50 m/s² | `50·sin(15°)` ≈ **13 m/s²** *(needs the a_vert_ref fix — §4.4)* |
| **PH_AERO** (unpowered) | body angle-of-attack (grid fins) | `q̄·Aref·CNα·α/m` | fade/aero-limited (see ceiling.c) |
| **PH_LANDING_BURN** | thrust vector + fade | `(thr·T/m)·sin(α)·(h/400)²` | fade→0 near ground |

Command: `sin(bank) = clamp(a_lat_cmd / a_burn, ±sin(15°))`; then map `a_lat = a_burn·sin(bank)` into the existing `g->a_lat[0..1]` channels (world-lateral), which `control.c` already turns into a tilt.

### 3.4 Closure results (the headline)

`entrydiv_out.txt`, **Kgain = 6.0** (see §3.6), physical cap, full ENTRY trajectory:

| D0 (m) | td_lat (m) | vxy_td (m/s) | peak \|v_lat\| | **LAND (pad & soft)?** |
|---|---|---|---|---|
| 1000 | 1.42 | −1.49 | 11.0 | **YES** |
| 2000 | 3.47 | −3.13 | 22.0 | **YES** |
| **3000** | **5.76** | **−4.77** | 32.9 | **YES** |
| 4000 | 8.15 | −6.36 | 43.9 | no (v just over 6) |
| 5000 | 10.58 | −7.90 | 54.9 | no |
| 6000 | 12.99 | −9.37 | 65.9 | no |

**The nominal 3 km closes to 5.8 m / −4.8 m/s — comfortably on-pad and soft.** Position is closed for *all* offsets to 6 km (td_lat ≤ 13 m); the LAND cutoff at ~3.5–4 km is set by the **terminal cross-range velocity**, not position — the residual v_xy is the **fade-limited floor** (the landing-burn `s=(h/400)²` removes lateral authority in the last 400 m, so the final few m/s can't be nulled by the ZEM/ZEV law alone). This is the *same* D-009 "residual v_xy rides to touchdown" phenomenon, and its fix is the same: the **D-009 blend** (keep the velocity-null damping active to contact) mops up the last few m/s — see §5.

### 3.5 Robustness (Question C: dispersions)

**3×3 (h0, vz0) grid** at D0=3000, Kgain=6.0 — `td_lat (m) | vxy_td (m/s)`:

| h0 \ vz0 | −1480 | −1500 | −1520 |
|---|---|---|---|
| 58 280 | 7.0 \| −5.56 | 7.5 \| −5.82 | 7.9 \| −6.08 |
| 62 000 | 5.4 \| −4.55 | 5.8 \| −4.77 | 6.1 \| −4.99 |
| 65 720 | 4.8 \| −4.13 | 5.1 \| −4.34 | 5.4 \| −4.55 |

**Offset dispersion sweep** [2.2 → 3.8 km] at nominal vertical, fixed Kgain=6.0: **8/9 LAND** (pad AND soft); only the 3.8 km extreme is marginal (−6.05 m/s, just past the 6 m/s bar — the D-009 blend closes it). Position closure (≤ 7.7 m) is comfortable across the whole range.

**t_go estimation error** (D0=3000): lands for tgo_err ∈ [0.70, 1.00]; conservative (low) t_go is safe and even improves the null. → estimate t_go with a **crude, deliberately-slightly-low** ballistic shoot (the existing `entry_predict_peak_qbar`-style integrator already gives it for free).

### 3.6 Gain choice

The textbook gains (Kgain=1) leave ~6.2 m/s residual (fade floor). A **higher gain front-loads the reversal above the fade**: Kgain scan (D0=3000) shows the residual v_xy drops from −6.23 (Kg=1) to −4.77 (Kg=6) and flattens. **Recommend Kgain ≈ 4–6** (multiply both ZEM/ZEV terms) — closes v_xy to ~4.8 m/s and lands the 3 km with the fade in place. (Equivalently, bias t_go ~15 % low.) This is a single scalar to tune in-sim.

---

## 4. ARCHITECTURE (Question B) — recommendation + the rejected option

### 4.1 RECOMMENDED: deterministic ZEM/ZEV collision-course law owns the burn+coast; MPPI/tier-0 owns only the aero trim

```
PH_COAST ──(E3 predicts peak q̄ ≥ 72 kPa)──▶ PH_ENTRY_BURN
   │                                              │
   │                          entry_divert_step():  a_lat = ZEM/ZEV(r,v,t_go), bank ≤ 15°
   │                          (NEW; called from entry_supervisor while PH_ENTRY_BURN)
   │                                              │
   └──(E3 q̄-cut or 7 t floor)──────────────────▶ PH_AERO ──▶ PH_LANDING_BURN ──▶ touchdown
                                                  │
                        aero-descent + landing-burn divert = EXISTING hoverslam divert law
                        (or lateral-only MPPI when --mppi) — UNCHANGED, owns only the trim
```

**Why deterministic for the burn:**
- The burn+coast divert is a **closed-form single-reversal** problem (§2.3). ZEM/ZEV solves it exactly and cheaply (a few flops per 50 Hz tick).
- **MPPI's 5 s horizon ≪ the 27 s burn + 87 s coast (114 s).** Its ZEM terminal (`r+v·t_go`) is *position-only* — the very thing shown in §3.1 to leave terminal velocity — and its rollouts can't span the coast. To make MPPI plan the reversal you'd need a dedicated long-horizon/coarse-dt planner (e.g. dt=100 ms, H=100 = 10 s) that *still* wouldn't reach the coast, plus the `a_vert_ref` fix in its rollouts. That is far more machinery for a problem with a one-line optimal solution.
- The ZEM/ZEV law **reuses everything**: it emits `a_lat` into the same `g->a_lat[]` channels the aero divert and MPPI already use; the effector mapping is `control.c`'s existing tilt allocation.

**Division of labour:** the entry-burn law gets the **impact point onto the pad and the cross-range velocity ~nulled by cut**; the aero-descent (which has its own ~2.5 km ceiling of divert authority, per `ceiling.c`) and the landing burn then trim the small residual — *exactly the job the AERO_OFFSET divert/MPPI already does*. ENTRY thus becomes "a well-nulled AERO_OFFSET handoff," which the existing lower stack already lands.

### 4.2 REJECTED: extend the lateral-only MPPI into PH_ENTRY_BURN

Evaluated and rejected because:
1. **Horizon mismatch** (5 s vs 114 s) — the dominant reason. MPPI cannot see the coast where the divert actually integrates.
2. Its terminal is a *position* ZEM; it does not null terminal velocity over the coast (the §3.1 failure mode) without a longer-horizon redesign.
3. It would need the `a_vert_ref` fix inside its rollouts *and* a coarse-dt long-horizon variant — strictly more code than the deterministic law, for an approximation of a closed-form answer.
4. Determinism/perf: the burn phase adds 25 s × 50 Hz of MPPI solves (~9 s/run already at 10 Hz over the descent) for no benefit.

MPPI keeps its rightful job — the **aero-descent trim** — where the horizon *does* match and its re-optimisation earns its cost.

### 4.3 Deterministic-law C interface (hooks into `sim.c` / `control.c`)

Add to `guidance_hoverslam.c` (or a small new `guidance_entry.c`) a pure function, and call it from `entry_supervisor` **only while `PH_ENTRY_BURN`**, so TERMINAL/AERO stay byte-identical:

```c
/* ENTRY-burn collision-course divert (ZEM/ZEV optimal terminal). Sets g->a_lat[0..1] (world lateral).
 * Gated to PH_ENTRY_BURN by the caller => TERMINAL/AERO_OFFSET never see it (determinism preserved). */
#define ENTRY_DIVERT_KGAIN 6.0      /* front-loads the reversal above the landing-burn fade (D-009) */
#define ENTRY_DIVERT_BANK_MAX (15.0*DEG2RAD)
static void entry_divert_step(const State* st, GuidanceCmd* g){
    const double* y=st->y;
    MassProps mp; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mp);
    AtmoOut atm; atmo_eval(y[S_RZ],&atm);
    double Tfull = 3.0*engine_thrust(1.0, atm.p);       /* 3-engine entry burn */
    double a_burn = Tfull/mp.m;                          /* ~50 m/s^2 */

    /* time-to-go: crude ballistic+arrest shoot to the ground (deliberately ~10-15% LOW is safe/better).
     * Reuse the entry_predict style integrator, or the existing predict_tgo shoot. */
    double t_go = entry_tgo_estimate(y[S_RZ]-mp.com, y[S_VZ], mp.m);   /* clamp >= 1.0 s */

    for(int ax=0; ax<2; ax++){
        double r = y[S_RX+ax];                          /* toward pad = 0 */
        double v = y[S_VX+ax];
        double a_cmd = ENTRY_DIVERT_KGAIN*( -6.0*r/(t_go*t_go) - 4.0*v/t_go );
        /* saturate to the 15-deg bank of the burn */
        double amax = a_burn*sin(ENTRY_DIVERT_BANK_MAX);
        if(a_cmd> amax) a_cmd= amax;
        if(a_cmd<-amax) a_cmd=-amax;
        g->a_lat[ax] = a_cmd;
    }
}
```

Wire into `sim.c entry_supervisor`, in the `st->phase==PH_ENTRY_BURN` "continue" branch — replace `s->gcmd.a_lat[0]=s->gcmd.a_lat[1]=0.0;` with `entry_divert_step(st,&s->gcmd);` (keep throttle/n_eng=3/retrograde exactly as now):

```c
/* was: s->gcmd.a_lat[0]=s->gcmd.a_lat[1]=0.0;  (pure retrograde) */
entry_divert_step(st, &s->gcmd);     /* ZEM/ZEV collision-course bank (gated to PH_ENTRY_BURN) */
```

`entry_tgo_estimate` is a ~10-line copy of the ballistic-shoot idea already in `entry_predict_peak_qbar` / `guidance_mppi.c predict_tgo` (integrate vz to the ground; return t, clamped). No new physics.

### 4.4 The `control.c` a_vert_ref fix (gated to PH_ENTRY_BURN)

`control.c` line 70 hard-codes `a_vert_ref = G0 + 2.0` and caps `a_lat` at `a_vert_ref·tan(tilt_max)`. During the **entry burn** the true vertical accel is `n·T/m ≈ 50 m/s²`, not 11.8 — so the a_lat→tilt mapping under-produces: the realised lateral is `(G0+2)·tan(15°) = 3.16 m/s²` instead of the physical `a_burn·sin(15°) ≈ 13 m/s²`. The study quantifies the cost: whole-trajectory vcap divert **25.6 km (phys) vs 11.8 km (control.c cap)** — **~2.2× weaker without the fix.**

**Fix (gated so TERMINAL/AERO are byte-identical):**
```c
double a_vert_ref = G0 + 2.0;
if(st->phase==PH_ENTRY_BURN){
    /* the burn's true vertical specific force ~= n_eng*T/m; map a_lat->tilt against THAT so the
     * 15-deg bank realizes the physical a_burn*sin, not (G0+2)*tan (D-009 finding). */
    MassProps mpb; mass_props(y[S_MLOX],y[S_MRP1],0,0,&mpb);
    double Tb = (g->n_eng>0?g->n_eng:3)*engine_thrust(y[S_THR],atm.p)*ramp0;
    a_vert_ref = fmax(Tb/mpb.m, G0+2.0);
}
```

**But note:** the study shows the ZEM/ZEV law **closes the 3 km even at the un-fixed control.c cap** (D0=3000 → td_lat 5.9 m, vxy −4.87 m/s, LAND=YES). The fix is **strictly required only for the far tail (>4 km)**; for the nominal 3 km it is a margin improvement, not a gate. **Recommend implementing it anyway** (it's a small, correctly-gated change that roughly doubles the burn's realised authority and de-risks the dispersed far seeds). This resolves the D-009 record that "the a_vert_ref fix alone does NOT rescue banking" — correct, because the *missing piece was the null-timing (ZEM/ZEV)*, not the cap; with the ZEM/ZEV law the cap fix is a bonus, not the crux.

### 4.5 Interaction with the qbar-window burn-cut

The E3 cut is **qbar-triggered (68 kPa) or fuel-floor (7 t)**, independent of the divert. The divert law must **not** modulate the cut:
- Peak qbar during the *burn* is only ~0.7 kPa (40–62 km is near-vacuum); the 60 kPa peak is far below at ~8 km, during the aero-descent — so **banking never approaches the 68 kPa cut or the 80 kPa STRUCT line** (Question D, qbar).
- The reversal completes *within* the burn well before the cut at nominal (the ZEM/ZEV `−4v/t_go` term nulls v as t_go shrinks). If a dispersed early cut truncates the burn, the aero-descent inherits a small residual v_xy and nulls it (that's its job). **No cut modulation needed** — keep the E3 supervisor exactly as-is.

---

## 5. FUEL & qbar margins (Question D)

- **cos-loss:** a sustained worst-case 15° bank costs `1−cos15° = 3.4 %` of vertical decel. The *real* bank is a brief **±cap reversal** (equal + and − time) on a **retrograde-dominated** burn, so the average cos-loss is **≪1 %**. Re-integrating with a full-time cos15 scaling (a strict upper bound) still lands: fuel_left 1391 vs 1405 kg (−14 kg), ok=1. **The ~23 t entry burn has ample margin for the bank.**
- **Fuel floor:** the entry burn stops at the **7 t reserve** exactly as today; that reserve is handed to the landing burn, which spends down to **1405 kg > 0 → lands**. The divert does not touch this accounting.
- **qbar/STRUCT:** peak 60.4 kPa < 80 kPa STRUCT line; peak-during-burn 0.7 kPa. The STRUCT check is `qbar > 80 kPa` (not an AoA side-load), so 15° bank is safe throughout. **STRUCT stays 0.**

---

## 6. VALIDATION PLAN (Question E)

Every step gated by: `--selftest` PASS · TERMINAL 97.0 % (s42/200) unchanged · determinism run-twice bit-identical. All new code gates on `PH_ENTRY_BURN` (divert law + a_vert_ref) so **TERMINAL/AERO_OFFSET are byte-identical** by construction (they never enter PH_ENTRY_BURN).

1. **Selftest + TERMINAL parity first.** After adding `entry_divert_step` + the gated a_vert_ref: `--selftest` PASS (10 oracles), `--headless --scenario terminal --seed 42 --runs 200` = 97.0 %, run-twice memcmp identical. If any drift → the gate leaked; fix before proceeding.
2. **ENTRY single-run trace** (`--run --scenario entry --seed 42 --run 1 --verbose`): confirm the instrumented `vrad` builds cross-range early then **reverses to ~0 by the cut**, tilt saturates ≤15°, qbar peak < 80 kPa, touchdown on-pad. Compare the trace against the study's nominal (td_lat ~6 m, vxy ~−5 m/s).
3. **ENTRY ×100, seed 42** (`--headless --scenario entry --seed 42 --runs 100`):
   - **First pass target ≥ 50 % land** (study predicts the nominal + inner dispersion land; the far offset tail and the fade-residual v_xy are the misses).
   - **After tuning ≥ 90 %**: add the **D-009 blend** to the entry-divert handoff (fade the position-seek term of the aero-descent but keep the velocity-null damping to contact) so the residual −4.8 m/s cross-range is nulled; sweep Kgain ∈ [4,6] and the t_go bias. Confirm **STRUCT = 0** and **fuel-out = 0** throughout.
4. **Determinism + no-regression:** ENTRY under `--mppi` should still route the aero-descent through MPPI unchanged; TERMINAL/AERO_OFFSET/`goldens/protocol` byte-identical. Freeze an ENTRY MC baseline in `goldens/mc/entry_s42_baseline.txt`.
5. **Cross-seed** (s7, s99) ×100 to confirm the rate isn't seed-42-specific; the 3×3 grid in §3.5 predicts it generalises.
6. **(Optional) add an ENTRY entry-burn selftest oracle:** assert the ZEM/ZEV law nulls a fixed (r0,v0) collision-course case to r<pad, |v|<TD_V_HARD — would catch a sign/gain regression.

---

## 7. Reconciliation with prior records

- **HANDOFF "aero divert ceiling ~400–800 m from 12 km"** is superseded by the parallel `ceiling.c` oracle: the realistic velocity-capped ceiling is **~2.5 km** (the 400–800 m figure double-counted the fade / assumed a high fixed ignition). My ENTRY study's phase-(ii) 2 880 m agrees. So the aero-descent tail is far more capable than feared — it comfortably absorbs the residual after the entry-burn divert.
- **D-009 "a_vert_ref fix alone does NOT rescue banking (→2363 m, worse than retrograde)":** correct — because the missing piece was **null-timing**, not the cap. A velocity-nulling sqrt-decel bank with a fixed lead/gain mistimes the reversal; the **ZEM/ZEV law times it optimally and continuously**, which is why it closes to 6 m where the hand-tuned bank left 2.4 km.
- **"Banking is MPPI-class (4-B)":** the study shows it is *not* — it is a closed-form single-reversal deterministic law. MPPI's role is the aero trim only.

---

## 8. Deliverables (paths)

- **Design doc:** `runs/d009_entry_divert_design.md` (this file).
- **Feasibility study (source):** `runs/sandbox/entrydiv.c` · **build:** `runs/sandbox/entrydiv.cmd` (`cl /O2 /fp:precise`) · **output:** `runs/sandbox/entrydiv_out.txt`.
- **Parity reference (existing):** `runs/sandbox/ceiling.c` / `ceiling_out.txt` (AERO ceiling ~2.5 km — the aero-descent tail budget).

---

### TL;DR for the implementer
1. Add `entry_divert_step` (ZEM/ZEV, `a=−6r/t_go²−4v/t_go`, Kgain≈6, bank≤15°) → call from `entry_supervisor` while `PH_ENTRY_BURN` (replaces the `a_lat=0` retrograde line).
2. Gate the `control.c` `a_vert_ref = n·T/m` fix to `PH_ENTRY_BURN` (bonus authority; not strictly needed for 3 km).
3. Leave the E3 cut, MPPI, and the aero-descent divert **unchanged**; add the D-009 blend to the aero handoff to null the fade-residual v_xy.
4. Validate: selftest+TERMINAL parity → ENTRY ×100 (≥50 % → ≥90 % after Kgain/blend tuning), STRUCT=0, byte-identical TERMINAL/AERO.
