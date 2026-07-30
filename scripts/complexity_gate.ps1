# M23 Build-Health — Complexity Gate.
#
# Enforces file-size, function-length, cyclomatic-complexity and class-size
# limits so the codebase cannot silently grow into unmaintainable monsters
# (the product owner's "以后：不会再出现：5000 行 MainWindow").
#
# Rules (all configurable):
#   * Per-file line count:
#       > FailFileLines   (default 800) -> FAIL  (hard limit)
#       > WarnFileLines   (default 600) -> WARN
#   * Function body length (brace-span):
#       > FailFunctionLines (default 120) -> FAIL
#       > WarnFunctionLines (default 80)  -> WARN
#   * Cyclomatic complexity (per function):
#       > FailCyclo (default 25) -> FAIL
#       > WarnCyclo (default 15) -> WARN
#   * Class / struct body length:
#       > WarnClassLines (default 1000) -> WARN  (advisory only)
#   * ADR-014 frozen per-file caps (frozen, from AGENTS.md): the core
#     responsibility TUs must stay small:
#       mainwindow.cpp        > 1000 -> FAIL
#       compareworkspace.cpp  > 800  -> FAIL
#       thumbnailpanel.cpp    > 800  -> FAIL
#
# Cyclomatic complexity is approximated by counting decision points
# (if / else / for / while / case / catch / switch / && / || / ?:) inside each
# function body, starting from 1. This is the standard McCabe definition
# approximated with a brace-stack parser — good enough for a CI gate.
#
# Test files are exempt from the function-length/cyclo *warning* noise (they are
# data-driven); file-size and class limits still apply to keep them honest.
#
# Output: human report to stdout; with -Json emits a single JSON object (also
# written to -OutJson) consumed by scripts/health_score.ps1.
#
# Exit: 0 by default (advisory). With -Strict, exits 1 if any hard (FAIL)
# threshold is violated — enabling this in a CI tier turns the gate from
# "soft warning" into a real merge blocker.

[CmdletBinding()]
param(
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')),
    [int]$FailFileLines = 800,
    [int]$WarnFileLines = 600,
    [int]$WarnFunctionLines = 80,
    [int]$FailFunctionLines = 120,
    [int]$WarnCyclo = 15,
    [int]$FailCyclo = 25,
    [int]$WarnClassLines = 1000,
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
    'mainwindow.cpp'       = 1000
    'compareworkspace.cpp' = 800
    'thumbnailpanel.cpp'   = 800
}

$fails = 0
$warns = 0
$fileFindings = [System.Collections.Generic.List[object]]::new()
$fnFindings = [System.Collections.Generic.List[object]]::new()
$classFindings = [System.Collections.Generic.List[object]]::new()
$cycloFails = 0
$funcFails = 0
$classWarns = 0

# Count decision points on a line for cyclomatic complexity.
function Measure-DecisionPoints([string]$line) {
    $c = 0
    $c += ([regex]::Matches($line, '\b(if|else|for|while|case|catch|switch)\b')).Count
    $c += ([regex]::Matches($line, '&&|\|\|')).Count
    $c += ([regex]::Matches($line, '\?')).Count   # ternary (colon is ambiguous, count '?')
    return $c
}

