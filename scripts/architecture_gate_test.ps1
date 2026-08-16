# M46 — Architecture gate regression test.
#
# Proves BOTH directions of the gate contract:
#   1. a planted direct Repository include in a UI-layer TU is flagged (R2),
#      and a planted Compare-layer Repository include is flagged (R2 now covers
#      Compare — the stale imageviewer/preview loading-boundary exemption is
#      gone because the ImageLoading facade exists);
#   2. a planted Domain->Core include is flagged (R4);
#   3. the REAL tree reports zero architecture warnings — the gate's "0" means
#      the current architecture is actually enforced, not exempted.
#
# Usage: powershell -File scripts/architecture_gate_test.ps1 <repo-root>
# Exit code: 0 = pass, 1 = fail.

param(
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'

function Invoke-GateJson($repoRoot) {
    $out = Join-Path $env:TEMP ("arch_gate_out_" + [guid]::NewGuid().ToString('N') + '.json')
    try {
        & (Join-Path $PSScriptRoot 'architecture_gate.ps1') -Repo $repoRoot -Json -OutJson $out | Out-Null
        $json = Get-Content $out -Raw | ConvertFrom-Json
        return $json
    }
    finally {
        Remove-Item $out -ErrorAction SilentlyContinue
    }
}

$failures = 0
function Check($cond, $msg) {
    if ($cond) { Write-Host "  PASS: $msg" }
    else { Write-Host "  FAIL: $msg"; $script:failures++ }
}

# ── 1. Planted violations are flagged ───────────────────────────────────────
# The planted repo is placed at the REPO ROOT (not $env:TEMP): the gate scans
# only <repo>/src and deliberately excludes build trees — the hermetic CTest
# TEMP lives under build_msvc/, where a planted repo would be invisible.
$tmp = Join-Path $Repo (".arch_gate_tmp_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $tmp 'src') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $tmp 'src\domain') -Force | Out-Null
try {
    $uiViolation = @'
// planted UI-layer violation (R2)
#include "core/image/ImageRepository.h"
'@
    Set-Content -Path (Join-Path $tmp 'src\fake_ui_widget.cpp') -Value $uiViolation -Encoding UTF8

    $compareViolation = @'
// planted Compare-layer violation (R2 now covers Compare)
#include "core/image/ImageRepository.h"
'@
    Set-Content -Path (Join-Path $tmp 'src\fake_compare_workspace.cpp') -Value $compareViolation -Encoding UTF8

    $domainViolation = @'
// planted Domain-layer violation (R4)
#include "core/EventBus.h"
'@
    Set-Content -Path (Join-Path $tmp 'src\domain\fake_domain_type.h') -Value $domainViolation -Encoding UTF8

    # A legitimate UI file that loads through the facade must NOT be flagged.
    $cleanUi = @'
// legitimate: goes through the Application facade
#include "application/ImageLoadingService.h"
'@
    Set-Content -Path (Join-Path $tmp 'src\clean_ui_widget.cpp') -Value $cleanUi -Encoding UTF8

    $j = Invoke-GateJson $tmp
    $rules = @($j.violations | ForEach-Object { $_.rule })
    Check ($rules -contains 'R2' -and @($j.violations | Where-Object { $_.file -match 'fake_ui_widget' }).Count -eq 1) `
        'planted UI-layer Repository include is flagged (R2)'
    Check ($rules -contains 'R2' -and @($j.violations | Where-Object { $_.file -match 'fake_compare_workspace' }).Count -eq 1) `
        'planted Compare-layer Repository include is flagged (R2)'
    Check ($rules -contains 'R4') 'planted Domain->Core include is flagged (R4)'
    Check (@($j.violations | Where-Object { $_.file -match 'clean_ui_widget' }).Count -eq 0) `
        'facade-based UI include is NOT flagged'

    # ── 2. The real tree reports zero warnings ────────────────────────────
    $real = Invoke-GateJson $Repo
    Check ($real.warnings -eq 0) "real tree reports 0 architecture warnings (got $($real.warnings))"
}
finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

Write-Host "architecture_gate_test: $($failures) failure(s)"
exit $failures
