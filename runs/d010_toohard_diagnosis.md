# D-010 TOO-HARD cohort diagnosis + ranked fixes (agent TOOHARD / rpj3lzls, 2026-07-18)

Isolated tree: `_th_wt\` (top CMakeLists + core, MSVC Release). Kvel fins-deployed set 1.2->0.9
per directive. All builds/gates in `_th_wt\` only. Instrumentation added (worktree-only, no
physics/RNG change — verdicts bit-identical before/after adding it): `impact_vz`, `impact_vxy`
(vertical/horizontal split at first contact) and `h_ign,vz_ign,vxy_ign,fuel_ign` (landing-burn
ignition state), emitted as extra trailing CSV columns.

Cohort defn (matches headless "too-hard" bucket): verdict CRASHED/TIPPED, `td_lat<=26 m` (on-pad),
`td_v>6`, `fault!=FUEL`. Note `td_v = |v|_3D` at first contact = sqrt(vz^2 + vxy^2) — a residual
LATERAL speed feeds `td_v` just as much as vertical.

## Baselines (s42, this build)

| config | ENTRY land/too-hard | AERO land/too-hard | score(E+A) |
|---|---|---|---|
| D-009 (Kvel=1.2) | 50 / 10 | 60.3 / 40 | — |
| Kvel=0.9 (directive) | 69 / **23** | 71.7 / **40** | 140.7 |

Kvel=0.9 collapses OFF-PAD (ENTRY 38->5, AERO 73->40) and lifts landed rate, but TOO-HARD becomes
the binding failure (ENTRY 10->23). Matches peer KVEL sweep (#1170): ENTRY too-hard is monotonic in
Kvel — 1.0:17, 0.9:23, 0.8/0.7:30, while off-pad falls 30->5->3.

## ROOT CAUSE — the too-hard cohort is 100% LATERAL, not vertical

Instrumented vz/vxy at contact for the ENTIRE cohort (23 ENTRY + 40 AERO):

```
                 |vz|_contact          |vxy|_contact        lateral-dominated
