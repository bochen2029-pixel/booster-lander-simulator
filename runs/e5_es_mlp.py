"""E5 — OUTCOME OPTIMISATION of a small MLP gain policy over twelve legal features (2026-09-15).

theta = clamp( b2 + Wlin.phi + W2.tanh(W1.phi + b1) ),   phi in R^12 (sim.c rfly_mlp_theta):
  0 alt/62000  1 lateral/3000  2 vz/1500  3 vxy/300  4 n_eng/3  5 prop/30000   (the D-050 six)
  6 engine_on  7 fins_deployed  8 min(1, t_since_engine_count_changed/20)  9 Mach/5  10 qbar/60000  11 cos(tilt)
Loaded at runtime by --rfly-mlp FILE (default off, byte-identical when absent). The linear skip is
initialised to the E4 champion (its six features into columns 0..5, its bias into b2) with the hidden
path silent, so iteration 0 reproduces the champion exactly; the ES then grows the class.

Weight file (text): "nin nhid" then Wlin[10][nin], b2[10], W1[nhid][nin], b1[nhid], W2[10][nhid],
whitespace-separated, row by row.

Usage: python runs/e5_es_mlp.py [--iters 150] [--pairs 32] [--runs 40] [--workers 14]
"""
import argparse, csv, json, os, random, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

EXE = r"C:/bl_e1/build_e3/bin/Release/booster-core.exe"
OUTD = r"D:/bl_e1_data/es_mlp"
CHAMP = r"D:/bl_e1_data/es/champion.json"
RT_LO = [0.30, 0.30, 0.60, 0.40, 0.25, 0.40, 0.50, 0.40, 0.00, 0.50]
RT_HI = [4.00, 4.00, 3.00, 2.50, 3.00, 2.50, 2.50, 3.50, 1.50, 2.50]
NIN, NHID, NOUT = 12, 16, 10
VAL_SEEDS = [8100, 8101, 8102]
HELD_OUT = [42, 7, 99]
R_VERDICT = {1: 1.0, 2: 0.85, 3: 0.55, 4: 0.05, 5: 0.0}

# parameter layout (flat): Wlin[10][12] (120) | b2[10] (10) | W1[16][12] (192) | b1[16] (16) | W2[10][16] (160) = 498
N_WLIN, N_B2, N_W1, N_B1, N_W2 = NOUT * NIN, NOUT, NHID * NIN, NHID, NOUT * NHID
NPAR = N_WLIN + N_B2 + N_W1 + N_B1 + N_W2

def write_weights(vec, path):
    i = 0
    with open(path, "w") as fh:
        fh.write(f"{NIN} {NHID}\n")
        for _ in range(NOUT): fh.write(" ".join(f"{x:.9g}" for x in vec[i:i + NIN]) + "\n"); i += NIN
        fh.write(" ".join(f"{x:.9g}" for x in vec[i:i + NOUT]) + "\n"); i += NOUT
        for _ in range(NHID): fh.write(" ".join(f"{x:.9g}" for x in vec[i:i + NIN]) + "\n"); i += NIN
        fh.write(" ".join(f"{x:.9g}" for x in vec[i:i + NHID]) + "\n"); i += NHID
        for _ in range(NOUT): fh.write(" ".join(f"{x:.9g}" for x in vec[i:i + NHID]) + "\n"); i += NHID
    assert i == NPAR

def reward_rows(rows):
    tot = 0.0
    for r in rows:
        v = int(r["verdict"]); base = R_VERDICT.get(v, 0.0)
        if v <= 3:
            base -= 0.03 * max(0.0, float(r["td_v"]) - 1.5)
            base -= 0.004 * max(0.0, float(r["td_lat"]) - 1.0)
        tot += base
    return tot / len(rows)

