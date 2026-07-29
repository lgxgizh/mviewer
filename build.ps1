$ErrorActionPreference = 'Stop'

# ── Parameter (bash-safe: no ValidateSet) ─────────────────────────────────
$Task = 'Release'
if ($args.Count -gt 0) { $Task = $args[0] }

if ($Task -eq '--help' -or $Task -eq '-h') {
    Write-Host @"
MViewer Build System — Single Entry Point
========================================

Usage:
    .\build.ps1              Release build (default)
    .\build.ps1 Release      Release build
    .\build.ps1 Debug        Debug build
    .\build.ps1 Test         Build + run tests
    .\build.ps1 Clean        Remove build_msvc/

Qt detection order: Qt6_DIR > QT_ROOT > D:\QT\6.11.1 (legacy fallback).

"@
    exit 0
}

Set-Location $PSScriptRoot

# ── 1. Auto-locate VS + init environment ──────────────────────────────────
function Import-MSVCEnvironment {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { Write-Error "vswhere.exe not found"; exit 1 }

    $vsJson = & $vswhere -products * -format json | ConvertFrom-Json
    $vsPath = $script:vsPath = $vsJson.installationPath
    if (-not $vsPath) { Write-Error "VS Build Tools not found"; exit 1 }

    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { Write-Error "vcvars64.bat not found"; exit 1 }

    Write-Host "[VS] $vsPath" -ForegroundColor Cyan
    Write-Host "[VS] Initializing environment..." -ForegroundColor Cyan

    cmd /c "`"$vcvars`" x64 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2])
        }
    }
}

Import-MSVCEnvironment | Out-Null

# ── 1b. Deploy MSVC C++ runtime next to the built exe ───────────────────────
# windeployqt (CMake POST_BUILD) ships the Qt DLLs; this step ships the MSVC CRT
# so MViewer.exe (and every test exe in bin/) is fully self-contained and launches
# without the VS redistributable installed on PATH / system-wide. This directly
# fixes the recurring "missing VCRUNTIME140.dll" / 0xc0000135 errors when running
# the exe (or the test suite) directly.
# Order of CRT source lookup: the active toolset's VCToolsRedistDir env var, then
# the well-known D:\msvc layout used on this dev box, then the resolved VS path.
function Deploy-Runtime {
    $binDir = Join-Path $buildDir 'bin'
    if (-not (Test-Path $binDir)) { return }

    $crtRoots = @(
        $env:VCToolsRedistDir,
        'D:\msvc\VC\Redist\MSVC',
        (Join-Path $script:vsPath 'VC\Redist\MSVC')
    ) | Where-Object { $_ -and (Test-Path $_) }

    $crtDir = $null
    # Prefer the desktop v143 toolset CRT (matches the compiler), not the
    # onecore/store variants; fall back to any VC14x CRT if it isn't present.
    foreach ($root in $crtRoots) {
        $crtDir = Get-ChildItem -Path $root -Directory -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\Microsoft\.VC143\.CRT$' -and $_.FullName -notmatch '\\onecore\\' } |
            Select-Object -First 1
        if ($crtDir) { break }
    }
    if (-not $crtDir) {
        foreach ($root in $crtRoots) {
            $crtDir = Get-ChildItem -Path $root -Directory -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\x64\\Microsoft\.VC14\d+\.CRT$' -and $_.FullName -notmatch '\\onecore\\' } |
                Select-Object -First 1
            if ($crtDir) { break }
        }
    }

    if ($crtDir) {
        foreach ($dll in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll',
                            'msvcp140_1.dll', 'msvcp140_2.dll', 'concrt140.dll')) {
            $src = Join-Path $crtDir.FullName $dll
            if (Test-Path $src) { Copy-Item $src $binDir -Force }
        }
        Write-Host "[Deploy] MSVC CRT <- $($crtDir.FullName)" -ForegroundColor Green
    } else {
        Write-Warning "[Deploy] MSVC VC14x.CRT not found; ensure the VS redistributable is installed or on PATH."
    }
}

# ── 2. Locate Qt (priority order: env vars > default) ────────────────────
# install-qt-action@v4 exports QT_ROOT_DIR (already the msvc2022_64 root).
# Other setups may set Qt6_DIR (lib/cmake/Qt6) or QT_ROOT (install prefix).
$qtPath = $null
if ($env:QT_ROOT_DIR -and (Test-Path $env:QT_ROOT_DIR)) {
    $qtPath = $env:QT_ROOT_DIR
    Write-Host "[Qt] `$env:QT_ROOT_DIR = $qtPath" -ForegroundColor Cyan
}
elseif ($env:Qt6_DIR) {
    # Qt6_DIR usually points at <prefix>/lib/cmake/Qt6; walk up to the Qt root.
    $qtPath = (Resolve-Path (Join-Path $env:Qt6_DIR '..\..\..')).Path
    Write-Host "[Qt] `$env:Qt6_DIR = $qtPath" -ForegroundColor Cyan
}
elseif ($env:QT_ROOT) {
    $qtPath = Join-Path $env:QT_ROOT 'msvc2022_64'
    Write-Host "[Qt] `$env:QT_ROOT = $qtPath" -ForegroundColor Cyan
}
elseif (Test-Path 'D:\QT\6.11.1\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake') {
    $qtPath = 'D:\QT\6.11.1\msvc2022_64'
    Write-Host "[Qt] Default: $qtPath" -ForegroundColor Cyan
}
elseif (Test-Path "$env:USERPROFILE\Qt\6.11.1\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake") {
    $qtPath = "$env:USERPROFILE\Qt\6.11.1\msvc2022_64"
    Write-Host "[Qt] Default: $qtPath" -ForegroundColor Cyan
}
elseif (Test-Path "$env:ProgramFiles\Qt\6.11.1\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake") {
    $qtPath = "$env:ProgramFiles\Qt\6.11.1\msvc2022_64"
    Write-Host "[Qt] Default: $qtPath" -ForegroundColor Cyan
}
else {
    Write-Error @"
Qt 6 not found. Set one of:
  `$env:QT_ROOT_DIR  e.g. C:\Qt\6.11.1\msvc2022_64   (install-qt-action@v4 default)
  `$env:Qt6_DIR      e.g. C:\Qt\6.11.1\msvc2022_64\lib\cmake\Qt6
  `$env:QT_ROOT      e.g. C:\Qt\6.11.1
Or install Qt to: D:\QT\6.11.1\msvc2022_64
"@
    exit 1
}

if (-not (Test-Path (Join-Path $qtPath 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    Write-Error "Qt cmake config not found at $qtPath"
    exit 1
}

# ── 3. Configure (always run CMake; it is idempotent) ─────────────────────
$buildDir = Join-Path $PSScriptRoot 'build_msvc'
$config   = if ($Task -eq 'Debug') { 'Debug' } else { 'Release' }

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory $buildDir | Out-Null
}

Set-Location $buildDir
# Quote the -D defines so variable expansion is deterministic and paths
# containing spaces (per-machine Qt installs) are handled correctly.
cmake -G Ninja "-DCMAKE_BUILD_TYPE=$config" "-DCMAKE_PREFIX_PATH=$qtPath" ..

# ── 4. Task execution ─────────────────────────────────────────────────────
switch ($Task) {
    'Clean' {
        Set-Location $PSScriptRoot
        Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
        Write-Host "[Clean] Done" -ForegroundColor Green
    }
    'Test' {
        Write-Host "[Build] $config..." -ForegroundColor Cyan
        cmake --build . -j
        if ($LASTEXITCODE -ne 0) { throw "Build failed" }
        # Ship Qt (via windeployqt POST_BUILD) + MSVC CRT into bin/ so the test
        # executables are self-contained and don't trip 0xc0000135.
        Deploy-Runtime
        Write-Host "[Test] Running CTest..." -ForegroundColor Cyan
        # Make Qt DLLs resolvable for the test executables (avoids
        # 0xc0000135 STATUS_DLL_NOT_FOUND). Qt < 6 forbids loading from the
        # working directory, so an explicit PATH entry is required.
        $env:PATH = "$qtPath\bin" + [System.IO.Path]::PathSeparator + $env:PATH
        $env:QT_QPA_PLATFORM = 'offscreen'
        ctest --output-on-failure --output-junit test-results.xml -j4
        if ($LASTEXITCODE -ne 0) { Write-Warning "Tests failed" }
    }
    default {
        Write-Host "[Build] $config..." -ForegroundColor Cyan
        cmake --build . -j
        if ($LASTEXITCODE -ne 0) { throw "Build failed" }
        # Deploy Qt (windeployqt POST_BUILD) + MSVC CRT into bin/ so MViewer.exe
        # launches directly without any Qt / VS redistributable on PATH.
        Deploy-Runtime
        Write-Host "[Build] OK - $config" -ForegroundColor Green
    }
}
