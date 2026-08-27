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

Write-Host "release_contract_tests: $(if ($failures -eq 0) { 'PASS' } else { 'FAIL' })"
exit $(if ($failures -eq 0) { 0 } else { 1 })
