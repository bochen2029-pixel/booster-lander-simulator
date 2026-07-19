// runData.ts — the CSV parser + cause bucketing for MC CONSTELLATION (S2B).
//
// PURE, DOM-free, unit-tested. This is the data spine of the constellation view:
// parse a per-run Monte-Carlo CSV (end-state only) into typed RunRow[], classify
// each run into an outcome bucket, and roll up the summary counts.
//
// ── SCHEMA (per-run CSV, one row per run) ────────────────────────────────────
//   seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,
//   max_qbar,peak_qdot,t_total,max_crush
//
//   verdict : 0..3 = landed grades (0 perfect .. 3 hard-but-down), 4 = TIPPED,
//             5 = CRASHED.
//   fault   : 0 = none, 1 = fuel-out (propellant depletion).
//   td_v    : touchdown vertical speed [m/s].
//   td_lat  : touchdown lateral miss from pad center [m]. on-pad iff td_lat <= 26.
//
// ── CAUSE BUCKETING (the load-bearing classification) ────────────────────────
// The outcome bucket a run lands in is NOT the raw verdict — a CRASHED run has a
// *reason*, and the reasons have a PRECEDENCE that matters:
//
//   1. landed   : verdict 0..3            (any grade that stayed upright & down)
//   2. tipped   : verdict == 4
//   3. crashed (verdict == 5), sub-classified in THIS ORDER:
//        a. fuel-out : fault == 1         ← checked FIRST. a propellant-depletion
//                                            crash is a fuel-out even if it also
//                                            missed the pad (it ran dry on the way).
//        b. off-pad  : td_lat > 26        (crashed beyond the 26 m ring)
//        c. too-hard : td_v > 6           (on-pad, no fault, came in too fast)
//        d. other    : anything remaining (on-pad, soft-ish, no fault — rare)
//
// WHY fuel-out BEFORE off-pad: verified against runs/d012_entry_v4.csv, whose
// canonical population is 84 landed / 6 off-pad / 9 too-hard / 1 fuel-out. Run 14
// in that file is fault==1 AND td_lat=157.8 (way off-pad); it MUST bucket as
// fuel-out to reproduce the known counts. runData.test.ts locks this exactly.
// (Spec's informal "off-pad = crashed & td_lat>26" is the *geometry*; the fuel
// fault is the true cause and takes precedence.)

/** Every column of a per-run CSV row, typed. Extra/unknown columns are ignored. */
export interface RunRow {
  seed: number;
  scenario: string;
  run: number;
  verdict: number;
  fault: number;
  td_v: number;
  td_lat: number;
  td_tilt: number;
  settled_tilt: number;
  fuel: number;
  max_qbar: number;
  peak_qdot: number;
  t_total: number;
  max_crush: number;
}

/** The outcome bucket a run is classified into (see module header for rules). */
export type CauseBucket =
  | "landed"
  | "off-pad"
  | "too-hard"
  | "fuel-out"
  | "tipped"
  | "other";

/** All buckets in a stable display order (chips, legend, summary strip). */
export const CAUSE_ORDER: readonly CauseBucket[] = [
  "landed",
  "off-pad",
  "too-hard",
  "fuel-out",
  "tipped",
  "other",
] as const;

/** on-pad iff lateral miss is within the 26 m ring. */
export const PAD_RING_M = 26;

/** A run row plus its derived classification + display fields. */
export interface ClassifiedRun {
  row: RunRow;
  /** 0-based index within the parsed file (stable; drives the synthesized angle). */
  index: number;
  bucket: CauseBucket;
  /** true iff td_lat <= 26. */
  onPad: boolean;
  /** true iff verdict in 0..3. */
  landed: boolean;
}

/** Roll-up counts keyed by bucket, plus the totals. */
export interface RunSummary {
  total: number;
  counts: Record<CauseBucket, number>;
  /** landed / total, in [0,1]; NaN for an empty set. */
  landedFrac: number;
}

// ── column parsing ───────────────────────────────────────────────────────────

