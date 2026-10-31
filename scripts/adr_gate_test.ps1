# ADR gate regression.
#
# Proves the protected-path set covers ImageRepository and src/core/compare/
# (as well as the older cache/scheduler/repository/compare
# paths), that an ADR in the same diff passes, that an unrelated path passes,
# and that a failed git diff is a non-zero gate result instead of a clean pass.
#
# Usage: powershell -File scripts/adr_gate_test.ps1
# Exit code: 0 = pass, 1 = fail.

param(
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$failures = 0
function Check($cond, $msg) {
    if ($cond) { Write-Host "  PASS: $msg" }
    else { Write-Host "  FAIL: $msg"; $script:failures++ }
}

$savedEvent = $env:GITHUB_EVENT_NAME
$savedBase = $env:GITHUB_BASE_REF

function Invoke-AdrStrict([string]$dir) {
    $gate = Join-Path $PSScriptRoot 'adr_gate.ps1'
    $exe = (Get-Process -Id $PID).Path
    & $exe -NoProfile -ExecutionPolicy Bypass -File $gate -Repo $dir -Strict | Out-Host
    $script:adrExit = $LASTEXITCODE
}

function New-FixtureRepo {
    $root = if ($env:TEMP) { $env:TEMP } else { [System.IO.Path]::GetTempPath() }
    $dir = Join-Path $root ("adr_gate_" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    & git -C $dir init -q
    if ($LASTEXITCODE -ne 0) { throw "git init failed in $dir" }
    & git -C $dir config user.email "adr-gate@test"
    & git -C $dir config user.name "adr-gate"
    Set-Content -Path (Join-Path $dir 'README.md') -Value "base" -Encoding ascii
    & git -C $dir add -- README.md
    & git -C $dir commit -q -m "base"
    if ($LASTEXITCODE -ne 0) { throw "base commit failed in $dir" }
    return $dir
}

function Add-RepoFile([string]$dir, [string]$rel, [string]$content) {
    $full = Join-Path $dir ($rel -replace '/', [System.IO.Path]::DirectorySeparatorChar)
    $parent = Split-Path -Parent $full
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Set-Content -Path $full -Value $content -Encoding ascii
    & git -C $dir add -- $rel
    if ($LASTEXITCODE -ne 0) { throw "git add failed for $rel" }
}

function Commit-All([string]$dir, [string]$message) {
    & git -C $dir commit -q -m $message
    if ($LASTEXITCODE -ne 0) { throw "git commit failed: $message" }
}

function Set-PullRequestBase([string]$dir) {
    $base = (& git -C $dir rev-parse 'HEAD~1').Trim()
    & git -C $dir update-ref refs/remotes/origin/master $base
    if ($LASTEXITCODE -ne 0) { throw "update-ref failed" }
    $env:GITHUB_EVENT_NAME = 'pull_request'
    $env:GITHUB_BASE_REF = 'master'
}

$dirs = @()
try {
    # Protected paths must fail without an ADR, under a pull_request three-dot diff.
    $protected = @(
        'src/core/image/ImageRepository.cpp',
        'src/core/image/ImageRepository.h',
        'src/core/image/ImageRepository_load.cpp',
        'src/core/compare/CompareEngine.cpp',
        'src/core/cache/CacheManager.cpp',
        'src/core/scheduler/TaskScheduler.cpp',
        'src/core/repository/Repository.cpp',
        'src/compare/CompareEngine.cpp'
    )
    foreach ($rel in $protected) {
        $dir = New-FixtureRepo
        $dirs += $dir
        Add-RepoFile $dir $rel 'change'
        Commit-All $dir 'arch change'
        Set-PullRequestBase $dir
        Invoke-AdrStrict $dir
        Check ($script:adrExit -ne 0) "protected path fails without an ADR: $rel"
    }

    # The compare UI layer is not ADR-protected (architecture_gate R3 covers it).
    $dir = New-FixtureRepo
    $dirs += $dir
    Add-RepoFile $dir 'src/compareworkspace_keyboard.cpp' 'change'
    Commit-All $dir 'compare ui change'
    Set-PullRequestBase $dir
    Invoke-AdrStrict $dir
    Check ($script:adrExit -eq 0) 'compareworkspace UI change passes without an ADR'

    # Same ImageRepository change plus an ADR passes.
    $dir = New-FixtureRepo
    $dirs += $dir
    Add-RepoFile $dir 'src/core/image/ImageRepository_async.cpp' 'change'
    Add-RepoFile $dir 'docs/adr/999-test.md' 'why'
    Commit-All $dir 'arch plus adr'
    Set-PullRequestBase $dir
    Invoke-AdrStrict $dir
    Check ($script:adrExit -eq 0) 'ImageRepository change with an ADR passes'

    # This PR's shape: scripts and workflows, no protected path.
    $dir = New-FixtureRepo
    $dirs += $dir
    Add-RepoFile $dir 'scripts/note.txt' 'not architecture'
    Add-RepoFile $dir '.github/workflows/ci.yml' 'name: test'
    Commit-All $dir 'ci only'
    Set-PullRequestBase $dir
    Invoke-AdrStrict $dir
    Check ($script:adrExit -eq 0) 'non-protected paths (scripts, workflows) pass'

    # Nearby image file that is not ImageRepository stays unprotected.
    $dir = New-FixtureRepo
    $dirs += $dir
    Add-RepoFile $dir 'src/core/image/Decoder.cpp' 'change'
    Commit-All $dir 'decoder'
    Set-PullRequestBase $dir
    Invoke-AdrStrict $dir
    Check ($script:adrExit -eq 0) 'src/core/image/Decoder.cpp is not a protected path'

    # Failed three-dot diff must not fall through to a clean HEAD~1 and pass.
    $dir = New-FixtureRepo
    $dirs += $dir
    Add-RepoFile $dir 'README.md' 'still not architecture'
    Commit-All $dir 'readme only'
    $env:GITHUB_EVENT_NAME = 'pull_request'
    $env:GITHUB_BASE_REF = 'no-such-base'
    Invoke-AdrStrict $dir
    Check ($script:adrExit -ne 0) 'failed git diff is a gate failure'
}
finally {
    $env:GITHUB_EVENT_NAME = $savedEvent
    $env:GITHUB_BASE_REF = $savedBase
    foreach ($d in $dirs) {
        Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "adr_gate_test: $failures failure(s)"
exit $failures
