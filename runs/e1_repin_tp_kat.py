"""Re-pin the theta-prior KAT in core/main.c FROM THE C PASS (house law: never from numpy).

Usage: python runs/e1_repin_tp_kat.py <path to booster-core.exe built with the NEW header>
Runs `<exe> --tp-kat`, parses the ten TP_EXP[i] lines, and rewrites the `const double EXP[10] = { ... };`
block inside test_theta_kat() in core/main.c. Prints old and new so the change is legible.
"""
import re, subprocess, sys, pathlib

exe = str(pathlib.Path(sys.argv[1]).resolve())   # absolute, native separators (CreateProcess rejects a relative forward-slash path)
root = pathlib.Path(__file__).resolve().parents[1]
main_c = root / "core" / "main.c"

out = subprocess.run([exe, "--tp-kat"], capture_output=True, text=True, check=True).stdout
vals = re.findall(r"TP_EXP\[(\d+)\] = ([^;]+);", out)
if len(vals) != 10:
    sys.exit(f"error: expected 10 TP_EXP lines from --tp-kat, got {len(vals)}:\n{out}")
vals = [v for _, v in sorted(vals, key=lambda p: int(p[0]))]
print("--tp-kat:", out.splitlines()[0])

src = main_c.read_text(encoding="utf-8")
pat = re.compile(r"(const double EXP\[10\] = \{)(.*?)(\};)", re.S)
m = pat.search(src)
if not m:
    sys.exit("error: could not find the EXP[10] block in core/main.c")
old = m.group(2)
new = "\n        " + ",\n        ".join(", ".join(vals[i:i+4]) for i in range(0, 10, 4)).rstrip() + " "
new_src = src[:m.start(2)] + new + src[m.end(2):]
if new_src == src:
    print("KAT pin already matches this binary (unchanged)")
else:
    main_c.write_text(new_src, encoding="utf-8", newline="\n")
    print("old:", " ".join(old.split()))
    print("new:", " ".join(new.split()))
    print(f"re-pinned {main_c}")
