"""rowformat.py — THE canonical mirror of the C teacher-tap row layout (N1, S0).

MIRRORS, field for field, core/policy_tap.h (the row) + core/policy_obs.h (the OBS_* order).
The C tap writes fixed-layout binary rows of IEEE-754 float64, little-endian, tightly packed, no
padding, NO file header. This module reads them back and names every column, so the offline trainer
(train_s0.py) and the exporter (export_weights.py) never re-derive the layout.

IF YOU CHANGE THE C LAYOUT (policy_obs.h / policy_tap.h) YOU MUST CHANGE THIS FILE IN LOCKSTEP.
The interface is FROZEN (canon App-G): a change is a re-architecture event (a new NP_VERSION + a
retrain + a re-golden), never a silent edit. NPOBS_N here must equal NP_N_IN in the exported header.

Provenance (canon §8.1): every OBS_* column is a legal quantity (nav view + atmo + legal
target/health). The three action columns are the EXECUTED command (the imitation label a*).
"""

from __future__ import annotations
import numpy as np

# ----------------------------------------------------------------------------------------------------
# The FROZEN observation-ingredient order — mirror of the OBS_* enum in core/policy_obs.h.
# ----------------------------------------------------------------------------------------------------
OBS_NAMES = [
    "r_x_rel",   # 0  target-relative horizontal offset x  (r_xy - target_xy) [m]
    "r_y_rel",   # 1  target-relative horizontal offset y                     [m]
    "h",         # 2  height above deck  h = r_z - com - deck_z               [m]
    "v_x",       # 3  world horizontal velocity x                             [m/s]
    "v_y",       # 4  world horizontal velocity y                             [m/s]
    "v_z",       # 5  world vertical velocity                                 [m/s]
    "zb2w_x",    # 6  body +Z axis in world (tilt vector) x
    "zb2w_y",    # 7  body +Z axis in world y
    "zb2w_z",    # 8  body +Z axis in world z
    "w_x",       # 9  body angular rate x                                     [rad/s]
    "w_y",       # 10 body angular rate y                                     [rad/s]
    "w_z",       # 11 body angular rate z                                     [rad/s]
    "prop",      # 12 total propellant mass (lox+rp1)                         [kg]
    "mach",      # 13 Mach = |v| / a(h)
    "qbar",      # 14 dynamic pressure 0.5*rho*|v|^2                          [Pa]
    "fins",      # 15 fins_deployed flag {0,1}
    "eng_on",    # 16 engine_on flag {0,1}
    "ign_t",     # 17 ign_timer                                              [s] (<0 when off)
    "eh0",       # 18 engine-0 chamber-P health flag {0,1}
    "eh1",       # 19 engine-1 chamber-P health flag {0,1}
    "eh2",       # 20 engine-2 chamber-P health flag {0,1}
    "relights",  # 21 relights_left
    "ign_margin",# 22 ignition-altitude margin  h - ignite_h                 [m]
    "t_vx",      # 23 target velocity estimate x                             [m/s]
    "t_vy",      # 24 target velocity estimate y                             [m/s]
    "cov_xx",    # 25 target covariance xx                                   [m^2]
    "cov_yy",    # 26 target covariance yy                                   [m^2]
    "cov_xy",    # 27 target covariance xy                                   [m^2]
    "t_age",     # 28 target staleness                                       [s]
    "t_valid",   # 29 target_valid flag {0,1}   <-- END OF THE FROZEN v1 PREFIX (NP_VERSION <= 6)
    # ---- App-G v2 (2026-07-25, the oracle-distill re-architecture): the SELF-SENSED contingency
    # channel. The honest sensor version of `eh0/eh1/eh2` — an engine-out shows up here as a thrust
    # deficit in sf_z plus an induced wdot, with NO fault flag. Measured on the first RFLY tap:
    # sf_z 52.37 -> 36.67 m/s^2 and wdot_y +0.021 -> -0.135 rad/s^2 in the failure tick.
    # These are APPENDED, never inserted: a v1 policy reads cols 0..29 and cannot tell they exist.
    "sf_x",      # 30 achieved specific force x (accelerometer), world       [m/s^2]
    "sf_y",      # 31 achieved specific force y                              [m/s^2]
    "sf_z",      # 32 achieved specific force z                              [m/s^2]
    "wdot_x",    # 33 body angular acceleration x                            [rad/s^2]
    "wdot_y",    # 34 body angular acceleration y                            [rad/s^2]
    "wdot_z",    # 35 body angular acceleration z                            [rad/s^2]
    "a_lat0_prev",  # 36 LAST executed lateral command x                     [m/s^2]
    "a_lat1_prev",  # 37 LAST executed lateral command y                     [m/s^2]
    "thr_prev",     # 38 LAST executed throttle
]
NPOBS_N = len(OBS_NAMES)           # == 39 ; must equal core/policy_obs.h NPOBS_N (and be >= NP_N_IN)
assert NPOBS_N == 39, "OBS layout drift vs policy_obs.h"
NPOBS_N_V1 = 30                    # the frozen v1 prefix — what NP_VERSION 6 consumes

