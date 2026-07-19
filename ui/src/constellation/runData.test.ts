// runData.test.ts — locks the CSV parser + cause bucketing for MC CONSTELLATION.
//
// The gate-critical assertion: runs/d012_entry_v4.csv MUST classify to EXACTLY
//   84 landed / 6 off-pad / 9 too-hard / 1 fuel-out   (0 tipped, 0 other)
// This is the canonical population from the D-012 sweep; if the bucketing drifts,
// the summary strip lies. We assert every count exactly, not just landed.

import { describe, it, expect } from "vitest";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";
import {
  parseRunCsv,
  classifyRow,
  classifyRuns,
  summarize,
  loadRunCsv,
  diffRuns,
  PAD_RING_M,
  type RunRow,
} from "./runData";

const __dirname = dirname(fileURLToPath(import.meta.url));
// The canonical d012 population is vendored as an in-tree fixture (a byte copy of
// runs/d012_entry_v4.csv), so the exact-count assertion is self-contained and
// runs on any checkout — no dependency on a path outside the worktree.
const D012 = resolve(__dirname, "__fixtures__", "d012_entry_v4.csv");

/** Minimal row factory for unit tests of the classifier. */
function mkRow(p: Partial<RunRow>): RunRow {
  return {
    seed: 42,
    scenario: "entry",
    run: 1,
    verdict: 0,
    fault: 0,
    td_v: 1,
    td_lat: 1,
    td_tilt: 0,
    settled_tilt: 0,
    fuel: 1000,
    max_qbar: 0,
    peak_qdot: 0,
    t_total: 100,
    max_crush: 0,
    ...p,
  };
}

describe("classifyRow — the bucketing precedence", () => {
  it("landed grades 0..3 are all 'landed' regardless of td_v/td_lat", () => {
    for (const v of [0, 1, 2, 3]) {
      expect(classifyRow(mkRow({ verdict: v, td_v: 99, td_lat: 99 }))).toBe("landed");
    }
  });

  it("verdict 4 is 'tipped'", () => {
    expect(classifyRow(mkRow({ verdict: 4 }))).toBe("tipped");
  });

  it("crashed + fault==1 is 'fuel-out' EVEN WHEN off-pad (precedence!)", () => {
    // This is the run-14 case: dry AND way off-pad -> fuel-out wins.
    expect(
      classifyRow(mkRow({ verdict: 5, fault: 1, td_lat: 157.8, td_v: 96 }))
    ).toBe("fuel-out");
  });

  it("crashed + off-pad (no fault) is 'off-pad'", () => {
    expect(classifyRow(mkRow({ verdict: 5, fault: 0, td_lat: 31.8, td_v: 5 }))).toBe(
      "off-pad"
    );
  });

  it("crashed + on-pad + td_v>6 (no fault) is 'too-hard'", () => {
    expect(classifyRow(mkRow({ verdict: 5, fault: 0, td_lat: 4.6, td_v: 12.6 }))).toBe(
      "too-hard"
    );
  });

  it("the 26 m ring is inclusive: td_lat==26 is on-pad", () => {
    // exactly on the ring, crashed hard, no fault -> too-hard (on-pad), not off-pad
    expect(classifyRow(mkRow({ verdict: 5, fault: 0, td_lat: 26, td_v: 8 }))).toBe(
      "too-hard"
    );
    // just outside -> off-pad
    expect(classifyRow(mkRow({ verdict: 5, fault: 0, td_lat: 26.001, td_v: 8 }))).toBe(
      "off-pad"
    );
  });

  it("crashed + on-pad + soft + no fault is 'other' (the residual bucket)", () => {
    expect(classifyRow(mkRow({ verdict: 5, fault: 0, td_lat: 4, td_v: 2 }))).toBe(
      "other"
    );
  });

  it("PAD_RING_M is 26", () => {
    expect(PAD_RING_M).toBe(26);
  });
});

