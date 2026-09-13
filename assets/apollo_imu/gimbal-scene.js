// ─────────────────────────────────────────────────────────────────────────────
// Apollo Gimbal Simulator — IMU physics core + three.js views
//
// FRAMES
//   world  : ENU (x East, y North, z Up).  Inertial for our purposes.
//   case   : IMU housing, rigidly fixed to the vehicle (the phone).
//            X = outer gimbal axis (roll), Z = middle gimbal axis (yaw),
//            Y = inner gimbal axis (pitch). Apollo Block II order.
//   SM     : stable member. v_case = Rx(OGA)·Rz(MGA)·Ry(IGA) · v_SM
//   device : W3C DeviceOrientation frame (x right, y up the long side, z out of
//            the screen). case X = device Y (long axis), case Y = −device X,
//            case Z = device Z (screen normal) → phone flat on a table = zero attitude.
//
// STABILIZATION LOOP (what the real ISS did, and what runs here every 2 ms)
//   1. kinematics : q_SM_inertial = q_case · Rx(o)·Rz(m)·Ry(i)  from the ACTUAL
//                   gimbal angles — the platform is a rigid mechanism.
//   2. gyros      : three IRIG floats integrate the SM inertial rate about the SM
//                   axes (θx,θy,θz) + drift; floats saturate at ±floatMax
//                   (that is how the reference is physically lost in gimbal lock).
//   3. resolvers  : gyro errors → gimbal axis commands via the IGA/MGA resolver
//                   chain.  Δo = −(θx·cos i + θz·sin i)/cos m   (sec MGA → ∞ at lock)
//                   Δm = −(θz·cos i − θx·sin i),  Δi = −θy + sin m·Δo
//   4. torque motors: 2nd-order accel-limited servo per axis, transcribed from
//                   p1_gimbal.c (gacc = wn²·err − 2ζwn·rate; g += dt·gr; gr += dt·gacc)
//                   + optional loop integrator, optional mechanical stops with
//                   rate anti-windup (BUG-P3-3).
//   Gimbal lock is therefore emergent: as |MGA|→90° the outer gimbal command goes
//   to infinity, the motor saturates, the SM is dragged by the case, the floats
//   hit their stops and the platform reference is gone until re-alignment.
//
// REUSE: window.AGS.core exposes qCase (set it from your vehicle), servo angles,
//   tele (telemetry), align()/cage(), p (servo params). step(dt) is pure physics.
// ─────────────────────────────────────────────────────────────────────────────
import * as THREE from 'https://unpkg.com/three@0.184.0/build/three.module.js';

const D2R = Math.PI / 180, R2D = 180 / Math.PI;
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const wrapPi = (a) => { while (a > Math.PI) a -= 2 * Math.PI; while (a < -Math.PI) a += 2 * Math.PI; return a; };
const AX = new THREE.Vector3(1, 0, 0), AY = new THREE.Vector3(0, 1, 0), AZ = new THREE.Vector3(0, 0, 1);
// case → device axis mapping (columns: caseX, caseY, caseZ expressed in device axes)
const Q_CASE_TO_DEV = new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().set(
  0, -1, 0, 0,
  1, 0, 0, 0,
  0, 0, 1, 0,
  0, 0, 0, 1));

/* ══════════════════════════ SERVO (p1_gimbal.c) ══════════════════════════ */
class Servo {
  constructor() { this.a = 0; this.r = 0; this.i = 0; this.pinned = 0; this.sat = false; }
  reset() { this.r = 0; this.i = 0; this.sat = false; }
  step(cmd, dt, p) {
    const err = wrapPi(cmd - this.a);
    const kiGain = p.ki ? 0.1 * p.wn * p.wn * p.wn : 0;
    let acc = p.wn * p.wn * err + kiGain * this.i - 2 * p.zeta * p.wn * this.r;
    this.sat = Math.abs(acc) > p.accMax;
    acc = clamp(acc, -p.accMax, p.accMax);
    if (kiGain && !this.sat) this.i = clamp(this.i + err * dt, -p.accMax / kiGain, p.accMax / kiGain);
    this.a += dt * this.r;
    this.r += dt * acc;
    if (Math.abs(this.r) > p.rateMax) { this.r = (this.r < 0 ? -1 : 1) * p.rateMax; this.sat = true; }   // motor / amplifier slew limit
    if (p.stops) {
      const L = p.lim;
      if (this.a > L) { this.a = L; this.pinned = 1; if (p.aw && this.r > 0) this.r = 0; }
      else if (this.a < -L) { this.a = -L; this.pinned = -1; if (p.aw && this.r < 0) this.r = 0; }
      else this.pinned = 0;
    }
    this.a = wrapPi(this.a);
  }
}

/* ══════════════════════════ CORE ══════════════════════════ */
class Core {
  constructor() {
    this.qCase = new THREE.Quaternion();     // case → world
    this.qTarget = new THREE.Quaternion();   // raw source attitude
    this.qRef = new THREE.Quaternion();      // SM reference (REFSMMAT), set at align
    this.qSM = new THREE.Quaternion();       // case → SM (actual, from gimbal angles)
    this.qI = new THREE.Quaternion();        // SM → world (actual)
    this.qIprev = new THREE.Quaternion(); this._havePrev = false;
    this.qCasePrev = new THREE.Quaternion();
    this.mat = new THREE.Matrix4();
    this.servo = { o: new Servo(), m: new Servo(), i: new Servo() };
    this.p = { wn: 120, zeta: 0.75, accMax: 6000 * D2R, rateMax: 180 * D2R, ki: true, floatMax: 3 * D2R, secFloor: 0.02,
      stops: false, lim: 85 * D2R, aw: true };
    this.maxSrcRate = 1.4;                   // rad/s cap for demo / manual case motion
    this.theta = { x: 0, y: 0, z: 0 };       // IRIG float angles (rad, SM axes)
    this.tumbled = false;
    this.driftRate = 0;                      // deg/s, exaggerated demo drift
    this.smooth = 0.18;
    this.src = 'demo'; this.sensorState = 'idle';
    this.caged = false;
    this.demo = 'ptc'; this.demoT = 0;
    this.man = { roll: 0, pitch: 0, yaw: 0 };
    // virtual RCS flight: stick (roll, pitch-up, yaw-right) in −1..1, body rates w (rad/s, case axes)
    this.fly = { mode: 'rate', stick: { roll: 0, pitch: 0, yaw: 0 }, w: new THREE.Vector3(), accel: 12 * D2R, maxRate: 25 * D2R, jets: { x: 0, y: 0, z: 0 } };
    this.raw = { alpha: null, beta: null, gamma: null };
    this.acc = { x: 0, y: 0, z: 9.81 };
    this.gyro = { p: 0, q: 0, r: 0 };        // device rotation rates deg/s (beta, gamma, alpha)
    this.wCase = { x: 0, y: 0, z: 0 };       // case body rates deg/s from attitude
    this.dv = new THREE.Vector3(); this.pip = { x: 0, y: 0, z: 0 };
    this.heading = null; this.headingSrc = '—';
    this.hz = 0; this._frames = 0; this._hzT = 0;
    this.t = 0; this._acc = 0; this._last = 0;
    this.history = [];
    this.ideal = { o: 0, m: 0, i: 0 };
    this.tele = { oga: 0, mga: 0, iga: 0, ogaC: 0, mgaC: 0, igaC: 0, ro: 0, rm: 0, ri: 0, eo: 0, em: 0, ei: 0,
      thx: 0, thy: 0, thz: 0, err: 0, lockMargin: 90, lock: 'OK', roll: 0, pitch: 0, yaw: 0, heading: null,
      headingSrc: '—', hz: 0, src: 'DEMO', sensor: 'idle', dv: 0, pip: { x: 0, y: 0, z: 0 },
      gyro: { p: 0, q: 0, r: 0 }, wCase: { x: 0, y: 0, z: 0 }, gmag: 0, caged: false, t: 0, pinnedM: 0,
      event: null, tumbled: false, satO: false, secGain: 1 };
    this._q1 = new THREE.Quaternion(); this._q2 = new THREE.Quaternion(); this._q3 = new THREE.Quaternion();
    this._qNew = new THREE.Quaternion();
    this._dq = new THREE.Quaternion(); this._e = new THREE.Euler();
    this._v = new THREE.Vector3(); this._v2 = new THREE.Vector3(); this._v3 = new THREE.Vector3();
    this.event = null;
    this._setCaseEuler(0, 0, 0, this.qCase); this.qTarget.copy(this.qCase); this.qRef.copy(this.qCase);
  }

