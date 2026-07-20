<#
# package_portable.ps1 -- M11.3 Release Engineering
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

# 1) Qt runtime + plugins via windeployqt (release only, no translations/debug).
#    Run it against BOTH MViewer.exe AND mviewer_core.dll: the core DLL is what
#    actually links Qt6::Sql (DiskCache), and MViewer.exe does not import it
#    directly -- so windeployqt on MViewer.exe alone can miss Qt6Sql.dll.
& $windeployqt --release --no-translations --no-compiler-runtime --dir $staging $exe 2>&1 | ForEach-Object { Write-Host "  [windeployqt] $_" }
$coreDll = Join-Path $root (Join-Path $BuildDir "bin/mviewer_core.dll")
if (Test-Path $coreDll) {
    & $windeployqt --release --no-translations --no-compiler-runtime --dir $staging $coreDll 2>&1 | ForEach-Object { Write-Host "  [windeployqt:core] $_" }
}
# Safety net: Qt6Sql.dll is required at runtime (DiskCache uses SQLite). Copy it
# explicitly in case windeployqt still misses it.
$sqlSrc = Join-Path $QtDir "bin/Qt6Sql.dll"
if ((Test-Path $sqlSrc) -and -not (Test-Path (Join-Path $staging "Qt6Sql.dll"))) {
    Copy-Item $sqlSrc $staging | Out-Null; Write-Host "  [qt] Qt6Sql.dll (explicit)"
}

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
# The shared core library links Qt6::Sql etc. -- must ship alongside the exe.
if (Test-Path $coreDll) { Copy-Item $coreDll $staging | Out-Null; Write-Host "  [asset] mviewer_core.dll" }
foreach ($f in @("README.md","CHANGELOG.md","LICENSE")) {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src $staging | Out-Null; Write-Host "  [asset] $f" }
}
# Benchmark corpus generator + golden data (optional, keeps mviewer_bench usable)
$benchSrc = Join-Path $root "benchmarks"
if (Test-Path $benchSrc) { Copy-Item $benchSrc (Join-Path $staging "benchmarks") -Recurse | Out-Null; Write-Host "  [asset] benchmarks/" }

# 3b) Defense-in-depth: a Release package must contain ONLY the shipping app.
#     Strip any test / diagnostic / bench executables that may have landed in
#     staging (they must never ship to end users).
$forbidden = @("test", "tests", "mviewer_bench", "mviewer_unit", "benchmark",
               "golden_main", "ui_fixture", "vision_regression", "core_tests",
               "analysis_panel", "compare_workflow", "product_browse",
               "shortcuts_tests", "workspace_persist", "m3pipeline", "testdata_regression")
$removed = 0
Get-ChildItem $staging -Filter *.exe | ForEach-Object {
    $base = $_.BaseName
    foreach ($bad in $forbidden) {
        if ($base -like "$bad*" -or $base -like "*$bad*") {
            Remove-Item $_.FullName -Force
            Write-Host "  [strip] $($_.Name)"
            $removed++
            break
        }
    }
}
if ($removed -gt 0) { Write-Host "  [strip] removed $removed non-shipping executable(s)" }

# 4) Compress ----------------------------------------------------------------
$zip = Join-Path $root (Join-Path $OutDir "MViewer-$Version-portable.zip")
if (Test-Path $zip) { Remove-Item -Force $zip }
# Compress the staging directory contents (forward-slash entries for portability).
Compress-Archive -Path "$staging/*" -DestinationPath $zip -CompressionLevel Optimal
Write-Host "=== portable zip: $zip ($([math]::Round((Get-Item $zip).Length/1MB,1)) MB) ==="
