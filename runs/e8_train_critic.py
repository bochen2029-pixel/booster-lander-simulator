"""
E8 — THE CRITIC, BUILT PROPERLY: full observation, ranking objective, designed candidates.

Q(obs39, mean_theta, cand_theta) -> standardised log(cost+1). Consulted ONLY to rank the CEM's
candidates within one replan iteration, so it is TRAINED to rank: a listwise softmax over each
replan group with the plant's argmin as the label, a pairwise RankNet term on every pair whose
costs differ by more than a margin, and a small auxiliary regression so the scale stays sane.

Why E7 failed and this should not: E7 regressed absolute cost from twelve summary magnitudes that
do not determine the state (argmin agreement 3-7%, chance is ~6-12%). Here the input is the full
legal observation the tap and every net consume, plus the CEM mean the candidate was drawn around
(so "which way from here" is a question the net can answer), and the loss is the question the
search actually asks.

Data: D:/bl_e1_data/e8/*.cand, 71 f64 per row (guidance_rfly.h RFLY_CAND_ROW). Groups =
(seed, run, t, iter); designed rows (iter 0, around the same mean) join iteration 0's group.
Split BY RUN (15% held out). Metrics that matter, on held-out runs:
    top-1 agreement with the plant's argmin
    regret = cost[critic pick] / cost[best]  (median, p90)
    P(critic pick lands | plant pick lands)  -- the one that predicts flight

Export: the text format guidance_rfly.c:rfly_load_critic reads:
    "59 H"  mu[59] sd[59] w1[H][59] b1[H] w2[H][H] b2[H] w3[H] b3 ymu ysd
Dead observation channels get sd=1, mu=their constant, so they contribute exactly 0 in C.
"""
import argparse, glob, os, sys, time
import numpy as np

NCOL, NOBS, NTH = 71, 39, 10
C_T, C_SEED, C_RUN, C_BIG, C_IT, C_DES = 0, 1, 2, 3, 4, 5
C_OBS, C_MEAN, C_CAND = 6, 45, 55
C_COST, C_LANDED = 65, 66
NIN = NOBS + 2 * NTH   # 59


def load(paths, max_rows_per_file=None):
    X, y, land, gid, run_id = [], [], [], [], []
    for p in paths:
        n = os.path.getsize(p) // (NCOL * 8)
        if n == 0: continue
        a = np.fromfile(p, dtype=np.float64, count=n * NCOL).reshape(n, NCOL)
        a = a[np.isfinite(a).all(axis=1)]
        if max_rows_per_file: a = a[:max_rows_per_file]
        seed = a[:, C_SEED].astype(np.int64); run = a[:, C_RUN].astype(np.int64)
        if np.isin(seed, [42, 7, 99]).any(): sys.exit("held-out law: gate seeds present in the candidate log")
        t_key = np.round(a[:, C_T] * 10).astype(np.int64)
        it = a[:, C_IT].astype(np.int64)                    # designed rows already carry iter=0
        g = (seed * 100000 + run) * 100000 + t_key * 4 + it
        X.append(np.concatenate([a[:, C_OBS:C_OBS+NOBS], a[:, C_MEAN:C_MEAN+NTH], a[:, C_CAND:C_CAND+NTH]], axis=1))
        y.append(np.log1p(np.maximum(a[:, C_COST], 0.0)))
        land.append(a[:, C_LANDED] > 0.5)
        gid.append(g); run_id.append(seed * 100000 + run)
    return (np.vstack(X), np.concatenate(y), np.concatenate(land), np.concatenate(gid), np.concatenate(run_id))


def groups_of(gid):
    order = np.argsort(gid, kind="stable")
    gs = gid[order]
    starts = np.r_[0, np.flatnonzero(np.diff(gs)) + 1]
    ends = np.r_[starts[1:], len(gs)]
    return order, starts, ends


