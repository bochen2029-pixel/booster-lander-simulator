import csv, math, statistics as st
from collections import Counter

def load(path):
    rows=[]
    with open(path) as f:
        for r in csv.DictReader(f):
            for k in ('td_v','td_lat','td_tilt','settled_tilt','fuel','max_qbar','peak_qdot','t_total','max_crush'):
                r[k]=float(r[k])
            for k in ('run','verdict','fault'): r[k]=int(r[k])
            rows.append(r)
    return rows

VG={0:'NONE',1:'PERFECT',2:'GOOD',3:'HARD',4:'TIPPED',5:'CRASHED'}

def pctl(vals,p):
    if not vals: return float('nan')
    s=sorted(vals); k=(len(s)-1)*p/100.0; f=math.floor(k); c=math.ceil(k)
    return s[int(k)] if f==c else s[f]+(s[c]-s[f])*(k-f)

def bucket(name,path):
    rows=load(path); n=len(rows)
    # failures = HARD, TIPPED, CRASHED, or any fault, or NOT good+
    bad=[r for r in rows if r['verdict'] in (3,4,5)]
    print(f"\n##### {name}  n={n}  #(HARD/TIPPED/CRASH)={len(bad)} #####")
    # bucket rule set (dominant mode). We lack vz at contact, so:
    #  LATERAL-MISS: td_lat > 10 m (off the GOOD lateral band, and for CRASH off-pad)
    #  TILT-AT-CONTACT: td_tilt > 3 deg
    #  HOT (residual): td_lat <= 10 and tilt<=3 but td_v>4  (vertical or near-vertical hot)
    #  FUEL: fuel margin < 500 kg (thin)
    b=Counter()
    for r in bad:
        latm = r['td_lat']; tilt=math.degrees(r['td_tilt']); v=r['td_v']; fuel=r['fuel']
        if latm>10.0: b['LATERAL-MISS']+=1
        elif tilt>3.0: b['TILT-AT-CONTACT']+=1
        elif v>4.0: b['HOT-RESIDUAL(<=10m,upright)']+=1
        else: b['OTHER(inband hard/crush)']+=1
        if fuel<500: b['(also FUEL-thin<500kg)']+=1
    for k,v in sorted(b.items(), key=lambda kv:-kv[1]):
        print(f"   {k:35s} {v:4d}  ({100*v/max(1,len(bad)):.1f}% of bad)")
    # correlation: among ALL landed, is td_v driven by td_lat?
    landed=[r for r in rows if r['verdict'] in (1,2,3)]
    if len(landed)>5:
        xs=[r['td_lat'] for r in landed]; ys=[r['td_v'] for r in landed]
        mx=st.mean(xs); my=st.mean(ys)
        cov=sum((x-mx)*(y-my) for x,y in zip(xs,ys))/len(xs)
        sx=st.pstdev(xs); sy=st.pstdev(ys)
        corr=cov/(sx*sy) if sx>0 and sy>0 else float('nan')
        print(f"   corr(td_lat, td_v) over LANDED = {corr:+.3f}   (n={len(landed)})")
    # p99 td_v across landed and across ALL (crash included, capped by grade at 6/12)
    lv=[r['td_v'] for r in landed]
    print(f"   LANDED td_v: p50={pctl(lv,50):.2f} p90={pctl(lv,90):.2f} p95={pctl(lv,95):.2f} p99={pctl(lv,99):.2f} max={max(lv):.2f}")
    # GOOD+ fraction and what p95<=3 would need
    gp=[r for r in rows if r['verdict'] in (1,2)]
    print(f"   GOOD+ = {len(gp)}/{n} = {100*len(gp)/n:.1f}%   (M2 gate: >=98% GOOD+, p95<=3)")

for name,path in [("TERMINAL","C_terminal_s42.csv"),("AERO_OFFSET","C_aero_s42.csv"),("CHAOS","C_chaos_s42.csv")]:
    bucket(name,"C:\\Booster_Lander_Simulator\\runs\\"+path)