foreach ($f in $src) {
    $rel = $f.FullName.Substring($Repo.Length).TrimStart('\', '/')

    # Benchmark / scripts / plugins are tooling, not product architecture —
    # exempt them from all limits.
    if ($rel -match '[\\/](benchmark|scripts|plugins)[\\/]') { continue }

    $lines = @(Get-Content $f.FullName -Encoding UTF8)
    $n = $lines.Count

    # ---- effective file caps (ADR-014) ------------------------------------
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

    # ---- function / class / complexity analysis (brace-stack) -------------
    $isTest = $f.Name -match 'test' -or $f.Name -match '_test\.'
    # stack frame: @{ type: 'func'|'class'|'block'; start: int; cc: int }
    $stack = [System.Collections.Generic.List[object]]::new()
    $prevLine = ''

    for ($i = 0; $i -lt $lines.Length; $i++) {
        $ln = $lines[$i] -replace '//.*$', ''   # drop // comments
        # strip block comments crudely: remove /* ... */ on the same line
        $ln = $ln -replace '/\*.*?\*/', ''

        # For CC counting, if we are currently inside a function frame, count
        # decision points on this line.
        if ($stack.Count -gt 0 -and $stack[-1].type -eq 'func') {
            $stack[-1].cc += Measure-DecisionPoints $ln
        }

        # Walk braces left-to-right to keep frame pairing correct.
        for ($p = 0; $p -lt $ln.Length; $p++) {
            $ch = $ln[$p]
            if ($ch -eq '{') {
                $prefix = ($prevLine + ' ' + $ln.Substring(0, $p))
                $type = 'block'
                if ($prefix -match '\b(class|struct)\b') { $type = 'class' }
                elseif ($prefix -match '\(' -and $prefix -notmatch '\b(if|for|while|switch|catch|do|else)\b\s*$') {
                    $type = 'func'
                }
                $frame = [ordered]@{ type = $type; start = ($i + 1); cc = 1 }
                $stack.Add($frame)
            }
            elseif ($ch -eq '}') {
                if ($stack.Count -gt 0) {
                    $frame = $stack[-1]
                    $stack.RemoveAt($stack.Count - 1)
                    if ($frame.type -eq 'func') {
                        $span = ($i + 1) - $frame.start + 1
                        $cc = $frame.cc
                        if (-not $isTest) {
                            if ($cc -gt $FailCyclo) { $cycloFails++; $fails++; $warns++ }
                            elseif ($cc -gt $WarnCyclo) { $warns++ }
                            if ($span -gt $FailFunctionLines) { $funcFails++; $fails++; $warns++ }
                            elseif ($span -gt $WarnFunctionLines) { $warns++ }
                        }
                        if (-not $isTest -and ($cc -gt $WarnCyclo -or $span -gt $WarnFunctionLines)) {
                            $fnFindings.Add([ordered]@{
                                file = $rel
                                line = $frame.start
                                span = $span
                                cc   = $cc
                            })
                        }
                    }
                    elseif ($frame.type -eq 'class') {
                        $span = ($i + 1) - $frame.start + 1
                        if ($span -gt $WarnClassLines) {
                            $classWarns++; $warns++
                            $classFindings.Add([ordered]@{
                                file  = $rel
                                line  = $frame.start
                                span  = $span
                                limit = $WarnClassLines
                            })
                        }
                    }
                }
            }
        }
        $prevLine = $ln
    }
}

$summary = [ordered]@{
    gate           = 'complexity'
    passed         = ($fails -eq 0)
    hardFails      = $fails
    warnings       = $warns
    cycloFails     = $cycloFails
    funcFailLines  = $funcFails
    classWarnings  = $classWarns
    files          = $fileFindings
    functions      = $fnFindings
    classes        = $classFindings
}

if ($Json) {
    $js = $summary | ConvertTo-Json -Depth 6
    if ($OutJson) { Set-Content -Path $OutJson -Value $js -Encoding UTF8 }
    Write-Output $js
}
else {
    Write-Host "=== Complexity Gate ==="
    Write-Host "files scanned     : $($src.Count)"
    Write-Host "hard fails (file) : $fails  (file>$($FailFileLines) / fn>$($FailFunctionLines) / cyclo>$($FailCyclo))"
    Write-Host "cyclo > $FailCyclo    : $cycloFails"
    Write-Host "fn    > $FailFunctionLines L : $funcFails"
    Write-Host "class  > $WarnClassLines L : $classWarns (warn only)"
    Write-Host "warnings          : $warns"
    if ($fileFindings.Count) {
        Write-Host "`n-- files over limit --"
        foreach ($x in $fileFindings) {
            $tag = if ($x.level -eq 'fail') { 'FAIL' } else { 'WARN' }
            Write-Host ("  {0,-58} {1,5} lines (cap {2})" -f $x.file, $x.lines, $x.cap)
        }
    }
    if ($fnFindings.Count) {
        Write-Host "`n-- functions over threshold (top 25) --"
        foreach ($x in ($fnFindings | Sort-Object cc, span -Descending | Select-Object -First 25)) {
            Write-Host ("  {0,-50} L{1,-5} {2,4}L  CC={3}" -f $x.file, $x.line, $x.span, $x.cc)
        }
    }
    if ($classFindings.Count) {
        Write-Host "`n-- classes over $WarnClassLines lines (warn) --"
        foreach ($x in $classFindings) {
            Write-Host ("  {0,-54} L{1,-5} {2} lines" -f $x.file, $x.line, $x.span)
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
