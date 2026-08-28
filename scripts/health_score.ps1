# M23 Build-Health — Project Health Score.
#
# The centerpiece of the engineering-system. It aggregates every gate into a
# single, always-current view of project health, so every PR / nightly run can
# prove: features did not regress, performance did not drop, architecture did
# not rot, code quality keeps rising.
#
# Dimensions (each 0-100; overall = weighted mean of available dimensions):
#   Build        build succeeded (inferred from a runnable test artifact)
#   Unit Test    ctest pass rate
#   Performance  benchmark hard-gate pass rate (vs performance_budget.json)
#   Code Quality clang-tidy + cppcheck error/warning pressure
#   Coverage     per-module line coverage (Core weighted highest)
#   Memory       stability scenario: RSS / handle growth across N iterations
#   Architecture layer-dependency rule violations (architecture_gate.ps1)
#   Complexity   file-size / function-length limit violations (complexity_gate.ps1)
#
# The two gate scripts are ALWAYS executed (so the health job always has data);
# the rest are read from optional artifacts supplied via parameters / discovered
# in conventional locations. Missing artifacts are marked "n/a" (neutral) rather
# than failing — a gate that was not run should not penalize the score.
#
# Output: writes docs/quality/dashboard.md (committed snapshot) and prints a
# short summary. Exit 0 always (health is a report, never a build breaker).

[CmdletBinding()]
param(
    [string]$Repo = (Get-Location).Path,
    [string]$CtestXml   = '',
    [string]$Cppcheck   = '',
    [string]$ClangTidy  = '',
    [string]$BenchmarkJson = '',
    [string]$CoverageJson   = '',
    [string]$StabilityJson  = '',
    [string]$Dashboard  = (Join-Path $Repo 'docs/quality/dashboard.md'),
    [string]$OutJson    = (Join-Path $Repo 'build_health.json')
)

$ErrorActionPreference = 'Stop'

$scriptRoot = if ($PSScriptRoot) {
    $PSScriptRoot
} elseif ($MyInvocation.MyCommand.Path) {
    Split-Path -Parent $MyInvocation.MyCommand.Path
} else {
    Join-Path $Repo 'scripts'
}

function Write-Utf8NoBom([string]$Path, [string]$Value) {
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Invoke-Gate($script) {
    $json = & $script -Repo $Repo -Json 2>$null | Out-String
    try { return ($json | ConvertFrom-Json) } catch { return $null }
}

# ---- always-run gates --------------------------------------------------------
$complexity   = Invoke-Gate (Join-Path $scriptRoot 'complexity_gate.ps1')
$architecture = Invoke-Gate (Join-Path $scriptRoot 'architecture_gate.ps1')

# ---- helpers -----------------------------------------------------------------
function SafeFile($p) { if ($p -and (Test-Path $p)) { return $p } ; return '' }

# ---- Build & Unit Test (from ctest junit) -----------------------------------
$build = $null; $tests = $null
$ct = SafeFile $CtestXml
if (-not $ct) {
    $ct = Get-ChildItem -Path (Join-Path $Repo 'build_msvc') -Recurse -Filter 'test-results*.xml' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ct) { $ct = $ct.FullName }
}
if ($ct) {
    try {
        [xml]$junit = Get-Content -Path $ct -Raw -Encoding UTF8
        $tc = $junit.SelectNodes('//testcase')
        $fail = 0; $err = 0
        foreach ($c in $tc) {
            if ($c.SelectSingleNode('./*[local-name()="failure"]')) { $fail++ }
            if ($c.SelectSingleNode('./*[local-name()="error"]'))    { $err++ }
        }
        $total = $tc.Count
        $run = $total - $fail - $err
        $build = 100.0
        $tests = if ($total -gt 0) { [math]::Round(100.0 * $run / $total, 1) } else { $null }
        $testsTotal = $total; $testsFailed = ($fail + $err)
    } catch {
        Write-Host ("WARN: ctest xml parse failed: {0}" -f $_.Exception.Message)
        $tests = $null; $build = $null
    }
}
else { $testsTotal = $null; $testsFailed = $null }

