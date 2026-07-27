"""train_theta.py — the THETA-PRIOR trainer (oracle-distill, D-041). OFFLINE PRECOMPUTE.

WHAT THIS NET IS FOR — and what it is NOT for.

GM_RFLY lands the compound 36/36 by running a CEM over a 10-D theta of gain multipliers, warm-started
from IDENTITY (= plain hoverslam) and ground down over ~150 s of CPU per flight. This net predicts
that theta from the legal onboard observation, and its ONLY job is to replace the identity warm start
with a better one, so the same search reaches the same basin in far fewer rollouts. That is exactly
the role AlphaZero's policy network plays for MCTS: a prior over where to look, not the answer.

It is NOT a controller. LODESTAR already ran the "net predicts theta, analytic law flies it" experiment
against the sandbox oracle and it was a conclusive NO-GO (ORACLE_LODESTAR §13: every variant <= nominal
theta, flat learning curve) — because that law's theta basin is a +-5% knife edge and an open-loop
selection cannot hit it. As a PRIOR the bar is completely different and much lower: beat identity. A
warm start that is merely closer than identity is already a win, and the CEM's own elitism keeps
identity in the population, so a bad prior costs compute, never correctness. Nothing here can make the
flown trajectory worse than the search would have found anyway.

WHY THE TARGET IS LEGITIMATE. Theta is PRIVILEGED teacher context: the CEM chose it by flying
candidates through the real plant with the actual disturbance realization. That is fine for a TARGET
(a teacher may cheat) and forbidden as an INPUT (canon §8.1 provenance). The input here is strictly
the same legal observation the student policy reads.

Usage:
  python train_theta.py --data data\\s0rf --verdict-csv data\\s0rf --out runs\\theta_prior.pt
"""

from __future__ import annotations
import argparse, os, sys, time
import numpy as np

import rowformat as rf
from train_s0 import load_dataset, load_verdicts, verdict_filter, split_by_run

# ---- THE THETA BOUNDS — mirror of RT_LO / RT_HI in core/guidance_rfly.c ----------------------------
# The CEM clamps every candidate into this box, so a prediction outside it is meaningless. The net's
# tanh output is de-normalized into exactly this range (the same pattern the throttle channel uses in
# train_s0.py / guidance_neural.c), which makes an out-of-box prediction structurally impossible.
RT_LO = np.array([0.30, 0.30, 0.60, 0.40, 0.25, 0.40, 0.50, 0.40, 0.00, 0.50])
RT_HI = np.array([4.00, 4.00, 3.00, 2.50, 3.00, 2.50, 2.50, 3.50, 1.50, 2.50])
# RT_IDENTITY — the current warm start, and therefore THE baseline this net has to beat.
RT_IDENTITY = np.array([1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 1.0])