  /* ---------- attitude builders ---------- */
  // roll about case X, pitch (nose up +), yaw = compass heading of the nose (0 = North)
  _setCaseEuler(roll, pitch, yaw, out) {
    out.setFromAxisAngle(AZ, Math.PI / 2 - yaw)
      .multiply(this._q1.setFromAxisAngle(AY, -pitch))
      .multiply(this._q2.setFromAxisAngle(AX, roll));
    return out;
  }
  _quatFromOrientation(alpha, beta, gamma, out) {
    this._e.set(beta * D2R, gamma * D2R, alpha * D2R, 'ZXY');   // W3C: Rz(α)·Rx(β)·Ry(γ), device → ENU
    out.setFromEuler(this._e);
    out.multiply(Q_CASE_TO_DEV);                                 // case → device → ENU
  }

  /* ---------- sensors ---------- */
  async enableSensors() {
    const DOE = window.DeviceOrientationEvent, DME = window.DeviceMotionEvent;
    if (!DOE) { this.sensorState = 'unsupported'; return this.sensorState; }
    try {
      if (typeof DOE.requestPermission === 'function') {
        const r = await DOE.requestPermission();
        if (r !== 'granted') { this.sensorState = 'denied'; return this.sensorState; }
      }
      if (DME && typeof DME.requestPermission === 'function') { try { await DME.requestPermission(); } catch (e) {} }
    } catch (e) { this.sensorState = 'denied'; return this.sensorState; }
    this._bindSensors();
    this.sensorState = 'waiting'; this.src = 'imu';
    setTimeout(() => { if (this.sensorState === 'waiting') { this.sensorState = 'unsupported'; this.src = 'demo'; } }, 2500);
    return this.sensorState;
  }
  _bindSensors() {
    if (this._bound) return; this._bound = true;
    const onOri = (e) => {
      if (e.alpha === null && e.beta === null && e.gamma === null) return;
      this.sensorState = 'live';
      this.raw = { alpha: e.alpha, beta: e.beta, gamma: e.gamma };
      this._quatFromOrientation(e.alpha || 0, e.beta || 0, e.gamma || 0, this.qTarget);
      if (typeof e.webkitCompassHeading === 'number') { this.heading = e.webkitCompassHeading; this.headingSrc = 'MAG'; }
      else if (e.absolute) { this.heading = (360 - (e.alpha || 0)) % 360; this.headingSrc = 'ABS'; }
      else { this.heading = (360 - (e.alpha || 0)) % 360; this.headingSrc = 'REL'; }
    };
    const onMot = (e) => {
      const g = e.accelerationIncludingGravity;
      if (g && g.x !== null) this.acc = { x: g.x || 0, y: g.y || 0, z: g.z || 0 };
      const r = e.rotationRate;
      if (r && r.alpha !== null) this.gyro = { p: r.beta || 0, q: r.gamma || 0, r: r.alpha || 0 };
    };
    window.addEventListener('deviceorientationabsolute', onOri, true);
    window.addEventListener('deviceorientation', onOri, true);
    window.addEventListener('devicemotion', onMot, true);
  }

  /* ---------- commands ---------- */
  align() {   // fine align: take the current SM attitude as the reference, null the floats
    this.qRef.copy(this.qI); this.theta = { x: 0, y: 0, z: 0 }; this.tumbled = false;
    this.servo.o.i = this.servo.m.i = this.servo.i.i = 0;
    this.flash('FINE ALIGN · REFSMMAT SET');
  }
  cage() {    // coarse align to 0/0/0, then re-reference
    this.caged = true; this.servo.o.reset(); this.servo.m.reset(); this.servo.i.reset();
    this.flash('COARSE ALIGN · GIMBALS → 0');
    clearTimeout(this._cageT);
    this._cageT = setTimeout(() => { this.caged = false; this.align(); }, 1500);
  }
  zeroDv() { this.dv.set(0, 0, 0); this.flash('PIPA COUNTERS ZEROED'); }
  flash(msg) { this.event = { msg, t: this.t }; }
  setSource(s) {
    this.src = s;
    if (s === 'manual') { const a = this._caseAngles(); this.man.roll = a.roll * D2R; this.man.pitch = a.pitch * D2R; this.man.yaw = a.heading * D2R; }
    if (s === 'demo') this.demoT = 0;
    this.flash('SOURCE · ' + s.toUpperCase());
  }
  setDemo(d) { this.demo = d; this.demoT = 0; if (this.src !== 'demo') this.setSource('demo'); }
  setStick(roll, pitch, yaw) { const s = this.fly.stick; s.roll = clamp(roll, -1, 1); s.pitch = clamp(pitch, -1, 1); s.yaw = clamp(yaw, -1, 1); }
  setFlyMode(m) { this.fly.mode = m; this.flash('SCS · ' + (m === 'rate' ? 'RATE CMD' : 'ACCEL CMD')); }
  killRates() { this.fly.w.set(0, 0, 0); this.flash('RATES NULLED'); }
  drag(dx, dy) { this.man.yaw += dx * 0.007; this.man.pitch = clamp(this.man.pitch + dy * 0.007, -1.53, 1.53); }

  /* ---------- attitude sources ---------- */
  _demoAttitude(t) {
    const d = this.demo;
    if (d === 'lock') { const s = Math.min(t / 10, 1.15); const w = clamp((t - 6) / 3, 0, 1); return { roll: 0.10 * Math.sin(t * 0.9), pitch: w * 0.35 * Math.sin(t * 0.9 + 0.8), yaw: s * 92 * D2R }; }
    if (d === 'tumble') return { roll: t * 0.5, pitch: 0.8 * Math.sin(t * 1.15), yaw: t * 0.8 };
    if (d === 'step') { const k = Math.floor(t / 2.4) % 4; const a = [0, 45, -35, 18][k] * D2R; return { roll: -a * 0.55, pitch: a * 0.35, yaw: a }; }
    return { roll: t * 0.17, pitch: 0.28 * Math.sin(t * 0.23 + 1.1), yaw: 0.42 * Math.sin(t * 0.17) };   // PTC barbecue roll
  }
  // exponential approach with an angular-rate cap: no instantaneous jumps of the case
  _approach(target, dt, k, maxRate) {
    const ang = 2 * Math.acos(clamp(Math.abs(this.qCase.dot(target)), 0, 1));
    if (ang < 1e-7) return;
    let a = ang * clamp(k * dt, 0, 1);
    if (maxRate && a > maxRate * dt) a = maxRate * dt;
    this.qCase.slerp(target, Math.min(1, a / ang));
  }
  _updateSource(dt) {
    if (this.src === 'imu' && this.sensorState === 'live') {
      this._approach(this.qTarget, dt, 60 - this.smooth * 250, 0);
    } else if (this.src === 'manual') {
      this._setCaseEuler(this.man.roll, this.man.pitch, this.man.yaw, this.qTarget);
      this._approach(this.qTarget, dt, 9, this.maxSrcRate);
    } else if (this.src === 'fly') {
      // RCS attitude dynamics. Stick → body-axis torque: roll +X, nose-up = −Y, nose-right = −Z
      const f = this.fly, w = f.w, st = f.stick, a = f.accel * dt;
      const cmd = [st.roll, -st.pitch, -st.yaw];
      const comp = [w.x, w.y, w.z], jets = [0, 0, 0];
      for (let k = 0; k < 3; k++) {
        if (f.mode === 'rate') {              // rate command: jets fire until body rate matches the stick, damp when centred
          const want = cmd[k] * f.maxRate, d = want - comp[k];
          const dw = clamp(d, -a, a); comp[k] += dw; jets[k] = Math.abs(dw) > 1e-6 ? Math.sign(dw) : 0;
        } else {                              // acceleration command: jets fire while the stick is deflected, no damping
          comp[k] += cmd[k] * a; jets[k] = Math.abs(cmd[k]) > 0.02 ? Math.sign(cmd[k]) : 0;
        }
      }
      w.set(comp[0], comp[1], comp[2]); f.jets = { x: jets[0], y: jets[1], z: jets[2] };
      const mag = w.length();
      if (mag > 1e-9) { this._q1.setFromAxisAngle(this._v.copy(w).divideScalar(mag), mag * dt); this.qCase.multiply(this._q1).normalize(); }
      this.qTarget.copy(this.qCase);
    } else {
      this.demoT += dt;
      const a = this._demoAttitude(this.demoT);
      this._setCaseEuler(a.roll, a.pitch, a.yaw, this.qTarget);
      this._approach(this.qTarget, dt, 7, this.maxSrcRate);
    }
  }
  _caseAngles() {
    const x = this._v.set(1, 0, 0).applyQuaternion(this.qCase);
    const y = this._v2.set(0, 1, 0).applyQuaternion(this.qCase);
    const z = this._v3.set(0, 0, 1).applyQuaternion(this.qCase);
    return {
      heading: (Math.atan2(x.x, x.y) * R2D + 360) % 360,
      pitch: Math.asin(clamp(x.z, -1, 1)) * R2D,
      roll: Math.atan2(y.z, z.z) * R2D,
    };
  }