# ----------------------------------------------------------------------------------------------------
# Full row layout — mirror of core/policy_tap.h.  [ meta(3) | obs(NPOBS_N) | action(3) ]
# ----------------------------------------------------------------------------------------------------
META_NAMES   = ["t", "seed", "run"]          # cols 0,1,2
ACTION_NAMES = ["a_lat0", "a_lat1", "throttle"]  # the EXECUTED command a*  (the imitation LABEL)
# ---- TEACHER CONTEXT (2026-07-25): the GM_RFLY theta flown at this tick. PRIVILEGED — the CEM chose
# it by flying candidates through the real plant with the actual disturbance realization, i.e. by
# seeing the future. It is the training TARGET for the theta-prior net (the CEM warm-start), and it
# must NEVER enter the student policy's input (canon §8.1 provenance / directive 6). It lives past the
# action columns so `split()` — which returns only obs and act — cannot reach it by accident.
# All-zero == "not an RFLY row" (a real theta is never all-zero).
THETA_NAMES  = ["rt_ekr", "rt_ekv", "rt_ebank", "rt_adecel", "rt_tlead",
                "rt_kdiv", "rt_kvnear", "rt_ignm", "rt_tgtlead", "rt_kv"]
N_THETA      = len(THETA_NAMES)               # == RFLY_N_THETA (core/guidance_rfly.h)

COLS         = META_NAMES + OBS_NAMES + ACTION_NAMES + THETA_NAMES
ROW_N        = len(COLS)                      # 3 + 39 + 3 + 10 = 55 doubles
ROW_BYTES    = ROW_N * 8                      # 440 bytes/row
assert ROW_N == 55 and ROW_BYTES == 440, "row layout drift vs policy_tap.h"

# The v1 row width, for the width-detecting guard in read_rows. A dataset farmed before the v2
# widening is 36 f64 = 288 B/row; mixing the two silently would shear every column, so we DETECT
# and REFUSE rather than reshape into garbage.
ROW_N_V1     = 3 + NPOBS_N_V1 + 3             # 36 doubles = 288 bytes/row

# Column-index constants (handy for slicing).
COL = {name: i for i, name in enumerate(COLS)}
I_T, I_SEED, I_RUN = 0, 1, 2
OBS_SLICE    = slice(3, 3 + NPOBS_N)                          # cols 3 .. 41
ACT_SLICE    = slice(3 + NPOBS_N, 3 + NPOBS_N + 3)            # cols 42 .. 44
THETA_SLICE  = slice(3 + NPOBS_N + 3, 3 + NPOBS_N + 3 + N_THETA)   # cols 45 .. 54 (PRIVILEGED)


def read_rows(path: str) -> np.ndarray:
    """Read one .bin tap file -> float64 array of shape (n_rows, ROW_N).

    Validates the file size is an exact multiple of ROW_BYTES (no partial/truncated rows — a clean
    tap always closes on a whole row). Raises ValueError otherwise.

    ALSO detects a v1-width (288 B/row) dataset and refuses it by name. This matters because 288
    and 360 share no common factor issue that would make the mistake loud on its own: a v1 file
    whose row count happens to divide by 45 would reshape into silently sheared columns, and the
    trainer would learn from garbage without ever erroring. Refuse explicitly.
    """
    raw = np.fromfile(path, dtype="<f8")   # little-endian float64
    if raw.size % ROW_N != 0:
        if raw.size % ROW_N_V1 == 0:
            raise ValueError(
                f"{path}: this is a v1-socket dataset ({ROW_N_V1} f64 = {ROW_N_V1*8} B/row, "
                f"NPOBS_N=30). The socket was widened to NPOBS_N={NPOBS_N} on 2026-07-25 "
                f"(App-G v2, the self-sensed contingency channel), so this file must be re-farmed "
                f"against the current binary. Mixing widths shears every column silently."
            )
        raise ValueError(
            f"{path}: {raw.size*8} bytes is not a multiple of ROW_BYTES={ROW_BYTES} "
            f"(row is {ROW_N} f64). Truncated/corrupt tap file?"
        )
    return raw.reshape(-1, ROW_N)


