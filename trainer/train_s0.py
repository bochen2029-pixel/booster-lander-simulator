"""train_s0.py — S0 DISTILLATION trainer (N1). OFFLINE PRECOMPUTE, the ONLY sanctioned Python.

Canon reconciliation (CLAUDE_v2.md §2, §19.1; neural_policy_design.md §E.1): the SIM and every
SHIPPED byte are C/C++/CUDA. This trainer is PRECOMPUTE — it consumes the teacher tap's (o, a*) rows
and produces a frozen fp64 C weights header (via export_weights.py). Its output is DATA, like a golden
or a swept constant; its code never runs in the sim path, never ships, never touches determinism.
Training MAY be nondeterministic (GPU, shuffling); the SHIPPED artifact is frozen and re-golden'd.

WHAT IT DOES (neural_policy_design.md §B.1, §B.4, §C.3, §D intro):
  * load one-or-many .bin tap logs (rowformat.py);
  * ENFORCE THE HELD-OUT LAW (§13.6.3, ABSOLUTE): REFUSE any file whose rows carry seed in {42,7,99}
    — the gate seeds are NEVER in a training set. This is a hard error, in code, not a convention;
  * build the App-G observation features; normalize with (mu, sd) COMPUTED FROM THE TRAINING SET and
    saved with the checkpoint (they FREEZE into the export — part of the deterministic artifact);
  * an MLP per §B.4: input N -> hidden -> hidden -> hidden -> 3, tanh hidden; outputs tanh-bounded and
    de-normalized to a_lat in +-A_LAT_GAMUT and throttle in [ENG_THR_MIN, 1] (the real tree constants,
    baked in below);
  * per-channel-weighted MSE (a_lat channels weighted vs throttle to comparable units, §C.3);
  * train/val split BY RUN (not by row) — no leakage of a trajectory across the split;
  * CUDA if available; save checkpoint + a metrics line (val MSE per channel).

Argparse: --data (file or glob or dir, repeatable) --out --epochs --hidden --seed.

The distillation loss is imitation error ||pi(o) - a*||^2_W (neural_policy_design.md §D intro, §B.1).
"""

from __future__ import annotations
import argparse, glob, os, sys, json, time
import numpy as np

import rowformat as rf

# ---- REAL TREE CONSTANTS (baked in; read from the source of truth, cited) --------------------------
A_LAT_GAMUT = 3.2    # core/guidance_mppi.c:40  — physical lateral-accel gamut [m/s^2] (= a_vert_ref*tan15)
ENG_THR_MIN = 0.40   # core/constants.h:38      — minimum engine throttle
NP_VERSION_TRAINED = 1  # the version this trainer targets; export_weights.py stamps the header

# ---- THE HELD-OUT LAW (canon §13.6.3 / §17): gate seeds NEVER trained on ---------------------------
HELD_OUT_SEEDS = {42, 7, 99}

# ---- VERDICT CODES (mirror of core/state.h `enum { V_NONE, V_PERFECT, ... }`) ----------------------
V_NONE, V_PERFECT, V_GOOD, V_HARD, V_TIPPED, V_CRASHED = 0, 1, 2, 3, 4, 5
V_NAME = {0: "NONE", 1: "PERFECT", 2: "GOOD", 3: "HARD", 4: "TIPPED", 5: "CRASHED"}


def load_verdicts(specs):
    """Read headless --out CSV manifests -> {(seed, run): verdict}.

    The CSV contract (core/main.c cmd_headless) is
        seed,scenario,run,verdict,fault,td_v,td_lat,...
    and its `run` column is the SAME 1-based index the tap writes into row col 2, so (seed, run)
    joins the two exactly.
    """
    import csv as _csv
    files = []
    for s in specs:
        if os.path.isdir(s):
            files += sorted(glob.glob(os.path.join(s, "*.csv")))
        elif any(ch in s for ch in "*?["):
            files += sorted(glob.glob(s))
        else:
            files.append(s)
    verdicts = {}
    for p in files:
        with open(p, newline="") as fh:
            for rec in _csv.DictReader(fh):
                verdicts[(int(rec["seed"]), int(rec["run"]))] = int(rec["verdict"])
    if not verdicts:
        sys.exit(f"error: --verdict-csv matched no usable CSV rows: {specs}")
    print(f"  verdict manifest: {len(verdicts)} runs from {len(files)} csv file(s)")
    return verdicts


