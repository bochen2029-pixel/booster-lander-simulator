# PROTO report — telemetry schema extension (D-010 item 1 / D-011 addendum)

**Agent:** PROTO (opus-4.8, intercom id `yht76x7q`, lane `proto`)
**Date:** 2026-07-18 (Saturday, evening) · **Worktree:** `C:\Booster_Lander_Simulator\_proto_wt\` (gate-free spike; REAL TREE NEVER TOUCHED)
**Mission:** the pre-authorized, do-not-defer telemetry schema extension as ONE validated unit — add the D-010
diegetic predicted-impact marker fields to the one-way telemetry contract. Renderer remains a PURE OBSERVER
(D-011 hard line): fields are populated by a pure read of sim/gcmd state in the telemetry writer; no feedback
path; guidance reads nothing new.

## Fields added to `BlTlmFixed` (core/protocol.h)

| field | type | meaning |
|---|---|---|
| `pred_impact[2]` | `float` (world XY, m) | guidance's predicted impact point — the diegetic marker that converges onto the pad as the solve tightens |
| `ignite_h` | `float` (m) | aero-aware landing-burn ignition altitude |

## Version bump

`BL_PROTO_VERSION 2u → 3u` (core/protocol.h). HELLO/TLM/STATS all carry `ver`; the TS decoder rejects any
frame whose `ver != PROTO_VERSION`, so old clients fail loud against a v3 stream (intended).

## Offsets — before / after (`BlTlmFixed`, `#pragma pack(1)`)

Placement: inside the **guidance-derived** group, immediately after `dist_pad` (their semantic home). All f32,
all 4-byte aligned under pack(1) — no new padding. `sizeof(BlTlmFixed): 276 → 288` (+12 B).

| field | offset v2 | offset v3 |
|---|---:|---:|
| t_go | 212 | 212 |
| dist_pad | 216 | 216 |
| **pred_impact[2]** | — | **220** (220, 224) |
| **ignite_h** | — | **228** |
| deploy_frac | 220 | 232 |
| stroke[4] | 224 | 236 (236,240,244,248) |
| f_aero[3] | 240 | 252 (252,256,260) |
| deck_z | 252 | 264 |
| deck_quat[4] | 256 | 268 (268,272,276,280) |
| plan_n | 272 | 284 |
| cloud_n | 274 | 286 |
| **total fixed size** | **276** | **288** |

Everything at/above `dist_pad` (offset ≤ 219) is byte-stable; only the guidance-derived tail and below shift +12.

## C-side static asserts updated (core/protocol.h)

- `sizeof(BlTlmFixed) == 276` → `== 288`
- `offsetof(deck_z) == 252` → `== 264`
- `offsetof(plan_n) == 272` → `== 284`
- **added** `offsetof(pred_impact) == 220`, `offsetof(ignite_h) == 228`
- (unchanged pins verified still hold: step@8, t@16, r@32, quat@56, mass@84, throttle_cmd@112, rcs_mask@152,
  mach@160, p_chamber@176)

These are compile-time `_Static_assert`s — the Release build **succeeded**, which is the authoritative proof
that the layout matches the pins.

## Value plumbing (pure read; directive 2 / D-011 hard line)

All new computation lives in the telemetry writer path (`fill_tlm` in `core/main.c`) — guidance inputs are
unchanged, no state is written, dt/RNG untouched.

- **`pred_impact` — v1 formula (ballistic / ZEM projection):**
  ```
  tg = clamp(gcmd.t_go, 0, 60)
  pred_impact[0] = r_x + v_x * tg      // st->y[S_RX] + st->y[S_VX]*tg
  pred_impact[1] = r_y + v_y * tg      // st->y[S_RY] + st->y[S_VY]*tg
  ```
  The world-XY the current lateral state coasts to over the estimated time-to-go. **Converges onto the pad as
  the solve tightens** (t_go and v_xy shrink toward a centered touchdown) — this is the D-010 intent ("it
  actually solved it," visible from 62 km). Same semantic for GM_HOVERSLAM and GM_MPPI so the renderer has one
  consistent marker.
  **Limitation:** kinematic only — no wind, no future burn steering, no aero. It is a first-order marker, NOT
  the solver's own predicted touchdown (MPPI's internal ZEM foresight is anchored at the ignition gate, not
  streamed). A v2 could stream the planner's own terminal projection; v1 is the D-010-sanctioned "marker that
  converges."

