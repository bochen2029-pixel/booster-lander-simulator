/* protocol.h — process-boundary packet layout (canon §10, Appendix B).
 *
 * SHARED SOURCE OF TRUTH. The TypeScript decoder in ui/src/net/ is generated
 * from THIS file by tools/gen_protocol_ts.py — never hand-edit the TS mirror.
 *
 * Rules (Appendix B): #pragma pack(push,1), explicit _pad fields, little-endian,
 * static_assert(sizeof) on every packet. Canonical byte layouts frozen as hex
 * goldens (goldens/protocol/*.hex). All multi-byte fields little-endian; the sim
 * only runs on LE hosts (x86_64) — a compile guard enforces this.
 *
 * fp policy (canon §6.1, §10.3): plant integrates fp64; telemetry DOWNCASTS to
 * fp32 for every state field. The only f64 on the wire is sim time `t` (and step
 * is u64) — needed for propagation-delayed audio scheduling at 2.92 s/km where
 * fp32 seconds would quantize ~decimeter boom timing.
 *
 * Layout was chosen so that: (a) all f64/u64 land on 8-byte boundaries within the
 * struct, (b) all f32 land on 4-byte boundaries, (c) the u8/u16 status word is a
 * single contiguous 8-byte island. With pack(1) the compiler adds nothing; we get
 * the alignment for free by ordering + one explicit pad. Verified by the offset
 * static_asserts at the bottom.
 */
#ifndef BOOSTER_PROTOCOL_H
#define BOOSTER_PROTOCOL_H

#include <stdint.h>
#include <assert.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define BL_STATIC_ASSERT(c, m) _Static_assert((c), m)
#else
#  define BL_STATIC_ASSERT(c, m) typedef char bl_sa_##__LINE__[(c) ? 1 : -1]
#endif

/* LE-only guard: the wire format is little-endian and we do not byte-swap. */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#  error "protocol.h assumes a little-endian host (x86_64)."
#endif

/* Wire protocol version. Bump on ANY layout change; HELLO carries it too and the
 * renderer refuses to decode a mismatched TLM_VERSION.
 * v3 (D-010 item 1 / D-011 addendum "DO NOT DEFER THE TELEMETRY SCHEMA"): added
 * the guidance-derived pred_impact[2] + ignite_h fields to BlTlmFixed (the diegetic
 * predicted-impact marker + landing-burn ignition altitude). TS mirror + hex
 * goldens re-frozen as one unit (re-baseline pre-authorized by both ADRs).
 * v4 (N0, D-019, §10.9 — THE WIDE SOCKET, executed as ONE validated unit, the D-013 ceremony):
 * BlTlmFixed += the §8.1 TargetEstimate view (target_est_xy[2], target_est_vxy[2], target_cov[3],
 * target_src, target_valid, target_age) + EngineHealth (eng_health bitmask, eng_n) + guidance_np_ver,
 * placed after the guidance-derived group (ignite_h); every downstream offset shifts +40;
 * sizeof 288 -> 328. BlHello += module-mask bits (TARGET/ENGINE_OUT/NEURAL), world_id + world_hash,
 * np_version; sizeof 72 -> 80. The renderer draws the ESTIMATE marker (with uncertainty ellipse)
 * distinct from truth — "what the rocket believes", directive-8-honest. TS mirror + hex goldens
 * re-frozen as one unit (re-baseline pre-authorized by D-019). */
#define BL_PROTO_VERSION   4u

/* Packet magic tags (first 4 bytes of every frame — lets the decoder switch on
 * packet kind and reject garbage). ASCII, read as LE u32. */
#define BL_MAGIC_HELLO  0x304C4C48u /* 'HLL0' */
#define BL_MAGIC_TLM    0x304D4C54u /* 'TLM0' */
#define BL_MAGIC_EVT    0x30545645u /* 'EVT0' */
#define BL_MAGIC_STATS  0x30545453u /* 'STT0' */
#define BL_MAGIC_CMD    0x30444D43u /* 'CMD0' — the ONE client->server frame (Mode 2 interactive) */

