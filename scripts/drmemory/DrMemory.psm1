# Native PowerShell paths only; never consume an MSYS $PWD as a Windows path.
Set-StrictMode -Version Latest

function Get-PeMachine([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE file: $Path" }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset + 6 -gt $stream.Length) { throw "Invalid PE offset: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $Path" }
        return $reader.ReadUInt16()
    } finally { $stream.Dispose() }
}

function Find-DrMemory([string] $Root, [string] $Target) {
    $machine = Get-PeMachine $Target
    $folder = switch ($machine) { 0x8664 { 'bin64' }; 0x014c { 'bin' }; default { throw "Unsupported target PE machine: $machine" } }
    $candidates = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter drmemory.exe |
        Where-Object { $_.Directory.Name -eq $folder -and (Get-PeMachine $_.FullName) -eq $machine })
    if ($candidates.Count -ne 1) { throw "Expected one $folder/drmemory.exe for target machine $machine, found $($candidates.Count) under $Root" }
    return $candidates[0].FullName
}

function Get-DrMemoryResult {
    param([string] $LogRoot, [string] $TargetName, [int] $ExitCode,
          [string] $Output = '', [bool] $TimedOut = $false, [string] $LaunchError = '')
    $files = @(Get-ChildItem -LiteralPath $LogRoot -Recurse -File -Filter results.txt -ErrorAction SilentlyContinue |
        Where-Object { $_.Length -gt 0 -and $_.Directory.Name -like "DrMemory-$TargetName.*" })
    $details = @($files | ForEach-Object {
        $text = [IO.File]::ReadAllText($_.FullName)
        $summary = [regex]::Match($text, '(?ms)^(?:NO )?ERRORS FOUND:\s*\r?\n(.*?)(?=^ERRORS IGNORED:|^Details:|\z)')
        $counts = @([regex]::Matches($summary.Groups[1].Value, '(?m)^\s*(\d+) unique,\s*(\d+) total[^\r\n]*') |
            Where-Object { $_.Value -notmatch 'still-reachable' })
        $positive = @($counts | Where-Object { [long]$_.Groups[1].Value -gt 0 -or [long]$_.Groups[2].Value -gt 0 })
        $errorRecords = [regex]::Matches($text, '(?m)^Error #\d+:').Count
        [pscustomobject]@{
            path = $_.FullName
            summaryComplete = ($text -match '(?m)^FINAL SUMMARY:' -and $text -match '(?m)^Details:\s+\S' -and $summary.Success -and $counts.Count -ge 3)
            errorRecords = $errorRecords
            hasReportedErrors = ($errorRecords -gt 0 -or $positive.Count -gt 0)
            potentialFindings = ($text -match '(?m)^\s*[1-9]\d* potential (?:error|leak)')
            # 2.6.0 can generate syscall metadata then restart the target. Keep
            # this report, but never use its header as completion evidence.
            bootstrapRestart = ($text -match 'System call information is missing.*Restarting to trigger auto-generation')
        }
    })
    $targets = @($details | Where-Object { -not $_.bootstrapRestart })
    $errors = @($targets | Where-Object { $_.hasReportedErrors })
    $potential = @($targets | Where-Object { $_.potentialFindings })
    $complete = $targets.Count -gt 0 -and @($targets | Where-Object { -not $_.summaryComplete }).Count -eq 0
    $instrumented = @($targets | Where-Object { $_.hasReportedErrors -or $_.summaryComplete }).Count -gt 0
    $errorRecordTotal = 0
    foreach ($detail in $details) { $errorRecordTotal += $detail.errorRecords }
    $r = [ordered]@{ state = 'no-report'; executed = $instrumented; completed = $false
        memoryVerdict = $(if ($errors.Count -gt 0) { 'errors-observed' } else { 'unknown' })
        exitCode = $ExitCode; reports = @($files | ForEach-Object { $_.FullName }); reportDetails = $details
        observedErrorRecords = $errorRecordTotal
        reason = 'No nonempty target results.txt produced in the fresh invocation directory' }
    if ($LaunchError) { $r.state = 'infrastructure-error'; $r.reason = $LaunchError }
    elseif ($TimedOut) { $r.state = 'timeout'; $r.reason = 'Bounded deadline exceeded; process tree terminated. Any observed findings remain in reportDetails/memoryVerdict; no complete-run verdict' }
    elseif ($errors.Count -gt 0) {
        $r.state = 'executed-errors'; $r.completed = $complete; $r.reason = 'Real target report contains memory errors/leaks; no suppressions added'
    } elseif ($complete -and $potential.Count -gt 0) {
        $r.state = 'executed-potential'; $r.completed = $true; $r.reason = 'Potential/ignored findings need review; inspect retained potential_errors.txt'
    } elseif ($complete -and $ExitCode -eq 0) {
        $r.state = 'executed-clean'; $r.completed = $true; $r.memoryVerdict = 'clean-reported-categories'; $r.reason = 'All target summaries complete, zero unsuppressed counts, launcher exit zero; default tool suppressions and reported categories still limit coverage'
    } elseif ($complete) {
        $r.state = 'target-or-tool-error'; $r.completed = $true; $r.reason = 'Target reports complete but nonzero exit prevents a clean result'
    } elseif ($targets.Count -gt 0) {
        $r.state = 'incomplete-report'; $r.reason = 'One or more target reports lack a complete summary; no clean execution verdict'
    } elseif ($details.Count -gt 0 -or $Output -match '(?i)system call information is missing|unable to load client library|failed to start the target|unsupported operating system') {
        $r.state = 'unsupported'; $r.reason = 'Only bootstrap/OS-loader incompatibility evidence exists; no completed memory verdict'
    } elseif ($ExitCode -ne 0) { $r.state = 'tool-error'; $r.reason = 'Launcher exited nonzero without a target report; inspect stdout/stderr' }
    return [pscustomobject]$r
}