def evaluate(vec, seed, runs, tag):
    wpath = os.path.join(OUTD, "tmp", f"{tag}_{seed}.w")
    out = os.path.join(OUTD, "tmp", f"{tag}_{seed}.csv")
    write_weights(vec, wpath)
    cmd = [EXE, "--headless", "--scenario", "entry", "--seed", str(seed), "--runs", str(runs),
           "--rfly", "--rfly-mlp", wpath, "--engine-out", "random", "--out", out]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    except subprocess.TimeoutExpired:
        return None, 0, "timeout"
    if p.returncode != 0 or not os.path.exists(out):
        return None, 0, f"rc={p.returncode} {p.stderr[-200:]}"
    with open(out, newline="") as fh:
        rows = list(csv.DictReader(fh))
    for f in (out, wpath):
        try: os.remove(f)
        except OSError: pass
    if len(rows) != runs:
        return None, 0, f"rows={len(rows)}"
    return reward_rows(rows), sum(1 for r in rows if int(r["verdict"]) <= 3), ""

def clamp_b2(vec):
    for o in range(NOUT):
        k = N_WLIN + o
        vec[k] = min(RT_HI[o], max(RT_LO[o], vec[k]))
    return vec

def init_from_champion(rng):
    c = json.load(open(CHAMP))["vec"]          # 70 = 10 x [bias + 6 weights]
    vec = [0.0] * NPAR
    for o in range(NOUT):
        b = c[o * 7]; w6 = c[o * 7 + 1:o * 7 + 7]
        for f in range(6): vec[o * NIN + f] = w6[f]
        vec[N_WLIN + o] = b
    base = N_WLIN + N_B2
    for k in range(N_W1): vec[base + k] = rng.gauss(0.0, 0.3)       # hidden features, random but silent (W2 = 0)
    return vec

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=150)
    ap.add_argument("--pairs", type=int, default=32)
    ap.add_argument("--runs", type=int, default=40)
    ap.add_argument("--workers", type=int, default=14)
    ap.add_argument("--lr", type=float, default=0.12)
    ap.add_argument("--val-every", type=int, default=5)
    ap.add_argument("--seed", type=int, default=20260916)
    ap.add_argument("--seed-base", type=int, default=8200)
    args = ap.parse_args()
    os.makedirs(os.path.join(OUTD, "tmp"), exist_ok=True)
    log = open(os.path.join(OUTD, "log.jsonl"), "a", encoding="utf-8")
    rng = random.Random(args.seed)

    mean = init_from_champion(rng)
    sigma = [0.0] * NPAR
    for o in range(NOUT):
        for f in range(NIN): sigma[o * NIN + f] = 0.06
        sigma[N_WLIN + o] = 0.03 * (RT_HI[o] - RT_LO[o])
    base = N_WLIN + N_B2
    for k in range(N_W1): sigma[base + k] = 0.08
    for k in range(N_B1): sigma[base + N_W1 + k] = 0.08
    for k in range(N_W2): sigma[base + N_W1 + N_B1 + k] = 0.05

    pool = ThreadPoolExecutor(max_workers=args.workers)
    t0 = time.time()
    print(f"[E5-ES] params={NPAR} iters={args.iters} pairs={args.pairs} runs/eval={args.runs} workers={args.workers}", flush=True)

    def validate(vec, it):
        futs = [pool.submit(evaluate, vec, s, 60, f"val{it}") for s in VAL_SEEDS]
        res = [f.result() for f in futs]
        rs = [r for r, _, _ in res if r is not None]
        return (sum(rs) / len(rs) if rs else None), sum(n for _, n, _ in res), [m for _, _, m in res if m]

    v0, l0, msgs = validate(mean, -1)
    if v0 is None:
        print(f"[E5-ES] ABORT: warm-start validation failed: {msgs}", flush=True); return 2
    champion = {"vec": list(mean), "val": v0, "landed": l0, "iter": -1}
    json.dump(champion, open(os.path.join(OUTD, "champion.json"), "w"))
    print(f"[E5-ES] warm start (= E4 champion through the MLP) validation: reward {v0:.4f} landed {l0}/180 (E4 champion was 125/180 here)", flush=True)
    log.write(json.dumps({"iter": -1, "val": v0, "landed": l0}) + "\n"); log.flush()

    for it in range(args.iters):
        seed = args.seed_base + it
        eps = [[rng.gauss(0.0, 1.0) for _ in range(NPAR)] for _ in range(args.pairs)]
        cands = []
        for e in eps:
            cands.append(clamp_b2([m + s * x for m, s, x in zip(mean, sigma, e)]))
            cands.append(clamp_b2([m - s * x for m, s, x in zip(mean, sigma, e)]))
        cands.append(list(mean))
        futs = [pool.submit(evaluate, c, seed, args.runs, f"it{it}c{k}") for k, c in enumerate(cands)]
        res = [f.result() for f in futs]
        rewards = [r if r is not None else 0.0 for r, _, _ in res]
        fails = [(k, msg) for k, (r, _, msg) in enumerate(res) if r is None]
        if fails:
            print(f"  it{it}: {len(fails)} evaluation(s) failed: {fails[:2]}", flush=True)
        n = 2 * args.pairs
        order = sorted(range(n), key=lambda k: rewards[k])
        rank = [0.0] * n
        for pos, k in enumerate(order): rank[k] = pos / (n - 1) - 0.5
        grad = [0.0] * NPAR
        for i in range(args.pairs):
            d = rank[2 * i] - rank[2 * i + 1]
            if d != 0.0:
                ei = eps[i]
                for j in range(NPAR): grad[j] += d * ei[j]
        for j in range(NPAR):
            mean[j] += args.lr * sigma[j] * grad[j] / args.pairs
        clamp_b2(mean)
        r_mean = rewards[-1]; l_mean = res[-1][1]
        best_k = max(range(n), key=lambda k: rewards[k])
        rec = {"iter": it, "seed": seed, "mean_reward": r_mean, "mean_landed": l_mean,
               "best_cand_reward": rewards[best_k], "best_cand_landed": res[best_k][1],
               "elapsed_min": round((time.time() - t0) / 60, 1)}
        msg = (f"  it{it:3d} seed {seed}: mean policy reward {r_mean:.4f} landed {l_mean}/{args.runs}"
               f" | best candidate {rewards[best_k]:.4f} landed {res[best_k][1]}/{args.runs} [{rec['elapsed_min']} min]")
        if (it + 1) % args.val_every == 0:
            v, l, _ = validate(mean, it)
            rec.update({"val": v, "val_landed": l})
            msg += f" | VAL {v if v is not None else float('nan'):.4f} landed {l}/180"
            if v is not None and v > champion["val"]:
                champion = {"vec": list(mean), "val": v, "landed": l, "iter": it}
                json.dump(champion, open(os.path.join(OUTD, "champion.json"), "w"))
                msg += "  <- new champion"
        json.dump({"vec": mean, "iter": it}, open(os.path.join(OUTD, "mean.json"), "w"))
        log.write(json.dumps(rec) + "\n"); log.flush()
        print(msg, flush=True)

    print(f"\n[E5-ES] champion: iter {champion['iter']} val reward {champion['val']:.4f} landed {champion['landed']}/180", flush=True)
    write_weights(champion["vec"], os.path.join(OUTD, "champion.w"))
    print("[E5-ES] HELD-OUT, flown once:", flush=True)
    futs = [pool.submit(evaluate, champion["vec"], s, 60, "heldout") for s in HELD_OUT]
    tot = 0
    for s, f in zip(HELD_OUT, futs):
        r, l, msg = f.result(); tot += l
        print(f"  seed {s}: landed {l}/60  reward {r if r is not None else float('nan'):.4f} {msg}", flush=True)
    print(f"  TOTAL {tot}/180 = {100.0 * tot / 180:.1f}%", flush=True)
    print("E5-DONE", flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())
