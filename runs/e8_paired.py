"""
E8 — the paired read: two arms flown on the SAME faults, compared draw by draw.

Every arm of runs/e8_fly.sh flies 42/7/99 x60 with --out, one csv row per draw (state.h: 1/2/3
= PERFECT/GOOD/HARD count as landed; 4 TIPPED, 5 CRASHED, 0 none do not). Same seed + same run index = the same fault
realisation, so the honest comparison is not two totals but the draws that FLIP: landed under one
arm and not the other. The exact two-sided sign test on the flips says whether the difference is
larger than the flips' own coin.

    python runs/e8_paired.py DIR_A ARM_A DIR_B ARM_B
"""
import csv, math, os, sys

LANDED = {1, 2, 3}


def read(d, arm):
    out = {}
    for s in (42, 7, 99):
        p = os.path.join(d, f"{arm}_s{s}.csv")
        if not os.path.exists(p):
            sys.exit(f"missing {p}")
        with open(p) as f:
            for r in csv.DictReader(f):
                out[(s, int(r["run"]))] = (int(r["verdict"]), float(r["td_lat"]))
    return out


def sign_p(k, n):   # exact two-sided binomial(n, 1/2) tail at k
    if n == 0:
        return 1.0
    tail = sum(math.comb(n, i) for i in range(0, min(k, n - k) + 1)) / 2 ** n
    return min(1.0, 2 * tail)


def main():
    da, a, db, b = sys.argv[1:5]
    A, B = read(da, a), read(db, b)
    keys = sorted(set(A) & set(B))
    if len(keys) != len(A) or len(keys) != len(B):
        sys.exit(f"the arms did not fly the same draws ({len(A)} vs {len(B)}, {len(keys)} shared)")
    la = sum(A[k][0] in LANDED for k in keys); lb = sum(B[k][0] in LANDED for k in keys)
    pa = sum(A[k][0] == 1 for k in keys); pb = sum(B[k][0] == 1 for k in keys)
    a_only = [k for k in keys if A[k][0] in LANDED and B[k][0] not in LANDED]
    b_only = [k for k in keys if B[k][0] in LANDED and A[k][0] not in LANDED]
    print(f"{a}: {la}/{len(keys)} landed, PERFECT {pa}    {b}: {lb}/{len(keys)} landed, PERFECT {pb}")
    for s in (42, 7, 99):
        ks = [k for k in keys if k[0] == s]
        print(f"  s{s:<3} {a} {sum(A[k][0] in LANDED for k in ks):2d}   {b} {sum(B[k][0] in LANDED for k in ks):2d}")
    n = len(a_only) + len(b_only)
    print(f"flips: {a} only {len(a_only)}, {b} only {len(b_only)}  ->  {b} - {a} = {len(b_only) - len(a_only):+d} draws, "
          f"sign test p = {sign_p(len(b_only), n):.3g} (n = {n})")
    both = [k for k in keys if A[k][0] in LANDED and B[k][0] in LANDED]
    if both:
        ma = sum(A[k][1] for k in both) / len(both); mb = sum(B[k][1] for k in both) / len(both)
        print(f"on the {len(both)} draws both landed: mean lateral {a} {ma:.2f} m, {b} {mb:.2f} m")


if __name__ == "__main__":
    main()
