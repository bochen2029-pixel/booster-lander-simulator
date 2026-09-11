"""
D-047 — THE BEST CONSTANT THETA: how far does the REFLEX TIER go with no search and no privilege?

WHY THIS EXISTS
---------------
D-046 measured GM_RFLY at 180/180 on the engine-out frontier, but the search is a PRIVILEGED
oracle (it flies the true realization) and costs 76 s/flight. Everything deployable sits at
4-9/60. The gap was read as "the compound is search-necessary".

But the repo has never asked the cheap question: what is the BEST FIXED theta? D-042 built
--rfly-fixed purely as an ablation and tested only identity and a mean -- never an optimized
constant. A constant theta is a genuinely deployable controller:

    * zero inference        - it is ten numbers in a header
    * zero privilege        - it needs no knowledge of the fault, present or future
    * zero latency          - 0.39 s/run measured, vs 76 s for the CEM (195x)
    * zero new architecture - it IS the reactive stack; theta only re-gains D-030's own knobs

Hand probes on seed 42 before writing this (60 draws each, ~23 s):
    identity [1,1,1,1,1,1,1,1,0,1]            ->  9/60   (== the known reactive+D-030 baseline)
    D-046 run-0's converged theta, frozen     ->  9/60   (reproduces D-042: a converged theta
                                                          held constant is no better than identity)
    aggressive divert [3,2,2.5,...]           ->  5/60   WORSE
    divert knobs at the box ceiling [4,4,3]   -> 13/60   BETTER

So the landscape has headroom AND is non-monotone -- partway hurts, all the way helps. That is
exactly the shape that rewards a search and defeats hand-tuning, and it is why four ADRs of
hand-picked multipliers (D-030) plateaued where they did.

THE HELD-OUT LAW IS THE WHOLE DESIGN HERE
-----------------------------------------
Seeds 42/7/99 are EVAL-ONLY and never appear in training data. Optimizing a constant on seed 42
and then reporting seed 42 would be training on the test set -- the single easiest way to
manufacture a fake win in this repo. So the CEM optimizes on TRAINING seeds only; the winner is
then flown on 42/7/99 exactly once, and that held-out number is the only one that may be quoted.

PRE-REGISTERED READS (declared before the run)
----------------------------------------------
    held-out <= 13/60   the ceiling probe was luck; the reflex tier is genuinely capped near the
                        D-030 plateau and constant-theta is a dead end. Report as a null.
    ~15-25/60           real headroom. A ten-number, zero-privilege controller roughly doubles
                        to triples the deployable rate. Ships as a new default gain vector.
    > 25/60             the reflex tier was leaving most of its value on the table for seven
                        weeks, and "the compound is search-necessary" needs re-scoping: much of
                        what the search was buying was reachable by a constant all along.

Whatever it returns is reported with its denominator and its training/held-out split, including
the gap between them -- the overfit is part of the result, not a footnote.

HONEST BOUND: a constant theta cannot exceed the search, because the search's population
CONTAINS every constant (elitism slot 0 is the identity warm start). This measures how much of
GM_RFLY's advantage came from ADAPTING theta versus from simply HAVING a better theta.

Runs single-threaded on purpose: it is designed to share a box with the 16-core d046b CEM batch
and cost it ~6% rather than half.
"""
import json, os, random, subprocess, sys, time

EXE   = r"C:/Booster_Lander_Simulator/build/bin/Release/booster-core.exe"
OUTD  = r"C:/Booster_Lander_Simulator/runs/d047"
LOG   = os.path.join(OUTD, "evals.jsonl")      # append-only: a crash leaves every evaluation
BEST  = os.path.join(OUTD, "best.json")

# the box, copied from guidance_rfly.c:21-22 (RT_LO / RT_HI)
RT_LO = [0.30, 0.30, 0.60, 0.40, 0.25, 0.40, 0.50, 0.40, 0.00, 0.50]
RT_HI = [4.00, 4.00, 3.00, 2.50, 3.00, 2.50, 2.50, 3.50, 1.50, 2.50]
NAMES = ["EKR", "EKV", "EBANK", "ADECEL", "TLEAD", "KDIV", "KVNEAR", "IGNM", "TGTLEAD", "KV"]
IDENT = [1, 1, 1, 1, 1, 1, 1, 1, 0, 1]

TRAIN_SEEDS = [5000, 5001]        # 5xxx/6xxx/7xxx are the training band; 42/7/99 are eval-only
HELD_OUT    = [42, 7, 99]
RUNS        = 60

POP, ELITE, ITERS = 24, 6, 8
RNG = random.Random(20260911)     # fixed: the search is reproducible


