# Build the MViewer NSIS installer (M12.3 Release Engineering).
#
# Stages a windeployqt deployment of MViewer.exe into a staging dir, then runs
# makensis on installer/mviewer.nsi. The deployment includes the Qt imageformat
# plugins (imageformats/qtiff.dll ...), so the installed app decodes TIFF/JPEG/
# PNG/etc. on a clean Windows with no Qt (closes gap G1).
#
# Requires NSIS (makensis.exe) on PATH or under ProgramFiles. If makensis is
# absent the script fails with a clear message instead of producing a broken
# installer. The NSIS install itself is a one-time build-tool setup:
#   choco install nsis   (or download from https://nsis.sourceforge.io/)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File pack_installer.ps1
# Output: D:/mviewer/dist/MViewer-1.0.0-setup.exe

param(
    [string]$BuildDir = "D:/mviewer/build_msvc",
    [string]$StageDir = "D:/mviewer/dist/stage",
    [string]$DistDir  = "D:/mviewer/dist",
    [string]$QtBin    = "D:/QT/6.11.1/msvc2022_64/bin",
    [string]$Version  = "1.0.0"
)

$ErrorActionPreference = 'Stop'
$repo = 'D:\mviewer'

# 1) Release build (sanctioned entry point).
Write-Host "=== Step 1: Release build ==="
& powershell -ExecutionPolicy Bypass -File (Join-Path $repo 'build.ps1') Release
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED"; exit 1 }

$exe = Join-Path $BuildDir 'bin\MViewer.exe'
if (-not (Test-Path $exe)) { Write-Host "ERROR: $exe not found"; exit 1 }

# 2) Stage a clean windeployqt deployment.
Write-Host "=== Step 2: windeployqt stage ==="
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
$windeploy = Join-Path $QtBin 'windeployqt.exe'
if (-not (Test-Path $windeploy)) { Write-Host "ERROR: $windeploy missing"; exit 1 }
# Deploy into the staging dir (not the build bin, to keep the build tree clean).
cmd /c "$windeploy --release --no-translations --no-opengl-sw --no-system-d3d-compiler $exe --dir $StageDir 2>&1"
if (-not (Test-Path (Join-Path $StageDir 'Qt6Core.dll'))) {
    Write-Host "ERROR: windeployqt did not deploy Qt6Core.dll"; exit 1
}

# 3) G1 guard: the imageformat plugins MUST be present so TIFF (qtiff.dll) and
#    other formats decode on a clean Windows. Fail loudly if missing.
$qtiff = Join-Path $StageDir 'imageformats\qtiff.dll'
if (-not (Test-Path $qtiff)) {
    Write-Host "ERROR: imageformats/qtiff.dll not deployed — TIFF would fail on clean Windows (G1). Aborting installer build."
    exit 1
}
Write-Host "G1 guard OK: imageformats/qtiff.dll present."

# 4) Locate makensis (NSIS).
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if (-not $makensis) {
    $nsis = Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'
    if (-not (Test-Path $nsis)) { $nsis = Join-Path $env:ProgramFiles 'NSIS\makensis.exe' }
    if (-not (Test-Path $nsis)) {
        Write-Host "ERROR: NSIS (makensis.exe) not found. Install NSIS to build the installer:"
        Write-Host "  choco install nsis   OR   https://nsis.sourceforge.io/"
        exit 1
    }
    $makensis = $nsis
}
Write-Host "=== Step 3: makensis ==="
& $makensis /DDEPLOY_DIR="$StageDir" /DOUTDIR="$DistDir" /DAPPVERSION="$Version" (Join-Path $repo 'installer\mviewer.nsi') 2>&1 | ForEach-Object { Write-Host $_ }
$setup = Join-Path $DistDir "MViewer-$Version-setup.exe"
if (Test-Path $setup) {
    Write-Host "=== installer written: $setup ==="
    Write-Host (Get-Item $setup | Select-Object -ExpandProperty Length | ForEach-Object { "size: $([math]::Round($_/1MB)) MB" })
} else {
    Write-Host "ERROR: installer not produced."; exit 1
}
