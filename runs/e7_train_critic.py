"""E7 — train the CRITIC: Q(phi[12], theta[10]) -> log(cost) on the search's own candidate evaluations.

Rows from --rfly-cand-log (27 f64): t, seed, run, big, phi[12], theta[10], cost. Split BY RUN (seed, run).
Net: 22 -> H -> H -> 1, tanh, inputs standardised, target log(cost) standardised. Best-val checkpoint.
Export (text, read by rfly_load_critic): "nin nhid" then in_mu[22], in_sd[22], W1[H][22], b1[H],
W2[H][H], b2[H], W3[1][H], b3[1], out_mu, out_sd.  Prediction = out_mu + out_sd * net(standardised x).

Usage: python runs/e7_train_critic.py --data D:/bl_e1_data/e7 --out D:/bl_e1_data/e7/critic.w [--hidden 128] [--epochs 40]
"""
import argparse, glob, os, sys, time
import numpy as np

NCOL = 27
def load(specs):
    parts = []
    for s in specs:
        files = sorted(glob.glob(os.path.join(s, "*.cand"))) if os.path.isdir(s) else sorted(glob.glob(s))
        for f in files:
            n = os.path.getsize(f)
            whole = (n // (NCOL * 8)) * NCOL
            if whole == 0:
                print(f"  skip {f}: no whole rows"); continue
            a = np.fromfile(f, dtype=np.float64, count=whole).reshape(-1, NCOL)
            if n % (NCOL * 8) != 0:
                print(f"  {os.path.basename(f)}: torn tail of {n % (NCOL*8)} bytes dropped (process died mid-write)")
            parts.append(a); print(f"  loaded {os.path.basename(f)}: {len(a):,} rows")
    return np.concatenate(parts, axis=0)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--batch", type=int, default=8192)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--val-frac", type=float, default=0.15)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--relative", action="store_true",
                    help="target = log cost minus the mean log cost of its replan group (the search only needs the within-replan ranking)")
    args = ap.parse_args()
    import torch, torch.nn as nn
    torch.manual_seed(args.seed); np.random.seed(args.seed)
    dev = "cuda" if torch.cuda.is_available() else "cpu"

    rows = load(args.data)
    seeds = rows[:, 1].astype(np.int64)
    bad = np.isin(seeds, [42, 7, 99])
    if bad.any(): sys.exit("held-out law: gate seeds present in the candidate log")
    X = np.concatenate([rows[:, 4:16], rows[:, 16:26]], axis=1)          # phi[12] + theta[10]
    y = np.log(np.maximum(rows[:, 26], 1.0))
    key = seeds * 100000 + rows[:, 2].astype(np.int64)
    if args.relative:
        grp_all = key * 100000 + (rows[:, 0] * 10).astype(np.int64)
        order = np.argsort(grp_all, kind="stable"); g_sorted = grp_all[order]
        starts = np.flatnonzero(np.r_[True, g_sorted[1:] != g_sorted[:-1]])
        means = np.add.reduceat(y[order], starts) / np.diff(np.r_[starts, len(order)])
        gm = np.empty(len(y)); gm[order] = np.repeat(means, np.diff(np.r_[starts, len(order)]))
        y = y - gm
        print(f"  relative target: log cost minus its replan-group mean ({len(starts)} groups); residual sd {y.std():.3f}")
    runs = np.unique(key); rng = np.random.default_rng(args.seed); rng.shuffle(runs)
    n_val = max(1, int(len(runs) * args.val_frac)); val_runs = set(runs[:n_val].tolist())
    va = np.array([k in val_runs for k in key]); tr = ~va
    print(f"  rows {len(rows):,}  runs {len(runs)} -> {n_val} val / {len(runs)-n_val} train; rows {tr.sum():,} / {va.sum():,}")
    print(f"  cost p10/50/90 {np.percentile(rows[:,26],[10,50,90]).round(1)}  log-cost sd {y.std():.3f}")

    mu = X[tr].mean(0); sd = X[tr].std(0); sd[sd < 1e-8] = 1.0
    ymu = y[tr].mean(); ysd = y[tr].std()
    Xt = torch.tensor((X - mu) / sd, dtype=torch.float32, device=dev)
    yt = torch.tensor((y - ymu) / ysd, dtype=torch.float32, device=dev)
    tri = torch.tensor(np.flatnonzero(tr), device=dev); vai = torch.tensor(np.flatnonzero(va), device=dev)

    H = args.hidden
    net = nn.Sequential(nn.Linear(22, H), nn.Tanh(), nn.Linear(H, H), nn.Tanh(), nn.Linear(H, 1)).to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=args.lr)
    def ev(idx):
        net.eval()
        with torch.no_grad():
            p = net(Xt[idx]).squeeze(1); e = p - yt[idx]
            return float((e * e).mean().sqrt())
    # baseline: predict the mean => rmse 1.0 in standardised units
    best = {"val": float("inf"), "ep": -1, "state": None}
    t0 = time.time(); n = tri.numel()
    for ep in range(args.epochs):
        net.train(); perm = tri[torch.randperm(n, device=dev)]
        for i in range(0, n, args.batch):
            bi = perm[i:i + args.batch]
            loss = ((net(Xt[bi]).squeeze(1) - yt[bi]) ** 2).mean()
            opt.zero_grad(); loss.backward(); opt.step()
        v = ev(vai)
        if v < best["val"]:
            best = {"val": v, "ep": ep + 1, "state": {k: t.detach().clone() for k, t in net.state_dict().items()}}
        if (ep + 1) % 5 == 0 or ep == 0:
            print(f"  ep {ep+1:3d} train rmse {ev(tri):.4f}  val rmse {v:.4f}  (standardised; mean-predictor = 1.0)  [{time.time()-t0:.0f}s]")
    net.load_state_dict(best["state"])
    print(f"  best epoch {best['ep']} val rmse {best['val']:.4f}; in log-cost units {best['val']*ysd:.3f} (cost factor x{np.exp(best['val']*ysd):.2f})")
    # rank quality on validation: within each (run, replan) group, does the critic's argmin match the search's argmin?
    net.eval()
    with torch.no_grad():
        pred = net(Xt).squeeze(1).cpu().numpy() * ysd + ymu
    grp = key * 100000 + (rows[:, 0] * 10).astype(np.int64)
    hits = 0; tot = 0; regret = []
    for g in np.unique(grp[va]):
        m = np.flatnonzero((grp == g) & va)
        if len(m) < 4: continue
        c = rows[m, 26]; p = pred[m]
        tot += 1; hits += int(np.argmin(p) == np.argmin(c))
        regret.append(c[np.argmin(p)] / max(c.min(), 1.0))
    print(f"  validation replans {tot}: critic argmin == plant argmin in {hits/tot*100:.1f}%; chosen-candidate cost / best cost: median {np.median(regret):.3f}, p90 {np.percentile(regret,90):.3f}")

    sdict = {k: v.detach().cpu().double().numpy() for k, v in net.state_dict().items()}
    with open(args.out, "w") as fh:
        fh.write(f"22 {H}\n")
        fh.write(" ".join(f"{v:.17g}" for v in mu) + "\n"); fh.write(" ".join(f"{v:.17g}" for v in sd) + "\n")
        for r in sdict["0.weight"]: fh.write(" ".join(f"{v:.17g}" for v in r) + "\n")
        fh.write(" ".join(f"{v:.17g}" for v in sdict["0.bias"]) + "\n")
        for r in sdict["2.weight"]: fh.write(" ".join(f"{v:.17g}" for v in r) + "\n")
        fh.write(" ".join(f"{v:.17g}" for v in sdict["2.bias"]) + "\n")
        fh.write(" ".join(f"{v:.17g}" for v in sdict["4.weight"][0]) + "\n")
        fh.write(f"{sdict['4.bias'][0]:.17g}\n")
        fh.write(f"{ymu:.17g} {ysd:.17g}\n")
    print(f"  wrote {args.out}")

if __name__ == "__main__":
    main()
