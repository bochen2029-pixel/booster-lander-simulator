"""Build a frozen regression suite of the worst-case ICs for Agent B (MPPI M4/M5).
Each case is fully determined by (scenario, seed, run) because dispersions are seeded.
We pull the exact IC (h0, vz0, lateral) from the sim's verbose header line so the
manifest is human-readable and self-checking, and record the current hoverslam verdict
as the baseline MPPI must beat.
"""
import csv, subprocess, re, json, math

EXE=r"C:\Booster_Lander_Simulator\build\bin\Release\booster-core.exe"
VG={0:'NONE',1:'PERFECT',2:'GOOD',3:'HARD',4:'TIPPED',5:'CRASHED'}

def load(path):
    rows=[]
    with open(path) as f:
        for r in csv.DictReader(f):
            for k in ('td_v','td_lat','td_tilt','fuel'): r[k]=float(r[k])
            for k in ('run','verdict','fault'): r[k]=int(r[k])
            rows.append(r)
    return rows

def ic_of(scen, seed, run):
    out=subprocess.run([EXE,"--run","--scenario",scen,"--seed",str(seed),"--run",str(run),"--verbose"],
                       capture_output=True,text=True).stdout
    m=re.search(r"h0=\s*([\-\d.]+)\s*m\s*vz0=\s*([\-\d.]+)",out)
    # first telemetry line gives initial lateral
    lat=None
    for line in out.splitlines():
        mm=re.search(r"lat=\s*([\-\d.]+)",line)
        if mm: lat=float(mm.group(1)); break
    res=re.search(r"RESULT:\s*(\w+).*?td_v=([\d.]+)\s*m/s\s*lat=([\d.]+)",out)
    h0=float(m.group(1)) if m else None; vz0=float(m.group(2)) if m else None
    return h0,vz0,lat,(res.group(1) if res else '?'),(float(res.group(2)) if res else None),(float(res.group(3)) if res else None)

cases=[]
# TERMINAL: worst 8 landed by td_v + the 2 crashes
tr=load(r"C:\Booster_Lander_Simulator\runs\C_terminal_s42.csv")
worst_land=sorted([r for r in tr if r['verdict'] in (1,2,3)],key=lambda r:-r['td_v'])[:8]
crashes=[r for r in tr if r['verdict']==5]
for r in worst_land+crashes:
    cases.append(("terminal",42,r['run'],"worst-landed" if r['verdict']!=5 else "CRASH"))
# AERO_OFFSET: 3 worst off-pad crashes + the best-lateral landed (edge cases)
ar=load(r"C:\Booster_Lander_Simulator\runs\C_aero_s42.csv")
aworst=sorted([r for r in ar if r['verdict']==5],key=lambda r:-r['td_lat'])[:2]
abest=sorted([r for r in ar if r['verdict'] in (1,2,3)],key=lambda r:r['td_lat'])[:2]
for r in aworst+abest: cases.append(("aero_offset",42,r['run'],"aero-offpad" if r['verdict']==5 else "aero-landed"))
# CHAOS: 2 worst
cr=load(r"C:\Booster_Lander_Simulator\runs\C_chaos_s42.csv")
cworst=sorted([r for r in cr if r['verdict']==5],key=lambda r:-r['td_lat'])[:2]
for r in cworst: cases.append(("chaos",42,r['run'],"chaos-offpad"))

manifest=[]
for scen,seed,run,tag in cases:
    h0,vz0,lat0,verd,tdv,tdlat=ic_of(scen,seed,run)
    manifest.append(dict(scenario=scen,seed=seed,run=run,tag=tag,
                         h0=round(h0,1) if h0 else None,vz0=round(vz0,1) if vz0 else None,
                         lateral0=round(lat0,1) if lat0 is not None else None,
                         baseline_verdict=verd,baseline_td_v=tdv,baseline_td_lat=tdlat))

with open(r"C:\Booster_Lander_Simulator\runs\regression_worstcase.json","w") as f:
    json.dump(dict(note="Frozen worst-case regression suite (hoverslam baseline). "
                        "Each case = (scenario,seed,run); dispersions are seeded so ICs are exact. "
                        "MPPI M4/M5 must improve baseline_verdict. GOOD+ target for TERMINAL cases; "
                        "on-pad LANDED for AERO/CHAOS off-pad cases.",
                   cases=manifest),f,indent=2)

print("scenario     seed  run  tag            h0       vz0     lat0    baseline")
for m in manifest:
    print(f"{m['scenario']:12s} {m['seed']:4d} {m['run']:4d}  {m['tag']:13s} {str(m['h0']):8s} {str(m['vz0']):7s} {str(m['lateral0']):7s}  {m['baseline_verdict']} v={m['baseline_td_v']} lat={m['baseline_td_lat']}")
print("\nwrote regression_worstcase.json  (", len(manifest), "cases )")
