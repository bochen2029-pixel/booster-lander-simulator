// dom.ts — the CONSTELLATION DOM chrome (drop zone, file pickers, summary strip,
// filter chips + legend, hover/pin card, A/B transition strip, schematic caveat,
// toast, empty state).
//
// PURE DOM — NO three/WebGPU imports. This split lets the chrome be smoke-tested in
// jsdom without booting a GPU renderer. app.ts imports buildDom() and wires the
// callbacks to the 3D scene. No framework: this is chrome (canon E.2 — React is for
// the main cockpit's panels; this standalone page is vanilla).

import {
  diffRuns,
  CAUSE_ORDER,
  type ClassifiedRun,
  type RunSummary,
  type CauseBucket,
} from "./runData";
import type { Pick } from "./types";
import { BUCKET_COLOR, BUCKET_LABEL, cssHex } from "./design";

/** A loaded + classified CSV, with its summary. Shared with app.ts. */
export interface LoadedFile {
  name: string;
  runs: ClassifiedRun[];
  summary: RunSummary;
  skipped: number;
}

/** The imperative API buildDom returns; app.ts drives it. */
export interface DomApi {
  canvas: HTMLCanvasElement;
  arcsToggle: HTMLInputElement;
  setEmpty(empty: boolean): void;
  setSummary(a: LoadedFile | null, b: LoadedFile | null): void;
  setTransition(d: ReturnType<typeof diffRuns>, aName: string, bName: string): void;
  clearTransition(): void;
  showCard(pick: Pick, fileLabel: string | undefined, pinned: boolean): void;
  hideCard(): void;
  reflectFilter(active: Set<CauseBucket>): void;
  onFilterToggle(cb: (b: CauseBucket) => void): void;
  onFiles(cb: (files: File[], side?: "A" | "B") => void): void;
  onClearB(cb: () => void): void;
  onUnpin(cb: () => void): void;
  onResetView(cb: () => void): void;
  toast(msg: string): void;
}

