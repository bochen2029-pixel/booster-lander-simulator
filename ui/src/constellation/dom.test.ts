// @vitest-environment jsdom
//
// dom.test.ts — smoke test that the CONSTELLATION chrome actually RENDERS the DOM:
// summary strip with the right counts, filter chips with per-bucket tallies, the
// hover/pin card with the correct cause text, and the A/B transition strip. This is
// a real render check of the interactive shell (no GPU needed); the 3D scene render
// is verified at integration (canon defers the visual check).
//
// Uses jsdom (per-file env above). buildDom() is pure DOM — it imports NO three.

import { describe, it, expect, beforeEach } from "vitest";
import { buildDom, describeCause, type LoadedFile } from "./dom";
import { loadRunCsv, classifyRuns, parseRunCsv, diffRuns } from "./runData";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const D012 = resolve(__dirname, "__fixtures__", "d012_entry_v4.csv");

function loadedFrom(path: string, name: string): LoadedFile {
  const { runs, summary, parse } = loadRunCsv(readFileSync(path, "utf8"));
  return { name, runs, summary, skipped: parse.skipped.length };
}

beforeEach(() => {
  document.body.innerHTML = '<div id="app"></div>';
  document.head.innerHTML = "";
});

describe("buildDom — renders the chrome skeleton", () => {
  it("creates the canvas, top bar, chips, caveat and empty state", () => {
    const dom = buildDom();
    expect(dom.canvas.tagName).toBe("CANVAS");
    expect(document.querySelector(".const-top")).toBeTruthy();
    expect(document.querySelector(".const-caveat")).toBeTruthy();
    // 6 filter chips (one per bucket)
    expect(document.querySelectorAll(".const-chip").length).toBe(6);
    // schematic caveat must say what it is (honesty gate)
    const caveat = document.querySelector(".const-caveat")!.textContent!;
    expect(caveat).toMatch(/schematic/i);
    expect(caveat).toMatch(/miss magnitude/i);
    expect(caveat).toMatch(/wave/i);
  });

  it("injects styles exactly once even across two builds", () => {
    buildDom();
    buildDom();
    expect(document.querySelectorAll("#const-styles").length).toBe(1);
  });
});

describe("setSummary — the summary strip matches the d012 counts", () => {
  it("renders 84/100 landed 84% and per-cause tallies from the real file", () => {
    const dom = buildDom();
    const a = loadedFrom(D012, "d012_entry_v4.csv");
    dom.setSummary(a, null);

    const strip = document.querySelector(".const-summary")!.textContent!;
    expect(strip).toContain("84/100 landed");
    expect(strip).toContain("84%");
    // per-cause counts appear in the strip
    expect(strip).toMatch(/OFF-PAD 6/);
    expect(strip).toMatch(/TOO-HARD 9/);
    expect(strip).toMatch(/FUEL-OUT 1/);

    // chip tallies reflect the same counts
    const landedChipN = document
      .querySelector('.const-chip[data-bucket="landed"] .const-n')!
      .textContent;
    expect(landedChipN).toBe("84");
    expect(
      document.querySelector('.const-chip[data-bucket="off-pad"] .const-n')!.textContent
    ).toBe("6");
    expect(
      document.querySelector('.const-chip[data-bucket="too-hard"] .const-n')!.textContent
    ).toBe("9");
    expect(
      document.querySelector('.const-chip[data-bucket="fuel-out"] .const-n')!.textContent
    ).toBe("1");
  });

  it("clears the strip and zeroes chips when passed null", () => {
    const dom = buildDom();
    dom.setSummary(loadedFrom(D012, "d012_entry_v4.csv"), null);
    dom.setSummary(null, null);
    expect(document.querySelector(".const-summary")!.textContent).toBe("");
    expect(
      document.querySelector('.const-chip[data-bucket="landed"] .const-n')!.textContent
    ).toBe("0");
  });
});

describe("showCard — the per-run card shows the right fields + cause", () => {
  it("renders run number, td_v, td_lat, and the fuel-out cause line for run 14", () => {
    const dom = buildDom();
    const runs = loadRunCsv(readFileSync(D012, "utf8")).runs;
    const run14 = runs.find((r) => r.row.run === 14)!;
    expect(run14.bucket).toBe("fuel-out");

    dom.showCard({ run: run14, side: "A" }, undefined, true);
    const card = document.querySelector(".const-card")!;
    expect((card as HTMLElement).style.display).toBe("block");
    const txt = card.textContent!;
    expect(txt).toContain("run 14");
    expect(txt).toContain("FUEL-OUT");
    expect(txt).toMatch(/96\.03/); // td_v
    expect(txt).toContain("(off-pad)"); // td_lat 157.8 -> off-pad geometry
    expect(txt).toMatch(/precedence/i); // cause explanation mentions the rule
    // pinned card has an unpin affordance
    expect(card.querySelector("[data-unpin]")).toBeTruthy();
  });

  it("hideCard hides it", () => {
    const dom = buildDom();
    const run = loadRunCsv(readFileSync(D012, "utf8")).runs[0];
    dom.showCard({ run, side: "A" }, undefined, false);
    dom.hideCard();
    expect((document.querySelector(".const-card") as HTMLElement).style.display).toBe(
      "none"
    );
  });
});

describe("setTransition — the A/B strip shows flip counts", () => {
  it("shows flipped count and flow chips for a synthetic diff", () => {
    const dom = buildDom();
    const a = classifyRuns(
      parseRunCsv(
        "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush\n" +
          "42,e,1,5,0,8,4,0,0,1,0,0,1,0\n" + // too-hard
          "42,e,2,3,0,4,5,0,0,1,0,0,1,0" // landed
      ).rows
    );
    const b = classifyRuns(
      parseRunCsv(
        "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush\n" +
          "42,e,1,3,0,4,5,0,0,1,0,0,1,0\n" + // now landed (flip)
          "42,e,2,3,0,4,5,0,0,1,0,0,1,0" // unchanged
      ).rows
    );
    dom.setTransition(diffRuns(a, b), "base.csv", "cand.csv");
    const strip = document.querySelector(".const-transition")!;
    expect((strip as HTMLElement).style.display).toBe("flex");
    const txt = strip.textContent!;
    expect(txt).toContain("1 flipped");
    expect(txt).toMatch(/TOO-HARD→LANDED/);
    expect(txt).toContain("base.csv");
    expect(txt).toContain("cand.csv");
  });
});

describe("reflectFilter — chips reflect active set", () => {
  it("marks selected chips on and others off", () => {
    const dom = buildDom();
    dom.reflectFilter(new Set(["landed"]));
    const landed = document.querySelector('.const-chip[data-bucket="landed"]')!;
    const offpad = document.querySelector('.const-chip[data-bucket="off-pad"]')!;
    expect(landed.classList.contains("const-chip-on")).toBe(true);
    expect(offpad.classList.contains("const-chip-off")).toBe(true);
  });
});

describe("describeCause — human text matches the bucketing rules", () => {
  it("names the fuel-out precedence explicitly", () => {
    const r = classifyRuns(
      parseRunCsv(
        "seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,fuel,max_qbar,peak_qdot,t_total,max_crush\n" +
          "42,e,1,5,1,96,157,0,0,0,0,0,1,0"
      ).rows
    )[0];
    expect(describeCause(r)).toMatch(/FUEL-OUT/);
    expect(describeCause(r)).toMatch(/precedence/i);
  });
});