/* Mode-2 INTERACTIVE COMMAND (client->server, canon §M2 — the fenced live-inject channel).
 * The renderer is a PURE OBSERVER by default; --interactive OPTS IN to this inbound channel, which
 * DELIBERATELY WAIVES determinism (wall-clock-timed injections) — the run is journaled (sim-time +
 * command to the serve log) so it can be replayed. Absent --interactive, any client data frame is
 * dropped exactly as before (byte-identical, deterministic). Params are type-specific (see below). */
#define BL_CMD_NONE       0u
#define BL_CMD_GUST       1u  /* p[0]=peak m/s (0=>default), p[1]=dir deg, p[2]=half-width m (0=>default) */
#define BL_CMD_ENGINE_OUT 2u  /* p[0]=engine (1|2 side; 0=>seeded side) — fires on the next multi-eng burn */

/* TLM.flags bitfield */
#define BL_TLM_FLAG_SEA_ACTIVE     (1u << 0) /* deck_z/deck_quat valid                       */
#define BL_TLM_FLAG_RAW_MODE       (1u << 1) /* guidance RAW parameterization                 */
#define BL_TLM_FLAG_NAV_NOISY      (1u << 2) /* nav in NOISY mode                             */
#define BL_TLM_FLAG_TARGET_MOVABLE (1u << 3) /* v4: target not pinned at origin — read target_est_xy */
#define BL_TLM_FLAG_ENGINE_OUT     (1u << 4) /* v4: an engine has failed this run (eng_health has a 0) */

/* v4 target_src provenance tag (mirrors state.h TGT_*; on the wire in BlTlmFixed.target_src). */
#define BL_TGT_SRC_FIXED     0u
#define BL_TGT_SRC_SEEDED    1u
#define BL_TGT_SRC_BEACON    2u
#define BL_TGT_SRC_PERCEIVED 3u
#define BL_TGT_SRC_DRAG      4u

/* v4 HELLO module-mask bits (mirror state.h MOD_*; the renderer learns which capabilities are armed). */
#define BL_MOD_SLOSH       (1u << 0)
#define BL_MOD_SEA         (1u << 1)
#define BL_MOD_NAV_NOISY   (1u << 2)
#define BL_MOD_FINS        (1u << 3)
#define BL_MOD_TURB        (1u << 4)
#define BL_MOD_INJECT      (1u << 5)
#define BL_MOD_TARGET      (1u << 6)  /* v4 movable target (§4.5) armed */
#define BL_MOD_ENGINE_OUT  (1u << 7)  /* v4 engine-out (§4.6) armed     */
/* MOD_NEURAL reserved for N1 (GM_NEURAL); the mask is u8 today — widen with the enum when N1 lands. */

/* Tail element caps (canon §10.3). The decoder bounds-checks against these. */
#define BL_PLAN_MAX   64u   /* ghost-line knots per guidance tick            */
#define BL_CLOUD_MAX  128u  /* rollout terminal samples @10 Hz               */

#pragma pack(push, 1)

/* ---- CMD (client->server, Mode 2 interactive) — 24 bytes, masked on the wire ----
 * The ONLY inbound frame. Fixed-size + magic-tagged like every other frame so the
 * server can reject garbage. Honored ONLY under --interactive (else dropped). */
typedef struct BlCmd {
    uint32_t magic;   /* 0  BL_MAGIC_CMD                        */
    uint16_t type;    /* 4  BL_CMD_*                            */
    uint16_t seq;     /* 6  client command sequence (journal)   */
    float    p[4];    /* 8  type-specific params                */
    /* total 24 */
} BlCmd;

/* ---- TLM @125 Hz — fixed head, then plan[] then cloud[] tails ---------------
 * Offsets in comments are the running byte offset with pack(1). Keep them exact:
 * the static_asserts below are the contract, and gen_protocol_ts.py parses this.
 */