- **`ignite_h`:** computed by the existing `compute_ignite_h(const State*)` in `guidance_mppi.c` — the
  aero-aware thrust-only suicide-burn bisection that already matches hoverslam's ignition trigger and is what
  the MPPI planner precomputes per replan (`MppiState.ignite_h`). It reads ONLY `st->y` (pure function). Exposed
  via a new public wrapper `bl_predict_ignite_h()` (decl in `guidance_mppi.h`, one-line def in
  `guidance_mppi.c`) and called from `fill_tlm`. Computed fresh from live state each frame; no feedback path.

## TypeScript mirror (ui/src/net/decode.ts) + tests

- `PROTO_VERSION 2 → 3`; `TLM_FIXED_SIZE 276 → 288`.
- `TlmFrame` interface: added `predImpact: [number, number]` and `igniteH: number`.
- decoder: `predImpact = [f(220), f(224)]`, `igniteH = f(228)`; shifted reads for deployFrac(232), stroke(236),
  fAero(252), deckZ(264), deckQuat(268), plan_n(284), cloud_n(286); added both fields to the returned object.
- `ui/src/net/decode.test.ts`: sentinel builder + assertions updated (every float field = its own offset), new
  `predImpact==[220,224]` / `igniteH==228` assertions, tail-count writes at 284/286, "fixed size 288".
- No other ui consumers reference these offsets/fields (grep-verified: only decode.ts + decode.test.ts).

## Files touched (all in `_proto_wt/`)

| file | change |
|---|---|
| `core/protocol.h` | version 2→3, 2 struct fields, sizeof + 3 offset asserts (1 changed pair + 2 added) |
| `core/guidance_mppi.h` | public decl `double bl_predict_ignite_h(const State*)` |
| `core/guidance_mppi.c` | one-line public wrapper over the existing `compute_ignite_h` |
| `core/main.c` | `fill_tlm`: populate `pred_impact` (r+v·t_go) and `ignite_h` |
| `ui/src/net/decode.ts` | version, size, interface, decoder offsets, returned object |
| `ui/src/net/decode.test.ts` | sentinel offsets + assertions for v3 layout |
| `goldens/protocol/{hello,tlm,evt}.hex` | re-frozen (see below) |
| `runs/ws_probe.mjs` | NEW: minimal live-wire smoke reader (Node built-ins) |

## Test results

**C side (worktree, Release, MSVC /fp:precise):**
- Build: **clean**, zero warnings — the compile-time static asserts (sizeof 288 + all offset pins) PASS.
- `--selftest`: **SELFTEST: PASS** (all 10 oracles incl. determinism memcmp).
- `--headless --scenario terminal --seed 42 --runs 200`: **194/200 = 97.0% — EXACTLY the sacred gate**, identical
  sub-counts (PERFECT 15 / GOOD 167 / HARD 12 / CRASHED 6; off-pad 6, td_v 1.93). Physics is byte-untouched — the
  change is purely additive to the telemetry writer.

**TypeScript (worktree, vitest v2.1.9, clean `pnpm install --frozen-lockfile`):**
- `vitest run`: **26/26 pass** — `decode.test.ts` (4) incl. the offset-sentinel test that now validates every
  field at its shifted offset + the two new fields at 220/228; `frame.test.ts` (22, coordinate conversion,
  unaffected). (The pre-existing copied `node_modules` was pnpm-symlink-broken by the tree copy; a fresh
  `pnpm install` in `_proto_wt/ui` fixed it — the toolchain is healthy.)

## Golden regeneration (goldens/protocol/*.hex)

