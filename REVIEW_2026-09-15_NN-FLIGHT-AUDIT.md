# AUDIT — the NN-flight roadmap and the root-cause report, checked against disk

*Written 2026-09-15 by Claude Opus 5, read-only, at Bo Chen's direction. Reviews
`ROADMAP_NN-FLIGHT_2026-09-15.md` (this repo) and `C:\ONE\ONE_BOOSTER_NN-ROOT-CAUSE_2026-09-15_S3.md`,
both authored the same day by Claude Fable 5.1 from the worktree `C:\bl_e1` at HEAD `99c7f65`.
Nothing was run and nothing was changed to produce this file; every claim below was checked against
files on disk. The point of the audit is the gap between what those two documents assert and what
has a receipt.*

---

## 1 · The diagnosis holds, and it overturns D-053 — which was mine

The root-cause report's three corpus measurements are well designed, and the first is the one that
carries the argument:

**At t = 0 the label is not a function of the state.** Every run starts from the same scenario up to
dispersion, so t = 0 is the one observation all runs share. There the gain spread *across runs*
equals the spread across the whole corpus (ratios 0.85–1.06 on all ten coordinates, both the
compound and the clean corpus), and a five-fold held-out ridge from the t = 0 observation to the
t = 0 gains gives **R² between −0.19 and +0.12 on every coordinate**. Not weak signal — no signal,
at the decision that sets the entry gains, where D-029 puts ~¾ of compound losses.

**The teacher's gains never move when the engine dies — 192 of 192 runs.** The next change comes a
median 12.8 s later, on the periodic clock. The July corpus therefore taught every student to hold
its gains through an engine loss. That is exactly the behaviour **D-052 later proved was worth +19
draws to remove** (158/180 → 177/180 on event replan). The corpus trained the anti-lesson.

### D-053 was wrong, and the error is causal, not numerical

D-053 (2026-09-11, mine) matched k-nearest neighbours in the full 39-D observation space, measured
θ dispersion among matched neighbours at **0.295** of global, and concluded *"the labels are well
determined; regression was well posed."* The report's objection is correct:

> **The state at time t is a consequence of the gains over [0, t].** Matching rows on downstream
> state therefore partially matches them on the label itself. D-053 conditioned on a
> post-treatment variable and read the resulting agreement as evidence that the observation
> determines the label — the causal arrow reversed.

The t = 0 test is the clean control precisely because there is no history to have shaped it, and it
returns **1.0, not 0.295**. Repeating D-053's own neighbour measurement on a stride-8 subsample
gives 0.44 full-observation, 0.36 without the accelerometer block, 0.45 without the last executed
command — so the effect is real, is not a command-channel artifact, and **is not the quantity that
matters.**

**A consequence nobody has drawn.** D-053 is what retired the generative / multi-sample
(flow-matching) direction from PLAN.md §5. If the label is genuinely an arbitrary draw from a wide
valley whose mean is identity — the one point that crashes — then that **is** the multimodal-target
condition, and the argument used to close that door does not hold. The report's own preferred fixes
(remove the arbitrariness at the source, or learn the *judgment* instead of the *choice*) are
better than modelling the ambiguity, so the direction stays closed **on merit rather than on
D-053's measurement**. PLAN.md §5's stated reason should be corrected.

**What survives from D-053 untouched:** the seven constant observation channels (`FINS`, `EH0`,
`COVXX/YY/XY`, `TAGE`, `TVALID`), five of them a consequence of the fed target.

---

## 2 · The receipt audit — two headline numbers are prose, not data

Checked every claim in the roadmap's §0 ladder against `D:\bl_e1_data`:

| claim | receipt | status |
|---|---|---|
| E6 cold 1/32 = **179/180**, warm = 170/180 | `e6/{cold,warm}_s{42,7,99}.csv` | ✅ **reproduces to the row** (verdict enum `V_NONE=0`, so landed = 1\|2\|3; 60/59/60 and 57/55/58 exactly) |
| flags-off byte-identity | `gate_terminal_e7c.txt` | ✅ **byte-identical to `runs/n0main_terminal.txt`**; `gate/off*.csv` all one hash |
| seven default-off flags, nothing committed | `git -C C:\bl_e1 diff` | ✅ exactly seven, five files, detached at `99c7f65` |
| E4 champion **129/180** | `es/champion.json` holds `{"val":0.486,"landed":125,"iter":49}` | ❌ **validation only — no held-out receipt** |
| E5 champion **129/180** | `es_mlp/champion.json` holds `{"val":0.501,"landed":126,"iter":19}` | ❌ **validation only — no held-out receipt** |
| E7 pilot 16/60, six-seed **52/180** | `e7/critic_pilot_*.err` carry `[rfly_critic …]` prediction lines only | ❌ **no `LANDED:` line exists in any file under `e7/` or `gate/`** |

`e4_es_policy.py:174` and `e5_es_mlp.py:182` `print()` the held-out flight to **stdout**, and the
runs were launched with only stderr redirected. The numbers are almost certainly real — they were
read off a live terminal — but they are **not reconstructable from disk**, and the ladder is being
quoted as though they are.

