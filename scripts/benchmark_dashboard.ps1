# M13 Phase 2 / M21 — Benchmark Dashboard generator.
# Parses mviewer_bench result logs (result_*.txt) into history.csv and renders
# an index.html trend page with inline sparklines (no external deps).
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
    $text = Get-Content $f.FullName -Raw -Encoding UTF8
    $row = [ordered]@{}
    $row['file'] = $f.Name
    if ($text -match 'corpus-size=(\d+)') { $row['corpus'] = $Matches[1] }
    $row['date'] = $f.LastWriteTime.ToString('yyyy-MM-dd')
    if ($text -match 'B2 first_thumbnail_ms=([\d.]+)') { $row['first_thumb_ms'] = $Matches[1] }
    if ($text -match 'B3 decode_p50_ms_jpeg=([\d.]+)') { $row['decode_jpeg_p50_ms'] = $Matches[1] }
    if ($text -match 'B4 thumbnails_per_sec=([\d.]+)') { $row['thumbs_per_sec'] = $Matches[1] }
    if ($text -match 'B7 switch_warm_p50_ms=([\d.]+)') { $row['switch_warm_p50_ms'] = $Matches[1] }
    if ($text -match 'B8 switch_p50_ms=([\d.]+)') { $row['switch_p50_ms'] = $Matches[1] }
    if ($text -match 'B6 peak_cache_bytes=([\d.eE+]+)') { $row['peak_cache_bytes'] = $Matches[1] }
    if ($text -match 'finalRssMB=(\d+)') { $row['rss_mb'] = $Matches[1] }
    $rows += ,$row
}

if ($rows.Count -eq 0) { Write-Host "no benchmark result files found"; exit 1 }

$cols = @('date','file','corpus','first_thumb_ms','decode_jpeg_p50_ms','thumbs_per_sec','switch_warm_p50_ms','switch_p50_ms','peak_cache_bytes','rss_mb')
$csv = Join-Path $OutDir 'history.csv'
$lines = @(($cols -join ','))
foreach ($r in $rows) {
    $vals = foreach ($c in $cols) {
        if ($r.Contains($c)) { $r[$c] } else { '' }
    }
    $lines += ($vals -join ',')
}
Set-Content -Path $csv -Value $lines -Encoding UTF8
Write-Host "wrote $csv ($($rows.Count) rows)"

# Build table rows with correct $_ scoping (M21 fix).
$tableRows = New-Object System.Collections.Generic.List[string]
foreach ($r in $rows) {
    $cells = foreach ($c in $cols) {
        $v = if ($r.Contains($c)) { $r[$c] } else { '' }
        "<td>$v</td>"
    }
    $tableRows.Add("<tr>$($cells -join '')</tr>")
}
$table = $tableRows -join "`n"

# JSON series for sparklines (numeric columns only).
function Get-Series([string]$key) {
    $vals = @()
    foreach ($r in $rows) {
        if ($r.Contains($key) -and $r[$key] -match '^[\d.eE+-]+$') {
            $vals += [double]$r[$key]
        } else {
            $vals += $null
        }
    }
    return ($vals | ConvertTo-Json -Compress)
}

$seriesJson = @{
    first_thumb_ms     = (Get-Series 'first_thumb_ms')
    decode_jpeg_p50_ms = (Get-Series 'decode_jpeg_p50_ms')
    thumbs_per_sec     = (Get-Series 'thumbs_per_sec')
    switch_p50_ms      = (Get-Series 'switch_p50_ms')
    peak_cache_bytes   = (Get-Series 'peak_cache_bytes')
} | ConvertTo-Json -Compress

$html = Join-Path $OutDir 'index.html'
$th = ($cols | ForEach-Object { "<th>$_</th>" }) -join ''

