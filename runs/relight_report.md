# RELIGHT report — relights-3 + high-arrest engine-cut (D-013 draft)

Agent: RELIGHT (intercom `jfj7nthd`) designed + implemented; **RELIGHT-2 (intercom `bvjdq9bo`)
verified the changes are live, re-ran the A/B honestly, and completed this report** (the first
RELIGHT returned with a stale-exe A/B — see §1a). Lane `relight`. Worktree: `_rl3_wt/`
(CMakeLists+core copy, VS2022 x64, OpenMP on). Main tree untouched. Session 2026-07-18 night,
continuing D-012.

Unblocks the ENTRY fuel-out tail (D-012 addendum lever b, parked as "relight-blocked"): the
**min-throttle climb trap**. This is the deliberate, ADR-grade unit that lever asked for.

**Verdict up front: NEEDS-WORK (do NOT integrate as-is).** The changes are correct, live, and
non-regressing, but the AS-IMPLEMENTED design does NOT move any landing rate: ENTRY s42 88→88,
s7 79→79, AERO t0 220→220, all BYTE-IDENTICAL. The cut fires and re-lights, but the re-light
re-enters the identical min-throttle trap and the one-cut latch blocks a second rescue, so
fuel-out crashes become *less severe* (run 14: 93.67→55.31 m/s) but are NOT converted to landings.
The relight budget (2→3) and the cut machinery are sound and verified; the anti-cycling guard is
the limiting design choice. Full numbers + the fix direction in §4/§6.

---

## 1. The trap (why the cut is needed) — baseline run-14 trace

`entry --seed 42 --run 14 --verbose` (main tree, D-012 baseline), the fuel-marginal longest-flight seed:

- Ignites the landing burn at t≈112 s, h≈2592 m (fuel-marginal after the entry burn).
- Suicide-burn tracker arrests the descent at **t≈130 s, h≈4.4 m, vz=-9.3 m/s**.
- Then **TWR at min-throttle (ENG_THR_MIN 0.40) exceeds 1**: at t=132 s vz flips **+3.7 m/s** and the
  vehicle CLIMBS — GM_HOVERSLAM has no mid-burn shutdown, so it cannot stop pushing.
- It climbs monotonically h 4→113→249→413 m, vz building +3.7→+46 m/s at thr=0.40 the whole way,
  burning propellant it does not have (m 27686→25670 kg over the climb).
- Burns dry (m=25600 = dry mass), free-falls, **RESULT: CRASHED fault=FUEL td_v=93.67 m/s** at t≈165 s.

s42 has 2 such fuel-outs, s7 has 5 (the D-012 anatomy: op 5 / th 5 / **fuel 2** at s42).

---

## 1a. Stale-exe trap (why this report was re-run) — RELIGHT-2 verification

The first RELIGHT's "treatment" batch (`_rl3_wt/rl3_entry_s42.txt`, `rl3_entry_s7.txt`) is
**character-for-character identical to its own baseline** (`base_entry_s42.txt`, `base_entry_s7.txt`):
88/100 fuel 2 td_v 3.77 and 79/100 fuel 5 td_v 3.58, to the digit. That is the classic stale-exe
signature — a batch run against a binary without the changes (or the same binary twice). RELIGHT-2
re-verified from scratch:

- **Source audit:** all five §2 changes are present and correct in `_rl3_wt/core/{state.h,
  scenario.c, guidance_hoverslam.c, sim.c, guidance_mppi.c}` (verified line-by-line).
- **Exe identity:** the worktree exe is NOT byte-identical to main (`_rl3_wt` sha `4BFE66D2…` →
  after a clean rebuild `1E482FCB…`; main `826291CE…`) — three distinct binaries. So the *source*
  was compiled; the first RELIGHT's error was batching the A/B against the wrong exe, not a bad build.
- **Forced clean rebuild:** touched all five sources, `cmake --build … --config Release` recompiled
  guidance_hoverslam.c / guidance_mppi.c / scenario.c / sim.c (+ Generating Code + relink) — the
  build line was confirmed, not assumed.
- **Live-proof (the decisive test):** ENTRY s42 run 14 with the fresh worktree exe is **NOT
  byte-identical to baseline** — before `CRASHED FUEL td_v=93.67`, after `CRASHED FUEL td_v=55.31`.
  The changes are LIVE. (Had run 14 matched, the protocol was to STOP and debug the build.)

All A/B numbers in §4 were then produced with the freshly-rebuilt `_rl3_wt` exe (sha `1E482FCB…`)
and are honest.

---

## 2. The fix — five coordinated changes (file + line)

Plant relaxation (relights 2→3) + a guidance cut rule + shutdown latches mirroring the ignition
latch + the directive-7 MPPI rollout mirror + a one-cut-per-flight anti-cycling latch.

