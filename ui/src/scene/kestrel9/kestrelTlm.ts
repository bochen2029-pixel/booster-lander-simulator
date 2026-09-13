// kestrelTlm.ts — the field-name adapter between the app's decoded telemetry (TlmFrame, camelCase —
// ui/src/net/decode.ts) and the vendored Kestrel-9 renderer, which reads the packet's OWN names
// (snake_case, exactly core/protocol.h: throttle_act, n_eng, gimbal_act[2], fins_act[4],
// deploy_frac, stroke[4], Q_heat, p_amb, mach, qbar) plus one derived extra, bell_alt.
// Pure — no three, no DOM — so the vitest node environment pins the mapping.
import type { TlmFrame } from "../../net/decode";

export interface KestrelTlm {
  throttle_act: number;
  n_eng: number;
  gimbal_act: [number, number];
  fins_act: [number, number, number, number];
  deploy_frac: number;
  stroke: [number, number, number, number];
  Q_heat: number;
  p_amb: number;
  mach: number;
  qbar: number;
  /** bell-exit height over the surface under the vehicle [m] (clamps the free jet into a wall jet). */
  bell_alt: number;
}

export function makeKestrelTlm(): KestrelTlm {
  return {
    throttle_act: 0,
    n_eng: 0,
    gimbal_act: [0, 0],
    fins_act: [0, 0, 0, 0],
    deploy_frac: 0,
    stroke: [0, 0, 0, 0],
    Q_heat: 0,
    p_amb: 101325,
    mach: 0,
    qbar: 0,
    bell_alt: 1e5,
  };
}

/**
 * Height of the bell exit above the surface: base-plane Z minus the surface Z (0 on land, deck_z
 * at sea) minus the bell's depth below the base plane. Never negative — the sim's contact model
 * owns the ground; this only shapes the jet.
 */
export function bellAltitude(baseZ: number, groundZ: number, bellLength: number): number {
  const h = baseZ - groundZ - bellLength;
  return h > 0 ? h : 0;
}

/** Map one decoded frame onto the reusable snake_case object (no allocation). */
export function tlmToKestrel(f: TlmFrame, bellAltM: number, out: KestrelTlm): KestrelTlm {
  out.throttle_act = f.throttleAct;
  out.n_eng = f.nEng;
  out.gimbal_act[0] = f.gimbalAct[0];
  out.gimbal_act[1] = f.gimbalAct[1];
  for (let i = 0; i < 4; i++) {
    out.fins_act[i] = f.finsAct[i];
    out.stroke[i] = f.stroke[i];
  }
  out.deploy_frac = f.deployFrac;
  out.Q_heat = f.Qheat;
  out.p_amb = f.pAmb;
  out.mach = f.mach;
  out.qbar = f.qbar;
  out.bell_alt = bellAltM;
  return out;
}
