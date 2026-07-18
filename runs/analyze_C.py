import csv, math, statistics as st, sys

def load(path):
    rows=[]
    with open(path) as f:
        for r in csv.DictReader(f):
            for k in ('td_v','td_lat','td_tilt','settled_tilt','fuel','max_qbar','peak_qdot','t_total','max_crush'):
                r[k]=float(r[k])
            for k in ('run','verdict','fault'):
                r[k]=int(r[k])
            rows.append(r)
    return rows

VG={1:'PERFECT',2:'GOOD',3:'HARD',4:'TIPPED',5:'CRASHED'}

def pct(vals,p):
    if not vals: return float('nan')
    s=sorted(vals); k=(len(s)-1)*p/100.0; f=math.floor(k); c=math.ceil(k)
    if f==c: return s[int(k)]
    return s[f]+(s[c]-s[f])*(k-f)

def summ(name,path):
    rows=load(path)
    n=len(rows)
    landed=[r for r in rows if r['verdict'] in (1,2,3)]
    good_plus=[r for r in rows if r['verdict'] in (1,2)]
    hard=[r for r in rows if r['verdict']==3]
    crash=[r for r in rows if r['verdict']==5]
    print(f"\n===== {name}  (n={n}) =====")
    from collections import Counter
    c=Counter(VG[r['verdict']] for r in rows)
    print("  verdicts:",dict(c))
    print(f"  landed={len(landed)} ({100*len(landed)/n:.1f}%)  GOOD+={len(good_plus)} ({100*len(good_plus)/n:.1f}%)  HARD={len(hard)}  CRASH={len(crash)}")
    allv=[r['td_v'] for r in landed]
    if allv:
        print(f"  LANDED td_v: mean={st.mean(allv):.2f} p50={pct(allv,50):.2f} p90={pct(allv,90):.2f} p95={pct(allv,95):.2f} p99={pct(allv,99):.2f} max={max(allv):.2f}")
    allv2=[r['td_v'] for r in rows if r['verdict'] in (1,2,3)]
    # touchdown lat over landed
    lat=[r['td_lat'] for r in landed]
    if lat:
        print(f"  LANDED td_lat: mean={st.mean(lat):.2f} p50={pct(lat,50):.2f} p95={pct(lat,95):.2f} max={max(lat):.2f}")
    # what pushes HARD? compare td_v/lat distributions GOOD vs HARD
    if hard and good_plus:
        gv=[r['td_v'] for r in good_plus]; hv=[r['td_v'] for r in hard]
        gl=[r['td_lat'] for r in good_plus]; hl=[r['td_lat'] for r in hard]
        print(f"  GOOD+ td_v mean={st.mean(gv):.2f}  HARD td_v mean={st.mean(hv):.2f}")
        print(f"  GOOD+ lat  mean={st.mean(gl):.2f}  HARD lat  mean={st.mean(hl):.2f}")
        # crush: HARD grade requires crush>=80% OR just v in (4,6]? check
        hc=[r['max_crush'] for r in hard]
        print(f"  HARD max_crush: mean={st.mean(hc):.3f} min={min(hc):.3f} max={max(hc):.3f}  (stroke cap 0.40)")
        # how many HARD are hard purely by speed 4-6 vs by crush
        by_speed=[r for r in hard if r['td_v']>4.0]
        print(f"  HARD by speed(>4 m/s): {len(by_speed)}/{len(hard)}")
    # crash lat (off-pad): how far
    if crash:
        cl=[r['td_lat'] for r in crash]
        print(f"  CRASH td_lat: mean={st.mean(cl):.1f} p50={pct(cl,50):.1f} p95={pct(cl,95):.1f} max={max(cl):.1f}  (pad R=26)")
        cv=[r['td_v'] for r in crash]
        print(f"  CRASH td_v: mean={st.mean(cv):.2f} max={max(cv):.2f}")
    # list worst-5 landed by td_v with their run ids
    worst=sorted(landed,key=lambda r:-r['td_v'])[:8]
    print("  worst-8 landed (run, td_v, td_lat, tilt, fuel):")
    for r in worst:
        print(f"    run {r['run']:4d}  v={r['td_v']:.2f}  lat={r['td_lat']:.2f}  tilt={math.degrees(r['td_tilt']):.2f}deg  fuel={r['fuel']:.0f} verdict={VG[r['verdict']]}")

for name,path in [("TERMINAL","C_terminal_s42.csv"),("AERO_OFFSET","C_aero_s42.csv"),("CHAOS","C_chaos_s42.csv")]:
    try:
        summ(name,"C:\\Booster_Lander_Simulator\\runs\\"+path)
    except FileNotFoundError:
        print("missing",path)
