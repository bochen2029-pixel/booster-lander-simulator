"""
D-050 — A LEARNED CONDITIONAL POLICY, TRAINED ON OUTCOMES INSTEAD OF LABELS.

    theta = clamp( b + W . phi(legal nav state) )      70 parameters

THE POINT. Every learned artifact this repo has produced was trained by REGRESSION onto a
privileged teacher's labels, and all three failed: pi 0/12 (Phase 3, five DAgger rounds), theta-hat
2/12 on the compound, the CEM warm-start negative. The ROADMAP's Phase 5 -- RL fine-tune, gated
"ONLY if imitation plateaus short of the frontier" -- has had its precondition satisfied since
July and was never run.

D-047 showed WHY regression was the wrong tool, with a number: eight IDENTICAL flights flip
outcome from a 3.5% gain change (EKV 3.859 -> 4.000, same seed, same 60 draws, 33 -> 41 landed).
The sim is deterministic given (theta, seed, run), so that is not sampling noise -- it is a
genuinely knife-edged response surface. Least-squares onto the conditional mean of a discontinuous
target is ill-posed by construction, and no amount of capacity or data fixes it. That is the
Phase-3 wall, stated as a property of the objective rather than of the student.

Optimizing ACTUAL LANDING OUTCOME is indifferent to that discontinuity. It needs no labels, no
teacher, and -- decisively -- NO PRIVILEGE: phi reads six §4.3-legal observables from the NAV
view, so unlike the CEM (whose candidates fly the true realization, D-046) this is legal to
deploy. It is also 70 parameters rather than theta-hat's 39,434, which is the difference between
searchable at 0.39 s/flight and not.

WHAT IT IS A TEST OF. D-047 put an unconditional constant at ~34/60 honest. A constant is exactly
this policy with W = 0, and the warm start sets W = 0 with b = D-047's winner, held in elitism
slot 0 -- so D-050 can only match-or-beat it. The question is therefore sharp and pre-registered:

    does ADAPTING theta to the state beat simply HAVING a good constant,
    when neither is allowed to see the future?

That is the same question D-046's blind-teacher arm asks of the search, asked of a policy.

PRE-REGISTERED READS (before the run)
  no better than the constant  -> adaptation buys nothing a good constant does not already buy at
                                 this feature set. Report as a null. It would ALSO mean the
                                 estate's three learned nulls were never about learning: the
                                 constant was the whole ceiling and identity was the whole gap.
  better, held-out             -> conditioning pays, the feature set is the right one, and scaling
                                 from 70 params toward a real net is justified rather than assumed.
  better in-sample only        -> D-047's winner's curse recurring one level up with 7x the
                                 parameters. Expected if it happens, and reported as overfit.

TWO VARIANCE FIXES, both from outside review, both cheap and both absent from D-047:
  * COMMON RANDOM NUMBERS. Every candidate in a generation is scored on the IDENTICAL seed set,
    so comparisons are paired and the between-candidate seed variance drops out entirely. The sim
    is deterministic given (theta, seed, run), so this makes a within-generation difference a
    real difference rather than a draw difference.
  * ROTATING SEED SETS. The pair changes every generation from a pool, so the search cannot
    memorise one set's knife edges -- which is the mechanism behind D-047's measured -19% drop
    from in-sample to fresh seeds. Paired WITHIN a generation, fresh ACROSS generations.

OBJECTIVE: the weighted verdict (PERFECT 3, GOOD 2, HARD 1) plus the TD_ANCHOR gate, not raw
LANDED. An ordinal objective is already far softer than a binary one against a knife edge, and
the anchor stops rate being bought at the edge of the envelope (D-047's leader arrived at max
td_v 5.83 against a 6.0 crash threshold).

HELD-OUT: seeds 42/7/99 are NOT touched here. Note the standing contamination -- seed 42 was read
by hand during D-047's probes, so it is reported separately and never as a clean held-out number.
"""
import json, os, random, subprocess, sys, time

EXE  = r"C:/Booster_Lander_Simulator/build2/bin/Release/booster-core.exe"
OUTD = r"C:/Booster_Lander_Simulator/runs/d050"
LOG, BEST = os.path.join(OUTD, "evals.jsonl"), os.path.join(OUTD, "best.json")

RT_LO = [0.30, 0.30, 0.60, 0.40, 0.25, 0.40, 0.50, 0.40, 0.00, 0.50]
RT_HI = [4.00, 4.00, 3.00, 2.50, 3.00, 2.50, 2.50, 3.50, 1.50, 2.50]
NF = 6                                  # bias + 6 weights per output = 7 per output, 70 total
SEED_POOL = [5000, 5001, 5002, 5003, 5004, 5005]
SEEDS_PER_GEN, RUNS = 2, 60
POP, ELITE, ITERS = 30, 8, 14
W_PERFECT, W_GOOD, W_HARD = 3, 2, 1
TD_ANCHOR = 5.8
RNG = random.Random(20260911)


