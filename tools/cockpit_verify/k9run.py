"""k9run.py TAG QUERY -- one terminal-scenario serve run against a fresh peek page, shooting the
k9variant.js burst. TAG names the shots (runs/shots/TAG_*.jpg); QUERY is the page query string
(e.g. 'raf&port=8790' or 'raf&port=8790&legacy'). Page first, then serve (serve exits when its
single client drops). Prints the JSON list of shots + the peek full-page PNG path."""
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
REPO = os.path.abspath(os.path.join(HERE, "..", "..")).replace("\\", "/")
PEEK = os.environ.get("PEEK", "C:/peek/peek.py")
CORE = os.environ.get("BOOSTER_CORE", f"{REPO}/build/bin/Release/booster-core.exe")
tag, query = sys.argv[1], sys.argv[2]
port = re.search(r"port=(\d+)", query).group(1)
SCENARIO = os.environ.get("SCENARIO", "terminal")

# 1. fresh page (kept alive)
p = subprocess.run([sys.executable, PEEK, f"http://localhost:5183/?{query}", "--keep", "--text"],
                   capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
m = re.search(r"CDP port (\d+) \(pid (\d+)\)", p.stdout + p.stderr)
if not m:
    print("PEEK-OPEN-FAILED", (p.stdout + p.stderr)[-800:]); sys.exit(2)
cdp, pid = m.group(1), m.group(2)
print(f"page open: cdp={cdp} pid={pid}")

def js(code, timeout=240):
    r = subprocess.run([sys.executable, PEEK, "--attach", cdp, "--js", code],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    txt = r.stdout + r.stderr
    i = txt.find("js:"); j = txt.find("\ntext:", i)
    shot = re.search(r"shot:\s+(\S+)", txt)
    return (txt[i + 3:j].strip() if i >= 0 else txt[-600:]), (shot.group(1) if shot else None)

# 2. serve (terminal scenario: ~26 s, landing burn from 8 km)
err = open(f"{REPO}/runs/serve_{tag}.err", "w")
EXTRA = os.environ.get("SERVE_EXTRA", "").split()
SEED = os.environ.get("SEED", "42")
core = subprocess.Popen([CORE, "--serve", "--port", port, "--interactive", "--scenario", SCENARIO, "--seed", SEED, "--run", "1"] + EXTRA,
                        cwd=REPO, stdout=subprocess.DEVNULL, stderr=err)
time.sleep(2.5)
print("core pid", core.pid, "alive" if core.poll() is None else f"EXITED {core.returncode}")

# 3. the burst (installs its tag, then runs)
js(f"window.__k9tag={tag!r}; 'tagged'")
with open(os.environ.get("K9JS", f"{HERE}/k9variant.js"), encoding="utf-8") as f:
    result, shot = js(f.read(), timeout=300)
print("burst:", result)
print("page shot:", shot)
# 4. a final full-page screenshot with the HUD/FDAI on the landed vehicle
result2, shot2 = js("__cam('FREE_ORBIT'); JSON.stringify({model: __doc.vehicleModel, fdai: (document.querySelector('.fdai-root')||{}).innerText})")
print("final:", result2[:300])
print("final page shot:", shot2)
time.sleep(1)
subprocess.run(["taskkill", "/PID", pid, "/F"], capture_output=True)
try:
    core.wait(timeout=10)
except subprocess.TimeoutExpired:
    core.kill()
err.close()
print("done", tag)
