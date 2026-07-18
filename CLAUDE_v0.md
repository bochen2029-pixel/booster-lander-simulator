# Booster Landing Simulator

A 6-DOF propulsive landing simulator where the guidance actually solves the landing
problem in real time. Native C/CUDA core does physics and guidance. Three.js in a
Tauri shell draws it. A one-way state stream connects them.

The point of this project is that **it cannot cheat**. Everything below exists to
make faking the result structurally impossible rather than a matter of discipline.

---

## 0. Prime directives

These are not preferences. Violating any of them defeats the purpose of the project.

1. **State changes only through the integrator.** The guidance layer's entire output
   is an actuator vector: throttle, two gimbal angles, four fin deflections, RCS mask.
   No code path anywhere writes position, velocity, attitude, angular velocity, or
   mass except `integrate_rk4()`.

2. **The renderer is a pure observer.** The command channel (§3.3) is a closed
   enumeration. Nothing in it can write vehicle state. If the renderer crashes, the
   simulation is unaffected.

3. **Fixed timestep, always.** `dt` is a compile-time-ish constant, never a frame
   delta, never a wall-clock delta. Real time affects only pacing, never integration.

4. **Deterministic.** Seeded RNG, no wall-clock reads inside the sim, no
   uninitialised memory, no unordered floating-point reduction on the host path.
   Same seed plus same initial conditions must produce a bit-identical trajectory.

5. **Headless must work.** The same binary, with no socket and no renderer, runs
   N randomized descents and prints a success rate. This is the artifact that proves
   the simulation is real.

6. **If guidance can't solve it, the vehicle crashes.** Do not add an assist term, a
   nudge, a soft clamp toward the pad, or a "recovery mode" that bypasses the
   controller. A crash is a valid and useful outcome.

---

## 1. Repository layout

```
core/                 C + CUDA. No graphics, no networking dependencies.
  dynamics.{h,cu}     Shared __host__ __device__ equations of motion
  integrator.{h,c}    RK4, quaternion handling
  vehicle.{h,c}       Mass properties, geometry, actuator limits
  atmosphere.{h,c}    Exponential density model
  guidance_hoverslam.{h,c}
  guidance_mppi.{h,cu}
  protocol.h          Packet structs, shared with the shell
  main.c              CLI: --serve | --headless
shell/                Tauri v2 (Rust). Spawns core as a sidecar, relays the socket.
ui/                   Three.js renderer. Read-only.
tools/                Monte Carlo analysis, plotting, trajectory replay
```

`core/` must build and run with `ui/` and `shell/` deleted.

---

## 2. The plant

### 2.1 State vector (14 elements)

| Symbol | Size | Frame | Units |
|---|---|---|---|
| `r` | 3 | world | m |
| `v` | 3 | world | m/s |
| `q` | 4 | body → world, **scalar-last (x,y,z,w)** | - |
| `w` | 3 | **body** | rad/s |
| `m` | 1 | - | kg |

World frame is right-handed, **Z up**, origin at the pad center. Scalar-last
quaternion ordering is chosen to match `THREE.Quaternion` and reduce conversion bugs.

### 2.2 Forces and torques

Compute every force with its application point, then take moments about the current
center of mass. Never apply a torque directly without a force and a lever arm.

- **Thrust.** Magnitude `throttle * T_max`, applied at the gimbal pivot `r_gimbal`
  (body frame, at the base). Direction set by two gimbal angles in the gimbal plane.
  Torque = `(r_gimbal - r_com) x F_thrust`. This offset is the entire source of pitch
  and yaw control authority.
- **Gravity.** At the center of mass. Produces no torque, by definition.
- **Aerodynamics.** Drag and lift applied at the center of pressure. On a booster
  descending engine-first, **`r_cp` is forward of `r_com`**, which makes the vehicle
  aerodynamically unstable. This is intentional and correct. It is why grid fins
  exist. Torque = `(r_cp - r_com) x F_aero`.