function Invoke-DrMemoryProcess {
    param([string] $Tool, [string] $Target, [string] $LogRoot, [string] $EvidenceRoot,
          [int] $TimeoutSeconds = 180)
    $stdout = Join-Path $EvidenceRoot stdout.txt
    $stderr = Join-Path $EvidenceRoot stderr.txt
    $args = @('-batch', '-logdir', $LogRoot, '-exit_code_if_errors', '86', '--', $Target)
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Tool
    foreach ($arg in $args) { $info.ArgumentList.Add($arg) }
    $info.WorkingDirectory = Split-Path -Parent $Target
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.Environment['QT_QPA_PLATFORM'] = 'offscreen'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $exitCode = -1; $timedOut = $false; $launchError = ''; $launched = $false
    $outTask = $null; $errTask = $null
    try {
        $launched = $process.Start()
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $errTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            $process.Kill($true)
            if (-not $process.WaitForExit(10000)) { throw 'Process tree did not exit after termination' }
        }
        $exitCode = $process.ExitCode
    } catch { $launchError = $_.ToString() }
    finally {
        # A descendant can retain inherited pipe handles. Never block indefinitely on ReadToEnd.
        $outText = ''; $errText = ''; $captureComplete = $true
        if ($null -ne $outTask) {
            if ($outTask.Wait(5000)) { $outText = $outTask.Result } else { $captureComplete = $false }
        }
        if ($null -ne $errTask) {
            if ($errTask.Wait(5000)) { $errText = $errTask.Result } else { $captureComplete = $false }
        }
        [IO.File]::WriteAllText($stdout, $outText)
        [IO.File]::WriteAllText($stderr, $errText)
        $process.Dispose()
    }
    if (-not $captureComplete -and -not $launchError) { $launchError = 'Inherited output pipes did not close within deadline; stdout/stderr capture incomplete' }
    $r = Get-DrMemoryResult -LogRoot $LogRoot -TargetName ([IO.Path]::GetFileName($Target)) -ExitCode $exitCode -Output ($outText + "`n" + $errText) -TimedOut $timedOut -LaunchError $launchError
    $r | Add-Member -NotePropertyName outputCaptureComplete -NotePropertyValue $captureComplete
    $r | Add-Member -NotePropertyName launched -NotePropertyValue $launched
    $r | Add-Member -NotePropertyName tool -NotePropertyValue $Tool
    $r | Add-Member -NotePropertyName target -NotePropertyValue $Target
    $r | Add-Member -NotePropertyName arguments -NotePropertyValue $args
    return $r
}

# Explicit package catalog. Ids are exact; callers cannot select "latest".
# expectedSha256 is null only when no published digest was supplied for that
# asset. A custom ToolRoot is not a catalog entry and must not be labeled here.
function Get-DrMemoryPackageCatalog {
    @(
        [pscustomobject]@{
            Id = '2.6.0'
            Url = 'https://github.com/DynamoRIO/drmemory/releases/download/release_2.6.0/DrMemory-Windows-2.6.0.zip'
            ExpectedSha256 = $null
            ArchiveFolder = 'DrMemory-Windows-2.6.0'
        }
        [pscustomobject]@{
            Id = '2.6.20434'
            Url = 'https://github.com/DynamoRIO/drmemory/releases/download/cronbuild-2.6.20434/DrMemory-Windows-2.6.20434.zip'
            ExpectedSha256 = 'ed9c0e3f1bdb7f8db1adc13531493fb1c451e15f375638592c01d8837825a73a'
            ArchiveFolder = 'DrMemory-Windows-2.6.20434'
        }
    )
}

function Resolve-DrMemoryPackage([string] $Id) {
    $hit = @(Get-DrMemoryPackageCatalog | Where-Object { $_.Id -eq $Id })
    if ($hit.Count -ne 1) { throw "Unsupported Dr.Memory package id: '$Id'" }
    return $hit[0]
}

function Test-DrMemoryPackageHash([string] $Path, [string] $ExpectedSha256) {
    $got = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ExpectedSha256 -and $got -ne $ExpectedSha256.ToLowerInvariant()) {
        throw "Dr.Memory package checksum mismatch: expected $ExpectedSha256 observed $got"
    }
    return $got
}

function Get-DrMemoryExpandedIdentity([string] $Root, [string] $ExpectedFolder) {
    $dirs = @(Get-ChildItem -LiteralPath $Root -Directory -Filter $ExpectedFolder)
    if ($dirs.Count -ne 1) {
        throw "Package folder $ExpectedFolder was not found under $Root"
    }
    return [IO.Path]::GetFullPath($dirs[0].FullName)
}

Export-ModuleMember -Function Get-PeMachine, Find-DrMemory, Get-DrMemoryResult, Invoke-DrMemoryProcess, Get-DrMemoryPackageCatalog, Resolve-DrMemoryPackage, Test-DrMemoryPackageHash, Get-DrMemoryExpandedIdentity
