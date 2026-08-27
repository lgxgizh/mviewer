<##
.SYNOPSIS
    Run the MViewer release checklist and write a machine-readable report.

.DESCRIPTION
    ReleaseCandidate is the strict Release Candidate mode. It always runs
    build, tests, selftest, performance gates, child-process crash smoke,
    packaging, manifest generation, and the post-package artifact contract.
    Missing items are failures in strict mode; WARN/SKIP cannot pass it.
#>
[CmdletBinding()]
param(
    [switch]$Package,
    [switch]$SkipBench,
    [string]$Steps = '',
    [switch]$ReleaseCandidate,
    [string]$Version = '',
    [string]$OutDir = 'dist'
)

$ErrorActionPreference = 'Stop'
$repo = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $repo
$strict = [bool]$ReleaseCandidate
$report = @()
$overall = $true

function Add-Result([string]$status, [string]$detail) {
    $color = @{ PASS = 'Green'; FAIL = 'Red'; WARN = 'Yellow'; SKIP = 'DarkGray' }[$status]
    Write-Host "[$status] $detail" -ForegroundColor $color
    $script:report += "| $status | $detail |"
    if ($status -eq 'FAIL') { $script:overall = $false }
}

function Heading([string]$number, [string]$name) {
    Write-Host "`n=== Step $number : $name ===" -ForegroundColor Cyan
}

function Wanted([string]$number) {
    if ($strict) { return $true }
    if ($Steps -eq '') { return $true }
    return $Steps.Split(',').Trim().Contains($number)
}

