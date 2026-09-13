/* KESTREL-9 STL export — closed, printable / voxelizable solids.
 *
 * The render model uses open cylinders, zero-thickness bells and 14 mm lattice
 * walls (right for pixels, wrong for slicers and voxelizers). This pass rebuilds
 * the vehicle from CLOSED primitives in the vehicle's local frame and writes a
 * binary STL:
 *
 *   exportSTL(THREE, booster, { variant: 'cfd' })     // metres, 1:1, sim Z-up
 *   exportSTL(THREE, booster, { variant: 'print', scale: 1/200 })  // mm, Z-up
 *
 * variant 'cfd'   : every closed part incl. thin ones; nozzle interiors hollow to
 *                   the throat so an inlet BC can sit on the throat plane; units m.
 * variant 'print' : thin parts (< ~0.5 mm at scale) dropped, lattice fins become
 *                   solid plates, units mm. Overlapping closed shells are fine for
 *                   slicers (union). For CFD parity-voxelizers, run a boolean
 *                   union / remesh first (Blender Remesh, MeshLib) — see README.
 *
 * Frame: three Y-up -> sim Z-up via §10.7 inverse: (x, y, z)_sim = (x, -z, y)_three.
 */

const TAU = Math.PI * 2;

const REUSE = /^(LegHip|LegSeg|LegCollar|FootBody|FootPad|LegStrut|RcsBody|RcsNozzle|FinRoot|FinHinge|OctawebRib|Turbopump|GasGenDuct|FuelManifold|GimbalActuator)/;
const THIN = /^(RcsNozzle|LegStrut|GasGenDuct|GimbalActuator)/;

export function buildExportModel(THREE, booster, variant = 'cfd') {
  const V = booster.vehicle, R = V.radius;
  const out = new THREE.Group();
  out.name = 'KESTREL9_export_' + variant;
  const seg = variant === 'print' ? 96 : 128;
  const isPrint = variant === 'print';

  // pose for export: legs down, fins neutral, engines straight
  const prev = { ...booster.state, gimbal: [...booster.state.gimbal], fins: [...booster.state.fins], stroke: [...booster.state.stroke] };
  booster.setTelemetry({ deploy_frac: 1, fins_act: [0, 0, 0, 0], gimbal_act: [0, 0], stroke: [0, 0, 0, 0] });
  booster.group.updateMatrixWorld(true);
  const inv = booster.group.matrixWorld.clone().invert();

  const add = (geo, matrix, name) => {
    const m = new THREE.Mesh(geo);
    m.name = name;
    if (matrix) m.applyMatrix4(matrix);
    out.add(m);
    return m;
  };
  const localOf = o => inv.clone().multiply(o.matrixWorld);

  // tank barrel — closed
  add(new THREE.CylinderGeometry(R, R, V.barrel, seg, 1, false),
    new THREE.Matrix4().makeTranslation(0, V.barrel / 2, 0), 'Barrel');

  // interstage — one closed "cup" lathe (open top, 5 cm wall, floor = LOX dome plane)
  const yB = V.barrel, yT = V.barrel + V.interstage;
  const cup = [
    new THREE.Vector2(0, yB), new THREE.Vector2(R, yB), new THREE.Vector2(R, yT),
    new THREE.Vector2(R - 0.05, yT), new THREE.Vector2(R - 0.05, yB + 0.12), new THREE.Vector2(0, yB + 0.12),
  ];
  add(new THREE.LatheGeometry(cup, seg), null, 'Interstage');

  // raceway — closed box along the dorsal line
  const raceLen = V.barrel - 3.0;
  const race = new THREE.Matrix4().makeRotationY(THREE.MathUtils.degToRad(-22.5))
    .multiply(new THREE.Matrix4().makeTranslation(0, 2.6 + raceLen / 2, R + 0.06));
  add(new THREE.BoxGeometry(0.5, raceLen, 0.2), race, 'Raceway');

  // grid fins — solid plates (lattice is far below print / voxel resolution)
  const [fs, fh] = V.finPanel;
  booster.parts.fins.forEach((f, i) => {
    add(new THREE.BoxGeometry(fs + 0.06, fh + 0.06, isPrint ? 0.18 : 0.13), localOf(f.panel), 'GridFin' + i);
  });

  // engines — thick closed bell: outer contour down, lip, inner contour back up, throat cap
  const rt = 0.115, re = V.bellExit, yTh = -0.06, yE = -V.bellLength, wall = isPrint ? 0.05 : 0.03;
  const prof = [new THREE.Vector2(0, 0.42), new THREE.Vector2(0.215, 0.42), new THREE.Vector2(0.215, 0.10), new THREE.Vector2(rt, yTh)];
  const N = 20;
  for (let i = 1; i <= N; i++) { const t = i / N; prof.push(new THREE.Vector2(rt + (re - rt) * Math.pow(t, 0.6), yTh + (yE - yTh) * t)); }
  prof.push(new THREE.Vector2(re + 0.024, yE - 0.035), new THREE.Vector2(re + 0.024 - wall, yE - 0.035));
  for (let i = N; i >= 1; i--) { const t = i / N; prof.push(new THREE.Vector2(Math.max(0.02, rt + (re - rt) * Math.pow(t, 0.6) - wall), yTh + (yE - yTh) * t)); }
  prof.push(new THREE.Vector2(Math.max(0.02, rt - wall), yTh), new THREE.Vector2(0, yTh));   // throat plane cap
  const bellGeo = new THREE.LatheGeometry(prof, isPrint ? 72 : 96);
  booster.parts.engines.forEach(e => {
    add(bellGeo, localOf(e.gimbal), (e.index === 0 ? 'MerlinCenter' : 'Merlin' + e.index) + '_Bell');
    // chamber dome as a full sphere (render uses an open hemisphere)
    const dome = localOf(e.mesh).multiply(new THREE.Matrix4().makeTranslation(0, 0.42, 0));
    add(new THREE.SphereGeometry(0.2, 20, 14), dome, 'ChamberDome' + e.index);
  });

  // octaweb base plate — closed disc that ties the bells to the barrel bottom
  add(new THREE.CylinderGeometry(R - 0.02, R - 0.02, 0.12, seg, 1, false),
    new THREE.Matrix4().makeTranslation(0, 0.06, 0), 'BasePlate');

  // reuse every closed primitive from the render model (legs, feet, RCS, fin roots, plumbing)
  booster.group.traverse(o => {
    if (!o.isMesh || !REUSE.test(o.name)) return;
    if (isPrint && THIN.test(o.name)) return;
    add(o.geometry, localOf(o), o.name);
  });

  // restore the live pose
  booster.setTelemetry({ deploy_frac: prev.deploy, fins_act: prev.fins, gimbal_act: prev.gimbal, stroke: prev.stroke });
  return out;
}

