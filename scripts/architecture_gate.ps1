# M23 Build-Health — Architecture Gate.
#
# Prevents the layered architecture (UI -> Application -> Core -> Domain) from
# slowly rotting. Rules are enforced by analyzing `#include "..."` edges:
#
#   R1  UI / Application layer must NOT include Cache headers directly
#       (e.g. ThumbnailCache / CacheManager / ImageCache). Cache is an
#       infrastructure concern; UI should reach pixels through Repository/Domain.
#   R2  Widget (UI), Application and Compare layers must NOT include the
#       Repository header directly (ImageRepository). They go through the
#       Application-layer ImageLoadingService / Core ImageLoadingFacade
#       (M44/M46: the loading facade exists and every product loading access
#       converges on it — there is no sanctioned direct-Repository exception).
#   R3  Compare module must NOT depend on the Thumbnail module
#       (ThumbnailPanel / ThumbnailProvider / ThumbnailCache). Coupling these
#       two view modules is the classic source of sync bugs.
#   R4  Domain layer must NOT include anything from Core / UI / Application.
#       Domain is the zero-dependency base; an upward edge breaks the whole
#       layering contract.
#
# Violations are WARNING-level (per the product owner's directive: "违反：CI：
# 直接：Warning。") — they never fail the build, but the Health Score deducts
# points so architecture drift is always visible.
#
# Output: human report to stdout; with -Json emits a JSON object (also to
# -OutJson) consumed by scripts/health_score.ps1. Exit code is always 0.
# The M46 regression test (architecture_gate_test.ps1) proves both directions:
# a planted violation is flagged, and the real tree reports 0 warnings.

[CmdletBinding()]
param(
    [string]$Repo = (Get-Location).Path,
    [switch]$Json,
    [string]$OutJson = ''
)

$ErrorActionPreference = 'Stop'

# Match an include path/name against a forbidden basename pattern.
function Test-Forbidden($incPath, $pattern) {
    $base = ($incPath -split '[\\/]')[-1]
    return ($base -match $pattern) -or ($incPath -match $pattern)
}

$src = Get-ChildItem -Path (Join-Path $Repo 'src') -Recurse -Include *.cpp, *.h |
    Where-Object { $_.FullName -notmatch '[\\/](build_msvc|build|build_sa|build_asan|build_ubsan|build_clazy|build_perf|testdata|node_modules)[\\/]' }

$violations = [System.Collections.Generic.List[object]]::new()

foreach ($f in $src) {
    $rel = $f.FullName.Substring($Repo.Length).TrimStart('\', '/')
    # Benchmark / test / plugin harnesses are allowed to poke internal
    # implementations (they are tools, not product architecture) — skip them so
    # they don't drown out real product-layer violations.
    if ($rel -match 'benchmark|[\\/](scripts|plugins)[\\/]' -or $rel -match 'test') { continue }
    # Exempt design-allowed direct edges — these are NOT layering violations:
    #  * Private implementation headers (`*_p.h`) are a class's internal pimpl;
    #    a UI coordinator legitimately reaches Core singletons (Cache/Repository)
    #    from there. It is an internal detail, not a public API leaking the layer.
    #  * Thumbnail infrastructure (thumbnailprovider/cache/pipeline) *is* the
    #    cache/thumbnail service and must reference ThumbnailCache directly; it is
    #    only mis-classified as `ui` because it lives in `src/` root.
    if ($f.Name -match '_p\.h$' -or $rel -match '(?i)thumbnail(provider|cache|pipeline)') {
        continue
    }
    $layer = 'other'
    if ($rel -match '[\\/]domain[\\/]') { $layer = 'domain' }
    elseif ($rel -match '[\\/]core[\\/]') { $layer = 'core' }
    elseif ($rel -match '[\\/]compare') { $layer = 'compare' }
    elseif ($rel -match '[\\/]application[\\/]') { $layer = 'application' }
    elseif ($rel -match '[\\/]widgets[\\/]') { $layer = 'ui' }
    elseif ($rel -match '^[\\/]?src[\\/][^\\/]+\.') { $layer = 'ui' }  # src root TU (mainwindow.cpp etc.)

    # M46: the historical imageviewer.cpp / previewpanel.cpp exemption is
    # GONE — the Application/Core loading facades exist and Preview/Viewer/
    # Compare all load through them, so a direct Repository include anywhere
    # in UI/Application/Compare is a real layering violation, not a sanctioned
    # boundary.

    # only UI/Application/Compare/other can violate R1-R3; domain R4 checked below
    $lines = Get-Content $f.FullName -Encoding UTF8
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $ln = $lines[$i]
        if ($ln -notmatch '#\s*include\s*"([^"]+)"') { continue }
        $inc = $Matches[1]
        # A file including its own header (e.g. thumbnailcache.cpp -> thumbnailcache.h)
        # is not a layering violation.
        $incBase = ($inc -split '[\\/]')[-1] -replace '\.h$', ''
        $ownBase = $f.BaseName
        if ($incBase -eq $ownBase) { continue }

        if ($layer -in @('ui', 'application', 'compare')) {
            if (Test-Forbidden $inc '(?i)(cachemanager|thumbnailcache|imagecache|^cache\.h$)') {
                $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$inc; rule='R1';
                    message='UI/Application references Cache directly (use Repository/Domain)' })
            }
            if (Test-Forbidden $inc '(?i)(repository|imagerepository)') {
                $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$inc; rule='R2';
                    message='Widget/Compare accesses Repository directly (go through the ImageLoading facade)' })
            }
        }
        if ($layer -eq 'compare') {
            if (Test-Forbidden $inc '(?i)(thumbnail|thumbnailpanel|thumbnailprovider|thumbnailcache)') {
                $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$inc; rule='R3';
                    message='Compare depends on Thumbnail module (decouple the two view modules)' })
            }
        }
        if ($layer -eq 'domain') {
            if ($inc -match '^(core|ui|widgets|application)[\\/]') {
                $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$inc; rule='R4';
                    message='Domain depends on an upper layer (Domain must stay dependency-free)' })
            }
        }
    }
}

$summary = [ordered]@{
    gate       = 'architecture'
    passed     = $true            # advisory: never hard-fails
    warnings   = $violations.Count
    violations = $violations
}

if ($Json) {
    $js = $summary | ConvertTo-Json -Depth 6
    if ($OutJson) { Set-Content -Path $OutJson -Value $js -Encoding UTF8 }
    Write-Output $js
}
else {
    Write-Host "=== Architecture Gate ==="
    Write-Host "files scanned : $($src.Count)"
    # Keep this script ASCII-safe: Windows PowerShell can misread UTF-8 without
    # a BOM under a non-UTF-8 runner code page, turning typographic quotes into
    # parser tokens. The human report does not need a Unicode dash here.
    Write-Host "violations    : $($violations.Count)  (advisory - never fails the build)"
    if ($violations.Count) {
        foreach ($v in ($violations | Sort-Object rule, file)) {
            Write-Host ("  [{0}] {1} L{2}: {3}  ({4})" -f $v.rule, $v.file, $v.line, $v.include, $v.message)
        }
    }
    Write-Host "`nARCHITECTURE: $($violations.Count) advisory violation(s)"
}
