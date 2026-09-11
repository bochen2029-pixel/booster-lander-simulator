"""
D-053 — ARE THE TEACHER'S LABELS MULTIMODAL? The test that decides whether the
generative / multi-sample direction is ARGUED or merely fashionable.

WHY THIS EXISTS. I claimed D-047's knife-edge (eight identical flights flipping outcome from a
3.5% gain change) makes regression "ill-posed by construction", and used that to explain why pi,
theta-hat and the warm-start all failed. Outside review correctly pointed out the gap, and I
downgraded the claim to a hypothesis:

    the knife-edge is a property of OUTCOME as a function of THETA.
    but pi and theta-hat regressed onto TEACHER LABELS, not onto outcome.

Label-regression is ill-posed only if the LABELS THEMSELVES are multimodal -- i.e. at
near-identical observations the CEM converged on SEVERAL well-separated thetas (all roughly
equally good, chosen arbitrarily by the search's own randomness), so the conditional MEAN that
least-squares targets lands between the modes and represents none of them.

That is a different claim, and it is testable on data already on disk: 1.76M tap rows across 12
oracle farms, each carrying the 39-D observation AND the 10-D theta the CEM was flying.

THE MEASUREMENT. For query rows, find k nearest neighbours in standardised observation space
drawn from DIFFERENT (seed, run) pairs -- different disturbance realisations that nonetheless put
the vehicle in nearly the same state. Then compare:

    sigma_within   theta dispersion among those matched-observation neighbours
    sigma_global   theta dispersion across the whole corpus

    ratio = sigma_within / sigma_global

    ratio -> 0    theta is well determined by the observation. Labels are unimodal, regression is
                  WELL-POSED, and the three distillation funerals need another explanation
                  entirely. The Qwen-Drive generative argument loses its main support.
    ratio -> 1    the observation barely constrains theta at all. The CEM is choosing among many
                  near-equivalent answers, the conditional mean is the average of several good
                  answers (which is a bad answer), and least-squares cannot represent the target.
                  Flow matching / multi-sample proposal becomes the INDICATED fix rather than an
                  analogy borrowed from a model card.

Requiring neighbours from DIFFERENT RUNS is the whole point: within one run theta is piecewise
constant between replans, so same-run neighbours would trivially share a label and manufacture
ratio -> 0. The comparison has to be across independent realisations.

A second, blunter readout is printed beside it: the per-dimension dispersion ratio, so one can see
WHICH theta components are ambiguous. If the divert family (EKR/EKV/EBANK) is well determined and
only the damping family is ambiguous, that is a different story from uniform ambiguity.

Zero GPU. Zero new flights. Reads only what the July farms already wrote.
"""
import glob, os, sys
import numpy as np

ROW = 55                      # 3 meta + 39 obs + 3 action + 10 theta
N_OBS, N_ACT, N_TH = 39, 3, 10
OBS0, TH0 = 3, 3 + N_OBS + N_ACT          # obs cols 3..41, theta cols 45..54

FARMS = ["data/s0rf", "data/s0rf_clean", "data/s0rf_m4aero"]
MAX_ROWS_PER_FILE = 60000     # stride-sampled, keeps memory sane and the sample unbiased in time
K = 12                        # neighbours per query
N_QUERY = 3000
RNG = np.random.default_rng(20260911)


def load():
    obs, th, run_id = [], [], []
    files = []
    for f in FARMS:
        files += sorted(glob.glob(os.path.join(f, "*.bin")))
    if not files:
        print("no .bin farms found"); sys.exit(2)
    for fi, path in enumerate(files):
        n = os.path.getsize(path) // (ROW * 8)
        if n == 0:
            continue
        a = np.fromfile(path, dtype=np.float64)
        a = a[: (len(a) // ROW) * ROW].reshape(-1, ROW)
        stride = max(1, len(a) // MAX_ROWS_PER_FILE)
        a = a[::stride]
        t = a[:, TH0:TH0 + N_TH]
        keep = np.abs(t).sum(axis=1) > 0          # all-zero theta = "no teacher context" sentinel
        a = a[keep]
        if len(a) == 0:
            continue
        obs.append(a[:, OBS0:OBS0 + N_OBS])
        th.append(a[:, TH0:TH0 + N_TH])
        # a run is unique per (file, seed, run)
        run_id.append(fi * 1e9 + a[:, 1] * 1e4 + a[:, 2])
    return np.vstack(obs), np.vstack(th), np.concatenate(run_id)


def main():
    obs, th, rid = load()
    print(f"corpus: {len(obs):,} teacher rows, {len(np.unique(rid)):,} distinct runs")

    # standardise; drop constant channels (target_age is a structural zero, and there are others)
    mu, sd = obs.mean(0), obs.std(0)
    live = sd > 1e-9
    print(f"observation channels with any variance: {live.sum()}/{N_OBS} "
          f"(dead: {np.where(~live)[0].tolist()})")
    X = (obs[:, live] - mu[live]) / sd[live]

    th_sd_global = th.std(0)
    print(f"\nglobal theta dispersion (per component):\n  "
          + "  ".join(f"{v:.3f}" for v in th_sd_global))

    try:
        from scipy.spatial import cKDTree
        tree = cKDTree(X)
        use_tree = True
    except Exception:
        use_tree = False
    print(f"\nneighbour search: {'cKDTree' if use_tree else 'brute force'}")

    qi = RNG.choice(len(X), size=min(N_QUERY, len(X)), replace=False)
    within, dists, used = [], [], 0
    for i in qi:
        if use_tree:
            d, j = tree.query(X[i], k=K * 6)       # over-fetch, then filter to other runs
        else:
            dd = np.linalg.norm(X - X[i], axis=1)
            j = np.argpartition(dd, K * 6)[: K * 6]; d = dd[j]
        m = rid[j] != rid[i]                       # DIFFERENT realisation only
        j, d = j[m][:K], d[m][:K]
        if len(j) < 4:
            continue
        within.append(th[j].std(0))
        dists.append(d.mean())
        used += 1

    W = np.array(within)
    ratio = W.mean(0) / np.maximum(th_sd_global, 1e-12)
    NAMES = ["EKR", "EKV", "EBANK", "ADECEL", "TLEAD", "KDIV", "KVNEAR", "IGNM", "TGTLEAD", "KV"]

    print(f"\nqueries used: {used}  ·  mean neighbour distance in standardised obs space: "
          f"{np.mean(dists):.3f} sigma")
    print("\n=== DISPERSION RATIO — theta spread among MATCHED observations, vs the whole corpus ===")
    print("    (-> 0 means the observation determines theta, regression is well posed)")
    print("    (-> 1 means the observation barely constrains theta: labels are ambiguous)")
    for n, r, w, g in zip(NAMES, ratio, W.mean(0), th_sd_global):
        bar = "#" * int(round(r * 40))
        print(f"  {n:<8} ratio {r:5.3f}   within {w:6.3f}   global {g:6.3f}   {bar}")
    print(f"\n  MEAN RATIO ACROSS COMPONENTS: {ratio.mean():.3f}")
    print("\n  reading: >0.7 = labels largely unexplained by the observation (multimodal / ambiguous,")
    print("           least-squares targets a mean that represents no actual choice)")
    print("           <0.3 = labels well determined; regression was well posed and the three")
    print("           distillation nulls need a different explanation entirely")
    return 0


if __name__ == "__main__":
    sys.exit(main())
