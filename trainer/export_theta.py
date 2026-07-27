"""export_theta.py — freeze a theta-prior checkpoint into core/theta_policy_weights.h (R2, D-042).

The SECOND net of the AlphaZero structure: obs(39, App-G v2) -> theta_hat[10], de-normalized into the
RT box [RT_LO, RT_HI] (guidance_rfly.c). It drives GM_RFLY's gcmd.rt each tick IN PLACE of the CEM —
a 10 us gain schedule where D-040 spent ~150 s of search. Same freeze discipline as the policy net
(neural_policy_design.md F.1): fp64, row-major, %.17g, a fixed j-outer/i-inner loop order in C for
bit-determinism. Distinct header + guard from neural_policy_weights.h — the two nets never collide.

The theta net's de-norm MIRRORS train_theta.py ThetaPrior.forward EXACTLY:
    u = tanh(head(body(obs_normalized)));  theta = RT_LO + (RT_HI - RT_LO) * 0.5 * (u + 1)

Usage: python export_theta.py --ckpt runs/theta_prior_full.pt --out ../core/theta_policy_weights.h
"""
from __future__ import annotations
import argparse, hashlib, os, datetime, sys
import numpy as np


def fmt_vec(name, arr):
    vals = ", ".join(f"{x:.17g}" for x in np.asarray(arr, np.float64).ravel())
    return f"static const double {name}[{np.asarray(arr).shape[0]}] = {{ {vals} }};\n"


def fmt_mat(name, mat):
    mat = np.asarray(mat, np.float64); r, c = mat.shape
    lines = [f"static const double {name}[{r}][{c}] = {{"]
    for row in mat:
        lines.append("  { " + ", ".join(f"{x:.17g}" for x in row) + " },")
    lines.append("};\n")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tp-version", type=int, default=1)
    args = ap.parse_args()

    import torch
    ck = torch.load(args.ckpt, map_location="cpu", weights_only=False)
    if ck.get("kind") != "theta_prior":
        sys.exit(f"error: {args.ckpt} is not a theta_prior checkpoint (kind={ck.get('kind')})")
    n_in, hid, layers, n_out = ck["n_in"], ck["hidden"], ck["layers"], ck["n_out"]
    if layers != 3:
        sys.exit(f"error: exporter specialized to 3 layers (got {layers})")

    sd = ck["state_dict"]
    W0, B0 = sd["body.0.weight"], sd["body.0.bias"]
    W1, B1 = sd["body.2.weight"], sd["body.2.bias"]
    W2, B2 = sd["body.4.weight"], sd["body.4.bias"]
    Wo, Bo = sd["head.weight"], sd["head.bias"]
    assert W0.shape == (hid, n_in) and Wo.shape == (n_out, hid), (W0.shape, Wo.shape)
    mu, sdv, lo, hi = ck["in_mu"], ck["in_sd"], ck["rt_lo"], ck["rt_hi"]

    h = hashlib.sha256()
    for a in (W0, B0, W1, B1, W2, B2, Wo, Bo, mu, sdv, lo, hi):
        h.update(np.asarray(a, np.float64).tobytes())
    sha = h.hexdigest()[:16]
    vnr = np.asarray(ck.get("val_nrmse", [float("nan")]), np.float64).mean()

    L = [f"/* theta_policy_weights.h — FROZEN theta-prior (GM_RFLY gain schedule, R2/D-042). GENERATED",
         " * by trainer/export_theta.py. A versioned precompute artifact (canon 20). fp64, row-major,",
         " * %.17g; the C forward pass reads W[j][i] j-outer/i-inner for bit-determinism.",
         f" * STAMP: date={datetime.date.today().isoformat()} TP_VERSION={args.tp_version} sha={sha}",
         f" *   val_nrmse={vnr:.4f}  n_runs={ck.get('n_runs','?')}  ckpt={os.path.basename(args.ckpt)} */",
         "#ifndef BL_THETA_POLICY_WEIGHTS_H", "#define BL_THETA_POLICY_WEIGHTS_H", "",
         f"#define TP_VERSION   {args.tp_version}", f"#define TP_N_IN      {n_in}",
         f"#define TP_N_HID     {hid}", f"#define TP_N_LAYERS  {layers}", f"#define TP_N_OUT     {n_out}", "",
         "/* frozen input normalization (mu, sd) — from the training set */",
         fmt_vec("TP_IN_MU", mu), fmt_vec("TP_IN_SD", sdv),
         "/* de-norm target box: theta = TP_RT_LO + (TP_RT_HI-TP_RT_LO)*0.5*(tanh+1) */",
         fmt_vec("TP_RT_LO", lo), fmt_vec("TP_RT_HI", hi), "",
         "/* layer 0 */", fmt_mat("TP_W0", W0), fmt_vec("TP_B0", B0),
         "/* layer 1 */", fmt_mat("TP_W1", W1), fmt_vec("TP_B1", B1),
         "/* layer 2 */", fmt_mat("TP_W2", W2), fmt_vec("TP_B2", B2),
         "/* output head */", fmt_mat("TP_W_OUT", Wo), fmt_vec("TP_B_OUT", Bo), "",
         "#endif /* BL_THETA_POLICY_WEIGHTS_H */"]
    with open(args.out, "w", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")
    print(f"[export_theta] wrote {args.out}  TP_VERSION={args.tp_version} sha={sha} val_nrmse={vnr:.4f}")


if __name__ == "__main__":
    main()
