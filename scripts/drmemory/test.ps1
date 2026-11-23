#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot DrMemory.psm1) -Force
$root = Join-Path ([IO.Path]::GetTempPath()) ('drmemory contract spaces ' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$script:checks = 0
function Check([bool] $Ok, [string] $Message) {
    if (-not $Ok) { throw "FAIL: $Message" }; $script:checks++; Write-Host "PASS: $Message"
}
function Throws([scriptblock] $Body, [string] $Message) {
    $threw = $false; try { & $Body } catch { $threw = $true }; Check $threw $Message
}
function FakePE([string] $Path, [uint16] $Machine) {
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force
    $b = [byte[]]::new(256)
    [BitConverter]::GetBytes([uint16]0x5a4d).CopyTo($b, 0)
    [BitConverter]::GetBytes([int]128).CopyTo($b, 60)
    [BitConverter]::GetBytes([uint32]0x4550).CopyTo($b, 128)
    [BitConverter]::GetBytes($Machine).CopyTo($b, 132)
    [IO.File]::WriteAllBytes($Path, $b)
}
try {
    $toolRoot = Join-Path $root 'tool archive'; $target = Join-Path $root 'target space/core_tests.exe'
    $x64 = Join-Path $toolRoot 'DrMemory-Windows-2.6.0/bin64/drmemory.exe'
    $x86 = Join-Path $toolRoot 'DrMemory-Windows-2.6.0/bin/drmemory.exe'
    FakePE $target 0x8664; FakePE $x64 0x8664; FakePE $x86 0x014c
    Check ((Find-DrMemory $toolRoot $target) -eq $x64) 'actual archive bin64 selected for x64 target, spaces retained'
    FakePE $target 0x014c
    Check ((Find-DrMemory $toolRoot $target) -eq $x86) 'actual archive bin selected for x86 target'
    FakePE $target 0xaa64
    Throws { Find-DrMemory $toolRoot $target } 'unsupported architecture is explicit'
    FakePE $target 0x8664
    FakePE (Join-Path $toolRoot 'duplicate/bin64/drmemory.exe') 0x8664
    Throws { Find-DrMemory $toolRoot $target } 'ambiguous archive discovery rejected'
    Remove-Item -LiteralPath (Join-Path $toolRoot duplicate) -Recurse
    Remove-Item -LiteralPath $x64
    Throws { Find-DrMemory $toolRoot $target } 'x64 target never falls back silently to x86 launcher'
    $logs = Join-Path $root logs; $null = New-Item -ItemType Directory -Path $logs
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'no-report') 'zero exit without report is not pass'
    Check ((Get-DrMemoryResult $logs core_tests.exe 1).state -eq 'tool-error') 'nonzero without report is tool error'
    Check ((Get-DrMemoryResult -LogRoot $logs -TargetName core_tests.exe -ExitCode 0 -TimedOut $true).state -eq 'timeout') 'timeout never passes'
    Check ((Get-DrMemoryResult -LogRoot $logs -TargetName core_tests.exe -ExitCode 0 -LaunchError 'missing binary').state -eq 'infrastructure-error') 'launch failure never passes'
    Check ((Get-DrMemoryResult -LogRoot $logs -TargetName core_tests.exe -ExitCode 1 -Output 'System call information is missing').state -eq 'unsupported') 'OS instrumentation incompatibility distinguished'
    $dir = Join-Path $logs 'DrMemory-unrelated.exe.123.000'; $null = New-Item -ItemType Directory -Path $dir
    Set-Content (Join-Path $dir results.txt) 'not a target report'
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'no-report') 'unrelated reports cannot certify target'
    $dir = Join-Path $logs 'DrMemory-core_tests.exe.123.000'; $null = New-Item -ItemType Directory -Path $dir
    $report = Join-Path $dir results.txt
    [IO.File]::WriteAllText($report, '')
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'no-report') 'empty target report never passes'
    Set-Content $report 'partial results'
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'incomplete-report') 'partial report never passes'
    $zero = "FINAL SUMMARY:`nNO ERRORS FOUND:`n 0 unique, 0 total unaddressable access(es)`n 0 unique, 0 total uninitialized access(es)`n 0 unique, 0 total invalid heap argument(s)`nDetails: fixture/results.txt`n"
    Set-Content $report ($zero -replace 'Details:[^\r\n]*','')
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'incomplete-report') 'truncated final summary without report footer is not clean'
    Set-Content $report $zero
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'executed-clean') 'real final zero-count format recognized'
    Check ((Get-DrMemoryResult $logs core_tests.exe 2).state -eq 'target-or-tool-error') 'target failure cannot become clean'
    Set-Content $report ($zero + "ERRORS IGNORED:`n 2 potential error(s) (suspected false positives)`n")
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'executed-potential') 'potential findings require review, not clean claim'
    Set-Content $report ($zero.Replace('NO ERRORS FOUND:', 'ERRORS FOUND:').Replace('0 unique, 0 total unaddressable', '1 unique, 2 total unaddressable'))
    Check ((Get-DrMemoryResult $logs core_tests.exe 86).state -eq 'executed-errors') 'real memory counts propagated'
    Set-Content $report "Error #1: UNADDRESSABLE ACCESS`npartial summary"
    Check ((Get-DrMemoryResult $logs core_tests.exe 86).state -eq 'executed-errors') 'memory finding retained even when summary incomplete'
    $timeoutResult = Get-DrMemoryResult -LogRoot $logs -TargetName core_tests.exe -ExitCode -1 -TimedOut $true
    Check ($timeoutResult.state -eq 'timeout' -and $timeoutResult.memoryVerdict -eq 'errors-observed' -and $timeoutResult.executed -and -not $timeoutResult.completed) 'timeout preserves observed findings without claiming completed run'
    Set-Content $report $zero
    $partialDir = Join-Path $logs 'DrMemory-core_tests.exe.456.000'
    $null = New-Item -ItemType Directory -Path $partialDir
    Set-Content (Join-Path $partialDir results.txt) 'partial child report'
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'incomplete-report') 'one complete report cannot hide an incomplete child report'
    Set-Content (Join-Path $partialDir results.txt) 'System call information is missing for this operating system: Restarting to trigger auto-generation of system call information.'
    Check ((Get-DrMemoryResult $logs core_tests.exe 0).state -eq 'executed-clean') 'bootstrap restart retained but not confused with final target report'
    $repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $toolingWorkflow = [IO.File]::ReadAllText((Join-Path $repoRoot '.github/workflows/drmemory.yml'))
    $releaseWorkflow = [IO.File]::ReadAllText((Join-Path $repoRoot '.github/workflows/release.yml'))
    $toolingJob = [regex]::Match($toolingWorkflow, '(?ms)^  actual-execution:.*?(?=^  [a-z][a-z0-9-]*:|\z)').Value
    $releaseJob = [regex]::Match($releaseWorkflow, '(?ms)^  dr-memory:.*?(?=^  [a-z][a-z0-9-]*:|\z)').Value
    Check ($toolingJob -match '(?m)^    if: \$\{\{ false \}\}\r?$') 'automatic tooling Dr.Memory job explicitly disabled'
    Check ($toolingJob -match '(?m)^    name: Dr\.Memory DISABLED \(not executed; no memory verdict\)\r?$') 'tooling skipped job explicitly states no execution or memory verdict'
    Check ($releaseJob -match '(?m)^    if: \$\{\{ false \}\}\r?$') 'automatic Release Dr.Memory job explicitly disabled'
    Check ($releaseJob -match '(?m)^    name: Dr\.Memory DISABLED \(not executed; no memory verdict\)\r?$') 'Release skipped job explicitly states no execution or memory verdict'
    Write-Host "Dr.Memory contracts: $script:checks passed"
} finally { Remove-Item -LiteralPath $root -Recurse -Force }