ENTRY too-hard   mean 2.41 [1.63..2.61]  mean 6.44 [5.46..8.72]   23/23
AERO  too-hard   mean 2.38 [2.08..2.45]  mean 7.11 [5.56..10.20]  40/40
```

- **Vertical channel is HEALTHY.** `|vz|_contact ~= 2.4 m/s` in EVERY too-hard run — near the vtd=1.5
  target. base_frac=0.85 lands soft vertically; the terminal commit/min-throttle arrests vz fine.
- **The entire td_v excess is residual cross-range velocity.** `|vxy| > |vz|` in 63/63 runs.
  `td_v = sqrt(2.4^2 + vxy^2) ~= vxy` for vxy>5. td_v>6  <=>  vxy>5.5 m/s.
- **`vxy_contact` is a smooth continuum, not bimodal** (histogram: landed cohort maxes at vxy~5.6-5.8;
  too-hard is simply its right TAIL beyond ~5.5). This is a damping-margin tail, not a distinct mode.
- Tilt is uniformly LOW at contact (2.4-3.5 deg) — tilt-mediated cos-loss is NOT a contributor.
- **The pre-D-009 extreme slams (90-140 m/s) are GONE** — 0 runs with td_v>20 in either cohort.
  Worst is ENTRY 39 @ 9.01, AERO 291 @ 10.48.

Per-run examples (was -> the split-fix below):
- AERO 291: td_v 10.48 (|vxy| 10.2, overshot pad center, vrad flipped to +11 outbound) -> 5.25 LANDS
- AERO 196 (centered lat 1.8): td_v 9.34 (|vxy| 9.0) -> 4.09 LANDS
- ENTRY 77 (centered lat 1.0): td_v 6.40 (|vz| 2.6, |vxy| 5.9)

## Ignition-margin statistics — NOT the cause

The suicide_burn_margin trigger is NOT underestimating for dispersed seeds. Ignition state is
statistically IDENTICAL between the too-hard cohort and the soft-landing cohort (td_v<=4):

```
ENTRY  too-hard   h_ign mean 2951 m [2633..3973]  |vz_ign| 300  fuel_ign 7615 kg
ENTRY  soft       h_ign mean 2796 m [2614..3532]  |vz_ign| 295  fuel_ign 7211 kg
AERO   too-hard   h_ign mean 2970 m [2823..3080]  |vz_ign| 285  fuel_ign 10002 kg
AERO   soft       h_ign mean 2998 m [2868..3210]  |vz_ign| 287  fuel_ign 10010 kg
```

Too-hard seeds do NOT ignite late/low, do NOT arrive faster, and land with FULL fuel reserve
(7.6t ENTRY / 10t AERO). The vertical suicide burn is not the binding constraint. => dispersion-aware
ignite-margin (mission candidate a) and gentler base_frac (candidate b) target the wrong channel.

## MECHANISM (predicts peer KVEL's trend)

The near-ground velocity-null gain that arrests `v_xy` before contact IS `Kvel`. In the fins-deployed
landing burn, once `lat_scale=((h_feet-30)/90)^2` fades the position-SEEK to zero (below h_feet~30),
the command is pure `Kvel*(-v_xy)` (same in the 0-2 s ignition-hold window). Dropping Kvel 1.2->0.9
weakens that null by 25% -> residual cross-range velocity (built up crossing the aero/thrust dead-zone)
is not fully nulled -> the vxy tail spills past 5.5 m/s -> too-hard.

Kvel does TWO OPPOSED jobs: (A) shape the aero-divert inward-velocity command up high [off-pad wants
Kvel LOW — a gentle inward command doesn't overshoot into the crossover dead-zone] and (B) null v_xy
near the deck [too-hard wants Kvel HIGH]. They are coupled today. This exactly predicts KVEL's
monotonic too-hard-vs-Kvel (weaker B as Kvel drops) with simultaneously falling off-pad (better A).

## RANKED FIXES

### #1 (RECOMMENDED, TESTED) — Height-split the cross-range velocity-null gain (fins-deployed)

Decouple (A) and (B) by ALTITUDE: keep the divert Kvel on the inward-SEEK term (divert byte-identical,
off-pad frontier preserved) but BOOST only the `-v_xy` null gain as the feet approach the deck.

`_th_wt\core\guidance_hoverslam.c`, near the top (with LANDING_IGNITE_MARGIN):
```c
/* TH FIX A — height-split of the cross-range velocity-null gain (fins-deployed only). */
#define KVEL_SPLIT_H  250.0
#define KVEL_NEAR     1.6
```
In `hoverslam_step`, replace the two `g->a_lat[...] = Kvel*(vdes[...]*lat_scale - v_xy[...]);` lines:
```c
double Kvd = Kvel;
if(st->fins_deployed){
    double b = (KVEL_SPLIT_H - h_feet)/KVEL_SPLIT_H; if(b<0.0)b=0.0; if(b>1.0)b=1.0;
    Kvd = Kvel + b*(KVEL_NEAR - Kvel);   /* 0.9 above SPLIT_H -> 1.6 at the deck */
}
/* Boost ONLY the -v_xy null term; keep the inward-seek at the divert Kvel. Boosting the seek too
 * chases the pad faster -> more overshoot into the crossover -> WORSE too-hard AND off-pad (measured
 * SCORE 154.7 damp-only vs 135.7 whole-error-boost). Above SPLIT_H Kvd==Kvel (byte-identical). */