  /* ---------- fixed-step physics (500 Hz) ---------- */
  step(dt) {
    const s = this.servo, p = this.p, th = this.theta;
    // 1. rigid kinematics: where the SM actually is
    this.qSM.setFromAxisAngle(AX, s.o.a)
      .multiply(this._q1.setFromAxisAngle(AZ, s.m.a))
      .multiply(this._q2.setFromAxisAngle(AY, s.i.a));
    this.qI.copy(this.qCase).multiply(this.qSM);
    // 2. IRIG floats integrate SM inertial rotation (SM axes) + drift
    if (this._havePrev) {
      this._dq.copy(this.qIprev).invert().multiply(this.qI);
      const sg = this._dq.w < 0 ? -2 : 2;
      th.x += sg * this._dq.x; th.y += sg * this._dq.y; th.z += sg * this._dq.z;
    }
    this.qIprev.copy(this.qI); this._havePrev = true;
    if (this.driftRate) { const k = this.driftRate * D2R * dt; th.x += 0.5 * k; th.y += k; th.z -= 0.3 * k; }
    const FM = p.floatMax;
    for (const k of ['x', 'y', 'z']) { if (th[k] > FM) { th[k] = FM; this.tumbled = true; } else if (th[k] < -FM) { th[k] = -FM; this.tumbled = true; } }
    // 3. resolver chain: gyro errors → gimbal axis displacements
    const ci = Math.cos(s.i.a), si = Math.sin(s.i.a), cm = Math.cos(s.m.a), sm = Math.sin(s.m.a);
    let cmL = cm; if (Math.abs(cmL) < p.secFloor) cmL = (cmL < 0 ? -1 : 1) * p.secFloor;
    const dO = -(th.x * ci + th.z * si) / cmL;
    const dM = -(th.z * ci - th.x * si);
    const dI = -th.y + sm * dO;
    this._secGain = 1 / Math.abs(cmL);
    // 4. torque-motor servos
    if (this.caged) { s.o.step(0, dt, p); s.m.step(0, dt, p); s.i.step(0, dt, p); }
    else { s.o.step(s.o.a + dO, dt, p); s.m.step(s.m.a + dM, dt, p); s.i.step(s.i.a + dI, dt, p); }
    // ideal (IK) angles for the readouts: Rx(o)·Rz(m)·Ry(i) = qCase⁻¹·qRef
    this.mat.makeRotationFromQuaternion(this._q3.copy(this.qCase).invert().multiply(this.qRef));
    const e = this.mat.elements;
    this.ideal.m = Math.asin(clamp(-e[4], -1, 1)); this.ideal.o = Math.atan2(e[6], e[5]); this.ideal.i = Math.atan2(e[8], e[0]);
  }

  update(now) {
    if (!this._last) this._last = now;
    let dt = (now - this._last) / 1000; this._last = now;
    dt = clamp(dt, 0, 0.1);
    this.t += dt;
    this.qCasePrev.copy(this.qCase);
    this._updateSource(dt);
    // case body rates (deg/s) from the attitude change, case axes
    if (dt > 0) {
      this._dq.copy(this.qCasePrev).invert().multiply(this.qCase);
      const sg = (this._dq.w < 0 ? -2 : 2) / dt * R2D, k = 0.35;
      this.wCase.x += (sg * this._dq.x - this.wCase.x) * k; this.wCase.y += (sg * this._dq.y - this.wCase.y) * k; this.wCase.z += (sg * this._dq.z - this.wCase.z) * k;
    }
    this._acc += dt;
    // the case moves continuously: interpolate its attitude across the 2 ms substeps
    this._qNew.copy(this.qCase);
    const DT = 0.002; let n = 0; const total = Math.min(Math.floor(this._acc / DT), 60);
    while (this._acc >= DT && n < 60) {
      n++;
      this.qCase.slerpQuaternions(this.qCasePrev, this._qNew, total > 0 ? n / total : 1);
      this.step(DT); this._acc -= DT;
    }
    this.qCase.copy(this._qNew);
    if (this._acc > 0.25) this._acc = 0;
    // PIPA: ΔV in SM axes (device accel → case → SM)
    this._v.set(this.acc.x, this.acc.y, this.acc.z).applyQuaternion(this._q1.copy(Q_CASE_TO_DEV).invert())
      .applyQuaternion(this._q2.copy(this.qSM).invert());
    this.dv.addScaledVector(this._v, dt);
    this.pip = { x: Math.round(this.dv.x / 0.0585), y: Math.round(this.dv.y / 0.0585), z: Math.round(this.dv.z / 0.0585) };
    this._frames++; this._hzT += dt;
    if (this._hzT > 0.5) { this.hz = Math.round(this._frames / this._hzT); this._frames = 0; this._hzT = 0; }
    this._publish();
  }

  _publish() {
    const s = this.servo, c = this.ideal, t = this.tele, th = this.theta;
    const mga = s.m.a * R2D;
    const err = 2 * Math.acos(clamp(Math.abs(this.qI.dot(this.qRef)), -1, 1)) * R2D;
    const ang = this._caseAngles();
    t.oga = s.o.a * R2D; t.mga = mga; t.iga = s.i.a * R2D;
    t.ogaC = c.o * R2D; t.mgaC = c.m * R2D; t.igaC = c.i * R2D;
    t.ro = s.o.r * R2D; t.rm = s.m.r * R2D; t.ri = s.i.r * R2D;
    t.eo = wrapPi(c.o - s.o.a) * R2D * 60; t.em = wrapPi(c.m - s.m.a) * R2D * 60; t.ei = wrapPi(c.i - s.i.a) * R2D * 60;
    t.thx = th.x * R2D * 60; t.thy = th.y * R2D * 60; t.thz = th.z * R2D * 60;
    t.err = err; t.lockMargin = Math.abs(90 - Math.abs(mga));   // distance of the middle gimbal from either lock
    t.lock = t.lockMargin <= 5 ? 'LOCK' : t.lockMargin <= 20 ? 'WARN' : 'OK';
    t.roll = ang.roll; t.pitch = ang.pitch; t.yaw = ang.heading;
    t.heading = this.sensorState === 'live' ? this.heading : ang.heading;
    t.headingSrc = this.sensorState === 'live' ? this.headingSrc : 'SIM';
    t.hz = this.hz; t.src = this.src.toUpperCase(); t.sensor = this.sensorState;
    t.jets = this.fly.jets; t.flyMode = this.fly.mode; t.w = { x: this.fly.w.x * R2D, y: this.fly.w.y * R2D, z: this.fly.w.z * R2D };
    t.gyro = this.gyro; t.wCase = this.wCase; t.gmag = Math.hypot(this.acc.x, this.acc.y, this.acc.z);
    t.dv = this.dv.length(); t.pip = this.pip; t.caged = this.caged; t.t = this.t;
    t.pinnedM = s.m.pinned; t.tumbled = this.tumbled; t.satO = s.o.sat; t.secGain = this._secGain || 1;
    t.event = this.event && (this.t - this.event.t) < 2.4 ? this.event.msg : null;
    const h = this.history;
    if (h.length === 0 || this.t - h[h.length - 1][0] > 0.08) { h.push([this.t, mga, err]); if (h.length > 360) h.shift(); }
  }
}

const core = new Core();

