#!/usr/bin/env pwsh
# =============================================================================
# MViewer clang-format guard
# -----------------------------------------------------------------------------
# Mirrors the CI "Format check" job's clang-format step locally so that format
# violations are caught BEFORE a push (instead of failing the PR gate later).
#
# Usage:
#   pwsh scripts/format-check.ps1            # check ALL C++ sources (CI mirror)
#   pwsh scripts/format-check.ps1 -Staged    # check only files staged in git
#                                           (used by the pre-commit hook)
#   pwsh scripts/format-check.ps1 -Fix       # apply clang-format -i in place
#   pwsh scripts/format-check.ps1 -Fix -Staged
#
# Exit code: 0 = clean, 1 = violations found (or clang-format missing).
#
# clang-format resolution order:
#   1. -ClangFormat <path>   (explicit)
#   2. clang-format on PATH
#   3. Common install locations (D:\LLVM\bin, C:\Program Files\LLVM\bin)
# =============================================================================
param(
    [switch]$Fix,
    [switch]$Staged,
    [string]$ClangFormat = ""
)

$ErrorActionPreference = 'Stop'

# --- locate clang-format ------------------------------------------------------
$cf = $ClangFormat
if (-not $cf) {
    $cf = (Get-Command clang-format -ErrorAction SilentlyContinue).Source
}
if (-not $cf) {
    $candidates = @(
        "D:\LLVM\bin\clang-format.exe",
        "C:\Program Files\LLVM\bin\clang-format.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $cf = $c; break }
    }
}
if (-not $cf) {
    Write-Host "::error:: clang-format not found. Install LLVM (e.g. 'pip install clang-format') or add it to PATH."
    exit 1
}

# --- collect files ------------------------------------------------------------
$repoRoot = (git rev-parse --show-toplevel)
if ($Staged) {
    $files = git -C $repoRoot diff --cached --name-only --diff-filter=ACM |
        Where-Object { $_ -match '\.(cpp|h)$' }
} else {
    $files = Get-ChildItem -Path $repoRoot -Recurse -Include *.cpp, *.h |
        Where-Object {
            $_.FullName -notmatch '[\\/]build(_msvc)?[\\/]' -and
            $_.FullName -notmatch '[\\/]build[\\/]' -and
            $_.FullName -notmatch '[\\/]\.git[\\/]' -and
            $_.FullName -notmatch '[\\/]Qt[\\/]' -and
            $_.FullName -notmatch '[\\/]Qt6[\\/]'
        } | ForEach-Object { $_.FullName }
}

if ($files.Count -eq 0) {
    Write-Host "format-check: no C++ files to check."
    exit 0
}

# --- fix mode ----------------------------------------------------------------
if ($Fix) {
    & $cf -i $files
    Write-Host "format-check: applied clang-format -i to $($files.Count) file(s)."
    exit 0
}

# --- dry-run check ------------------------------------------------------------
$bad = @()
foreach ($f in $files) {
    & $cf --dry-run --Werror $f 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { $bad += $f }
}

if ($bad.Count -gt 0) {
    Write-Host "::error:: format-check: $($bad.Count) file(s) not clang-format clean:"
    $bad | ForEach-Object { Write-Host "  $_" }
    Write-Host "Fix with: pwsh scripts/format-check.ps1 -Fix"
    exit 1
}

Write-Host "format-check: OK ($($files.Count) file(s) clean)."
exit 0
