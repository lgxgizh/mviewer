# version_gate.ps1 -- M24 version-consistency gate (CTest: `version_consistency`)
#
# Enforces the M24 version SSOT: CMake project(VERSION) is the single source.
# Fails when:
#   1. build/version_info.txt disagrees with the expected version.
#   2. CMakeLists.txt project(VERSION) disagrees with the expected version.
#   3. The app/UpdateChecker/workspace hard-code a version literal instead of
#      consuming MViewerVersion.h.
#   4. Any new "1.0.x" style literal appears in src/ (diagnostic list included).
#   5. Packaging scripts no longer read version_info.txt.
#   6. STATUS.md does not state the current version.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/version_gate.ps1 `
#       -SourceDir <repo> -BuildDir <build> -Version <X.Y.Z>
param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Version
)
$ErrorActionPreference = 'Stop'
$failed = $false
function Fail([string]$msg) { Write-Host "FAIL: $msg"; $script:failed = $true }
function Ok([string]$msg) { Write-Host "ok: $msg" }

# 1) generated version_info.txt
$verFile = Join-Path $BuildDir 'version_info.txt'
if (-not (Test-Path $verFile)) {
    Fail "generated $verFile missing — run CMake configure first"
} else {
    $line = (Get-Content $verFile | Where-Object { $_ -match '^MVIEWER_VERSION=' } | Select-Object -First 1)
    if ($line -and ($line.Trim() -eq "MVIEWER_VERSION=$Version")) { Ok "version_info.txt == $Version" }
    else { Fail "version_info.txt says '$line', expected MVIEWER_VERSION=$Version" }
}

# 2) CMakeLists.txt project VERSION
$cmakeLine = (Get-Content (Join-Path $SourceDir 'CMakeLists.txt') | Where-Object { $_ -match '^\s*VERSION\s+[0-9]+\.[0-9]+\.[0-9]+' } | Select-Object -First 1)
if ($cmakeLine -and ($cmakeLine -match "VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)") -and ($Matches[1] -eq $Version)) {
    Ok "CMakeLists.txt VERSION == $Version"
} else {
    Fail "CMakeLists.txt VERSION '$($cmakeLine.Trim())' != expected $Version"
}

# 3) hard-coded version literals in code
$src = Join-Path $SourceDir 'src'
$patterns = @(
    @{ Name = 'UpdateChecker ctor literal';      Regex = 'UpdateChecker\s+[A-Za-z_]\w*\s*\(\s*"[0-9]';   Where = 'UpdateChecker/1.0' }
    @{ Name = 'appVersion literal';              Regex = 'appVersion\s*=\s*"[0-9]';                    Where = '' }
    @{ Name = 'setApplicationVersion literal';   Regex = 'setApplicationVersion\s*\(\s*"[0-9]';       Where = '' }
    @{ Name = '1.0.x semver literal';            Regex = '"[1]\.[0-9]+\.[0-9]+"';                      Where = '0.1.0 analyzer versions / ICC data / user-agent' }
)
foreach ($p in $patterns) {
    $hits = Get-ChildItem -Path $src -Recurse -Include *.cpp, *.h |
        Select-String -Pattern $p.Regex -ErrorAction SilentlyContinue
    $real = @($hits | Where-Object { $_.Line -notmatch [regex]::Escape($p.Where) })
    if ($real.Count -eq 0) { Ok "no $($p.Name)" }
    else {
        Fail "$($p.Name) found:"
        foreach ($h in $real) { Write-Host "    $($h.Path):$($h.LineNumber): $($h.Line.Trim())" }
    }
}

# 4) packaging scripts must consume version_info.txt
foreach ($ps in @('package_release.ps1', 'package_portable.ps1')) {
    $path = Join-Path $SourceDir (Join-Path 'scripts' $ps)
    if (-not (Test-Path $path)) { Fail "missing script $ps"; continue }
    if ((Get-Content $path) -match 'version_info\.txt') { Ok "$ps reads version_info.txt" }
    else { Fail "$ps does not reference version_info.txt (version SSOT broken)" }
}

# 5) STATUS.md must state the current version
$status = Get-Content (Join-Path $SourceDir 'STATUS.md') -Raw
if ($status -match [regex]::Escape($Version)) { Ok "STATUS.md mentions $Version" }
else { Fail "STATUS.md does not mention current version $Version" }

if ($failed) {
    Write-Host "version_consistency: FAIL"
    exit 1
}
Write-Host "version_consistency: PASS (all sources agree on $Version)"
exit 0