export function buildDom(): DomApi {
  const root = document.getElementById("app") ?? document.body;
  root.innerHTML = "";

  const canvas = el("canvas", "const-canvas") as HTMLCanvasElement;
  root.appendChild(canvas);

  // ── top bar: title + load controls + A/B + reset ────────────────────────────
  const top = el("div", "const-top");
  top.innerHTML = `
    <div class="const-title">
      <span class="const-dot"></span> MC CONSTELLATION
      <span class="const-sub">end-state population · pad-centric</span>
    </div>`;
  const loadWrap = el("div", "const-loadwrap");
  const pickA = fileButton("Load CSV (A)");
  const pickB = fileButton("Load CSV (B) — A/B diff");
  const clearB = btn("Clear B");
  const resetView = btn("Reset view");
  const arcsLabel = el("label", "const-arcs");
  const arcsToggle = el("input") as HTMLInputElement;
  arcsToggle.type = "checkbox";
  arcsToggle.checked = true;
  arcsLabel.appendChild(arcsToggle);
  arcsLabel.appendChild(document.createTextNode(" schematic arcs"));
  loadWrap.append(pickA.button, pickB.button, clearB, resetView, arcsLabel);
  top.appendChild(loadWrap);
  root.appendChild(top);

  // ── summary strip ───────────────────────────────────────────────────────────
  const summary = el("div", "const-summary");
  root.appendChild(summary);

  // ── transition strip (A/B) ──────────────────────────────────────────────────
  const transition = el("div", "const-transition");
  transition.style.display = "none";
  root.appendChild(transition);

  // ── filter chips + legend ───────────────────────────────────────────────────
  const chips = el("div", "const-chips");
  const chipEls = new Map<CauseBucket, HTMLElement>();
  for (const b of CAUSE_ORDER) {
    const c = el("button", "const-chip");
    c.dataset.bucket = b;
    c.innerHTML = `<span class="const-sw" style="background:${cssHex(BUCKET_COLOR[b])}"></span>${BUCKET_LABEL[b]} <span class="const-n" data-n="${b}">0</span>`;
    chips.appendChild(c);
    chipEls.set(b, c);
  }
  root.appendChild(chips);

  // ── hover / pin card ────────────────────────────────────────────────────────
  const card = el("div", "const-card");
  card.style.display = "none";
  root.appendChild(card);

  // ── schematic caveat (always visible, honest) ───────────────────────────────
  const caveat = el("div", "const-caveat");
  caveat.innerHTML =
    `SCHEMATIC: descent arcs + glyph bearings are synthesized from run index ` +
    `(golden-angle layout) — the CSV carries touchdown <b>miss magnitude only</b>, ` +
    `not the true ground track. Radius = td_lat (real). True ribbons need recorded ` +
    `trajectories (.bltlm) — wave&nbsp;F2.`;
  root.appendChild(caveat);

  // ── empty-state overlay ─────────────────────────────────────────────────────
  const empty = el("div", "const-empty");
  empty.innerHTML =
    `<div class="const-empty-inner">` +
    `<div class="const-empty-h">Drop a per-run CSV</div>` +
    `<div class="const-empty-p">or use “Load CSV (A)”. Load a second file for the A/B flip diff.<br>` +
    `Try <code>runs/d012_entry_v4.csv</code>.</div></div>`;
  root.appendChild(empty);

  // ── toast ───────────────────────────────────────────────────────────────────
  const toastEl = el("div", "const-toast");
  toastEl.style.display = "none";
  root.appendChild(toastEl);

  injectStyles();

  // ── drag & drop (whole window) ──────────────────────────────────────────────
  let filesCb: (files: File[], side?: "A" | "B") => void = () => {};
  ["dragenter", "dragover"].forEach((t) =>
    window.addEventListener(t, (e) => {
      e.preventDefault();
      root.classList.add("const-dragging");
    })
  );
  ["dragleave", "drop"].forEach((t) =>
    window.addEventListener(t, (e) => {
      e.preventDefault();
      if (t === "drop") root.classList.remove("const-dragging");
    })
  );
  window.addEventListener("drop", (e) => {
    root.classList.remove("const-dragging");
    const dt = (e as DragEvent).dataTransfer;
    if (!dt) return;
    const files = Array.from(dt.files).filter((f) => /\.csv$/i.test(f.name));
    if (files.length) filesCb(files);
  });

  // file picker plumbing
  pickA.input.addEventListener("change", () => {
    if (pickA.input.files) filesCb(Array.from(pickA.input.files), "A");
    pickA.input.value = "";
  });
  pickB.input.addEventListener("change", () => {
    if (pickB.input.files) filesCb(Array.from(pickB.input.files), "B");
    pickB.input.value = "";
  });

  let clearBCb = () => {};
  clearB.addEventListener("click", () => clearBCb());
  let resetCb = () => {};
  resetView.addEventListener("click", () => resetCb());
  // late-bound unpin callback (the card is rebuilt on each show; its ✕ calls this)
  let unpinCb = () => {};

  let toastTimer = 0;

  return {
    canvas,
    arcsToggle,

    setEmpty(e) {
      empty.style.display = e ? "grid" : "none";
    },

    setSummary(a, b) {
      if (!a) {
        summary.innerHTML = "";
        for (const [, c] of chipEls) {
          const n = c.querySelector(".const-n");
          if (n) n.textContent = "0";
        }
        this.setEmpty(true);
        return;
      }
      this.setEmpty(false);
      const s = a.summary;
      const pct = ((s.landedFrac || 0) * 100).toFixed(0);
      const parts: string[] = [
        `<span class="const-file">A · ${escapeHtml(a.name)}</span>`,
        `<span class="const-big">${s.counts.landed}/${s.total} landed <b>${pct}%</b></span>`,
      ];
      for (const bkt of CAUSE_ORDER) {
        if (bkt === "landed") continue;
        if (s.counts[bkt] === 0 && bkt === "other") continue;
        parts.push(
          `<span class="const-cnt"><span class="const-sw" style="background:${cssHex(BUCKET_COLOR[bkt])}"></span>${BUCKET_LABEL[bkt]} ${s.counts[bkt]}</span>`
        );
      }
      if (b) {
        parts.push(
          `<span class="const-file const-b">B · ${escapeHtml(b.name)}</span>`,
          `<span class="const-cnt">${b.summary.counts.landed}/${b.summary.total} landed <b>${((b.summary.landedFrac || 0) * 100).toFixed(0)}%</b></span>`
        );
      }
      summary.innerHTML = parts.join("");
      // update chip counts (from A)
      for (const bkt of CAUSE_ORDER) {
        const n = chipEls.get(bkt)?.querySelector(".const-n") as HTMLElement | null;
        if (n) n.textContent = String(s.counts[bkt]);
      }
    },

    setTransition(d, aName, bName) {
      transition.style.display = "flex";
      const flows = Object.entries(d.flowCounts).sort((x, y) => y[1] - x[1]);
      const chipsHtml = flows
        .map(([k, v]) => {
          const [from, to] = k.split("->") as [CauseBucket, CauseBucket];
          return `<span class="const-flow"><span class="const-sw" style="background:${cssHex(BUCKET_COLOR[from])}"></span>→<span class="const-sw" style="background:${cssHex(BUCKET_COLOR[to])}"></span> ${BUCKET_LABEL[from]}→${BUCKET_LABEL[to]} <b>${v}</b></span>`;
        })
        .join("");
      transition.innerHTML =
        `<span class="const-tlabel">A→B TRANSITIONS · ${escapeHtml(aName)} → ${escapeHtml(bName)}</span>` +
        `<span class="const-flip">${d.flippedCount} flipped</span>` +
        (chipsHtml || `<span class="const-flow">no bucket changes</span>`) +
        (d.onlyInA.length || d.onlyInB.length
          ? `<span class="const-only">A-only ${d.onlyInA.length} · B-only ${d.onlyInB.length}</span>`
          : "");
    },

    clearTransition() {
      transition.style.display = "none";
      transition.innerHTML = "";
    },

    showCard(pick, fileLabel, isPinned) {
      const r = pick.run;
      const row = r.row;
      const col = cssHex(BUCKET_COLOR[r.bucket]);
      const causeLine = describeCause(r);
      card.style.display = "block";
      card.innerHTML =
        `<div class="const-card-h" style="border-color:${col}">` +
        `<span class="const-sw" style="background:${col}"></span>` +
        `run ${row.run}` +
        (fileLabel ? ` <span class="const-side">[${pick.side}]</span>` : "") +
        `<span class="const-verdict">${BUCKET_LABEL[r.bucket]}</span>` +
        (isPinned ? `<span class="const-pin" data-unpin="1">PINNED ✕</span>` : "") +
        `</div>` +
        `<table class="const-kv">` +
        kv("verdict", `${row.verdict}`) +
        kv("td_v", `${row.td_v.toFixed(3)} m/s`) +
        kv("td_lat", `${row.td_lat.toFixed(2)} m ${r.onPad ? "(on-pad)" : "(off-pad)"}`) +
        kv("td_tilt", `${row.td_tilt.toFixed(4)} rad`) +
        kv("fuel", `${row.fuel.toFixed(1)} kg`) +
        kv("fault", `${row.fault}${row.fault === 1 ? " (fuel-out)" : ""}`) +
        kv("max_qbar", `${row.max_qbar.toFixed(0)} Pa`) +
        kv("t_total", `${row.t_total.toFixed(1)} s`) +
        `</table>` +
        `<div class="const-cause">${causeLine}</div>`;
      const unpin = card.querySelector<HTMLElement>("[data-unpin]");
      if (unpin) unpin.addEventListener("click", () => unpinCb());
    },

    hideCard() {
      card.style.display = "none";
    },

    reflectFilter(active) {
      for (const [b, c] of chipEls) {
        c.classList.toggle("const-chip-off", active.size > 0 && !active.has(b));
        c.classList.toggle("const-chip-on", active.has(b));
      }
    },

    onFilterToggle(cb) {
      for (const [b, c] of chipEls) {
        c.addEventListener("click", () => cb(b));
      }
    },

    onFiles(cb) {
      filesCb = cb;
    },
    onClearB(cb) {
      clearBCb = cb;
    },
    onUnpin(cb) {
      unpinCb = cb;
    },
    onResetView(cb) {
      resetCb = cb;
    },

    toast(msg) {
      toastEl.textContent = msg;
      toastEl.style.display = "block";
      if (toastTimer) window.clearTimeout(toastTimer);
      toastTimer = window.setTimeout(() => (toastEl.style.display = "none"), 3200);
    },
  };
}

