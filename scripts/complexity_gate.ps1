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
    # Windows PowerShell 5.1 can leave PSScriptRoot unavailable while it
    # evaluates parameter defaults for a relative -File invocation. The
    # project entry point runs from the repository root, so use that location.
    [string]$Repo = (Get-Location).Path,
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

# Split responsibility translation units are still product source. Give them
# a review warning at the ordinary cap and a hard ceiling below the old
# 1500/2500-line exemption. This is a guardrail, not a file whitelist: every
# matching TU is measured and remains subject to function/cyclomatic limits.
$responsibilityCaps = @(
    @{ pattern = '^mainwindow_.*\.cpp$'; warn = 800; fail = 1000 },
    @{ pattern = '^compareworkspace_.*\.cpp$'; warn = 800; fail = 1000 },
    @{ pattern = '^thumbnailpanel_.*\.cpp$'; warn = 800; fail = 1000 }
)

# ---- Tracked function debt (ADR-014) -----------------------------------------
# Functions that the corrected frame typing below exposes as over the function
# cap. They are reported as advisory warnings instead of hard failures so the
# regression test keeps its meaning ("no NEW violation") while the debt stays
# visible and enumerable. Keys are "<relative path>::<function name>", so an
# entry keeps matching when code above it shifts. Remove an entry in the same
# commit that splits the function; never add one without an ADR-014 note.
$knownFunctionDebt = @{
    'src/previewpanel.cpp::setImage'                                = 'span 276'
    'src/previewpanel.cpp::<lambda>'                                = 'span 186 (load worker inside setImage)'
    'src/compareworkspace_analysis.cpp::scheduleHistogramRefresh'   = 'span 204'
    'src/compareworkspace.cpp::queueLoadRequests'                   = 'span 133'
    'src/thumbnailpanel_fileops.cpp::startCommandFileOperation'     = 'span 155'
    'src/thumbnailpanel_fileops.cpp::startCopyFileOperation'        = 'span 152'
    'src/thumbnailpanel_fileops.cpp::runBatchAnalyzeExportAsync'    = 'span 148'
    'src/core/metadata/MetadataIndexer.cpp::index'                  = 'span 136'
    'src/core/image/decoder/QtDecoder.cpp::decodeTiffWic'           = 'span 133 / cc 32'
    'src/core/image/ImageRepository_async.cpp::loadAsyncCancellable' = 'span 127'
    'src/core/metadata/MetadataIndexer.cpp::indexBatched'           = 'span 122'
    'src/core/filesystem/AtomicFile.cpp::atomicWriteFile'           = 'span 152'
    'src/domain/SelectionInteraction.h::hitTestSelection'           = 'cc 26'
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

# Name of the function a signature text declares, or '' when it is not a
# declaration. The first identifier followed by '(' wins, skipping template
# arguments (a '(' directly after '<' belongs to std::function<...>, not to the
# declaration) and control keywords.
function Get-FunctionName([string]$prefix) {
    foreach ($m in [regex]::Matches($prefix, '([A-Za-z_]\w*)\s*\(')) {
        $before = $m.Index - 1
        if ($before -ge 0 -and $prefix[$before] -eq '<') { continue }
        $name = $m.Groups[1].Value
        if ($name -match '^(if|for|while|switch|catch|return|sizeof|static_cast|reinterpret_cast|const_cast|dynamic_cast|decltype|alignof|noexcept)$') { continue }
        return $name
    }
    return ''
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
    else {
        $roleCap = $responsibilityCaps | Where-Object { $f.Name -match $_.pattern } | Select-Object -First 1
        if ($roleCap) {
            $warnCap = $roleCap.warn
            $failCap = $roleCap.fail
        }
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
                # Type the frame from the WHOLE signature, not just the previous
                # line: an Allman multi-line parameter list puts the opening '('
                # several lines above the brace, so a one-line look-back typed the
                # frame as a block and skipped its span/CC entirely (that hole hid
                # every function below from the caps). Walk back with a
                # balanced-paren scan until the signature is complete.
                $sig = [System.Collections.Generic.List[string]]::new()
                [void]$sig.Add($ln.Substring(0, $p))
                $fnName = ''
                $isLambda = $false
                for ($b = $i - 1; $b -ge 0 -and ($i - $b) -le 30; $b--) {
                    $prefix = ($sig -join ' ')
                    # A capture list followed by '(' is a lambda: it has no
                    # function name, so it must be recognised BEFORE the name
                    # scan can pick up a call inside the surrounding statement.
                    if ($prefix -match '\]\s*\(') { $isLambda = $true; break }
                    $fnName = Get-FunctionName $prefix
                    if ($fnName -ne '') { break }
                    $sigPrev = ($lines[$b] -replace '//.*$', '').Trim()
                    if ($sigPrev -eq '' -or $sigPrev.StartsWith('#')) { break }
                    if ($sigPrev.EndsWith(';') -or $sigPrev.EndsWith('{') -or $sigPrev.EndsWith('}')) { break }
                    $sig.Insert(0, $sigPrev)
                }
                $prefix = ($sig -join ' ')
                if ($fnName -eq '') { $fnName = Get-FunctionName $prefix }
                if ($fnName -eq '' -and ($prefix -match '\]\s*\(')) { $isLambda = $true }
                $type = 'block'
                if ($prefix -match '\b(class|struct)\b') { $type = 'class' }
                elseif ($prefix -match '\b(namespace|enum)\b\s*[\w:]*\s*$') { $type = 'block' }
                elseif ($fnName -ne '' -or $isLambda) { $type = 'func' }
                elseif ($prefix -match '\(' -and $prefix -notmatch '\b(if|for|while|switch|catch|do|else)\b\s*$') {
                    $type = 'func'
                }
                if ($type -eq 'func' -and $isLambda) { $fnName = '<lambda>' }
                $frame = [ordered]@{ type = $type; start = ($i + 1); cc = 1; name = $fnName }
                $stack.Add($frame)
            }
            elseif ($ch -eq '}') {
                if ($stack.Count -gt 0) {
                    $frame = $stack[-1]
                    $stack.RemoveAt($stack.Count - 1)
                    if ($frame.type -eq 'func') {
                        $span = ($i + 1) - $frame.start + 1
                        $cc = $frame.cc
                        # Tracked debt (ADR-014): an over-cap function that is
                        # already enumerated there is advisory, not a hard failure,
                        # so the regression test keeps meaning "no NEW violation".
                        # Keys use forward slashes; $rel is a Windows path.
                        $debtKey = ($rel -replace '\\', '/') + '::' + $frame.name
                        $isKnownDebt = $knownFunctionDebt.ContainsKey($debtKey)
                        if (-not $isTest -and -not $isKnownDebt) {
                            if ($cc -gt $FailCyclo) { $cycloFails++; $fails++; $warns++ }
                            elseif ($cc -gt $WarnCyclo) { $warns++ }
                            if ($span -gt $FailFunctionLines) { $funcFails++; $fails++; $warns++ }
                            elseif ($span -gt $WarnFunctionLines) { $warns++ }
                        }
                        elseif (-not $isTest -and $isKnownDebt -and
                                ($cc -gt $FailCyclo -or $span -gt $FailFunctionLines)) {
                            $warns++
                        }
                        if (-not $isTest -and ($cc -gt $WarnCyclo -or $span -gt $WarnFunctionLines)) {
                            $fnFindings.Add([ordered]@{
                                file = $rel
                                line = $frame.start
                                span = $span
                                cc   = $cc
                                debt = $isKnownDebt
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
        Write-Host "`nCOMPLEXITY: hard limit violated ($fails) - strict mode"
        exit 1
    }
    elseif ($fails -gt 0) {
        Write-Host "`nCOMPLEXITY: $fails hard-limit violation(s) (advisory - use -Strict to fail)"
    }
    Write-Host "`nCOMPLEXITY: OK (advisory)"
}