# ---- Code Quality (clang-tidy + cppcheck) -----------------------------------
$cq = $null
$ctErr = 0; $ctWarn = 0; $ckErr = 0; $ckWarn = 0
$ck = SafeFile $Cppcheck
if ($ck) {
    Get-Content $ck | ForEach-Object {
        if ($_ -match ':\s*(error|warning):') {
            if ($Matches[1] -eq 'error') { $ckErr++ } else { $ckWarn++ }
        }
    }
}
$tidy = SafeFile $ClangTidy
if ($tidy) {
    Get-Content $tidy | ForEach-Object {
        if ($_ -match ':\s*error:') { $ctErr++ }
        elseif ($_ -match ':\s*warning:') { $ctWarn++ }
    }
}
if (($ckErr + $ckWarn + $ctErr + $ctWarn) -gt 0) {
    $penalty = $ctErr * 5 + $ckErr * 10 + ($ctWarn + $ckWarn) * 1
    $cq = [math]::Max(0, 100 - $penalty)
}

# ---- Performance (benchmark hard-gate) --------------------------------------
$perf = $null
$bj = SafeFile $BenchmarkJson
if ($bj) {
    try {
        $b = Get-Content $bj -Raw | ConvertFrom-Json
        $passed = if ($null -ne $b.passed) { $b.passed } else { 0 }
        $total  = if ($null -ne $b.total)  { $b.total }  else { 0 }
        if ($total -gt 0) { $perf = [math]::Round(100.0 * $passed / $total, 1) }
        elseif ($b.scenarios) {
            $p = ($b.scenarios | Where-Object { $_.pass -eq $true }).Count
            $t = $b.scenarios.Count
            if ($t -gt 0) { $perf = [math]::Round(100.0 * $p / $t, 1) }
        }
    } catch {}
}

# ---- Coverage (per-module) --------------------------------------------------
$cover = $null; $covCore = $null; $covUi = $null; $covCompare = $null
$cj = SafeFile $CoverageJson
if ($cj) {
    try {
        $c = Get-Content $cj -Raw | ConvertFrom-Json
        $covCore    = [double]($c.core);    $covUi = [double]($c.ui)
        $covCompare = [double]($c.compare); $cover = [double]($c.overall)
        if (-not $cover) { $cover = [math]::Round($covCore*0.6 + $covUi*0.2 + $covCompare*0.2, 1) }
    } catch {}
}

# ---- Memory (stability scenario) --------------------------------------------
$mem = $null
$sj = SafeFile $StabilityJson
if ($sj) {
    try {
        $s = Get-Content $sj -Raw | ConvertFrom-Json
        if ($s.pass -eq $false) { $mem = [math]::Max(0, 100 - [double]($s.growthPenalty)) }
        else { $mem = 100.0 }
    } catch {}
}

# ---- Architecture & Complexity scores ---------------------------------------
$arch = if ($architecture) { [math]::Max(0, 100 - [math]::Min($architecture.warnings, 20) * 5) } else { $null }
$cx = if ($complexity) {
    [math]::Max(0, 100 - $complexity.hardFails * 15 - [math]::Min($complexity.warnings, 20) * 2)
} else { $null }

# ---- Known Issues (Bug Gate) ----------------------------------------------
# Driven by scripts/known_issues_gate.ps1 + docs/known_issues/known_issues.json.
# 100 when every OPEN issue links an existing regression test; penalized by 25
# per unlinked open issue (a defect "fixed" without a regression test).
$ki = $null; $kiScore = $null
$kiScript = Join-Path $PSScriptRoot 'known_issues_gate.ps1'
if (Test-Path $kiScript) {
    try {
        $kij = (& $kiScript -Json 2>$null | Out-String | ConvertFrom-Json)
        $ki = $kij
        if ($kij) {
            $open = if ($null -ne $kij.openIssues) { [int]$kij.openIssues } else { 0 }
            $unlinked = if ($kij.unlinkedIssues) { $kij.unlinkedIssues.Count } else { 0 }
            $kiScore = if ($open -eq 0) { 100.0 } else { [math]::Max(0, 100 - $unlinked * 25) }
        }
    } catch { $ki = $null }
}