// ── card cause explanation (matches the bucketing rules, human-readable) ──────
export function describeCause(r: ClassifiedRun): string {
  const row = r.row;
  switch (r.bucket) {
    case "landed":
      return `Landed (grade ${row.verdict}). Cool-white→gold by grade.`;
    case "off-pad":
      return `Cause: OFF-PAD — crashed with td_lat ${row.td_lat.toFixed(1)} m > 26 m ring.`;
    case "too-hard":
      return `Cause: TOO-HARD — on-pad but td_v ${row.td_v.toFixed(2)} m/s > 6 m/s, no fault.`;
    case "fuel-out":
      return `Cause: FUEL-OUT — fault==1 (propellant depletion). Takes precedence over off-pad.`;
    case "tipped":
      return `TIPPED (verdict 4) — went over after contact.`;
    default:
      return `Crashed on-pad, soft, no fault (residual bucket).`;
  }
}

// ── tiny DOM helpers ──────────────────────────────────────────────────────────
function el(tag: string, className?: string): HTMLElement {
  const e = document.createElement(tag);
  if (className) e.className = className;
  return e;
}
function btn(label: string): HTMLButtonElement {
  const b = el("button", "const-btn") as HTMLButtonElement;
  b.textContent = label;
  return b;
}
function fileButton(label: string) {
  const wrap = el("label", "const-btn const-filebtn");
  wrap.textContent = label;
  const input = el("input") as HTMLInputElement;
  input.type = "file";
  input.accept = ".csv,text/csv";
  input.style.display = "none";
  wrap.appendChild(input);
  return { button: wrap, input };
}
function kv(k: string, v: string): string {
  return `<tr><td>${k}</td><td>${v}</td></tr>`;
}
function escapeHtml(s: string): string {
  return s.replace(/[&<>"]/g, (c) =>
    c === "&" ? "&amp;" : c === "<" ? "&lt;" : c === ">" ? "&gt;" : "&quot;"
  );
}

// ── styles (injected once) ────────────────────────────────────────────────────
function injectStyles() {
  if (document.getElementById("const-styles")) return;
  const s = document.createElement("style");
  s.id = "const-styles";
  s.textContent = STYLE;
  document.head.appendChild(s);
}

const STYLE = `
:root { color-scheme: dark; }
* { box-sizing: border-box; }
html,body { margin:0; height:100%; background:#0a0c10; overflow:hidden;
  font:13px/1.45 ui-monospace,"Cascadia Code",Menlo,monospace; color:#c8d2e0; }
.const-canvas { position:fixed; inset:0; display:block; width:100vw; height:100vh; }

.const-top { position:fixed; top:0; left:0; right:0; z-index:5; display:flex;
  align-items:center; justify-content:space-between; gap:12px; padding:10px 14px;
  background:linear-gradient(#0b0e13ee,#0b0e1300); pointer-events:none; }
.const-top > * { pointer-events:auto; }
.const-title { font-size:15px; letter-spacing:.06em; font-weight:600; color:#e6edf6;
  display:flex; align-items:center; gap:8px; }
.const-sub { font-size:11px; color:#7f8ba0; letter-spacing:.02em; font-weight:400; }
.const-dot { width:9px; height:9px; border-radius:50%; background:#39d0ff;
  box-shadow:0 0 10px #39d0ff; }
.const-loadwrap { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
.const-btn { background:#161c26; border:1px solid #2a3444; color:#cdd7e5;
  padding:6px 11px; border-radius:6px; cursor:pointer; font:inherit; font-size:12px; }
.const-btn:hover { background:#1d2431; border-color:#3a475c; }
.const-arcs { display:flex; align-items:center; gap:5px; color:#9aa6b8; font-size:12px;
  padding:0 4px; cursor:pointer; user-select:none; }

.const-summary { position:fixed; top:52px; left:14px; right:14px; z-index:5;
  display:flex; gap:14px; align-items:center; flex-wrap:wrap; pointer-events:none;
  font-size:12px; }
.const-summary .const-file { color:#7f8ba0; }
.const-summary .const-b { color:#8fa0c0; }
.const-big { font-size:14px; color:#e6edf6; }
.const-big b { color:#39d0ff; font-size:16px; }
.const-cnt { display:inline-flex; align-items:center; gap:5px; color:#b6c1d2; }
.const-sw { display:inline-block; width:10px; height:10px; border-radius:2px;
  box-shadow:0 0 6px currentColor; }

.const-transition { position:fixed; top:80px; left:14px; right:14px; z-index:5;
  display:flex; gap:12px; align-items:center; flex-wrap:wrap; pointer-events:none;
  font-size:11.5px; color:#b6c1d2; background:#0d1118cc; border:1px solid #23303f;
  border-radius:8px; padding:6px 10px; width:max-content; max-width:calc(100vw - 28px); }
.const-tlabel { color:#8fa0c0; letter-spacing:.04em; }
.const-flip { color:#ffd479; font-weight:600; }
.const-flow { display:inline-flex; align-items:center; gap:3px; }
.const-only { color:#7f8ba0; }

.const-chips { position:fixed; left:14px; bottom:44px; z-index:5; display:flex;
  gap:7px; flex-wrap:wrap; max-width:60vw; }
.const-chip { display:inline-flex; align-items:center; gap:6px; background:#12171f;
  border:1px solid #26313f; color:#c1cbda; padding:5px 9px; border-radius:20px;
  cursor:pointer; font:inherit; font-size:11.5px; }
.const-chip:hover { border-color:#3a475c; }
.const-chip-on { border-color:#39d0ff; box-shadow:0 0 0 1px #39d0ff55 inset; }
.const-chip-off { opacity:.4; }
.const-n { color:#7f8ba0; }

.const-card { position:fixed; top:56px; right:14px; z-index:6; width:268px;
  background:#0d1118f2; border:1px solid #26313f; border-radius:10px; padding:0;
  box-shadow:0 8px 30px #000a; overflow:hidden; }
.const-card-h { display:flex; align-items:center; gap:8px; padding:9px 11px;
  border-left:3px solid #888; background:#11161f; font-size:13px; color:#e6edf6; }
.const-verdict { margin-left:auto; font-size:11px; letter-spacing:.05em; color:#aeb9ca; }
.const-side { color:#7f8ba0; }
.const-pin { margin-left:8px; font-size:10px; color:#ffd479; cursor:pointer;
  border:1px solid #4a4326; border-radius:4px; padding:1px 5px; }
.const-kv { width:100%; border-collapse:collapse; font-size:11.5px; }
.const-kv td { padding:3px 11px; border-top:1px solid #1a212c; }
.const-kv td:first-child { color:#7f8ba0; width:82px; }
.const-kv td:last-child { color:#d5deea; text-align:right; }
.const-cause { padding:8px 11px; font-size:11px; color:#9fb0c4; background:#0b0f16;
  border-top:1px solid #1a212c; }

.const-caveat { position:fixed; left:0; right:0; bottom:0; z-index:5; padding:7px 14px;
  background:#100b06e8; border-top:1px solid #3a2e18; color:#d6b483; font-size:11px;
  letter-spacing:.01em; }
.const-caveat b { color:#ffcf8a; font-weight:600; }

.const-empty { position:fixed; inset:0; z-index:4; display:grid; place-items:center;
  pointer-events:none; }
.const-empty-inner { text-align:center; color:#8b98ab; }
.const-empty-h { font-size:20px; color:#c8d2e0; letter-spacing:.04em; }
.const-empty-p { margin-top:8px; font-size:12.5px; }
.const-empty-p code { color:#39d0ff; }

.const-dragging .const-empty { background:#0a1a24aa; }
.const-dragging::after { content:""; position:fixed; inset:8px; z-index:9;
  border:2px dashed #39d0ff; border-radius:14px; pointer-events:none; }

.const-toast { position:fixed; left:50%; bottom:40px; transform:translateX(-50%);
  z-index:8; background:#12171fe8; border:1px solid #2a3444; color:#cdd7e5;
  padding:8px 14px; border-radius:8px; font-size:12px; box-shadow:0 6px 20px #0008; }
`;
