/* constants.h — every physical constant, one place. See CLAUDE_v1.md App-A / §5.
 * Provenance tags in comments: [official] [estimate] [community] [chosen] [derived].
 */
#ifndef BL_CONSTANTS_H
#define BL_CONSTANTS_H

/* World / environment */
#define G0            9.80665         /* m/s^2 [official] */
#define R_EARTH       6356766.0       /* m, geopotential radius [official] */
#define R_AIR         287.053         /* J/(kg K) [official] */
#define GAMMA_AIR     1.4
#define RHO0          1.225           /* kg/m^3 sea level [official] */
#define P0_ATM        101325.0        /* Pa [official] */
/* N0 (§4.7): Earth is World #0; its parameter set is §4.1-4.3. The World abstraction (§4.7) is a
 * forward-pointer (Mars etc. arrive at N4); today HELLO carries a PINNED Earth id+hash so a replay
 * is attributable to the world that flew it. [chosen] pin — a real per-world FNV hash lands with §4.7. */
#define WORLD_EARTH_ID   0u
#define WORLD_EARTH_HASH 0x4EA27408u   /* [chosen] pinned Earth-World hash (World #0) */

/* N0 (§4.6): the 3-engine cluster geometry the surviving-centroid math needs — center engine on the
 * axis + a symmetric side pair on a ring of radius ENG_RING_R (engineout_design §2.1). Side-out
 * moves the survivor centroid to −R/2 (induced torque); center-out keeps it on-axis (thrust loss
 * only). [chosen, representative] — the octaweb outer ring sits ~2/3 out. Sensitivity is linear. */
#define ENG_RING_R    (0.6*VEH_RADIUS)  /* m, side-engine ring radius [chosen, representative] */

/* Integration */
#define DT            0.002           /* s, 500 Hz fixed [directive 3] */
#define GUIDANCE_DT   0.020           /* s, 50 Hz */
#define TLM_DECIM     4               /* telemetry every 4th step -> 125 Hz */

/* Engine (Merlin-1D class, per engine) — consistency triple is source of truth */
#define ENG_T_SL      845000.0        /* N sea-level 100% [official] */
#define ENG_ISP_SL    282.0           /* s [official] */
#define ENG_ISP_VAC   311.0           /* s [official] */
#define ENG_MDOT_100  (ENG_T_SL/(G0*ENG_ISP_SL))   /* 305.6 kg/s [derived] */
#define ENG_T_VAC     (ENG_MDOT_100*G0*ENG_ISP_VAC)/* 932 kN [derived] */
#define ENG_AE        ((ENG_T_VAC-ENG_T_SL)/P0_ATM)/* 0.859 m^2 [derived] */
#define ENG_THR_MIN   0.40            /* min throttle [official-ish] */
#define ENG_GIMBAL_MAX (5.0*0.017453292519943295)  /* rad, +-5 deg [community] */
#define ENG_GIMBAL_RATE (15.0*0.017453292519943295) /* rad/s [chosen] */
#define ENG_GIMBAL_ACC (60.0*0.017453292519943295)  /* rad/s^2 [chosen] */
#define ENG_THR_RATE  2.0             /* throttle/s slew [chosen] */
#define ENG_THR_TAU   0.10            /* s first-order lag [chosen] */
#define ENG_IGN_GREEN 0.30            /* s: TEA-TEB flash after cmd [chosen] */
#define ENG_IGN_T0    0.50            /* s: thrust starts rising [chosen] */
#define ENG_IGN_T1    1.50            /* s: reaches commanded [chosen] */
#define ENG_SHUT_TAU  0.15            /* s shutdown decay [chosen] */

/* Vehicle structure (KESTREL-9) */
#define VEH_LEN       47.7            /* m total [official] */
#define VEH_STAGE_LEN 41.2            /* m [official] */
#define VEH_DIA       3.66            /* m [official] */
#define VEH_RADIUS    (VEH_DIA*0.5)
#define VEH_AREF      10.52           /* m^2 reference area [derived] */
#define VEH_DRY       25600.0        /* kg incl legs+fins [estimate,pinned] */
#define VEH_DRY_COMZ  12.4           /* m above base [chosen] */

