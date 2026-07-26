# trainer/ — S0 distillation precompute (N1)

**This directory is OFFLINE PRECOMPUTE. It never ships and never runs in the sim path.** Its only
product is a frozen fp64 C weights header (`core/neural_policy_weights.h`) consumed bit-exactly by
`GM_NEURAL`. Gitignored except the exported header (canon repo layout, CLAUDE_v2.md §3).

## The precompute reconciliation (canon §2 tooling rule + §19.1; neural_policy_design.md §E.1)

The canon's C-only rule (§2) governs **the sim and every shipped byte** — those are C/C++/CUDA, always.
The **trainer is PRECOMPUTE**: it produces a *data artifact* (float arrays in a `.h`), exactly like a
golden file, a swept coefficient (`KDIV_SEEK`), or `ceiling.c`'s `D_phys`. Because its output is data,
not project code — and because it never runs in the deterministic loop, never ships, and never touches
the `memcmp` oracle — it is allowed to be Python/PyTorch (the autodiff/RL ecosystem lives there and it
runs on the GPU, not the sim CPU). The line the whole project draws (§20, directive 11) is *precompute
in, telemetry out*: **training may be nondeterministic; the shipped inference is a hand-rolled
fixed-order fp64 C forward pass that is bit-deterministic and golden'd.** `export_weights.py` is the
freeze; `core/guidance_neural.c` is the shipped inference. The C-only rule is honored where it matters.

## The pipeline (S0 = distill the MPPI expert to MPPI quality at NN speed)

```
  MPPI teacher run (C)                 trainer (Python precompute)              shipped (C)
  --headless ... --mppi                                                         GM_NEURAL
       --policy-log data.bin  ──►  train_s0.py  ──►  ckpt.pt  ──►  export_weights.py  ──►  neural_policy_weights.h
       (o, a*) rows                (imitation)      (frozen θ)     (fp64 C header)          (fixed-order fp64 fwd pass)
```

Row format: `rowformat.py` mirrors `core/policy_tap.h` + `core/policy_obs.h` field-for-field (30 obs
ingredients + the 3-channel executed command a*). If the C layout changes, change `rowformat.py` in
lockstep — the interface is FROZEN (App-G); a change is a re-architecture event (a new `NP_VERSION`).

## THE HELD-OUT LAW (canon §13.6.3 / §17 — ABSOLUTE, enforced in code)

Gate seeds **42, 7, 99 are NEVER in a training set.** `train_s0.py` hard-refuses (exit 1) any `.bin`
whose rows carry a gate seed — the generalization proof is that the policy is *gated* on 42/7/99 having
never trained on them. Generate training data with a disjoint range (e.g. seeds 1000–9999).

## Files

| file | role |
|---|---|
| `rowformat.py`     | the row-layout constant + numpy reader (mirror of `policy_tap.h`/`policy_obs.h`). `python rowformat.py <file.bin>` prints a summary + sanity round-trip. |
| `train_s0.py`      | load one-or-many `.bin` logs; App-G features; normalize with train-set (μ,σ) frozen into the ckpt; MLP `N→128→128→128→3` (tanh hidden; a_lat de-norm ±3.2, throttle [0.40,1]); per-channel-weighted MSE; **train/val split BY RUN**; CUDA if available; saves ckpt + a val-MSE-per-channel metrics line; **refuses gate seeds**. |
| `export_weights.py`| ckpt → `core/neural_policy_weights.h` per §F.1 EXACTLY (fp64, row-major, `%.17g`, generation stamp = weights hash + val MSE + date). |

## Run instructions

Data-gen is done by the main session (the farm), not here. Once `.bin` logs exist under `data/s0/`:

```bash
# 1) train (offline; CUDA auto-detected). NEVER point --data at a gate-seed (42/7/99) file.
python trainer/train_s0.py --data data/s0 --out runs/s0.pt --epochs 200 --hidden 128 --seed 1234

# 2) freeze -> the shipped C header (regenerating it is an ADR event: new NP_VERSION + KAT + re-golden)
python trainer/export_weights.py --ckpt runs/s0.pt --out core/neural_policy_weights.h

# 3) rebuild the C tree; the selftest NP KAT will FAIL until you regenerate its expected vector from
#    the new header (the KAT is header-versioned by construction — see core/tests / cmd_selftest).
```

Argparse (train): `--data` (file|glob|dir, repeatable) `--out` `--epochs` `--hidden` `--layers`
`--seed` `--val-frac` `--batch` `--lr` `--w-alat` `--w-throttle`.
Argparse (export): `--ckpt` `--out` `--np-version`.

## Environment

torch 2.9.1+cu128 on Python 3.13 (this machine, RTX 4070 Ti SUPER sm_89, CUDA available). numpy 2.4.2.
The trainer's own environment is outside the core's zero-dep rule but inside the ADR log (§2).
