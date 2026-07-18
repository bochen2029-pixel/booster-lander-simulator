# Agent A — Grid-fin 6-DOF aero model + entry guidance (scratch/derivation)

## 1. Geometry & frames (canon §4.1)
- Body frame: origin at base-plane center, **+Z toward interstage (up the vehicle)**, X through
  leg pair (0deg), Y at 90deg. Fins at azimuths phi_i in {45,135,225,315} deg.
- Descent is **engine-first**: base (Z=0) leads, relative wind hits from -Z body. So `vrel_b`
  has large NEGATIVE z. AoA measured off -Z axis: cos(alpha) = -vhat_z (matches dynamics.c).
- Each fin i sits at mount position (body): r_i = (Rf*cos phi_i, Rf*sin phi_i, FIN_Z),
  Rf = VEH_RADIUS = 1.83 m, FIN_Z = 45 m.

## 2. Per-fin unit vectors (body frame)
For fin i at azimuth phi_i, define the local right-handed fin triad:
- **radial (outward)**  e_r_i  = (cos phi, sin phi, 0)
- **tangential (roll)** e_t_i  = (-sin phi, cos phi, 0)   [+ in the +azimuth / left-hand-about-+Z sense]
- **axial**             e_z    = (0,0,1)

A grid fin is a lattice panel whose chord lies (nominally) along the body axis (Z) and whose span
lies radially. It hinges about the **tangential axis e_t_i** (deflection delta_i rotates the panel
leading edge into/out of the flow). Its aerodynamic **normal** (lift direction at zero deflection)
is the tangential direction e_t_i: a positive alpha on that panel pushes it tangentially. Deflection
delta_i rotates the effective panel normal in the e_r/e_z plane... but the FORCE a grid fin makes is
dominated by turning the through-flow: to first order the fin normal force acts along a direction
n_i that combines the panel's response to (a) axial flow through a deflected panel and (b) cross-flow.

### Simplest faithful model (the one to implement)
Treat each fin as a flat lifting surface with:
- **planform normal at zero deflection** pointing in the **tangential** direction? No — a grid fin
  mounted with chord along Z and hinge along tangential e_t makes its lift in the plane spanned by
  {e_r, e_z} as it deflects, i.e. lift is ~ tangential-normal? Resolve carefully below.

Hinge axis = e_t_i (tangential). Deflecting by delta_i about e_t_i tilts the panel's chord line
(originally along +Z) toward +/- e_r. The panel's outward normal at zero deflection is e_t_i is WRONG
for a control fin — a control fin's normal (the direction it pushes air, hence reaction force) must
have a component that changes with delta about the hinge. Rotating a Z-aligned chord about the
tangential axis sweeps the chord in the (e_r, e_z) plane; the panel normal is therefore in the
(e_r, e_z) plane and rotates with delta:
    n_i(delta) = sin(delta_eff) * e_z_flowish ... 

CLEANER: work in the local 2D aero of each fin using two scalars:
  - local axial flow  w_ax_i = vrel_b . (-e_z)   (flow along -Z, the "freestream" the fin sees)
  - local radial flow w_r_i  = vrel_b . e_r_i
  - local tang. flow  w_t_i  = vrel_b . e_t_i
The grid fin is a control surface hinged on e_t: its lift responds to the flow component in the
(e_z, e_r) plane. Effective local AoA of fin i:
    alpha_i = delta_i + atan2(w_r_i, w_ax_i)            (deflection + incoming flow incidence in that plane)
and the fin lift acts along e_r_i (radial), magnitude:
    L_i = qbar_i * S_f * CNa_f(M) * alpha_i             (clamp/ stall at |alpha_i|>25deg)
plus a drag/axial term along -flow. This gives:
  - RADIAL force  Fr_i = L_i               (this is what makes pitch/yaw)
  - AXIAL  force  Fa_i = -(qbar_i S_f)*(CD0 + k*CNa*alpha_i^2)  along flow (small; damping/roll via tang)

