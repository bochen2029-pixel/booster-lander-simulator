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
AREF=10.52;m=42400.0
# Full aero-descent GUIDANCE: proportional cross-range nulling with qbar-scheduled AoA cap.
# AoA cap: 12deg where qbar<10kPa; ramp to <=4deg where qbar>30kPa (side-load |a|>15deg@qbar>30 = STRUCT_FAIL; keep well under)
def aoa_cap(qbar):
    if qbar<10000: return 12.0
    if qbar>30000: return 4.0
    return 12.0 + (4.0-12.0)*(qbar-10000)/20000.0
def guided(lat0, vb=340, hb=40000, Kp=0.6):
    dt=0.02;h=hb;vz=-vb;t=0;x=lat0;vx=0  # x = lateral offset FROM pad (want ->0)
    qmax=0; sideload_fail_s=0; amax_used=0
    while h>4600 and t<120:
        T,p,rho,a=atmo(h);sp=math.sqrt(vz*vz+vx*vx);M=sp/a;qbar=0.5*rho*sp*sp
        qmax=max(qmax,qbar)
        # command lateral accel to null offset+vel (proportional-nav-ish): a_cmd = -Kp*(x) - Kd*vx
        Kd=1.2
        a_cmd = -Kp*x - Kd*vx   # steer back toward pad (x->0)
        # convert to AoA: alpha = m*a_cmd/(qbar*AREF*CNa), cap by schedule
        cap=aoa_cap(qbar)*math.pi/180
        if qbar>500:
            alpha = m*a_cmd/(qbar*AREF*CNa(M))
        else: alpha=0
        if alpha>cap:alpha=cap
        if alpha<-cap:alpha=-cap
        amax_used=max(amax_used,abs(alpha)*180/math.pi)
        Fn=qbar*AREF*CNa(M)*alpha
        D=0.5*rho*sp*sp*AREF*CA(M)
        uz=vz/sp if sp>1e-3 else -1; ux=vx/sp if sp>1e-3 else 0
        # lift acts toward -sign(x) (steer to pad); approximate lift perpendicular in the x-z steer plane
        # Fn sign follows alpha sign; direction: +x when alpha>0 here we set alpha to reduce x so Fn opposes x
        ax=Fn/m*math.copysign(1,-x if x!=0 else 1) if False else Fn/m  # Fn already signed via alpha
        # simpler: lift lateral accel = a_cmd-ish but limited; recompute actual from alpha
        a_lat_actual = Fn/m
        ax = a_lat_actual - D/m*ux
        az=-g0 - D/m*uz
        vx+=ax*dt; vz+=az*dt; h+=vz*dt; x+=vx*dt; t+=dt
        if qbar>30000 and abs(alpha)*180/math.pi>15: sideload_fail_s+=dt
    return round(x,0), round(vx,0), round(qmax/1000,1), round(amax_used,1), round(sideload_fail_s,2)
print("qbar-SCHEDULED AoA aero-descent guidance (null lateral offset -> pad), m=42.4t:")
print(" lat0_m -> residual_lat@4.6km, residual_vx, qbar_peak_kPa, max_AoA_used, sideload_fail_s")
for l0 in (500,800,1200,1479,2000):
    r=guided(l0)
    print(f"  {l0:5d} -> lat={r[0]:6.0f}m vx={r[1]:4.0f} qmax={r[2]}kPa AoAmax={r[3]}deg SLfail={r[4]}s")
print("\nGate: residual_lat@handoff should be within landing-burn ceiling (~400m), sideload_fail_s<2.0, qmax<80")
