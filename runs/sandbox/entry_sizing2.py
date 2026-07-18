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
    M=abs(M)
    pts=[(0,0.85),(0.6,0.88),(0.9,1.10),(1.1,1.40),(1.5,1.25),(2,1.10),(3,0.95),(5,0.92),(8,0.90)]
    for i in range(len(pts)-1):
        if M<=pts[i+1][0]:
            (x0,y0),(x1,y1)=pts[i],pts[i+1]; return y0+(y1-y0)*(M-x0)/(x1-x0)
    return 0.90
m0=55600.0; AREF=10.52; Rn=1.83; Hk=1.7415e-4; Tvac=932000.0; Ae=0.859; Ispv=311.0; Ispsl=282.0

def vterm(h,m):
    T,p,rho,a=atmo(h); 
    # iterate CA with Mach
    v=math.sqrt(2*m*g0/(rho*AREF*0.9))
    for _ in range(4):
        M=v/a; v=math.sqrt(2*m*g0/(rho*AREF*CA(M)))
    return v

def run(v0, fpa_deg, ignite_qbar, cut_v_frac, throttle=1.0):
    # velocity-magnitude planner: start burn when qbar exceeds ignite_qbar (predictive proxy),
    # cut when |v| <= cut_v_frac * v_term(h,m). base-first, 2D vertical (fpa affects path length).
    dt=0.005; h=62000.0; sfpa=math.sin(math.radians(fpa_deg))  # -70 -> negative
    v=abs(v0); vx=v*math.cos(math.radians(fpa_deg)); vz=-v*abs(sfpa)  # downrange + down
    m=m0; t=0; qmax=0; qdotmax=0; Q=0; burned=False; burning=False; hi=None; hcut=None
    tsust_over=0; qdot_over=0
    while h>3000 and t<400:
        T,p,rho,a=atmo(h)
        speed=math.sqrt(vx*vx+vz*vz); M=speed/a; qbar=0.5*rho*speed*speed
        vt=vterm(h,m)
        thr=0.0; ds=1.0
        want_burn = (not burned) and (qbar>=ignite_qbar or speed>1.15*vt)
        if want_burn and not burning and h>35000: burning=True; hi=h
        if burning and speed<=cut_v_frac*vt: burning=False; burned=True; hcut=h
        if burning:
            thr=3*(Tvac-Ae*p)*throttle
            CT=thr/max(qbar*AREF,1.0)
            if CT>0.5: tt=min((CT-0.5)/2.5,1.0); ds=1.0+tt*(0.05-1.0)
            Isp=Ispv-(Ispv-Ispsl)*(p/101325.0); m-=thr/(Isp*g0)*dt
            if m<25600: m=25600
        drag=0.5*rho*speed*speed*AREF*CA(M)*ds
        # accel: gravity down (-z), drag opposes velocity, thrust opposes velocity (retro)
        if speed>1e-3:
            ux,uz=vx/speed,vz/speed
            ax=-(drag+thr)/m*ux; az=-g0-(drag+thr)/m*uz
        else: ax=0; az=-g0
        vx+=ax*dt; vz+=az*dt; h+=vz*dt; t+=dt
        qdot=Hk*math.sqrt(rho/Rn)*speed**3; Q+=qdot*dt
        qmax=max(qmax,qbar); qdotmax=max(qdotmax,qdot)
        if qbar>80000: tsust_over+=dt
        if qdot>300000: qdot_over+=dt
    return dict(qmax=round(qmax/1000,1),qdot=round(qdotmax/1000,1),Q=round(Q/1e6,2),
                prop=round(m0-m,0),hi=round((hi or 0)/1000,1),hcut=round((hcut or 0)/1000,1),
                vend=round(math.sqrt(vx*vx+vz*vz),0),qbar_over_s=round(tsust_over,2),qdot_over_s=round(qdot_over,2))

print("velocity-targeted burn, fpa=-70:")
for iq,cf in [(35000,0.9),(40000,0.85),(30000,0.95),(45000,1.0)]:
    print(f"  ignite_qbar={iq/1000:.0f}kPa cut@{cf}*vterm:", run(1550,-70,iq,cf))
print("\nfpa=-80 (steeper), ignite 35kPa cut 0.9:", run(1550,-80,35000,0.9))
print("fpa=-60 (shallower),ignite 35kPa cut 0.9:", run(1550,-60,35000,0.9))
print("\nGates: qbar_over_s and qdot_over_s must be < 2.0 s to avoid STRUCT/THERMAL_FAIL")