/** Binary STL of every mesh under root; scale in output units per metre; Z-up (sim world). */
export function toBinarySTL(THREE, root, { scale = 1, zUp = true, header = 'KESTREL-9 procedural booster' } = {}) {
  root.updateMatrixWorld(true);
  const tris = [];
  const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3();
  const im = new THREE.Matrix4(), m = new THREE.Matrix4();
  root.traverse(o => {
    if (!o.isMesh && !o.isInstancedMesh) return;
    const g = o.geometry.index ? o.geometry.toNonIndexed() : o.geometry;
    const pos = g.attributes.position;
    const count = o.isInstancedMesh ? o.count : 1;
    for (let k = 0; k < count; k++) {
      m.copy(o.matrixWorld);
      if (o.isInstancedMesh) { o.getMatrixAt(k, im); m.multiply(im); }
      const flip = m.determinant() < 0;
      for (let i = 0; i < pos.count; i += 3) {
        a.fromBufferAttribute(pos, i).applyMatrix4(m);
        b.fromBufferAttribute(pos, i + 1).applyMatrix4(m);
        c.fromBufferAttribute(pos, i + 2).applyMatrix4(m);
        tris.push(flip ? [a.clone(), c.clone(), b.clone()] : [a.clone(), b.clone(), c.clone()]);
      }
    }
    if (g !== o.geometry) g.dispose();
  });

  const buf = new ArrayBuffer(84 + tris.length * 50);
  const dv = new DataView(buf);
  const enc = new TextEncoder().encode(header.slice(0, 79));
  new Uint8Array(buf, 0, 80).set(enc);
  dv.setUint32(80, tris.length, true);
  let off = 84;
  const n = new THREE.Vector3(), e1 = new THREE.Vector3(), e2 = new THREE.Vector3();
  const put = v => {
    // three Y-up -> sim Z-up: (x, y, z)_sim = (x, -z, y)_three
    const x = v.x * scale, y = zUp ? -v.z * scale : v.y * scale, z = zUp ? v.y * scale : v.z * scale;
    dv.setFloat32(off, x, true); dv.setFloat32(off + 4, y, true); dv.setFloat32(off + 8, z, true); off += 12;
  };
  for (const [p, q, r] of tris) {
    e1.subVectors(q, p); e2.subVectors(r, p); n.crossVectors(e1, e2).normalize();
    put(n); put(p); put(q); put(r);
    dv.setUint16(off, 0, true); off += 2;
  }
  return buf;
}

export function exportSTL(THREE, booster, { variant = 'cfd', scale, filename } = {}) {
  const model = buildExportModel(THREE, booster, variant);
  const s = scale ?? (variant === 'print' ? 1000 / 200 : 1);   // print default 1:200 in mm
  const buf = toBinarySTL(THREE, model, { scale: s, header: `KESTREL-9 ${variant} ${variant === 'print' ? 'mm 1:' + Math.round(1000 / s) : 'metres 1:1'} Z-up` });
  model.traverse(o => { if (o.isMesh && !REUSE.test(o.name)) o.geometry.dispose(); });
  const name = filename || (variant === 'print' ? `kestrel9_print_1-${Math.round(1000 / s)}_mm.stl` : 'kestrel9_cfd_1-1_m.stl');
  const blob = new Blob([buf], { type: 'model/stl' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url; link.download = name; document.body.appendChild(link); link.click();
  setTimeout(() => { document.body.removeChild(link); URL.revokeObjectURL(url); }, 2000);
  return { name, bytes: buf.byteLength, triangles: (buf.byteLength - 84) / 50 };
}

export default exportSTL;