**Why this one matters most.** The roadmap's three "facts that decide the plan" are receipted
unevenly: fact 1 (labels unlearnable) is strongly receipted from the corpus; fact 3 is half
receipted (E6 yes, E7 no); **fact 2 — "a feedforward map plateaus near 72% no matter how it is
trained, reached twice" — is the load-bearing justification for *build a critic, not a policy*, and
it is the one with no receipt.** Both champion weight vectors are on disk; re-flying them is
minutes.

---

## 3 · Two unsealed results nobody has reconciled

| | rate | PERFECT | cost | receipt |
|---|---|---|---|---|
| **E6 cold, joint 1/32 + event replan** | **179/180 = 99.4%** | 24 | ~1 s/flight | ✅ csvs |
| **D-057 `iters_0.2` + event replan** | **180/180 = 100%** | **104** | — | ✅ `runs/d057_budget_sweep.txt` |
| D-054, the current deployable row | 175/180 (sealed 585/600 = 97.5%) | 27 | 6.3 s/flight | ✅ |

Both beat the row the project currently quotes, on the same development pool, at less compute. They
were found by two different sessions on two different paths and **have never been put side by
side.** `iters_0.2` carries **four times the PERFECT count** of the deployable row — a precision
difference, not a rate difference, and precision is the axis the budget was supposed to buy.

Neither has been flown on a sealed band. The roadmap files this under §6 housekeeping; it belongs
higher, because it may change what "deployable" means before anything is built on top of it.

---

## 4 · D-057 was killed, not finished — and not by the box

6 of 9 arms flown, then `iters_0.3 s42` died at 59.7 min: **0-byte stdout, no error text, no
`LANDED:` line.** The `.err` tail stops mid-replan at t = 20.5 s. I checked the Windows System log
for 2026-09-13 09:30–12:00 (IDs 1, 41, 42, 107, 109, 506, 507, 6008, 12, 13): **no power or
shutdown event in that window.** So this was not the 09-12 kernel-shutdown pattern; the cause is
unrecorded.

Nothing has run since 2026-09-13 11:16. Matched pair **B (`pop_0.5` vs `iters_0.5`) was never
flown**, so the sweep cannot yet answer its own pre-registered question. The script *is* resumable
(`d057_budget_sweep.ps1:78` tests for a `LANDED:` line before re-flying), so relaunching costs only
the missing arms.

---

## 5 · Two framing notes on the roadmap

**The critic is not needed for deployment, and the roadmap should say so in §0.** The deployable
requirement is ~0.5 s/replan against a 0.1 Hz outer loop — already ~20× margin, and E6's 1/32 is
another 4× beyond that. What the critic buys is the *scientific* answer the operator asked for
(*can a network fly this*) plus the asynchronous cockpit. §5 half-says this — *"the method is the
result"* — while §0 frames it as the flight computer. Both are true; conflating them is how a
research goal quietly becomes a requirement.

**§2's propose–rank–confirm is the strongest part of the plan and should not be traded away.** A
search optimising against a learned critic will find the critic's errors; confirming the top two
candidates with real plant rollouts *at events only* keeps the plant's guarantee exactly where the
flight is decided, at a cost of two rollouts. Note that §1.3's first flight deliberately runs the
critic **alone** at budget 1.0 — the arm most exposed to that failure — which is the right
experiment to run, provided the result is read as a measurement of exploitation and not of the
method.

**On §5's plant-epoch ordering:** defensible, and correctly left as the operator's call. The
tension worth naming is that a critic trained on a plant carrying about half its true low-Mach drag
(D-060) learns a judgment about the wrong world; the counter — that the rerun is cheap once the
pipeline exists, and that how much transfers is itself a finding — is good.

---

## 6 · Recommended order, differing from §8 only at the front

1. **Fly the 1/32 arm and `iters_0.2` on a fresh sealed band, side by side** (half a day). They are
   receipted, they beat the quoted row, and one of them may *be* the deployable row.
2. **Close the receipt gap** — re-fly the E4 and E5 champions from `champion.json`, capturing
   stdout (minutes). Fact 2 should not carry the plan un-receipted.
3. Relaunch D-057 for the three missing arms, including matched pair B.
4. Then phase 1 as written. Its §1.1 data design is the real work and the roadmap has it right.

Housekeeping the roadmap §6 lists is all correct: the seven flags merge as one commit **after** the
gate battery is re-run in the main tree, and that is the operator's word to give, not mine.

---

*Files checked: `D:\bl_e1_data\{es,es_mlp,e6,e7,gate,pilot}`, `C:\bl_e1` (git diff, five files),
`runs/d057_budget_sweep.{txt,ps1}`, `runs/d057/`, `runs/d055_sealed_verify.txt`,
`runs/n0main_terminal.txt`, the Windows System log, and both reviewed documents. Worktrees
`bl_opus_oracle` and `condescending-mcnulty` are July-era, clean, and unrelated.*