| # | file | change |
|---|------|--------|
| 1 | `_rl3_wt/core/state.h` (struct State, after `relights_left`) | add `int high_arrest_cut_used;` — the anti-cycling latch bit (one high-arrest cut per flight). |
| 2 | `_rl3_wt/core/scenario.c:76` | `relights_left = 2 → 3`; init `high_arrest_cut_used=0`. (memset already zeroes the new field; explicit for clarity.) |
| 3 | `_rl3_wt/core/guidance_hoverslam.c` (burning branch, after `g->engine_cmd=1`) | **HIGH-ARREST CUT**: `if(fins_deployed && !high_arrest_cut_used && relights_left>0 && h_feet>40 && vz>-0.5){ engine_cmd=0; throttle=0; return; }`. Guidance commanding engine-off (canon §9). TERMINAL is fins-stowed → never reaches here. |
| 4 | `_rl3_wt/core/sim.c` (GM_HOVERSLAM block, after ignition latch) | **SHUTDOWN LATCH**: `if(!gcmd.engine_cmd && engine_on && phase==PH_LANDING_BURN && fins_deployed){ engine_on=0; ign_timer=-1; high_arrest_cut_used=1; }`. Plant write stays in sim.c. Mirrors the ignition latch; precedent = E3 entry-burn CUT writing truth directly. |
| 4b | `_rl3_wt/core/sim.c` (GM_MPPI block, after ignition latch) | same shutdown latch mirrored (directive 7: identical plant behavior under both guidance modes). |
| 5 | `_rl3_wt/core/guidance_mppi.c` `cmd_from_u_lean` | **rollout mirror** of the cut (`hi_cut` gate, same condition) + shutdown latch inside `rollout_cost` and `warm_start_nominal` (both gated `h_feet>40` so the h<2.5 commit-to-touchdown cut is untouched). Directive 7: MPPI ranks post-cut trajectories the way they actually fly. |

**Mechanism of the fix:** at the trap, the vehicle is arrested/climbing HIGH and badly off-profile.
Cutting the engine lets it free-fall back ONTO the sqrt-decel profile; the existing aero-aware
ignition trigger (`suicide_burn_margin <= LANDING_IGNITE_MARGIN 150`) then RE-LIGHTS it low for a
short, fuel-efficient suicide burn — exactly what the un-trapped seeds already do. Needs a spare
relight → the D-013 budget of 3.

**Anti-cycling — provably terminates.** The cut fires at most ONCE per flight (the
`high_arrest_cut_used` latch, set in sim.c the same tick the plant shuts down). A second high arrest
cannot re-trigger a cut, so cut→relight cannot oscillate: after the single cut the flight either
lands on the re-light or fails as any ordinary run would. See §4 for the run-14 verbose confirmation.

---

## 3. Gates (every worktree build)

All re-run by RELIGHT-2 with the freshly-rebuilt worktree exe (sha `1E482FCB…`):

- `--selftest`: **PASS** (10 oracles incl. determinism memcmp; TWR_min=1.321, all fin-stability
  oracles green).
- TERMINAL s42/200: **194/200 byte-exact** (15 PERFECT / 167 GOOD / 12 HARD / 6 CRASHED, off-pad 6,
  td_v 1.93 — the exact D-012 signature). The cut is fins-deployed-gated; TERMINAL (fins stowed)
  is byte-identical. ✔
- **Determinism pair on ENTRY run 14: PASS** — run twice, both `RESULT: CRASHED fault=FUEL
  td_v=55.31 m/s lat=27.55 m tilt=17.76 deg fuel=0 kg t=160.1 s maxq=52517 Pa` (bit-identical). ✔
- **MPPI single-run invariance (directive 7, mandatory after the rollout-mirror change): PASS** —
  AERO s42 run 1 `--mppi` is byte-identical worktree vs main (`td_v=2.63 lat=10.48 fuel=4444
  maxq=35438`). The rollout mirror does not perturb ordinary (non-trap) trajectories. ✔

---

## 4. A/B results (full spec winds)

Baseline = main-tree D-012 exe (relights 2, no cut; sha `826291CE…`). Treatment = freshly-rebuilt
`_rl3_wt` (relights 3 + cut; sha `1E482FCB…`).

| batch | baseline | relights-3 + cut | Δ |
|---|---|---|---|
| ENTRY s42 x100 LANDED | 88 (op 5, th 5, **fuel 2**) | **88** (op 5, th 5, **fuel 2**) | **0 — byte-identical** |
| ENTRY s7  x100 LANDED | 79 (op 6, th 9, **fuel 5**) | **79** (op 6, th 9, **fuel 5**) | **0 — byte-identical** |
| TERMINAL s42 x200 | 194 | 194 (byte-exact) | 0 |
| AERO tier-0 s42 x300 (leak spot) | 220 (op 47, th 30, fuel 1) | **220** (op 47, th 30, fuel 1) | **0 — byte-identical (no regression)** |
| AERO `--mppi` s42 x60 (dir-7) | 44 (op 13, th 2, fuel 1) | **44** (op 13, th 2, fuel 1; identical means — rollout-mirror inert) | **0** |

