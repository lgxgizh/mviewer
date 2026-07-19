# G1 proof: simulate a clean Windows (no system Qt) and decode a TIFF.
# Deploys MViewer.exe + Qt runtime to an isolated staging dir via windeployqt,
# compiles g1_tiff_probe against mviewer_core, runs it with PATH restricted to
# the staged Qt dir only. If it decodes a .tif, G1 (TIFF on clean Windows) is proven.
$ErrorActionPreference = 'Stop'
$repo = 'D:\mviewer'
$qtBin = 'D:\QT\6.11.1\msvc2022_64\bin'
$build = Join-Path $repo 'build_msvc'
$stage = Join-Path $repo 'dist\g1_stage'
$tiff = Join-Path $repo 'testdata\golden\1024x768\gradient_1024x768.tiff'

if (-not (Test-Path $tiff)) { Write-Host "ERROR: tiff fixture missing"; exit 1 }

# 1) Stage a clean windeployqt deployment of MViewer.exe.
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$windeploy = Join-Path $qtBin 'windeployqt.exe'
cmd /c "$windeploy --release --no-translations --no-opengl-sw --no-system-d3d-compiler $build\bin\MViewer.exe --dir $stage 2>&1"
if (-not (Test-Path (Join-Path $stage 'Qt6Core.dll'))) { Write-Host "ERROR: windeployqt failed"; exit 1 }
$qtiff = Join-Path $stage 'imageformats\qtiff.dll'
if (-not (Test-Path $qtiff)) { Write-Host "ERROR: qtiff.dll not deployed"; exit 1 }
Write-Host "G1 deploy: Qt runtime + qtiff.dll staged at $stage"

# 2) Compile the probe against mviewer_core.lib.
$probe = Join-Path $repo 'scripts\g1_tiff_probe.cpp'
$out = Join-Path $stage 'g1_tiff_probe.exe'
$qtInclude = 'D:\QT\6.11.1\msvc2022_64\include'
$qtLib = 'D:\QT\6.11.1\msvc2022_64\lib'
# Build flags mirror the project's C++20 /utf-8 / permissive- settings.
$cl = "cl.exe /nologo /std:c++20 /utf-8 /EHsc /MD /O2 " +
      "/I$dqtInclude /I$dqtInclude\QtCore /I$dqtInclude\QtGui " +
      "/I$repo\src " +
      "/DQT_CORE_LIB /DQT_GUI_LIB " +
      "`"$probe`" " +
      "`"$build\src\mviewer_core.lib`" " +
      "`"$qtLib\Qt6Core.lib`" `"$qtLib\Qt6Gui.lib`" " +
      "/link /SUBSYSTEM:CONSOLE /OUT:`"$out`""
cmd /c $cl
if (-not (Test-Path $out)) { Write-Host "ERROR: probe compile failed"; exit 1 }

# The probe links mviewer_core.dll at runtime — stage it next to the probe so
# the clean-Windows simulation (PATH = staged dir only) can resolve it.
Copy-Item (Join-Path $build 'bin\mviewer_core.dll') $stage -Force
if (-not (Test-Path (Join-Path $stage 'mviewer_core.dll'))) { Write-Host "ERROR: mviewer_core.dll copy failed"; exit 1 }

# 3) Run with PATH restricted to the staged Qt dir only (clean-Windows sim).
$env:PATH = "$stage;$qtBin"
$env:QT_QPA_PLATFORM = 'offscreen'
# Qt discovers imageformat plugins from QT_PLUGIN_PATH (or a qt.conf). In the
# isolated staging dir this points Qt at imageformats/qtiff.dll etc.
$env:QT_PLUGIN_PATH = $stage
Write-Host "G1 run: PATH restricted to staged dir; decoding $tiff"
& $out $tiff
exit $LASTEXITCODE