typedef struct BlTlmFixed {
    /* --- framing (0) --- */
    uint32_t magic;        /* 0   BL_MAGIC_TLM                                */
    uint16_t ver;          /* 4   BL_PROTO_VERSION                           */
    uint16_t flags;        /* 6   BL_TLM_FLAG_*                              */
    /* --- time / sequence (8) — the only 8-byte-wide fields, kept 8-aligned - */
    uint64_t step;         /* 8   physics step index                         */
    double   t;            /* 16  sim time [s] (ONLY f64 field)              */
    uint32_t seq;          /* 24  telemetry sequence (gap detect)            */
    uint32_t _pad0;        /* 28  -> realign f32 block to 32                 */
    /* --- state (32), all fp32, world frame (canon §4.1, §6.1) --- */
    float r[3];            /* 32  position  [m]  (world, Z-up)               */
    float v[3];            /* 44  velocity  [m/s]                            */
    float quat[4];         /* 56  body->world, scalar-last xyzw              */
    float w[3];            /* 72  angular velocity [rad/s] (body)            */
    /* --- mass block (84) --- */
    float mass;            /* 84  total mass [kg]                            */
    float com_z;           /* 88  CoM height above base [m]                  */
    float I_diag[3];       /* 92  diag inertia about CoM [kg m^2]            */
    float prop_lox;        /* 104 LOX remaining [kg]                         */
    float prop_rp1;        /* 108 RP-1 remaining [kg]                        */
    /* --- actuators (112) --- */
    float throttle_cmd;    /* 112 commanded throttle [0.40..1] or 0          */
    float throttle_act;    /* 116 actual (post-lag) throttle                 */
    float gimbal_cmd[2];   /* 120 commanded gimbal [rad] (pitch,yaw)         */
    float gimbal_act[2];   /* 128 actual gimbal [rad]                        */
    float fins_act[4];     /* 136 actual fin deflections [rad]               */
    /* --- status island (152): one contiguous 8-byte block of small ints --- */
    uint16_t rcs_mask;     /* 152 8 nozzles, 1 bit each (which fired)        */
    uint8_t  n_eng;        /* 154 engines lit this frame {0,1,3}             */
    uint8_t  phase;        /* 155 BlPhase enum                               */
    uint8_t  guidance_mode;/* 156 0 none / 1 hoverslam / 2 mppi              */
    uint8_t  verdict;      /* 157 BlVerdict enum (NONE until settled)        */
    uint16_t solver_flags; /* 158 SOLVER_DEGRADED etc bitmask                */
    /* --- environment & derived (160), fp32 --- */
    float mach;            /* 160                                            */
    float qbar;            /* 164 dynamic pressure [Pa]                      */
    float alpha_total;     /* 168 total AoA [rad]                            */
    float p_amb;           /* 172 ambient pressure [Pa]  (plume p_a)         */
    float p_chamber;       /* 176 chamber pressure [Pa]  (plume p_0) *ADDED* */
    float wind_local[3];   /* 180 wind at vehicle [m/s] (world)             */
    float a_body[3];       /* 192 sensed accel [m/s^2] (body) — HUD g-meter  */
    float qdot_heat;       /* 204 stagnation heat rate [W/m^2]              */
    float Q_heat;          /* 208 integrated heat load [J/m^2] -> soot state */
    /* --- guidance-derived (212) --- */
    float t_go;            /* 212 time-to-go [s]                             */
    float dist_pad;        /* 216 slant distance to pad [m]                  */
    float pred_impact[2];  /* 220 predicted impact point, world XY [m] *v3*  */
    float ignite_h;        /* 228 landing-burn ignition altitude [m]   *v3*  */
    /* --- v4 THE WIDE SOCKET (§8.1/§10.9): the TargetEstimate view + EngineHealth. The renderer
     *     draws the ESTIMATE marker (uncertainty ellipse from target_cov) distinct from the truth
     *     deck — "what the rocket believes" (directive-8-honest). Nominal at N0 (origin/FIXED/valid,
     *     all engines healthy) but wired now so the schema never re-bumps for the policy stack. --- */
    float target_est_xy[2]; /* 232 estimated target position, world XY [m]  *v4* */
    float target_est_vxy[2];/* 240 estimated target velocity, world XY [m/s]*v4* */
    float target_cov[3];    /* 248 2x2 covariance packed xx,yy,xy [m^2]     *v4* */
    uint8_t target_src;     /* 260 BL_TGT_SRC_* provenance                  *v4* */
    uint8_t target_valid;   /* 261 0 before first acquisition               *v4* */
    uint16_t _pad1;         /* 262 -> realign target_age to 4               *v4* */
    float target_age;       /* 264 s since last target update (staleness)   *v4* */
    uint8_t eng_health;     /* 268 per-engine health BITMASK (bit i = engine i healthy) *v4* */
    uint8_t eng_n;          /* 269 engines currently firing/available (EngineHealth.n_eng) *v4* */
    uint16_t guidance_np_ver;/*270 neural-policy version (0 = none/GM!=NEURAL)*v4* */
    /* --- legs (272) --- */
    float deploy_frac;     /* 272 0..1 leg deploy fraction                   */
    float stroke[4];       /* 276 per-leg crush stroke [m]                   */
    /* --- aero force for HUD/VFX (292) --- */
    float f_aero[3];       /* 292 aero force [N] (world)                     */
    /* --- ASDS deck pose (304), valid iff flags & SEA_ACTIVE --- */
    float deck_z;          /* 304 deck heave [m]                             */
    float deck_quat[4];    /* 308 deck attitude, xyzw                        */
    /* --- tail counts (324) --- */
    uint16_t plan_n;       /* 324 count of plan knots (<= BL_PLAN_MAX)       */
    uint16_t cloud_n;      /* 326 count of cloud samples (<= BL_CLOUD_MAX)   */
    /* total fixed size = 328 (v3 was 288; +40 for the wide socket)          */
} BlTlmFixed;

