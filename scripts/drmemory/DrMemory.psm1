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
    $r = [ordered]@{ state = 'no-report'; executed = $false; memoryVerdict = 'unknown'; exitCode = $ExitCode
        reports = @($files | ForEach-Object { $_.FullName }); reason = 'No nonempty target results.txt produced in the fresh invocation directory' }
    if ($LaunchError) { $r.state = 'infrastructure-error'; $r.reason = $LaunchError }
    elseif ($TimedOut) { $r.state = 'timeout'; $r.reason = 'Bounded instrumentation deadline exceeded; process tree terminated' }
    elseif ($files.Count -gt 0) {
        $text = ($files | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
        $summary = [regex]::Match($text, '(?ms)^(?:NO )?ERRORS FOUND:\s*\r?\n(.*?)(?=^ERRORS IGNORED:|^Details:|\z)')
        $counts = [regex]::Matches($summary.Groups[1].Value, '(?m)^\s*(\d+) unique,\s*(\d+) total[^\r\n]*')
        $counts = @($counts | Where-Object { $_.Value -notmatch 'still-reachable' })
        $errors = @($counts | Where-Object { [long]$_.Groups[1].Value -gt 0 -or [long]$_.Groups[2].Value -gt 0 })
        $complete = $text -match '(?m)^FINAL SUMMARY:' -and $summary.Success -and $counts.Count -ge 3
        if ($text -match '(?m)^Error #\d+' -or $errors.Count -gt 0) {
            $r.state = 'executed-errors'; $r.executed = $true; $r.memoryVerdict = 'errors'; $r.reason = 'Real target report contains memory errors/leaks; no suppressions added'
        } elseif ($complete -and $text -match '(?m)^\s*[1-9]\d* potential (?:error|leak)') {
            $r.state = 'executed-potential'; $r.executed = $true; $r.reason = 'Potential/ignored findings need review; inspect the retained potential_errors.txt'
        } elseif ($complete -and $ExitCode -eq 0) {
            $r.state = 'executed-clean'; $r.executed = $true; $r.memoryVerdict = 'clean-reported-categories'; $r.reason = 'Complete target error summary has zero counts and launcher exited zero; inspect ignored/potential categories in raw report'
        } elseif ($complete) {
            $r.state = 'target-or-tool-error'; $r.executed = $true; $r.reason = 'Target report exists but nonzero exit prevents a clean result'
        } else { $r.state = 'incomplete-report'; $r.reason = 'Target report lacks a complete error summary; not a clean execution verdict' }
    } elseif ($Output -match '(?i)system call information is missing|unable to load client library|failed to start the target|unsupported operating system') {
        $r.state = 'unsupported'; $r.reason = 'Tool output reports an instrumentation/OS loader incompatibility; no memory verdict'
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

Export-ModuleMember -Function Get-PeMachine, Find-DrMemory, Get-DrMemoryResult, Invoke-DrMemoryProcess