/* ══════════════════════════ LOOPS / ASSETS ══════════════════════════ */
const _views = [];
function stepManual(seconds, fps) {
  fps = fps || 60;
  const n = Math.round(seconds * fps), dt = 1000 / fps;
  let t = core._last || performance.now();
  for (let i = 0; i < n; i++) { t += dt; core.update(t); }
  _views.forEach((v) => v.renderOnce && v.renderOnce());
  return core.tele;
}
function driveLoop(fn) {   // rAF with a timer fallback — hidden tabs starve rAF
  let last = 0;
  const frame = () => { last = performance.now(); fn(last); };
  const raf = () => { frame(); requestAnimationFrame(raf); };
  requestAnimationFrame(raf);
  setInterval(() => { if (performance.now() - last > 130) frame(); }, 60);
}
let _env = null;
function envTexture() {
  if (_env) return _env;
  const c = document.createElement('canvas'); c.width = 512; c.height = 256;
  const g = c.getContext('2d');
  const grd = g.createLinearGradient(0, 0, 0, 256);
  grd.addColorStop(0, '#e6edf3'); grd.addColorStop(0.45, '#7d8990'); grd.addColorStop(0.53, '#3c4348'); grd.addColorStop(1, '#15181a');
  g.fillStyle = grd; g.fillRect(0, 0, 512, 256);
  g.globalAlpha = 0.95; g.fillStyle = '#ffffff';
  g.fillRect(52, 14, 160, 30); g.fillRect(304, 28, 86, 18);
  g.globalAlpha = 0.30; g.fillRect(0, 118, 512, 5);
  g.globalAlpha = 0.45; g.fillRect(415, 62, 58, 58);
  const t = new THREE.CanvasTexture(c);
  t.mapping = THREE.EquirectangularReflectionMapping; t.colorSpace = THREE.SRGBColorSpace;
  _env = t; return t;
}
function labelTexture(lines, w, h, bg, fg) {
  const c = document.createElement('canvas'); c.width = w; c.height = h;
  const g = c.getContext('2d');
  g.fillStyle = bg; g.fillRect(0, 0, w, h);
  g.strokeStyle = 'rgba(0,0,0,.28)'; g.lineWidth = 5; g.strokeRect(7, 7, w - 14, h - 14);
  g.fillStyle = fg; g.textAlign = 'center'; g.textBaseline = 'middle';
  const step = h / (lines.length + 0.5);
  lines.forEach((l, i) => {
    g.font = (i === 0 ? '700 ' : '500 ') + (i === 0 ? Math.round(h * 0.21) : Math.round(h * 0.14)) + 'px Helvetica, Arial, sans-serif';
    g.fillText(l, w / 2, step * (i + 0.8));
  });
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 4;
  return t;
}
function makeMats() {
  return {
    alum: new THREE.MeshStandardMaterial({ name: 'aluminium', color: 0xbcc2c7, metalness: 0.72, roughness: 0.3 }),
    alum2: new THREE.MeshStandardMaterial({ name: 'aluminium_cast', color: 0xa2a8ad, metalness: 0.6, roughness: 0.45 }),
    dark: new THREE.MeshStandardMaterial({ name: 'anodised_dark', color: 0x23282c, metalness: 0.6, roughness: 0.44 }),
    black: new THREE.MeshStandardMaterial({ name: 'black_phenolic', color: 0x121417, metalness: 0.4, roughness: 0.58 }),
    steel: new THREE.MeshStandardMaterial({ name: 'steel', color: 0x8b9299, metalness: 1, roughness: 0.2 }),
    gold: new THREE.MeshStandardMaterial({ name: 'gold_foil', color: 0xc79a4b, metalness: 1, roughness: 0.3 }),
    ivory: new THREE.MeshStandardMaterial({ name: 'instrument_case', color: 0xdedbd3, metalness: 0.3, roughness: 0.5 }),
    pcb: new THREE.MeshStandardMaterial({ name: 'pcb', color: 0x2c5a45, metalness: 0.2, roughness: 0.7 }),
    copper: new THREE.MeshStandardMaterial({ name: 'copper_winding', color: 0xa9662f, metalness: 0.9, roughness: 0.42 }),
    glass: new THREE.MeshPhysicalMaterial({ name: 'window_glass', color: 0xcfe0e6, metalness: 0, roughness: 0.06, transparent: true, opacity: 0.14, side: THREE.DoubleSide, depthWrite: false, envMapIntensity: 2.2 }),
  };
}
function shellLathe(r, thick, a0, a1, seg, rseg) {
  const pts = [];
  for (let i = 0; i <= seg; i++) { const a = a0 + (a1 - a0) * i / seg; pts.push(new THREE.Vector2(Math.sin(a) * r, Math.cos(a) * r)); }
  for (let i = seg; i >= 0; i--) { const a = a0 + (a1 - a0) * i / seg; pts.push(new THREE.Vector2(Math.sin(a) * (r - thick), Math.cos(a) * (r - thick))); }
  return new THREE.LatheGeometry(pts, rseg);
}
function boltRing(grp, radius, count, y, mat, size) {
  const g = new THREE.CylinderGeometry(size, size, size * 1.5, 6);
  for (let i = 0; i < count; i++) {
    const a = (i / count) * Math.PI * 2;
    const m = new THREE.Mesh(g, mat); m.name = 'bolt_' + i;
    m.position.set(Math.cos(a) * radius, y, Math.sin(a) * radius); grp.add(m);
  }
}
function cloneMats(group) {   // own material instances for a subtree
  const map = new Map();
  group.traverse((o) => {
    if (o.isMesh && o.material) {
      if (!map.has(o.material)) map.set(o.material, o.material.clone());
      o.material = map.get(o.material);
    }
  });
}
// bearing / torque-motor drum along local Y, with a coloured axis index ring
function bearing(M, r, len, col, name) {
  const g = new THREE.Group(); g.name = name;
  const body = new THREE.Mesh(new THREE.CylinderGeometry(r, r, len, 24), M.alum); g.add(body);
  const cap = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.86, r * 0.86, len * 0.22, 24), M.black); cap.position.y = len * 0.5; g.add(cap);
  const coil = new THREE.Mesh(new THREE.TorusGeometry(r * 0.98, r * 0.16, 8, 28), M.copper); coil.rotation.x = Math.PI / 2; coil.position.y = -len * 0.18; g.add(coil);
  const idx = new THREE.Mesh(new THREE.TorusGeometry(r * 1.04, r * 0.09, 8, 28), new THREE.MeshBasicMaterial({ color: col })); idx.rotation.x = Math.PI / 2; idx.position.y = len * 0.2; g.add(idx);
  return g;
}

