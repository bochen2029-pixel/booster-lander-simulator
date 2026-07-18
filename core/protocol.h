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
 * renderer refuses to decode a mismatched TLM_VERSION. */
#define BL_PROTO_VERSION   2u

/* Packet magic tags (first 4 bytes of every frame — lets the decoder switch on
 * packet kind and reject garbage). ASCII, read as LE u32. */
#define BL_MAGIC_HELLO  0x304C4C48u /* 'HLL0' */
#define BL_MAGIC_TLM    0x304D4C54u /* 'TLM0' */
#define BL_MAGIC_EVT    0x30545645u /* 'EVT0' */
#define BL_MAGIC_STATS  0x30545453u /* 'STT0' */

/* TLM.flags bitfield */
#define BL_TLM_FLAG_SEA_ACTIVE  (1u << 0) /* deck_z/deck_quat valid          */
#define BL_TLM_FLAG_RAW_MODE    (1u << 1) /* guidance RAW parameterization    */
#define BL_TLM_FLAG_NAV_NOISY   (1u << 2) /* nav in NOISY mode                */

/* Tail element caps (canon §10.3). The decoder bounds-checks against these. */
#define BL_PLAN_MAX   64u   /* ghost-line knots per guidance tick            */
#define BL_CLOUD_MAX  128u  /* rollout terminal samples @10 Hz               */

#pragma pack(push, 1)

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
    /* --- legs (220) --- */
    float deploy_frac;     /* 220 0..1 leg deploy fraction                   */
    float stroke[4];       /* 224 per-leg crush stroke [m]                   */
    /* --- aero force for HUD/VFX (240) --- */
    float f_aero[3];       /* 240 aero force [N] (world)                     */
    /* --- ASDS deck pose (252), valid iff flags & SEA_ACTIVE --- */
    float deck_z;          /* 252 deck heave [m]                             */
    float deck_quat[4];    /* 256 deck attitude, xyzw                        */
    /* --- tail counts (272) --- */
    uint16_t plan_n;       /* 272 count of plan knots (<= BL_PLAN_MAX)       */
    uint16_t cloud_n;      /* 274 count of cloud samples (<= BL_CLOUD_MAX)   */
    /* total fixed size = 276                                                */
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
BL_STATIC_ASSERT(sizeof(BlTlmFixed)   == 276, "TLM fixed head must be 276 bytes");
BL_STATIC_ASSERT(sizeof(BlPlanKnot)   == 16,  "plan knot must be 16 bytes");
BL_STATIC_ASSERT(sizeof(BlCloudSample)== 12,  "cloud sample must be 12 bytes");
BL_STATIC_ASSERT(sizeof(BlEvt)        == 48,  "EVT must be 48 bytes");

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
BL_STATIC_ASSERT(offsetof(BlTlmFixed, deck_z)     == 252, "deck_z@252");
BL_STATIC_ASSERT(offsetof(BlTlmFixed, plan_n)     == 272, "plan_n@272");

#endif /* BOOSTER_PROTOCOL_H */