**Landing rate is unmoved on every batch.** All summary lines match baseline to the digit
(td_v/lat/tilt/fuel means, every verdict bucket). The single-run invariance + AERO-t0 byte-identity
confirm **zero regression**: the cut fires on nothing but the fuel-out trap runs.

### Run-14 before/after (ENTRY s42)

| | baseline (relights 2) | treatment (relights 3 + cut) |
|---|---|---|
| result | CRASHED FUEL | CRASHED FUEL |
| td_v | **93.67 m/s** | **55.31 m/s** |
| terminal lat | 128.9 m | 27.6 m |
| peak climb after 1st arrest | h≈**550 m**, tilt→**69°** (tumbles) | h≈**168 m**, tilt→**21°** (stays controlled) |
| trajectory | arrest h=4.4 → min-thr climb to 550 m → tumble → too-late re-ignite → 94 m/s slam | arrest h=4.4 → min-thr climb → **CUT at t=136.5, h=44.8, vz=+11** (thr→0) → coast → **re-light t=138** → re-enters min-thr climb to 168 m → burns dry → 55 m/s |

The cut works exactly as designed: at the high arrest it commands `engine_cmd=0`, the sim.c latch
shuts the plant + sets `high_arrest_cut_used`, the vehicle free-falls back toward the profile, and
the aero-aware trigger re-lights it low. **But** at re-light the vehicle is at ~50–70 m with mass
near dry (~27 t vs 25.6 t dry), where min-throttle (0.40) still gives TWR>1 — so it climbs *again*,
and the one-cut latch (correctly, to prevent oscillation) blocks a second rescue. It burns dry and
crashes anyway, just far less violently (staying near the pad instead of tumbling 500 m downrange).

### Per-run evidence (the mechanism IS acting; the count just doesn't move)

Per-run CSV diff of ENTRY s42 x100, baseline vs treatment (`_rl3_wt/ab_entry_s42_{base,treat}.csv`)
shows **exactly two changed rows** — the two fuel-out runs — and 98 byte-identical rows:

| run | baseline td_v / lat | treatment td_v / lat |
|---|---|---|
| 14 | 93.67 m/s / 128.9 m | 55.31 m/s / 27.6 m |
| 88 | 130.07 m/s / 206.9 m | 97.79 m/s / 52.8 m |

Both stay CRASHED-FUEL (so LANDED is unchanged), but both are markedly less severe and stay near
the pad. This is a clean, perfectly-gated intervention that is simply **insufficient to convert**
the trap — not a leak and not a regression.

---

## 5. ADR draft (D-013)

**Status: DRAFT — recommend NOT ADOPTING the guidance cut as-is; the relight-budget plant change
is separately defensible but delivers no rate benefit without a better cut rule.** Recorded here as
a measured negative result in the house tradition (failures with numbers).

### 5.1 The plant change: relights 2 → 3 (igniter cartridge count)

`scenario.c` budgets `relights_left` as a **scenario constant** — how many TEA-TEB pyrophoric
igniter cartridges are loaded — not a physics limit. Real Falcon-9 boosters fly a canonical
**3-burn descent** (boostback, entry, landing) and carry TEA-TEB for each restart; RTLS profiles
routinely light three times after stage separation. Budgeting 3 cartridges is therefore a
**physically faithful relaxation**, arguably a *correction* of an under-provisioned constant rather
than a concession: the old value 2 was the tighter assumption. ENTRY spends 2 on its entry + landing
burns, leaving 1 spare — exactly enough for one high-arrest cut→relight. This part of the change is
clean and I would keep it (it also merely widens the feasible set; on its own, without a cut rule,
it changes nothing — verified implicitly, since removing the cut leaves the extra relight unused).

### 5.2 The guidance change: the high-arrest engine cut — directive-3 discussion (honest answer)

Directive 3 is "if guidance can't solve it, the vehicle crashes — no assist terms, no clamps toward
the pad." The question that matters: **is the high-arrest cut a physical-constant correction, or is
it guidance assistance?**

**Honest answer: it is legitimate guidance, NOT an assist term — and it stays within directive 3.**
The reasoning, without hedging:

- The cut commands only `engine_cmd=0` (throttle to zero). Canon §9 explicitly permits guidance to
  command the engine off — a real vehicle CAN shut down mid-burn, and a controller that never shuts
  down when it is climbing away from the pad on residual thrust is *less* physical, not more. This
  is the same class of action as the E3 entry-burn CUT already in the tree.
