"""peekjs.py PORT FILE.js | PORT --expr 'JS' -- run JS in the kept peek Chrome via CDP attach.
No shell quoting: argv goes straight to peek.py. Prints only the js: result block."""
import subprocess
import sys

port = sys.argv[1]
if sys.argv[2] == "--expr":
    code = sys.argv[3]
else:
    with open(sys.argv[2], encoding="utf-8") as f:
        code = f.read()
out = subprocess.run(
    [sys.executable, "C:/peek/peek.py", "--attach", port, "--js", code],
    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=240,
)
txt = out.stdout + out.stderr
i = txt.find("js:")
j = txt.find("\ntext:", i)
print(txt[i:j] if i >= 0 else txt[-1500:])
