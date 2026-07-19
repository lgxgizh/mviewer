# M13 Phase 2 / review P3 — Benchmark Dashboard generator.
# Parses mviewer_bench result logs (result_*.txt) into history.csv and renders
# an index.html trend page so "did performance drop in the last 3 months?" is
# answerable. Additive script only — no core change. (Subtraction rule.)
param(
    [string]$ResultDir = 'D:\mviewer_bench_data',
    [string]$OutDir    = 'D:\mviewer\benchmark\report'
)

$ErrorActionPreference = 'Continue'

if (-not (Test-Path $ResultDir)) { Write-Host "no result dir: $ResultDir"; exit 1 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Collect rows: one per result_*.txt. Columns derived from [PASS] B* lines.
$rows = @()
foreach ($f in (Get-ChildItem $ResultDir -Filter 'result_*.txt' | Sort-Object Name)) {
    $text = Get-Content $f.FullName -Raw
    $row = [ordered]@{}
    $row['file'] = $f.Name
    # corpus-size from header "corpus-size=NNN"
    if ($text -match 'corpus-size=(\d+)') { $row['corpus'] = $Matches[1] }
    # timestamp = file mtime
    $row['date'] = $f.LastWriteTime.ToString('yyyy-MM-dd')
    # B2 first thumbnail (take the COLD total ms)
    if ($text -match 'B2 first_thumbnail_ms=([\d.]+)') { $row['first_thumb_ms'] = $Matches[1] }
    # B3 jpeg decode p50
    if ($text -match 'B3 decode_p50_ms_jpeg=([\d.]+)') { $row['decode_jpeg_p50_ms'] = $Matches[1] }
    # B4 thumbnails/sec
    if ($text -match 'B4 thumbnails_per_sec=([\d.]+)') { $row['thumbs_per_sec'] = $Matches[1] }
    # B7/B8 switch warm p50
    if ($text -match 'B7 switch_warm_p50_ms=([\d.]+)') { $row['switch_warm_p50_ms'] = $Matches[1] }
    if ($text -match 'B8 switch_p50_ms=([\d.]+)') { $row['switch_p50_ms'] = $Matches[1] }
    # B6 peak cache bytes
    if ($text -match 'B6 peak_cache_bytes=([\d.eE+]+)') { $row['peak_cache_bytes'] = $Matches[1] }
    # B9 rss (may be 0 on builds without psapi; record as-is)
    if ($text -match 'finalRssMB=(\d+)') { $row['rss_mb'] = $Matches[1] }
    $rows += $row
}

if ($rows.Count -eq 0) { Write-Host "no benchmark result files found"; exit 1 }

# Write history.csv
$cols = @('date','file','corpus','first_thumb_ms','decode_jpeg_p50_ms','thumbs_per_sec','switch_warm_p50_ms','switch_p50_ms','peak_cache_bytes','rss_mb')
$csv = Join-Path $OutDir 'history.csv'
$lines = @(($cols -join ','))
foreach ($r in $rows) {
    $vals = $cols | ForEach-Object { if ($r.Contains($_)) { $r[$_] } else { '' } }
    $lines += ($vals -join ',')
}
Set-Content -Path $csv -Value $lines
Write-Host "wrote $csv ($($rows.Count) rows)"

# Render index.html (self-contained, no external deps).
$html = Join-Path $OutDir 'index.html'
$table = ($rows | ForEach-Object {
    $cells = $cols | ForEach-Object { "<td>$(if($_.Contains($_)){$_[$_]}else{''})</td>" }
    "<tr>$($cells -join '')</tr>"
}) -join "`n"

$htmlBody = @"
<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>MViewer Benchmark Dashboard</title>
<style>body{font-family:system-ui,Segoe UI,Arial;margin:2rem;color:#222}
h1{font-size:1.4rem}h2{font-size:1.1rem;margin-top:2rem}
table{border-collapse:collapse;margin-top:1rem}td,th{border:1px solid #ccc;padding:.4rem .7rem;font-size:.85rem;text-align:right}
th{background:#f0f0f0}.note{color:#666;font-size:.85rem}
.metric{display:inline-block;margin:.4rem 1rem .4rem 0}
</style></head>
<body>
<h1>MViewer Benchmark Dashboard</h1>
<p class="note">Generated from <code>$ResultDir</code>. Each row = one <code>mviewer_bench</code> run.
Re-run the harness and regenerate to compare history.</p>
<h2>History (CSV)</h2>
<table><tr>$(($cols | ForEach-Object { "<th>$_</th>" }) -join '')</tr>
$table
</table>
<h2>Reading the trend</h2>
<ul>
<li><b>first_thumb_ms</b> — review budget &lt;300ms cold. Lower is better.</li>
<li><b>decode_jpeg_p50_ms</b> — JPEG decode median. Lower is better.</li>
<li><b>thumbs_per_sec</b> — thumbnail throughput. Higher is better.</li>
<li><b>switch_*_p50_ms</b> — image-switch latency (warm/preloaded). Lower is better.</li>
<li><b>peak_cache_bytes</b> / <b>rss_mb</b> — memory. rss_mb may read 0 on builds without psapi.</li>
</ul>
<p class="note">To add a data point: run <code>mviewer_bench --corpus-size N --enforce</code>,
copy its stdout to <code>$ResultDir/result_&lt;size&gt;.txt</code>, then re-run this script.</p>
</body></html>
"@
Set-Content -Path $html -Value $htmlBody
Write-Host "wrote $html"
Write-Host "DASHBOARD_OK rows=$($rows.Count)"
