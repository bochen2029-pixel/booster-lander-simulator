/* nav.h — measurement layer (the "estimation honesty" upgrade). CLAUDE_v1.md §8.1.
 *
 * Canon §8: plant state ──► nav.c (measurement layer) ──► guidance (50 Hz) ──►
 *           attitude+allocation (500 Hz) ──► actuator commands ──► plant
 *
 * The measurement layer sits between the true plant State and guidance. Two modes:
 *
 *   NAV_TRUTH  (default): PASS-THROUGH. The nav view is a byte copy of truth. This
 *              path is BIT-IDENTICAL to the pre-nav code (the hard acceptance gate:
 *              selftest memcmp + every scenario's success rate reproduced exactly).
 *
 *   NAV_NOISY  (MOD_NAV_NOISY / --nav-noisy): guidance sees a NOISY estimate of the
 *              vehicle's kinematic state. Per canon §8.1:
 *                position σ = [0.5, 0.5, 0.3] m   (world x,y,z)
 *                velocity σ = 0.1 m/s             (each world axis)
 *                attitude σ = 0.1°                (small-angle body tilt, all 3 axes)
 *                gyro bias  = random-walk on the body angular rate (ω)
 *              INJECT_DISTURBANCE(sensor_bias) acts HERE — on the measurement, never on
 *              truth (v0 left this undefined; §8.1 says it matters). See nav.c.
 *
 * DETERMINISM (sacred, same contract as rng/mppi): every noise draw comes from the
 * Philox RNG_NAV stream (rng.h) keyed by (master_seed, RNG_NAV) with the counter formed
 * from (step, run) and per-field lanes. No wall-clock, no hidden state beyond the single
 * explicit NavState the caller owns. Same (seed, run, step) => bit-identical nav view =>
 * replayable, parity-safe.
 *
 * TIMING (§8.2): the nav view is produced ONCE PER GUIDANCE TICK (50 Hz), NOT at the
 * 500 Hz physics rate. All guidance layers (hoverslam, MPPI, the entry supervisor +
 * entry_divert_step, and the sim.c wind-rejection integral) consume the nav view for
 * that tick instead of raw truth.
 *
 * WHAT IS PERTURBED vs WHAT PASSES THROUGH (design decision, per §8.1 intent):
 *   PERTURBED (the "sensed" kinematic estimate — what a real NAV solution measures):
 *     y[S_RX..S_RZ]  world position
 *     y[S_VX..S_VZ]  world velocity
 *     y[S_QX..S_QW]  attitude quaternion (small-angle body rotation applied)
 *     y[S_WX..S_WZ]  body angular rate (gyro output = truth + random-walk bias)
 *   PASS-THROUGH (truth — the vehicle's OWN bookkeeping, not an external measurement):
 *     masses (S_MLOX,S_MRP1), actuator states (S_THR,S_G*,S_F*), slosh, heat,
 *     engine_on / n_eng / ign_timer / ada / relights_left, deploy_frac / deploy_cmd,
 *     crush, N2, wind_filt, t / step / phase / verdict / fault / fins_deployed.
 *   Rationale: throttle/gimbal/fin/mass/ignition state are commanded-and-integrated
 *   internal quantities the flight computer already knows exactly; only the vehicle's
 *   pose and motion through the world are ESTIMATED and therefore noisy.
 */
#ifndef BL_NAV_H
#define BL_NAV_H

#include <stdint.h>
#include "state.h"
#include "vmath.h"

/* Nav mode. NAV_TRUTH is the default and MUST be pass-through (bit-identical). */
enum { NAV_TRUTH = 0, NAV_NOISY = 1 };

/* Persistent measurement-layer state. The ONLY hidden state the nav layer owns.
 * Zero-initialized at run start (memset in sim_init, alongside the rest of Sim).
 * Holds the integrated gyro-bias random walk (the one term that is NOT a memoryless
 * per-tick draw — a bias walks, canon §8.1 "gyro bias random-walk"). Everything else
 * is regenerated per tick from Philox, so replay needs nothing but (seed, run, step). */
typedef struct {
    int      mode;          /* NAV_TRUTH | NAV_NOISY (set from module mask at init) */
    uint32_t seed;          /* master seed (mirrors Sim.seed) */
    uint32_t run;           /* run index (for per-run replayable draws) */
    int      inject;        /* MOD_INJECT active => add the sensor-bias term (§8.1) */
    double   gyro_bias[3];  /* integrated gyro-bias random walk, body axes [rad/s] */
    int      inited;        /* 0 until nav_init runs */
} NavState;

/* Initialize the measurement layer for a run. mode from the module mask
 * (MOD_NAV_NOISY -> NAV_NOISY, else NAV_TRUTH); inject from MOD_INJECT. Zeroes the
 * gyro-bias walk. seed/run key every subsequent Philox draw. */
void nav_init(NavState* nav, int module_mask, uint32_t seed, uint32_t run);

/* Produce the nav view for the current guidance tick. Reads TRUTH `st`, writes a full
 * State copy into `out` whose r/v/q/w fields are the measured estimate and whose every
 * other field is truth (pass-through). Advances the gyro-bias random walk by one
 * guidance step when NAV_NOISY. In NAV_TRUTH this is exactly `*out = *st` (memcpy) with
 * no RNG draws and no bias update — provably transparent.
 *
 * Call ONCE per 50 Hz guidance tick, before any guidance layer runs. `step` is the
 * current sim step counter (st->step); it keys the per-tick Philox counter so the draw
 * is unique per tick and replayable. */
void nav_measure(NavState* nav, const State* st, uint64_t step, State* out);

/* Re-sync the PASS-THROUGH (non-perturbed) fields of an already-measured view `out` from
 * live truth `st`, WITHOUT re-drawing noise or re-perturbing the kinematics. Use this
 * after any within-tick code that mutates plant bookkeeping between measuring the view and
 * the guidance layer that consumes it — e.g. the entry supervisor's CUT, which flips
 * engine_on / phase / ign_timer on truth mid-tick. The perturbed kinematic estimate
 * (y[S_RX..S_WZ]) is preserved exactly; everything else is refreshed to truth so guidance
 * never reads a stale engine/phase snapshot. No RNG, no bias update — pure field copy;
 * in NAV_TRUTH it is a full memcpy (still a byte copy of truth). */
void nav_resync(const State* st, State* out);

#endif /* BL_NAV_H */
