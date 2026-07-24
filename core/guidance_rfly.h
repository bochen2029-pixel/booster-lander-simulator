/* guidance_rfly.h — GM_RFLY: the OPTIMIZER-IN-THE-LOOP over the NATIVE reactive stack (D-040 pivot).
 *
 * PROVENANCE. The direct port of the LODESTAR sandbox 3-phase law (GM_CFLY) is an honest NULL on
 * the main-tree plant: it fuel-starves even the CLEAN entry while the native hoverslam stack lands
 * the same cases with 2-3.3 t of margin (Phase-A data: runs/D040_draft.md, runs/cfly_diag2.txt).
 * The sandbox 16/16 certified the SEARCH ARCHITECTURE — oracle-strength solve at t=0, warm CEM
 * replans in flight — not that particular law. GM_RFLY keeps the architecture and aims it at the
 * law that IS fuel-feasible here: the entry-supervisor + hoverslam reactive stack, whose compound
 * failure mode is a LATERAL 35 m soft/upright miss — precisely a gain-tuning problem.
 *
 * THETA = 10 interpretable per-scenario overrides of the stack's frozen gains, MULTIPLIERS around
 * identity (RT_TGTLEAD is additive around 0). Identity theta == the shipped D-030 stack EXACTLY,
 * and elitism seeds candidate 0 with the warm start, so the t=0 population CONTAINS plain
 * hoverslam: the search can only match or beat the baseline, never regress it.
 *
 * CONTRACTS (mirrors guidance_cfly.h):
 *  - Default OFF (GM_RFLY unarmed) => rt_on stays 0 everywhere => every legacy mode byte-identical.
 *  - Deterministic: plant RNG pure-per-(seed,step); the CEM sampler is a seeded xorshift => replays.
 *  - Candidates: full-Sim copy, noreplan=1, fly the REAL plant to the horizon; cost arbitrates
 *    (sandbox-faithful cost_of — no fuel term).
 *  - The theta flows to the law through GuidanceCmd.rt (OpenMP-safe per-candidate). */
#ifndef BL_GUIDANCE_RFLY_H
#define BL_GUIDANCE_RFLY_H

struct Sim;

#define RFLY_N_THETA 10
enum {
    RT_EKR    = 0,  /* × entry-divert KR (after the D-030 n_eng<3 re-auth)          */
    RT_EKV    = 1,  /* × entry-divert KV                                            */
    RT_EBANK  = 2,  /* × entry-divert bank cap (absolute ceiling 45 deg)            */
    RT_ADECEL = 3,  /* × A_DECEL       — divert deceleration profile aggressiveness */
    RT_TLEAD  = 4,  /* × T_LEAD        — cross-range reversal pre-emption           */
    RT_KDIV   = 5,  /* × Kvel schedule — divert seek/brake gain (fins-deployed)     */
    RT_KVNEAR = 6,  /* × KVEL_NEAR     — near-ground velocity-null boost            */
    RT_IGNM   = 7,  /* × LANDING_IGNITE_MARGIN — landing-burn ignition timing       */
    RT_TGTLEAD= 8,  /* + target_vxy lead ON THE SEEK term (identity 0; the D-038
                     *   redemption: lead the vdes seek — fades with lat_scale —
                     *   never the damping, which is what nulled D-038)             */
    RT_KV     = 9,  /* × Kv            — vertical profile tracking gain             */
};

typedef struct {
    double th[RFLY_N_THETA];
    double next_replan_t;   /* 0 => the big t=0 solve fires on the first gtick */
    int    noreplan;        /* candidates never replan (no recursion) */
} RflyState;

#define RFLY_REPLAN_DT 10.0

void rfly_init(struct Sim* s);
void rfly_replan(struct Sim* s, int big);

#endif
