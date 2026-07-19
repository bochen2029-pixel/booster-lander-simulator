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

# NOTE: $ErrorActionPreference = "Stop" does NOT catch native-command failures
# (pnpm/cargo exit codes) — check $LASTEXITCODE after every native step, or a
# failed `cargo tauri build` surfaces later as a bogus Copy-Item error.
function Assert-Native([string]$what) {
    if ($LASTEXITCODE -ne 0) { throw "$what failed with exit code $LASTEXITCODE" }
}

# 1. Fresh frontend bundle (both pages: cockpit + constellation).
# This script OWNS the frontend build; tauri.conf.json's beforeBuildCommand is
# deliberately empty (a relative `pnpm -C ../ui` hook resolved from the wrong
# cwd under cargo-tauri and died on 'C:\ui').
pnpm -C (Join-Path $repo "ui") install --frozen-lockfile
Assert-Native "pnpm install"
pnpm -C (Join-Path $repo "ui") build
Assert-Native "pnpm build"

# 2. Ensure the sidecar seat holds the CURRENT core build (Tauri externalBin naming)
$core = Join-Path $repo "build\bin\Release\booster-core.exe"
$seat = Join-Path $repo "shell\binaries\booster-core-x86_64-pc-windows-msvc.exe"
New-Item -ItemType Directory -Force (Split-Path $seat) | Out-Null
Copy-Item $core $seat -Force

# 3. Release build + bundle (NSIS installer; the bare exe is the portable artifact)
$env:PATH += ";$env:USERPROFILE\.cargo\bin"
Push-Location (Join-Path $repo "shell")
try {
    cargo tauri build
    Assert-Native "cargo tauri build"
} finally { Pop-Location }

# 4. Assemble the portable folder
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item (Join-Path $repo "shell\target\release\booster-shell.exe") (Join-Path $dist "Booster Lander.exe") -Force
Copy-Item $seat (Join-Path $dist "booster-core.exe") -Force
Copy-Item (Join-Path $repo "shell\target\release\bundle\nsis\Booster Lander_0.1.0_x64-setup.exe") $dist -Force

Write-Host "`nPORTABLE BUNDLE READY: $dist"
# ASCII only inside quoted strings: an em-dash's UTF-8 bytes read under Windows
# PowerShell 5.1's ANSI codepage contain 0x94 (a cp1252 smart-quote), which
# terminates the string early and kills the whole parse. pwsh is the documented
# host, but keep the strings 5.1-proof anyway.
Write-Host "Double-click 'Booster Lander.exe' - no terminals, no browser."
