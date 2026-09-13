"""k9noatt.py TAG -- the v5 end-to-end: serve an ENTRY flight with a tight gimbal-servo limit, poll
the page in short JS calls (peek's eval cap is 60 s) until the plant reports the reference LOST,
capture the FDAI text + a live HDR frame then, and again 6 s later. Env: BOOSTER_CORE, PORT (8790),
SEED (7), RATE (30)."""
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
REPO = os.path.abspath(os.path.join(HERE, "..", "..")).replace("\\", "/")
PEEK = os.environ.get("PEEK", "C:/peek/peek.py")
CORE = os.environ.get("BOOSTER_CORE", f"{REPO}/build4/bin/Release/booster-core.exe")
PORT = os.environ.get("PORT", "8790")
SEED = os.environ.get("SEED", "7")
RATE = os.environ.get("RATE", "30")
tag = sys.argv[1]

p = subprocess.run([sys.executable, PEEK, f"http://localhost:5183/?raf&port={PORT}", "--keep", "--text"],
                   capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
m = re.search(r"CDP port (\d+) \(pid (\d+)\)", p.stdout + p.stderr)
if not m:
    print("PEEK-OPEN-FAILED", (p.stdout + p.stderr)[-600:]); sys.exit(2)
cdp, pid = m.group(1), m.group(2)
print(f"page open: cdp={cdp} pid={pid}")

def js(code, timeout=90):
    r = subprocess.run([sys.executable, PEEK, "--attach", cdp, "--text", "--js", code],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    txt = r.stdout + r.stderr
    i = txt.find("js:"); j = txt.find("\ntext:", i)
    shot = re.search(r"shot:\s+(\S+)", txt)
    return (txt[i + 3:j].strip() if i >= 0 else txt[-400:]), (shot.group(1) if shot else None)

err = open(f"{REPO}/runs/serve_{tag}.err", "w")
core = subprocess.Popen([CORE, "--serve", "--port", PORT, "--interactive", "--scenario", "entry", "--seed", SEED, "--run", "1",
                         "--rfly", "--rfly-blind", "--rfly-event-replan", "--rfly-budget", "0.125", "--engine-out", "random",
                         "--imu-platform", RATE], cwd=REPO, stdout=subprocess.DEVNULL, stderr=err)
time.sleep(3)
print("core pid", core.pid, "alive" if core.poll() is None else f"EXITED {core.returncode}")

SNAP = ("(()=>{const t=__telem(); if(!t) return 'null'; const fd=(document.querySelector('.fdai-root')||{}).innerText||'';"
        "return JSON.stringify({t:+t.t.toFixed(1),phase:t.phase,flags:t.imuFlags,err:+t.imuErrDeg.toFixed(2),margin:+t.imuMarginDeg.toFixed(1),"
        "ann:fd.split('\\n')[1]||'', src:(fd.match(/SRC\\n(\\S+)/)||[])[1]||'', fdai:fd.replace(/\\n/g,' ').slice(0,220)})})()")
CAP = "(async()=>{__cam('FREE_ORBIT'); await new Promise(r=>setTimeout(r,400)); const u=await __shotHDR(1280,720,0.85,1280); const r=await fetch('/__cap?name=%s',{method:'POST',body:u}); return r.status})()"

t0 = time.time()
lost_snap = None
last = None
while time.time() - t0 < 260:
    s, _ = js(SNAP, timeout=120)
    try:
        d = json.loads(s)
    except Exception:
        d = None
    if d:
        last = d
        if d["flags"] & 2:
            lost_snap = d
            break
        if d["phase"] >= 7:
            break
    time.sleep(3.0)
print("at loss / end:", json.dumps(last))
r1, shot1 = js(CAP % (tag + "_noatt"), timeout=60)
print("capture 1:", r1, shot1)
time.sleep(6)
s2, _ = js(SNAP, timeout=120)
print("plus 6 s:", s2)
r2, shot2 = js(CAP % (tag + "_noatt6"), timeout=60)
print("capture 2:", r2, shot2)
# let the flight finish, then a final page screenshot with the HUD
while core.poll() is None and time.time() - t0 < 320:
    time.sleep(2)
s3, shot3 = js(SNAP, timeout=120)
print("final:", s3, "| page shot:", shot3)
subprocess.run(["taskkill", "/PID", pid, "/F"], capture_output=True)
try:
    core.wait(timeout=10)
except subprocess.TimeoutExpired:
    core.kill()
err.close()
print("done", tag)
