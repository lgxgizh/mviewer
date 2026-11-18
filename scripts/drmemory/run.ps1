#Requires -Version 7.0
[CmdletBinding()]
param([string] $ToolRoot = '',
      [string] $Target = 'build_msvc/bin/core_tests.exe',
      [string] $EvidenceRoot = 'drmemory_out',
      [ValidateRange(10,600)][int] $TimeoutSeconds = 180,
      [string] $Package = '2.6.0')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot DrMemory.psm1) -Force
$evidence = [IO.Path]::GetFullPath($EvidenceRoot)
# Each invocation gets a fresh subtree: old reports can never satisfy detection.
$runRoot = Join-Path $evidence ([guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $runRoot -Force
$logs = Join-Path $runRoot logs
$null = New-Item -ItemType Directory -Path $logs -Force
$r = $null
$pdb = $null
$packageId = $null
$packageUrl = $null
$expectedSha256 = $null
$observedSha256 = $null
# A supplied ToolRoot is an external tree. Do not invent a catalog version for it.
$toolVersion = 'unidentified'
$toolVersionSource = 'unidentified'
$observedToolBanner = $null
$customToolRoot = [bool]$ToolRoot
$downloadedRoot = $null
try {
    if (-not $customToolRoot) {
        $spec = Resolve-DrMemoryPackage $Package
        $packageId = $spec.Id
        $packageUrl = $spec.Url
        $expectedSha256 = $spec.ExpectedSha256
        $ToolRoot = Join-Path ([IO.Path]::GetTempPath()) ('drmemory tool ' + [guid]::NewGuid().ToString('N'))
        $downloadedRoot = $ToolRoot
        $null = New-Item -ItemType Directory -Path $ToolRoot -Force
        $archive = Join-Path $runRoot 'drmemory.zip'
        # The URL comes from the catalog object, not from a composed shell string.
        Invoke-WebRequest -Uri $spec.Url -OutFile $archive -TimeoutSec 120
        try {
            $observedSha256 = Test-DrMemoryPackageHash -Path $archive -ExpectedSha256 $spec.ExpectedSha256
        } catch {
            Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
            throw
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $ToolRoot
        Remove-Item -LiteralPath $archive
        $null = Get-DrMemoryExpandedIdentity -Root $ToolRoot -ExpectedFolder $spec.ArchiveFolder
        $toolVersion = $spec.Id
        $toolVersionSource = 'catalog-folder'
    }
    $targetPath = (Resolve-Path -LiteralPath $Target).Path
    $pdb = [IO.Path]::ChangeExtension($targetPath, '.pdb')
    if (-not (Test-Path -LiteralPath $pdb -PathType Leaf)) {
        throw "Missing target linker PDB: $pdb. Build diagnostic target with MVIEWER_ALL_PDBS=ON"
    }
    $tool = Find-DrMemory -Root (Resolve-Path -LiteralPath $ToolRoot).Path -Target $targetPath
    $r = Invoke-DrMemoryProcess -Tool $tool -Target $targetPath -LogRoot $logs -EvidenceRoot $runRoot -TimeoutSeconds $TimeoutSeconds
} catch {
    if ($downloadedRoot) {
        Remove-Item -LiteralPath $downloadedRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    $r = [pscustomobject]@{
        state = 'infrastructure-error'
        executed = $false
        completed = $false
        memoryVerdict = 'unknown'
        launched = $false
        reports = @()
        reason = $_.ToString()
    }
    [IO.File]::WriteAllText((Join-Path $runRoot 'infrastructure-error.txt'), $_.ToString())
}
$stdoutFile = Join-Path $runRoot 'stdout.txt'
if (Test-Path -LiteralPath $stdoutFile) {
    $bannerMatch = [regex]::Match([IO.File]::ReadAllText($stdoutFile), 'Dr\. Memory version [^\r\n]+')
    if ($bannerMatch.Success) { $observedToolBanner = $bannerMatch.Value.Trim() }
}
# observedToolBanner is evidence only. It must not grant executed-clean.
if ($pdb) { $r | Add-Member -NotePropertyName targetPdb -NotePropertyValue $pdb }
$r | Add-Member -NotePropertyName packageId -NotePropertyValue $packageId
$r | Add-Member -NotePropertyName packageUrl -NotePropertyValue $packageUrl
$r | Add-Member -NotePropertyName expectedSha256 -NotePropertyValue $expectedSha256
$r | Add-Member -NotePropertyName observedSha256 -NotePropertyValue $observedSha256
$r | Add-Member -NotePropertyName toolVersion -NotePropertyValue $toolVersion
$r | Add-Member -NotePropertyName toolVersionSource -NotePropertyValue $toolVersionSource
$r | Add-Member -NotePropertyName observedToolBanner -NotePropertyValue $observedToolBanner
$r | Add-Member -NotePropertyName os -NotePropertyValue ([Runtime.InteropServices.RuntimeInformation]::OSDescription)
$r | Add-Member -NotePropertyName runnerArchitecture -NotePropertyValue ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString())
$r | Add-Member -NotePropertyName evidenceDirectory -NotePropertyValue $runRoot
$r | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'status.json') -Encoding utf8
$r | ConvertTo-Json -Depth 8 | Write-Host
$scopeLine = ''
if ([IO.Path]::GetFileName($Target) -eq 'test_result.exe') {
    $scopeLine = "- Scope: test_result is a Result/CRT control only. It is not core_tests, not a Qt/JIT qualification, and not a product memory-clean verdict.`n"
}
$shownPackage = $(if ($packageId) { $packageId } else { 'unidentified' })
$summary = @"
## Dr.Memory advisory: $($r.state)

- Actual launcher started: **$($r.launched)**
- Instrumented target evidence verified: **$($r.executed)**
- Complete target summaries verified: **$($r.completed)**
- Memory verdict: **$($r.memoryVerdict)**
- Package id: $shownPackage
- Tool version source: $toolVersionSource
$scopeLine- Reason: $($r.reason)
- Raw target reports, stdout/stderr and status.json are retained in the drmemory-evidence artifact.
- Unsupported, unavailable, timeout and no-report outcomes are **not passes**. This diagnostic remains non-blocking for release publication.
"@
if ($env:GITHUB_STEP_SUMMARY) { Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value $summary }
if ($env:GITHUB_OUTPUT) { Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "state=$($r.state)" }
# Only verified completed zero-error execution returns success. Job-level release
# best-effort policy does not convert this status.json/step error into "clean".
if ($r.state -ne 'executed-clean') {
    Write-Host "::error title=Dr.Memory $($r.state)::$($r.reason -replace '[\r\n]+',' ')"
    exit 1
}
