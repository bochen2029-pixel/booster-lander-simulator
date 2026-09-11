"""
D-049 (①e) — THREE CONSTANT THETAS, SELECTED ON FLIGHT PHASE.

The mechanism D-047's quality data demands.

D-047 showed a single optimized constant reaches ~84/120 on training where identity reaches ~18
-- but with ZERO PERFECT landings: mean td_v 4.09 m/s against a 6.0 crash threshold, 11.37 m off
centre. It buys survival and cannot buy precision, and the reason is structural rather than a
tuning shortfall:

    the entry divert wants MAXIMUM lateral authority and MINIMUM damping
    the terminal flare wants exactly the opposite

One constant must pick. It picks aggressive -- because that is what converts an off-pad crash
into a landing at all -- and then pays for it at touchdown. D-042 reached the same conclusion from
the other direction and it is the sharpest sentence in that ADR: run-0's own converged theta, held
constant, CRASHES its own draw. Theta must vary by phase; the search's whole advantage may be
that it re-solves rather than that it foresees.

So: theta = theta[band], band = entry-burn / aero / landing-burn.

Phase is the vehicle's OWN FLIGHT STATE (State.phase), not a prediction and not privileged
information, so this remains a reflex with search-settable parameters:

    no inference   it is a table lookup on a state the vehicle is already in
    no privilege   nothing about the fault, present or future, is consulted
    no latency     0.39 s/run, against 76 s for the CEM
    no net         thirty numbers in a header

WHY THIS AND NOT ①d (n_eng). Measured, not assumed: the fault fires at t in [4,18] s of a
~117-140 s flight, so n_eng<3 holds for roughly 90% of every draw on this battery and the n_eng
switch barely discriminates HERE. It still protects clean flight and stays available as
--rfly-fixed-eo, but phase is the axis the quality data actually points at.

OBJECTIVE: quality-weighted (PERFECT 3, GOOD 2, HARD 1) PLUS A HARD ANCHOR. The weight alone
still lets the search trade PERFECTs for HARDs at some exchange rate, and D-047's leader arrived
at max td_v 5.83 against a 6.0 crash threshold -- one gust from a crash it was counting as a win.
So TD_ANCHOR is a GATE, not a term: a candidate whose WORST arrival sits inside it scores zero
regardless of rate. Rate bought at the edge of the envelope is not rate. Raw landed and max td_v
are recorded on every eval so all three objectives stay comparable.

WARM START: all three bands = D-047's winner. That makes plain D-047 exactly representable and it
sits in elitism slot 0, so D-049 can only match-or-beat it on the training objective.

PRE-REGISTERED READS
  no better than D-047 weighted -> phase is not the axis; the aggression that buys closure is
                                  needed all the way down and the HARD arrivals are the price of
                                  landing at all. Report as a null; ①e closes.
  better weighted, similar rate -> the flare band recovers PRECISION the single constant had to
                                  trade away. This is the predicted outcome.
  better weighted AND better rate -> a thirty-number, zero-privilege, zero-latency controller,
                                  and "the compound is search-necessary" needs re-scoping.

HONEST BOUND, unchanged: no constant schedule can beat the search, whose population contains
every constant. This measures how much of GM_RFLY's edge was RE-SOLVING versus FORESEEING.

Trained on seeds 5000/5001 only. Held-out 42/7/99 flown ONCE, at the end, by d047_verify.py.
"""
import json, os, random, subprocess, sys, time

EXE  = r"C:/Booster_Lander_Simulator/build/bin/Release/booster-core.exe"
OUTD = r"C:/Booster_Lander_Simulator/runs/d049"
LOG  = os.path.join(OUTD, "evals.jsonl")
BEST = os.path.join(OUTD, "best.json")

RT_LO = [0.30, 0.30, 0.60, 0.40, 0.25, 0.40, 0.50, 0.40, 0.00, 0.50]
RT_HI = [4.00, 4.00, 3.00, 2.50, 3.00, 2.50, 2.50, 3.50, 1.50, 2.50]
BANDS = ["entry", "aero", "landing"]
TRAIN_SEEDS, RUNS = [5000, 5001], 60
POP, ELITE, ITERS = 30, 8, 10
W_PERFECT, W_GOOD, W_HARD = 3, 2, 1
TD_ANCHOR = 5.8   # hard gate: worst arrival must clear the 6.0 crash threshold by margin
RNG = random.Random(20260911)