- **Grid fins.** Four independent deflections, each generating a force at its own
  mounting point near the top of the vehicle.
- **RCS.** Cold gas, low authority. Required for roll (see §2.5).

Atmosphere: `rho = rho0 * exp(-h / H)`, `H ~= 8500 m`. Include it in the guidance
model too, not just the plant.

### 2.3 Mass properties (recompute every step)

- `mdot = -T / (Isp * g0)`
- **Center of mass shifts** as propellant drains. Model remaining propellant as a
  cylinder whose height decreases; combine with the dry structure's fixed CoM.
- **Inertia tensor changes.** Recompute `I` each step from dry structure (thin-walled
  cylinder) plus propellant (solid cylinder of current height), with parallel axis
  transfer to the current CoM. Freezing `I` is a common shortcut that quietly makes
  the vehicle behave wrong at exactly the moment it matters.

### 2.4 Integration

RK4 at 500 Hz (`dt = 2 ms`).

- Quaternion derivative: `qdot = 0.5 * q (x) [w, 0]` (scalar-last).
- Renormalize `q` after every full step, not inside the RK4 stages.
- Rotational dynamics: from `dL/dt = tau` with `L = I w` and `I` time-varying,

  ```
  I wdot = tau - w x (I w) - Idot w
  ```

  Both the gyroscopic term `w x (I w)` and the `Idot w` term are required. Dropping
  the gyroscopic term silently degrades the 3D case into a decorated 2D one.

### 2.5 Known trap

**A single gimbaled engine cannot produce roll torque about its own thrust axis.**
Thrust passes through the gimbal pivot, so its moment arm about the roll axis is
zero regardless of gimbal deflection. Roll authority must come from RCS or
differential fin deflection. Every 2D implementation misses this; it will be the
first thing that surprises you in 3D.

### 2.6 Termination

Detect ground contact by the actual geometry (lowest point of the vehicle crossing
`z = 0`), not by clamping the CoM. On contact, evaluate and freeze:

```
landed  := |v| < 2 m/s  AND  tilt < 5 deg  AND  lateral error < 5 m  AND  |w| < 0.2 rad/s
crashed := otherwise
```

---

## 3. Process boundary

### 3.1 Transport

Localhost WebSocket, binary frames, little-endian, packed structs. Default port 8787,
configurable. Do not use JSON: at 120 Hz it wastes bandwidth and introduces
float round-tripping error into what is supposed to be an exact observation.

Bandwidth is a non-issue. The fixed telemetry record is under 150 bytes; at 120 Hz
that is well under 20 KB/s including the trajectory tail. Never stream frames.

### 3.2 Downstream: telemetry (core → renderer)

```c
struct TelemetryHeader {   // packed, little-endian
  uint32_t magic;          // 'RTLM' 0x4D4C5452
  uint32_t seq;            // monotonic; renderer detects drops
  double   t;              // sim time, s
  float    r[3];           // position, world, m
  float    v[3];           // velocity, world, m/s
  float    q[4];           // attitude, x y z w, body -> world
  float    w[3];           // angular velocity, body, rad/s
  float    mass;           // kg
  float    throttle;       // 0..1, commanded
  float    gimbal[2];      // rad
  float    fins[4];        // rad
  uint32_t rcs_mask;       // bitfield
  float    f_aero[3];      // net aero force, world (visualisation only)
  uint8_t  phase;          // 0 coast 1 aero 2 burn 3 landed 4 crashed
  uint8_t  guidance_mode;  // 0 none 1 hoverslam 2 mppi
  uint16_t flags;
  uint16_t plan_n;         // trajectory knots following
  uint16_t _pad;
};
// followed by: float plan[plan_n][3]   predicted positions, world frame
```

The `plan` tail is what makes the guidance visible. Render it as a ghost line
(§4.3).

### 3.3 Upstream: commands (renderer → core)

