# M23 Build-Health — ADR Gate.
#
# The reviewer's hardening rule: any PR that changes architecturally-significant
# code (Repository / Cache / Scheduler / Compare pipeline) MUST add or update an
# ADR in docs/adr/. This prevents architecture from silently rotting — six months
# later, the code explains WHY it is the way it is, not just what it is.
#
# Matching paths (architectural):
#   src/core/repository/*   src/core/cache/*   src/core/scheduler/*
#   src/core/image/ImageRepository*   src/core/compare/*
#   src/compare/*
# (src/compareworkspace* is the compare UI layer, edited by most feature
# PRs; it is guarded by architecture_gate R3, not by the ADR gate.)
#
# Pass  : no architectural file changed, OR at least one docs/adr/* file changed.
# Fail  : architectural file changed but NO docs/adr/* file changed.
#         A git diff that exits non-zero is also a failure (an unknown change
#         set must not pass). On pull_request that does not fall through to
#         HEAD~1.
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
# A missing revision writes to stderr. Keep that a captured exit code, not a
# terminating error that skips the failure path below (PS 7 native-command pref).
$PSNativeCommandUseErrorActionPreference = $false

$archRe = 'src[\\/]core[\\/](repository|cache|scheduler)|src[\\/]compare[\\/]|' +
    'src[\\/]core[\\/]image[\\/]ImageRepository|src[\\/]core[\\/]compare[\\/]'

function Get-GitDiffNames([string]$Range) {
    # stderr is part of the output stream so a missing ref cannot be mistaken
    # for an empty (clean) diff. Only real path lines count as files.
    # A range such as 'HEAD~1 HEAD' is two revisions; pass them as separate
    # arguments (one quoted string made git reject the push-event diff).
    $revs = @($Range -split '\s+' | Where-Object { $_ })
    $output = @(git -C $Repo diff --name-only @revs 2>&1)
    $code = $LASTEXITCODE
    $names = @($output | Where-Object {
        $_ -is [string] -and -not [string]::IsNullOrWhiteSpace($_)
    })
    return @{ Ok = ($code -eq 0); Files = $names }
}

# Determine the changed file set for this event. A non-zero git diff is a
# gate failure: do not treat "no names" as "nothing architectural changed".
$files = @()
$diffFailed = $false
if ($env:GITHUB_EVENT_NAME -eq 'pull_request') {
    $base = $env:GITHUB_BASE_REF
    if ([string]::IsNullOrWhiteSpace($base)) {
        $diffFailed = $true
    }
    else {
        $prDiff = Get-GitDiffNames "origin/$base...HEAD"
        if (-not $prDiff.Ok) {
            $diffFailed = $true
        }
        else {
            $files = @($prDiff.Files)
        }
    }
}
# Successful empty pull_request diff still consults the tip commit, matching
# the previous fallback. A failed diff does not.
if (-not $diffFailed -and $files.Count -eq 0) {
    $tip = Get-GitDiffNames 'HEAD~1 HEAD'
    if (-not $tip.Ok) {
        $diffFailed = $true
    }
    else {
        $files = @($tip.Files)
    }
}

$archChanged = @($files | Where-Object { $_ -match $archRe })
$adrChanged = @($files | Where-Object { $_ -match 'docs[\\/]adr[\\/]' })

$passed = $true
$reason = ''
if ($diffFailed) {
    $passed = $false
    $reason = 'git diff failed; refusing to treat an unknown change set as clean'
}
elseif ($archChanged.Count -gt 0 -and $adrChanged.Count -eq 0) {
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
        if ($Strict) {
            if ($diffFailed) { Write-Host 'ADR-GATE: FAIL (git diff failed)'; exit 1 }
            Write-Host 'ADR-GATE: FAIL (add or update an ADR for this change)'
            exit 1
        }
    }
    Write-Host "ADR-GATE: OK"
}