# ---- assemble dimensions -----------------------------------------------------
$dims = [ordered]@{
    Build        = $build
    'Unit Test'  = $tests
    Performance  = $perf
    'Code Quality' = $cq
    Coverage     = $cover
    Memory       = $mem
    Architecture = $arch
    Complexity   = $cx
    Regression   = $kiScore
}
$weights = [ordered]@{
    Build=10; 'Unit Test'=20; Performance=15; 'Code Quality'=15;
    Coverage=10; Memory=10; Architecture=10; Complexity=10; Regression=5
}
$sum = 0.0; $wsum = 0.0
foreach ($k in $dims.Keys) {
    $v = $dims[$k]
    if ($null -ne $v) { $sum += $v * $weights[$k]; $wsum += $weights[$k] }
}
$overall = if ($wsum -gt 0) { [math]::Round($sum / $wsum, 1) } else { $null }

# ---- render dashboard.md -----------------------------------------------------
$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$sha = (git -C $Repo rev-parse --short HEAD 2>$null) + ''
$grade = if ($overall -ge 90) { 'A' } elseif ($overall -ge 80) { 'B' } elseif ($overall -ge 70) { 'C' } elseif ($overall -ge 60) { 'D' } else { 'F' }

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine('# MViewer - Project Health Dashboard')
[void]$sb.AppendLine()
[void]$sb.AppendLine('> Auto-generated by `scripts/health_score.ps1`. Snapshot: **' + $stamp + '** - commit `' + $sha + '`')
[void]$sb.AppendLine()
[void]$sb.AppendLine("## Health Score: $overall / 100  (grade **$grade**)")
[void]$sb.AppendLine()
[void]$sb.AppendLine('| Dimension | Score | Status |')
[void]$sb.AppendLine('|---|---:|---|')
foreach ($k in $dims.Keys) {
    $v = $dims[$k]
    if ($null -eq $v) { [void]$sb.AppendLine("| $k | n/a | not measured this run |") ; continue }
    $badge = if ($v -ge 90) { 'PASS' } elseif ($v -ge 75) { 'OK' } elseif ($v -ge 60) { 'WATCH' } else { 'FAIL' }
    [void]$sb.AppendLine("| $k | $v | $badge |")
}
[void]$sb.AppendLine()
[void]$sb.AppendLine('## Gate Details')
[void]$sb.AppendLine()
[void]$sb.AppendLine('### Complexity')
if ($complexity) {
    [void]$sb.AppendLine("- hard fails: **$($complexity.hardFails)** - warnings: **$($complexity.warnings)**")
    [void]$sb.AppendLine("- cyclomatic > 25: **$($complexity.cycloFails)** - function > 120 lines: **$($complexity.funcFailLines)** - class > 1000 lines: **$($complexity.classWarnings)** (warn only)")
    [void]$sb.AppendLine('- gate truth: hard-fail zero is PASS; advisory warnings are accepted baseline debt and remain maintenance feedback')
    [void]$sb.AppendLine('- rules: file > 800 lines FAIL - function > 120 lines FAIL - cyclomatic > 25 FAIL - function > 80 / cyclo > 15 WARN - class > 1000 WARN (ADR-014 frozen caps for mainwindow/compareworkspace/thumbnailpanel)')
    if ($complexity.files.Count) {
        [void]$sb.AppendLine('- files over limit:')
        foreach ($x in $complexity.files) { [void]$sb.AppendLine('  - `' + $x.file + '` - ' + $x.lines + ' lines (cap ' + $x.cap + ', ' + $x.level + ')') }
    }
    if ($complexity.functions.Count) {
        [void]$sb.AppendLine('- most complex functions (top 10 by cyclomatic):')
        foreach ($x in ($complexity.functions | Sort-Object cc, span -Descending | Select-Object -First 10)) {
            [void]$sb.AppendLine("  - ``$($x.file)`` L$($x.line) - $($x.span) lines / CC=$($x.cc)")
        }
    }
} else { [void]$sb.AppendLine('- not run') }
[void]$sb.AppendLine()
[void]$sb.AppendLine('### Architecture')
if ($architecture) {
    [void]$sb.AppendLine("- advisory violations: **$($architecture.warnings)**")
    if ($architecture.violations.Count) {
        foreach ($v in ($architecture.violations | Sort-Object rule, file | Select-Object -First 20)) {
            [void]$sb.AppendLine('  - [' + $v.rule + '] `' + $v.file + '` L' + $v.line + ': ' + $v.message)
        }
    }
} else { [void]$sb.AppendLine('- not run') }
[void]$sb.AppendLine()
[void]$sb.AppendLine('### Coverage')
if ($null -ne $cover) {
    [void]$sb.AppendLine("- Core **$covCore%** - UI **$covUi%** - Compare **$covCompare%** - Overall **$cover%**")
    [void]$sb.AppendLine('- gate: Core < 85% -> FAIL (nightly)')
} else { [void]$sb.AppendLine('- not measured this run (nightly coverage job)') }
[void]$sb.AppendLine()
[void]$sb.AppendLine('### Memory / Stability')
if ($null -ne $mem) {
    [void]$sb.AppendLine("- stability score: **$mem**")
} else { [void]$sb.AppendLine('- not measured this run (nightly stability scenarios B9 / B17)') }
[void]$sb.AppendLine()
[void]$sb.AppendLine('### Known Issues (Bug Gate)')
if ($ki) {
    $open = if ($null -ne $ki.openIssues) { $ki.openIssues } else { 0 }
    $unlinked = if ($ki.unlinkedIssues) { $ki.unlinkedIssues.Count } else { 0 }
    [void]$sb.AppendLine("- total issues: **$($ki.totalIssues)** - open: **$open** - unlinked (no regression test): **$unlinked**")
    if ($ki.unlinkedIssues.Count) {
        [void]$sb.AppendLine('- UNLINKED (gate would FAIL):')
        foreach ($u in $ki.unlinkedIssues) { [void]$sb.AppendLine('  - ' + $u) }
    }
    [void]$sb.AppendLine('- DB: `docs/known_issues/known_issues.json` - policy: every open issue must link a regression test (ADR-015)')
} else { [void]$sb.AppendLine('- not run') }
[void]$sb.AppendLine()
[void]$sb.AppendLine('### Benchmark Trend')
[void]$sb.AppendLine('- rolling history + SVG sparklines: `benchmark/report/index.html` (regenerated nightly by `scripts/benchmark_trend.ps1`)')
[void]$sb.AppendLine('- baseline + +/-10% regression gate: `benchmark/performance_budget.json` (nightly `quality` job)')
[void]$sb.AppendLine()
[void]$sb.AppendLine('---')
[void]$sb.AppendLine('_Generated by the M23 Build-Health system. See QUALITY.md and docs/adr/ for the governing rules._')

Write-Utf8NoBom $Dashboard $sb.ToString().TrimEnd("`n", "`r")

$result = [ordered]@{
    overall = $overall
    grade   = $grade
    dimensions = $dims
    generated = $stamp
    commit = $sha
}
$resultJson = $result | ConvertTo-Json -Depth 6
Write-Utf8NoBom $OutJson $resultJson

# ---- stdout summary ----------------------------------------------------------
Write-Host "=== Project Health Score ==="
Write-Host ("Overall: {0} (grade {1})" -f $overall, $grade)
foreach ($k in $dims.Keys) {
    $v = $dims[$k]
    $vs = if ($null -eq $v) { 'n/a' } else { "$v" }
    Write-Host ("  {0,-14} {1}" -f $k, $vs)
}
Write-Host "Dashboard -> $Dashboard"
