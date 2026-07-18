import math
# US76 layers (H' km, T K, L K/km, p Pa) from canon A.1
LYR=[(0.0,288.15,-6.5,101325.0),(11.0,216.65,0.0,22632.1),(20.0,216.65,1.0,5474.89),
     (32.0,228.65,2.8,868.019),(47.0,270.65,0.0,110.906),(51.0,270.65,-2.8,66.9389),
     (71.0,214.65,-2.0,3.95642)]
g0=9.80665; R=287.053; r0=6356766.0; gam=1.4
def atmo(h_geom):
    Hp = r0*h_geom/(r0+h_geom)/1000.0  # geopotential km
    if Hp>=84.852: Hp=84.852
    for i in range(len(LYR)-1,-1,-1):
        Hb,Tb,L,pb=LYR[i]
        if Hp>=Hb:
            T=Tb+L*(Hp-Hb)
            if abs(L)<1e-9: p=pb*math.exp(-g0*(Hp-Hb)*1000.0/(R*Tb))
            else: p=pb*(T/Tb)**(-g0/(R*(L/1000.0)))
            rho=p/(R*T); a=math.sqrt(gam*R*T)
            return T,p,rho,a
    return LYR[0][1],LYR[0][3],LYR[0][3]/(R*288.15),340.0

# ENTRY IC (canon App-D): h0=62000, vertical v0 ~ -1500 (fpa -70deg), but treat |v|~1500-1600
# KESTREL-9: dry 25600, prop0(entry)=30000 kg -> m0 ~ 55600. AREF=10.52
m0=55600.0; AREF=10.52; Rn=1.83; Hk=1.7415e-4
CA_sub=0.85; CA_tr=1.40  # transonic peak
def CA(M):
    M=abs(M)
    if M<0.6: return 0.85+ (0.88-0.85)*(M/0.6)
    if M<0.9: return 0.88+(1.10-0.88)*((M-0.6)/0.3)
    if M<1.1: return 1.10+(1.40-1.10)*((M-0.9)/0.2)
    if M<1.5: return 1.40+(1.25-1.40)*((M-1.1)/0.4)
    if M<2.0: return 1.25+(1.10-1.25)*((M-1.5)/0.5)
    if M<3.0: return 1.10+(0.95-1.10)*((M-2.0)/1.0)
    if M<5.0: return 0.95+(0.92-0.95)*((M-3.0)/2.0)
    return 0.90

# integrate vertical channel, NO entry burn, base-first, straight-down approx (fpa -70 ~ steepish)
def run(v0, burn=None):
    # burn = (h_start, h_stop) meters; 3 engines full; SRP shield drag->~5%
    dt=0.01; h=62000.0; v=-abs(v0); t=0.0
    m=m0
    qmax=0; qdotmax=0; Q=0; hb_lo=None
    Tvac=932000.0; Ae=0.859; Ispv=311.0; Ispsl=282.0
    burning=False
    trace=[]
    while h>3000 and t<400:
        T,p,rho,a=atmo(h)
        speed=abs(v); M=speed/a
        qbar=0.5*rho*speed*speed
        # thrust
        thr=0.0; drag_scale=1.0
        if burn and burn[0]>=h>=burn[1]:
            thr=3*(Tvac-Ae*p)  # full throttle 3 eng
            # SRP shield: C_T = T/(qbar*AREF); shield 1->0.05 for CT 0.5->3
            CT=thr/max(qbar*AREF,1.0)
            if CT>0.5:
                tt=min((CT-0.5)/(3.0-0.5),1.0); drag_scale=1.0+tt*(0.05-1.0)
            burning=True
            Isp=Ispv-(Ispv-Ispsl)*(p/101325.0)
            mdot=thr/(Isp*g0); m-=mdot*dt
            if m<25600: m=25600
        drag=0.5*rho*speed*speed*AREF*CA(M)*drag_scale  # opposes motion (upward, v<0)
        a_net = -g0 + drag/m + (thr/m)   # drag & thrust both decelerate a downward fall (push up)
        v += a_net*dt; h += v*dt; t+=dt
        qdot=Hk*math.sqrt(rho/Rn)*speed**3
        Q+=qdot*dt
        qmax=max(qmax,qbar); qdotmax=max(qdotmax,qdot)
        if h<40000 and hb_lo is None: pass
    return dict(qmax_kPa=qmax/1000, qdotmax_kW=qdotmax/1000, Q_MJ=Q/1e6,
                v_end=v, m_end=m, prop_used=m0-m)

print("ENTRY, |v0|=1550, NO burn:      ", {k:round(x,1) for k,x in run(1550).items()})
print("ENTRY, |v0|=1550, burn 55->42km:", {k:round(x,1) for k,x in run(1550,(55000,42000)).items()})
print("ENTRY, |v0|=1550, burn 60->45km:", {k:round(x,1) for k,x in run(1550,(60000,45000)).items()})
print("ENTRY, |v0|=1550, burn 58->40km:", {k:round(x,1) for k,x in run(1550,(58000,40000)).items()})
print("Limits: qbar<=80 kPa, qdot<=300 kW/m^2 (sustained)")

print("\n--- Terminal-velocity qbar analysis (drag=weight equilibrium, unpowered) ---")
# At terminal velocity: 0.5 rho v^2 A CA = m g  => qbar_term = m g / (A CA)
# qbar at terminal is INDEPENDENT of altitude (=mg/(A CA))! So if mg/(A CA) < 80kPa we're safe once on terminal.
m_dry=25600.0
for m in (55600, 45000, 35000, 25600):
    for M,lbl in ((0.8,"M0.8 CA1.05"),(3.0,"M3 CA0.95"),(0.6,"M0.6 CA0.85")):
        ca=CA(M)
        qterm = m*g0/(AREF*ca)
        print(f"  m={m:6.0f} {lbl}: qbar_terminal = {qterm/1000:6.1f} kPa")
    break
print("  (terminal qbar scales with mass/CA; note it can exceed 80 kPa when heavy+low-CA)")

print("\n--- What matters: peak qbar during descent BEFORE reaching terminal ---")
# The real driver: entering dense air too fast. Descent qbar peaks when the vehicle is still
# faster than local terminal velocity. Burn must bleed v so that at each altitude, v <= v_term(h)+margin.
def vterm(h,m,M_guess=1.0):
    T,p,rho,a=atmo(h); ca=CA(M_guess)
    return math.sqrt(2*m*g0/(rho*AREF*ca))
for h in (60000,50000,40000,30000,20000,10000,5000):
    T,p,rho,a=atmo(h)
    vt=vterm(h,45000)
    print(f"  h={h/1000:4.1f}km rho={rho:.5f}  v_term(45t)~{vt:6.0f} m/s (M~{vt/a:.1f}), qbar@vterm={0.5*rho*vt*vt/1000:.0f}kPa")
