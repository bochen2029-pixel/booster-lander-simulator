"""
D-047 VERIFY — fly one constant theta across EVERY battery, not just the one it was tuned on.

A theta optimized on the engine-out draw is an ENGINE-OUT SPECIALIST until proven otherwise. The
whole D-030 lineage is a warning here: gains that buy recovery on one axis can quietly cost clean
flight, which is why the repo carries standing no-regression floors at all. So the winner is not a
result until it has been flown on the axes it was NOT tuned for.

Usage:  python runs/d047_verify.py [path/to/best.json | "v0,v1,...,v9"]
        (default: runs/d047/best.json, else runs/d047/result.json)

Batteries, all with --rfly-fixed so each is ~23 s per 60 draws:

  EO HELD-OUT      entry + --engine-out random, x60, seeds 42/7/99   <- the headline
                   reference on this exact batch: GM_RFLY (privileged search) 180/180,
                   MPPI 4/60, reactive+D-030 9-10/60, identity constant 9/60 (s42)

  COMPOUND         entry + engine-out x gust 15@6000:1000 x target circle:15:40, x12, 42/7/99
                   reference: GM_RFLY 36/36 (D-040). This asks the load-bearing question --
                   how much of "the compound is search-necessary" was really "nobody ever
                   optimized the constant"?

  CLEAN ENTRY      entry, x60, seed 42, no fault          <- no-regression
  CLEAN AERO       aero_offset, x60, seed 42, no fault    <- no-regression

Identity is flown beside the candidate on every battery, in the same process order, so each row
carries its own control rather than being compared against a number from another week and another
binary (the D-046 baseline-correction lesson: E0's MPPI 1/60 was pre-D-030 and quoting it as a
control was a cross-version error).

Silence is never success: a battery that yields no LANDED line is reported as FAILED, never as 0.

RUN THIS ONCE, ON THE FINAL WINNER.
-----------------------------------
It is tempting to point this at each intermediate best.json as the CEM climbs, to get an early
read. Do not. Held-out seeds evaluated repeatedly, with the best-looking one kept, IS model
selection on the test set -- the same error as tuning a constant on seed 42 and then reporting
seed 42, just laundered through a loop. The search selects on TRAINING seeds 5000/5001; held-out
is flown exactly once, and whatever it returns is the number, including the train/held-out gap.
"""
import json, os, subprocess, sys

EXE = r"C:/Booster_Lander_Simulator/build/bin/Release/booster-core.exe"
IDENT = [1, 1, 1, 1, 1, 1, 1, 1, 0, 1]
NAMES = ["EKR", "EKV", "EBANK", "ADECEL", "TLEAD", "KDIV", "KVNEAR", "IGNM", "TGTLEAD", "KV"]

BATTERIES = [
    ("EO held-out",  "entry",       60, [42, 7, 99], ["--engine-out", "random"]),
    ("COMPOUND",     "entry",       12, [42, 7, 99], ["--engine-out", "random",
                                                      "--gust", "15@6000:1000",
                                                      "--target", "circle:15:40"]),
    ("clean ENTRY",  "entry",       60, [42],        []),
    ("clean AERO",   "aero_offset", 60, [42],        []),
]


def fly(theta, scen, runs, seed, extra):
    cmd = [EXE, "--headless", "--scenario", scen, "--seed", str(seed), "--runs", str(runs),
           "--rfly", "--rfly-fixed", ",".join(f"{v:.4f}" for v in theta)] + extra
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=3600)
    except subprocess.TimeoutExpired:
        return None, None
    landed, means = None, ""
    for line in p.stdout.splitlines():
        if line.startswith("LANDED:"):
            landed = int(line.split()[1].split("/")[0])
        if "landed means" in line:
            means = line.strip()
    return landed, means


def load_theta(arg):
    if arg and "," in arg:
        return [float(x) for x in arg.split(",")]
    path = arg or ("runs/d047/best.json" if os.path.exists("runs/d047/best.json")
                   else "runs/d047/result.json")
    d = json.load(open(path))
    return d.get("theta_vec") or d["theta"]


def main():
    th = load_theta(sys.argv[1] if len(sys.argv) > 1 else None)
    print("D-047 VERIFY — one constant theta, every battery\n")
    print("  theta: " + "  ".join(f"{NAMES[i]}={th[i]:.2f}" for i in range(10)) + "\n")

    out = {"theta_vec": [round(v, 4) for v in th], "batteries": {}}
    for label, scen, runs, seeds, extra in BATTERIES:
        tot_c = tot_i = 0
        of = runs * len(seeds)
        per_c, per_i, failed = {}, {}, False
        for s in seeds:
            lc, mc = fly(th, scen, runs, s, extra)
            li, mi = fly(IDENT, scen, runs, s, extra)
            if lc is None or li is None:
                failed = True
                break
            tot_c += lc; tot_i += li; per_c[s] = lc; per_i[s] = li
        if failed:
            print(f"  {label:<14} FAILED — a run produced no LANDED line (NOT a zero)")
            out["batteries"][label] = {"FAILED": True}
            continue
        delta = tot_c - tot_i
        sign = "+" if delta >= 0 else ""
        print(f"  {label:<14} candidate {tot_c:3d}/{of:<3d}   identity {tot_i:3d}/{of:<3d}   "
              f"delta {sign}{delta}")
        print(f"  {'':<14}   per-seed candidate {per_c}   identity {per_i}")
        out["batteries"][label] = {"candidate": tot_c, "identity": tot_i, "of": of,
                                   "per_seed_candidate": per_c, "per_seed_identity": per_i,
                                   "delta": delta}

    json.dump(out, open("runs/d047/verify.json", "w"), indent=2)
    print("\n  -> runs/d047/verify.json")
    print("\nD047-VERIFY-DONE")


if __name__ == "__main__":
    main()