$htmlBody = @"
<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>MViewer Benchmark Dashboard</title>
<style>
body{font-family:system-ui,Segoe UI,Arial;margin:2rem;color:#222;background:#fafafa}
h1{font-size:1.4rem}h2{font-size:1.1rem;margin-top:2rem}
table{border-collapse:collapse;margin-top:1rem;background:#fff}
td,th{border:1px solid #ccc;padding:.4rem .7rem;font-size:.85rem;text-align:right}
th{background:#f0f0f0}.note{color:#666;font-size:.85rem}
.charts{display:flex;flex-wrap:wrap;gap:1.2rem;margin:1rem 0}
.chart{background:#fff;border:1px solid #ddd;border-radius:6px;padding:.6rem .8rem}
.chart h3{margin:0 0 .4rem;font-size:.9rem;color:#444}
canvas{display:block}
</style></head>
<body>
<h1>MViewer Benchmark Dashboard</h1>
<p class="note">Generated from <code>$ResultDir</code>. Each row = one <code>mviewer_bench</code> run.
Inline sparklines use pure Canvas (no CDN).</p>

<h2>Trends</h2>
<div class="charts" id="charts"></div>

<h2>History (CSV)</h2>
<table><tr>$th</tr>
$table
</table>

<h2>Reading the trend</h2>
<ul>
<li><b>first_thumb_ms</b> - review budget &lt;300ms cold. Lower is better.</li>
<li><b>decode_jpeg_p50_ms</b> - JPEG decode median. Lower is better.</li>
<li><b>thumbs_per_sec</b> - thumbnail throughput. Higher is better.</li>
<li><b>switch_*_p50_ms</b> - image-switch latency. Lower is better.</li>
<li><b>peak_cache_bytes</b> / <b>rss_mb</b> - memory.</li>
</ul>

<script>
const series = $seriesJson;
const metrics = [
  {key:'first_thumb_ms', label:'First thumb (ms)', lowerBetter:true},
  {key:'decode_jpeg_p50_ms', label:'JPEG decode p50 (ms)', lowerBetter:true},
  {key:'thumbs_per_sec', label:'Thumbs / sec', lowerBetter:false},
  {key:'switch_p50_ms', label:'Switch p50 (ms)', lowerBetter:true},
  {key:'peak_cache_bytes', label:'Peak cache (bytes)', lowerBetter:true}
];
function spark(canvas, values, lowerBetter) {
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0,0,w,h);
  const nums = values.filter(v => v !== null && !isNaN(v));
  if (nums.length < 1) {
    ctx.fillStyle = '#999'; ctx.font = '12px sans-serif';
    ctx.fillText('no data', 8, h/2); return;
  }
  const min = Math.min(...nums), max = Math.max(...nums);
  const span = (max - min) || 1;
  ctx.strokeStyle = '#ddd'; ctx.beginPath();
  ctx.moveTo(0, h-1); ctx.lineTo(w, h-1); ctx.stroke();
  ctx.strokeStyle = lowerBetter ? '#c44' : '#2a7';
  ctx.lineWidth = 2; ctx.beginPath();
  let started = false;
  values.forEach((v, i) => {
    if (v === null || isNaN(v)) return;
    const x = values.length === 1 ? w/2 : i * (w-4) / (values.length-1) + 2;
    const y = h - 4 - ((v - min) / span) * (h - 8);
    if (!started) { ctx.moveTo(x, y); started = true; }
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
  // last point
  const last = nums[nums.length-1];
  ctx.fillStyle = '#333'; ctx.font = '11px sans-serif';
  ctx.fillText(String(last), 4, 12);
}
const root = document.getElementById('charts');
metrics.forEach(m => {
  const box = document.createElement('div'); box.className = 'chart';
  const title = document.createElement('h3'); title.textContent = m.label;
  const c = document.createElement('canvas'); c.width = 220; c.height = 64;
  box.appendChild(title); box.appendChild(c); root.appendChild(box);
  spark(c, series[m.key] || [], m.lowerBetter);
});
</script>
</body></html>
"@
Set-Content -Path $html -Value $htmlBody -Encoding UTF8
Write-Host "wrote $html"
Write-Host "DASHBOARD_OK rows=$($rows.Count)"
