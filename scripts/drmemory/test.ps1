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
    Check ($timeoutResult.memoryVerdict -ne 'clean-reported-categories' -and $timeoutResult.state -ne 'executed-clean') 'timed-out run is never memory clean'
    $oldPkg = Resolve-DrMemoryPackage '2.6.0'
    $newPkg = Resolve-DrMemoryPackage '2.6.20434'
    Check ($oldPkg.Url -eq 'https://github.com/DynamoRIO/drmemory/releases/download/release_2.6.0/DrMemory-Windows-2.6.0.zip') '2.6.0 stays pinned to its release URL'
    Check ($null -eq $oldPkg.ExpectedSha256) '2.6.0 has no invented checksum'
    Check ($newPkg.Url -eq 'https://github.com/DynamoRIO/drmemory/releases/download/cronbuild-2.6.20434/DrMemory-Windows-2.6.20434.zip') '2.6.20434 stays pinned to the cronbuild zip'
    Check ($newPkg.ExpectedSha256 -eq 'ed9c0e3f1bdb7f8db1adc13531493fb1c451e15f375638592c01d8837825a73a') '2.6.20434 checksum is the published zip digest'
    Check (@(Get-DrMemoryPackageCatalog).Count -eq 2) 'catalog has only the two selected packages'
    Throws { Resolve-DrMemoryPackage 'latest' } 'alias latest is rejected'
    Throws { Resolve-DrMemoryPackage 'cronbuild' } 'alias cronbuild is rejected'
    Throws { Resolve-DrMemoryPackage '2.6.99999' } 'unknown version is rejected'
    Throws { Resolve-DrMemoryPackage '' } 'empty version is rejected'
    $fixture = Join-Path $root 'fixture.bin'
    [IO.File]::WriteAllBytes($fixture, [byte[]](1, 2, 3, 4))
    $fixtureHash = (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant()
    Check ((Test-DrMemoryPackageHash $fixture $fixtureHash) -eq $fixtureHash) 'matching checksum accepted'
    Check ((Test-DrMemoryPackageHash $fixture $null) -eq $fixtureHash) 'missing expected checksum records the observed digest'
    Throws { Test-DrMemoryPackageHash $fixture $newPkg.ExpectedSha256 } 'bytes that are not the pinned zip fail checksum'
    $customRoot = Join-Path $root 'custom tool'
    FakePE (Join-Path (Join-Path $customRoot 'bin64') 'drmemory.exe') 0x8664
    Throws { Get-DrMemoryExpandedIdentity $customRoot 'DrMemory-Windows-2.6.20434' } 'bin64 alone is not a catalog package identity'
    $expanded = Join-Path $root 'expanded'
    $null = New-Item -ItemType Directory -Path (Join-Path (Join-Path $expanded 'DrMemory-Windows-2.6.20434') 'bin64') -Force
    $identity = Get-DrMemoryExpandedIdentity $expanded 'DrMemory-Windows-2.6.20434'
    Check ($identity -eq [IO.Path]::GetFullPath((Join-Path $expanded 'DrMemory-Windows-2.6.20434'))) 'catalog folder identity accepted'
    $runner = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'run.ps1')
    Check ($runner -match '\[string\]\s+\$Package\s+=\s+''2\.6\.0''') 'default package parameter remains 2.6.0'
    Check ($runner -notmatch 'toolVersion -NotePropertyValue ''2\.6\.0''') 'runner does not stamp every result as 2.6.0'
    $badRoot = Join-Path $root 'bad package'
    # Start-Process joins ArgumentList into a command line; use the native list
    # so script and evidence paths remain single arguments on Windows/Linux.
    $badInfo = [Diagnostics.ProcessStartInfo]::new()
    $badInfo.FileName = (Get-Process -Id $PID).Path
    $badInfo.UseShellExecute = $false
    foreach ($arg in @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'run.ps1'),
                       '-Package', 'not-a-package', '-EvidenceRoot', $badRoot)) {
        $badInfo.ArgumentList.Add($arg)
    }
    $badRun = [Diagnostics.Process]::new()
    $badRun.StartInfo = $badInfo
    try {
        $null = $badRun.Start()
        if (-not $badRun.WaitForExit(30000)) {
            $badRun.Kill($true)
            throw 'Unsupported-package contract child exceeded 30 seconds'
        }
        Check ($badRun.ExitCode -eq 1) 'unsupported package exits 1'
    } finally { $badRun.Dispose() }
    $badStatus = @(Get-ChildItem -LiteralPath $badRoot -Recurse -Filter status.json)
    Check ($badStatus.Count -eq 1) 'unsupported package writes one status'
    $bad = Get-Content -Raw -LiteralPath $badStatus[0].FullName | ConvertFrom-Json
    Check ($bad.state -eq 'infrastructure-error' -and $bad.launched -eq $false -and $bad.executed -eq $false) 'unsupported package is infrastructure-error and not launched'
    Check ($bad.memoryVerdict -eq 'unknown' -and $bad.toolVersion -eq 'unidentified') 'unsupported package is not a memory verdict or a fake version'
    Check (@(Get-ChildItem -LiteralPath $badRoot -Recurse -Filter drmemory.exe -ErrorAction SilentlyContinue).Count -eq 0) 'unsupported package does not download a tool'
    $workflow = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '../../.github/workflows/drmemory.yml')
    Check ($workflow -match '(?m)^      - ''scripts/drmemory/\*\*''') 'pull request still watches the drmemory scripts'
    Check ($workflow -match '(?m)^      - ''\.github/workflows/drmemory\.yml''') 'pull request still watches drmemory.yml'
    Check ($workflow -match '(?m)^      - ''\.github/workflows/release\.yml''') 'pull request still watches release.yml'
    Check ($workflow -match 'compare_maintained_packages:[\s\S]*?default:\s*false') 'comparison input defaults off'
    Check ($workflow -match [regex]::Escape('run: ./scripts/drmemory/run.ps1 -EvidenceRoot "drmemory_out/Windows path with spaces"')) 'default execution command is unchanged'
    $comparison = [regex]::Match($workflow, '(?ms)^  package-comparison:.*\z').Value
    Check ($comparison.Length -gt 0) 'package comparison job exists'
    Check ($comparison -match 'compare_maintained_packages == ''true''') 'comparison runs only when the input is true'
    Check ($comparison -match [regex]::Escape('-Package 2.6.0 -Target')) 'comparison runs the pinned 2.6.0 package'
    Check ($comparison -match [regex]::Escape('-Package 2.6.20434')) 'comparison runs the pinned 2.6.20434 package'
    Check ($comparison -match [regex]::Escape('build_msvc/bin/test_result.exe')) 'comparison target is test_result'
    Check ($comparison -match '(?m)^\s*if:\s*always\(\)\s*$') 'later package still runs after a failed baseline'
    Check ($comparison -notmatch 'continue-on-error') 'comparison red is not turned into a pass'
    Check ($comparison -notmatch 'test_filesystem') 'comparison does not use the filesystem suite'
    Check ($workflow -match 'compare_core_tests:[\s\S]*?default:\s*false') 'core comparison defaults off'
    Check (([regex]::Matches($comparison, [regex]::Escape('-Target build_msvc/bin/test_result.exe'))).Count -eq 2) 'Result comparison still uses the same target twice'
    Check (([regex]::Matches($comparison, [regex]::Escape('-Target build_msvc/bin/core_tests.exe'))).Count -eq 2) 'core comparison uses the same existing target twice'
    $coreGuard = 'if: ${{ always() && github.event.inputs.compare_core_tests == ''true'' }}'
    Check (([regex]::Matches($comparison, [regex]::Escape($coreGuard))).Count -eq 2) 'both core steps require explicit manual opt-in and survive earlier diagnostic failure'
    $release = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '../../.github/workflows/release.yml')
    Check ($release -match '(?m)^\s*run: \./scripts/drmemory/run\.ps1\s*$') 'release caller remains the default invocation'
    Check ($release -notmatch 'run\.ps1[^\r\n]*-Package') 'release caller does not select a package'
    $actualJob = [regex]::Match($workflow, '(?ms)^  actual-execution:.*?(?=^  [a-z][a-z0-9-]*:|\z)').Value
    Check ($actualJob -match '(?m)^    if: \$\{\{ false \}\}\r?$') 'automatic diagnostic execution is explicitly disabled'
    Check ($actualJob -match 'name: Dr\.Memory DISABLED') 'disabled diagnostic does not claim a memory pass'
    $releaseJob = [regex]::Match($release, '(?ms)^  dr-memory:.*?(?=^  [a-z][a-z0-9-]*:|\z)').Value
    Check ($releaseJob -match '(?m)^    if: \$\{\{ false \}\}\r?$') 'release diagnostic execution is explicitly disabled'
    Check ($releaseJob -match 'name: Dr\.Memory DISABLED') 'disabled release check does not claim a memory pass'
    Write-Host "Dr.Memory contracts: $script:checks passed"
} finally { Remove-Item -LiteralPath $root -Recurse -Force }
