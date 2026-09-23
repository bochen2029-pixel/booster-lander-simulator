"""E4 — OUTCOME OPTIMISATION of the 70-parameter conditional gain policy (2026-09-15).

theta = clamp( b + W . phi(legal nav state) ), the exact --rfly-policy class of D-050 (sim.c:718),
optimised by an antithetic evolution strategy on ACTUAL LANDING OUTCOMES: no labels, no teacher,
no privilege (phi reads six nav-view observables), flights at ~0.39 s each, sixteen in parallel.

Why this and not more label regression: E1..E3 measured that every label-trained net is a noisy
constant (held-out error per coordinate equals the constant-mean baseline), because the search's
pick at a state is an arbitrary point in a wide basin whose average is identity. Outcome
optimisation is indifferent to that. D-050 attempted this with 420 evaluations from a warm start
pinned on the box boundary and an ordinal four-level objective; the ledger (D-050 addendum 1 c)
asked for exactly this re-run: interior start, continuous margin objective, properly powered.

Design: CRN within an iteration (every candidate flies the same seed), a fresh seed each iteration
from 8000+, rank-shaped antithetic ES, the ES MEAN validated every few iterations on fixed seeds
8100-8102, the best-validated mean kept as champion, and the held-out seeds 42/7/99 flown ONCE at
the end. Exe: the E3 worktree build (build_e3), whose --rfly-policy path is the main tree's.

Usage: python runs/e4_es_policy.py [--iters 80] [--pairs 24] [--runs 40] [--workers 14]
"""
import argparse, csv, json, os, random, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

EXE = r"C:/bl_e1/build_e3/bin/Release/booster-core.exe"
OUTD = r"D:/bl_e1_data/es"
RT_LO = [0.30, 0.30, 0.60, 0.40, 0.25, 0.40, 0.50, 0.40, 0.00, 0.50]
RT_HI = [4.00, 4.00, 3.00, 2.50, 3.00, 2.50, 2.50, 3.50, 1.50, 2.50]
NF = 6
D047 = [3.9486512123718245, 3.6070627977252863, 3.0, 1.1034248624801957, 0.25, 0.4,
        0.6043122432336955, 0.5579795297922319, 0.33269879919580125, 0.7624253416827261]
VAL_SEEDS = [8100, 8101, 8102]
HELD_OUT = [42, 7, 99]
R_VERDICT = {1: 1.0, 2: 0.85, 3: 0.55, 4: 0.05, 5: 0.0}

def reward_rows(rows):
    """Continuous margin reward, mean over draws. Landed rows are shaped by touchdown speed and
    lateral miss; crashes score 0 (the manifest carries no shaping for them)."""
    if not rows:
        return None
    tot = 0.0
    for r in rows:
        v = int(r["verdict"]); base = R_VERDICT.get(v, 0.0)
        if v <= 3:
            base -= 0.03 * max(0.0, float(r["td_v"]) - 1.5)
            base -= 0.004 * max(0.0, float(r["td_lat"]) - 1.0)
        tot += base
    return tot / len(rows)

def landed_count(rows):
    return sum(1 for r in rows if int(r["verdict"]) <= 3)

def evaluate(vec70, seed, runs, tag):
    out = os.path.join(OUTD, "tmp", f"{tag}_{seed}.csv")
    csvarg = ",".join(f"{v:.6f}" for v in vec70)
    cmd = [EXE, "--headless", "--scenario", "entry", "--seed", str(seed), "--runs", str(runs),
           "--rfly", "--rfly-policy", csvarg, "--engine-out", "random", "--out", out]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    except subprocess.TimeoutExpired:
        return None, 0, "timeout"
    if p.returncode != 0 or not os.path.exists(out):
        return None, 0, f"rc={p.returncode} {p.stderr[-200:]}"
    with open(out, newline="") as fh:
        rows = list(csv.DictReader(fh))
    try: os.remove(out)
    except OSError: pass
    if len(rows) != runs:
        return None, 0, f"rows={len(rows)}"
    return reward_rows(rows), landed_count(rows), ""

