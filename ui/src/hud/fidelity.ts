// fidelity.ts — an in-game GRAPHICS FIDELITY toggle (HIGH / LOW).
//
// The heavy bits of this renderer are the volumetric raymarched plume (fx/plume.ts) and
// the bloom post-pass (scene/postfx.ts), plus rendering at 2× device pixel ratio. LOW
// drops the render resolution to 1× and bypasses bloom (renderer.render direct) — a big
// win on low-end GPUs — while HIGH keeps the full cinematic path. The choice persists in
// localStorage. Renderer-only; never crosses the telemetry boundary.

export type Fidelity = "HIGH" | "LOW";

export interface FidelityHandle {
  readonly level: Fidelity;
}

const KEY = "bl_fidelity";

/** Mount the GFX toggle (top-right). `onChange` is called once on mount and on each
 *  toggle with the new level; the caller applies pixel-ratio / bloom / etc. */
export function installFidelity(onChange: (level: Fidelity) => void): FidelityHandle {
  let level: Fidelity = localStorage.getItem(KEY) === "LOW" ? "LOW" : "HIGH";

  const btn = document.createElement("button");
  btn.style.cssText =
    "position:fixed;top:14px;right:14px;z-index:25;pointer-events:auto;cursor:pointer;" +
    "user-select:none;background:rgba(10,14,18,.72);color:#9fc4dc;" +
    "border:1px solid rgba(120,150,175,.35);border-radius:6px;padding:6px 12px;" +
    "font:600 11px ui-monospace,Menlo,Consolas,monospace;letter-spacing:.05em;" +
    "box-shadow:0 2px 10px #0007";
  btn.title = "Graphics fidelity — LOW drops bloom + render resolution for weaker GPUs";

  const paint = () => {
    btn.textContent = `⚙ GFX · ${level}`;
    btn.style.color = level === "HIGH" ? "#7fe0a8" : "#ffcf6b";
    btn.style.borderColor = level === "HIGH" ? "#7fe0a855" : "#ffcf6b55";
  };
  btn.onclick = () => {
    level = level === "HIGH" ? "LOW" : "HIGH";
    localStorage.setItem(KEY, level);
    paint();
    onChange(level);
  };
  paint();
  document.body.appendChild(btn);

  onChange(level); // apply the persisted choice on boot
  return {
    get level() {
      return level;
    },
  };
}
