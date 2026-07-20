<#
# package_portable.ps1 — M11.3 Release Engineering
#
# Produce a self-contained, redistributable MViewer portable ZIP from a Release
# build. Uses windeployqt (Qt's official deploy tool) to gather the exact set of
# Qt6 runtime DLLs + platform/imageformat plugins the binary actually imports,
# then bundles the MSVC C++ runtime so the archive runs on a clean Windows box.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/package_portable.ps1 `
#       [-BuildDir build_msvc] [-QtDir D:/QT/6.11.1/msvc2022_64] `
#       [-Version 0.11.0] [-OutDir dist]
#
# Output: dist/MViewer-<version>-portable.zip
#>
param(
    [string]$BuildDir = "build_msvc",
    [string]$QtDir    = "D:/QT/6.11.1/msvc2022_64",
    [string]$Version  = "",
    [string]$OutDir   = "dist"
)

$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path

$exe = Join-Path $root (Join-Path $BuildDir "bin/MViewer.exe")
if (-not (Test-Path $exe)) {
    Write-Error "MViewer.exe not found at $exe. Build Release first (build.ps1 Release)."
}

# --- version (git describe, else fallback) -----------------------------------
if (-not $Version) {
    $Version = (git describe --tags --always 2>$null)
    if (-not $Version) { $Version = "0.0.0-dev" }
    $Version = $Version.TrimStart("v")
}

$windeployqt = Join-Path $QtDir "bin/windeployqt.exe"
if (-not (Test-Path $windeployqt)) { Write-Error "windeployqt not found at $windeployqt" }

$staging = Join-Path $root (Join-Path $OutDir "staging/MViewer")
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

# 1) Qt runtime + plugins via windeployqt (release only, no translations/debug)
& $windeployqt --release --no-translations --no-compiler-runtime --dir $staging $exe 2>&1 | ForEach-Object { Write-Host "  [windeployqt] $_" }

# 2) MSVC C++ runtime (matching toolset under D:/msvc/VC/Redist/MSVC) ----------
$crtDir = Get-ChildItem -Path "D:/msvc/VC/Redist/MSVC" -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1
if ($crtDir) {
    $crt = Join-Path $crtDir.FullName "x64/Microsoft.VC140.CRT"
    if (Test-Path $crt) {
        foreach ($dll in @("vcruntime140.dll","vcruntime140_1.dll","msvcp140.dll","msvcp140_1.dll","msvcp140_2.dll","concrt140.dll")) {
            $src = Join-Path $crt $dll
            if (Test-Path $src) { Copy-Item $src $staging | Out-Null; Write-Host "  [vcrt] $dll" }
        }
    } else { Write-Warning "VC140.CRT not found under $($crtDir.FullName); portable zip may need vc_redist." }
} else { Write-Warning "MSVC Redist not found; portable zip may need vc_redist installed." }

# 3) App assets --------------------------------------------------------------
Copy-Item $exe $staging | Out-Null
foreach ($f in @("README.md","CHANGELOG.md","LICENSE")) {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src $staging | Out-Null; Write-Host "  [asset] $f" }
}
# Benchmark corpus generator + golden data (optional, keeps mviewer_bench usable)
$benchSrc = Join-Path $root "benchmarks"
if (Test-Path $benchSrc) { Copy-Item $benchSrc (Join-Path $staging "benchmarks") -Recurse | Out-Null; Write-Host "  [asset] benchmarks/" }

# 4) Compress ----------------------------------------------------------------
$zip = Join-Path $root (Join-Path $OutDir "MViewer-$Version-portable.zip")
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zip -CompressionLevel Optimal
Write-Host "=== portable zip: $zip ($([math]::Round((Get-Item $zip).Length/1MB,1)) MB) ==="
