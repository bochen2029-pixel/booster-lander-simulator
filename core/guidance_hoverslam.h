/* guidance_hoverslam.h — tier 0 suicide-burn guidance. CLAUDE_v1.md §9.1. */
#ifndef BL_GUIDANCE_HOVERSLAM_H
#define BL_GUIDANCE_HOVERSLAM_H
#include "state.h"
#include "guidance.h"

/* Compute guidance command from (nav) state. Stateless per tick (recomputed). */
BL_HD void hoverslam_step(const State* st, GuidanceCmd* g);

/* Entry-burn supervisor (E3) predictor: forward-shoot a cheap ballistic (no-thrust) vertical
 * channel from (altitude h0, speed0, mass) to the ground, returning the PEAK dynamic pressure
 * [Pa] encountered. Predictive trigger for the 3-engine entry burn (Agent A §9) — see sim.c. */
BL_HD double entry_predict_peak_qbar(double h0, double speed0, double mass);

#endif
