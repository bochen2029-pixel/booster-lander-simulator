import { defineConfig } from "vitest/config";

// Canon §15: `pnpm -C ui dev` runs the browser dev server against `core --serve`
// (ws://127.0.0.1:8787). `pnpm tauri dev` wraps this same server in the shell.
//
// Tauri notes (canon §11.1): fixed dev port, no auto-open (the shell owns the
// window), and we must NOT clear the screen so Tauri's spawn logs stay visible.
export default defineConfig({
  clearScreen: false,
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
  },
  test: {
    // vitest picks up *.test.ts under src/. jsdom not needed — the conversion and
    // decoder tests are pure numeric, no DOM, no WebGPU.
    environment: "node",
    include: ["src/**/*.test.ts"],
  },
});
