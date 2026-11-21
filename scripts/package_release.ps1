<#
# package_release.ps1 -- M11.3 Release Engineering orchestrator
#
# Builds (optional) the Release target, then produces BOTH distribution artifacts
# from the review's M11.3 checklist:
#   1. dist/MViewer-<ver>-portable.zip   (self-contained, via package_portable.ps1)
#   2. dist/MViewer-<ver>-Setup.exe       (NSIS installer, via installer/MViewer.nsi)
#   3. dist/MViewer-<ver>-symbols.zip     (PDBs for MViewer.exe / mviewer_core.dll;
#      never shipped inside the zip or installer, used to read crash dumps)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 [-Build] [-Version 0.11.0]
#
# Prereqs: build.ps1 (build), Qt windeployqt, makensis on PATH or in PATH.
#>
param(
    [switch]$Build,
    [string]$Version = "",
    [string]$QtDir = "",
    [string]$OutDir = "dist"
)

$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
. (Join-Path $PSScriptRoot 'release_version.ps1')
$powerShellExe = Join-Path $PSHOME 'pwsh.exe'
if (-not (Test-Path -LiteralPath $powerShellExe)) {
    $powerShellExe = Join-Path $PSHOME 'powershell.exe'
}
if (-not $Version) {
    # M24 version SSOT: read from the CMake-generated file; git describe only
    # as a fallback so the installer name always matches the app version.
    $verFile = Join-Path $root "build_msvc/version_info.txt"
    if (Test-Path $verFile) {
        $verLine = Get-Content $verFile | Where-Object { $_ -match '^MVIEWER_VERSION=' } | Select-Object -First 1
        if ($verLine) { $Version = $verLine.Substring("MVIEWER_VERSION=".Length).Trim() }
    }
    if (-not $Version) {
        $Version = (git describe --tags --always 2>$null)
        if (-not $Version) { throw "Release version is required (no CMake version_info.txt or git tag found)" }
        $Version = $Version.TrimStart("v")
    }
}
$identity = Get-ReleaseVersionIdentity $Version
$Version = $identity.Version

# 1) Optional Release build
if ($Build) {
    Write-Host "=== build.ps1 Release ==="
    & $powerShellExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "build.ps1") Release
    if ($LASTEXITCODE -ne 0) { throw "Release build failed" }
}

# 1b) Authenticode signing (review item 2). Requires a code-signing certificate;
# a repository without one still ships, and the gap is stated rather than hidden.
# The certificate is supplied base64-encoded in $env:MVIEWER_SIGN_CERT with its
# password in $env:MVIEWER_SIGN_PASSWORD, so no private key ever lives in the
# repository or in the artifact.
$signCertB64 = $env:MVIEWER_SIGN_CERT
$signPassword = $env:MVIEWER_SIGN_PASSWORD
$signServer = $env:MVIEWER_SIGN_TIMESTAMP
if (-not $signServer) { $signServer = 'http://timestamp.digicert.com' }
$script:signedAny = $false
$script:signNote = 'unsigned (no MVIEWER_SIGN_CERT in the environment)'
$script:signCertPath = $null

