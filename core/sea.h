/* sea.h — SEA module: deterministic ASDS droneship deck motion (canon §4.4 / target_sandbox_design §A.1).
 *
 * A Pierson-Moskowitz sea state driving the deck's HEAVE (+ a small pitch/roll and a slow horizontal
 * station-keeping wander), realized as a PURE closed-form sum-of-sines: there is NO time integration, so
 * an identical (seed, Hs, t) yields a bit-identical pose — replay-safe by construction, even cleaner than
 * the wind (whose Dryden AR(1) part carries filter memory; the deck does not). Built once in sea_init,
 * evaluated per step. Default OFF (MOD_SEA unset) => never evaluated => byte-identical to a static pad
 * (canon directive 9; the TERMINAL/AERO/ENTRY gates never arm SEA).
 *
 * Stage-1b (D-035) wires HEAVE deck_z(t) + the heave-rate deck_vz(t) into the contact PLANT (§A.2): a
 * touchdown on a RISING deck loads the legs harder (closing faster), on a FALLING deck softer — leg crush
 * and tipover become sea-phase-dependent, the honest new physics. Pitch/roll (deck_quat) + the ±wander are
 * computed here for the renderer/HELLO (§11.9) and later stages; Stage-1b keeps the contact normal vertical
 * and the deck horizontally fixed (tilted-normal contact + ±3 m wander are the Stage-1c / SEA-polish work). */
#ifndef BL_SEA_H
#define BL_SEA_H
#include <stdint.h>

#define SEA_N 48   /* spectrum components (canon App-A.3: 48, equal-energy binned) */

typedef struct {
    double omega[SEA_N];   /* component angular frequencies [rad/s] (equal-energy quantiles of P-M) */
    double amp[SEA_N];     /* per-component heave amplitude [m] (equal-energy => constant = Hs/sqrt(8N)) */
    double phase[SEA_N];   /* seeded phases [rad] (Philox RNG_SEA) */
    double x0, y0;         /* mean deck station (world) [m] — the horizontal target center */
    double wander_omega[2], wander_amp[2], wander_phase[2];  /* slow ±wander station-keeping drift */
    double Hs;             /* significant wave height [m] — the operator-facing sea-state knob */
} SeaState;

/* Build the 48-component table from (seed, Hs). Hs is clamped to a small positive floor. wander_amp is the
 * ±amplitude [m] of the slow horizontal station-keeping drift (Stage-1c, §4.4): 0 => the deck is fixed at
 * the origin (Stage-1b heave-only, byte-identical); >0 => 2 seeded slow components (period ~40–80 s) so the
 * deck drifts within ±wander_amp and the guidance tracks target_xy(t_now) (§A.4 Option-i, horizontal). */
void sea_init(SeaState* S, uint32_t seed, double Hs, double wander_amp);

/* Pure deck pose at absolute sim time t (closed-form sum; NO integration). Any out-ptr may be NULL.
 *   deck_z  = Σ amp[k]·cos(omega[k]·t + phase[k])                 heave [m]
 *   deck_vz = −Σ amp[k]·omega[k]·sin(omega[k]·t + phase[k])       heave rate [m/s] (deck-relative leg loads)
 *   deck_quat[4] (xyzw) = small pitch/roll from the wave slope (renderer/HELLO; not fed to contact in 1b)
 *   target_x/y = (x0,y0) + slow wander sum                        horizontal deck station (world) [m] */
void sea_deck_pose(const SeaState* S, double t,
                   double* deck_z, double* deck_vz,
                   double deck_quat[4],
                   double* target_x, double* target_y);

#endif /* BL_SEA_H */