/* ══════════════════════════ IMU MODEL ══════════════════════════ */
function buildIMU(M) {
  const root = new THREE.Group(); root.name = 'imu';
  const caseG = new THREE.Group(); caseG.name = 'case'; root.add(caseG);
  // shell built around +Y, then tipped so the aperture faces +X (the outer gimbal axis)
  const shellG = new THREE.Group(); shellG.name = 'case_shell'; shellG.rotation.z = -Math.PI / 2; caseG.add(shellG);
  const rear = new THREE.Mesh(shellLathe(1.0, 0.045, Math.PI * 0.5, Math.PI, 40, 72), M.alum); rear.name = 'case_rear_dome'; shellG.add(rear);
  const band = new THREE.Mesh(new THREE.CylinderGeometry(1.005, 1.005, 0.13, 72, 1, true), M.alum2); band.name = 'case_equator_band'; shellG.add(band);
  const flange = new THREE.Mesh(new THREE.TorusGeometry(1.02, 0.035, 12, 80), M.alum); flange.name = 'case_flange'; flange.rotation.x = Math.PI / 2; shellG.add(flange);
  boltRing(shellG, 1.015, 24, 0.075, M.steel, 0.028);
  const liner = new THREE.Mesh(new THREE.SphereGeometry(0.94, 56, 36), new THREE.MeshStandardMaterial({ name: 'case_liner', color: 0x0d1012, metalness: 0.25, roughness: 0.82, side: THREE.BackSide }));
  liner.name = 'case_inner_liner'; shellG.add(liner);
  // rear outer-axis bearing housing on the case (−X)
  const rearBearing = bearing(M, 0.16, 0.34, 0xff5a4a, 'OGA_case_bearing'); rearBearing.position.y = -1.0; rearBearing.rotation.x = Math.PI; shellG.add(rearBearing);

  // front cover: annular bezel + glass porthole (removable / fadeable)
  const front = new THREE.Group(); front.name = 'front_cover'; shellG.add(front);
  const bezel = new THREE.Mesh(shellLathe(1.0, 0.05, 0.95, Math.PI * 0.5, 26, 72), M.alum); bezel.name = 'case_front_bezel'; front.add(bezel);
  const lip = new THREE.Mesh(new THREE.TorusGeometry(0.815, 0.026, 10, 64), M.alum2); lip.name = 'window_retaining_ring'; lip.rotation.x = Math.PI / 2; lip.position.y = 0.59; front.add(lip);
  boltRing(front, 0.90, 16, 0.45, M.steel, 0.022);
  const glass = new THREE.Mesh(new THREE.SphereGeometry(0.99, 48, 16, 0, Math.PI * 2, 0, 0.96), M.glass); glass.name = 'window_glass'; glass.renderOrder = 3; front.add(glass);
  const plate = new THREE.Mesh(new THREE.PlaneGeometry(0.30, 0.115),
    new THREE.MeshStandardMaterial({ map: labelTexture(['APOLLO', 'INERTIAL MEASUREMENT UNIT', 'BLOCK II · IRN 2018'], 512, 196, '#e7e4dc', '#16181b'), metalness: 0.1, roughness: 0.65 }));
  plate.name = 'id_plate'; plate.position.set(0, 0.62, 0.78); plate.rotation.set(-Math.PI / 2 + 0.95, 0, 0); front.add(plate);
  cloneMats(front);

  // connectors (±Z) and navigation-base feet (−Y)
  const conn = (z, r, h, name) => {
    const g = new THREE.Group(); g.name = name;
    const body = new THREE.Mesh(new THREE.CylinderGeometry(r, r * 1.12, h, 24), M.alum2); body.rotation.x = Math.PI / 2; g.add(body);
    const cap = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.92, r * 0.92, 0.03, 24), M.black); cap.rotation.x = Math.PI / 2; cap.position.z = (h / 2) * Math.sign(z); g.add(cap);
    for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
      const pin = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, 0.05, 6), M.gold); pin.rotation.x = Math.PI / 2;
      pin.position.set((i - 1) * r * 0.5, (j - 1) * r * 0.5, (h / 2 + 0.01) * Math.sign(z)); g.add(pin);
    }
    g.position.set(0, 0, z); return g;
  };
  caseG.add(conn(-1.12, 0.115, 0.28, 'connector_1A')); caseG.add(conn(1.12, 0.09, 0.24, 'connector_2B'));
  const nav = new THREE.Group(); nav.name = 'navigation_base';
  [[0.62, 0.5], [-0.62, 0.5], [0, -0.78]].forEach((p, i) => {
    const foot = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.1, 0.2), M.alum2);
    foot.position.set(p[0], -0.74, p[1]); foot.name = 'mount_foot_' + i; nav.add(foot);
  });
  caseG.add(nav);

  /* ---- OUTER GIMBAL: drum rotating about case X on a slewing bearing ---- */
  const outer = new THREE.Group(); outer.name = 'outer_gimbal'; caseG.add(outer);
  const oAsm = new THREE.Group(); oAsm.name = 'OGA'; outer.add(oAsm);
  const drum = new THREE.Mesh(new THREE.CylinderGeometry(0.86, 0.86, 0.30, 72, 1, true), M.dark); drum.name = 'OGA_drum'; drum.rotation.z = Math.PI / 2; drum.material.side = THREE.DoubleSide; oAsm.add(drum);
  for (const sx of [1, -1]) {
    const fl = new THREE.Mesh(new THREE.TorusGeometry(0.86, 0.028, 10, 90), M.alum); fl.rotation.y = Math.PI / 2; fl.position.x = sx * 0.15; fl.name = 'OGA_flange'; oAsm.add(fl);
  }
  const race = new THREE.Group(); race.rotation.z = -Math.PI / 2; race.position.x = 0.19; oAsm.add(race);   // bolt circle on the front lip = bearing race
  boltRing(race, 0.80, 20, 0, M.steel, 0.02);
  const oIdx = new THREE.Mesh(new THREE.TorusGeometry(0.89, 0.006, 6, 90), new THREE.MeshBasicMaterial({ color: 0xff5a4a })); oIdx.rotation.y = Math.PI / 2; oIdx.position.x = 0.15; oIdx.name = 'OGA_axis_index'; oAsm.add(oIdx);
  const backWeb = new THREE.Mesh(new THREE.RingGeometry(0.30, 0.86, 72, 1), M.dark); backWeb.rotation.y = -Math.PI / 2; backWeb.position.x = -0.15; backWeb.material.side = THREE.DoubleSide; backWeb.name = 'OGA_back_web'; oAsm.add(backWeb);
  // middle-axis bearings carried by the drum at ±Z
  for (const sz of [1, -1]) { const b = bearing(M, 0.10, 0.20, 0x64a8ff, 'MGA_bearing'); b.position.set(0, 0, sz * 0.76); b.rotation.x = sz > 0 ? Math.PI / 2 : -Math.PI / 2; oAsm.add(b); }

  /* ---- MIDDLE GIMBAL: octagonal frame in the YZ plane, rotating about Z ---- */
  const middle = new THREE.Group(); middle.name = 'middle_gimbal'; outer.add(middle);
  const mgaRing = new THREE.Group(); mgaRing.name = 'MGA'; middle.add(mgaRing);
  const frame = new THREE.Mesh(new THREE.TorusGeometry(0.64, 0.032, 6, 8), M.black); frame.rotation.y = Math.PI / 2; frame.rotation.x = Math.PI / 8; frame.name = 'MGA_octagon_frame'; mgaRing.add(frame);
  const frame2 = new THREE.Mesh(new THREE.TorusGeometry(0.64, 0.018, 6, 8), M.alum2); frame2.rotation.copy(frame.rotation); frame2.position.x = 0.05; frame2.name = 'MGA_frame_rail'; mgaRing.add(frame2);
  for (const sz of [1, -1]) { const st = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.05, 0.12, 16), M.steel); st.rotation.x = Math.PI / 2; st.position.set(0, 0, sz * 0.66); st.name = 'MGA_trunnion'; mgaRing.add(st); }
  // inner-axis bearings on the frame at ±Y
  for (const sy of [1, -1]) { const b = bearing(M, 0.085, 0.18, 0x67e08a, 'IGA_bearing'); b.position.set(0, sy * 0.58, 0); b.rotation.x = sy > 0 ? 0 : Math.PI; mgaRing.add(b); }
  cloneMats(mgaRing);

  /* ---- INNER GIMBAL = STABLE MEMBER, rotating about Y on the frame bearings ---- */
  const inner = new THREE.Group(); inner.name = 'inner_gimbal'; middle.add(inner);
  const sm = new THREE.Group(); sm.name = 'stable_member'; inner.add(sm);
  const smMech = new THREE.Group(); smMech.name = 'sm_mechanism'; sm.add(smMech);
  for (const sy of [1, -1]) { const sh = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 0.34, 14), M.steel); sh.position.y = sy * 0.36; sh.name = 'IGA_shaft'; smMech.add(sh); }
  const block = new THREE.Mesh(new THREE.CylinderGeometry(0.36, 0.36, 0.34, 8), M.alum); block.rotation.z = Math.PI / 2; block.rotation.y = Math.PI / 8; block.name = 'sm_beryllium_block'; smMech.add(block);
  const face = new THREE.Mesh(new THREE.CylinderGeometry(0.37, 0.37, 0.03, 8), M.alum2); face.rotation.z = Math.PI / 2; face.rotation.y = Math.PI / 8; face.position.x = 0.17; face.name = 'sm_face_plate'; smMech.add(face);
  // IRIG gyros on the +X face, input axes X / Y / Z
  [[1, 0, 0, 0.19, 0.10, -0.14], [0, 1, 0, 0.26, -0.11, 0.12], [0, 0, 1, 0.26, 0.17, 0.10]].forEach((v, i) => {
    const g = new THREE.Group(); g.name = 'IRIG_25_gyro_' + 'XYZ'[i];
    const can = new THREE.Mesh(new THREE.CylinderGeometry(0.075, 0.075, 0.2, 24), M.steel); g.add(can);
    const end = new THREE.Mesh(new THREE.CylinderGeometry(0.078, 0.078, 0.018, 24), M.gold); end.position.y = 0.105; g.add(end);
    const coil = new THREE.Mesh(new THREE.TorusGeometry(0.078, 0.016, 8, 24), M.copper); coil.rotation.x = Math.PI / 2; g.add(coil);
    g.quaternion.setFromUnitVectors(AY, new THREE.Vector3(v[0], v[1], v[2]));
    g.position.set(v[3], v[4], v[5]); smMech.add(g);
  });
  // PIPA accelerometers
  [[0.24, 0.22, -0.14, 0], [0.24, -0.20, -0.16, 0.5], [0.05, -0.28, 0.20, 1.2]].forEach((v, i) => {
    const g = new THREE.Group(); g.name = 'PIPA_16_accel_' + 'XYZ'[i];
    const box = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.11, 0.11), M.ivory); g.add(box);
    const lid = new THREE.Mesh(new THREE.BoxGeometry(0.155, 0.01, 0.115), M.steel); lid.position.y = 0.058; g.add(lid);
    const lbl = new THREE.Mesh(new THREE.PlaneGeometry(0.12, 0.05), new THREE.MeshStandardMaterial({ map: labelTexture(['PIPA ' + 'XYZ'[i], 'AGC 16'], 256, 110, '#f0eee8', '#1a1c1f'), roughness: 0.7, metalness: 0.05 }));
    lbl.position.y = 0.0645; lbl.rotation.x = -Math.PI / 2; g.add(lbl);
    g.position.set(v[0], v[1], v[2]); g.rotation.set(0, v[3], Math.PI / 2 * (i === 2 ? 0 : 1)); smMech.add(g);
  });
  const eBox = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.08, 0.22), M.pcb); eBox.name = 'sm_preamp'; eBox.position.set(-0.2, -0.16, 0.18); smMech.add(eBox);
  const smPlate = new THREE.Mesh(new THREE.PlaneGeometry(0.22, 0.09), new THREE.MeshStandardMaterial({ map: labelTexture(['APOLLO', 'STABLE MEMBER · SM 116'], 512, 210, '#e7e4dc', '#16181b'), metalness: 0.1, roughness: 0.65 }));
  smPlate.name = 'sm_label'; smPlate.position.set(0.19, -0.02, 0.22); smPlate.rotation.y = Math.PI / 2; smMech.add(smPlate);
  const wireCols = [0xd23a2e, 0xe0b23a, 0x2f6fd0, 0xe8e6df, 0x3aa06a];
  for (let i = 0; i < 8; i++) {
    const a0 = Math.random() * Math.PI * 2, a1 = a0 + 1.0 + Math.random(), r0 = 0.37 + Math.random() * 0.03, pts = [];
    for (let k = 0; k <= 5; k++) { const a = a0 + (a1 - a0) * (k / 5); pts.push(new THREE.Vector3(-0.12 + 0.3 * (k / 5) * Math.random(), Math.cos(a) * r0, Math.sin(a) * r0)); }
    const tube = new THREE.Mesh(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 24, 0.01, 6, false),
      new THREE.MeshStandardMaterial({ color: wireCols[i % wireCols.length], roughness: 0.55, metalness: 0.05 }));
    tube.name = 'harness_' + i; smMech.add(tube);
  }
  // SM axis triad
  const triad = new THREE.Group(); triad.name = 'sm_axis_triad';
  [[0xff5a4a, AX], [0x67e08a, AY], [0x64a8ff, AZ]].forEach(([col, dir], i) => {
    const m = new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0.92 });
    const shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.008, 0.008, 0.62, 8), m);
    const tip = new THREE.Mesh(new THREE.ConeGeometry(0.026, 0.07, 12), m);
    const q = new THREE.Quaternion().setFromUnitVectors(AY, dir);
    shaft.quaternion.copy(q); shaft.position.copy(dir).multiplyScalar(0.31); tip.quaternion.copy(q); tip.position.copy(dir).multiplyScalar(0.65);
    shaft.name = 'axis_' + 'XYZ'[i]; tip.name = 'axis_tip_' + 'XYZ'[i]; triad.add(shaft); triad.add(tip);
  });
  smMech.add(triad);

  /* ---- FLIGHT DISPLAY: graduated 8-ball on the platform + case-fixed bezel, reticle, needles ---- */
  const ball = new THREE.Mesh(new THREE.SphereGeometry(0.55, 96, 64), new THREE.MeshStandardMaterial({ name: 'fdai_ball_paint', map: ballTexture(), roughness: 0.5, metalness: 0.04 }));
  ball.name = 'attitude_ball'; sm.add(ball);
  const fdaiG = new THREE.Group(); fdaiG.name = 'fdai_bezel_display'; caseG.add(fdaiG);
  const maskMat = new THREE.MeshStandardMaterial({ name: 'fdai_mask', color: 0x0b0d0f, roughness: 0.9, metalness: 0.1, side: THREE.DoubleSide });
  const mask = new THREE.Mesh(new THREE.RingGeometry(0.555, 1.0, 128, 1), maskMat); mask.name = 'fdai_mask_ring'; mask.rotation.y = Math.PI / 2; mask.position.x = 0.40; fdaiG.add(mask);
  const maskBack = new THREE.Mesh(new THREE.CircleGeometry(0.62, 64), maskMat); maskBack.name = 'fdai_mask_back'; maskBack.rotation.y = Math.PI / 2; maskBack.position.x = -0.05; fdaiG.add(maskBack);
  const scale = new THREE.Mesh(new THREE.RingGeometry(0.60, 0.78, 128, 1), new THREE.MeshStandardMaterial({ name: 'bezel_scale', map: bezelScaleTexture(), transparent: true, roughness: 0.7, metalness: 0.05 }));
  scale.name = 'roll_scale'; scale.rotation.y = Math.PI / 2; scale.position.x = 0.60; fdaiG.add(scale);
  const white = new THREE.MeshBasicMaterial({ color: 0xe8e6e1 }), orange = new THREE.MeshBasicMaterial({ color: 0xe0952f });
  const rollPivot = new THREE.Group(); rollPivot.name = 'roll_pointer_pivot'; fdaiG.add(rollPivot);
  const rollIndex = new THREE.Mesh(new THREE.ConeGeometry(0.03, 0.07, 3), white); rollIndex.name = 'roll_pointer'; rollIndex.position.set(0.63, 0.555, 0); rollIndex.rotation.z = Math.PI; rollPivot.add(rollIndex);
  const retH = new THREE.Mesh(new THREE.BoxGeometry(0.01, 0.012, 0.40), white); retH.name = 'reticle_h'; retH.position.x = 0.60; fdaiG.add(retH);
  const retV = new THREE.Mesh(new THREE.BoxGeometry(0.01, 0.12, 0.012), white); retV.name = 'reticle_v'; retV.position.set(0.60, 0.06, 0); fdaiG.add(retV);
  const retC = new THREE.Mesh(new THREE.TorusGeometry(0.05, 0.006, 6, 32), white); retC.name = 'reticle_ring'; retC.rotation.y = Math.PI / 2; retC.position.x = 0.60; fdaiG.add(retC);
  const nRoll = new THREE.Mesh(new THREE.BoxGeometry(0.012, 0.32, 0.012), orange); nRoll.name = 'needle_roll_err'; nRoll.position.set(0.62, 0.36, 0); fdaiG.add(nRoll);
  const nYaw = new THREE.Mesh(new THREE.BoxGeometry(0.012, 0.32, 0.012), orange); nYaw.name = 'needle_yaw_err'; nYaw.position.set(0.62, -0.36, 0); fdaiG.add(nYaw);
  const nPitch = new THREE.Mesh(new THREE.BoxGeometry(0.012, 0.012, 0.32), orange); nPitch.name = 'needle_pitch_err'; nPitch.position.set(0.62, 0, 0.36); fdaiG.add(nPitch);
  // rate pointers on the bezel edges (white triangles)
  const rRoll = new THREE.Mesh(new THREE.ConeGeometry(0.022, 0.05, 3), white); rRoll.name = 'rate_roll'; rRoll.position.set(0.66, 0.70, 0); rRoll.rotation.z = Math.PI; fdaiG.add(rRoll);
  const rYaw = new THREE.Mesh(new THREE.ConeGeometry(0.022, 0.05, 3), white); rYaw.name = 'rate_yaw'; rYaw.position.set(0.66, -0.70, 0); fdaiG.add(rYaw);
  const rPitch = new THREE.Mesh(new THREE.ConeGeometry(0.022, 0.05, 3), white); rPitch.name = 'rate_pitch'; rPitch.position.set(0.66, 0, 0.70); rPitch.rotation.x = Math.PI / 2; fdaiG.add(rPitch);
  const needles = { roll: nRoll, yaw: nYaw, pitch: nPitch, rRoll, rYaw, rPitch, rollPivot };
  const mech = [oAsm, mgaRing, smMech];

  return { root, caseG, front, outer, middle, inner, mgaRing, sm, triad, glass, ball, fdaiG, needles, mech };
}
function bezelScaleTexture() {
  const S = 1024, c = document.createElement('canvas'); c.width = S; c.height = S;
  const g = c.getContext('2d'), cx = S / 2, cy = S / 2, R = S / 2;
  g.fillStyle = '#1a1d20'; g.beginPath(); g.arc(cx, cy, R, 0, Math.PI * 2); g.fill();
  g.strokeStyle = '#f2f0ea'; g.lineCap = 'butt';
  for (let d = 0; d < 360; d += 5) {
    const a = d * D2R, maj = d % 30 === 0, mid = d % 10 === 0;
    const r0 = R * (maj ? 0.80 : mid ? 0.86 : 0.90), r1 = R * 0.97;
    g.lineWidth = maj ? 9 : mid ? 6 : 3; g.beginPath();
    g.moveTo(cx + Math.sin(a) * r0, cy - Math.cos(a) * r0); g.lineTo(cx + Math.sin(a) * r1, cy - Math.cos(a) * r1); g.stroke();
  }
  g.fillStyle = '#f2f0ea'; g.font = '700 46px Helvetica, Arial, sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
  for (let d = 0; d < 360; d += 30) { const a = d * D2R, r = R * 0.72; g.fillText(String(d / 10).padStart(2, '0'), cx + Math.sin(a) * r, cy - Math.cos(a) * r); }
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8; return t;
}

