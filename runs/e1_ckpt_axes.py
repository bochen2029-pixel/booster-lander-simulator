"""Print per-coordinate held-out nrmse of theta-prior checkpoints (train_theta.py saves it), beside the
identity and mean-theta baselines, so the coordinates the net fails to learn are visible.
Usage: python runs/e1_ckpt_axes.py <ckpt.pt> [<ckpt.pt> ...]"""
import sys
import numpy as np
import torch

NAMES = ["EKR","EKV","EBANK","ADECEL","TLEAD","KDIV","KVNEAR","IGNM","TGTLEAD","KV"]
for p in sys.argv[1:]:
    ck = torch.load(p, map_location="cpu", weights_only=False)
    v = np.asarray(ck["val_nrmse"]); i = np.asarray(ck["baseline_identity_nrmse"]); m = np.asarray(ck["baseline_mean_nrmse"])
    print(f"\n{p}  runs={ck.get('n_runs')}  val mean={v.mean():.4f}")
    print("  coord     val_nrmse  mean-theta  identity   ratio val/mean")
    for k, nm in enumerate(NAMES):
        print(f"  {nm:8s}  {v[k]:9.4f}  {m[k]:10.4f}  {i[k]:8.4f}   {v[k]/m[k] if m[k]>0 else float('nan'):.2f}")
