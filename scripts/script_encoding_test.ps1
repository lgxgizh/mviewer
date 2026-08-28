# M52 regression test for Windows PowerShell script execution and UTF-8 output.

param(
    [string]$Repo = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'
$failures = 0

function Check($condition, [string]$message) {
    if ($condition) { Write-Host "PASS: $message" }
    else { Write-Host "FAIL: $message"; $script:failures++ }
}

function Read-StrictUtf8([string]$Path) {
    $encoding = New-Object System.Text.UTF8Encoding($false, $true)
    return $encoding.GetString([System.IO.File]::ReadAllBytes($Path))
}

function CheckGeneratedFile([string]$Path, [string]$Label) {
    try {
        $text = Read-StrictUtf8 $Path
        $bad = @([char]0x9205, [char]0x8DEF, [char]0x951F, [char]0xFFFD, [char]0x00C3, [char]0x00C2) |
            Where-Object { $text.Contains([string]$_) }
        Check ($bad.Count -eq 0) "$Label has no common mojibake tokens"
        $roundTrip = [System.Text.UTF8Encoding]::new($false).GetString(
            [System.Text.UTF8Encoding]::new($false).GetBytes($text))
        Check ($roundTrip -ceq $text) "$Label round-trips as UTF-8"
    }
    catch {
        Check $false "$Label is valid UTF-8 ($($_.Exception.Message))"
    }
}

$tmp = Join-Path $env:TEMP ("m52_script_encoding_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
try {
    $matrix = Join-Path $tmp 'test_matrix.md'
    $dashboard = Join-Path $tmp 'dashboard.md'
    $health = Join-Path $tmp 'build_health.json'

    Push-Location $Repo
    try {
        # Use the supported Windows PowerShell 5.1 executable and a relative
        # -File path, matching the documented local/CI invocation shape.
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test_matrix.ps1 `
            -Out $matrix -Repo $Repo | Out-Null
        Check ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $matrix)) `
            'test_matrix.ps1 runs from the Windows PowerShell entry point'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\health_score.ps1 `
            -Dashboard $dashboard -OutJson $health -Repo $Repo | Out-Null
        Check ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $dashboard) -and (Test-Path -LiteralPath $health)) `
            'health_score.ps1 runs from the Windows PowerShell entry point'
    }
    finally {
        Pop-Location
    }

    CheckGeneratedFile $matrix 'test_matrix.md'
    CheckGeneratedFile $dashboard 'dashboard.md'
    CheckGeneratedFile $health 'build_health.json'

    $unicodePath = Join-Path $tmp 'unicode_roundtrip.txt'
    $unicode = [string]::Concat([char]0x4E2D, [char]0x6587, ' ', [char]0xD83D, [char]0xDE00)
    [System.IO.File]::WriteAllText($unicodePath, $unicode, (New-Object System.Text.UTF8Encoding($false)))
    Check ((Read-StrictUtf8 $unicodePath) -ceq $unicode) 'UTF-8 non-ASCII content round-trips without code-page loss'
}
finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "script_encoding_test: $failures failure(s)"
exit $failures