def metrics(pred, y, land, gid):
    order, st, en = groups_of(gid)
    top1 = regrets = lands_given = n = 0; lg_n = 0
    for s, e in zip(st, en):
        if e - s < 2: continue
        idx = order[s:e]
        pb = idx[np.argmin(y[idx])]; pc = idx[np.argmin(pred[idx])]
        n += 1; top1 += (pb == pc)
        regrets = regrets if isinstance(regrets, list) else []
        regrets.append(np.expm1(y[pc]) / max(np.expm1(y[pb]), 1e-6))
        if land[pb]: lg_n += 1; lands_given += land[pc]
    r = np.array(regrets) if n else np.array([np.nan])
    return dict(groups=n, top1=top1 / max(n, 1), regret_med=float(np.median(r)),
                regret_p90=float(np.percentile(r, 90)), land_given=lands_given / max(lg_n, 1), chance=None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="D:/bl_e1_data/e8")
    ap.add_argument("--out", default="D:/bl_e1_data/e8/critic_v1.w")
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--margin", type=float, default=0.10, help="pairwise margin in log(cost+1)")
    ap.add_argument("--w_list", type=float, default=1.0)
    ap.add_argument("--w_pair", type=float, default=1.0)
    ap.add_argument("--w_mse", type=float, default=0.1)
    ap.add_argument("--val_frac", type=float, default=0.15)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--eval", default=None, metavar="CRITIC.w",
                    help="score an existing exported critic on this data's held-out runs (no training); "
                         "same run split as training with the same --seed, so rounds are comparable")
    args = ap.parse_args()

    import torch, torch.nn as nn, torch.nn.functional as F
    torch.manual_seed(args.seed); np.random.seed(args.seed)
    dev = "cuda" if torch.cuda.is_available() else "cpu"

    # --data may be a comma-separated list of dirs: round-0 farm + each phase-1.5 round's
    # critic-visited log (designed=2 rows), trained on the union.
    paths = sorted(p for d in args.data.split(",") for p in glob.glob(os.path.join(d.strip(), "s*.cand")))
    if not paths: sys.exit("no .cand files")
    X, y, land, gid, run_id = load(paths)
    print(f"rows {len(X):,}  groups {len(np.unique(gid)):,}  runs {len(np.unique(run_id)):,}  files {len(paths)}", flush=True)

    # split BY RUN
    runs = np.unique(run_id); rng = np.random.default_rng(args.seed); rng.shuffle(runs)
    nval = max(1, int(len(runs) * args.val_frac)); val_runs = set(runs[:nval].tolist())
    isval = np.isin(run_id, list(val_runs))
    print(f"train runs {len(runs)-nval}  val runs {nval}  train rows {(~isval).sum():,}  val rows {isval.sum():,}", flush=True)

    # standardise on TRAIN rows; dead channels -> mu=const, sd=1
    mu = X[~isval].mean(0); sd = X[~isval].std(0)
    dead = sd < 1e-9; sd = np.where(dead, 1.0, sd)
    print(f"dead input channels: {int(dead[:NOBS].sum())}/{NOBS} obs (indices {np.flatnonzero(dead[:NOBS]).tolist()})", flush=True)
    ymu, ysd = float(y[~isval].mean()), float(y[~isval].std() + 1e-9)
    Xs = ((X - mu) / sd).astype(np.float32); ys = ((y - ymu) / ysd).astype(np.float32)

    # group tensors: pad each group to its size, keep a mask
    def build_batches(mask_rows, shuffle):
        gm = np.where(mask_rows, gid, -1)               # rows outside the split collapse to one -1 group
        order, st, en = groups_of(gm)
        grp = [order[s:e] for s, e in zip(st, en) if gm[order[s]] >= 0 and e - s >= 2]   # MASKED gid
        big = max((len(g) for g in grp), default=0)
        assert big <= 256, f"a replan group has {big} rows — groups are colliding (see gid key)"
        if shuffle: rng.shuffle(grp)
        return grp
    tr_groups = build_batches(~isval, True); va_groups = build_batches(isval, False)
    print(f"train groups {len(tr_groups):,}  val groups {len(va_groups):,}  "
          f"mean group size {np.mean([len(g) for g in tr_groups]):.1f}", flush=True)

    if args.eval:
        # numpy forward pass with the exported file's own mu/sd (NOT this data's), exactly as C does
        with open(args.eval) as f:
            nin, H = map(int, f.readline().split())
            emu = np.array(f.readline().split(), float); esd = np.array(f.readline().split(), float)
            W1 = np.array([f.readline().split() for _ in range(H)], float); b1 = np.array(f.readline().split(), float)
            W2 = np.array([f.readline().split() for _ in range(H)], float); b2 = np.array(f.readline().split(), float)
            W3 = np.array(f.readline().split(), float); b3 = float(f.readline()); eymu, eysd = map(float, f.readline().split())
        x = (X - emu) / esd
        h1 = np.tanh(x @ W1.T + b1); h2 = np.tanh(h1 @ W2.T + b2); pred = (h2 @ W3 + b3).astype(np.float32)
        sel = np.concatenate(va_groups); mv = metrics(pred[sel], y[sel], land[sel], gid[sel])
        chance = 1.0 / np.mean([len(g) for g in va_groups])
        print(f"EVAL {args.eval}: VAL top1 {mv['top1']:.3f} (chance {chance:.3f})  regret med {mv['regret_med']:.2f} "
              f"p90 {mv['regret_p90']:.2f}  P(land|best lands) {mv['land_given']:.3f}  groups {mv['groups']}", flush=True)
        return 0

    Xt = torch.tensor(Xs, device=dev); yt = torch.tensor(ys, device=dev)
    H = args.hidden
    net = nn.Sequential(nn.Linear(NIN, H), nn.Tanh(), nn.Linear(H, H), nn.Tanh(), nn.Linear(H, 1)).to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)

    def batch_loss(groups):
        B = len(groups); L = max(len(g) for g in groups)
        assert L <= 256, f"batch group size {L}: the pairwise term is (B,L,L) and this would not fit"
        idx = np.full((B, L), -1, dtype=np.int64)
        for b, g in enumerate(groups): idx[b, :len(g)] = g
        idx_t = torch.tensor(idx, device=dev); m = idx_t >= 0
        safe = idx_t.clamp(min=0)
        pred = net(Xt[safe]).squeeze(-1)                       # (B, L)
        tgt = yt[safe]
        neg = float("-inf")
        # listwise: softmax over -pred, label = argmin target
        logits = torch.where(m, -pred, torch.full_like(pred, neg))
        label = torch.where(m, tgt, torch.full_like(tgt, float("inf"))).argmin(dim=1)
        l_list = F.cross_entropy(logits, label)
        # pairwise RankNet on pairs with |dy| > margin
        d_pred = pred.unsqueeze(2) - pred.unsqueeze(1)           # (B,L,L)  i - j
        d_tgt = tgt.unsqueeze(2) - tgt.unsqueeze(1)
        pm = m.unsqueeze(2) & m.unsqueeze(1) & (d_tgt.abs() > args.margin)
        # P(i better than j) should be 1 when tgt_i < tgt_j  =>  want pred_i < pred_j
        lab = (d_tgt < 0).float()
        l_pair = F.binary_cross_entropy_with_logits(-d_pred[pm], lab[pm]) if pm.any() else pred.sum() * 0
        l_mse = F.mse_loss(pred[m], tgt[m])
        return args.w_list * l_list + args.w_pair * l_pair + args.w_mse * l_mse

    def evaluate(groups):
        net.eval()
        with torch.no_grad():
            pred = np.empty(len(Xs), dtype=np.float32)
            for i in range(0, len(Xs), 65536):
                pred[i:i+65536] = net(Xt[i:i+65536]).squeeze(-1).cpu().numpy()
        net.train()
        sel = np.concatenate(groups)
        return metrics(pred[sel], y[sel], land[sel], gid[sel])

    best, best_state, t0 = -1.0, None, time.time()
    BATCH = 256
    for ep in range(args.epochs):
        rng.shuffle(tr_groups); tot = 0.0; nb = 0
        for i in range(0, len(tr_groups), BATCH):
            loss = batch_loss(tr_groups[i:i+BATCH])
            opt.zero_grad(); loss.backward(); opt.step(); tot += float(loss); nb += 1
        sched.step()
        mv = evaluate(va_groups)
        chance = 1.0 / np.mean([len(g) for g in va_groups])
        print(f"ep {ep:3d}  loss {tot/max(nb,1):.4f}  VAL top1 {mv['top1']:.3f} (chance {chance:.3f})  "
              f"regret med {mv['regret_med']:.2f} p90 {mv['regret_p90']:.2f}  "
              f"P(land|best lands) {mv['land_given']:.3f}  [{(time.time()-t0)/60:.1f} min]", flush=True)
        if mv["top1"] > best:
            best = mv["top1"]; best_state = {k: v.detach().cpu().clone() for k, v in net.state_dict().items()}

    net.load_state_dict(best_state)
    W1, b1 = net[0].weight.detach().cpu().numpy(), net[0].bias.detach().cpu().numpy()
    W2, b2 = net[2].weight.detach().cpu().numpy(), net[2].bias.detach().cpu().numpy()
    W3, b3 = net[4].weight.detach().cpu().numpy().reshape(-1), float(net[4].bias.detach().cpu().numpy()[0])
    with open(args.out, "w") as f:
        f.write(f"{NIN} {H}\n")
        f.write(" ".join(f"{v:.17g}" for v in mu) + "\n"); f.write(" ".join(f"{v:.17g}" for v in sd) + "\n")
        for h in range(H): f.write(" ".join(f"{v:.17g}" for v in W1[h]) + "\n")
        f.write(" ".join(f"{v:.17g}" for v in b1) + "\n")
        for h in range(H): f.write(" ".join(f"{v:.17g}" for v in W2[h]) + "\n")
        f.write(" ".join(f"{v:.17g}" for v in b2) + "\n")
        f.write(" ".join(f"{v:.17g}" for v in W3) + "\n"); f.write(f"{b3:.17g}\n"); f.write(f"{ymu:.17g} {ysd:.17g}\n")
    mv = evaluate(va_groups)
    print(f"\nEXPORTED {args.out}  best VAL top1 {best:.3f}  regret med {mv['regret_med']:.2f}  "
          f"P(land|best lands) {mv['land_given']:.3f}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