/* Tail element structs (appended immediately after BlTlmFixed, tightly packed) */
typedef struct BlPlanKnot {   /* ghost line: colored by planned throttle */
    float r[3];               /* 0  world position [m]                   */
    float throttle;           /* 12 planned throttle at this knot        */
} BlPlanKnot;                 /* size 16 */

typedef struct BlCloudSample {/* possibility cloud: rollout terminal xy + weight */
    float xy[2];              /* 0  world XY of terminal state [m]       */
    float weight;             /* 8  normalized MPPI weight [0..1]        */
} BlCloudSample;              /* size 12 */

/* ---- EVT (reliable, ordered) — canon §10.4 ---- */
typedef struct BlEvt {
    uint32_t magic;   /* 0  BL_MAGIC_EVT           */
    uint16_t code;    /* 4  BlEvtCode              */
    uint16_t _pad0;   /* 6                         */
    uint64_t step;    /* 8                         */
    double   t;       /* 16                        */
    float    args[6]; /* 24 code-specific payload  */
    /* total 48 */
} BlEvt;

/* ---- HELLO — first frame the server sends after the WS handshake -------------
 * Session descriptor + the invariant vehicle/world constants the renderer needs
 * to build the scene once (mass-independent geometry, wire caps, decimation).
 * Sent exactly once; the client validates ver == BL_PROTO_VERSION before decoding
 * any TLM. Layout ordered f64/u64 first (8-aligned), then f32, then the small-int
 * island — same discipline as BlTlmFixed. *ADDED by RENDERER-STREAM track 4-C.* */
