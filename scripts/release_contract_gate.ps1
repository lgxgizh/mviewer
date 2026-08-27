<##
.SYNOPSIS
    Strict post-package gate for the MViewer Release Candidate contract.

.DESCRIPTION
    Validates exact artifact names, runtime payload, forbidden development
    files, packaged --selftest, installer PE version identity, and SHA256SUMS.
    Every check is a hard failure; this script is intentionally unsuitable for
    a best-effort warning-only release path.
#>
[CmdletBinding()]
param(
    [string]$ArtifactDir = 'dist',
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$InstallerPath = '',
    [string]$ManifestPath = ''
)

$ErrorActionPreference = 'Stop'
$repo = (Get-Location).Path
. (Join-Path $PSScriptRoot 'release_version.ps1')
$identity = Get-ReleaseVersionIdentity $Version
$version = $identity.Version
$artifactRoot = (Resolve-Path (Join-Path $repo $ArtifactDir)).Path
$zipPath = Join-Path $artifactRoot "MViewer-$version-portable.zip"
if (-not (Test-Path -LiteralPath $zipPath)) {
    throw "Strict RC gate: missing exact portable artifact '$zipPath'"
}

if ($InstallerPath -eq '') { $InstallerPath = Join-Path $artifactRoot "MViewer-$version-Setup.exe" }
if (-not (Test-Path -LiteralPath $InstallerPath)) {
    throw "Strict RC gate: missing exact installer '$InstallerPath'"
}

$manifest = if ($ManifestPath -ne '') { $ManifestPath } else { Join-Path $artifactRoot 'SHA256SUMS.txt' }
if (-not (Test-Path -LiteralPath $manifest)) {
    throw "Strict RC gate: missing checksum manifest '$manifest'"
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("mviewer-rc-gate-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
try {
    Expand-Archive -LiteralPath $zipPath -DestinationPath $tempRoot -Force
    $payload = @(Get-ChildItem -LiteralPath $tempRoot -Recurse -File)
    $relative = @{}
    foreach ($file in $payload) {
        $name = ($file.FullName.Substring($tempRoot.Length).TrimStart('\', '/') -replace '\\', '/')
        $relative[$name.ToLowerInvariant()] = $file
    }

    foreach ($required in @(
        'MViewer.exe', 'mviewer_core.dll', 'Qt6Core.dll', 'Qt6Gui.dll',
        'Qt6Widgets.dll', 'Qt6Sql.dll', 'platforms/qwindows.dll',
        'sqldrivers/qsqlite.dll'
    )) {
        if (-not $relative.ContainsKey($required.ToLowerInvariant())) {
            throw "Strict RC gate: portable payload missing '$required'"
        }
    }

    foreach ($file in $payload) {
        $name = ($file.FullName.Substring($tempRoot.Length).TrimStart('\', '/') -replace '\\', '/')
        if ($file.Extension -match '\.(pdb|obj|lib|cpp|c|h|hpp)$' -or
            $file.Name -match '(^|_)(test|tests|bench|benchmark|golden|fixture)' -or
            $file.Name -match 'Qt6.*d\.dll$') {
            throw "Strict RC gate: forbidden development file in portable payload: $name"
        }
    }
    if ($relative.Keys | Where-Object { $_ -like 'benchmarks/*' -or $_ -like '*.vcxproj*' }) {
        throw 'Strict RC gate: development corpus/project files must not ship'
    }

    $exe = Join-Path $tempRoot 'MViewer.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Strict RC gate: packaged MViewer.exe is not at archive root' }
    $oldQpa = $env:QT_QPA_PLATFORM
    # Validate the production platform plugin. The shipping payload deliberately
    # contains qwindows.dll, while the offscreen plugin is test-only.
    $env:QT_QPA_PLATFORM = 'windows'
    Push-Location $tempRoot
    try {
        $selfTestLog = Join-Path $tempRoot 'selftest.log'
        $selfTestError = Join-Path $tempRoot 'selftest.err'
        $startInfo = New-Object System.Diagnostics.ProcessStartInfo
        $startInfo.FileName = $exe
        $startInfo.Arguments = '--selftest'
        $startInfo.WorkingDirectory = $tempRoot
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $selfTest = New-Object System.Diagnostics.Process
        $selfTest.StartInfo = $startInfo
        if (-not $selfTest.Start()) {
            throw 'Strict RC gate: could not start packaged MViewer.exe --selftest'
        }
        $stdoutTask = $selfTest.StandardOutput.ReadToEndAsync()
        $stderrTask = $selfTest.StandardError.ReadToEndAsync()
        try {
            if (-not $selfTest.WaitForExit(120000)) {
                Stop-Process -Id $selfTest.Id -Force -ErrorAction SilentlyContinue
                throw 'Strict RC gate: packaged MViewer.exe --selftest timed out'
            }
            $selfTest.WaitForExit()
            $stdout = $stdoutTask.Result
            $stderr = $stderrTask.Result
            [IO.File]::WriteAllText($selfTestLog, $stdout)
            [IO.File]::WriteAllText($selfTestError, $stderr)
            $exitCode = $selfTest.ExitCode
            if ($exitCode -ne 0) {
                throw "Strict RC gate: packaged MViewer.exe --selftest failed (exit $exitCode): $($stderr.Trim())"
            }
        } finally {
            if (-not $selfTest.HasExited) {
                Stop-Process -Id $selfTest.Id -Force -ErrorAction SilentlyContinue
            }
        }
        $selfTest.Dispose()
    } finally {
        Pop-Location
        if ($null -eq $oldQpa) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
        else { $env:QT_QPA_PLATFORM = $oldQpa }
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}

$info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path $InstallerPath).Path)
$actualFileVersion = ($info.FileVersion -replace '[^0-9.].*$', '').TrimEnd('.')
$actualProductVersion = ($info.ProductVersion -replace '[^0-9.].*$', '').TrimEnd('.')
if ($actualFileVersion -ne $identity.WindowsVersion -or $actualProductVersion -ne $identity.WindowsVersion) {
    throw "Strict RC gate: installer PE version mismatch (file='$actualFileVersion', product='$actualProductVersion', expected='$($identity.WindowsVersion)')"
}

$manifestLines = Get-Content -LiteralPath $manifest
$expected = @("MViewer-$version-portable.zip", "MViewer-$version-Setup.exe")
foreach ($artifact in $expected) {
    $line = $manifestLines | Where-Object { $_ -match ('^[0-9a-fA-F]{64}\s{2}' + [regex]::Escape($artifact) + '$') } | Select-Object -First 1
    if (-not $line) { throw "Strict RC gate: checksum manifest has no entry for '$artifact'" }
    $expectedHash = ($line -split '\s+')[0].ToLowerInvariant()
    $actualHash = (Get-FileHash -LiteralPath (Join-Path $artifactRoot $artifact) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expectedHash -ne $actualHash) { throw "Strict RC gate: checksum mismatch for '$artifact'" }
}

Write-Host "Strict RC release contract: PASS ($version; Windows metadata $($identity.WindowsVersion))" -ForegroundColor Green