**This enumeration is the anti-cheat contract.** It is closed. Adding anything that
writes vehicle state defeats the project.

| Command | Payload | Notes |
|---|---|---|
| `RESET` | `seed`, `ic_preset` | Full re-init |
| `PAUSE` / `RESUME` | - | Stops advancing; does not change `dt` |
| `STEP` | `n` | Advance exactly n physics steps |
| `SET_TIMESCALE` | `float` | **Wall-clock pacing only.** Never touches `dt` |
| `SET_GUIDANCE_MODE` | `mode` | 0 none, 1 hoverslam, 2 mppi |
| `INJECT_DISTURBANCE` | `type`, `magnitude` | wind gust, thrust misalignment, mass error, sensor bias |
| `SET_INITIAL_CONDITIONS` | struct | Only valid while in reset state |

Camera state never crosses the boundary. It belongs to the renderer alone.

`INJECT_DISTURBANCE` earns its place: a scripted animation shatters when you throw
a gust at it mid-descent, while a real controller re-plans and still lands. Keep it
prominent in the UI.

### 3.4 Rates

| Loop | Rate |
|---|---|
| Physics | 500 Hz, fixed |
| Guidance | 50 Hz |
| Telemetry push | 120 Hz (decimated from physics) |
| Render | display refresh, interpolated |

---

## 4. Guidance

Implement strictly in this order. Do not start on MPPI before hoverslam lands.

### 4.1 Tier 0 — Hoverslam

Choose vehicle parameters such that **minimum-throttle thrust-to-weight exceeds 1 at
landing mass** (a real Merlin bottoms out near 40% throttle). The vehicle therefore
physically cannot hover, and the ignition point must be exact.

Solve for ignition altitude numerically: integrate backward from `(h=0, v=0)` at full
throttle, including mass depletion and drag, until the state matches current velocity.
Recompute continuously during descent; do not solve once and commit.

This tier is the plant's correctness test. If a well-solved hoverslam lands
repeatably, the dynamics are sound. If it does not, fix the plant before touching
the optimizer.

### 4.2 Tier 1 — MPPI on CUDA

Model Predictive Path Integral control. This is where the GPU genuinely earns its
place; the plant itself is microseconds per step and does not belong on a GPU.

- 8192–16384 rollouts, one CUDA thread each
- Horizon 3–5 s at a 20 ms control step (150–250 steps per rollout)
- Per-thread `curand` state, seeded deterministically from the master seed
- Sample control perturbations from a Gaussian around the previous solution
  (warm start from the shifted prior sequence)
- Cost: terminal position error, terminal velocity, terminal tilt, angular rate,
  fuel consumed, plus penalties for throttle bounds, gimbal limits, glideslope
  violation, and max dynamic pressure
- Weights `w_i = exp(-(S_i - S_min) / lambda)`, normalized; take the weighted mean
  of the control sequences
- Execute the first control, shift, repeat

**Critical:** the rollout dynamics must be *the same source* as the plant dynamics.
Mark the equations of motion `__host__ __device__` and compile them for both. If the
rollout model silently diverges from the plant, you have built a cheat with extra
steps. Once it works, deliberately introducing model mismatch becomes a legitimate
robustness test, but only after parity is established.

Export the weighted-mean rollout as the `plan[]` tail in telemetry.

### 4.3 Tier 2 — Lossless convexification (optional)

Acikmese and Blackmore's relaxation turns fuel-optimal powered descent into a
second-order cone program that provably preserves the optimal solution despite the
non-convex minimum-thrust constraint. Flight-tested by JPL as G-FOLD. Only worth it
if you want optimality guarantees; MPPI gets you a working vehicle far sooner.

---

## 5. Renderer

Tauri v2 shell, Three.js in the webview, `ui/` has zero authority over simulation state.

### 5.1 Coordinate conversion

Sim is Z-up right-handed. Three.js is Y-up right-handed. Convert **in exactly one
function**, at the packet boundary, and never anywhere else:

```
three.x =  sim.x
three.y =  sim.z
three.z = -sim.y
```

The quaternion conversion is the bug-prone part. Do not hand-derive it and hope.
Write a unit test: take a known body-frame vector, rotate it by `q` in sim space,
convert the result; separately convert `q` then rotate the converted vector. The two
must agree to floating-point tolerance.

### 5.2 Interpolation

Buffer the two most recent telemetry packets and render roughly one packet interval
in the past. Lerp position, slerp attitude. This decouples a 120 Hz stream from a
144 Hz display and hides jitter without ever touching the simulation.

### 5.3 Visuals

Target a stylized, readable aesthetic, not photorealism. There is no hardware ray
tracing available in a browser and there will not be for years, so do not design
around it.

Highest leverage, in order:

1. **The plume as a real moving light source.** A `PointLight` parented to the engine
   bell, intensity scaled by throttle, so it lights the pad and the vehicle's own
   underside as it descends. This one detail sells the scene more than everything
   else combined.
2. Plume geometry from additive-blended billboards or a GPU particle system.
3. `ACESFilmicToneMapping` plus `UnrealBloomPass` on the HDR plume core.
4. Atmospheric scattering shader driving sky color as a function of altitude.
5. Dust kick: particle burst triggered on ground proximity.
6. GTAO or SSAO, vignette, subtle film grain via `EffectComposer`.

### 5.4 HUD and instrumentation

Altitude, vertical and lateral velocity, mass, TWR at current throttle, gimbal
deflection, fuel margin, time to ignition, guidance mode, solver latency.

**The ghost line is the centerpiece.** Draw `plan[]` as a translucent line from the
vehicle to the pad. When a disturbance is injected, the plan visibly snaps to a new
solution and the vehicle chases it. That is the guidance thinking, on screen, and no
faked implementation can produce it.

---

## 6. Headless validation

```
core --headless --runs 10000 --seed 42 --out results.csv
```

No socket, no renderer, no graphics linkage. Randomize initial conditions across
altitude, velocity, lateral offset, attitude, angular rate, and mass. Report:

- Landing success rate
- Distributions: lateral error, terminal velocity, terminal tilt, fuel margin
- Failure taxonomy: fuel exhaustion, excessive terminal velocity, tipover, loss of control

A staged animation cannot produce a success rate. If this prints 950/1000 with no
renderer attached, the simulation is real by construction.

---

## 7. Build order

1. Plant, RK4, hoverslam. Headless only, CSV output, no socket, no renderer.
   **Do not proceed until tier 0 lands repeatably.**
2. Telemetry socket plus a deliberately ugly Three.js scene (a capsule and a ground
   plane). Prove the stream, the interpolation, and the coordinate conversion.
3. MPPI on CPU, single-threaded, a few hundred rollouts. Validate the cost function
   where it is easy to debug.
4. Port MPPI to CUDA. Scale rollouts. Verify host/device dynamics parity by running
   an identical rollout on both and diffing.
5. Disturbance injection and the Monte Carlo harness.
6. Aesthetics.

Aesthetics last, deliberately. It is the seductive part and it will absorb unlimited
time while hiding whether the physics underneath is correct.

---

## 8. Anti-patterns

Each of these silently makes the simulation fake. Watch for them in review.

- Lerping or easing position toward the pad
- A PD controller acting on position with no mass, inertia, or torque model
- Integrating on `requestAnimationFrame` delta
- Clamping the vehicle above ground instead of detecting impact
- An "assist" or "stabilization" term that engages when the optimizer struggles
- Freezing the inertia tensor or the center of mass
- Rollout dynamics that have drifted from plant dynamics
- Any write to vehicle state originating in `ui/` or `shell/`
- Non-deterministic reduction order in the MPPI weighted mean on the host path
- Tuning the cost function until the ghost line looks pretty rather than until the
  headless success rate improves