typedef struct BlHello {
    uint32_t magic;        /* 0   BL_MAGIC_HELLO                              */
    uint16_t ver;          /* 4   BL_PROTO_VERSION                           */
    uint16_t flags;        /* 6   reserved (0)                               */
    /* --- 8-byte-wide block (8) --- */
    double   t0;           /* 8   sim time at session start [s] (0.0)        */
    uint64_t seed;         /* 16  RNG seed (u32 value, widened)              */
    /* --- rates / caps (24) --- */
    float    dt;           /* 24  physics step [s] (0.002)                   */
    float    tlm_hz;       /* 28  telemetry emit rate [Hz] (125)             */
    uint32_t tlm_decim;    /* 32  steps per telemetry frame (TLM_DECIM)      */
    uint32_t run_idx;      /* 36  scenario run index                         */
    /* --- vehicle geometry (mass-independent) (40) --- */
    float    veh_len;      /* 40  total length [m]                           */
    float    veh_dia;      /* 44  diameter [m]                               */
    float    leg_span;     /* 48  deployed leg footprint diameter [m]        */
    float    pad_radius;   /* 52  landing pad radius [m]                     */
    float    deck_z;       /* 56  ground/deck height [m]                     */
    float    pc_ref;       /* 60  chamber-pressure reference [Pa]            */
    /* --- session ids / caps island (64) --- */
    uint16_t plan_max;     /* 64  BL_PLAN_MAX                                */
    uint16_t cloud_max;    /* 66  BL_CLOUD_MAX                               */
    uint8_t  scenario;     /* 68  scenario enum                              */
    uint8_t  guidance_mode;/* 69  0 none / 1 hoverslam / 2 mppi / 3 neural   */
    uint8_t  modules;      /* 70  module mask (BL_MOD_*)                     */
    uint8_t  world_id;     /* 71  v4: World id (§4.7; Earth=0)               */
    /* --- v4 world + policy provenance (72) --- */
    uint32_t world_hash;   /* 72  v4: pinned World-parameter hash (Earth pin)*/
    uint16_t np_version;   /* 76  v4: neural-policy version (0 = none)       */
    uint16_t _pad1;        /* 78  -> pad to 80                              */
    /* total 80 (v3 was 72; +8 for world id/hash + np_version)               */
} BlHello;

/* ---- STATS @~10 Hz — lightweight session/run scalars for the HUD ribbon ------
 * Peaks + margins the renderer shows without re-deriving from the TLM stream.
 * *ADDED by RENDERER-STREAM track 4-C — reconcile in real tree.* */
typedef struct BlStats {
    uint32_t magic;        /* 0   BL_MAGIC_STATS                             */
    uint16_t ver;          /* 4   BL_PROTO_VERSION                           */
    uint16_t _pad0;        /* 6                                             */
    uint64_t step;         /* 8   physics step at emit                       */
    double   t;            /* 16  sim time [s]                               */
    float    max_qbar;     /* 24  peak dynamic pressure so far [Pa]          */
    float    peak_qdot;    /* 28  peak stagnation heat rate so far [W/m^2]   */
    float    fuel_kg;      /* 32  propellant remaining (LOX+RP1) [kg]        */
    float    twr;          /* 36  current thrust/weight                      */
    float    tlm_seq;      /* 40  last telemetry seq (as float, monotonic)   */
    float    fps_emit;     /* 44  measured emit rate [Hz] (wall-clock)       */
    /* total 48                                                              */
} BlStats;

#pragma pack(pop)

/* ---- enums (kept small; values are wire-stable, append-only) ---- */
typedef enum BlPhase {
    BL_PHASE_INIT = 0, BL_PHASE_COAST, BL_PHASE_ENTRY_BURN, BL_PHASE_AERO_DESCENT,
    BL_PHASE_LANDING_BURN, BL_PHASE_TOUCHDOWN, BL_PHASE_SETTLING,
    BL_PHASE_LANDED, BL_PHASE_TIPPED, BL_PHASE_CRASHED,
    BL_PHASE_FUEL_DEPLETED, BL_PHASE_STRUCT_FAIL, BL_PHASE_THERMAL_FAIL, BL_PHASE_LOC
} BlPhase;

typedef enum BlVerdict {
    BL_VERDICT_NONE = 0, BL_VERDICT_LANDED_PERFECT, BL_VERDICT_LANDED_GOOD,
    BL_VERDICT_LANDED_HARD, BL_VERDICT_TIPPED, BL_VERDICT_CRASHED
} BlVerdict;

typedef enum BlEvtCode {
    BL_EVT_PHASE_CHANGE = 0, BL_EVT_IGNITION_CMD, BL_EVT_GREEN_FLASH,
    BL_EVT_ENGINE_START, BL_EVT_ENGINE_SHUTDOWN, BL_EVT_MACH1_CROSS /*args=r_emit[3]*/,
    BL_EVT_LEG_DEPLOY, BL_EVT_TOUCHDOWN /*args=v_impact,tilt*/, BL_EVT_VERDICT /*args[0]=grade*/,
    BL_EVT_GUST /*args=vec[3]*/, BL_EVT_FAULT /*args[0]=type*/, BL_EVT_TARGET_CHANGED /*args[0]=pad*/,
    BL_EVT_RCS_PULSE /*args[0]=mask*/, BL_EVT_SOLVER_DEGRADED
} BlEvtCode;