def split(rows: np.ndarray):
    """(rows) -> (obs[n, NPOBS_N], act[n, 3])  — the STUDENT trainer's X, Y.

    Deliberately does NOT return theta: the student must never see privileged teacher context.
    Use theta_of() explicitly, and only from the theta-prior trainer.
    """
    return rows[:, OBS_SLICE].copy(), rows[:, ACT_SLICE].copy()


def theta_of(rows: np.ndarray) -> np.ndarray:
    """(rows) -> theta[n, N_THETA] — the PRIVILEGED teacher context (the CEM's per-scenario tuning).

    Only the theta-prior net trains on this, and only as a TARGET (obs -> theta_hat), never as an
    input to the student policy. Rows whose theta is all-zero were not flown by GM_RFLY.
    """
    return rows[:, THETA_SLICE].copy()


def seeds_in(rows: np.ndarray) -> np.ndarray:
    """The distinct seeds present (col 1). Used to ENFORCE THE HELD-OUT LAW (§13.6.3)."""
    return np.unique(rows[:, I_SEED].astype(np.int64))


def runs_in(rows: np.ndarray) -> np.ndarray:
    """The distinct (seed, run) pairs present — the train/val split is BY RUN, not by row."""
    key = rows[:, [I_SEED, I_RUN]].astype(np.int64)
    return np.unique(key, axis=0)


if __name__ == "__main__":
    # Tiny CLI: `python rowformat.py <file.bin>` -> summary + sanity (the Gate-(c) round-trip probe).
    import sys, os
    if len(sys.argv) < 2:
        print(f"row layout: {ROW_N} f64 = {ROW_BYTES} B/row  | obs={NPOBS_N} | cols={COLS}")
        sys.exit(0)
    p = sys.argv[1]
    rows = read_rows(p)
    obs, act = split(rows)
    print(f"file        : {p}  ({os.path.getsize(p)} bytes)")
    print(f"rows        : {rows.shape[0]}  (== bytes/{ROW_BYTES})")
    print(f"cols        : {rows.shape[1]}  (expect {ROW_N})")
    print(f"seeds       : {seeds_in(rows).tolist()}")
    print(f"runs(s,r)   : {runs_in(rows).shape[0]} distinct")
    print(f"t range     : [{rows[:,I_T].min():.3f}, {rows[:,I_T].max():.3f}] s")
    print(f"obs finite  : {np.isfinite(obs).all()}")
    print(f"act finite  : {np.isfinite(act).all()}")
    print(f"a_lat range : [{act[:,0].min():.3f},{act[:,0].max():.3f}] x  [{act[:,1].min():.3f},{act[:,1].max():.3f}] y")
    print(f"throttle rng: [{act[:,2].min():.3f}, {act[:,2].max():.3f}]  (0 or [ENG_THR_MIN..1])")
    # spot-check a couple named obs columns are physical
    print(f"h range     : [{obs[:,OBS_NAMES.index('h')].min():.1f}, {obs[:,OBS_NAMES.index('h')].max():.1f}] m")
    print(f"eng_health  : eh0 unique {np.unique(obs[:,OBS_NAMES.index('eh0')]).tolist()}")
    # App-G v2 self-sensed channel — the honest engine-out signature
    print(f"sf_z range  : [{obs[:,OBS_NAMES.index('sf_z')].min():.2f}, {obs[:,OBS_NAMES.index('sf_z')].max():.2f}] m/s^2")
    print(f"wdot_y range: [{obs[:,OBS_NAMES.index('wdot_y')].min():.3f}, {obs[:,OBS_NAMES.index('wdot_y')].max():.3f}] rad/s^2")
    # teacher context (privileged)
    th = theta_of(rows)
    n_rfly = int((np.abs(th).sum(axis=1) > 0).sum())
    print(f"theta rows  : {n_rfly}/{rows.shape[0]} carry GM_RFLY teacher context")
    if n_rfly:
        m = th[np.abs(th).sum(axis=1) > 0]
        print(f"theta means : " + ", ".join(f"{n}={v:.2f}" for n, v in zip(THETA_NAMES, m.mean(axis=0))))
