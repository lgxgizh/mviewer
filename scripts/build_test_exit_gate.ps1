# build_test_exit_gate.ps1 -- verify that the canonical local test gate cannot
# report success after CTest fails.
param(
    [Parameter(Mandatory = $true)][string]$SourceDir
)

$ErrorActionPreference = 'Stop'
$buildScript = Join-Path $SourceDir 'build.ps1'
if (-not (Test-Path -LiteralPath $buildScript)) {
    Write-Error "Missing canonical build script: $buildScript"
    exit 1
}

$content = Get-Content -Raw -LiteralPath $buildScript
$contract = '(?s)ctest\s+--output-on-failure\s+--output-junit\s+test-results\.xml\s+"-j\$testJobs"\s*' +
            '\$testExitCode\s*=\s*\$LASTEXITCODE\s*' +
            'if\s*\(\s*\$testExitCode\s+-ne\s+0\s*\)\s*\{[^}]*' +
            'exit\s+\$testExitCode\s*\}'

if ($content -notmatch $contract) {
    Write-Error 'build.ps1 Test must capture the CTest exit code and return it unchanged on failure.'
    exit 1
}

Write-Host 'build_test_exit_gate: PASS (CTest failures propagate through build.ps1 Test)'
