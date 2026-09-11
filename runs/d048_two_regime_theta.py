"""
D-048 (①d) — TWO CONSTANT THETAS, SWITCHED ON THE OBSERVED ENGINE COUNT.

WHY, in one line: D-047 forces ONE constant to serve two regimes that want opposite things.

D-047's structure, visible by its seventh evaluation: the leader pushes the DIVERT family
(EKR/EKV/EBANK) to the ceiling while pinning the DAMPING family (TLEAD/KDIV/KVNEAR/KV) to the
floor. Maximum lateral authority, minimum self-fighting. That is what an engine-out divert wants.
It is very likely the WRONG thing for undisturbed flight, and a single constant cannot say so --
it must compromise, and it pays for the compromise on the clean batteries. (Registered before
d047_verify ran: the D-047 winner will REGRESS on clean ENTRY and clean AERO.)

The fix is the cheapest conditioning available, and it is not learning:

    theta = (n_eng < 3) ? theta_eo : theta_healthy

n_eng is the SS4.3-LEGAL sensed firing count -- the identical quantity D-030 already switches its
bank cap on, and it rides the legal socket as OBS_EH0/EH1/EH2. So this needs:

    no inference      it is a branch on an observed flag, not a prediction
    no privilege      nothing about the FUTURE fault is used; only that one has already fired
    no latency        still a header of numbers; 0.39 s/run, vs 76 s for the CEM
    no net            twenty numbers instead of ten

This is the "reflex tier with search-settable parameters" made concrete: the reflex owns the
switch, the search owns the gains on either side of it, and the search can always fall back to
the reflex because identity stays in the population.

DESIGN
------
20-D CEM. theta_healthy warm-started at IDENTITY (do no harm to clean flight); theta_eo warm-
started at D-047's winner (start where the evidence already points). Elitism slot 0 is that pair,
so D-048 can only match-or-beat D-047 on the training objective by construction.

Trained on seeds 5000/5001 only. Held-out 42/7/99 flown ONCE, by d047_verify.py, on the final
winner -- never on intermediates, because repeatedly evaluating held-out and keeping the best IS
selection on the test set.

PRE-REGISTERED READS (before the run)
  no better than D-047 on train  ->  the compromise was cheap; one constant was enough, and the
                                     clean regression (if any) is the honest price. Report as a null.
  better on train AND clean holds ->  conditioning on an observed flag recovers what the single
                                     constant had to give away. Ships as a 20-number controller.
  better on EO, clean still down  ->  the damping floor is doing the work in BOTH regimes and the
                                     switch is not the axis; go to phase-scheduling (①e) instead.

HONEST BOUND, same as D-047: neither can beat the search, whose population contains every constant.
This measures how much of GM_RFLY's edge was ADAPTING theta versus HAVING the right two thetas.
"""
import json, os, random, subprocess, sys, time

EXE  = r"C:/Booster_Lander_Simulator/build/bin/Release/booster-core.exe"
OUTD = r"C:/Booster_Lander_Simulator/runs/d048"
LOG  = os.path.join(OUTD, "evals.jsonl")
BEST = os.path.join(OUTD, "best.json")

RT_LO = [0.30, 0.30, 0.60, 0.40, 0.25, 0.40, 0.50, 0.40, 0.00, 0.50]
RT_HI = [4.00, 4.00, 3.00, 2.50, 3.00, 2.50, 2.50, 3.50, 1.50, 2.50]
IDENT = [1, 1, 1, 1, 1, 1, 1, 1, 0, 1]

TRAIN_SEEDS = [5000, 5001]
RUNS = 60
POP, ELITE, ITERS = 28, 7, 8
RNG = random.Random(20260911)


# D-047's quality caveat, answered here. Raw LANDED counts HARD, and optimizing it drove the
# D-047 leader to 35/60 with 0 PERFECT / 8 GOOD / 27 HARD, mean td_v 4.09 m/s against a 6.0 crash
# threshold and 11.37 m off centre -- scrapes, not landings, parked on the verdict boundary.
# So D-048 optimizes a WEIGHTED verdict instead: a PERFECT landing is worth three HARD ones, and
# the search can no longer buy rate by degrading every arrival to the edge of the envelope.
# Raw landed is still recorded on every eval, so the two objectives stay comparable.
W_PERFECT, W_GOOD, W_HARD = 3, 2, 1