def clamp_bias(vec):
    for o in range(10):
        vec[o * (NF + 1)] = min(RT_HI[o], max(RT_LO[o], vec[o * (NF + 1)]))
    return vec

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=80)
    ap.add_argument("--pairs", type=int, default=24)
    ap.add_argument("--runs", type=int, default=40)
    ap.add_argument("--workers", type=int, default=14)
    ap.add_argument("--lr", type=float, default=0.12)
    ap.add_argument("--val-every", type=int, default=5)
    ap.add_argument("--seed", type=int, default=20260915)
    args = ap.parse_args()
    os.makedirs(os.path.join(OUTD, "tmp"), exist_ok=True)
    log = open(os.path.join(OUTD, "log.jsonl"), "a", encoding="utf-8")
    rng = random.Random(args.seed)

    # interior warm start: the D-047 constant, every coordinate at a box bound moved 5% of its box
    # width inward (D-050 add.1 c: half the search directions were dead on the boundary).
    b0 = []
    for o in range(10):
        w = RT_HI[o] - RT_LO[o]; v = D047[o]
        if v >= RT_HI[o] - 1e-9: v = RT_HI[o] - 0.05 * w
        if v <= RT_LO[o] + 1e-9: v = RT_LO[o] + 0.05 * w
        b0.append(v)
    mean = []
    for o in range(10):
        mean.append(b0[o]); mean.extend([0.0] * NF)
    sigma = []
    for o in range(10):
        sigma.append(0.06 * (RT_HI[o] - RT_LO[o])); sigma.extend([0.15] * NF)

    pool = ThreadPoolExecutor(max_workers=args.workers)
    champion = {"vec": list(mean), "val": None, "iter": -1}
    t0 = time.time()
    print(f"[E4-ES] iters={args.iters} pairs={args.pairs} runs/eval={args.runs} workers={args.workers} lr={args.lr}", flush=True)

    def validate(vec, it):
        futs = [pool.submit(evaluate, vec, s, 60, f"val{it}") for s in VAL_SEEDS]
        res = [f.result() for f in futs]
        rs = [r for r, _, _ in res if r is not None]
        landed = sum(n for _, n, _ in res)
        return (sum(rs) / len(rs) if rs else None), landed

    v0, l0 = validate(mean, -1)
    champion = {"vec": list(mean), "val": v0, "landed": l0, "iter": -1}
    print(f"[E4-ES] warm start validation: reward {v0:.4f} landed {l0}/180 on seeds {VAL_SEEDS}", flush=True)
    log.write(json.dumps({"iter": -1, "val": v0, "landed": l0, "vec": mean}) + "\n"); log.flush()

    for it in range(args.iters):
        seed = 8000 + it
        eps = [[rng.gauss(0.0, 1.0) for _ in range(70)] for _ in range(args.pairs)]
        cands = []
        for e in eps:
            plus = clamp_bias([m + s * x for m, s, x in zip(mean, sigma, e)])
            minus = clamp_bias([m - s * x for m, s, x in zip(mean, sigma, e)])
            cands.append(plus); cands.append(minus)
        cands.append(list(mean))
        futs = [pool.submit(evaluate, c, seed, args.runs, f"it{it}c{k}") for k, c in enumerate(cands)]
        res = [f.result() for f in futs]
        rewards = [r if r is not None else 0.0 for r, _, _ in res]
        fails = [(k, msg) for k, (r, _, msg) in enumerate(res) if r is None]
        if fails:
            print(f"  it{it}: {len(fails)} evaluation(s) failed: {fails[:2]}", flush=True)
        n = 2 * args.pairs
        # rank-shaped fitness in [-0.5, 0.5] over the 2*pairs perturbed candidates
        order = sorted(range(n), key=lambda k: rewards[k])
        rank = [0.0] * n
        for pos, k in enumerate(order):
            rank[k] = pos / (n - 1) - 0.5
        grad = [0.0] * 70
        for i in range(args.pairs):
            fp, fm = rank[2 * i], rank[2 * i + 1]
            for j in range(70):
                grad[j] += (fp - fm) * eps[i][j]
        for j in range(70):
            grad[j] /= (2 * args.pairs)
            mean[j] += args.lr * sigma[j] * grad[j] * 2.0
        clamp_bias(mean)
        r_mean = rewards[-1]; l_mean = res[-1][1]
        best_k = max(range(n), key=lambda k: rewards[k])
        rec = {"iter": it, "seed": seed, "mean_reward": r_mean, "mean_landed": l_mean,
               "best_cand_reward": rewards[best_k], "best_cand_landed": res[best_k][1],
               "elapsed_min": round((time.time() - t0) / 60, 1)}
        msg = (f"  it{it:3d} seed {seed}: mean policy reward {r_mean:.4f} landed {l_mean}/{args.runs}"
               f" | best candidate {rewards[best_k]:.4f} landed {res[best_k][1]}/{args.runs}"
               f" [{rec['elapsed_min']} min]")
        if (it + 1) % args.val_every == 0:
            v, l = validate(mean, it)
            rec.update({"val": v, "val_landed": l})
            msg += f" | VAL {v:.4f} landed {l}/180"
            if v is not None and (champion["val"] is None or v > champion["val"]):
                champion = {"vec": list(mean), "val": v, "landed": l, "iter": it}
                json.dump(champion, open(os.path.join(OUTD, "champion.json"), "w"), indent=1)
                msg += "  <- new champion"
        json.dump({"vec": mean, "iter": it}, open(os.path.join(OUTD, "mean.json"), "w"))
        log.write(json.dumps(rec) + "\n"); log.flush()
        print(msg, flush=True)

    print(f"\n[E4-ES] champion: iter {champion['iter']} val reward {champion['val']:.4f} landed {champion['landed']}/180", flush=True)
    json.dump(champion, open(os.path.join(OUTD, "champion.json"), "w"), indent=1)
    print("[E4-ES] HELD-OUT, flown once:", flush=True)
    futs = [pool.submit(evaluate, champion["vec"], s, 60, "heldout") for s in HELD_OUT]
    tot = 0
    for s, f in zip(HELD_OUT, futs):
        r, l, msg = f.result(); tot += l
        print(f"  seed {s}: landed {l}/60  reward {r if r is not None else float('nan'):.4f} {msg}", flush=True)
    print(f"  TOTAL {tot}/180 = {100.0 * tot / 180:.1f}%", flush=True)
    print("E4-DONE", flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())
