import math
LYR=[(0.0,288.15,-6.5,101325.0),(11.0,216.65,0.0,22632.1),(20.0,216.65,1.0,5474.89),
     (32.0,228.65,2.8,868.019),(47.0,270.65,0.0,110.906),(51.0,270.65,-2.8,66.9389),(71.0,214.65,-2.0,3.95642)]
g0=9.80665; R=287.053; r0=6356766.0; gam=1.4
def atmo(h):
    Hp=r0*h/(r0+h)/1000.0
    if Hp>=84.852: Hp=84.852
    for i in range(len(LYR)-1,-1,-1):
        Hb,Tb,L,pb=LYR[i]
        if Hp>=Hb:
            T=Tb+L*(Hp-Hb)
            p=pb*math.exp(-g0*(Hp-Hb)*1000.0/(R*Tb)) if abs(L)<1e-9 else pb*(T/Tb)**(-g0/(R*(L/1000.0)))
            return T,p,p/(R*T),math.sqrt(gam*R*T)
    return 288.15,101325.0,1.225,340.0
def CA(M):
    M=abs(M);pts=[(0,0.85),(0.6,0.88),(0.9,1.10),(1.1,1.40),(1.5,1.25),(2,1.10),(3,0.95),(5,0.92),(8,0.90)]
    for i in range(len(pts)-1):
        if M<=pts[i+1][0]:(x0,y0),(x1,y1)=pts[i],pts[i+1];return y0+(y1-y0)*(M-x0)/(x1-x0)
    return 0.90
AREF=10.52;Rn=1.83;Hk=1.7415e-4;Sf=2.4
# aero-descent 40km->4.6km, track qbar and available fin authority
def descent(vb,hb,m):
    dt=0.02;h=hb;vz=-vb;t=0;qmax=0;qmin=9e9;out=[]
    while h>4600 and t<300:
        T,p,rho,a=atmo(h);sp=abs(vz);M=sp/a;qbar=0.5*rho*sp*sp
        D=0.5*rho*sp*sp*AREF*CA(M);vz+=(-g0+D/m)*dt;h+=vz*dt;t+=dt
        qmax=max(qmax,qbar)
        if h<38000:qmin=min(qmin,qbar)
        if abs(h%5000)<vz*dt*-1 or h<5000:
            # fin normal force available at ~5deg deflection, 1 fin
            CNa=3.0*(0.55 if 0.8<M<1.2 else (0.8 if M>2 else 1.0))
            Ffin=qbar*Sf*CNa*(5*math.pi/180)  # N per fin at 5deg
            out.append((round(h/1000,1),round(sp,0),round(M,2),round(qbar/1000,1),round(Ffin/1000,1)))
    return qmax,out
print("AERO-DESCENT from 40km burnout @340 m/s, m=42400 (grid-fin steering phase):")
qm,tab=descent(340,40000,42400)
print(f"  peak qbar in descent = {qm/1000:.1f} kPa (gate: <=80)")
print("  h_km  v_m/s  Mach  qbar_kPa  Ffin_1fin@5deg_kN")
for row in tab: print("   ",row)
# lateral authority: fin lateral accel a_lat = (n_fins_effective * Ffin)/m
print("\nLateral steering authority (2 fins pitch pair, m=42400):")
for (hk,v,M,q,Ff) in tab:
    a_lat=2*Ff*1000/42400
    print(f"   h={hk}km: a_lat~{a_lat:.2f} m/s^2 ({a_lat/9.80665:.2f} g) at 5deg")