**Command (the canonical, in-tree path — same fill_tlm the live server uses):**
```powershell
$exe = ".\_proto_wt\build\bin\Release\booster-core.exe"
$exe --golden      # prints "HELLO 72 <hex>" / "TLM 288 <hex>" / "EVT 48 <hex>"
# write the hex field of each line to goldens/protocol/{hello,tlm,evt}.hex (+ trailing LF, no BOM)
```
Re-baseline pre-authorized by D-010 item 1 and the D-011 addendum + the gate protocol (D-012).

| golden | v2 | v3 | note |
|---|---:|---:|---|
| hello.hex | 72 B (144 hex) | 72 B (144 hex) | `ver` field 2→3; size unchanged |
| tlm.hex | 276 B (552 hex) | **288 B (576 hex)** | +12 B for the two fields |
| evt.hex | 48 B (96 hex) | 48 B (96 hex) | BlEvt has no `ver` field → unchanged |

**Proof the frozen golden is correct** (decoded raw bytes of the on-disk tlm.hex against the v3 offsets — a
TERMINAL s42 run1 frame at step 500):
- magic `0x304d4c54` (TLM0) ✓, ver **3** ✓, size **288** ✓, phase 4 (LANDING_BURN) ✓
- r=[62.64, 11.60, 1880.29], v=[−6.18, 4.06, −185.12], t_go=10.116, dist_pad=1881.4
- **pred_impact@220,224 = [0.145, 52.645]** — independent recompute r+v·t_go = [62.64−6.18·10.116,
  11.60+4.06·10.116] = [0.12, 52.67] → **matches** (fp32). The marker is already near pad-center in X (0.14 m).
- **ignite_h@228 = 1258.6 m** — plausible high light-up for a 185 m/s arrest.
- shifted tail sane: deck_z@264=0 (SEA off), deck_quat_w@280=1 (identity), plan_n@284=0, cloud_n@286=0.
- `--golden` re-emit == on-disk tlm.hex (byte-identical, deterministic).

## Live-wire sanity (mission item 6 — no browser)

`booster-core --serve --scenario terminal --seed 42 --run 1 --port 8137` + `node runs/ws_probe.mjs 8137`
(raw RFC6455 client, Node built-ins only):
```
HELLO ok: ver=3, size=72
TLM ok: ver=3, fixed=288, plan_n=0, cloud_n=0
  t_go=11.395  pred_impact=[-6.499, 55.550]  ignite_h=1198.41
  formula check r+v*t_go=[-6.499, 55.550]  match=true
PROBE-OK: HELLO+TLM parsed with v3 fields
```
Server-side confirmed clean: HELLO + TLM frames sent, client-disconnect detected, graceful shutdown
(`verdict=NONE emitted=2 frames`). The new fields decode live and the v1 formula reproduces the wire bytes
exactly.

## What the renderer does with this (D-010 diegetic impact-marker)

The renderer — a PURE OBSERVER over the one-way TLM stream — draws a marker on the ground plane at
`pred_impact` (world XY, converted sim→three at draw time). Streamed from 62 km, it starts far from the pad and
**slides onto the pad center as the guidance solve tightens** and cross-range velocity is nulled — the visible
"it actually solved it" beat (D-010's best idea). `ignite_h` lets the renderer show/annotate the landing-burn
ignition altitude (e.g. a horizon line the vehicle falls to before the burn lights, or a HUD readout that the
approaching descent is about to reach). Both are read-only overlays: no client input ever re-enters the sim
(D-011 hard line — precompute in, telemetry out, always).

## What remains

Nothing for this unit — it is complete and validated in the worktree (C asserts + 26 vitest + golden re-freeze
proof + live wire). **Integration into the real tree is a main-session decision** (fleet protocol: agents never
edit/build the real tree; main integrates). The exact patch set is the 6 source files + 3 goldens listed above;
re-running `--golden` in the real tree after the merge reproduces the frozen hex. Recommend a D-013 ADR entry
recording the v2→v3 bump, the pred_impact v1 formula + its kinematic limitation, and the golden re-baseline.