g->a_lat[0] = Kvel*vdes[0]*lat_scale - Kvd*v_xy[0];
g->a_lat[1] = Kvel*vdes[1]*lat_scale - Kvd*v_xy[1];
```
Also use `Kvd` in the 0-2 s damp-through-ignition block (correct-by-construction; =Kvel up high):
```c
if(st->fins_deployed && st->ign_timer>=0.0 && st->ign_timer<2.0){
    g->a_lat[0] = Kvd*(-v_xy[0]);
    g->a_lat[1] = Kvd*(-v_xy[1]);
}
```
`Kvd` must be declared BEFORE the `if(!st->engine_on){...return;}` block so both the unpowered and
powered paths (and the ign-window block after the return) see it. TERMINAL (fins stowed) untouched.

MEASURED (gated, s42): selftest PASS, TERMINAL 194/200 exact.
| | ENTRY | AERO | SCORE |
|---|---|---|---|
| Kvel=0.9 baseline | 69% (too-hard 23, off-pad 5) | 71.7% (th 40, op 40) | 140.7 |
| **+ split (RECOMMENDED)** | **78%** (th **15**, op 4) | **76.7%** (th **15**, op 49) | **154.7** |

Per-run verification: AERO 291 10.48->5.25 LANDS, AERO 196 9.34->4.09 LANDS. Residual too-hard (15/15
each) is still 100% lateral, now tightly just over the line (vxy 5.6-8.4) — the deepest-cross-range
seeds (e.g. ENTRY 39 vxy_ign 20.7, AERO 92 vxy_ign 17.5) that even 1.6x can't null in the time left.
Tuning: KVEL_NEAR sweet spot 1.6 (1.4 under-damps: th 21/35; 1.8 over: "other"/fuel-out creep,
score 153). SPLIT_H sweet spot 250 (400 over-fights seek -> off-pad up; 200/150 too little time).

### #2 (TESTED) — Split + LOWER divert Kvel (exploit the decoupling; ENTRY/AERO-opposed)

With the split protecting too-hard near the deck, the divert Kvel is FREE to go lower to chase off-pad.
Change only `double Kvel = st->fins_deployed ? 0.9 : 0.6;` (keep split 1.6/250):
| divert Kvel | ENTRY land/too-hard | AERO land/too-hard | SCORE |
|---|---|---|---|
| 0.9 (fix #1) | 78 / 15 | 76.7 / 15 | 154.7 |
| 0.8 | 74 / 23 | 82.3 / 4 | 156.3 |
| 0.7 | 72 / 24 | **86.3 / 0** | **158.3** |
ENTRY too-hard is MINIMIZED at divert 0.9; AERO too-hard at 0.7 — the two scenarios want OPPOSITE
divert gains (ENTRY carries more cross-range from its longer/faster divert, so a weak inward command
hurts it). Best raw score is divert 0.7 (AERO 86.3%, too-hard 0) but it regresses ENTRY. Recommend
divert 0.9 for balance; hand divert-Kvel to KVEL's systematic sweep as a now-independent knob. A
STATE-adaptive divert Kvel (scale with ignition energy / h_ign, since guidance can't see scenario)
is the principled follow-up.

### #3 (NOT recommended — TESTED negative) — base_frac 0.85->0.80 (mission candidate b)

Diagnosis predicts no help (vz is already ~2.4, not the problem). Confirmed: base_frac 0.80 + split
gave ENTRY 77 (th 16) / AERO 75.3 (th 14, off-pad 49->55), SCORE 152.3 < 154.7. A gentler/longer burn
adds wind exposure (more off-pad) and spends fuel, without touching the lateral tail. Reject for
too-hard. (Also rejects mission candidate a — dispersion-aware ignite margin — by the ignition-margin
stats above: too-hard and soft seeds ignite identically.)

## Bottom line

Too-hard under Kvel=0.9 is a near-ground LATERAL velocity-null-authority tail, fully explained and
predicted by the Kvel monotonic trend. Fix #1 (height-split the null gain) converts ~1/3 of the
too-hard cohort to landings on ENTRY and >half on AERO with zero TERMINAL byte-parity impact, and
makes the divert Kvel a clean independent lever for the off-pad/landed frontier (fix #2). base_frac
and ignite-margin are the wrong channels for this failure.
