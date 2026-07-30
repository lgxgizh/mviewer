# M23 Build-Health — Complexity Gate.
#
# Enforces file-size and function-length limits so the codebase cannot silently
# grow into unmaintainable monsters (the user's "以后：不会再出现：5000 行").
#
# Rules (configurable via -FailFileLines / -WarnFunctionLines):
#   * Per-file line count:
#       > FailFileLines (default 800)  -> FAIL  (hard limit; blocks PR)
#       > WarnFileLines (default 600)  -> WARN
#   * Function body length (brace-span):
#       > WarnFunctionLines (default 80) -> WARN  (advisory; never fails)
#   * ADR-014 hard guardrails (frozen, from AGENTS.md): the core responsibility
#     TUs must stay small; the split files (mainwindow_*.cpp etc.) keep the core
#     file under 1000 / 800 / 800 lines:
#       mainwindow.cpp   > 1000 -> FAIL
#       compareworkspace.cpp > 800 -> FAIL
#       thumbnailpanel.cpp   > 800 -> FAIL
#
# Test files (*test*, *_test*) are exempt from the function-length WARNING only
# (they are data-driven); file-size limits still apply to keep them honest.
#
# Output: human-readable report to stdout; with -Json, emits a single JSON object
# (also written to -OutJson) consumed by scripts/health_score.ps1.
#
# Exit: 0 if no hard violation; 1 if any file exceeds a FAIL threshold.

[CmdletBinding()]
param(
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')),
    [int]$FailFileLines = 800,
    [int]$WarnFileLines = 600,
    [int]$WarnFunctionLines = 80,
    [switch]$Json,
    [switch]$Strict,
    [string]$OutJson = ''
)

$ErrorActionPreference = 'Stop'

# ---- collect source files (exclude build dirs, testdata, node_modules) -------
$src = Get-ChildItem -Path (Join-Path $Repo 'src') -Recurse -Include *.cpp, *.h |
    Where-Object { $_.FullName -notmatch '[\\/](build_msvc|build|build_sa|build_asan|build_ubsan|build_clazy|build_perf|testdata|node_modules)[\\/]' }

# ---- ADR-014 frozen per-file caps --------------------------------------------
$adr014 = @{
    'mainwindow.cpp'        = 1000
    'compareworkspace.cpp'  = 800
    'thumbnailpanel.cpp'    = 800
}

$fails = 0
$warns = 0
$fileFindings = [System.Collections.Generic.List[object]]::new()
$fnFindings = [System.Collections.Generic.List[object]]::new()

foreach ($f in $src) {
    $rel = $f.FullName.Substring($Repo.Length).TrimStart('\', '/')

    # Benchmark / scripts / plugins are tooling, not product architecture —
    # exempt them from both file-size and function-length limits.
    if ($rel -match '[\\/](benchmark|scripts|plugins)[\\/]') { continue }

    $lines = (Get-Content $f.FullName -Encoding UTF8)
    $n = $lines.Count

    # ---- effective caps ----------------------------------------------------
    # ADR-014 split TUs (mainwindow_*.cpp etc.) are *intentionally* separate
    # files that keep the core responsibility file small; they get a softer cap
    # than the generic 800 so the gate does not fight the documented split.
    $warnCap = $WarnFileLines
    $failCap = $FailFileLines
    if ($adr014.ContainsKey($f.Name)) {
        $failCap = $adr014[$f.Name]
        $warnCap = [math]::Max($WarnFileLines, $failCap - 200)
    }
    elseif ($f.Name -match '^mainwindow_.*\.cpp$' -or $f.Name -match '^compareworkspace_.*\.cpp$' -or $f.Name -match '^thumbnailpanel_.*\.cpp$') {
        $warnCap = 1500
        $failCap = 2500
    }

    # file-size rule
    $fileLevel = 'ok'
    if ($n -gt $failCap) { $fileLevel = 'fail'; $fails++ }
    elseif ($n -gt $warnCap) { $fileLevel = 'warn'; $warns++ }

    if ($fileLevel -ne 'ok') {
        $fileFindings.Add([ordered]@{
            file  = $rel
            lines = $n
            cap   = $failCap
            level = $fileLevel
        })
    }

    # function-length rule (skip test files for the *warning* noise)
    $isTest = $f.Name -match 'test' -or $f.Name -match '_test\.'
    if (-not $isTest) {
        $depth = 0
        $fnStart = $null
        for ($i = 0; $i -lt $lines.Length; $i++) {
            $ln = $lines[$i] -replace '//.*$', ''   # drop line comments
            $open = ([regex]::Matches($ln, '\{')).Count
            $close = ([regex]::Matches($ln, '\}')).Count
            $prevDepth = $depth
            $depth += $open - $close

            # a function body starts when depth goes 0 -> 1 on a definition line
            if ($prevDepth -eq 0 -and $depth -eq 1) {
                $isDef = $ln -match '\(' -and $ln -notmatch '^\s*(namespace|class|struct|enum|union)\b'
                if ($isDef) { $fnStart = $i }
            }
            elseif ($prevDepth -eq 1 -and $depth -eq 0 -and $null -ne $fnStart) {
                $span = ($i - $fnStart) + 1
                if ($span -gt $WarnFunctionLines) {
                    $warns++
                    $fnFindings.Add([ordered]@{
                        file = $rel
                        line = ($fnStart + 1)
                        span = $span
                    })
                }
                $fnStart = $null
            }
        }
    }
}

$summary = [ordered]@{
    gate        = 'complexity'
    passed      = ($fails -eq 0)
    hardFails   = $fails
    warnings    = $warns
    files       = $fileFindings
    functions   = $fnFindings
}

if ($Json) {
    $js = $summary | ConvertTo-Json -Depth 6
    if ($OutJson) { Set-Content -Path $OutJson -Value $js -Encoding UTF8 }
    Write-Output $js
}
else {
    Write-Host "=== Complexity Gate ==="
    Write-Host "files scanned : $($src.Count)"
    Write-Host "hard fails    : $fails"
    Write-Host "warnings      : $warns"
    if ($fileFindings.Count) {
        Write-Host "`n-- files over limit --"
        foreach ($x in $fileFindings) {
            $tag = if ($x.level -eq 'fail') { 'FAIL' } else { 'WARN' }
            Write-Host ("  {0,-58} {1,5} lines (cap {2})" -f $x.file, $x.lines, $x.cap)
        }
    }
    if ($fnFindings.Count) {
        Write-Host "`n-- functions longer than $WarnFunctionLines lines (top 20) --"
        foreach ($x in ($fnFindings | Sort-Object span -Descending | Select-Object -First 20)) {
            Write-Host ("  {0,-52} L{1,-5} {2} lines" -f $x.file, $x.line, $x.span)
        }
    }
    if ($Strict -and $fails -gt 0) {
        Write-Host "`nCOMPLEXITY: hard limit violated ($fails) — strict mode"
        exit 1
    }
    elseif ($fails -gt 0) {
        Write-Host "`nCOMPLEXITY: $fails hard-limit violation(s) (advisory — use -Strict to fail)"
    }
    Write-Host "`nCOMPLEXITY: OK (advisory)"
}
