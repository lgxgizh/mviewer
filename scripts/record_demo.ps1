# M18 demo asset generator.
# Produces a genuine animated GIF of the M18 workflow by rendering the REAL
# MainWindow (actual UI code) offscreen via Qt, grabbing three workflow states
# (directory open -> image selected with metadata -> live filename search), then
# stitching them with ffmpeg. The build/terminal session cannot reach the
# interactive display session, so a live screen recording is not possible here;
# this renders the authentic UI instead of faking frames.
param(
    [string]$RenderExe = "build_msvc/bin/mviewer_demo_render.exe",
    [string]$Assets = "demo_assets",
    [string]$Out = "dist/mviewer_demo.gif"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot | Split-Path -Parent
Push-Location $root

# Ensure the MViewer shared lib + Qt + UCRT are resolvable when launching the
# render exe (the terminal/MSYS PATH may not include them).
$bin = Resolve-Path "build_msvc/bin"
$qt = "D:\QT\6.11.1\msvc2022_64\bin"
$env:PATH = "$bin;$qt;" + $env:PATH

# Resolve output to an absolute path so frame-list paths are never ambiguous.
$Out = (Resolve-Path (Split-Path $Out) -ErrorAction SilentlyContinue).Path + "\" + [System.IO.Path]::GetFileName($Out)
if (-not (Test-Path $RenderExe)) { throw "render exe not found: $RenderExe" }
if (-not (Test-Path $Assets)) { throw "assets dir not found: $Assets" }
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffmpeg) { throw "ffmpeg not found on PATH (install: choco install ffmpeg)" }

New-Item -ItemType Directory -Force -Path (Split-Path $Out) | Out-Null

# Render the three states (offscreen) into the Out directory's parent dir.
$env:QT_QPA_PLATFORM = "offscreen"
& $RenderExe (Resolve-Path $Assets) (Split-Path $Out) 2>&1 | Out-Null
$env:QT_QPA_PLATFORM = ""

$s1 = Join-Path (Split-Path $Out) "demo_state1_dir.png"
$s2 = Join-Path (Split-Path $Out) "demo_state2_metadata.png"
$s3 = Join-Path (Split-Path $Out) "demo_state3_search.png"
if (-not ((Test-Path $s1) -and (Test-Path $s2) -and (Test-Path $s3))) {
    throw "render did not produce all three state PNGs"
}

# Stitch into an animated GIF: hold each state ~2.2s, loop.
$tmpList = Join-Path (Split-Path $Out) "_demo_frames.txt"
@"
file '$s1'
duration 2.2
file '$s2'
duration 2.2
file '$s3'
duration 2.2
"@ | Set-Content -NoNewline $tmpList

& ffmpeg -y -hide_banner -loglevel error `
    -f concat -safe 0 -i $tmpList `
    -vf "fps=2,scale=960:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" `
    $Out

# Static screenshot artifact (state 2: directory open + image selected with
# metadata panel populated) for README / release assets. Copy BEFORE the state
# PNGs are cleaned up below.
$shot = Join-Path (Split-Path $Out) "mviewer_screenshot.png"
Copy-Item $s2 $shot -Force
Write-Host "SCREENSHOT_OK: $shot"

Remove-Item $tmpList, $s1, $s2, $s3 -ErrorAction SilentlyContinue

if (Test-Path $Out) {
    Write-Host "DEMO_GIF_OK: $Out ($([math]::Round((Get-Item $Out).Length/1KB)) KB)"
} else {
    throw "GIF not produced"
}
Pop-Location
