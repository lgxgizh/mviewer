<#
.SYNOPSIS
    Runs the MViewer release checklist (docs/release/RELEASE_CHECKLIST.md) end to
    end and produces a PASS/FAIL report. This automates the 7-step manual
    checklist so a release candidate can be validated with a single command.

.DESCRIPTION
    Executes the release gate steps defined in RELEASE_CHECKLIST.md:
      1. Build (Release)
      2. Core tests (ctest, via build.ps1 Test)
      3. MViewer --selftest self-diagnostics
      4. Performance regression gate (mviewer_bench --smoke / --enforce)
      5. Crash-diagnostics env var (informational)
      6. Packaging (opt-in via -Package)
      7. Release manifest (changelog / version verification)
    Writes release_checklist_report.md and returns a non-zero exit code if any
    hard step fails.

.PARAMETER Package
    Also run the packaging step (scripts/package_release.ps1) if present.

.PARAMETER SkipBench
    Skip the performance gate (mviewer_bench) — useful for quick local checks.

.PARAMETER Steps
    Comma-separated subset of step numbers to run, e.g. "1,2,4". Default: all.

.EXAMPLE
    pwsh -ExecutionPolicy Bypass -File scripts/run_release_checklist.ps1
#>
[CmdletBinding()]
param(
    [switch] $Package,
    [switch] $SkipBench,
    [string] $Steps = ""
)

$ErrorActionPreference = 'Stop'
$repo = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $repo

$report = @()
$overall = $true

function Log-Step([string]$n, [string]$name) { Write-Host "`n=== Step $n : $name ===" -ForegroundColor Cyan }
function Result([string]$status, [string]$detail) {
    $color = @{ PASS='Green'; FAIL='Red'; WARN='Yellow'; SKIP='DarkGray' }[$status]
    Write-Host "[$status] $detail" -ForegroundColor $color
    $script:report += "| $status | $detail |"
    if ($status -eq 'FAIL') { $script:overall = $false }
}

function Find-Binary([string]$name) {
    $cands = @(
        "$repo\bin\$name",
        "$repo\bin\Release\$name",
        "$repo\bin\Debug\$name",
        "$repo\build_msvc\bin\$name",
        "$repo\build_msvc\bin\Release\$name",
        "$repo\build_msvc\bin\Debug\$name"
    )
    foreach ($c in $cands) { if (Test-Path $c) { return $c } }
    return $null
}

function Invoke-Step([string]$label, [scriptblock]$sb) {
    try { & $sb }
    catch {
        Result FAIL "$label : $_"
    }
}

$want = @{}
if ($Steps -ne '') { $Steps.Split(',') | ForEach-Object { $want[$_.Trim()] = $true } }
function Wanted([string]$n) { return ($want.Count -eq 0) -or $want.ContainsKey($n) }

# ── Step 1: Build Release ────────────────────────────────────────────────
if (Wanted '1') {
    Log-Step '1' '构建 Release'
    Invoke-Step 'build' {
        & powershell -ExecutionPolicy Bypass -File "$repo\build.ps1" Release
        if ($LASTEXITCODE -ne 0) { Result FAIL 'build.ps1 Release 失败 (exit ' + $LASTEXITCODE + ')' }
        else { Result PASS 'build.ps1 Release 成功' }
    }
}

# ── Step 2: Core tests (ctest) ───────────────────────────────────────────
if (Wanted '2') {
    Log-Step '2' '运行核心测试 (ctest)'
    Invoke-Step 'test' {
        & powershell -ExecutionPolicy Bypass -File "$repo\build.ps1" Test
        if ($LASTEXITCODE -ne 0) { Result FAIL 'build.ps1 Test 失败 (exit ' + $LASTEXITCODE + ')' }
        else { Result PASS 'build.ps1 Test 全绿' }
    }
}

# ── Step 3: MViewer --selftest ───────────────────────────────────────────
if (Wanted '3') {
    Log-Step '3' '运行 MViewer --selftest'
    $mv = Find-Binary 'mviewer.exe'
    if ($null -eq $mv) {
        Result WARN '未找到 mviewer.exe (先运行 Step 1 构建)'
    }
    else {
        Invoke-Step 'selftest' {
            & $mv --selftest
            if ($LASTEXITCODE -ne 0) { Result FAIL 'mviewer --selftest 返回 ' + $LASTEXITCODE }
            else { Result PASS 'mviewer --selftest 通过' }
        }
    }
}

