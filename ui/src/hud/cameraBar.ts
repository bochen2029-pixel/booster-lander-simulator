// cameraBar.ts — a VISIBLE camera-view selector. The presets used to be keyboard-only
// (keys 1–4 / 0), so the onboard hull-cam etc. existed but had no button. This is that
// button: a click drives the DirectorRig live (the SAME path as the hotkeys). Renderer-only;
// never crosses the telemetry boundary. Stays in sync with the hotkeys + auto-cuts by polling
// the director's current preset each frame (refresh()).

import type { DirectorRig, CameraPreset } from "../director/director";
import type { Vector3 } from "three/webgpu";

interface View {
  label: string;
  preset?: CameraPreset;
  auto?: boolean;
  title: string;
}

// Order top→bottom. FREE_ORBIT is the default external view; ONBOARD_DOWN is the
// "camera bolted to the rocket looking down the legs + plume" the operator asked for.
const VIEWS: View[] = [
  { label: "◄ ORBIT", preset: "FREE_ORBIT", title: "External orbit — drag to rotate, wheel to zoom (default)" },
  { label: "▼ ONBOARD", preset: "ONBOARD_DOWN", title: "Camera mounted ON the hull, looking down the legs + plume" },
  { label: "» CHASE", preset: "CHASE", title: "Chase cam trailing the descent" },
  { label: "≡ PAD", preset: "PAD_LONG_LENS", title: "Pad long-lens tracking shot" },
  { label: "◎ AUTO", auto: true, title: "Cinematic auto-director — cuts cameras on flight events" },
];

const BTN_CSS =
  "cursor:pointer;user-select:none;text-align:left;min-width:108px;" +
  "background:rgba(10,14,18,.72);color:#9fc4dc;border:1px solid rgba(120,150,175,.35);" +
  "border-radius:6px;padding:6px 11px;font:600 11px ui-monospace,Menlo,Consolas,monospace;" +
  "letter-spacing:.04em;box-shadow:0 2px 10px #0007;transition:background .12s,color .12s";

export interface CameraBar {
  /** Re-sync the highlight with the director (call each frame — auto-cuts + hotkeys). */
  refresh(): void;
}

export function installCameraBar(
  director: DirectorRig,
  getPos: () => Vector3,
  getVel: () => Vector3
): CameraBar {
  const bar = document.createElement("div");
  bar.style.cssText =
    "position:fixed;top:52px;right:14px;z-index:25;display:flex;flex-direction:column;gap:5px;pointer-events:auto";

  const label = document.createElement("div");
  label.textContent = "CAMERA";
  label.style.cssText =
    "color:#5f7a8c;font:600 9px ui-monospace,Consolas,monospace;letter-spacing:.12em;" +
    "text-align:right;margin-bottom:1px;text-shadow:0 1px 2px #000a;pointer-events:none";
  bar.appendChild(label);

  const buttons: { el: HTMLButtonElement; v: View }[] = [];
  for (const v of VIEWS) {
    const b = document.createElement("button");
    b.textContent = v.label;
    b.title = v.title;
    b.style.cssText = BTN_CSS;
    b.onclick = () => {
      if (v.auto) director.setAuto(true);
      else director.select(v.preset!, getPos(), getVel());
      paint();
    };
    bar.appendChild(b);
    buttons.push({ el: b, v });
  }
  document.body.appendChild(bar);

  function paint(): void {
    const activePreset = director.preset;
    const isAuto = director.auto;
    for (const { el, v } of buttons) {
      const active = v.auto ? isAuto : !isAuto && v.preset === activePreset;
      el.style.background = active ? "rgba(38,66,86,.95)" : "rgba(10,14,18,.72)";
      el.style.color = active ? "#7fe0a8" : "#9fc4dc";
      el.style.borderColor = active ? "#7fe0a8aa" : "rgba(120,150,175,.35)";
    }
  }
  paint();
  return { refresh: paint };
}
