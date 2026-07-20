<#
# package_release.ps1 -- M11.3 Release Engineering orchestrator
#
# Builds (optional) the Release target, then produces BOTH distribution artifacts
# from the review's M11.3 checklist:
#   1. dist/MViewer-<ver>-portable.zip   (self-contained, via package_portable.ps1)
#   2. dist/MViewer-<ver>-Setup.exe       (NSIS installer, via installer/MViewer.nsi)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 [-Build] [-Version 0.11.0]
#
# Prereqs: build.ps1 (build), Qt windeployqt, makensis on PATH or in PATH.
#>
param(
    [switch]$Build,
    [string]$Version = ""
)

$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
if (-not $Version) {
    $Version = (git describe --tags --always 2>$null)
    if (-not $Version) { $Version = "0.0.0-dev" }
    $Version = $Version.TrimStart("v")
}

# 1) Optional Release build
if ($Build) {
    Write-Host "=== build.ps1 Release ==="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "build.ps1") Release
    if ($LASTEXITCODE -ne 0) { throw "Release build failed" }
}

# 2) Portable zip
Write-Host "=== package_portable ==="
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "scripts/package_portable.ps1") -Version $Version
if ($LASTEXITCODE -ne 0) { throw "portable packaging failed" }

# 3) NSIS installer (needs the staging dir from package_portable)
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if (-not $makensis) {
    # Common install locations
    $candidates = @("C:/Program Files (x86)/NSIS/makensis.exe","C:/Program Files/NSIS/makensis.exe")
    foreach ($c in $candidates) { if (Test-Path $c) { $makensis = $c; break } }
}
$staging = Join-Path $root "dist/staging/MViewer"
if (-not (Test-Path $staging)) { throw "staging dir missing: $staging (run package_portable first)" }

if ($makensis) {
    Write-Host "=== makensis (installer) ==="
    # VIProductVersion needs exactly 4 segments (X.X.X.X)
    $viVersion = ($Version -replace '[^0-9.].*$', '')  # drop pre-release suffix
    $viVersion = ($viVersion -split '\.')[0..3] -join '.'  # take up to 4 segs
    while (($viVersion -split '\.').Count -lt 4) { $viVersion += '.0' }
    $outFile = Join-Path $root "dist/MViewer-$Version-Setup.exe"
    New-Item -ItemType Directory -Force -Path (Split-Path $outFile) | Out-Null
    & $makensis /DAPP_DIR="$staging" /DVERSION="$Version" /DVI_VERSION="$viVersion" "/DOUTFILE=$outFile" (Join-Path $root "installer/MViewer.nsi")
    if ($LASTEXITCODE -ne 0) { throw "NSIS build failed" }
    $setup = Join-Path $root "dist/MViewer-$Version-Setup.exe"
    if (Test-Path $setup) { Write-Host "=== installer: $setup ($([math]::Round((Get-Item $setup).Length/1MB,1)) MB) ===" }
} else {
    Write-Warning "makensis not found -- skipped installer. Portable zip is the distributable."
}

Write-Host "=== release packaging complete (version $Version) ==="