- It contains **no term that pulls toward the pad**: no position clamp, no lateral bias, no
  fabricated force. `a_lat` is untouched; only the throttle channel is gated off. The vehicle then
  falls under gravity + aero alone and must re-solve the landing from the profile on its own. If the
  re-solve fails, it crashes — and in fact it *does* crash (run 14/88), which is the tell that this
  is not an assist: an assist term would have rescued it. Directive 3 holds precisely because the
  cut does not save an unsolvable run; it only removes a pathological "push while climbing" that no
  correct controller would do.
- Contrast with the counter-example the D-012 addendum warned about (ENTRY_FUEL_FLOOR medicine,
  D-009): that would *reserve* fuel — a global cushion that flatters the rate without solving the
  trajectory. The cut does the opposite: it spends the trajectory honestly and simply stops
  thrusting in the wrong direction.

So the mechanism is directive-clean. **The reason not to adopt it is not principle — it is that it
does not work well enough** (§6). It would be wrong to ship a plant relaxation (relights 3) whose
only observable effect is to make two crash-severity numbers smaller.

### 5.3 Recommendation

- **HOLD** the guidance cut (do not integrate into main) pending the one-cut-latch redesign in §6.
- The relights-3 change is defensible in isolation but should ride in **with** a cut rule that
  actually converts, so the ADR that lands it can show a rate delta. Landing it alone would add an
  untested spare relight to the plant for no measured benefit.
- Keep this worktree + the CSVs as the negative-result artifact; fold the finding into `DECISIONS.md`
  as a D-013 entry ("relight-budget relaxation is sound; the one-shot high-arrest cut is
  insufficient — the trap recurs post-relight; see §6 for the multi-cut / min-throttle-at-mass
  directions").

---

## 6. Limitations & the real finding (what the A/B revealed)

**The core limitation is structural, and the A/B is what exposed it:** the min-throttle climb trap
is a **recurring** condition, and a **one-shot** cut cannot clear it.

1. **The trap recurs after re-light.** After the cut, the vehicle re-lights at ~50–70 m with mass
   near dry, where `ENG_THR_MIN 0.40` still yields TWR>1 — so it climbs again (run 14: to 168 m).
   The `high_arrest_cut_used` latch, which correctly guarantees termination (no cut→relight
   oscillation), then forbids a second cut, and the run burns dry. The anti-cycling guard and the
   fix are in direct tension: the guard is exactly what prevents the fix from working twice.

2. **Net effect on landing rate: zero.** ENTRY s42 88→88, s7 79→79, AERO t0 220→220 — all
   byte-identical. The cut fires only on the fuel-out runs (2 at s42, 5 at s7) and makes those
   crashes less severe (td_v −25 to −41%, lateral drift −74 to −79%) but does not convert a single
   one. AERO is essentially untouched (1 fuel-out in 300 / 60).

3. **The genuinely promising directions (for a follow-up study, NOT this unit):**
   - **Multi-cut, relight-limited:** drop the `high_arrest_cut_used` one-shot latch and let the cut
     fire on *each* high arrest, using `relights_left>0` as the natural terminator. With 3
     cartridges (2 spent on entry+landing) there is exactly 1 spare, so this still bounds to one
     extra cut here — meaning the real lever is **more relights AND a smarter re-light**, or:
   - **Attack the root cause, not the symptom:** the trap exists because min-throttle TWR>1 at low
     mass. A **deep-throttle / mass-aware minimum** (let ENG_THR_MIN scale down as mass approaches
     dry, if the engine model supports it) or a **later, lower re-light target** (re-light only once
     the profile is genuinely catchable, biasing the re-ignition altitude down for the post-cut
     state) would stop the climb from recurring. This is a plant + guidance co-design, ADR-grade.
   - **Terminal give-up-gracefully:** if the cut has fired and the re-light still can't catch the
     profile, a controlled minimum-tilt descent (accept the hard landing but keep it upright and
     on-pad) already emerges partially — treatment run 14 lands at 27.6 m / tilt-controlled vs the
     baseline 128.9 m tumble. Worth quantifying whether "less-severe fuel-out" has any downstream
     value (e.g. an ASDS/recovery scenario where a 55 m/s upright impact ≠ a 94 m/s tumble).

4. **Scope honesty:** RELIGHT-2's mandate was to verify + finish the A/B of the AS-IMPLEMENTED
   design, not to redesign it. The above are recorded as directions, not tested — testing them is
   the dedicated follow-up study the D-012 addendum already anticipated ("oscillation-guard study").

5. **What is solid:** determinism preserved (bit-identical pair); TERMINAL parity preserved
   (194/200 byte-exact, fins-stowed gate holds); directive-7 mirror correct (MPPI single-run
   invariance byte-identical; AERO-t0 x300 byte-identical); the cut is perfectly gated (98/100
   ENTRY runs untouched). The engineering is sound; the design is just insufficient.