def score(vec70, seeds):
    """Weighted verdict across seeds. Every candidate in a generation gets the SAME seeds (CRN)."""
    total, detail = 0, {}
    csv = ",".join(f"{v:.6f}" for v in vec70)
    for s in seeds:
        cmd = [EXE, "--headless", "--scenario", "entry", "--seed", str(s), "--runs", str(RUNS),
               "--rfly", "--rfly-policy", csv, "--engine-out", "random"]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
        except subprocess.TimeoutExpired:
            return None, {}
        landed = weighted = None
        split, max_td = {}, 0.0
        for line in p.stdout.splitlines():
            if line.startswith("LANDED:"):
                landed = int(line.split()[1].split("/")[0])
            if "PERFECT" in line and "GOOD" in line and "HARD" in line:
                t = line.split()
                split = {t[i]: int(t[i + 1]) for i in range(0, len(t) - 1, 2)}
                weighted = (W_PERFECT * split.get("PERFECT", 0) + W_GOOD * split.get("GOOD", 0)
                            + W_HARD * split.get("HARD", 0))
            if "landed means" in line and "(max" in line:
                try:    max_td = float(line.split("(max")[1].split(")")[0])
                except (ValueError, IndexError): max_td = 0.0
        if landed is None or weighted is None:
            return None, {}                       # silence is not success
        if max_td > TD_ANCHOR:                    # the anchor is a gate, not a term
            detail[s] = {"w": 0, "landed": landed, "max_td_v": max_td, "ANCHOR_FAIL": True}
            return 0, detail
        total += weighted
        detail[s] = {"w": weighted, "landed": landed, "max_td_v": max_td,
                     "P": split.get("PERFECT", 0), "G": split.get("GOOD", 0),
                     "H": split.get("HARD", 0)}
    return total, detail


def main():
    os.makedirs(OUTD, exist_ok=True)
    t0 = time.time()

    # warm start: bias = D-047's winner, ALL WEIGHTS ZERO => exactly the unconditional constant.
    b0 = [4.0, 4.0, 3.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 1.0]
    for p in ("runs/d047/result.json", "runs/d047/best.json"):
        if os.path.exists(p):
            d = json.load(open(p))
            t = d.get("theta_vec") or d.get("theta")
            if isinstance(t, list) and len(t) == 10:
                b0 = t
                print(f"  warm start bias = D-047 winner ({p}); all weights 0 => the constant",
                      flush=True)
            break

    mean = []
    for o in range(10):
        mean.append(b0[o])
        mean.extend([0.0] * NF)
    # bias sd scaled to its own box; weight sd smaller -- phi is O(1), so a weight of 0.3 already
    # moves theta by ~0.3 across the state range, which is a large excursion for these gains.
    sd = []
    for o in range(10):
        sd.append((RT_HI[o] - RT_LO[o]) * 0.15)
        sd.extend([0.25] * NF)

    gbest, gvec, gseeds = -1, list(mean), None
    wmax = RUNS * SEEDS_PER_GEN * W_PERFECT

    def log(rec):
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec) + "\n")

    print(f"D-050 conditional policy  POP={POP} ITERS={ITERS}  70 params  wmax={wmax}", flush=True)
    print(f"  CRN within a generation; seed pair ROTATES across generations from {SEED_POOL}",
          flush=True)

    for it in range(ITERS):
        seeds = [SEED_POOL[(it * SEEDS_PER_GEN + k) % len(SEED_POOL)]
                 for k in range(SEEDS_PER_GEN)]
        cands = [list(mean) if (it == 0 and p == 0) else
                 [RNG.gauss(mean[i], sd[i]) for i in range(70)]
                 for p in range(POP)]
        scored = []
        for p, v in enumerate(cands):
            tot, det = score(v, seeds)
            if tot is None:
                log({"iter": it, "p": p, "FAILED": True}); continue
            scored.append((tot, v))
            landed = sum(d.get("landed", 0) for d in det.values())
            log({"iter": it, "p": p, "vec": v, "weighted": tot, "of": wmax,
                 "landed": landed, "seeds": seeds, "detail": det,
                 "elapsed_s": round(time.time() - t0, 1)})
            if tot > gbest:
                gbest, gvec, gseeds = tot, list(v), list(seeds)
                json.dump({"vec70": [round(x, 6) for x in gvec], "weighted": gbest, "of": wmax,
                           "landed": landed, "iter": it, "seeds": gseeds, "detail": det},
                          open(BEST, "w"), indent=2)
                print(f"  it{it} p{p}: NEW BEST w={tot}/{wmax} landed={landed}/"
                      f"{RUNS*SEEDS_PER_GEN} seeds={seeds}", flush=True)
        if not scored:
            print("  every candidate failed to score -- aborting", flush=True); return 2
        scored.sort(key=lambda r: -r[0])
        el = [r[1] for r in scored[:ELITE]]
        for i in range(70):
            mu = sum(t[i] for t in el) / len(el)
            var = sum((t[i] - mu) ** 2 for t in el) / len(el)
            mean[i] = mu
            floor = (RT_HI[i // 7] - RT_LO[i // 7]) * 0.03 if i % 7 == 0 else 0.04
            sd[i] = max(var ** 0.5, floor)
        print(f"  iter {it} [{seeds}]: best={scored[0][0]}/{wmax} gbest={gbest}/{wmax} "
              f"[{round((time.time()-t0)/60,1)} min]", flush=True)

    print("\nD050-DONE — winner in runs/d050/best.json", flush=True)
    print("  NOTE: gbest was scored on ONE rotating seed pair; re-score it on the full pool "
          "before any held-out flight (D-047's winner's curse was -19%).", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
