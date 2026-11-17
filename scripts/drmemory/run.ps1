#Requires -Version 7.0
[CmdletBinding()]
param([string] $ToolRoot = '', [string] $Target = 'build_msvc/bin/core_tests.exe',
      [string] $EvidenceRoot = 'drmemory_out', [ValidateRange(10,600)][int] $TimeoutSeconds = 180)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot DrMemory.psm1) -Force
$evidence = [IO.Path]::GetFullPath($EvidenceRoot)
# Each invocation gets a fresh subtree: old reports can never satisfy detection.
$runRoot = Join-Path $evidence ([guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $runRoot -Force
$logs = Join-Path $runRoot logs
$null = New-Item -ItemType Directory -Path $logs -Force
$r = $null
try {
    if (-not $ToolRoot) {
        $ToolRoot = Join-Path ([IO.Path]::GetTempPath()) ('drmemory-tool-' + [guid]::NewGuid().ToString('N'))
        $null = New-Item -ItemType Directory -Path $ToolRoot -Force
        $archive = Join-Path $runRoot drmemory.zip
        Invoke-WebRequest -Uri 'https://github.com/DynamoRIO/drmemory/releases/download/release_2.6.0/DrMemory-Windows-2.6.0.zip' -OutFile $archive -TimeoutSec 120
        Expand-Archive -LiteralPath $archive -DestinationPath $ToolRoot
        Remove-Item -LiteralPath $archive
    }
    $targetPath = (Resolve-Path -LiteralPath $Target).Path
    $tool = Find-DrMemory -Root (Resolve-Path -LiteralPath $ToolRoot).Path -Target $targetPath
    $r = Invoke-DrMemoryProcess -Tool $tool -Target $targetPath -LogRoot $logs -EvidenceRoot $runRoot -TimeoutSeconds $TimeoutSeconds
} catch {
    $r = [pscustomobject]@{ state = 'infrastructure-error'; executed = $false; memoryVerdict = 'unknown'; launched = $false
        reports = @(); reason = $_.ToString() }
    [IO.File]::WriteAllText((Join-Path $runRoot infrastructure-error.txt), $_.ToString())
}
$r | Add-Member -NotePropertyName toolVersion -NotePropertyValue '2.6.0'
$r | Add-Member -NotePropertyName os -NotePropertyValue ([Runtime.InteropServices.RuntimeInformation]::OSDescription)
$r | Add-Member -NotePropertyName runnerArchitecture -NotePropertyValue ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString())
$r | Add-Member -NotePropertyName evidenceDirectory -NotePropertyValue $runRoot
$r | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot status.json) -Encoding utf8
$r | ConvertTo-Json -Depth 8 | Write-Host
$summary = @"
## Dr.Memory advisory: $($r.state)

- Actual launcher started: **$($r.launched)**
- Complete instrumented execution verified: **$($r.executed)**
- Memory verdict: **$($r.memoryVerdict)**
- Reason: $($r.reason)
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