const REQUIRED_COLUMNS = [
  "seed",
  "scenario",
  "run",
  "verdict",
  "fault",
  "td_v",
  "td_lat",
  "td_tilt",
  "settled_tilt",
  "fuel",
  "max_qbar",
  "peak_qdot",
  "t_total",
  "max_crush",
] as const;

/** Numeric columns (everything except `scenario`). */
const NUMERIC_COLUMNS = REQUIRED_COLUMNS.filter((c) => c !== "scenario");

export interface ParseResult {
  rows: RunRow[];
  /** header column names, in file order. */
  header: string[];
  /** 1-based CSV line numbers that were skipped, with a reason (blank/malformed). */
  skipped: { line: number; reason: string }[];
}

/**
 * Parse per-run CSV text into typed RunRow[]. Tolerant of trailing whitespace,
 * blank lines, CRLF, and surrounding quotes on fields. Column order is taken from
 * the header, not assumed positional, so a reordered export still parses.
 *
 * Throws only if the header is missing a REQUIRED column — a structurally wrong
 * file is a hard error the UI must surface, not silently mis-plot.
 */
export function parseRunCsv(text: string): ParseResult {
  const rawLines = text.split(/\r?\n/);
  // Find the first non-blank line as the header.
  let headerIdx = -1;
  for (let i = 0; i < rawLines.length; i++) {
    if (rawLines[i].trim() !== "") {
      headerIdx = i;
      break;
    }
  }
  if (headerIdx < 0) {
    throw new Error("CSV is empty (no header row).");
  }

  const header = splitCsvLine(rawLines[headerIdx]).map((h) => h.trim().toLowerCase());
  const colIndex: Record<string, number> = {};
  header.forEach((h, i) => {
    if (!(h in colIndex)) colIndex[h] = i;
  });

  const missing = REQUIRED_COLUMNS.filter((c) => !(c in colIndex));
  if (missing.length > 0) {
    throw new Error(
      `CSV header missing required column(s): ${missing.join(", ")}. ` +
        `Got: ${header.join(", ")}`
    );
  }

  const rows: RunRow[] = [];
  const skipped: ParseResult["skipped"] = [];

  for (let i = headerIdx + 1; i < rawLines.length; i++) {
    const line = rawLines[i];
    if (line.trim() === "") continue; // silent skip of blank lines (e.g. trailing newline)

    const cells = splitCsvLine(line);
    if (cells.length < header.length) {
      skipped.push({
        line: i + 1,
        reason: `expected ${header.length} columns, got ${cells.length}`,
      });
      continue;
    }

    const get = (name: string): string => (cells[colIndex[name]] ?? "").trim();

    // Validate numeric fields; a NaN in any numeric column skips the row loudly
    // (a mis-plotted glyph is worse than a dropped one).
    let bad = "";
    const num = (name: string): number => {
      const v = Number(get(name));
      if (!Number.isFinite(v)) bad = name;
      return v;
    };

    const row: RunRow = {
      seed: num("seed"),
      scenario: get("scenario"),
      run: num("run"),
      verdict: num("verdict"),
      fault: num("fault"),
      td_v: num("td_v"),
      td_lat: num("td_lat"),
      td_tilt: num("td_tilt"),
      settled_tilt: num("settled_tilt"),
      fuel: num("fuel"),
      max_qbar: num("max_qbar"),
      peak_qdot: num("peak_qdot"),
      t_total: num("t_total"),
      max_crush: num("max_crush"),
    };

    if (bad !== "") {
      skipped.push({ line: i + 1, reason: `non-numeric ${bad}="${get(bad)}"` });
      continue;
    }
    rows.push(row);
  }

  // touch NUMERIC_COLUMNS so it isn't flagged unused under noUnusedLocals while
  // documenting intent; the per-field `num()` calls above are the real parse.
  void NUMERIC_COLUMNS;

  return { rows, header, skipped };
}

/** Split one CSV line, honoring simple double-quoted fields with "" escapes. */
function splitCsvLine(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  let inQuotes = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (inQuotes) {
      if (ch === '"') {
        if (line[i + 1] === '"') {
          cur += '"';
          i++;
        } else {
          inQuotes = false;
        }
      } else {
        cur += ch;
      }
    } else if (ch === '"') {
      inQuotes = true;
    } else if (ch === ",") {
      out.push(cur);
      cur = "";
    } else {
      cur += ch;
    }
  }
  out.push(cur);
  return out;
}