/* Propellant / tanks (two-column model) */
#define LOX_MAX       287400.0        /* kg [community] */
#define RP1_MAX       123500.0        /* kg [community] */
#define LOX_RHO       1220.0          /* kg/m^3 subcooled [chosen] */
#define RP1_RHO       833.0           /* kg/m^3 [chosen] */
#define TANK_AREA     9.9             /* m^2 column cross-section [chosen] */
#define LOX_BASE_Z    16.0            /* m, column base above vehicle base [chosen] */
#define RP1_BASE_Z    1.6             /* m [chosen] */
#define MIX_RATIO     2.33            /* LOX:RP1 by mass [community] */

/* Grid fins */
#define FIN_COUNT     4
#define FIN_Z         45.0            /* m body height of fin mounts [chosen] */
#define FIN_AREA      2.4             /* m^2 per fin [chosen] */
#define FIN_DEFL_MAX  (20.0*0.017453292519943295)  /* rad */
#define FIN_RATE      (20.0*0.017453292519943295)  /* rad/s */
#define FIN_CNA       3.0             /* /rad subsonic [chosen] */
#define FIN_STALL     (25.0*0.017453292519943295)
#define FIN_CT_DELTA_FRAC 0.35        /* tangential(roll)-cant coeff as frac of CNa_f [Agent A, chosen] */

/* RCS cold gas */
#define RCS_Z         40.5            /* m body height [chosen] */
#define RCS_FORCE     500.0          /* N per nozzle [estimate] */
#define RCS_ISP       68.0           /* s [estimate] */
#define RCS_N2        300.0          /* kg budget [chosen] */
#define RCS_ARM       1.83           /* m lateral moment arm (=radius) */

/* Landing legs */
#define LEG_COUNT     4
#define LEG_SPAN      18.0           /* m footprint diameter [official] */
#define LEG_RADIUS    (LEG_SPAN*0.5)
#define LEG_DEPLOY_H  250.0          /* m: command deploy at/below [chosen] */
#define LEG_DEPLOY_T  1.2            /* s sweep [chosen] */
#define LEG_K         3.0e6          /* N/m spring [chosen] */
#define LEG_C         1.2e5          /* N s/m damper [chosen] */
#define LEG_MU_S      0.6
#define LEG_MU_K      0.45
#define LEG_CRUSH_F   4.0e5          /* N plateau load [chosen] */
#define LEG_CRUSH_S   0.40           /* m stroke [chosen] */
#define LEG_FOOT_Z    0.0            /* feet at base plane when deployed */

/* Body aerodynamic pitch/yaw damping (Cmq, strip theory): tau = -0.5*rho*V*Cdc*D*J*w_perp.
 * Cdc is the effective crossflow damping coefficient; 0.6 gives ζ~0.1-0.15 (moderate, real
 * slender-body range) — enough to tame the aero pendulum without over-resisting maneuvers. */
#define BODY_CMQ_CDC  0.6

/* Plume / engine chamber (for telemetry plume model; not used by the plant) */
#define PC_REF        9.7e6          /* Pa chamber pressure ref [chosen] — p_chamber = throttle_act*PC_REF */

/* Budgets / failure lines */
#define QBAR_MAX      80000.0        /* Pa [estimate] */
#define HEAT_RN       1.83           /* m nose radius */
#define HEAT_K        1.7415e-4      /* Sutton-Graves Earth [official] */
#define HEAT_QDOT_MAX 300000.0       /* W/m^2 sustained [chosen] */

/* Pads */
#define PAD_RADIUS    26.0           /* m RTLS [chosen] */

/* Verdict thresholds (see §7.3) */
#define TD_V_PERFECT  2.0
#define TD_V_GOOD     4.0
#define TD_V_HARD     6.0
#define TD_LAT_PERFECT 2.0
#define TD_LAT_GOOD   10.0
#define TD_TILT_PERFECT (1.0*0.017453292519943295)
#define TD_TILT_GOOD  (3.0*0.017453292519943295)

/* Guidance targets */
#define TD_V_TARGET   1.5            /* m/s desired touchdown speed */
#define H_BIAS        2.0            /* m aim point above ground */

#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232
#define PI      3.141592653589793

#endif /* BL_CONSTANTS_H */
