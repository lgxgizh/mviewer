# Package verification (M12.3 / G1 gate).
#
# Unpacks the portable zip and asserts the Qt imageformat plugins are present,
# so TIFF/JPEG/PNG/etc. decode on a clean Windows with no Qt installed. This is
# the automated guard that G1 (TIFF fails on clean Windows) stays closed — it
# fails loudly if a future packaging change drops imageformats/qtiff.dll.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File test_package.ps1
#   powershell -ExecutionPolicy Bypass -File test_package.ps1 -Version 1.0.4
#   powershell -ExecutionPolicy Bypass -File test_package.ps1 -ZipPath "path/to/MViewer-x.y.z-portable.zip"
# The zip path defaults to dist/MViewer-<Version>-portable.zip (Version from
# -Version, else `git describe --tags --always`).

param(
    [string]$Version,
    [string]$ZipPath,
    [string]$WorkDir = "D:/mviewer/dist/_verify"
)

$ErrorActionPreference = 'Stop'
if (-not $ZipPath) {
    if (-not $Version) { $Version = (git describe --tags --always 2>$null); if (-not $Version) { $Version = "0.0.0-dev" } }
    $ZipPath = "D:/mviewer/dist/MViewer-$Version-portable.zip"
}
if (-not (Test-Path $ZipPath)) { Write-Host "ERROR: $ZipPath not found — run scripts/package_portable.ps1 -Version $Version first."; exit 1 }
if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
Expand-Archive -Path $ZipPath -DestinationPath $WorkDir -Force

$required = @(
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'Qt6Sql.dll',
    'msvcp140.dll',
    'vcruntime140.dll',
    'imageformats\qtiff.dll',
    'imageformats\qjpeg.dll',
    'platforms\qwindows.dll'
)
$ok = $true
foreach ($f in $required) {
    $p = Join-Path $WorkDir $f
    if (Test-Path $p) { Write-Host "  PASS: $f" }
    else { Write-Host "  FAIL: $f MISSING"; $ok = $false }
}

Remove-Item -Recurse -Force $WorkDir
if (-not $ok) { Write-Host "PACKAGE VERIFY FAILED"; exit 1 }
Write-Host "=== package verify OK: Qt runtime + imageformat plugins present (G1 closed) ==="
