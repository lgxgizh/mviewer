# Launcher: set up MSVC env (via vswhere, mirroring build.ps1) then run the G1 proof.
$ErrorActionPreference = 'Stop'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { Write-Error "vswhere not found"; exit 1 }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
if (-not $vsPath) { Write-Error "no VS with VC tools"; exit 1 }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { Write-Error "vcvars64.bat not found"; exit 1 }

# Capture the VC environment, then chain the proof script in the same cmd.
$proof = Join-Path $PSScriptRoot 'g1_clean_windows_proof.ps1'
cmd /c "`"$vcvars`" x64 && powershell -NoProfile -ExecutionPolicy Bypass -File `"$proof`""
exit $LASTEXITCODE
