# release_payload.ps1 — whitelist the shippable files of a build_msvc/bin dir.
#
# Only the app payload ships in the portable zip / installer staging:
#   * MViewer.exe (main) + mviewer_core.dll + example plugin DLLs
#   * Qt runtime DLLs (release variants only — debug Qt6*<d>.dll never ship)
#   * MSVC CRT / GPU runtime DLLs
#   * Qt plugin directories (platforms, imageformats, styles, ...)
#
# Test/benchmark/demo executables (*_tests.exe, test_*.exe, benchmark.exe,
# mviewer_demo*.exe, ...), PDBs and scratch artifacts are excluded.
param(
    [Parameter(Mandatory = $true)]
    [string]$BinDir
)

$bin = Get-ChildItem -LiteralPath $BinDir
$keep = $bin | Where-Object {
    $_.PSIsContainer -or
    $_.Name -match '^(MViewer\.exe|mviewer_core\.dll|example_\w+\.dll|msvcp140.*\.dll|vcruntime140.*\.dll|concrt140\.dll|d3dcompiler_47\.dll|dxcompiler\.dll|dxil\.dll|opengl32sw\.dll)$' -or
    ($_.Name -match '^Qt6\w+\.dll$' -and $_.Name -notmatch '^Qt6\w*d\.dll$')
}
$keep.FullName