/* ---- size + offset contract (frozen; goldens/protocol/tlm_layout.txt) ---- */
BL_STATIC_ASSERT(sizeof(BlTlmFixed)   == 328, "TLM fixed head must be 328 bytes (v4: +40 wide socket)");
BL_STATIC_ASSERT(sizeof(BlPlanKnot)   == 16,  "plan knot must be 16 bytes");
BL_STATIC_ASSERT(sizeof(BlCloudSample)== 12,  "cloud sample must be 12 bytes");
BL_STATIC_ASSERT(sizeof(BlEvt)        == 48,  "EVT must be 48 bytes");
BL_STATIC_ASSERT(sizeof(BlHello)      == 80,  "HELLO must be 80 bytes (v4: +world+np)");
BL_STATIC_ASSERT(sizeof(BlStats)      == 48,  "STATS must be 48 bytes");
BL_STATIC_ASSERT(sizeof(BlCmd)        == 24,  "CMD must be 24 bytes (client->server, Mode 2)");

/* offsetof pins — these are what the TS decoder mirrors; a change here is an ADR */
#include <stddef.h>
BL_STATIC_ASSERT(offsetof(BlTlmFixed, step)       == 8,   "step@8");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, t)          == 16,  "t@16");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, r)          == 32,  "r@32");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, quat)       == 56,  "quat@56");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, mass)       == 84,  "mass@84");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, throttle_cmd)== 112,"throttle_cmd@112");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, rcs_mask)   == 152, "rcs_mask@152");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, mach)       == 160, "mach@160");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, p_chamber)  == 176, "p_chamber@176");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, pred_impact)== 220, "pred_impact@220"); /* v3 (unchanged) */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, ignite_h)   == 228, "ignite_h@228");    /* v3 (unchanged) */
/* v4 wide-socket pins (new group @232) */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, target_est_xy) == 232, "target_est_xy@232");   /* v4 */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, target_est_vxy)== 240, "target_est_vxy@240");  /* v4 */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, target_cov) == 248, "target_cov@248");         /* v4 */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, target_src) == 260, "target_src@260");         /* v4 */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, target_age) == 264, "target_age@264");         /* v4 */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, eng_health) == 268, "eng_health@268");         /* v4 */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, guidance_np_ver) == 270, "guidance_np_ver@270");/* v4 */
/* v4-shifted tail pins (+40) */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, deploy_frac)== 272, "deploy_frac@272");  /* was 232 */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, deck_z)     == 304, "deck_z@304");       /* was 264 */
BL_STATIC_ASSERT(offsetof(BlTlmFixed, plan_n)     == 324, "plan_n@324");       /* was 284 */
/* HELLO pins */
BL_STATIC_ASSERT(offsetof(BlHello, t0)            == 8,   "hello.t0@8");
BL_STATIC_ASSERT(offsetof(BlHello, seed)          == 16,  "hello.seed@16");
BL_STATIC_ASSERT(offsetof(BlHello, dt)            == 24,  "hello.dt@24");
BL_STATIC_ASSERT(offsetof(BlHello, veh_len)       == 40,  "hello.veh_len@40");
BL_STATIC_ASSERT(offsetof(BlHello, plan_max)      == 64,  "hello.plan_max@64");
BL_STATIC_ASSERT(offsetof(BlHello, world_id)      == 71,  "hello.world_id@71");   /* v4 */
BL_STATIC_ASSERT(offsetof(BlHello, world_hash)    == 72,  "hello.world_hash@72"); /* v4 */
BL_STATIC_ASSERT(offsetof(BlHello, np_version)    == 76,  "hello.np_version@76"); /* v4 */
/* STATS pins */
BL_STATIC_ASSERT(offsetof(BlStats, step)          == 8,   "stats.step@8");
BL_STATIC_ASSERT(offsetof(BlStats, t)             == 16,  "stats.t@16");
BL_STATIC_ASSERT(offsetof(BlStats, max_qbar)      == 24,  "stats.max_qbar@24");

#endif /* BOOSTER_PROTOCOL_H */