describe("parseRunCsv — tolerant, header-driven", () => {
  it("parses a minimal well-formed CSV", () => {
    const csv = [
      "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush",
      "42,entry,1,3,0,4.6,6.5,0.03,0.0004,2054.5,56956.3,13395.5,140.86,0.0359",
    ].join("\n");
    const { rows } = parseRunCsv(csv);
    expect(rows).toHaveLength(1);
    expect(rows[0].scenario).toBe("entry");
    expect(rows[0].td_v).toBeCloseTo(4.6, 6);
    expect(rows[0].run).toBe(1);
  });

  it("skips blank lines and a trailing newline without error", () => {
    const csv =
      "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush\n" +
      "42,entry,1,3,0,4.6,6.5,0,0,1,0,0,1,0\n\n";
    const { rows, skipped } = parseRunCsv(csv);
    expect(rows).toHaveLength(1);
    expect(skipped).toHaveLength(0);
  });

  it("is column-order independent (reordered header still parses correctly)", () => {
    const csv = [
      "run,verdict,scenario,seed,fault,td_lat,td_v,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush",
      "7,5,aero,42,1,157.8,96.0,0,0,0,0,0,10,0",
    ].join("\n");
    const { rows } = parseRunCsv(csv);
    expect(rows[0].run).toBe(7);
    expect(rows[0].verdict).toBe(5);
    expect(rows[0].td_lat).toBeCloseTo(157.8, 3);
    expect(rows[0].td_v).toBeCloseTo(96, 3);
    // and it buckets as fuel-out (fault==1)
    expect(classifyRow(rows[0])).toBe("fuel-out");
  });

  it("skips a row with a non-numeric numeric field (records reason)", () => {
    const csv = [
      "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush",
      "42,entry,1,3,0,NOPE,6.5,0,0,1,0,0,1,0",
    ].join("\n");
    const { rows, skipped } = parseRunCsv(csv);
    expect(rows).toHaveLength(0);
    expect(skipped).toHaveLength(1);
    expect(skipped[0].reason).toContain("td_v");
  });

  it("throws when a required column is missing", () => {
    const csv = "seed,scenario,run\n42,entry,1";
    expect(() => parseRunCsv(csv)).toThrow(/missing required column/i);
  });

  it("throws on an empty document", () => {
    expect(() => parseRunCsv("")).toThrow(/empty/i);
  });
});

// The gate assertion — UNCONDITIONAL. The fixture is vendored in-tree, so these
// exact counts are always asserted (the whole point of the S2B summary-strip gate).
describe(
  "d012_entry_v4.csv — the canonical population (EXACT counts)",
  () => {
    const text = readFileSync(D012, "utf8");
    const { runs, summary, parse } = loadRunCsv(text);

    it("parses all 100 runs with zero skips", () => {
      expect(parse.skipped).toHaveLength(0);
      expect(runs).toHaveLength(100);
      expect(summary.total).toBe(100);
    });

    it("classifies to EXACTLY 84 landed / 6 off-pad / 9 too-hard / 1 fuel-out", () => {
      expect(summary.counts.landed).toBe(84);
      expect(summary.counts["off-pad"]).toBe(6);
      expect(summary.counts["too-hard"]).toBe(9);
      expect(summary.counts["fuel-out"]).toBe(1);
      expect(summary.counts.tipped).toBe(0);
      expect(summary.counts.other).toBe(0);
    });

    it("the buckets sum to the total (no run unclassified)", () => {
      const sum = Object.values(summary.counts).reduce((a, b) => a + b, 0);
      expect(sum).toBe(summary.total);
    });

    it("landedFrac == 0.84", () => {
      expect(summary.landedFrac).toBeCloseTo(0.84, 10);
    });

    it("the single fuel-out run is run 14 (fault==1, off-pad, would be miscounted without precedence)", () => {
      const fuel = runs.filter((r) => r.bucket === "fuel-out");
      expect(fuel).toHaveLength(1);
      expect(fuel[0].row.run).toBe(14);
      expect(fuel[0].row.fault).toBe(1);
      expect(fuel[0].onPad).toBe(false); // off-pad by geometry, but fuel-out by cause
    });
  }
);

describe("diffRuns — the mcdiff A/B visual", () => {
  it("matches by run number, tallies flips, and reports only-in-X", () => {
    const aText = [
      "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush",
      "42,entry,1,3,0,4,5,0,0,1,0,0,1,0", // landed
      "42,entry,2,5,0,8,4,0,0,1,0,0,1,0", // too-hard
      "42,entry,3,3,0,4,5,0,0,1,0,0,1,0", // landed (only in A)
    ].join("\n");
    const bText = [
      "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush",
      "42,entry,1,3,0,4,5,0,0,1,0,0,1,0", // landed (unchanged)
      "42,entry,2,3,0,4,5,0,0,1,0,0,1,0", // now landed (flip: too-hard->landed)
      "42,entry,4,5,0,9,50,0,0,1,0,0,1,0", // off-pad (only in B)
    ].join("\n");
    const a = classifyRuns(parseRunCsv(aText).rows);
    const b = classifyRuns(parseRunCsv(bText).rows);
    const d = diffRuns(a, b);

    expect(d.transitions).toHaveLength(2); // runs 1 and 2 are in both
    expect(d.flippedCount).toBe(1);
    expect(d.flowCounts["too-hard->landed"]).toBe(1);
    expect(d.onlyInA).toEqual([3]);
    expect(d.onlyInB).toEqual([4]);
    const run2 = d.transitions.find((t) => t.run === 2)!;
    expect(run2.flipped).toBe(true);
    const run1 = d.transitions.find((t) => t.run === 1)!;
    expect(run1.flipped).toBe(false);
  });
});

describe("summarize — empty set", () => {
  it("landedFrac is NaN for zero runs, counts all zero", () => {
    const s = summarize([]);
    expect(s.total).toBe(0);
    expect(Number.isNaN(s.landedFrac)).toBe(true);
    expect(s.counts.landed).toBe(0);
  });
});
