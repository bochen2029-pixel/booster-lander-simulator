"""Write an ablated copy of a tap corpus: keep the teacher's label on the named theta coordinates and
set every other coordinate to identity, so a net trained on the copy learns only that group.

Usage: python runs/e1_ablate_labels.py <src_dir> <dst_dir> <coords>
  coords: comma list from EKR,EKV,EBANK,ADECEL,TLEAD,KDIV,KVNEAR,IGNM,TGTLEAD,KV  (or 'all')
The .csv verdict manifests are copied unchanged. Row layout per core/policy_tap.h v2 (55 f64).
"""
import glob, os, shutil, sys
import numpy as np

NAMES = ["EKR","EKV","EBANK","ADECEL","TLEAD","KDIV","KVNEAR","IGNM","TGTLEAD","KV"]
IDENT = np.array([1,1,1,1,1,1,1,1,0,1], dtype=np.float64)
NCOL = 55
C_TH = slice(45, 55)

src, dst, coords = sys.argv[1], sys.argv[2], sys.argv[3]
fill = sys.argv[4] if len(sys.argv) > 4 else "identity"     # identity | const (the D-047 constant, the E3 anchor)
if fill == "const":
    IDENT = np.array([3.9486512123718245, 3.6070627977252863, 3.0, 1.1034248624801957, 0.25, 0.4,
                      0.6043122432336955, 0.5579795297922319, 0.33269879919580125, 0.7624253416827261])
keep = set(range(10)) if coords == "all" else {NAMES.index(c) for c in coords.split(",")}
os.makedirs(dst, exist_ok=True)
for f in sorted(glob.glob(os.path.join(src, "*.bin"))):
    rows = np.fromfile(f, dtype=np.float64).reshape(-1, NCOL)
    has = np.abs(rows[:, C_TH]).sum(axis=1) > 0          # rows with teacher context only
    th = rows[:, C_TH]
    for k in range(10):
        if k not in keep:
            th[has, k] = IDENT[k]
    rows[:, C_TH] = th
    rows.tofile(os.path.join(dst, os.path.basename(f)))
    print(f"  {os.path.basename(f)}: {len(rows):,} rows, kept {sorted(NAMES[k] for k in keep)}")
for f in glob.glob(os.path.join(src, "*.csv")):
    shutil.copy(f, dst)
print(f"wrote {dst}")
