import math
LYR=[(0.0,288.15,-6.5,101325.0),(11.0,216.65,0.0,22632.1),(20.0,216.65,1.0,5474.89),
     (32.0,228.65,2.8,868.019),(47.0,270.65,0.0,110.906),(51.0,270.65,-2.8,66.9389),
     (71.0,214.65,-2.0,3.95642)]
g0=9.80665; R=287.053; r0=6356766.0; gam=1.4
def atmo(h):
    Hp=r0*h/(r0+h)/1000.0
    if Hp>=84.852: Hp=84.852
    for i in range(len(LYR)-1,-1,-1):
        Hb,Tb,L,pb=LYR[i]
        if Hp>=Hb:
            T=Tb+L*(Hp-Hb)
            if abs(L)<1e-9: p=pb*math.exp(-g0*(Hp-Hb)*1000.0/(R*Tb))
            else: p=pb*(T/Tb)**(-g0/(R*(L/1000.0)))
            return T,p,p/(R*T),math.sqrt(gam*R*T)
    return 288.15,101325.0,1.225,340.0
def CA(M):
    M=abs(M); pts=[(0,0.85),(0.6,0.88),(0.9,1.10),(1.1,1.40),(1.5,1.25),(2,1.10),(3,0.95),(5,0.92),(8,0.90)]
    for i in range(len(pts)-1):
        if M<=pts[i+1][0]:
            (x0,y0),(x1,y1)=pts[i],pts[i+1]; return y0+(y1-y0)*(M-x0)/(x1-x0)
    return 0.90
m0=55600.0; AREF=10.52; Rn=1.83; Hk=1.7415e-4; Tvac=932000.0; Ae=0.859; Ispv=311.0; Ispsl=282.0

def predict_peak_qbar(h,vx,vz,m):
    # cheap ballistic (no thrust) forward-propagation of the vertical channel -> peak qbar & qdot
    dt=0.05; qp=0; qdp=0; steps=0
    while h>3000 and steps<8000:
        T,p,rho,a=atmo(h); sp=math.sqrt(vx*vx+vz*vz); M=sp/a
        qp=max(qp,0.5*rho*sp*sp); qdp=max(qdp,Hk*math.sqrt(rho/Rn)*sp**3)
        D=0.5*rho*sp*sp*AREF*CA(M)
        ux,uz=(vx/sp,vz/sp) if sp>1e-3 else (0,0)
        vx+=-D/m*ux*dt; vz+=(-g0-D/m*uz)*dt; h+=vz*dt; steps+=1
    return qp,qdp