def score(healthy, eo, seeds, runs=RUNS):
    total, detail = 0, {}
    for s in seeds:
        cmd = [EXE, "--headless", "--scenario", "entry", "--seed", str(s), "--runs", str(runs),
               "--rfly",
               "--rfly-fixed",    ",".join(f"{v:.4f}" for v in healthy),
               "--rfly-fixed-eo", ",".join(f"{v:.4f}" for v in eo),
               "--engine-out", "random"]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
        except subprocess.TimeoutExpired:
            return None, {}
        landed, weighted, split = None, None, None
        for line in p.stdout.splitlines():
            if line.startswith("LANDED:"):
                landed = int(line.split()[1].split("/")[0])
            if "PERFECT" in line and "GOOD" in line and "HARD" in line:
                t = line.split()
                g = {t[i]: int(t[i + 1]) for i in range(0, len(t) - 1, 2)}
                split = g
                weighted = (W_PERFECT * g.get("PERFECT", 0) + W_GOOD * g.get("GOOD", 0)
                            + W_HARD * g.get("HARD", 0))
        if landed is None or weighted is None:
            return None, {}          # silence is not success
        total += weighted
        detail[s] = {"weighted": weighted, "landed": landed, **(split or {})}
    return total, detail


def main():
    os.makedirs(OUTD, exist_ok=True)
    t0 = time.time()

    # theta_eo warm start = D-047's winner, if it exists; else the box-ceiling probe.
    eo0 = [4.0, 4.0, 3.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 1.0]
    for p in ("runs/d047/result.json", "runs/d047/best.json"):
        if os.path.exists(p):
            d = json.load(open(p))
            eo0 = d.get("theta_vec") or (d["theta"] if isinstance(d["theta"], list) else eo0)
            print(f"  theta_eo warm-started from {p}", flush=True)
            break

    mean = list(IDENT) + list(eo0)                      # 20-D: [healthy | eo]
    sd = [(RT_HI[i % 10] - RT_LO[i % 10]) * 0.25 for i in range(20)]
    gbest, gvec = -1, list(mean)
    n_train = RUNS * len(TRAIN_SEEDS) * W_PERFECT   # weighted max: every draw PERFECT

    def log(rec):
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec) + "\n")

    print(f"D-048 two-regime CEM  POP={POP} ITERS={ITERS}  train {TRAIN_SEEDS} x{RUNS}", flush=True)

    for it in range(ITERS):
        cands = []
        for p in range(POP):
            if it == 0 and p == 0:
                v = list(mean)                          # elitism: identity-healthy + D-047 eo
            else:
                v = [max(RT_LO[i % 10], min(RT_HI[i % 10], RNG.gauss(mean[i], sd[i])))
                     for i in range(20)]
            cands.append(v)

        scored = []
        for p, v in enumerate(cands):
            tot, det = score(v[:10], v[10:], TRAIN_SEEDS)
            if tot is None:
                log({"iter": it, "p": p, "vec": v, "FAILED": True}); continue
            scored.append((tot, v))
            log({"iter": it, "p": p, "vec": v, "landed": tot, "of": n_train, "detail": det,
                 "elapsed_s": round(time.time() - t0, 1)})
            if tot > gbest:
                gbest, gvec = tot, list(v)
                json.dump({"healthy": [round(x, 4) for x in gvec[:10]],
                           "eo": [round(x, 4) for x in gvec[10:]],
                           "train_landed": gbest, "train_of": n_train, "iter": it},
                          open(BEST, "w"), indent=2)
                print(f"  it{it} p{p}: NEW BEST {tot}/{n_train}  {det}", flush=True)

        if not scored:
            print("  every candidate failed to score -- aborting rather than reporting zeros",
                  flush=True)
            return 2

        scored.sort(key=lambda r: -r[0])
        el = [r[1] for r in scored[:ELITE]]
        for i in range(20):
            mu = sum(t[i] for t in el) / len(el)
            var = sum((t[i] - mu) ** 2 for t in el) / len(el)
            mean[i] = mu
            sd[i] = max(var ** 0.5, (RT_HI[i % 10] - RT_LO[i % 10]) * 0.03)
        print(f"  iter {it}: best={scored[0][0]}/{n_train} gbest={gbest}/{n_train} "
              f"[{round((time.time()-t0)/60,1)} min]", flush=True)

    print("\nD048-DONE — winner in runs/d048/best.json; fly held-out ONCE with d047_verify.py",
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
