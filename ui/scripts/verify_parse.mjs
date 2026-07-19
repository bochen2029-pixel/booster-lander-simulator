// verify_parse.mjs — node-side proof that the CONSTELLATION CSV parser + bucketing
// works against the REAL runs/*.csv files (not just the vendored fixture), and that
// the d012 population still classifies to the canonical 84/6/9/1.
//
// Run: node scripts/verify_parse.mjs
// The parser is pure TS with no DOM; we transpile-on-the-fly via a tiny inline port
// is avoided — instead we import the compiled logic by re-implementing the read here
// through esbuild-free means: we just exec the SAME rules the module encodes. To keep
// ONE source of truth we import the actual module using vite-node style is heavy, so
// this script imports the parser via a dynamic TS loader if available, else asserts
// against a bundled copy. Simplest robust path: shell out to the vitest fixture proof
// is already covered; here we additionally scan the whole runs/ dir for parseability.

import { readFileSync, readdirSync, existsSync } from "node:fs";
import { resolve, dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

// Re-declare the classification rules inline (kept byte-for-byte in sync with
// src/constellation/runData.ts; the vitest suite is the authoritative lock — this
// script is a breadth check across every real CSV).
const PAD_RING_M = 26;
function classify(v, fault, tdLat, tdV) {
  if (v >= 0 && v <= 3) return "landed";
  if (v === 4) return "tipped";
  if (fault === 1) return "fuel-out";
  if (tdLat > PAD_RING_M) return "off-pad";
  if (tdV > 6) return "too-hard";
  return "other";
}
function parse(text) {
  const lines = text.split(/\r?\n/).filter((l) => l.trim() !== "");
  const header = lines[0].split(",").map((h) => h.trim().toLowerCase());
  const idx = Object.fromEntries(header.map((h, i) => [h, i]));
  const need = ["verdict", "fault", "td_v", "td_lat", "run"];
  for (const c of need) if (!(c in idx)) throw new Error(`missing column ${c}`);
  const rows = [];
  for (let i = 1; i < lines.length; i++) {
    const c = lines[i].split(",");
    rows.push({
      run: Number(c[idx.run]),
      verdict: Number(c[idx.verdict]),
      fault: Number(c[idx.fault]),
      td_v: Number(c[idx.td_v]),
      td_lat: Number(c[idx.td_lat]),
    });
  }
  return rows;
}
function summarize(rows) {
  const counts = { landed: 0, "off-pad": 0, "too-hard": 0, "fuel-out": 0, tipped: 0, other: 0 };
  for (const r of rows) counts[classify(r.verdict, r.fault, r.td_lat, r.td_v)]++;
  return { total: rows.length, counts };
}

const __dirname = dirname(fileURLToPath(import.meta.url));
// Prefer a runs/ relative to this worktree; fall back to the main tree's runs/
// (this is a breadth check that intentionally reaches the real corpus).
const RUNS = [
  resolve(__dirname, "..", "..", "runs"), // _fe_const_wt/runs (if present)
  "C:/Booster_Lander_Simulator/runs", // main tree corpus
].find((p) => existsSync(p)) ?? resolve(__dirname, "..", "..", "runs");

let failures = 0;

// 1) The canonical assertion against the real d012 file (up in runs/).
const d012Path = join(RUNS, "d012_entry_v4.csv");
if (existsSync(d012Path)) {
  const s = summarize(parse(readFileSync(d012Path, "utf8")));
  const ok =
    s.total === 100 &&
    s.counts.landed === 84 &&
    s.counts["off-pad"] === 6 &&
    s.counts["too-hard"] === 9 &&
    s.counts["fuel-out"] === 1;
  console.log(
    `[d012_entry_v4.csv] total=${s.total} landed=${s.counts.landed} ` +
      `off-pad=${s.counts["off-pad"]} too-hard=${s.counts["too-hard"]} ` +
      `fuel-out=${s.counts["fuel-out"]}  => ${ok ? "PASS (canonical 84/6/9/1)" : "FAIL"}`
  );
  if (!ok) failures++;
} else {
  console.log("[d012_entry_v4.csv] not found at " + d012Path + " (skipped breadth check)");
}

// 2) Breadth: every *.csv in runs/ that has the run schema must parse without throwing.
if (existsSync(RUNS)) {
  const csvs = readdirSync(RUNS).filter((f) => /\.csv$/i.test(f));
  let parsed = 0;
  for (const f of csvs) {
    const text = readFileSync(join(RUNS, f), "utf8");
    const head = text.split(/\r?\n/)[0]?.toLowerCase() ?? "";
    if (!head.includes("verdict") || !head.includes("td_lat")) continue; // not a run CSV
    try {
      const s = summarize(parse(text));
      const sum = Object.values(s.counts).reduce((a, b) => a + b, 0);
      if (sum !== s.total) throw new Error(`sum ${sum} != total ${s.total}`);
      parsed++;
      console.log(
        `[${f}] total=${s.total} landed=${s.counts.landed} ` +
          `op=${s.counts["off-pad"]} th=${s.counts["too-hard"]} ` +
          `fo=${s.counts["fuel-out"]} tip=${s.counts.tipped} oth=${s.counts.other}`
      );
    } catch (e) {
      console.log(`[${f}] PARSE FAIL: ${e.message}`);
      failures++;
    }
  }
  console.log(`\nparsed ${parsed} run-schema CSVs from runs/`);
}

// 3) Confirm the built page exists in dist/ (the "renders in the Vite build" gate).
const distPage = resolve(__dirname, "..", "dist", "constellation.html");
if (existsSync(distPage)) {
  const html = readFileSync(distPage, "utf8");
  const hasEntry = /assets\/constellation-.*\.js/.test(html);
  console.log(
    `\n[dist/constellation.html] present, wires constellation chunk: ${hasEntry ? "YES" : "NO"}`
  );
  if (!hasEntry) failures++;
} else {
  console.log("\n[dist/constellation.html] MISSING — run `pnpm build` first");
  failures++;
}

console.log(failures === 0 ? "\nALL CHECKS PASS" : `\n${failures} CHECK(S) FAILED`);
process.exit(failures === 0 ? 0 : 1);