/* ══════════════════════════ IMU VIEW ══════════════════════════ */
function createIMUView(canvas) {
  const M = makeMats();
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
  renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 2));
  renderer.toneMapping = THREE.ACESFilmicToneMapping; renderer.toneMappingExposure = 1.05;
  renderer.outputColorSpace = THREE.SRGBColorSpace; renderer.localClippingEnabled = true;
  const scene = new THREE.Scene(); scene.environment = envTexture(); scene.environmentIntensity = 0.85;
  const camera = new THREE.PerspectiveCamera(36, 1, 0.05, 300);
  scene.add(new THREE.HemisphereLight(0xa8bccd, 0x14171a, 0.6));
  const key = new THREE.DirectionalLight(0xffffff, 2.0); key.position.set(3.0, 3.0, 1.8); scene.add(key);
  const fill = new THREE.DirectionalLight(0xbcd4ff, 0.55); fill.position.set(-1.5, -1, 3); scene.add(fill);
  const rim = new THREE.DirectionalLight(0xffd9a0, 1.1); rim.position.set(-3, 1.2, -1.5); scene.add(rim);
  const inner = new THREE.PointLight(0xfff2dd, 4.2, 7, 2); inner.position.set(1.3, 0.45, 0.4); scene.add(inner);

  const imu = buildIMU(M); scene.add(imu.root);

  // inertial backdrop: stars + local horizon ring, counter-rotated by the case attitude
  const starsG = new THREE.Group(); starsG.name = 'inertial_sphere'; scene.add(starsG);
  const N = 900, pos = new Float32Array(N * 3), col = new Float32Array(N * 3);
  for (let i = 0; i < N; i++) {
    const u = Math.random() * 2 - 1, th = Math.random() * Math.PI * 2, s = Math.sqrt(1 - u * u), R = 26 + Math.random() * 8;
    pos[i * 3] = Math.cos(th) * s * R; pos[i * 3 + 1] = u * R; pos[i * 3 + 2] = Math.sin(th) * s * R;
    const b = 0.35 + Math.random() * 0.65; col[i * 3] = b; col[i * 3 + 1] = b * (0.94 + Math.random() * 0.06); col[i * 3 + 2] = b * (0.9 + Math.random() * 0.1);
  }
  const sGeo = new THREE.BufferGeometry(); sGeo.setAttribute('position', new THREE.BufferAttribute(pos, 3)); sGeo.setAttribute('color', new THREE.BufferAttribute(col, 3));
  starsG.add(new THREE.Points(sGeo, new THREE.PointsMaterial({ size: 0.28, vertexColors: true, transparent: true, opacity: 0.85 })));
  const horizon = new THREE.Mesh(new THREE.TorusGeometry(1.42, 0.005, 6, 128), new THREE.MeshBasicMaterial({ color: 0x4de0c8, transparent: true, opacity: 0.22 }));
  horizon.name = 'local_horizon'; starsG.add(horizon);   // ENU horizon: normal = Up (Z)
  const north = new THREE.Mesh(new THREE.ConeGeometry(0.04, 0.12, 10), new THREE.MeshBasicMaterial({ color: 0x4de0c8 })); north.position.set(0, 1.42, 0); north.rotation.x = -Math.PI / 2; north.name = 'north_mark'; starsG.add(north);

  /* --- camera orbit --- */
  const cam = { az: 0.95, el: 0.32, r: 6, tAz: 0.95, tEl: 0.32, zoom: 1, base: 6, init: 0, look: -0.22, tLook: -0.22, fly: false };
  let dragMode = 'orbit', down = false, lx = 0, ly = 0, pinch0 = 0, r0 = 0; const pt = new Map();
  canvas.addEventListener('pointerdown', (e) => { canvas.setPointerCapture(e.pointerId); pt.set(e.pointerId, e); down = true; lx = e.clientX; ly = e.clientY; if (pt.size === 2) { const [a, b] = [...pt.values()]; pinch0 = Math.hypot(a.clientX - b.clientX, a.clientY - b.clientY); r0 = cam.zoom; } });
  canvas.addEventListener('pointermove', (e) => {
    if (!down) return; if (pt.has(e.pointerId)) pt.set(e.pointerId, e);
    if (pt.size === 2) { const [a, b] = [...pt.values()]; const d = Math.hypot(a.clientX - b.clientX, a.clientY - b.clientY); if (pinch0 > 0) cam.zoom = clamp(r0 * pinch0 / d, 0.55, 2.4); return; }
    const dx = e.clientX - lx, dy = e.clientY - ly; lx = e.clientX; ly = e.clientY;
    if (dragMode === 'vehicle' && core.src === 'manual') core.drag(dx, dy);
    else { cam.tAz -= dx * 0.007; cam.tEl = clamp(cam.tEl + dy * 0.007, -1.35, 1.35); }
  });
  const up = (e) => { pt.delete(e.pointerId); if (pt.size === 0) { down = false; pinch0 = 0; } };
  canvas.addEventListener('pointerup', up); canvas.addEventListener('pointercancel', up);
  canvas.addEventListener('wheel', (e) => { e.preventDefault(); cam.zoom = clamp(cam.zoom * (1 + e.deltaY * 0.0012), 0.55, 2.4); }, { passive: false });

  /* --- display state --- */
  const st = { mode: 'cut', reveal: 1, section: 0, paused: false };
  const clipPlane = new THREE.Plane(new THREE.Vector3(0, 0, -1), 1.2);
  const isUnder = (o, p) => { let q = o; while (q) { if (q === p) return true; q = q.parent; } return false; };
  function applyMode() {
    const m = st.mode, solid = m === 'solid';
    imu.mech.forEach((g) => { g.visible = !solid; });
    imu.ball.visible = solid; imu.fdaiG.visible = solid;
    imu.root.traverse((o) => {
      if (!o.isMesh || !o.material) return;
      const mat = o.material;
      if (mat.name === 'window_glass') { mat.opacity = m === 'solid' ? 0.30 : 0.13; return; }
      if (mat.isMeshBasicMaterial) return;
      const isCase = isUnder(o, imu.caseG) && !isUnder(o, imu.outer);
      if (m === 'xray') { mat.transparent = true; mat.opacity = isCase ? 0.10 : 0.42; mat.depthWrite = false; }
      else { mat.transparent = false; mat.opacity = 1; mat.depthWrite = true; }
      mat.needsUpdate = true;
    });
  }
  applyMode();
  const api = {
    setMode(m) { st.mode = m; applyMode(); if (m === 'solid') { cam.tAz = Math.PI / 2; cam.tEl = 0.0; cam.zoom = cam.fly ? 1.0 : 0.86; } else cam.zoom = cam.fly ? 1.15 : 1; },
    setFlyLayout(on) { cam.fly = !!on; cam.tLook = on ? -0.58 : -0.22; cam.zoom = on ? (st.mode === 'solid' ? 1.0 : 1.15) : (st.mode === 'solid' ? 0.86 : 1); },
    setReveal(v) { st.reveal = v; },
    setSection(v) { st.section = v; clipPlane.constant = 1.2 - v * 1.35; renderer.clippingPlanes = v > 0.01 ? [clipPlane] : []; },
    setStars(v) { starsG.visible = v; },
    setTriad(v) { imu.triad.visible = v; },
    setDragMode(m) { dragMode = m; },
    setPaused(v) { st.paused = v; },
    snapView(n) {
      if (n === 'front') { cam.tAz = Math.PI / 2; cam.tEl = 0.02; }
      if (n === 'top') { cam.tAz = Math.PI / 2; cam.tEl = 1.3; }
      if (n === 'side') { cam.tAz = 0; cam.tEl = 0.05; }
      if (n === 'iso') { cam.tAz = 0.95; cam.tEl = 0.32; }
    },
    dispose() { renderer.dispose(); },
  };
  let W = 0, H = 0;
  function resize() {
    const p = canvas.parentElement; if (!p) return;
    const w = p.clientWidth, h = p.clientHeight;
    if (!w || !h || (w === W && h === H)) return;
    W = w; H = h; renderer.setSize(w, h, false);
    camera.aspect = w / h; camera.updateProjectionMatrix();
    const vf = 2 * Math.tan(camera.fov * Math.PI / 360);
    cam.base = Math.max(3.3 / vf, 3.3 / (vf * camera.aspect));
    if (!cam.init) { cam.init = 1; cam.r = cam.base * cam.zoom; }
  }

  const qInv = new THREE.Quaternion(); let pulse = 0;
  const frame = (now, force) => {
    resize();
    if (st.paused && !force) return;
    const t = core.tele;
    imu.outer.rotation.x = core.servo.o.a; imu.middle.rotation.z = core.servo.m.a; imu.inner.rotation.y = core.servo.i.a;
    starsG.quaternion.copy(qInv.copy(core.qCase).invert());
    const rv = st.reveal; imu.front.position.y = rv * 0.55;
    imu.front.traverse((o) => { if (o.isMesh && o.material && rv > 0.02) { o.material.transparent = true; o.material.opacity = Math.max(0.06, 1 - rv * 0.94); o.material.depthWrite = rv < 0.4; } });
    imu.glass.visible = st.mode !== 'xray' && rv < 0.95;
    const solid = st.mode === 'solid';
    imu.fdaiG.visible = solid && rv < 0.5;
    if (solid) {
      const kk = 0.25 / (3 * D2R), th = core.theta, N = imu.needles, w = core.wCase, kr = 0.28 / 20;
      N.roll.position.z = clamp(th.x * kk, -0.3, 0.3); N.yaw.position.z = clamp(th.z * kk, -0.3, 0.3); N.pitch.position.y = clamp(-th.y * kk, -0.3, 0.3);
      N.rRoll.position.z = clamp(-w.x * kr, -0.3, 0.3); N.rYaw.position.z = clamp(w.z * kr, -0.3, 0.3); N.rPitch.position.y = clamp(-w.y * kr, -0.3, 0.3);
      N.rollPivot.rotation.x = core.servo.o.a;
    }
    const near = clamp((28 - t.lockMargin) / 28, 0, 1); pulse += 0.09;
    imu.mgaRing.traverse((o) => { if (o.isMesh && o.material && o.material.emissive) { const k = near * (0.55 + 0.45 * Math.sin(pulse)); o.material.emissive.setRGB(k * 0.85, k * 0.34, 0.02); } });
    horizon.material.opacity = 0.16 + 0.08 * Math.sin(core.t * 1.6) + near * 0.25;
    cam.az += (cam.tAz - cam.az) * 0.12; cam.el += (cam.tEl - cam.el) * 0.12; cam.r += (cam.base * cam.zoom - cam.r) * 0.12; cam.look += (cam.tLook - cam.look) * 0.12;
    camera.position.set(Math.cos(cam.el) * Math.sin(cam.az) * cam.r, Math.sin(cam.el) * cam.r + cam.look, Math.cos(cam.el) * Math.cos(cam.az) * cam.r);
    camera.lookAt(0, cam.look, 0); renderer.render(scene, camera);
  };
  driveLoop(frame);
  api.renderOnce = () => frame(performance.now(), true);
  _views.push(api);
  return api;
}

