<#
.SYNOPSIS
  M14.8 - Generate SHA256SUMS + release notes for a packaged MViewer release.

.DESCRIPTION
  Scans dist/ for shipping artifacts (portable zip, Setup.exe) and writes:
    dist/SHA256SUMS.txt          - standard "hash  filename" lines
    dist/RELEASE_NOTES.md        - auto-extracted section from CHANGELOG.md

  Safe to re-run; overwrites previous outputs. Does not modify CI workflows
  (AGENTS.md freezes release.yml); call this from package_release.ps1 or by hand.

.PARAMETER Version
  Version string used to pick the CHANGELOG section (e.g. "1.0.3"). Defaults to
  git describe, then "0.0.0-dev".

.PARAMETER OutDir
  Directory containing the release artifacts (default: dist).

.PARAMETER Strict
  Require the exact versioned portable ZIP and NSIS installer. Missing files
  are hard failures instead of an empty/warning-only manifest.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts/release_manifest.ps1 -Version 1.0.3
#>
param(
    [string]$Version = "",
    [string]$OutDir  = "dist",
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'release_version.ps1')

if (-not $Version) {
    $Version = (git describe --tags --always 2>$null)
    if (-not $Version) { $Version = "0.0.0-dev" }
    $Version = $Version.TrimStart("v")
}
if ($Strict) {
    $identity = Get-ReleaseVersionIdentity $Version
    $Version = $identity.Version
}

$outAbs = if ([IO.Path]::IsPathRooted($OutDir)) {
    [IO.Path]::GetFullPath($OutDir)
} else {
    [IO.Path]::GetFullPath((Join-Path $root $OutDir))
}
if (-not (Test-Path $outAbs)) {
    New-Item -ItemType Directory -Force -Path $outAbs | Out-Null
}

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][string]$Path)

    # Use .NET instead of Get-FileHash: the latter is not available in every
    # Windows PowerShell module environment used by CTest/GitHub runners.
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try {
        $hash = $sha256.ComputeHash($stream)
        return ([BitConverter]::ToString($hash).Replace('-', '')).ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
}

# -- 1) SHA256SUMS for shipping artifacts -------------------------------------
# Keep artifact discovery path-based. PowerShell's FileInfo pipeline behavior
# differs between Windows PowerShell hosts used locally and on CI.
$artifactPaths = @()
foreach ($name in @("MViewer-$Version-portable.zip", "MViewer-$Version-Setup.exe")) {
    $candidate = Join-Path $outAbs $name
    $exists = [IO.File]::Exists($candidate) -or (Test-Path -LiteralPath $candidate)
    if ($exists) {
        $artifactPaths += $candidate
    } elseif ($Strict) {
        throw "Strict release manifest: missing exact artifact '$candidate'"
    }
}
if (-not $Strict) {
    foreach ($pat in @("MViewer-*-portable.zip", "MViewer-*-Setup.exe", "MViewer-*.zip", "MViewer-*.exe")) {
        try {
            $artifactPaths += [IO.Directory]::GetFiles($outAbs, $pat)
        } catch [IO.DirectoryNotFoundException] {
            # The output directory is created above, but keep non-strict mode
            # warning-only if a provider removes it between operations.
        }
    }
}
# De-dup by full path. Keep the result as an array even when a caller supplies
# only one artifact; Windows PowerShell otherwise unwraps a pipeline result.
$artifactPaths = @($artifactPaths | Sort-Object -Unique)
$artifactCount = @($artifactPaths).Count

$sumsPath = Join-Path $outAbs "SHA256SUMS.txt"
if ($artifactCount -gt 0) {
    $lines = @()
    $lines += "# MViewer $Version - SHA256 checksums"
    $lines += "# Generated $(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')"
    foreach ($artifactPath in $artifactPaths) {
        $hash = Get-Sha256Hex -Path $artifactPath
        $fileName = [IO.Path]::GetFileName($artifactPath)
        # GNU coreutils style: "<hash>  <filename>" (two spaces)
        $lines += "$hash  $fileName"
        Write-Host ("  [sha256] {0}  {1}" -f $hash.Substring(0,12), $fileName)
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $manifestText = ($lines -join [Environment]::NewLine) + [Environment]::NewLine
    [IO.File]::WriteAllText($sumsPath, $manifestText, $utf8NoBom)
    if (-not (Test-Path -LiteralPath $sumsPath)) {
        throw "Release manifest failed to create checksum file '$sumsPath'"
    }
    Write-Host "=== SHA256SUMS: $sumsPath ($artifactCount file(s)) ==="
} elseif ($Strict) {
    throw "Strict release manifest: no release artifacts found under $outAbs"
} else {
    Write-Warning "No release artifacts found under $outAbs - writing empty SHA256SUMS."
    Set-Content -Path $sumsPath -Value "# No artifacts found for version $Version" -Encoding utf8
}

# -- 2) RELEASE_NOTES.md from CHANGELOG.md ------------------------------------
$changelog = Join-Path $root "CHANGELOG.md"
$notesPath = Join-Path $outAbs "RELEASE_NOTES.md"

if (-not (Test-Path $changelog)) {
    Write-Warning "CHANGELOG.md not found; skipping RELEASE_NOTES.md"
    return
}

$raw = Get-Content -Path $changelog -Raw -Encoding utf8
# Extract the first ## [X.Y.Z] section (or Unreleased) as the release notes body.
# Match from the first version heading up to (but not including) the next ## heading.
$section = $null
if ($raw -match '(?ms)^## \[([^\]]+)\][^\n]*\n(.*?)(?=^## |\z)') {
    $section = $Matches[0].TrimEnd()
}

$header = @"
# MViewer $Version

Auto-generated from CHANGELOG.md on $(Get-Date -Format 'yyyy-MM-dd').

"@

if ($section) {
    Set-Content -Path $notesPath -Value ($header + $section) -Encoding utf8
    Write-Host "=== RELEASE_NOTES: $notesPath ==="
} else {
    Set-Content -Path $notesPath -Value ($header + "_No CHANGELOG section found._") -Encoding utf8
    Write-Warning "Could not parse a version section from CHANGELOG.md"
}

Write-Host "=== release_manifest complete (version $Version) ==="
