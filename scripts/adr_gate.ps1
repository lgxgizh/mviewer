# M23 Build-Health — ADR Gate.
#
# The reviewer's hardening rule: any PR that changes architecturally-significant
# code (Repository / Cache / Scheduler / Compare pipeline) MUST add or update an
# ADR in docs/adr/. This prevents architecture from silently rotting — six months
# later, the code explains WHY it is the way it is, not just what it is.
#
# Matching paths (architectural):
#   src/core/repository/*   src/core/cache/*   src/core/scheduler/*
#   src/compare/*
#
# Pass  : no architectural file changed, OR at least one docs/adr/* file changed.
# Fail  : architectural file changed but NO docs/adr/* file changed.
#
# Exit: 0 by default. With -Strict, exits 1 on failure (wired as a required
# `ci-gate` dependency in .github/workflows/ci.yml so it blocks merge).

[CmdletBinding()]
param(
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')),
    [switch]$Json,
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'

$archRe = 'src[\\/]core[\\/](repository|cache|scheduler)|src[\\/]compare[\\/]'

# Determine the changed file set for this event.
$files = @()
if ($env:GITHUB_EVENT_NAME -eq 'pull_request') {
    $base = $env:GITHUB_BASE_REF
    if ($base) { $files = @(git -C $Repo diff --name-only "origin/$base...HEAD" 2>$null) }
}
if ($files.Count -eq 0) {
    $files = @(git -C $Repo diff --name-only HEAD~1 HEAD 2>$null)
}

$archChanged = @($files | Where-Object { $_ -match $archRe })
$adrChanged = @($files | Where-Object { $_ -match 'docs[\\/]adr[\\/]' })

$passed = $true
$reason = ''
if ($archChanged.Count -gt 0 -and $adrChanged.Count -eq 0) {
    $passed = $false
    $reason = "Architectural change(s) without an ADR: $($archChanged -join ', ')"
}

if ($Json) {
    [ordered]@{
        gate         = 'adr'
        passed       = $passed
        archFiles    = @($archChanged)
        adrFiles     = @($adrChanged)
        reason       = $reason
    } | ConvertTo-Json -Depth 6 | Write-Output
}
else {
    Write-Host "=== ADR Gate ==="
    Write-Host "architectural files changed: $($archChanged.Count)"
    Write-Host "ADR files changed         : $($adrChanged.Count)"
    if (-not $passed) {
        Write-Host "  $reason"
        if ($Strict) { Write-Host "ADR-GATE: FAIL (add or update an ADR for this change)"; exit 1 }
    }
    Write-Host "ADR-GATE: OK"
}
