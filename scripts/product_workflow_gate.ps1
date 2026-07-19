# M13 Phase 1 — Product Workflow gate.
# Chains the existing per-phase workflow executables in the full user order:
#   Browse -> Compare -> Analyzer -> Export -> Workspace
# (Restart/Restore is exercised inside workspace_persist_tests.)
# Fails (exit 1) if ANY link returns non-zero, so no PR can silently break
# the product loop. Orchestration only — no test logic lives here.
param(
    [Parameter(Mandatory=$true)][string]$BinDir
)

$ErrorActionPreference = 'Continue'
$env:QT_QPA_PLATFORM = 'offscreen'

$chain = @(
    'product_browse_tests',
    'compare_workflow_tests',
    'analysis_panel_tests',
    'export_tests',
    'workspace_persist_tests'
)

$pass = 0; $fail = 0
foreach ($t in $chain) {
    $exe = Join-Path $BinDir "$t.exe"
    if (-not (Test-Path $exe)) {
        Write-Host "  MISSING: $exe (build first via build.ps1 Test)"
        $fail++
        continue
    }
    Write-Host "`n=== running $t ==="
    & $exe 2>&1 | Out-Host
    if ($LASTEXITCODE -eq 0) { Write-Host "  OK: $t"; $pass++ }
    else { Write-Host "  FAIL: $t (exit=$LASTEXITCODE)"; $fail++ }
}

Write-Host "`n=== PRODUCT WORKFLOW GATE: $pass passed, $fail failed ==="
if ($fail -eq 0) { exit 0 } else { exit 1 }