def score(vec30, seeds, runs=RUNS):
    """vec30 = [entry(10) | aero(10) | landing(10)] -> (weighted total, per-seed detail)."""
    total, detail = 0, {}
    csv = ",".join(f"{v:.4f}" for v in vec30)
    for s in seeds:
        cmd = [EXE, "--headless", "--scenario", "entry", "--seed", str(s), "--runs", str(runs),
               "--rfly", "--rfly-fixed", ",".join(f"{v:.4f}" for v in vec30[:10]),
               "--rfly-fixed-phase", csv, "--engine-out", "random"]
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
                try:
                    max_td = float(line.split("(max")[1].split(")")[0])
                except (ValueError, IndexError):
                    max_td = 0.0
        if landed is None or weighted is None:
            return None, {}                      # silence is not success
        # THE ANCHOR. A weight alone still lets the search trade PERFECTs for HARDs at some
        # exchange rate, and D-047's leader arrived at max td_v 5.83 against a 6.0 crash
        # threshold -- one gust from a crash it was counting as a win. So this is a gate, not a
        # term: a candidate whose WORST arrival sits inside TD_ANCHOR of the threshold scores
        # zero regardless of how many draws it landed. Rate bought at the edge of the envelope
        # is not rate. (Set at 5.8 rather than lower because at n=60 a single marginal draw
        # would otherwise kill an otherwise-sound candidate on noise alone.)
        if max_td > TD_ANCHOR:
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

    base = [4.0, 4.0, 3.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 1.0]
    for p in ("runs/d047/result.json", "runs/d047/best.json"):
        if os.path.exists(p):
            d = json.load(open(p))
            t = d.get("theta_vec") or d["theta"]
            if isinstance(t, list):
                base = t
                print(f"  warm start: all three bands = D-047's winner ({p})", flush=True)
            break

    mean = list(base) * 3
    sd = [(RT_HI[i % 10] - RT_LO[i % 10]) * 0.22 for i in range(30)]
    gbest, gvec = -1, list(mean)
    wmax = RUNS * len(TRAIN_SEEDS) * W_PERFECT

    def log(rec):
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec) + "\n")

    print(f"D-049 phase-theta CEM  POP={POP} ITERS={ITERS}  weighted max {wmax}", flush=True)

    for it in range(ITERS):
        cands = [list(mean) if (it == 0 and p == 0) else
                 [max(RT_LO[i % 10], min(RT_HI[i % 10], RNG.gauss(mean[i], sd[i])))
                  for i in range(30)]
                 for p in range(POP)]
        scored = []
        for p, v in enumerate(cands):
            tot, det = score(v, TRAIN_SEEDS)
            if tot is None:
                log({"iter": it, "p": p, "FAILED": True}); continue
            scored.append((tot, v))
            log({"iter": it, "p": p, "vec": v, "weighted": tot, "of": wmax, "detail": det,
                 "elapsed_s": round(time.time() - t0, 1)})
            if tot > gbest:
                gbest, gvec = tot, list(v)
                json.dump({"bands": {BANDS[b]: [round(x, 4) for x in gvec[b*10:(b+1)*10]]
                                     for b in range(3)},
                           "vec30": [round(x, 4) for x in gvec],
                           "weighted": gbest, "of": wmax, "iter": it, "detail": det},
                          open(BEST, "w"), indent=2)
                landed = sum(d["landed"] for d in det.values())
                print(f"  it{it} p{p}: NEW BEST w={tot}/{wmax}  landed={landed}/120  {det}",
                      flush=True)
        if not scored:
            print("  every candidate failed to score -- aborting", flush=True)
            return 2
        scored.sort(key=lambda r: -r[0])
        el = [r[1] for r in scored[:ELITE]]
        for i in range(30):
            mu = sum(t[i] for t in el) / len(el)
            var = sum((t[i] - mu) ** 2 for t in el) / len(el)
            mean[i] = mu
            sd[i] = max(var ** 0.5, (RT_HI[i % 10] - RT_LO[i % 10]) * 0.03)
        print(f"  iter {it}: best={scored[0][0]}/{wmax} gbest={gbest}/{wmax} "
              f"[{round((time.time()-t0)/60,1)} min]", flush=True)

    print("\nD049-DONE — winner in runs/d049/best.json", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
