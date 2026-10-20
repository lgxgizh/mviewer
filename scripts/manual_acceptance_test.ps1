# Self-test for the manual-acceptance wizard and checklist schema.
# Pattern matches scripts/release_version_test.ps1 / complexity_gate_test.ps1.
#
# Usage: pwsh -File scripts/manual_acceptance_test.ps1 [<repo-root>]
# Exit: 0 = pass, 1 = fail.

param(
    [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
$failures = 0

function Check([bool]$Condition, [string]$Message) {
    if ($Condition) {
        Write-Host "PASS: $Message"
    }
    else {
        Write-Host "FAIL: $Message"
        $script:failures++
    }
}

function Get-HostExecutable {
    try {
        return (Get-Process -Id $PID).Path
    }
    catch {
        return $null
    }
}

function Invoke-Wizard {
    param([string[]]$Arguments)
    $wizard = Join-Path $RepoRoot (Join-Path 'scripts' 'manual_acceptance.ps1')
    $hostExe = Get-HostExecutable
    if (-not $hostExe) { throw 'unable to resolve the current PowerShell host executable' }
    $output = & $hostExe -NoProfile -ExecutionPolicy Bypass -File $wizard @Arguments 2>&1
    return [pscustomobject]@{
        Code = $LASTEXITCODE
        Output = ($output | Out-String)
    }
}

function Read-JsonFile([string]$Path) {
    $encoding = New-Object System.Text.UTF8Encoding $false, $true
    $text = $encoding.GetString([System.IO.File]::ReadAllBytes($Path))
    return ($text | ConvertFrom-Json)
}

if (-not (Test-Path -LiteralPath $RepoRoot)) {
    Write-Host "FAIL: repo root not found: $RepoRoot"
    exit 1
}

$checklistPath = Join-Path $RepoRoot (Join-Path 'docs' (Join-Path 'acceptance' 'manual_acceptance_checklist.json'))
Check (Test-Path -LiteralPath $checklistPath) 'checklist JSON exists under docs/acceptance/'

$document = $null
try {
    $document = Read-JsonFile $checklistPath
    Check $true 'checklist JSON parses as UTF-8'
}
catch {
    Check $false "checklist JSON parses as UTF-8 ($($_.Exception.Message))"
}

$items = @()
if ($null -ne $document) {
    $schema = [string]$document.schema
    Check ($schema -eq 'manual-acceptance/v1') "schema is manual-acceptance/v1 (got '$schema')"
    $items = @($document.items)
    Check ($items.Count -gt 0) 'checklist contains at least one item'
}

$requiredIds = @(
    'M46-10', 'M46-11', 'M46-12', 'M46-13', 'M46-14', 'M46-15', 'M46-16',
    'M46-17', 'M46-19', 'M46-20', 'M46-22', 'M46-23', 'M46-26',
    'M51-install', 'M51-unc', 'M51-gpu',
    'M57-playback', 'M57-disposal', 'M57-tiff100mp',
    'M59-lut', 'M59-hdr',
    'M61-pointer', 'M61-contrast'
)
$idSet = @{}
$index = 0
foreach ($item in $items) {
    $index++
    $id = [string]$item.id
    Check (-not [string]::IsNullOrWhiteSpace($id)) "items[$index] has id"
    if ($id) {
        Check (-not $idSet.ContainsKey($id)) "id '$id' is unique"
        $idSet[$id] = $true
        Check (-not [string]::IsNullOrWhiteSpace([string]$item.title)) "$id has title"
        Check (-not [string]::IsNullOrWhiteSpace([string]$item.expected)) "$id has expected"
        Check (@($item.steps).Count -gt 0) "$id has steps"
        Check ($null -ne $item.prerequisites) "$id has prerequisites"
        Check ($null -ne $item.source) "$id has source"
        if ($null -ne $item.source) {
            $report = [string]$item.source.report
            Check (-not [string]::IsNullOrWhiteSpace($report)) "$id has source.report"
            Check ($null -ne $item.source.row -and -not [string]::IsNullOrWhiteSpace([string]$item.source.row)) "$id has source.row"
            if ($report) {
                $reportPath = Join-Path $RepoRoot (($report -replace '/', [IO.Path]::DirectorySeparatorChar))
                Check (Test-Path -LiteralPath $reportPath) "$id source report exists ($report)"
            }
        }
        foreach ($extra in @($item.also)) {
            $extraReport = [string]$extra.report
            if ([string]::IsNullOrWhiteSpace($extraReport)) { continue }
            $extraPath = Join-Path $RepoRoot (($extraReport -replace '/', [IO.Path]::DirectorySeparatorChar))
            Check (Test-Path -LiteralPath $extraPath) "$id also-report exists ($extraReport)"
        }
    }
}

foreach ($needed in $requiredIds) {
    Check ($idSet.ContainsKey($needed)) "required id '$needed' is present"
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('mviewer-manual-acceptance-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
try {
    $dry = Invoke-Wizard @(
        '-RepoRoot', $RepoRoot,
        '-Checklist', $checklistPath,
        '-DryRun',
        '-NonInteractive',
        '-OutDir', $tmp
    )
    Check ($dry.Code -eq 0) "wizard -DryRun -NonInteractive exits 0 (got $($dry.Code))"
    $resultsPath = Join-Path $tmp 'results.json'
    $reportPath = Join-Path $tmp 'report.md'
    $envPath = Join-Path $tmp 'environment.json'
    Check (Test-Path -LiteralPath $resultsPath) 'DryRun writes results.json'
    Check (Test-Path -LiteralPath $reportPath) 'DryRun writes report.md'
    Check (Test-Path -LiteralPath $envPath) 'DryRun writes environment.json'
    if (Test-Path -LiteralPath $resultsPath) {
        $results = Read-JsonFile $resultsPath
        Check ($results.schema -eq 'manual-acceptance-results/v1') 'results schema is manual-acceptance-results/v1'
        Check (@($results.items).Count -eq $items.Count) 'results contain every checklist item'
        Check ($results.dryRun -eq $true) 'results record dryRun=true'
    }
    if (Test-Path -LiteralPath $reportPath) {
        $md = [IO.File]::ReadAllText($reportPath)
        Check ($md -match '\| # \| Item \| Result \| Evidence \|') 'markdown report uses the review table header'
        Check ($md -match 'M46-10') 'markdown report lists M46-10'
    }

    $listRun = Invoke-Wizard @(
        '-RepoRoot', $RepoRoot,
        '-Checklist', $checklistPath,
        '-List'
    )
    Check ($listRun.Code -eq 0) "wizard -List exits 0 (got $($listRun.Code))"
    Check ($listRun.Output -match 'M46-10') 'wizard -List prints M46-10'
    Check ($listRun.Output -match 'M61-contrast') 'wizard -List prints M61-contrast'

    $onlyRun = Invoke-Wizard @(
        '-RepoRoot', $RepoRoot,
        '-Checklist', $checklistPath,
        '-Only', 'M46-16,M46-19',
        '-List'
    )
    Check ($onlyRun.Code -eq 0) 'wizard -Only -List exits 0'
    Check ($onlyRun.Output -match 'M46-16') 'wizard -Only includes M46-16'
    Check ($onlyRun.Output -match 'M46-19') 'wizard -Only includes M46-19'
    Check ($onlyRun.Output -notmatch 'M46-10') 'wizard -Only excludes M46-10'

    $badOnly = Invoke-Wizard @(
        '-RepoRoot', $RepoRoot,
        '-Checklist', $checklistPath,
        '-Only', 'not-a-real-id',
        '-DryRun',
        '-NonInteractive',
        '-OutDir', (Join-Path $tmp 'bad-only')
    )
    Check ($badOnly.Code -eq 2) "unknown -Only id exits 2 (got $($badOnly.Code))"

    $badJson = Join-Path $tmp 'bad.json'
    [IO.File]::WriteAllText($badJson, '{ "schema": "manual-acceptance/v1", "items": [ { "id": "x" } ] }')
    $badRun = Invoke-Wizard @(
        '-RepoRoot', $RepoRoot,
        '-Checklist', $badJson,
        '-DryRun',
        '-NonInteractive',
        '-OutDir', (Join-Path $tmp 'bad-out')
    )
    Check ($badRun.Code -eq 2) "invalid checklist schema exits 2 (got $($badRun.Code))"
}
finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "manual_acceptance_tests: $(if ($failures -eq 0) { 'PASS' } else { 'FAIL' })"
exit $(if ($failures -eq 0) { 0 } else { 1 })
