"""
E8 — what the plant's own candidate log says a proposer is worth, per replan.

Reads the 71-column rows of runs/e8_cand_farm.* (guidance_rfly.h RFLY_CAND_ROW) and, for every
teacher replan, asks: if the plant had rolled out only the carried elite plus k of the sampler's
own draws, would the pick have landed? The CEM writes iteration 0's population in index order,
and slot 0 IS the carried elite (cand[0] = gtheta = the incoming mean), so "elite + k draws" is
exact from the log, no resampling. The random-proposer figure is the exact expectation over every
k-subset of the seven draws. This is the per-replan shadow of --rfly-rollouts k+1; the flight
compounds it over ~14 replans.

With --critic FILE (an exported E8 critic), the same question for a critic as the PROPOSER: the
critic ranks the seven draws, the plant rolls the elite plus the critic's top k. Scored only on
the trainer's held-out runs (the same by-run split, --seed/--val_frac as e8_train_critic.py), so
the critic never grades rows it was fitted on. Critic-proposes minus random-proposes at the same k
is the per-replan preview of arm De minus --rfly-rollouts k+1.

    python runs/e8_cand_stats.py DIR [DIR ...] [--critic FILE] [--seed 0] [--val_frac 0.15]

Safe on a live farm: reads floor(size/568) rows and drops each file's last replan group, which
the writer may still be filling. Held-out law: refuses files carrying seeds 42/7/99.
"""
import argparse, glob, itertools, os, sys
import numpy as np

NCOL = 71
C_T, C_SEED, C_RUN, C_BIG, C_IT, C_DES = 0, 1, 2, 3, 4, 5
C_OBS, C_MEAN, C_CAND, C_COST, C_LANDED = 6, 45, 55, 65, 66


def replans(path):
    n = os.path.getsize(path) // (NCOL * 8)
    a = np.fromfile(path, dtype=np.float64, count=n * NCOL).reshape(n, NCOL)
    if np.isin(a[:, C_SEED].astype(int), [42, 7, 99]).any():
        sys.exit(f"held-out law: gate seeds in {path}")
    key = a[:, C_RUN] * 1e6 + np.round(a[:, C_T] * 10)      # a replan = consecutive rows sharing (run, t)
    cut = np.r_[0, np.flatnonzero(np.diff(key) != 0) + 1, len(a)]
    out = []
    for s, e in zip(cut[:-1], cut[1:]):
        g = a[s:e]
        if ((g[:, C_IT] == 0) & (g[:, C_DES] == 0)).sum() >= 8:
            out.append(g)
    return out[:-1]          # the last group may be half-written on a live farm


def load_critic(path):
    with open(path) as f:
        nin, H = map(int, f.readline().split())
        mu = np.array(f.readline().split(), float); sd = np.array(f.readline().split(), float)
        W1 = np.array([f.readline().split() for _ in range(H)], float); b1 = np.array(f.readline().split(), float)
        W2 = np.array([f.readline().split() for _ in range(H)], float); b2 = np.array(f.readline().split(), float)
        W3 = np.array(f.readline().split(), float); b3 = float(f.readline())
    def q(rows):   # the C forward pass: (obs39, mean, cand) standardised -> tanh -> tanh -> linear
        x = (np.concatenate([rows[:, C_OBS:C_OBS + 39], rows[:, C_MEAN:C_MEAN + 10], rows[:, C_CAND:C_CAND + 10]], 1) - mu) / sd
        return np.tanh(np.tanh(x @ W1.T + b1) @ W2.T + b2) @ W3 + b3
    return q


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--critic", default=None)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--val_frac", type=float, default=0.15)
    args = ap.parse_args()
    paths = sorted(p for d in args.dirs for p in glob.glob(os.path.join(d, "s*.cand")))
    groups = [g for p in paths for g in replans(p)]
    if not groups:
        sys.exit("no complete replan groups")

    if args.critic:   # the trainer's split, reproduced: unique run ids of every finite row, shuffled by default_rng(seed)
        ids = []
        for p in paths:
            n = os.path.getsize(p) // (NCOL * 8)
            a = np.fromfile(p, dtype=np.float64, count=n * NCOL).reshape(n, NCOL)
            a = a[np.isfinite(a).all(axis=1)]
            ids.append(a[:, C_SEED].astype(np.int64) * 100000 + a[:, C_RUN].astype(np.int64))
        runs = np.unique(np.concatenate(ids))
        rng = np.random.default_rng(args.seed); rng.shuffle(runs)
        val = set(runs[:max(1, int(len(runs) * args.val_frac))].tolist())
        groups = [g for g in groups if int(g[0, C_SEED] * 100000 + g[0, C_RUN]) in val]
        q = load_critic(args.critic)
        print(f"critic {args.critic}: scoring {len(groups)} replans of {len(val)} held-out runs")

    K = [0, 1, 2, 4, 7]
    rnd = {k: 0.0 for k in K}; crit = {k: 0 for k in (1, 2, 4)}
    land_teacher = 0; teacher_is_elite = 0; n = 0; big_n = 0
    for g in groups:
        pop = g[g[:, C_DES] == 0]
        it0 = pop[pop[:, C_IT] == 0][:8]
        elite, draws = it0[0], it0[1:]
        n += 1; big_n += int(g[0, C_BIG] > 0.5)
        for k in K:   # exact expectation over every k-subset of the seven draws
            subs = list(itertools.combinations(range(7), k))
            tot = 0
            for S in subs:
                cand = np.vstack([elite[None], draws[list(S)]]) if k else elite[None]
                tot += cand[np.argmin(cand[:, C_COST]), C_LANDED] > 0.5
            rnd[k] += tot / len(subs)
        if args.critic:
            order = np.argsort(q(draws), kind="stable")
            for k in crit:
                cand = np.vstack([elite[None], draws[order[:k]]])
                crit[k] += cand[np.argmin(cand[:, C_COST]), C_LANDED] > 0.5
        best = pop[np.argmin(pop[:, C_COST])]
        land_teacher += best[C_LANDED] > 0.5
        teacher_is_elite += np.array_equal(best[C_CAND:C_CAND + 10], elite[C_CAND:C_CAND + 10])
    print(f"files {len(paths)}  replan groups {n}  (t=0 solves {big_n})")
    for k in K:
        line = f"  elite + {k} draws ({k + 1} rollouts): P(pick lands) random {rnd[k] / n:.3f}"
        if args.critic and k in crit:
            line += f"   critic-proposed {crit[k] / n:.3f}   (critic - random {100 * (crit[k] - rnd[k]) / n:+.1f} pts)"
        print(line)
    npop = len(groups[0][groups[0][:, C_DES] == 0])
    print(f"  teacher (both iterations, {npop} rollouts): P(pick lands) {land_teacher / n:.3f}   "
          f"P(pick == carried elite) {teacher_is_elite / n:.3f}")


if __name__ == "__main__":
    main()
