import { defineConfig } from "vitest/config";
import { resolve } from "node:path";
import { writeFileSync, mkdirSync } from "node:fs";

// DEV-ONLY capture sink: the in-app __shot hook POSTs a base64 JPEG to /__cap?name=foo
// and this writes runs/shots/foo.jpg so an agent (or the operator) can eyes-on the
// WebGPU output on disk without a base64 round-trip. configureServer runs only under
// `vite dev`; it is absent from the production build.
function captureToDisk() {
  return {
    name: "capture-to-disk",
    configureServer(server: any) {
      server.middlewares.use("/__cap", (req: any, res: any) => {
        if (req.method !== "POST") { res.statusCode = 405; res.end(); return; }
        const url = new URL(req.url, "http://x");
        const name = (url.searchParams.get("name") || "shot").replace(/[^a-z0-9_.-]/gi, "_");
        const chunks: Buffer[] = [];
        req.on("data", (c: Buffer) => chunks.push(c));
        req.on("end", () => {
          try {
            let b = Buffer.concat(chunks).toString("utf8");
            const comma = b.indexOf(",");
            if (b.startsWith("data:") && comma >= 0) b = b.slice(comma + 1);
            const dir = resolve(__dirname, "../runs/shots");
            mkdirSync(dir, { recursive: true });
            writeFileSync(resolve(dir, `${name}.jpg`), Buffer.from(b, "base64"));
            res.statusCode = 200; res.end("ok");
          } catch (e) { res.statusCode = 500; res.end(String(e)); }
        });
      });
    },
  };
}

// Canon §15: `pnpm -C ui dev` runs the browser dev server against `core --serve`
// (ws://127.0.0.1:8787). `pnpm tauri dev` wraps this same server in the shell.
//
// Tauri notes (canon §11.1): fixed dev port, no auto-open (the shell owns the
// window), and we must NOT clear the screen so Tauri's spawn logs stay visible.
export default defineConfig({
  clearScreen: false,
  plugins: [captureToDisk()],
  server: {
    port: 5183,
    strictPort: true,
    host: "127.0.0.1",
  },
  // three r185 ships prebuilt ESM; nothing special to optimize, but pin the entry
  // so a stray transitive `three` can never shadow 0.185.1.
  resolve: {
    dedupe: ["three"],
  },
  build: {
    target: "esnext", // WebGPU + top-level features; WebView2 is evergreen Chromium
    sourcemap: true,
    // Multi-page (S2B): `constellation.html` is the standalone MC CONSTELLATION view,
    // built alongside the main cockpit. Additive only — the default `index.html`
    // entry is preserved. Folded into the cockpit in wave F2.
    rollupOptions: {
      input: {
        main: resolve(__dirname, "index.html"),
        constellation: resolve(__dirname, "constellation.html"),
      },
    },
  },
  test: {
    // vitest picks up *.test.ts under src/. jsdom not needed — the conversion and
    // decoder tests are pure numeric, no DOM, no WebGPU.
    environment: "node",
    include: ["src/**/*.test.ts"],
  },
});
