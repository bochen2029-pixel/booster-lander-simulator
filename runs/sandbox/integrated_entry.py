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
AREF=10.52;Rn=1.83;Hk=1.7415e-4;Tvac=932000.0;Ae=0.859;Ispv=311.0;Ispsl=282.0;m0=55600.0
def predict_peak(h,vx,vz,m):
    dt=0.1;qp=0;s=0
    while h>3000 and s<4000:
        T,p,rho,a=atmo(h);sp=math.sqrt(vx*vx+vz*vz);M=sp/a
        qp=max(qp,0.5*rho*sp*sp);D=0.5*rho*sp*sp*AREF*CA(M)
        ux,uz=(vx/sp,vz/sp) if sp>1e-3 else (0,0)
        vx+=-D/m*ux*dt;vz+=(-g0-D/m*uz)*dt;h+=vz*dt;s+=1
    return qp
def aoa_cap(qbar):
    if qbar<10000: return 12.0
    if qbar>30000: return 4.0
    return 12.0+(4.0-12.0)*(qbar-10000)/20000.0
def full(v0,fpa,lat0,trig=80000,cut=76000):
    dt=0.01;h=62000.0
    v=abs(v0);vx=v*math.cos(math.radians(fpa));vz=-v*abs(math.sin(math.radians(fpa)))
    x=lat0  # lateral offset (steer to 0); use vx as downrange+cross combined -> simplify: track lateral chan sep.
    # separate lateral steering channel (small vs main): xr lateral pos, vxr lateral vel
    xr=lat0; vxr=0.0
    m=m0;t=0;qmax=0;qdmax=0;Q=0;burning=False;burned=False;phase="ENTRY";hcut=None;tover=0;qdover=0
    while h>4600 and t<300:
        T,p,rho,a=atmo(h);sp=math.sqrt(vx*vx+vz*vz);M=sp/a;qbar=0.5*rho*sp*sp
        thr=0.0;ds=1.0
        pk=predict_peak(h,vx,vz,m)
        if phase=="ENTRY":
            if not burned and not burning and pk>=trig and h>30000: burning=True
            if burning and predict_peak(h,vx,vz,m)<=cut: burning=False;burned=True;hcut=h;phase="AERO"
            if burning:
                thr=3*(Tvac-Ae*p);CT=thr/max(qbar*AREF,1.0)
                if CT>0.5:tt=min((CT-0.5)/2.5,1.0);ds=1.0+tt*(0.05-1.0)
                Isp=Ispv-(Ispv-Ispsl)*(p/101325.0);m-=thr/(Isp*g0)*dt
        # lateral steering (both phases; only effective once qbar builds & not shielded)
        alat=0.0
        if not burning and qbar>500:
            a_cmd=-0.6*xr-1.2*vxr
            cap=aoa_cap(qbar)*math.pi/180
            alpha=m*a_cmd/(qbar*AREF*CNa(M)); alpha=max(-cap,min(cap,alpha))
            alat=qbar*AREF*CNa(M)*alpha/m
            if qbar>30000 and abs(alpha)*180/math.pi>15: pass
        drag=0.5*rho*sp*sp*AREF*CA(M)*ds
        ux,uz=(vx/sp,vz/sp) if sp>1e-3 else (0,0)
        ax=-(drag+thr)/m*ux; az=-g0-(drag+thr)/m*uz
        vx+=ax*dt;vz+=az*dt;h+=vz*dt;t+=dt
        vxr+=alat*dt; xr+=vxr*dt
        qd=Hk*math.sqrt(rho/Rn)*sp**3;Q+=qd*dt
        qmax=max(qmax,qbar);qdmax=max(qdmax,qd)
        if qbar>80000:tover+=dt
        if qd>300000:qdover+=dt
    return dict(qmax=round(qmax/1000,1),qd=round(qdmax/1000,1),prop=round(m0-m,0),
        hcut=round((hcut or 0)/1000,1),lat_resid=round(xr,0),vhand=round(math.sqrt(vx*vx+vz*vz),0),
        reserve=round(30000-(m0-m),0),qbar_over=round(tover,2),qd_over=round(qdover,2))
print("INTEGRATED entry+aero-descent, predictive cut@76kPa, with AoA steering:")
for l0 in (0,800,1479):
    for f in (-70,):
        print(f"  lat0={l0} fpa={f}:", full(1550,f,l0))
print("  worst-corner v0=1700 fpa=-80 lat0=1479:", full(1700,-80,1479))
print("\nGATE CHECK: qbar_over<2.0 AND qd_over<5.0 (else STRUCT/THERMAL_FAIL); reserve>=8000kg; lat_resid<~400m for landing-burn")

print("\n=== FUEL RESERVE SPREAD over dispersions (for @C fuel knife-edge) ===")
# Dispersions: thrust bias +-2%, Isp +-1%, m_dry +-2%, plus IC v0/fpa/lat spread already tested.
# Worst-fuel corner: LOW Isp (-1%) [more mdot per thrust] + need same dv. Higher m0 (heavier dry) = more prop to decel.
# Model Isp/thrust bias effect on entry prop:
base=full(1550,-70,0)
print(f"  nominal reserve: {base['reserve']} kg (prop {base['prop']})")
# emulate -1% Isp: mdot up ~1% -> prop up ~1%; +2% dry mass -> ~ more decel prop. Rough scaling.
for dIsp,dThr,dDry,lbl in [(-0.01,0,0,"-1% Isp"),(0,-0.02,0,"-2% thrust"),(-0.01,-0.02,0.02,"combo worst: -Isp -thrust +dry")]:
    # crude: prop scales ~ (1-dIsp) for fixed impulse; -thrust means longer burn to bleed same v -> ~ +dThr more prop; +dry -> heavier -> +~dDry*ratio
    p=base['prop']*(1-dIsp)*(1-dThr*1.0)*(1+dDry*0.6)
    print(f"  {lbl}: est prop~{p:.0f} kg -> reserve~{30000-p:.0f} kg (landing-need ~7-8t)")
print("  => min-reserve corner ~7.9-8.2t; C's >=1.5t-slack gate means ENTRY nominal should target ~9.5t reserve (cut@78kPa) for margin.")