function Get-SignTool {
    $tool = Get-Command signtool -ErrorAction SilentlyContinue
    if ($tool) { return $tool.Source }
    foreach ($c in @(
        'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe',
        'C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/signtool.exe',
        'C:/Program Files (x86)/Windows Kits/10/bin/10.0.22000.0/x64/signtool.exe')) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

function Sign-Artifact([string]$Path) {
    if ($script:signedAny -eq $false -and -not $script:signCertPath) {
        if (-not $signCertB64 -or -not $signPassword) { return $false }
        $tool = Get-SignTool
        if (-not $tool) { Write-Host '=== signing: signtool.exe not found ==='; return $false }
        $script:signCertPath = Join-Path $env:TEMP ('mviewer-sign-' + [guid]::NewGuid().ToString('N') + '.pfx')
        [IO.File]::WriteAllBytes($script:signCertPath, [Convert]::FromBase64String($signCertB64))
        $script:signTool = $tool
    }
    Write-Host "=== signing $Path ==="
    & $script:signTool sign /fd SHA256 /tr $signServer /td SHA256 /f $script:signCertPath /p $signPassword $Path
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $Path (exit $LASTEXITCODE)" }
    $script:signedAny = $true
    $script:signNote = 'signed with the MVIEWER_SIGN_CERT certificate'
    return $true
}

if (-not $signCertB64 -or -not $signPassword) {
    Write-Host '=== signing: SKIPPED (MVIEWER_SIGN_CERT / MVIEWER_SIGN_PASSWORD not set) ==='
}

# 2) Portable zip
Write-Host "=== package_portable ==="
# Sign the shipped binaries first so the zip and the installer both carry it.
foreach ($target in @('build_msvc/bin/MViewer.exe', 'build_msvc/bin/mviewer_core.dll')) {
    $full = Join-Path $root $target
    if (Test-Path -LiteralPath $full) { [void](Sign-Artifact $full) }
}
& $powerShellExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "scripts/package_portable.ps1") `
    -Version $Version -OutDir $OutDir $(if ($QtDir) { @('-QtDir', $QtDir) } else { @() })
if ($LASTEXITCODE -ne 0) { throw "portable packaging failed" }

# 3) NSIS installer (needs the staging dir from package_portable)
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if (-not $makensis) {
    # Common install locations
    $candidates = @("C:/Program Files (x86)/NSIS/makensis.exe","C:/Program Files/NSIS/makensis.exe")
    foreach ($c in $candidates) { if (Test-Path $c) { $makensis = $c; break } }
}
$staging = Join-Path $root (Join-Path $OutDir "staging/MViewer")
if (-not (Test-Path $staging)) { throw "staging dir missing: $staging (run package_portable first)" }

if ($makensis) {
    Write-Host "=== makensis (installer) ==="
    $viVersion = $identity.WindowsVersion
    $outFile = Join-Path $root (Join-Path $OutDir "MViewer-$Version-Setup.exe")
    New-Item -ItemType Directory -Force -Path (Split-Path $outFile) | Out-Null
    & $makensis /DAPP_DIR="$staging" /DVERSION="$Version" /DVI_VERSION="$viVersion" "/DOUTFILE=$outFile" (Join-Path $root "installer/MViewer.nsi")
    if ($LASTEXITCODE -ne 0) { throw "NSIS build failed" }
    $setup = Join-Path $root (Join-Path $OutDir "MViewer-$Version-Setup.exe")
    if (Test-Path $setup) { Write-Host "=== installer: $setup ($([math]::Round((Get-Item $setup).Length/1MB,1)) MB) ===" }
    if (Test-Path $setup) { [void](Sign-Artifact $setup) }
} else { throw "makensis not found -- Strict RC packaging requires the installer" }

# 3b) Review P0-4: debug symbols as a separate asset. The Release build writes
# linker PDBs for the shipping binaries (CMakeLists.txt mviewer_release_pdb).
$symbolNames = @('MViewer.pdb', 'mviewer_core.pdb')
$symbolFiles = @()
foreach ($name in $symbolNames) {
    $pdb = Join-Path $root (Join-Path 'build_msvc/bin' $name)
    if (-not (Test-Path -LiteralPath $pdb)) { throw "missing release PDB '$pdb' (Release must link with /DEBUG)" }
    $symbolFiles += $pdb
}
$symbolsZip = Join-Path $root (Join-Path $OutDir "MViewer-$Version-symbols.zip")
if (Test-Path -LiteralPath $symbolsZip) { Remove-Item -LiteralPath $symbolsZip -Force }
Compress-Archive -LiteralPath $symbolFiles -DestinationPath $symbolsZip -CompressionLevel Optimal
Write-Host "=== symbols: $symbolsZip ($([math]::Round((Get-Item $symbolsZip).Length/1MB,1)) MB) ==="

# 4) M14.8/M51: SHA256SUMS + auto release notes
Write-Host "=== release_manifest (M14.8) ==="
. (Join-Path $root "scripts/release_manifest.ps1") `
    -Version $Version -OutDir $OutDir -Strict
if (-not $?) { throw "release_manifest failed" }

Write-Host "=== strict release contract gate ==="
& $powerShellExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "scripts/release_contract_gate.ps1") `
    -Version $Version -ArtifactDir $OutDir -ManifestPath (Join-Path $root (Join-Path $OutDir 'SHA256SUMS.txt'))
if ($LASTEXITCODE -ne 0) { throw "strict release contract gate failed" }

Write-Host "=== release packaging complete (version $Version) ==="
Write-Host "=== signatures: $script:signNote ==="
if ($script:signCertPath -and (Test-Path -LiteralPath $script:signCertPath)) {
    Remove-Item -LiteralPath $script:signCertPath -Force
}
