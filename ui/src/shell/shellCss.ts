// shell.css.ts — the cockpit chrome styles, injected as a <style> tag by mount()
// so the shell module is fully self-contained (no separate .css import to wire
// into index.html; keeps other agents' main.ts diffs clean). Dark-studio palette
// consistent with the Bonsai/booster house look (near-black glass, thin mono).

export const SHELL_CSS = `
.lz-topstrip {
  position: fixed; top: 0; left: 0; right: 0; z-index: 40;
  display: flex; align-items: center; gap: 14px;
  padding: 6px 12px;
  font: 12px/1.4 ui-monospace, "Cascadia Code", Consolas, monospace;
  color: #c8d0da;
  background: linear-gradient(180deg, rgba(8,11,14,0.82) 0%, rgba(8,11,14,0.35) 100%);
  backdrop-filter: blur(6px);
  user-select: none; pointer-events: none;
}
.lz-topstrip > * { pointer-events: auto; }
.lz-spacer { flex: 1 1 auto; }

/* --- connection chip --- */
.lz-chip {
  display: inline-flex; align-items: center; gap: 8px;
  padding: 4px 10px; border-radius: 999px;
  background: rgba(0,0,0,0.42); border: 1px solid rgba(255,255,255,0.07);
}
.lz-chip__dot {
  width: 9px; height: 9px; border-radius: 50%; flex: 0 0 auto;
  box-shadow: 0 0 8px currentColor;
}
.lz-chip__dot--pulse { animation: lz-pulse 1.4s ease-in-out infinite; }
@keyframes lz-pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.35; } }
.lz-chip__label { font-weight: 600; letter-spacing: 0.04em; }
.lz-chip__ident { color: #9aa4b0; }
.lz-chip__ver { font-size: 11px; }
.lz-chip__note { color: #7a828e; font-style: italic; }
.lz-chip__relight {
  margin-left: 4px; padding: 2px 9px; border-radius: 5px;
  background: #b5432f; color: #fff; border: none; cursor: pointer;
  font: inherit; font-weight: 600; letter-spacing: 0.05em;
}
.lz-chip__relight:hover { background: #cf4d36; }
.lz-chip__relight:disabled { opacity: 0.5; cursor: default; }

/* --- picker --- */
.lz-picker {
  display: inline-flex; align-items: center; gap: 6px;
  padding: 3px 8px; border-radius: 8px;
  background: rgba(0,0,0,0.42); border: 1px solid rgba(255,255,255,0.07);
}
.lz-picker__tag { color: #6b7480; font-size: 11px; }
.lz-picker input {
  background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1);
  color: #dfe6ee; border-radius: 4px; padding: 2px 6px; font: inherit;
}
.lz-picker__scenario { width: 90px; }
.lz-picker__seed, .lz-picker__run { width: 62px; }
.lz-picker input:disabled { opacity: 0.5; }
.lz-picker__launch {
  padding: 3px 12px; border-radius: 5px; border: none; cursor: pointer;
  background: #2f6db5; color: #fff; font: inherit; font-weight: 600; letter-spacing: 0.05em;
}
.lz-picker__launch:hover { background: #3a7fce; }
.lz-picker__launch:disabled { opacity: 0.5; cursor: default; }
.lz-picker--readonly { opacity: 0.85; }

/* --- toggles --- */
.lz-toggles { display: inline-flex; gap: 6px; }
.lz-toggle {
  padding: 3px 9px; border-radius: 5px; cursor: pointer; font: inherit;
  background: rgba(0,0,0,0.42); border: 1px solid rgba(255,255,255,0.09); color: #aab3bf;
}
.lz-toggle:hover { border-color: rgba(255,255,255,0.22); color: #dfe6ee; }
.lz-toggle--active { background: rgba(63,120,181,0.28); border-color: #3a7fce; color: #dfe6ee; }

/* --- panels (wire log / stderr) --- */
.lz-panel {
  position: fixed; z-index: 39;
  display: none; flex-direction: column;
  width: 520px; max-height: 40vh;
  background: rgba(6,9,12,0.9); border: 1px solid rgba(255,255,255,0.09);
  border-radius: 8px; overflow: hidden;
  font: 11px/1.45 ui-monospace, "Cascadia Code", Consolas, monospace;
  color: #c2cad4; backdrop-filter: blur(4px);
}
.lz-wirelog { top: 44px; right: 12px; }
.lz-stderr { top: 44px; right: 544px; }
.lz-panel__header {
  padding: 5px 10px; color: #8a93a0; letter-spacing: 0.05em;
  border-bottom: 1px solid rgba(255,255,255,0.07); background: rgba(0,0,0,0.35);
  flex: 0 0 auto;
}
.lz-panel__body { overflow-y: auto; padding: 6px 10px; }
.lz-wirelog__row { white-space: pre; color: #9fd0b0; }
.lz-stderr__row { white-space: pre-wrap; word-break: break-word; color: #d0b98a; }
.lz-stderr__row--muted { color: #6b7480; }

/* thin scrollbars — native-app feel */
.lz-panel__body::-webkit-scrollbar { width: 8px; }
.lz-panel__body::-webkit-scrollbar-thumb { background: rgba(255,255,255,0.15); border-radius: 4px; }
`;

export function injectShellCss(): void {
  if (document.getElementById("lz-shell-css")) return;
  const style = document.createElement("style");
  style.id = "lz-shell-css";
  style.textContent = SHELL_CSS;
  document.head.appendChild(style);
}