def verdict_filter(rows, verdicts, keep):
    """Keep only rows belonging to runs that ACTUALLY LANDED at the accepted grade.

    WHY THIS IS NOT OPTIONAL (oracle_distill_design.md §2, "verdict-filter EVERY oracle trajectory"):
    the teacher is a SEARCH, and a search that fails still emits a full trajectory of confident-looking
    commands. Training on its failures teaches the policy to reproduce them. Filtering on the verdict
    the byte-exact plant actually returned makes the dataset sim-executable BY CONSTRUCTION — every
    retained trajectory is one that provably worked.

    A run with no manifest entry is DROPPED, not kept: an unknown outcome is not evidence of success.
    """
    key = rows[:, [rf.I_SEED, rf.I_RUN]].astype(np.int64)
    keys = list(map(tuple, key.tolist()))
    mask = np.array([verdicts.get(k, -1) in keep for k in keys], dtype=bool)
    # report what the filter did, by grade — this is the honest yield number for the ADR
    tally, missing = {}, 0
    for k in set(keys):
        v = verdicts.get(k, None)
        if v is None:
            missing += 1
        else:
            tally[v] = tally.get(v, 0) + 1
    kept_runs = sum(n for v, n in tally.items() if v in keep)
    print(f"  verdict filter: keeping {V_NAME.get(min(keep), '?')}+ "
          f"({sorted(keep)}) -> {kept_runs}/{len(set(keys))} runs, "
          f"{int(mask.sum())}/{len(mask)} rows")
    print("    by grade: " + ", ".join(f"{V_NAME.get(v, v)}={n}" for v, n in sorted(tally.items()))
          + (f", NO-MANIFEST={missing}" if missing else ""))
    if not mask.any():
        sys.exit("error: verdict filter removed every row — check the CSV/bin (seed,run) join.")
    return rows[mask]


def _iter_bin_files(specs):
    """Expand --data specs (file | glob | directory) into a de-duplicated list of .bin paths."""
    out = []
    for s in specs:
        if os.path.isdir(s):
            out += sorted(glob.glob(os.path.join(s, "*.bin")))
        elif any(ch in s for ch in "*?["):
            out += sorted(glob.glob(s))
        else:
            out.append(s)
    seen, uniq = set(), []
    for p in out:
        ap = os.path.abspath(p)
        if ap not in seen and os.path.isfile(ap):
            seen.add(ap); uniq.append(ap)
    return uniq


def load_dataset(specs):
    """Load and concatenate all tap rows, ENFORCING the held-out law per file. Returns the raw rows."""
    files = _iter_bin_files(specs)
    if not files:
        sys.exit(f"error: --data matched no .bin files: {specs}")
    all_rows = []
    for p in files:
        rows = rf.read_rows(p)
        seeds = set(int(s) for s in rf.seeds_in(rows).tolist())
        # THE HELD-OUT LAW, ENFORCED IN CODE (a hard refusal, not a warning).
        bad = seeds & HELD_OUT_SEEDS
        if bad:
            sys.exit(
                f"error: HELD-OUT LAW VIOLATION (canon §13.6.3): '{p}' contains gate seed(s) "
                f"{sorted(bad)} — seeds {sorted(HELD_OUT_SEEDS)} are NEVER allowed in a training set. "
                f"Generate training data with disjoint seeds (e.g. 1000-9999) and GATE on 42/7/99."
            )
        print(f"  loaded {p}: {rows.shape[0]} rows, seeds {sorted(seeds)}")
        all_rows.append(rows)
    rows = np.concatenate(all_rows, axis=0)
    print(f"  total: {rows.shape[0]} rows from {len(files)} file(s)")
    return rows


def split_by_run(rows, val_frac, seed):
    """Split rows into train/val BY RUN (a whole (seed,run) trajectory goes to one side)."""
    key = rows[:, [rf.I_SEED, rf.I_RUN]].astype(np.int64)
    uniq = np.unique(key, axis=0)
    rng = np.random.default_rng(seed)
    perm = rng.permutation(len(uniq))
    n_val = max(1, int(round(len(uniq) * val_frac))) if len(uniq) > 1 else 0
    val_keys = set(map(tuple, uniq[perm[:n_val]].tolist()))
    is_val = np.array([tuple(k) in val_keys for k in key.tolist()], dtype=bool)
    return ~is_val, is_val, len(uniq), n_val


