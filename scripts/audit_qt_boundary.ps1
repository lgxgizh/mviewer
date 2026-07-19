# M12.5 Quality Audit — Qt-boundary scanner (5.1)
#
# Scans core/ and domain/ headers for Qt *UI* types that must NOT leak into the
# Qt-free layers (per AGENTS.md: "No Qt types in domain/ or core/ headers").
# Headers may include Qt *value* types that are required (e.g. <QString> for
# string interop is tolerated in core .cpp; but UI widget/painter/pixmap types
# are forbidden in headers).
#
# Forbidden in src/core/**/*.h and src/domain/**/*.h:
#   #include <QWidget>  #include <QPainter>  #include <QPixmap>
#   #include <QMainWindow> #include <QGraphicsView> #include <QImage>
#   (QImage is borderline — it is a value type but couples core to the GUI
#    module; flagged as a warning, not a hard fail, when inside core/ headers.)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/audit_qt_boundary.ps1
# Exit 0 = clean; exit 1 = forbidden include found.

$ErrorActionPreference = 'Stop'
$repo = 'D:\mviewer'
$forbidden = @('QWidget', 'QPainter', 'QPixmap', 'QMainWindow', 'QGraphicsView',
               'QGraphicsScene', 'QFrame', 'QScrollArea', 'QDialog')
$warn = @('QImage', 'QPicture', 'QBitmap')

$headers = Get-ChildItem -Path (Join-Path $repo 'src') -Recurse -Include *.h |
    Where-Object { $_.FullName -match '\\(core|domain)\\' }

# Sanctioned exceptions (documented in docs/acceptance/M12.5_quality_audit.md).
# These files are the DELIBERATE Qt <-> core boundary points and are not leaks:
#   * src/core/render/RenderEngine.h — the software renderer's public CPU
#     rasterization entry point legitimately uses QPainter/QRect. The rest of
#     the render API is Qt-free (operates on ImageData). Frozen per AGENTS.md.
#   * src/core/image/QtConvert.h — the explicit core<->Qt image conversion
#     adapter (ImageBuffer <-> QImage). By design this is where Qt enters core.
$allowlist = @(
    [regex]::Escape((Join-Path $repo 'src\core\render\RenderEngine.h')),
    [regex]::Escape((Join-Path $repo 'src\core\image\QtConvert.h'))
)

$fails = 0
$warns = 0
foreach ($h in $headers) {
    $full = $h.FullName
    $isAllowed = $false
    foreach ($a in $allowlist) { if ($full -match $a) { $isAllowed = $true; break } }
    $lines = Get-Content $full
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $ln = $lines[$i]
        foreach ($f in $forbidden) {
            if ($ln -match "#\s*include\s*<$f>") {
                if ($isAllowed) {
                    Write-Host "ALLOWED: $full`:$($i+1)  $f (sanctioned boundary — see M12.5 doc)"
                } else {
                    Write-Host "FAIL: $full`:$($i+1)  forbidden UI include: $f"
                    $fails++
                }
            }
        }
        foreach ($w in $warn) {
            if ($ln -match "#\s*include\s*<$w>") {
                if ($isAllowed) {
                    Write-Host "ALLOWED: $full`:$($i+1)  $w (sanctioned boundary — see M12.5 doc)"
                } else {
                    Write-Host "WARN: $full`:$($i+1)  Qt GUI value type in header: $w"
                    $warns++
                }
            }
        }
    }
}

Write-Host ""
Write-Host "=== M12.5 Qt-boundary scan ==="
Write-Host "headers scanned: $($headers.Count)"
Write-Host "forbidden hits : $fails"
Write-Host "warnings       : $warns"
if ($fails -gt 0) { exit 1 }
exit 0