# ── Step 4: Performance regression gate ─────────────────────────────────
if (Wanted '4' -and -not $SkipBench) {
    Log-Step '4' '性能回归门禁 (mviewer_bench --smoke / --enforce)'
    $bench = Find-Binary 'mviewer_bench.exe'
    if ($null -eq $bench) {
        Result WARN '未找到 mviewer_bench.exe (Step 1 应已构建)'
    }
    else {
        Invoke-Step 'bench-smoke' {
            & $bench --smoke
            if ($LASTEXITCODE -ne 0) { Result FAIL 'mviewer_bench --smoke 失败 (exit ' + $LASTEXITCODE + ')' }
            else { Result PASS 'mviewer_bench --smoke 通过' }
        }
        Invoke-Step 'bench-enforce' {
            & $bench --enforce
            if ($LASTEXITCODE -ne 0) { Result FAIL 'mviewer_bench --enforce 未达性能预算' }
            else { Result PASS 'mviewer_bench --enforce 达预算' }
        }
    }
}
elseif ($SkipBench) { Result SKIP 'Step 4 跳过 (-SkipBench)' }

# ── Step 5: Crash-diagnostics env var (informational) ────────────────────
if (Wanted '5') {
    Log-Step '5' '崩溃诊断环境变量 (MVIEWER_CRASH_DIAG)'
    $v = $env:MVIEWER_CRASH_DIAG
    if ($v -eq '1') {
        Result PASS "MVIEWER_CRASH_DIAG=1 已设置 (崩溃转储已开启)"
    }
    else {
        Result WARN "MVIEWER_CRASH_DIAG 未设置；发布前建议 set MVIEWER_CRASH_DIAG=1 后再跑一次冒烟"
    }
}

# ── Step 6: Packaging (opt-in) ───────────────────────────────────────────
if (Wanted '6') {
    Log-Step '6' '打包发布'
    $pkg = Join-Path $repo 'scripts\package_release.ps1'
    if (-not (Test-Path $pkg)) {
        Result SKIP 'scripts/package_release.ps1 不存在'
    }
    elseif (-not $Package) {
        Result SKIP '打包未启用 (加 -Package 运行)'
    }
    else {
        Invoke-Step 'package' {
            & powershell -ExecutionPolicy Bypass -File $pkg
            if ($LASTEXITCODE -ne 0) { Result FAIL '打包失败 (exit ' + $LASTEXITCODE + ')' }
            else { Result PASS '打包成功' }
        }
    }
}

# ── Step 7: Release manifest ─────────────────────────────────────────────
if (Wanted '7') {
    Log-Step '7' '生成发布清单 (changelog / version)'
    $manifest = Join-Path $repo 'scripts\release_manifest.ps1'
    if (-not (Test-Path $manifest)) {
        Result WARN 'scripts/release_manifest.ps1 不存在'
    }
    else {
        Invoke-Step 'manifest' {
            & powershell -ExecutionPolicy Bypass -File $manifest
            if ($LASTEXITCODE -ne 0) { Result FAIL 'release_manifest.ps1 校验失败' }
            else { Result PASS 'release_manifest.ps1 通过' }
        }
    }
}

# ── Report ───────────────────────────────────────────────────────────────
$date = Get-Date -Format 'yyyy-MM-dd HH:mm'
$md = @"
# MViewer 发布检查清单报告

生成时间: $date
总体结果: $(if ($overall) { 'PASS' } else { 'FAIL' })

| 结果 | 步骤 |
|------|------|
$($report -join "`n")

## 说明
- 本脚本自动执行 docs/release/RELEASE_CHECKLIST.md 的 7 个步骤。
- FAIL 表示硬门禁未通过（构建/测试/性能预算/清单）；WARN 为需人工确认；SKIP 为未启用或缺失。
- 任意 FAIL 会使进程以非零退出码结束。
"@
$md | Out-File -FilePath (Join-Path $repo 'release_checklist_report.md') -Encoding utf8
Write-Host "`n报告已写入 release_checklist_report.md" -ForegroundColor Cyan
Write-Host "总体结果: $(if ($overall) { 'PASS' } else { 'FAIL' })" -ForegroundColor $(if ($overall) { 'Green' } else { 'Red' })

exit $(if ($overall) { 0 } else { 1 })
