param(
    [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
$failures = 0
function Check([bool]$Condition, [string]$Message) {
    if ($Condition) { Write-Host "PASS: $Message" }
    else { Write-Host "FAIL: $Message"; $script:failures++ }
}

$nsi = Get-Content -LiteralPath (Join-Path $RepoRoot 'installer/mviewer.nsi') -Raw
Check ($nsi -match 'WriteRegStr HKCR "\.mvws"') '.mvws association is registered'
Check ($nsi -match 'WriteRegStr HKCR "\.mvproj"') '.mvproj association is registered'
Check ($nsi -match 'MViewer\.Workspace') 'workspace ProgID is present'
Check ($nsi -match 'MViewer\.Project') 'project ProgID is present'
Check ($nsi -match 'MViewer\.exe" "%1"') 'association command preserves quoted %1'
Check ($nsi -match '!error "VI_VERSION is required') 'installer has no silent version fallback'
Check ($nsi -match 'VIAddVersionKey "FileVersion" "\$\{VI_VERSION\}"') 'PE file version uses normalized identity'
Check ($nsi -notmatch 'WriteRegStr HKCR "\.mviewer"') 'obsolete .mviewer extension is not registered'

# Exercise the manifest writer with the exact strict artifact names. This is a
# small filesystem-only contract test and catches runner-specific path/encoding
# behavior before package_release reaches the strict gate.
$manifestRoot = Join-Path ([IO.Path]::GetTempPath()) ("mviewer-release_manifest_contract_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $manifestRoot | Out-Null
try {
    Set-Content -LiteralPath (Join-Path $manifestRoot 'MViewer-9.8.7-portable.zip') -Value 'zip-fixture' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $manifestRoot 'MViewer-9.8.7-Setup.exe') -Value 'installer-fixture' -Encoding utf8
    $fixturesExist = (Test-Path -LiteralPath (Join-Path $manifestRoot 'MViewer-9.8.7-portable.zip')) -and
        (Test-Path -LiteralPath (Join-Path $manifestRoot 'MViewer-9.8.7-Setup.exe'))
    Check $fixturesExist 'strict manifest fixture files are created'
    # CHANGELOG fixture: an empty [Unreleased] on top (as on master at tag
    # time) must NOT be picked; the exact [9.8.7] section must be.
    $fixtureChangelog = Join-Path $manifestRoot 'CHANGELOG.fixture.md'
    $fixtureText = "# Changelog`n`n## [Unreleased]`n`n## [9.8.7] - 2031-02-03`n`n### Fixed`n`n- contract-fixture-entry`n`n## [9.8.6] - 2031-01-01`n`n- older-entry`n"
    [IO.File]::WriteAllText($fixtureChangelog, $fixtureText, (New-Object System.Text.UTF8Encoding($false)))
    $manifestSucceeded = $false
    try {
        . (Join-Path $RepoRoot 'scripts/release_manifest.ps1') `
            -Version '9.8.7' -OutDir $manifestRoot -Strict -ChangelogPath $fixtureChangelog
        $manifestSucceeded = $?
    } catch {
        Write-Host "Manifest generation error: $($_.Exception.Message)"
    }
    Check $manifestSucceeded 'strict manifest generation exits successfully'
    $sums = Join-Path $manifestRoot 'SHA256SUMS.txt'
    Check (Test-Path -LiteralPath $sums) 'strict manifest writes SHA256SUMS.txt'
    if (Test-Path -LiteralPath $sums) {
        $sumText = Get-Content -LiteralPath $sums -Raw
        $artifactsListed = ($sumText -match 'MViewer-9\.8\.7-portable\.zip') -and
            ($sumText -match 'MViewer-9\.8\.7-Setup\.exe')
        Check $artifactsListed 'strict manifest lists both exact artifacts'
    }
    $notes = Join-Path $manifestRoot 'RELEASE_NOTES.md'
    Check (Test-Path -LiteralPath $notes) 'strict manifest writes RELEASE_NOTES.md'
    if (Test-Path -LiteralPath $notes) {
        $noteBytes = [IO.File]::ReadAllBytes($notes)
        $hasBom = ($noteBytes.Length -ge 3) -and ($noteBytes[0] -eq 0xEF) -and ($noteBytes[1] -eq 0xBB) -and ($noteBytes[2] -eq 0xBF)
        Check (-not $hasBom) 'release notes are UTF-8 without BOM'
        $noteText = [IO.File]::ReadAllText($notes)
        Check ($noteText -match '## \[9\.8\.7\] - 2031-02-03') 'release notes use the tagged version section'
        Check ($noteText -match 'contract-fixture-entry') 'release notes carry the version section body'
        Check ($noteText -notmatch 'Unreleased') 'release notes never use the [Unreleased] section'
        Check ($noteText -notmatch 'older-entry') 'release notes stop at the next version heading'
        Check ($noteText -notmatch 'Auto-generated') 'release notes have no build-date banner'
    }

    # Strict mode must fail when the tagged version has no CHANGELOG section.
    Set-Content -LiteralPath (Join-Path $manifestRoot 'MViewer-9.8.8-portable.zip') -Value 'zip-fixture' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $manifestRoot 'MViewer-9.8.8-Setup.exe') -Value 'installer-fixture' -Encoding utf8
    $missingFailed = $false
    try {
        . (Join-Path $RepoRoot 'scripts/release_manifest.ps1') `
            -Version '9.8.8' -OutDir $manifestRoot -ChangelogPath $fixtureChangelog -Strict
    } catch {
        $missingFailed = $_.Exception.Message -match '9\.8\.8'
    }
    Check $missingFailed 'strict manifest fails when the version has no CHANGELOG section'
} finally {
    if (Test-Path -LiteralPath $manifestRoot) {
        Remove-Item -LiteralPath $manifestRoot -Recurse -Force
    }
}

Write-Host "release_contract_tests: $(if ($failures -eq 0) { 'PASS' } else { 'FAIL' })"
exit $(if ($failures -eq 0) { 0 } else { 1 })