// ── classification ───────────────────────────────────────────────────────────

/** Classify a single row into its outcome bucket (see module header). */
export function classifyRow(row: RunRow): CauseBucket {
  const v = row.verdict;
  if (v >= 0 && v <= 3) return "landed";
  if (v === 4) return "tipped";
  // v === 5 (CRASHED) — sub-classify by cause, fuel-out first.
  if (row.fault === 1) return "fuel-out";
  if (row.td_lat > PAD_RING_M) return "off-pad";
  if (row.td_v > 6) return "too-hard";
  return "other";
}

/** Classify every row, attaching index + derived booleans. */
export function classifyRuns(rows: RunRow[]): ClassifiedRun[] {
  return rows.map((row, index) => ({
    row,
    index,
    bucket: classifyRow(row),
    onPad: row.td_lat <= PAD_RING_M,
    landed: row.verdict >= 0 && row.verdict <= 3,
  }));
}

/** Zeroed bucket-count record. */
function zeroCounts(): Record<CauseBucket, number> {
  return {
    landed: 0,
    "off-pad": 0,
    "too-hard": 0,
    "fuel-out": 0,
    tipped: 0,
    other: 0,
  };
}

/** Roll up classified runs into the summary strip counts. */
export function summarize(runs: ClassifiedRun[]): RunSummary {
  const counts = zeroCounts();
  for (const r of runs) counts[r.bucket]++;
  const total = runs.length;
  const landedFrac = total > 0 ? counts.landed / total : NaN;
  return { total, counts, landedFrac };
}

/** Convenience: parse + classify + summarize in one call. */
export function loadRunCsv(text: string): {
  parse: ParseResult;
  runs: ClassifiedRun[];
  summary: RunSummary;
} {
  const parse = parseRunCsv(text);
  const runs = classifyRuns(parse.rows);
  const summary = summarize(runs);
  return { parse, runs, summary };
}

// ── A/B diff (the mcdiff visual) ─────────────────────────────────────────────

/** One run's transition between two populations, matched by run number. */
export interface RunTransition {
  run: number;
  from: ClassifiedRun;
  to: ClassifiedRun;
  /** true iff the outcome bucket changed. */
  flipped: boolean;
}

export interface AbDiff {
  /** runs present in BOTH files (matched by `run`), in A's order. */
  transitions: RunTransition[];
  /** run numbers only in A. */
  onlyInA: number[];
  /** run numbers only in B. */
  onlyInB: number[];
  /** count of transitions where the bucket flipped. */
  flippedCount: number;
  /** flip counts keyed "fromBucket->toBucket" (only the changed ones). */
  flowCounts: Record<string, number>;
}

/**
 * Diff two classified populations by run number (the mcdiff visual). A run that
 * changed outcome bucket between A and B is a "flip" — those get the connecting
 * hairline in the 3D view and are tallied in the transition strip.
 */
export function diffRuns(a: ClassifiedRun[], b: ClassifiedRun[]): AbDiff {
  const bByRun = new Map<number, ClassifiedRun>();
  for (const r of b) bByRun.set(r.row.run, r);
  const aRuns = new Set<number>(a.map((r) => r.row.run));

  const transitions: RunTransition[] = [];
  const flowCounts: Record<string, number> = {};
  let flippedCount = 0;

  for (const from of a) {
    const to = bByRun.get(from.row.run);
    if (!to) continue;
    const flipped = from.bucket !== to.bucket;
    if (flipped) {
      flippedCount++;
      const key = `${from.bucket}->${to.bucket}`;
      flowCounts[key] = (flowCounts[key] ?? 0) + 1;
    }
    transitions.push({ run: from.row.run, from, to, flipped });
  }

  const onlyInA = a.filter((r) => !bByRun.has(r.row.run)).map((r) => r.row.run);
  const onlyInB = b.filter((r) => !aRuns.has(r.row.run)).map((r) => r.row.run);

  return { transitions, onlyInA, onlyInB, flippedCount, flowCounts };
}