def main():
    ap = argparse.ArgumentParser(description="S0 distillation trainer (offline precompute).")
    ap.add_argument("--data", nargs="+", required=True,
                    help="one or more .bin tap files, globs, or directories")
    ap.add_argument("--out", required=True, help="output checkpoint path (.pt)")
    ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--hidden", type=int, default=128, help="hidden width (§B.4: 64-128)")
    ap.add_argument("--layers", type=int, default=3, help="hidden layer count (§B.4: 3-4)")
    ap.add_argument("--seed", type=int, default=1234, help="training RNG seed (split + init)")
    ap.add_argument("--val-frac", type=float, default=0.15, help="fraction of RUNS held for validation")
    ap.add_argument("--batch", type=int, default=4096)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--w-alat", type=float, default=1.0,
                    help="per-channel MSE weight on each a_lat channel (§C.3 comparable-units)")
    ap.add_argument("--w-throttle", type=float, default=1.0,
                    help="per-channel MSE weight on throttle (§C.3)")
    ap.add_argument("--verdict-csv", nargs="+", default=[],
                    help="headless --out CSV manifest(s) (file|glob|dir). When given, ONLY rows from "
                         "runs that landed at an accepted grade are trained on (oracle-distill: the "
                         "teacher is a search, and its FAILURES must never become labels).")
    ap.add_argument("--keep-verdicts", default="1,2",
                    help="verdict codes kept by --verdict-csv (1=PERFECT 2=GOOD 3=HARD); default 1,2")
    args = ap.parse_args()

    import torch
    import torch.nn as nn

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"[train_s0] device={dev} torch={torch.__version__}")

    rows = load_dataset(args.data)
    if args.verdict_csv:
        keep = set(int(x) for x in args.keep_verdicts.split(","))
        rows = verdict_filter(rows, load_verdicts(args.verdict_csv), keep)
    obs_np, act_np = rf.split(rows)            # obs (n, NPOBS_N), act (n, 3) = [a_lat0, a_lat1, thr]

    # ---- LABEL CONDITIONING (§C.3): the EXECUTED a_lat intent can exceed the physical gamut (the
    # tilt-cap clamps it in control.c downstream). Clamp the a_lat LABELS to +-A_LAT_GAMUT so the
    # imitation target is exactly what reaches the plant and lies inside the policy's tanh-de-norm
    # output range (the D-009 lesson: railing beyond the gamut kills the gradient). Throttle is
    # already in {0} U [ENG_THR_MIN, 1]; leave it (0 = engine off, a legal commanded value). ----
    act_np = act_np.copy()
    act_np[:, 0] = np.clip(act_np[:, 0], -A_LAT_GAMUT, A_LAT_GAMUT)
    act_np[:, 1] = np.clip(act_np[:, 1], -A_LAT_GAMUT, A_LAT_GAMUT)

    tr_mask, va_mask, n_runs, n_val_runs = split_by_run(rows, args.val_frac, args.seed)
    print(f"  runs: {n_runs} total -> {n_val_runs} val / {n_runs-n_val_runs} train; "
          f"rows {tr_mask.sum()} train / {va_mask.sum()} val")

    # ---- Normalization (mu, sd) FROM THE TRAINING SET ONLY (frozen into the export, §C.3). --------
    obs_tr = obs_np[tr_mask]
    mu = obs_tr.mean(axis=0)
    sd = obs_tr.std(axis=0)
    sd = np.where(sd < 1e-8, 1.0, sd)   # guard constant features (e.g. nominal target fields at N0)

    # ---- Target (action) de-normalization spec, mirrored in the C forward pass (§F.1 NP_OUT_SCALE):
    #   net emits tanh(.) in [-1,1] per channel; a_lat = A_LAT_GAMUT * u ; throttle = 0.5*(u+1) then
    #   clamped to [ENG_THR_MIN,1]. We invert this to make the TRAINING TARGET in the net's raw
    #   (pre-de-norm) space, so the loss is on the de-normalized physical channels directly instead
    #   (simpler + matches export): we de-normalize the net output and compute MSE in physical units.
    A_out = np.array([A_LAT_GAMUT, A_LAT_GAMUT, 1.0], dtype=np.float64)  # informational (export uses it)

    obs_t = torch.tensor((obs_np - mu) / sd, dtype=torch.float32, device=dev)
    act_t = torch.tensor(act_np, dtype=torch.float32, device=dev)
    tr_idx = torch.tensor(np.where(tr_mask)[0], device=dev)
    va_idx = torch.tensor(np.where(va_mask)[0], device=dev) if va_mask.any() else None

    n_in = obs_np.shape[1]
    assert n_in == rf.NPOBS_N, f"obs width drift: dataset has {n_in}, rowformat says {rf.NPOBS_N}"

    # ---- The MLP (§B.4): n_in -> [hidden]*layers (tanh) -> 3 ; output head de-normalized in forward.
    class Policy(nn.Module):
        def __init__(self, n_in, hidden, layers):
            super().__init__()
            mods, d = [], n_in
            for _ in range(layers):
                mods += [nn.Linear(d, hidden), nn.Tanh()]
                d = hidden
            self.body = nn.Sequential(*mods)
            self.head = nn.Linear(d, 3)
            self.register_buffer("a_scale", torch.tensor([A_LAT_GAMUT, A_LAT_GAMUT, 1.0]))

        def forward(self, x):
            u = torch.tanh(self.head(self.body(x)))   # [-1,1]^3
            a_lat = self.a_scale[:2] * u[:, :2]        # +-A_LAT_GAMUT
            thr = ENG_THR_MIN + (1.0 - ENG_THR_MIN) * 0.5 * (u[:, 2:3] + 1.0)  # [ENG_THR_MIN,1]
            return torch.cat([a_lat, thr], dim=1)

    net = Policy(n_in, args.hidden, args.layers).to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=args.lr)
    wch = torch.tensor([args.w_alat, args.w_alat, args.w_throttle], dtype=torch.float32, device=dev)

    def weighted_mse(pred, targ):
        return (wch * (pred - targ) ** 2).mean()

    def per_channel_mse(idx):
        net.eval()
        with torch.no_grad():
            p = net(obs_t[idx]); t = act_t[idx]
            return ((p - t) ** 2).mean(dim=0).cpu().numpy()

    n_tr = tr_idx.numel()
    print(f"[train_s0] params={sum(p.numel() for p in net.parameters())}  n_in={n_in} hidden={args.hidden} layers={args.layers}")
    t0 = time.time()
    for ep in range(args.epochs):
        net.train()
        perm = tr_idx[torch.randperm(n_tr, device=dev)]
        tot = 0.0
        for i in range(0, n_tr, args.batch):
            bi = perm[i:i + args.batch]
            pred = net(obs_t[bi])
            loss = weighted_mse(pred, act_t[bi])
            opt.zero_grad(); loss.backward(); opt.step()
            tot += float(loss) * bi.numel()
        if (ep + 1) % max(1, args.epochs // 10) == 0 or ep == 0:
            tr_mse = per_channel_mse(tr_idx)
            msg = f"  ep {ep+1:4d}/{args.epochs}  train_wmse={tot/n_tr:.5e}  tr_mse[alat0,alat1,thr]={tr_mse}"
            if va_idx is not None:
                va_mse = per_channel_mse(va_idx)
                msg += f"  val_mse={va_mse}"
            print(msg)
    dt = time.time() - t0

    val_mse = per_channel_mse(va_idx) if va_idx is not None else np.array([float("nan")] * 3)
    tr_mse = per_channel_mse(tr_idx)

    # ---- Save the checkpoint (weights + FROZEN normalization + metadata). export_weights.py reads it.
    ckpt = {
        "np_version": NP_VERSION_TRAINED,
        "n_in": n_in, "hidden": args.hidden, "layers": args.layers, "n_out": 3,
        "state_dict": {k: v.detach().cpu().double().numpy() for k, v in net.state_dict().items()
                       if k != "a_scale"},
        "in_mu": mu.astype(np.float64),
        "in_sd": sd.astype(np.float64),
        "out_scale": A_out.astype(np.float64),   # a_lat gamut on ch0/1, throttle handled by de-norm
        "eng_thr_min": ENG_THR_MIN,
        "a_lat_gamut": A_LAT_GAMUT,
        "obs_names": rf.OBS_NAMES,
        "act_names": rf.ACTION_NAMES,
        "val_mse": val_mse.astype(np.float64),
        "train_mse": tr_mse.astype(np.float64),
        "n_rows": int(rows.shape[0]), "n_runs": int(n_runs), "n_val_runs": int(n_val_runs),
        "train_seconds": dt, "trainer_seed": args.seed,
    }
    import torch as _t
    _t.save(ckpt, args.out)
    # a metrics line (the gate readout): val MSE per channel
    print(f"[train_s0] SAVED {args.out}")
    print(f"[metrics] val_mse a_lat0={val_mse[0]:.6e} a_lat1={val_mse[1]:.6e} throttle={val_mse[2]:.6e} "
          f"| train_seconds={dt:.1f} | rows={rows.shape[0]} runs={n_runs}")


if __name__ == "__main__":
    main()
