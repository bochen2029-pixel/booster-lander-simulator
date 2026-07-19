# make_portable.ps1 — produce the one-click portable Booster Lander bundle.
#
# Output: dist\BoosterLander\
#   Booster Lander.exe                  <- double-click this; it spawns its own sim core
#   booster-core.exe                    <- the sim sidecar (job-object reaped on close)
#   Booster Lander_0.1.0_x64-setup.exe  <- optional NSIS installer of the same app
#
# Needs (one-time): pnpm, Rust toolchain, tauri-cli (cargo install tauri-cli --locked).
# The app is its own client (Tauri window over the system WebView2 — no browser, no
# terminals). Verified flow: launch -> supervisor spawns `booster-core --serve` on a
# free port -> HELLO v3 identity gate -> stream; closing the window reaps the sidecar.
#
# Usage:  pwsh -File tools\make_portable.ps1
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $repo "dist\BoosterLander"

# 1. Fresh frontend bundle (both pages: cockpit + constellation)
pnpm -C (Join-Path $repo "ui") install --frozen-lockfile
pnpm -C (Join-Path $repo "ui") build

# 2. Ensure the sidecar seat holds the CURRENT core build (Tauri externalBin naming)
$core = Join-Path $repo "build\bin\Release\booster-core.exe"
$seat = Join-Path $repo "shell\binaries\booster-core-x86_64-pc-windows-msvc.exe"
New-Item -ItemType Directory -Force (Split-Path $seat) | Out-Null
Copy-Item $core $seat -Force

# 3. Release build + bundle (NSIS installer; the bare exe is the portable artifact)
$env:PATH += ";$env:USERPROFILE\.cargo\bin"
Push-Location (Join-Path $repo "shell")
try { cargo tauri build } finally { Pop-Location }

# 4. Assemble the portable folder
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item (Join-Path $repo "shell\target\release\booster-shell.exe") (Join-Path $dist "Booster Lander.exe") -Force
Copy-Item $seat (Join-Path $dist "booster-core.exe") -Force
Copy-Item (Join-Path $repo "shell\target\release\bundle\nsis\Booster Lander_0.1.0_x64-setup.exe") $dist -Force

Write-Host "`nPORTABLE BUNDLE READY: $dist"
Write-Host "Double-click 'Booster Lander.exe' — no terminals, no browser."
