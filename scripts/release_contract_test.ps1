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
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'scripts/release_manifest.ps1') `
        -Version '9.8.7' -OutDir $manifestRoot -Strict
    Check ($LASTEXITCODE -eq 0) 'strict manifest generation exits successfully'
    $sums = Join-Path $manifestRoot 'SHA256SUMS.txt'
    Check (Test-Path -LiteralPath $sums) 'strict manifest writes SHA256SUMS.txt'
    if (Test-Path -LiteralPath $sums) {
        $sumText = Get-Content -LiteralPath $sums -Raw
        $artifactsListed = ($sumText -match 'MViewer-9\.8\.7-portable\.zip') -and
            ($sumText -match 'MViewer-9\.8\.7-Setup\.exe')
        Check $artifactsListed 'strict manifest lists both exact artifacts'
    }
} finally {
    if (Test-Path -LiteralPath $manifestRoot) {
        Remove-Item -LiteralPath $manifestRoot -Recurse -Force
    }
}

Write-Host "release_contract_tests: $(if ($failures -eq 0) { 'PASS' } else { 'FAIL' })"
exit $(if ($failures -eq 0) { 0 } else { 1 })
