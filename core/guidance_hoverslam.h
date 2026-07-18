/* guidance_hoverslam.h — tier 0 suicide-burn guidance. CLAUDE_v1.md §9.1. */
#ifndef BL_GUIDANCE_HOVERSLAM_H
#define BL_GUIDANCE_HOVERSLAM_H
#include "state.h"
#include "guidance.h"

/* Compute guidance command from (nav) state. Stateless per tick (recomputed). */
BL_HD void hoverslam_step(const State* st, GuidanceCmd* g);

/* D-012 STATE-ADAPTIVE divert gain schedule (fins-deployed POWERED-BURN lateral law). SHARED
 * with the MPPI rollout lean model (directive 7: the execution blend inherits hoverslam's law
 * via mppi_execute -> hoverslam_step, so the rollout damping mirror must use the SAME schedule —
 * header-shared so the constants cannot drift).
 * Gain = KDIV_SEEK at-or-below the vdes profile, blending to KDIV_BRAKE over KDIV_VBLEND m/s of
 * PROFILE OVERSPEED (|v_xy| - vdes_mag > 0: residual cross-range speed the profile cannot null).
 * POWERED-ONLY GATE — the v1/v2/v3 factorial (s42 ENTRY/AERO, all gates green each build):
 *   v1 SEEK .7/BRAKE .9 both phases: 69 / 66.3 — DO NOT RETRY (the TH-tree 0.7-seek optimum
 *      does not transfer under C14; AERO th->0 + GOOD 46->83 shows a soft-arrival upside but
 *      off-pad +31 kills it)
 *   v2 SEEK .9/BRAKE 1.2 both phases: 77 / 78.7 — AERO off-pad 58->37 but ENTRY th 7->12
 *   v3 brake UNPOWERED-only:          77 / 72.7 — isolates the split: ALL the AERO off-pad win
 *      is burn-phase braking (v3 lost it), ALL the ENTRY damage is unpowered braking (v3 kept
 *      it: th 12, fuel-out 6 — AoA episodes during the long aero-descent disturb trim/ignition
 *      with zero off-pad payoff), and burn-braking costs ENTRY nothing (v2==v3 on ENTRY).
 *   => brake POWERED-ONLY (v4). Also re-reads D-010's "Kvel 1.2 over-drove tilt (off-pad 30)":
 *      uniform-1.2's damage was the unpowered phase all along; the burn WANTS overspeed braking.
 *   v4 SEEK .9/BRAKE 1.2 powered-only: 84 / 76.3 (sum 160.3, best composed) — ENTRY restored
 *      (fuel-out 3->1: braking sheds lateral speed cheaper than trim-tilt does) + AERO keeps
 *      op 37. Conversion frontier moved to too-hard (AERO 22->30: ~1/3 of converted off-pads
 *      still arrive hot) — that is the BRAKE/VBLEND sweep's target (runs/d012_sweep.csv). */
/* Value selection (runs/d012_sweep.csv, 11-config grid, all rows selftest+TERMINAL-194 gated):
 * ENTRY th responds to brake ONLY at sharp onset (vblend 3: th 9->7->6->5 across brake 1.05->1.5;
 * at vblend 6/10 it is flat ~8-9). AERO wants moderate braking (op 36 @1.2/3 vs 47 @1.5/3 —
 * over-braking mid-burn costs deep-divert reach). (1.5,3) taken for the M6 push: ENTRY 88
 * (th 5, op 5) at AERO 73.3; the co-leaders (1.2,3)=E86/A77.3 and (1.5,6)=E87/A76.3 are the
 * fallbacks if cross-seed disagrees. */
#define KDIV_SEEK    0.9
#define KDIV_BRAKE   1.5
#define KDIV_VBLEND  3.0

/* Entry-burn supervisor (E3) predictor: forward-shoot a cheap ballistic (no-thrust) vertical
 * channel from (altitude h0, speed0, mass) to the ground, returning the PEAK dynamic pressure
 * [Pa] encountered. Predictive trigger for the 3-engine entry burn (Agent A §9) — see sim.c. */
BL_HD double entry_predict_peak_qbar(double h0, double speed0, double mass);

#endif
