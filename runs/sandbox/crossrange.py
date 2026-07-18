import math
LYR=[(0.0,288.15,-6.5,101325.0),(11.0,216.65,0.0,22632.1),(20.0,216.65,1.0,5474.89),
     (32.0,228.65,2.8,868.019),(47.0,270.65,0.0,110.906),(51.0,270.65,-2.8,66.9389),(71.0,214.65,-2.0,3.95642)]
g0=9.80665;R=287.053;r0=6356766.0;gam=1.4
def atmo(h):
    Hp=r0*h/(r0+h)/1000.0
    if Hp>=84.852:Hp=84.852
    for i in range(len(LYR)-1,-1,-1):
        Hb,Tb,L,pb=LYR[i]
        if Hp>=Hb:
            T=Tb+L*(Hp-Hb)
            p=pb*math.exp(-g0*(Hp-Hb)*1000.0/(R*Tb)) if abs(L)<1e-9 else pb*(T/Tb)**(-g0/(R*(L/1000.0)))
            return T,p,p/(R*T),math.sqrt(gam*R*T)
    return 288.15,101325.0,1.225,340.0
def CA(M):
    M=abs(M);p=[(0,0.85),(0.6,0.88),(0.9,1.10),(1.1,1.40),(1.5,1.25),(2,1.10),(3,0.95),(5,0.92),(8,0.90)]
    for i in range(len(p)-1):
        if M<=p[i+1][0]:(x0,y0),(x1,y1)=p[i],p[i+1];return y0+(y1-y0)*(M-x0)/(x1-x0)
    return 0.90
def CNa(M):
    M=abs(M);p=[(0,2.0),(0.6,2.1),(0.9,2.4),(1.1,2.5),(1.5,2.4),(2,2.3),(3,2.2),(5,2.1),(8,2.0)]
    for i in range(len(p)-1):
        if M<=p[i+1][0]:(x0,y0),(x1,y1)=p[i],p[i+1];return y0+(y1-y0)*(M-x0)/(x1-x0)
    return 2.0
AREF=10.52;Sf=2.4;m=42400.0
# Body at AoA alpha generates normal force Fn = qbar*AREF*CNa*alpha (canon body table).
# Cross-range = double integral of a_lat. Command a constant modest AoA (say cap 8deg during aero) and
# see how much lateral we build 40km->4.6km. This is the BODY-lift steering (dominant), fins TRIM the AoA.
def crossrange(alpha_cap_deg, vb=340, hb=40000):
    dt=0.02;h=hb;vz=-vb;t=0;x=0;vx=0
    acap=alpha_cap_deg*math.pi/180
    while h>4600 and t<120:
        T,p,rho,a=atmo(h);sp=math.sqrt(vz*vz+vx*vx);M=sp/a;qbar=0.5*rho*sp*sp
        # hold body AoA = acap (attitude loop), lift perpendicular to velocity ~ qbar*AREF*CNa*alpha
        Fn=qbar*AREF*CNa(M)*acap
        # drag
        D=0.5*rho*sp*sp*AREF*CA(M)
        # decompose: velocity mostly -z; lift acts +x (steer direction), drag opposes v
        uz=vz/sp if sp>1e-3 else -1; ux=vx/sp if sp>1e-3 else 0
        ax=Fn/m - D/m*ux
        az=-g0 - D/m*uz
        vx+=ax*dt; vz+=az*dt; h+=vz*dt; x+=vx*dt; t+=dt
    return round(x,0), round(t,1), round(math.sqrt(vx*vx+vz*vz),0)
print("BODY-AoA cross-range steering (aero-descent 40km->4.6km), m=42400:")
for ac in (2,4,6,8):
    cr,tt,vend=crossrange(ac)
    print(f"  AoA={ac}deg held: cross-range = {cr} m in {tt}s, v@4.6km={vend}")
print("\n(canon caps tilt at 8deg in burns; aero-descent AoA can be higher, fins TRIM it)")
print("(divert DEMAND: AERO_OFFSET lateral=800m; C measured hoverslam ceiling ~550m fins-dead)")
# what AoA to null 800m offset -> need cross-range >= 800m
for ac in (10,12,15):
    cr,tt,vend=crossrange(ac)
    print(f"  AoA={ac}deg: cross-range={cr}m")
