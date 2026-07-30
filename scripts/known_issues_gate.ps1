# M23 Build-Health — Known Issues (Bug) Gate.
#
# Implements the reviewer's "Bug Gate": every tracked Issue MUST be linked to a
# committed regression test (or a CI job that reproduces it). When an Issue is
# still open, its `regression_tests` entries must point at files that actually
# exist in the tree — otherwise the gate fails. This guarantees that fixing a
# defect also locks in a regression test, so the defect can never silently
# return.
#
# The DB lives at docs/known_issues/known_issues.json (schema known-issues/v1):
#   { "issues": [ { id, title, component, status, root_cause, fix_milestone,
#                   regression_tests: [path ...] } ] }
#
# `status`:
#   * "resolved"      — fixed; regression documented (skipped by the gate)
#   * "open"          — still broken; MUST have a linked regression test
#   * "investigating" — root cause unknown; MUST have a repro/regression test
#
# Exit: 0 by default. With -Strict, exits 1 if any open/investigating issue
# lacks a linked, existing regression test — turning the gate into a merge
# blocker (wired as such in .github/workflows/ci.yml).

[CmdletBinding()]
param(
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')),
    [string]$DbPath = (Join-Path $Repo 'docs/known_issues/known_issues.json'),
    [switch]$Json,
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $DbPath)) {
    Write-Host "known_issues_gate: no DB at $DbPath - nothing to enforce"
    exit 0
}

try { $data = Get-Content $DbPath -Raw -Encoding UTF8 | ConvertFrom-Json }
catch { Write-Host "known_issues_gate: invalid JSON in $DbPath"; exit 1 }

$open = [System.Collections.Generic.List[string]]::new()
$unlinked = [System.Collections.Generic.List[string]]::new()

foreach ($iss in $data.issues) {
    if ($iss.status -eq 'resolved') { continue }
    $id = [string]$iss.id
    $open.Add($id)
    $tests = @($iss.regression_tests)
    if ($tests.Count -eq 0) { $unlinked.Add("$id (no regression_tests)"); continue }
    foreach ($t in $tests) {
        if (-not (Test-Path (Join-Path $Repo $t))) {
            $unlinked.Add("$id -> $t (missing)")
            break
        }
    }
}

$passed = ($unlinked.Count -eq 0)

if ($Json) {
    [ordered]@{
        gate           = 'known-issues'
        passed         = $passed
        totalIssues    = $data.issues.Count
        openIssues     = $open.Count
        unlinkedIssues = @($unlinked)
    } | ConvertTo-Json -Depth 6 | Write-Output
}
else {
    Write-Host "=== Known Issues (Bug Gate) ==="
    Write-Host "total issues : $($data.issues.Count)"
    Write-Host "open         : $($open.Count)"
    Write-Host "unlinked     : $($unlinked.Count)"
    foreach ($u in $unlinked) { Write-Host "  UNLINKED: $u" }
    if ($Strict -and -not $passed) {
        Write-Host "KNOWN-ISSUES: FAIL (open issue without a regression test)"
        exit 1
    }
    Write-Host "KNOWN-ISSUES: OK"
}