def run(v0,fpa_deg,qbar_trigger,cut_qbar,throttle=1.0,predict_every=0.1):
    dt=0.005; h=62000.0
    v=abs(v0); vx=v*math.cos(math.radians(fpa_deg)); vz=-v*abs(math.sin(math.radians(fpa_deg)))
    m=m0; t=0; qmax=0; qdotmax=0; Q=0; burned=False; burning=False; hi=None; hcut=None
    tover=0; qdover=0; tpred=0; predq=0
    while h>3000 and t<400:
        T,p,rho,a=atmo(h); speed=math.sqrt(vx*vx+vz*vz); M=speed/a; qbar=0.5*rho*speed*speed
        if t>=tpred:
            predq,predqd=predict_peak_qbar(h,vx,vz,m); tpred=t+predict_every
        thr=0.0; ds=1.0
        # TRIGGER: predicted ballistic peak qbar would exceed trigger -> burn (predictive!)
        if (not burned) and (not burning) and predq>=qbar_trigger and h>30000: burning=True; hi=h
        # CUT: current qbar dropped below cut target on the way (velocity bled enough) OR
        #   predicted remaining peak now under 80 kPa with margin
        if burning:
            predq2,_=predict_peak_qbar(h,vx,vz,m)
            if predq2<=cut_qbar: burning=False; burned=True; hcut=h
        if burning:
            thr=3*(Tvac-Ae*p)*throttle
            CT=thr/max(qbar*AREF,1.0)
            if CT>0.5: tt=min((CT-0.5)/2.5,1.0); ds=1.0+tt*(0.05-1.0)
            Isp=Ispv-(Ispv-Ispsl)*(p/101325.0); m-=thr/(Isp*g0)*dt
            if m<25600:m=25600; 
        drag=0.5*rho*speed*speed*AREF*CA(M)*ds
        ux,uz=(vx/speed,vz/speed) if speed>1e-3 else (0,0)
        ax=-(drag+thr)/m*ux; az=-g0-(drag+thr)/m*uz
        vx+=ax*dt; vz+=az*dt; h+=vz*dt; t+=dt
        qd=Hk*math.sqrt(rho/Rn)*speed**3; Q+=qd*dt
        qmax=max(qmax,qbar); qdotmax=max(qdotmax,qd)
        if qbar>80000:tover+=dt
        if qd>300000:qdover+=dt
    return dict(qmax=round(qmax/1000,1),qdot=round(qdotmax/1000,1),Q=round(Q/1e6,2),prop=round(m0-m,0),
        hi=round((hi or 0)/1000,1),hcut=round((hcut or 0)/1000,1),vend=round(math.sqrt(vx*vx+vz*vz),0),
        qbar_over_s=round(tover,2),qdot_over_s=round(qdover,2))

print("PREDICTIVE trigger (ballistic peak-qbar), fpa=-70:")
for qt,qc in [(80000,75000),(70000,70000),(80000,70000),(75000,72000)]:
    print(f"  trig@predpeak={qt/1000:.0f} cut@predpeak={qc/1000:.0f}:", run(1550,-70,qt,qc))
print("\nfpa sensitivity (trig 80 cut 72):")
for f in (-60,-70,-80):
    print(f"  fpa={f}:", run(1550,f,80000,72000))
print("\nv0 sensitivity (trig 80 cut 72, fpa-70):")
for vv in (1400,1550,1700):
    print(f"  v0={vv}:", run(vv,-70,80000,72000))
print("\nGate: qbar_over_s<2.0 AND qdot_over_s<5.0")

print("\n=== PROPELLANT OPTIMIZATION: can we spend less on entry, leave more for landing? ===")
# Higher cut_qbar target = shorter burn = less prop, but closer to the 80 limit.
for qc in (72000,76000,79000):
    r=run(1550,-70,80000,qc)
    print(f"  cut@predpeak={qc/1000:.0f}kPa -> prop={r['prop']}kg qmax={r['qmax']} vend={r['vend']} m/s, land_reserve={30000-r['prop']:.0f}kg")
# throttle sensitivity
print("  throttle 0.7:", {k:run(1550,-70,80000,74000,throttle=0.7)[k] for k in ('qmax','prop','vend','qbar_over_s')})
print("\nLanding-burn handoff: what is v at 4.6km after unpowered aero-descent from 40km burnout?")
def aero_descent(vstart,hstart,m):
    dt=0.02;h=hstart;vx=0;vz=-vstart;t=0
    while h>4600 and t<300:
        T,p,rho,a=atmo(h);sp=math.sqrt(vx*vx+vz*vz);M=sp/a
        D=0.5*rho*sp*sp*AREF*CA(M)
        uz=vz/sp if sp>1e-3 else -1
        vz+=(-g0-D/m*uz)*dt; h+=vz*dt; t+=dt
    return round(math.sqrt(vx*vx+vz*vz),0),round(t,1)
for vb,hb,mm in [(340,40000,42400),(335,40300,45000)]:
    v46,tt=aero_descent(vb,hb,mm)
    print(f"  burnout {vb}m/s@{hb/1000}km,m={mm}: v@4.6km = {v46} m/s over {tt}s  (canon target ~310 m/s)")