/* ══════════════════════════ FDAI (8-BALL) ══════════════════════════ */
function ballTexture() {
  const w = 2048, h = 1024, c = document.createElement('canvas'); c.width = w; c.height = h;
  const g = c.getContext('2d');
  g.fillStyle = '#d9d4c8'; g.fillRect(0, 0, w, h / 2); g.fillStyle = '#bdb8ad'; g.fillRect(0, h / 2, w, h / 2);
  g.fillStyle = '#b5342a'; g.fillRect(0, 0, w, 100); g.fillRect(0, h - 100, w, 100);   // gimbal-lock caps at ±Y (|MGA| > 72°)
  const line = (x1, y1, x2, y2, col, lw) => { g.strokeStyle = col; g.lineWidth = lw; g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y2); g.stroke(); };
  for (let d = -80; d <= 80; d += 10) { const y = h / 2 - (d / 180) * h; line(0, y, w, y, d === 0 ? '#111' : 'rgba(0,0,0,.45)', d % 30 === 0 ? 3 : 1.6); }
  for (let a = 0; a < 360; a += 10) { const x = (a / 360) * w, maj = a % 30 === 0; line(x, 0, x, h, maj ? (a % 90 === 0 ? '#111' : 'rgba(0,0,0,.55)') : 'rgba(0,0,0,.28)', maj ? 3 : 1.4); }
  line(0, h / 2, w, h / 2, '#0d0d0d', 6);
  g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillStyle = '#15181a';
  for (let a = 0; a < 360; a += 30) {
    const x = (a / 360) * w;
    for (const d of [60, 30, -30, -60]) { const y = h / 2 - (d / 180) * h; g.font = '700 30px Helvetica, Arial, sans-serif'; g.fillText(String(a / 10).padStart(2, '0'), x, y - 24); }
    g.font = '700 40px Helvetica, Arial, sans-serif'; g.fillText(String(a / 10).padStart(2, '0'), x, h / 2 - 36);
  }
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8; return t;
}
function createFDAIView(canvas) {
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, preserveDrawingBuffer: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 2)); renderer.outputColorSpace = THREE.SRGBColorSpace; renderer.toneMapping = THREE.ACESFilmicToneMapping;
  const scene = new THREE.Scene(); scene.environment = envTexture();
  const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 50); camera.position.set(4.2, 0, 0); camera.lookAt(0, 0, 0);   // pilot looks along −X (case roll axis)
  scene.add(new THREE.HemisphereLight(0xbcd0e0, 0x16181a, 0.7));
  const l1 = new THREE.DirectionalLight(0xffffff, 1.5); l1.position.set(3, 2, 1.2); scene.add(l1);
  const l2 = new THREE.DirectionalLight(0xa8c0ff, 0.5); l2.position.set(1, -1, -2); scene.add(l2);
  const ball = new THREE.Mesh(new THREE.SphereGeometry(1, 96, 64), new THREE.MeshStandardMaterial({ name: 'fdai_ball', map: ballTexture(), roughness: 0.44, metalness: 0.04 }));
  ball.name = 'fdai_ball'; scene.add(ball);
  let W = 0, H = 0;
  function resize() { const p = canvas.parentElement; if (!p) return; const w = p.clientWidth, h = p.clientHeight; if (!w || !h || (w === W && h === H)) return; W = w; H = h; renderer.setSize(w, h, false); camera.aspect = w / h; camera.updateProjectionMatrix(); }
  const st = { paused: false };
  const frame = () => { resize(); ball.quaternion.copy(core.qSM); renderer.render(scene, camera); };
  driveLoop(() => { if (!st.paused) frame(); });
  const api = { setPaused(v) { st.paused = v; }, renderOnce: frame, dispose() { renderer.dispose(); } };
  _views.push(api); return api;
}

/* ══════════════════════════ EXPORT ══════════════════════════ */
let _started = false;
function startCore() { if (_started) return; _started = true; driveLoop((t) => core.update(t)); }
startCore();
window.AGS = { core, createIMUView, createFDAIView, THREE, D2R, R2D, step: stepManual, ready: true };
window.dispatchEvent(new Event('ags-ready'));
