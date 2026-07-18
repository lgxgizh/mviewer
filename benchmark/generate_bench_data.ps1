# Generate the P3 benchmark datasets: benchmark/data/{small,medium,large}.
#
# Uses the existing, tested corpus generator inside mviewer_bench (--emit-data),
# which writes deterministic gradient+noise JPEG/PNG/TIFF images. This makes the
# review's "benchmark/data/ small:100 / medium:1000 / large:10000" tiers
# reproducible without committing binaries to git.
#
# Usage (from repo root, after a Release build):
#   powershell -ExecutionPolicy Bypass -File benchmark/generate_bench_data.ps1
#
# By default it materializes small+medium into benchmark/data/ (kept on disk).
# `large` is ~2 GB; pass -Tiers large (or -All) to also generate it. Output goes
# to D: by default to avoid starving the C: system drive.

param(
    [string]$Tiers = "small,medium",   # comma-separated: small,medium,large,all
    [string]$OutRoot = "D:/mviewer_bench_data",  # where tiers are written
    [string]$BuildDir = "D:/mviewer/build_msvc"
)

$ErrorActionPreference = 'Stop'
$repo = 'D:\mviewer'
$bench = Join-Path $BuildDir 'bin\mviewer_bench.exe'
if (-not (Test-Path $bench)) {
    Write-Host "ERROR: bench binary not found at $bench — run build.ps1 Release first."
    exit 1
}
$env:PATH = "D:\QT\6.11.1\msvc2022_64\bin;D:\msvc\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64;$env:PATH"

$map = @{
    small  = 100
    medium = 1000
    large  = 10000
}
if ($Tiers -eq 'all') { $Tiers = 'small,medium,large' }
$tierList = $Tiers.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }

foreach ($t in $tierList) {
    if (-not $map.ContainsKey($t)) { Write-Host "SKIP unknown tier '$t'"; continue }
    $n = $map[$t]
    $dest = Join-Path $OutRoot $t
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Write-Host "=== generating tier '$t' (n=$n) -> $dest ==="
    $freeBefore = (Get-PSDrive D).Free
    & $bench --emit-data $dest --corpus-size $n 2>&1 | ForEach-Object { Write-Host $_ }
    $freeAfter = (Get-PSDrive D).Free
    $sizeMB = [math]::Round((Get-ChildItem $dest -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB)
    Write-Host "tier '$t' done: ~$sizeMB MB on disk; D: free $([math]::Round($freeBefore/1MB)) -> $([math]::Round($freeAfter/1MB)) MB"
}

Write-Host "=== benchmark/data generation complete ==="
Write-Host "To point the benchmark at a tier: MVIEWER_BENCH_TMP=<tier-dir> mviewer_bench --corpus-size <n>"