Wait: radial lift on fins on OPPOSITE sides gives a couple => pitch/yaw. Tangential/roll comes from
the drag differential when fins deflect (grid-fin ROLL = differential fin cant producing tangential
force). Model roll via a dedicated tangential force from deflection:
    Ft_i = qbar_i * S_f * CT_delta * delta_i   (each fin's cant makes a tangential push; sum -> roll torque)

## 3. omega x r (MANDATORY, trap 6.9.5)
Local velocity at fin i mount:  v_local_i = vrel_b + omega x r_i
Use v_local_i (NOT vrel_b) to form w_ax, w_r, w_t. The omega x r term makes fins DAMP pitch/yaw/roll
rate naturally (a pitch rate changes each fin's local alpha with opposite sign fore/aft).

## 4. qbar per fin
qbar_i = 0.5 * rho * |v_local_i|^2. (rho at vehicle altitude; fins ~45 m up, negligible delta-h.)

## 5. CNa_f(M) table (canon App-A.6)
CNa_f(M) = 3.0 * dip(M),   dip = 0.55 for 0.8<M<1.2 (transonic authority hole from bow-shock cell choke),
                                = 0.8  for M>2.0,
                                = 1.0 otherwise, smoothly blended at the edges.
Stall: |alpha_i| > 25 deg -> CN saturates (hold CN at stall value, or linear-then-flat).

## 6. Force application & torque
For each fin: F_i (body) = Fr_i*e_r_i + Ft_i*e_t_i + Fa_i*e_axial(flow).
Apply at r_i. Torque_i = (r_i - r_com) x F_i. Sum all 4. Add to Tb, Fb in dynamics_deriv.

## 7. Effectiveness matrix B (4 deflections -> 3 body torques) for allocation/RAW MPPI
Linearize about current flight condition. Let k = qbar * S_f * CNa_f(M) (radial lift per rad),
Larm_z = (FIN_Z - com)  (axial lever from CoM to fin ring, POSITIVE, fins above CoM),
Rf = fin radial offset.

Fin i radial-lift response to its own deflection: dFr_i/ddelta_i = k, along e_r_i.
Radial force at (Rf cos phi, Rf sin phi, FIN_Z) with lever to com:
  arm_i = (Rf cos phi, Rf sin phi, Larm_z)
  F_i   = k*delta_i * (cos phi, sin phi, 0)
  tau_i = arm_i x F_i
        = | i               j               k              |
          | Rf cos          Rf sin          Larm_z         |
          | k d cos         k d sin         0              |
   tau_x = Rf sin*0 - Larm_z*(k d sin)      = -Larm_z k d sin phi
   tau_y = Larm_z*(k d cos) - Rf cos*0      = +Larm_z k d cos phi
   tau_z = Rf cos*(k d sin) - Rf sin*(k d cos) = Rf k d (cos sin - sin cos) = 0
So radial lift makes PURE pitch/yaw (tau_z=0), as expected. Roll must come from tangential (drag-cant).

Tangential (roll) response: Ft_i = kr*delta_i along e_t_i=(-sin,cos,0), kr = qbar*S_f*CT_delta.
  F_i = kr d (-sin phi, cos phi, 0), arm same.
  tau_z = Rf cos*(kr d cos) - Rf sin*(kr d (-sin)) = Rf kr d (cos^2 + sin^2) = Rf kr d
  tau_x = Rf sin*0 - Larm_z*(kr d cos) = -Larm_z kr d cos phi
  tau_y = Larm_z*(kr d (-sin)) - Rf cos*0 = -Larm_z kr d sin phi
So tangential force makes roll (Rf kr per fin, all add if same sign) + small pitch/yaw cross term.

### The 3x4 effectiveness matrix (torque = B @ delta), phi = 45,135,225,315
Using radial-lift dominant (pitch/yaw) + tangential (roll). Let A = Larm_z*k (pitch/yaw gain per rad),
                                                                 C = Rf*kr    (roll gain per rad).
  sin45=cos45=.7071; (phi: 45,135,225,315)
  cos phi = [ .707, -.707, -.707,  .707]
  sin phi = [ .707,  .707, -.707, -.707]
  tau_x = -A * sin phi   (from radial)   = A*[-.707,-.707,+.707,+.707]
  tau_y = +A * cos phi                    = A*[+.707,-.707,-.707,+.707]
  tau_z = +C * [1,1,1,1]  (from tangential cant, same sign all fins -> roll)

B (3x4):
        f1(45)    f2(135)   f3(225)   f4(315)
 roll  [  C        C         C         C    ]
 pitch [ -.707A   -.707A    +.707A    +.707A]   (tau_x, "pitch" about body X)
 yaw   [ +.707A   -.707A    -.707A    +.707A]   (tau_y, about body Y)

Allocation: given desired (roll,pitch,yaw) torque, delta = damped pseudo-inverse of B, clamp to
+/-20deg and rate 20deg/s. Because B has full row rank 3 with 4 fins, there is a 1-dim null space
(the "all fins equal" roll mode is orthogonal to pitch/yaw), so pitch/yaw/roll decouple cleanly:
  d_pitch:  delta = 0.707A^-1 * [-1,-1,+1,+1]*tau_x_cmd / (4*0.5)  ... (solve LS)
  Actually pseudo-inverse of the fixed pattern:
    delta_roll_i  = tau_roll_cmd  / (4C)          (all four +equal)
    delta_pitch_i = tau_pitch_cmd * s_p_i /(2*0.707A) with s_p=[-1,-1,+1,+1] pattern
    delta_yaw_i   = tau_yaw_cmd   * s_y_i /(2*0.707A) with s_y=[+1,-1,-1,+1]
  sum and clamp. (Because the pitch pattern [-1,-1,1,1], yaw [1,-1,-1,1], roll [1,1,1,1] are mutually
  orthogonal, the LS pseudo-inverse is just projection onto each pattern -> the closed forms above.)

## 8. Sign / stability check
Deployed fins at Z=45 (above com ~ 20-30 m) in engine-first descent are DOWNSTREAM (top trailing).
A body-axis perturbation alpha_body tilts the vehicle; the fixed fins see increased local alpha and
make radial lift that produces a RESTORING pitch torque (CoP moves toward fins, aft of CoM in flow
sense) => marginally stable base-first, matches canon §5.4. Verify sign in sim by a released-fin
damping test (nonzero omega decays).

## 9. ENTRY-BURN SUPERVISOR (targeting) — validated numbers
- Trigger is PREDICTIVE, not current-qbar. Each guidance tick: forward-shoot a cheap ballistic
  (no-thrust) vertical channel to touchdown -> predicted peak qbar (reuse hoverslam shooting pattern).
- IGNITE 3-eng full throttle when predicted_peak_qbar >= 80 kPa (fires ~62 km immediately, since
  ballistic peak is ~500 kPa). CUT when predicted_remaining_peak_qbar <= 76-78 kPa (fires ~40 km).
- Validated: qbar peak 75.8 kPa (0 s over 80), qdot 18.5 kW/m2 (0 s over 300), robust fpa[-60,-80],
  v0[1400,1700]. Prop used ~20.7 t -> reserve ~9.3 t (worst dispersion corner ~8.4 t > landing-need).
- The predictive-to-touchdown trigger self-consistently caps BOTH the burn-phase qbar AND the
  downstream aero-descent qbar (a hand-set 340 m/s @ 40 km cut re-accelerates to 91 kPa — over limit;
  the predictive cut carries the right velocity out so the whole descent stays < 80 kPa).

## 10. AERO-DESCENT GUIDANCE (40 km -> 4.6 km landing-burn handoff)
- Dominant divert lever is BODY-AoA (body Aref=10.52, CNa~2.4 makes ~4x the 4 fins' lift). Fins
  TRIM+STABILIZE the body at the commanded AoA (deployed fins put CoP aft of CoM -> holdable AoA).
- Guidance law: a_lat_cmd = -Kp*r_xy - Kd*v_xy (Kp~0.6, Kd~1.2); convert to body AoA
  alpha = clamp(m*a_lat/(qbar*Aref*CNa(M)), aoa_cap(qbar)); attitude loop holds alpha; fins allocate.
- aoa_cap SCHEDULE (side-load §5.7: |alpha|>15deg & qbar>30kPa >2s = STRUCT_FAIL):
  12 deg for qbar<10 kPa, ramp to 4 deg for qbar>30 kPa. Never trips the side-load line.
- Cross-range authority (measured): 4deg=664 m, 6deg=998 m, 8deg=1333 m over the descent -> beats
  the 800 m AERO_OFFSET demand. Combined with landing-burn's ~550 m thrust-vector divert = >1.3 km.
- SEQUENCING (C's caveat, critical): do the gross AoA-divert HIGH (>4-8 km). Below the terminal
  approach, set vdes/position-seek AoA -> 0 but KEEP the Kd*v_xy velocity-damping active to the
  ground, so the vehicle does not carry cross-range RATE into the landing burn (else the TERMINAL
  lateral limit-cycle just moves up to AERO).

## 11. HANDOFF to hoverslam landing burn
- Handoff at h ~ 4.6 km, |v| ~ 410 m/s (a bit hot vs canon's 310 m/s narrative; hoverslam re-solves
  ignition every tick so it absorbs this). No state discontinuity: the phase machine transitions
  AERO_DESCENT -> LANDING_BURN when the hoverslam forward-shoot solver reports a feasible ignition
  (v falls onto the suicide-burn v_ref profile). Fins stay active as aero dampers through the burn
  until qbar drops (§8.3 allocation: fins fade out as qbar<2000 Pa near the ground, gimbal takes over).
- The aero-descent lateral guidance and hoverslam's lateral channel are the SAME law form
  (a_lat = -Kp*r_xy - Kd*v_xy); only the actuator changes (AoA/fins above, tilt/gimbal below).
  This makes the handoff seamless — the outer loop is continuous, the inner allocation switches.

## 12. STAGED BUILD/TEST PLAN for ENTRY (quantitative gates)
Order chosen to unblock the dependency chain fins -> aero-descent -> entry supervisor, and because
Agent C proved fins are the critical path for everything past TERMINAL.

E0. FIN AERO PLANT (dynamics.c). Implement §2-§6 per-fin force at mount incl omega x r; add
    FIN_CT_DELTA_FRAC=0.35 to constants.h. Wire fin force into dynamics_deriv (currently absent).
    GATE E0 (unit/oracle, no guidance): (a) released-fin damping test — spin the vehicle to
    omega=0.3 rad/s at qbar~20 kPa, fins neutral: |omega| must DECAY (fins damp, trap 6.9.5).
    (b) static-margin sign: at fixed alpha_body=5deg, deployed fins produce a RESTORING pitch
    torque (CoP aft of CoM); stowed = destabilizing. (c) a single fin at +10deg, qbar=30kPa
    produces the analytic radial force qbar*Sf*CNa_f*alpha to 1e-6. (d) determinism memcmp still green.

E1. FIN ALLOCATION (control.c). Replace act->fins[]=0 with the projection allocator (§7):
    delta_roll/pitch/yaw closed forms from the B matrix, clamp 20deg/20deg/s. Extend §8.3 regime
    logic so 150-2000 Pa = RCS+fins, >2000 Pa = fins(+gimbal when burning).
    GATE E1: commanded (roll,pitch,yaw) torque triple is realized to <5% at qbar=30 kPa (no stall);
    pitch/yaw/roll cross-coupling < 5% (orthogonality holds); RCS<->fin handover monotone across
    the qbar regime boundaries (no torque discontinuity).

E2. AERO-DESCENT GUIDANCE (guidance_hoverslam.c or a new guidance_aero.c). Add the body-AoA
    lateral law (§10) active in AERO_DESCENT phase, with the aoa_cap schedule and the
    velocity-null-to-ground sequencing. Phase machine: COAST->(supervisor)->AERO_DESCENT->LANDING_BURN.
    GATE E2 (AERO_OFFSET scenario, fins live): >= 90% LANDED at 800 m lateral offset over 500 seeds;
    divert ceiling (max lateral offset still landing) >= 1000 m; ZERO STRUCT_FAIL from side-load;
    qbar stays < 80 kPa throughout (the aero-descent segment).

E3. ENTRY-BURN SUPERVISOR (new guidance_entry.c or sim.c hook). Predictive-peak-qbar ignite/cut
    (§9), 3-engine, above the MPPI/hoverslam layer. Thermal (Sutton-Graves) + qbar budgets live.
    GATE E3 (ENTRY scenario 62km): >= 95% LANDED over 500 seeds; qbar peak <= 80 kPa AND qdot peak
    <= 300 kW/m2 with ZERO sustained exceedance (0 STRUCT_FAIL, 0 THERMAL_FAIL); landing reserve at
    aero-descent handoff >= 8 t (mean ~9.3 t); GOOD+ >= 85%.

E4. INTEGRATION + ROBUSTNESS (feeds C's Tier-B). Combine E0-E3; run ENTRY under Tier-B disturbance
    (NAV_NOISY + 12 m/s gust + -8% thrust + -1% Isp + 2cm CoM). GATE E4: LAND >= 90% (ENTRY is the
    hardest scenario), reserve >= landing-need + 1.5 t slack (C's fuel-margin gate), ZERO
    controller-LOC, MONOTONE degradation in single-disturbance sweeps (no cliff). Freeze ENTRY
    golden (MC baseline + a canned ENTRY trajectory hash) — operator-signed.

E5. (post-MPPI) MPPI OWNS aero-descent+landing under the E3 supervisor. HIER first: MPPI commands
    a_lat, the E1 allocator+E2 AoA law run INSIDE the rollout (Agent B reuses them verbatim).
    RAW later: MPPI commands fin deltas directly via the B matrix. Entry supervisor stays a
    supervisor (it is a hard qbar/heat constraint, not something to hand to the stochastic planner).
