# Package MViewer as a portable ZIP (M11.3 Release Engineering).
#
# Builds Release, deploys Qt runtime next to the executable via windeployqt,
# then zips the deployment folder. This is the "portable zip" deliverable.
# (The NSIS/WiX *installer* remains a post-1.0 item per roadmap.md / RELEASE
# notes — this script does NOT require an installer builder.)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File pack_portable.ps1
# Output: D:/mviewer/dist/MViewer-portable.zip

param(
    [string]$BuildDir = "D:/mviewer/build_msvc",
    [string]$DistDir  = "D:/mviewer/dist",
    [string]$QtBin    = "D:/QT/6.11.1/msvc2022_64/bin",
    [string]$Version  = "1.0.0-rc"
)

$ErrorActionPreference = 'Stop'
$repo = 'D:\mviewer'

# 1) Release build (sanctioned entry point).
Write-Host "=== Step 1: Release build ==="
& powershell -ExecutionPolicy Bypass -File (Join-Path $repo 'build.ps1') Release
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED"; exit 1 }

$exe = Join-Path $BuildDir 'bin\MViewer.exe'
if (-not (Test-Path $exe)) { Write-Host "ERROR: $exe not found"; exit 1 }

# 2) Deploy Qt runtime beside the executable.
Write-Host "=== Step 2: windeployqt ==="
$windeploy = Join-Path $QtBin 'windeployqt.exe'
if (-not (Test-Path $windeploy)) { Write-Host "ERROR: $windeploy missing"; exit 1 }
# windeployqt emits benign warnings to stderr and may return non-zero; run it
# via cmd /c so its exit code is isolated (avoids PowerShell's RemoteException
# terminating the script under $ErrorActionPreference='Stop'). The deployment
# artifacts are verified below, not the exit code.
cmd /c "$windeploy --release --no-translations --no-opengl-sw --no-system-d3d-compiler $exe 2>&1"
if (-not (Test-Path (Join-Path $BuildDir 'bin\Qt6Core.dll'))) {
    Write-Host "ERROR: windeployqt did not deploy Qt6Core.dll"; exit 1
}
Write-Host "windeployqt: Qt runtime deployed."

# G1 guard: the imageformat plugins MUST be present so TIFF (qtiff.dll) and
# other formats decode on a clean Windows with no Qt installed. Fail loudly
# rather than shipping a zip where .tif/.tiff silently won't open.
$qtiff = Join-Path $BuildDir 'bin\imageformats\qtiff.dll'
if (-not (Test-Path $qtiff)) {
    Write-Host "ERROR: imageformats/qtiff.dll not deployed — TIFF would fail on clean Windows (G1). Aborting portable package."
    exit 1
}
Write-Host "G1 guard OK: imageformats/qtiff.dll present in deployment."

# 3) Zip the deployment folder.
Write-Host "=== Step 3: zip portable ==="
if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$zip = Join-Path $DistDir "MViewer-portable-$Version.zip"
$binDir = Join-Path $BuildDir 'bin'
Compress-Archive -Path "$binDir\*" -DestinationPath $zip -Force
Write-Host "=== portable zip written: $zip ==="
Write-Host (Get-Item $zip | Select-Object -ExpandProperty Length | ForEach-Object { "size: $([math]::Round($_/1MB)) MB" })