function Find-Binary([string]$name) {
    $candidates = @(
        (Join-Path $repo "bin\$name"),
        (Join-Path $repo "bin\Release\$name"),
        (Join-Path $repo "build_msvc\bin\$name"),
        (Join-Path $repo "build_msvc\bin\Release\$name")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Invoke-ProjectScript([string]$label, [string]$path, [string[]]$arguments) {
    try {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $path @arguments
        $code = $LASTEXITCODE
        if ($code -ne 0) { Add-Result FAIL "$label failed (exit $code)" }
        else { Add-Result PASS "$label passed" }
    } catch {
        Add-Result FAIL "$label failed: $_"
    }
}

if (-not $Version) {
    $versionFile = Join-Path $repo 'build_msvc/version_info.txt'
    if (Test-Path -LiteralPath $versionFile) {
        $versionLine = Get-Content -LiteralPath $versionFile | Where-Object { $_ -match '^MVIEWER_VERSION=' } | Select-Object -First 1
        if ($versionLine) { $Version = $versionLine.Substring('MVIEWER_VERSION='.Length).Trim() }
    }
    if (-not $Version) {
        $Version = (git describe --tags --always 2>$null)
        if ($Version) { $Version = $Version.TrimStart('v') }
    }
}

if (Wanted '1') {
    Heading '1' 'Release build'
    Invoke-ProjectScript 'build.ps1 Release' (Join-Path $repo 'build.ps1') @('Release')
}

if (Wanted '2') {
    Heading '2' 'CTest gate'
    Invoke-ProjectScript 'build.ps1 Test' (Join-Path $repo 'build.ps1') @('Test')
}

if (Wanted '3') {
    Heading '3' 'MViewer selftest'
    $viewer = Find-Binary 'MViewer.exe'
    if (-not $viewer) {
        if ($strict) { Add-Result FAIL 'MViewer.exe is missing' }
        else { Add-Result WARN 'MViewer.exe is missing' }
    } else {
        $oldQpa = $env:QT_QPA_PLATFORM
        $env:QT_QPA_PLATFORM = 'windows'
        try {
            $selfTest = Start-Process -FilePath $viewer -ArgumentList '--selftest' `
                -WorkingDirectory (Split-Path -Parent $viewer) -PassThru -Wait -WindowStyle Hidden
            $selfTest.Refresh()
            $selfTestCode = $selfTest.ExitCode
        } finally {
            if ($null -eq $oldQpa) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
            else { $env:QT_QPA_PLATFORM = $oldQpa }
        }
        if ($selfTestCode -ne 0) { Add-Result FAIL "MViewer --selftest failed (exit $selfTestCode)" }
        else { Add-Result PASS 'MViewer --selftest passed' }
    }
}

if (Wanted '4') {
    Heading '4' 'Performance gates'
    if ($SkipBench) {
        if ($strict) { Add-Result FAIL 'Strict RC does not allow -SkipBench' }
        else { Add-Result SKIP 'Benchmark gate skipped by -SkipBench' }
    } else {
        $bench = Find-Binary 'mviewer_bench.exe'
        if (-not $bench) {
            if ($strict) { Add-Result FAIL 'mviewer_bench.exe is missing' }
            else { Add-Result WARN 'mviewer_bench.exe is missing' }
        } else {
            & $bench --smoke
            $smokeCode = $LASTEXITCODE
            if ($smokeCode -ne 0) { Add-Result FAIL "mviewer_bench --smoke failed (exit $smokeCode)" }
            else { Add-Result PASS 'mviewer_bench --smoke passed' }
            & $bench --enforce
            $enforceCode = $LASTEXITCODE
            if ($enforceCode -ne 0) { Add-Result FAIL 'mviewer_bench --enforce failed' }
            else { Add-Result PASS 'mviewer_bench --enforce passed' }
        }
    }
}

if (Wanted '5') {
    Heading '5' 'Always-on crash diagnostics'
    $crashTest = Find-Binary 'test_crashhandler.exe'
    if (-not $crashTest) {
        if ($strict) { Add-Result FAIL 'test_crashhandler.exe is missing' }
        else { Add-Result WARN 'test_crashhandler.exe is missing' }
    } else {
        & $crashTest
        if ($LASTEXITCODE -ne 0) { Add-Result FAIL "crashhandler smoke failed (exit $LASTEXITCODE)" }
        else { Add-Result PASS 'always-on crash handler child-process smoke passed' }
    }
}

if (Wanted '6') {
    Heading '6' 'Strict package'
    $packageScript = Join-Path $repo 'scripts/package_release.ps1'
    if (-not (Test-Path -LiteralPath $packageScript)) {
        Add-Result FAIL 'package_release.ps1 is missing'
    } elseif (-not $Package -and -not $strict) {
        Add-Result SKIP 'Packaging is disabled; pass -Package'
    } else {
        Invoke-ProjectScript 'package_release.ps1' $packageScript @('-Version', $Version, '-OutDir', $OutDir)
    }
}

if (Wanted '7') {
    Heading '7' 'Release manifest'
    $manifestScript = Join-Path $repo 'scripts/release_manifest.ps1'
    if (-not (Test-Path -LiteralPath $manifestScript)) {
        Add-Result FAIL 'release_manifest.ps1 is missing'
    } else {
        $manifestArgs = @('-Version', $Version, '-OutDir', $OutDir)
        if ($strict) { $manifestArgs += '-Strict' }
        Invoke-ProjectScript 'release_manifest.ps1' $manifestScript $manifestArgs
    }
}

if (Wanted '8') {
    Heading '8' 'Strict artifact contract'
    $gateScript = Join-Path $repo 'scripts/release_contract_gate.ps1'
    if (-not $strict) {
        Add-Result SKIP 'Strict artifact contract is enabled by -ReleaseCandidate'
    } elseif (-not (Test-Path -LiteralPath $gateScript)) {
        Add-Result FAIL 'release_contract_gate.ps1 is missing'
    } else {
        Invoke-ProjectScript 'release_contract_gate.ps1' $gateScript @('-Version', $Version, '-ArtifactDir', $OutDir)
    }
}

$overallText = if ($overall) { 'PASS' } else { 'FAIL' }
$overallColor = if ($overall) { 'Green' } else { 'Red' }
$exitCode = if ($overall) { 0 } else { 1 }
$newline = [Environment]::NewLine
$md = "# MViewer Release Checklist Report"
$md += $newline + $newline + "Generated: " + (Get-Date -Format 'yyyy-MM-dd HH:mm')
$md += $newline + "Overall: " + $overallText
$md += $newline + $newline + "| Status | Step |" + $newline + "|--------|------|" + $newline
$md += ($report -join $newline)
$md += $newline + $newline + 'Strict RC mode requires package, installer, runtime, version, checksum, and packaged selftest to pass.'
$md | Out-File -FilePath (Join-Path $repo 'release_checklist_report.md') -Encoding utf8
Write-Host "`nReport written to release_checklist_report.md" -ForegroundColor Cyan
Write-Host "Overall: $overallText" -ForegroundColor $overallColor
exit $exitCode