def main():
    ap = argparse.ArgumentParser(description="theta-prior trainer (CEM warm start).")
    ap.add_argument("--data", nargs="+", required=True)
    ap.add_argument("--verdict-csv", nargs="+", default=[])
    ap.add_argument("--keep-verdicts", default="1,2")
    ap.add_argument("--out", required=True)
    ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--layers", type=int, default=3)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--val-frac", type=float, default=0.15)
    ap.add_argument("--batch", type=int, default=4096)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--weight-decay", type=float, default=0.0, help="AdamW L2 (M4 push: tames overfit)")
    args = ap.parse_args()

    import torch
    import torch.nn as nn
    torch.manual_seed(args.seed); np.random.seed(args.seed)
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"[train_theta] device={dev} torch={torch.__version__}")

    rows = load_dataset(args.data)                       # enforces the held-out law (42/7/99 refused)
    if args.verdict_csv:
        keep = set(int(x) for x in args.keep_verdicts.split(","))
        rows = verdict_filter(rows, load_verdicts(args.verdict_csv), keep)

    obs_np, _ = rf.split(rows)
    th_np = rf.theta_of(rows)

    # Drop rows with no teacher context (all-zero theta == "not flown by GM_RFLY"). Without this the
    # net would be trained to predict a theta that was never searched, on states where no search ran.
    has_theta = np.abs(th_np).sum(axis=1) > 0
    dropped = int((~has_theta).sum())
    if dropped:
        print(f"  dropping {dropped} row(s) with no GM_RFLY teacher context (all-zero theta)")
    rows, obs_np, th_np = rows[has_theta], obs_np[has_theta], th_np[has_theta]
    if not len(rows):
        sys.exit("error: no rows carry teacher context — was the farm run with --rfly?")

    tr_mask, va_mask, n_runs, n_val_runs = split_by_run(rows, args.val_frac, args.seed)
    print(f"  runs: {n_runs} -> {n_val_runs} val / {n_runs-n_val_runs} train; "
          f"rows {tr_mask.sum()} train / {va_mask.sum()} val")

    mu = obs_np[tr_mask].mean(axis=0)
    sd = obs_np[tr_mask].std(axis=0)
    sd = np.where(sd < 1e-8, 1.0, sd)

    obs_t = torch.tensor((obs_np - mu) / sd, dtype=torch.float32, device=dev)
    th_t = torch.tensor(th_np, dtype=torch.float32, device=dev)
    tr_idx = torch.tensor(np.where(tr_mask)[0], device=dev)
    va_idx = torch.tensor(np.where(va_mask)[0], device=dev) if va_mask.any() else None

    lo = torch.tensor(RT_LO, dtype=torch.float32, device=dev)
    hi = torch.tensor(RT_HI, dtype=torch.float32, device=dev)

    # NOTE (M4 push): overfitting is θ̂'s ceiling (best-val at epoch 1-2; effective sample size is
    # RUNS not rows). The fixes here are architecture-NEUTRAL so export_theta's body.0/2/4 keys stay
    # valid: weight_decay (--weight-decay, AdamW) + optional smaller --hidden. Dropout would shift the
    # Sequential indices and break the exporter, so it is deliberately not added.
    class ThetaPrior(nn.Module):
        def __init__(self, n_in, hidden, layers):
            super().__init__()
            mods, d = [], n_in
            for _ in range(layers):
                mods += [nn.Linear(d, hidden), nn.Tanh()]; d = hidden
            self.body = nn.Sequential(*mods)
            self.head = nn.Linear(d, len(RT_LO))

        def forward(self, x):
            u = torch.tanh(self.head(self.body(x)))          # [-1,1]^10
            return lo + (hi - lo) * 0.5 * (u + 1.0)          # -> the RT box, always

    net = ThetaPrior(obs_np.shape[1], args.hidden, args.layers).to(dev)
    opt = torch.optim.AdamW(net.parameters(), lr=args.lr, weight_decay=args.weight_decay)

    # ---- SCALE-FREE ERROR. The ten coordinates have very different ranges (EKR spans 0.3-4.0,
    # TGTLEAD 0.0-1.5), so a raw MSE would be dominated by whichever happens to be widest. Score in
    # units of each coordinate's own box width, so "0.1" means the same thing on every axis.
    width = torch.tensor(RT_HI - RT_LO, dtype=torch.float32, device=dev)

    def nrmse(pred, targ):
        return torch.sqrt((((pred - targ) / width) ** 2).mean(dim=0))

    def evaluate(idx):
        net.eval()
        with torch.no_grad():
            return nrmse(net(obs_t[idx]), th_t[idx]).cpu().numpy()

    # ---- THE TWO BASELINES THIS MUST BEAT, computed up front and printed with every result.
    # (1) IDENTITY: what the CEM warm-starts from today. Beating it is the whole point.
    # (2) MEAN: predicting the training-set mean theta, i.e. learning the average tuning and nothing
    #     state-dependent. A net that beats identity but NOT the mean has learned "the oracle usually
    #     wants roughly this", which is a constant — cheaper to hardcode than to ship a net for. That
    #     gap is the honest signal, and it is exactly the check LODESTAR's flat learning curve failed.
    with torch.no_grad():
        ident = torch.tensor(RT_IDENTITY, dtype=torch.float32, device=dev).expand_as(th_t)
        base_ident = nrmse(ident[tr_idx], th_t[tr_idx]).cpu().numpy()
        mean_th = th_t[tr_idx].mean(dim=0, keepdim=True).expand_as(th_t)
        base_mean = nrmse(mean_th[tr_idx], th_t[tr_idx]).cpu().numpy()
    print(f"[baseline] identity nrmse mean={base_ident.mean():.4f}  per-axis={np.round(base_ident,3)}")
    print(f"[baseline] mean-th  nrmse mean={base_mean.mean():.4f}  per-axis={np.round(base_mean,3)}")

    # ---- BASELINE 3, and the one that matters most here: THE UNTRAINED NET. -----------------------
    # This net's output layer is tanh de-normalized into the RT box, so an untrained net emits close
    # to the box MIDPOINT for every input — which may already score well against a theta distribution
    # that clusters mid-box. Without this measurement, an "early stopping beats the baselines!" result
    # is indistinguishable from "the initialization happened to sit near the answer". Measure it
    # before a single gradient step and make the comparison explicit.
    base_init = evaluate(va_idx if va_idx is not None else tr_idx)
    print(f"[baseline] UNTRAINED net nrmse mean={base_init.mean():.4f} "
          f"(if the best trained epoch does not clearly beat THIS, nothing was learned)")

    # ---- Best-val checkpointing. With theta near-constant within a run, the effective sample size is
    # the number of RUNS, not rows, so this net overfits within a couple of epochs. Keeping the final
    # epoch would report the overfit model; keep the best-val one and report which epoch it came from.
    best = {"val": float("inf"), "ep": -1, "state": None}

    n_tr = tr_idx.numel()
    print(f"[train_theta] params={sum(p.numel() for p in net.parameters())} n_in={obs_np.shape[1]}")
    t0 = time.time()
    for ep in range(args.epochs):
        net.train()
        perm = tr_idx[torch.randperm(n_tr, device=dev)]
        for i in range(0, n_tr, args.batch):
            bi = perm[i:i + args.batch]
            loss = (((net(obs_t[bi]) - th_t[bi]) / width) ** 2).mean()
            opt.zero_grad(); loss.backward(); opt.step()
        # evaluate EVERY epoch (it is cheap) so best-val selection is not aliased by the print cadence
        if va_idx is not None:
            v = float(evaluate(va_idx).mean())
            if v < best["val"]:
                best = {"val": v, "ep": ep + 1,
                        "state": {k: t.detach().clone() for k, t in net.state_dict().items()}}
        if (ep + 1) % max(1, args.epochs // 10) == 0 or ep == 0:
            tr_e = evaluate(tr_idx)
            msg = f"  ep {ep+1:4d}/{args.epochs}  train_nrmse={tr_e.mean():.4f}"
            if va_idx is not None:
                msg += f"  val_nrmse={evaluate(va_idx).mean():.4f}"
            print(msg)
    dt = time.time() - t0

    # restore the best-val weights — the final epoch is the overfit one
    if best["state"] is not None:
        net.load_state_dict(best["state"])
        print(f"[train_theta] restored BEST-VAL weights from epoch {best['ep']} "
              f"(val_nrmse {best['val']:.4f}); the final epoch was NOT kept")

    val_e = evaluate(va_idx) if va_idx is not None else np.full(len(RT_LO), np.nan)
    tr_e = evaluate(tr_idx)

    torch.save({
        "kind": "theta_prior", "n_in": obs_np.shape[1], "hidden": args.hidden,
        "layers": args.layers, "n_out": len(RT_LO),
        "state_dict": {k: v.detach().cpu().double().numpy() for k, v in net.state_dict().items()},
        "in_mu": mu.astype(np.float64), "in_sd": sd.astype(np.float64),
        "rt_lo": RT_LO, "rt_hi": RT_HI,
        "val_nrmse": val_e.astype(np.float64), "train_nrmse": tr_e.astype(np.float64),
        "baseline_identity_nrmse": base_ident.astype(np.float64),
        "baseline_mean_nrmse": base_mean.astype(np.float64),
        "theta_names": rf.THETA_NAMES,
        "n_rows": int(rows.shape[0]), "n_runs": int(n_runs), "train_seconds": dt,
    }, args.out)

    print(f"[train_theta] SAVED {args.out}")
    print(f"[metrics] val_nrmse={val_e.mean():.4f} (best epoch {best['ep']}) vs identity "
          f"{base_ident.mean():.4f} vs mean-theta {base_mean.mean():.4f} vs UNTRAINED {base_init.mean():.4f}")
    # The verdict, stated plainly rather than left for someone to infer from four numbers. Ordered
    # from the most damning check to the least: an untrained net that already scores well means the
    # box midpoint is simply a decent guess, and any "win" over identity is an artifact of the output
    # parameterisation rather than anything learned from the observation.
    if val_e.mean() >= base_init.mean() * 0.98:
        print("[VERDICT] NO-GO (nothing learned): no better than an UNTRAINED net, whose tanh de-norm "
              "already emits the mid-box theta. Any apparent edge over identity is the parameterisation, "
              "not the observation.")
    elif val_e.mean() >= base_ident.mean():
        print("[VERDICT] NO-GO: the prior is no better than the identity warm start it would replace.")
    elif val_e.mean() >= base_mean.mean():
        print("[VERDICT] WEAK: beats identity but not a constant mean theta — ship the constant, not a net.")
    else:
        print("[VERDICT] GO: state-dependent and better than every baseline. Wire it as the CEM warm start "
              "and measure the REAL metric — rollouts-to-basin vs identity, which is what it exists to cut.")


if __name__ == "__main__":
    main()
