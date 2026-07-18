# Ignition-transient root-cause — analysis notes (scratch)

Tool: runs/ign_probe.c (MSVC-compiled against frozen Release objs; NOT part of build).
Traces: probe_entry_s42_r1.txt, probe_aero_s42_r6.txt (0.05 s res), probe_entry_sgn.txt (with steer_sign/amax).

## Mechanism — TWO stages, both wind-induced

Both AERO_OFFSET and ENTRY arrive at landing-burn ignition CENTERED and near-vertical
(feather+hold now WORKS: tilt 1-4.5 deg at light, not the old 5.5-6). The ~120-150 m drift is
built AFTER ignition in two stages:

### STAGE 1 (the SEED): un-shielded crosswind aero side-force during the ramp (~1.0 s)
- At ignition qbar~40 kPa, wind ~20 m/s (ENTRY) / ~15 m/s (AERO). "Zero AoA" = aligned with the
  RELATIVE wind, so the body sits at alpha_wind = atan(Vwind/Vdescent) ~ 3.9 deg from vertical.
- Aero side force = qbar*Aref*CNalpha*alpha_wind ~ 72 kN / m ~ +2.2-4.3 m/s^2 OUTWARD (downwind).
- CRITICAL: the SRP plume shield (dynamics.c CT>0.5 blend) does NOT engage until the ramp is ~40%
  up (CT crosses 0.5 at t~112.25, ~0.75 s after light). So the side-force acts UN-SHIELDED
  (shield=1.0) for the first ~0.75 s, then shielded 1.0->0.35 over the next ~0.5 s.
- Feather+hold ZERO the guidance command through this window AND steer_sign is -1->0, so NOTHING
  counters the push. Integrated: **+3.5 to +4.6 m/s outward Dv seeded in ~1 s** (ENTRY vrad -2.3 ->
  +0.9; measured stage-1 aero impulse = 4.61 m/s).

### STAGE 2 (the AMPLIFY): guidance cannot claw it back — steer_sign crossover + amax clamp
Measured over ENTRY t=113..117 (guidance now fighting hard):
- guidance commands **-84 m/s inward Dv**; vehicle realizes **+6.8 m/s (OUTWARD)**. Ratio -8%.
Two independent chokes:
1. **amax clamp** (control.c): a_vert_ref = G0+2 = 11.8 in the landing burn, tilt cap 15 deg ->
   amax = 11.8*tan15 = **3.16 m/s^2**, FIXED for the whole burn. The -13..-32 m/s^2 command is
   clamped to 3.16 before anything else.
2. **steer_sign** (control.c smooth aero/thrust crossover): = (thr_est - qbar*Aref*CNa)/max(...).
   - t=111.5..112.5 (ramp): steer_sign = -1.0 -> -0.57  (aero dominates -> steering NEGATED)
   - t=112.5..113.3: steer_sign passes through **0** (dead zone -> realized authority ~0)
   - t=113.3..117: steer_sign only **0.01 -> 0.44** (still << 1). Even at amax, usable inward is
     ~3.16*steer_sign = 0.03 -> 1.4 m/s^2. Theoretical best-case inward over t=113..117 = only
     **-2.8 m/s** vs the -84 commanded.
- Net: for ~1 s at the crossover the vehicle has ZERO lateral authority; the +3.5 m/s outward seed
  free-runs. Worse, the thrust vector is actually canted OUTWARD (a_thrLat +6.6 m/s over t=113..120)
  because the crossover's negated steering tilted the body downwind and the slow steer_sign recovery
  + attitude lag holds it there — the "tilt-reversal transient" the smooth-blend comment feared,
  merely spread over seconds instead of snapped.
- lat integrates 12 m -> 116 m (ENTRY) / 96 m (AERO) before authority recovers, by which point it is
  too low/late to null. Terminal miss 140-150 m.

## Percentage split of the ~140 m miss (per the impulse budget)
- ~55-65%: STAGE-1 un-shielded aero side-force seeding +3.5-4.6 m/s outward during the ramp.
- ~35-45%: STAGE-2 authority collapse (steer_sign crossover ~0 for ~1 s + amax=3.16 clamp +
  outward-canted thrust from the negated-steer attitude lag) — prevents ANY claw-back and adds
  its own +6.6 m/s outward.
The two are multiplicative in effect: stage 1 needs stage 2 to become 140 m (in still air the same
stage-2 weakness lands fine because there is no seed; measured 32%/71% land, med 5-6 m). So the
"cause" is the crosswind seed; the "amplifier that makes it 140 m instead of ~15 m" is the
crossover/clamp.

## Direction consistency
scenario.c: the SAME `az` seeds BOTH the mean-wind azimuth (line 29) AND the initial body-tilt
azimuth (line 67-68). wind_az depends on run_idx (constant across seeds for a given run). The push is
always DOWNWIND; vrad is unambiguously outward (+) in every trace. CSV td_lat is unsigned but the
sign is not in question.

## Why the tried fixes failed
- FEATHER: straightens the body to VERTICAL before light, but "unloaded" in crosswind means aligned
  with RELATIVE wind (3.9 deg off vertical). You cannot be both vertical AND zero-AoA in wind. Once
  vertical, the crosswind aero side-force (STAGE 1) fires anyway. Feather removed the OLD thrust*tilt
  term (good — tilt now 1-4.5 not 5.5-6) but not the aero side-force.
- 2 s HOLD: hands the wind a free, un-countered window == exactly STAGE 1. Makes it worse, not better.
- INTEGRAL Ki=0.004 gated past 2 s: (a) starts AFTER the seed is already in; (b) its output is a
  lateral accel command that is STILL clamped to amax=3.16 and STILL multiplied by the small
  steer_sign -> realized ~0 through the crossover; (c) Ki=0.004*rr with rr~15 m -> 0.06 m/s^2/step
  growth, and capped at 2 m/s^2 which is below what's needed and far too slow. It cannot beat a
  +3.5 m/s seed with sub-1 m/s^2 authority.

## Fix levers (evidence-ranked) — see final report for code
1. Kill STAGE 2's steer_sign veto during the LANDING burn: once engine_on & thrust dominates
   (CT>~1), FORCE steer_sign=+1 (thrust authority) instead of the smooth crossover. The crossover
   logic exists for the UNPOWERED aero descent; during a full-thrust landing burn the thrust DOES
   dominate, so the negation is simply wrong there. This alone restores the -84 command's sign and
   lets amax (3.16) act. Biggest single lever.
2. Damp-through-ignition: keep -Kvel*v_xy active from t=0 of the burn (drop the hold), so the +3.5
   m/s seed is arrested as it forms with the ~3.16 authority (with steer=+1 from fix 1).
3. Upwind pre-bias via legal attitude inference (tilt-vertical vs v_inertial gives relative-wind
   dir): aim ~3-4 m/s upwind before light so stage-1 seed nets ~0.
4. Raise amax mid-burn: a_vert_ref uses n*T/m (~26 m/s^2) not G0+2, tripling amax to ~9-10 m/s^2 (as
   ENTRY_BURN already does) — lets the claw-back actually reverse the seed. Safe: STRUCT is qbar>80k.
