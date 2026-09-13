# cockpit_verify — the agent's eyes on the cockpit, without a human and without the Browser pane

The loop that verified D-056 (2026-09-12). It drives the real WebGPU cockpit in a throwaway
headless Chrome via `C:\peek` (pure-stdlib CDP client — the harness's own browser pane refuses
nothing here, but its dev server dies with its last tab, and peek does not care), poses the
offscreen HDR capture camera through the DEV globals `main.ts` / `hud/screenshot.ts` expose, and
writes JPEGs to `runs/shots/` through the vite `/__cap` sink.

```
pnpm -C ui dev                       # or preview_start booster-ui — port 5183, leave it running
python tools/cockpit_verify/k9run.py k9data "raf&port=8790"            # default model
python tools/cockpit_verify/k9run.py k9canvas "raf&port=8790&k9tex=data"
python tools/cockpit_verify/k9run.py legacy  "raf&port=8790&legacy"
```

Each run: opens `http://localhost:5183/?<query>` (page FIRST — `core --serve` exits when its single
client drops), starts `booster-core --serve --port <port> --interactive --scenario terminal`
(`SCENARIO=entry` for the full 140 s descent), runs `k9variant.js` (waits for LANDING_BURN, shoots
side views, waits for the ground, shoots hero / base / hull / octaweb), takes one full-page PNG with
the HUD + FDAI, kills both. Results: `runs/shots/<tag>_*.jpg`, `C:\peek\_shots\peek_*.png`.

| file | role |
| --- | --- |
| `peekjs.py PORT FILE.js` / `PORT --expr JS` | run JS on the kept page; no shell quoting (argv straight to peek) |
| `k9cap_install.js` | installs `__k9cap(name, {d, el, az, h, fov, lead})` (posed HDR shot around the base plane) and `__k9probe` (raw linear pixels) |
| `k9live.js` | one shot from the LIVE director camera (`__shotHDR`) — use this in flight |
| `k9burst.js` | wait-for-phase burst on the live camera (CHASE / ORBIT / PAD) |
| `k9variant.js` | the D-056 battery (burn side views + landed close-ups) |

## Lessons paid for (2026-09-12)

- **Posed captures cannot lead a fast vehicle.** A `__shotPoseHDR` capture lands ~1–2 s after the
  call; at 140 m/s a 1.2 s velocity lead still missed the frame. In flight use the live director
  camera (`k9live.js`); pose only when the vehicle is slow (< ~30 m/s) or landed (`lead: 0`).
- **`r` is the CoM.** Aim at `base = com − com_z` along the vehicle axis; `__telem()` returns `comZ`.
- **The pivot's render position is three-world + `__doc.world.position`** (floating origin).
- **Port 8787 is also wrangler's default.** A stray `workerd` answers the WebSocket handshake with
  HTTP 200; the page takes `?port=NNNN`.
- **A camera below the ground disc sees the sky through it** (single-sided `CircleGeometry`) —
  `el` must keep the eye above `y = 0`.