def score(theta, seeds, runs=RUNS):
    """Total landed across seeds. One subprocess per seed; ~23 s each, single-threaded."""
    total, detail = 0, {}
    for s in seeds:
        cmd = [EXE, "--headless", "--scenario", "entry", "--seed", str(s), "--runs", str(runs),
               "--rfly", "--rfly-fixed", ",".join(f"{v:.4f}" for v in theta),
               "--engine-out", "random"]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
        except subprocess.TimeoutExpired:
            return None, {}                      # a hang is not a zero -- refuse to score it
        landed = None
        for line in p.stdout.splitlines():
            if line.startswith("LANDED:"):
                landed = int(line.split()[1].split("/")[0])
                break
        if landed is None:
            return None, {}                      # silence is not success (the 09-09 lesson)
        total += landed
        detail[s] = landed
    return total, detail


def clamp(v, i):
    return max(RT_LO[i], min(RT_HI[i], v))


def main():
    os.makedirs(OUTD, exist_ok=True)
    t0 = time.time()

    def log(rec):
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec) + "\n")

    # seed the CEM mean at the best hand probe, not at identity: the probes already showed
    # identity is a local plateau and the box ceiling on the divert knobs is strictly better.
    mean = [4.0, 4.0, 3.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 1.0]
    sd   = [(RT_HI[i] - RT_LO[i]) * 0.30 for i in range(10)]

    gbest, gtheta = -1, list(mean)
    n_train = RUNS * len(TRAIN_SEEDS)

    print(f"D-047 constant-theta CEM  POP={POP} ELITE={ELITE} ITERS={ITERS}", flush=True)
    print(f"  train seeds {TRAIN_SEEDS} x {RUNS} = {n_train} draws/eval", flush=True)
    print(f"  held-out {HELD_OUT} flown ONCE at the end", flush=True)

    for it in range(ITERS):
        cands = []
        for p in range(POP):
            if it == 0 and p == 0:
                th = list(mean)                                  # elitism slot 0 = the warm start
            else:
                th = [clamp(RNG.gauss(mean[i], sd[i]), i) for i in range(10)]
            cands.append(th)

        scored = []
        for p, th in enumerate(cands):
            tot, det = score(th, TRAIN_SEEDS)
            if tot is None:
                log({"iter": it, "p": p, "theta": th, "FAILED": True})
                continue
            scored.append((tot, th, det))
            log({"iter": it, "p": p, "theta": th, "landed": tot, "of": n_train, "detail": det,
                 "elapsed_s": round(time.time() - t0, 1)})
            if tot > gbest:
                gbest, gtheta = tot, list(th)
                json.dump({"theta": gtheta, "train_landed": gbest, "train_of": n_train,
                           "iter": it, "elapsed_s": round(time.time() - t0, 1)},
                          open(BEST, "w"), indent=2)
                print(f"  it{it} p{p}: NEW BEST {tot}/{n_train}  {det}", flush=True)

        if not scored:
            print("  every candidate failed to score -- aborting rather than reporting zeros",
                  flush=True)
            return 2

        scored.sort(key=lambda r: -r[0])
        el = [r[1] for r in scored[:ELITE]]
        for i in range(10):
            mu = sum(t[i] for t in el) / len(el)
            var = sum((t[i] - mu) ** 2 for t in el) / len(el)
            mean[i] = mu
            sd[i] = max(var ** 0.5, (RT_HI[i] - RT_LO[i]) * 0.03)   # floor: never fully collapse
        print(f"  iter {it} done  best={scored[0][0]}/{n_train}  gbest={gbest}/{n_train}  "
              f"[{round((time.time()-t0)/60,1)} min]", flush=True)

    # ---- the held-out flight, once ----------------------------------------------------
    print("\n=== HELD-OUT (flown once, the only quotable number) ===", flush=True)
    ho_tot, ho_det = score(gtheta, HELD_OUT)
    base_tot, base_det = score(IDENT, HELD_OUT)

    result = {
        "theta": {NAMES[i]: round(gtheta[i], 4) for i in range(10)},
        "theta_vec": [round(v, 4) for v in gtheta],
        "train_landed": gbest, "train_of": n_train, "train_seeds": TRAIN_SEEDS,
        "held_out_landed": ho_tot, "held_out_of": RUNS * len(HELD_OUT),
        "held_out_detail": ho_det,
        "identity_held_out_landed": base_tot, "identity_held_out_detail": base_det,
        "minutes": round((time.time() - t0) / 60, 1),
    }
    json.dump(result, open(os.path.join(OUTD, "result.json"), "w"), indent=2)
    print(json.dumps(result, indent=2), flush=True)
    print("\nD047-CONST-DONE", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
