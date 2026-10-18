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
#   R5  Core / Domain HEADERS must not include Qt, with two named exceptions:
#       src/core/image/QtConvert.h and src/core/image/QtMetadataSemantics.h are
#       the sanctioned Qt adapters of the core image layer (they exist to convert
#       between core pixel buffers and Qt types). Everything else in core/ and
#       domain/ keeps Qt in .cpp bodies only — that is what makes those layers
#       Qt-free in practice, and it is checkable.
#
# Violations are WARNING-level by default. -Strict exits 1 when any violation
# is present. A missing tree, an unreadable file, a path outside the repo, or
# JSON that cannot be emitted also exits 1 — those are tool errors, not an
# empty clean report.
#
# Output: human report to stdout; with -Json emits a JSON object (also to
# -OutJson) consumed by scripts/health_score.ps1.
# The M46 regression test (architecture_gate_test.ps1) proves both directions:
# a planted violation is flagged, and the real tree reports 0 warnings.

[CmdletBinding()]
param(
    [string]$Repo = (Get-Location).Path,
    [switch]$Json,
    [string]$OutJson = '',
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

function ConvertTo-RepoRelative([string]$FullName) {
    $full = ([System.IO.Path]::GetFullPath($FullName) -replace '\\', '/')
    $root = ([System.IO.Path]::GetFullPath($Repo) -replace '\\', '/').TrimEnd('/')
    $prefix = $root + '/'
    if (-not $full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "architecture gate: '$FullName' is not under '$Repo'"
    }
    return $full.Substring($prefix.Length)
}

function Get-Layer([string]$RelativePath) {
    $path = $RelativePath -replace '\\', '/'
    if ($path -match '^src/domain/') { return 'domain' }
    if ($path -match '^src/core/') { return 'core' }
    if ($path -match '^src/compare/') { return 'compare' }
    if ($path -match '^src/application/') { return 'application' }
    if ($path -match '^src/widgets/') { return 'ui' }
    if ($path -match '^src/[^/]+\.(c|cc|cpp|cxx|h|hpp)$') { return 'ui' }
    return 'other'
}

function Test-Harness([string]$RelativePath, [string]$Name) {
    $path = $RelativePath -replace '\\', '/'
    foreach ($segment in ($path -split '/')) {
        if ($segment -match '^(?i)(benchmark|scripts|plugins)$') { return $true }
    }
    if ($Name -match '(?i)(test|benchmark)') { return $true }
    return $false
}

function Test-CacheHeader([string]$IncludePath) {
    $base = (($IncludePath -replace '\\', '/') -split '/')[-1]
    return $base -match '^(?i)(CacheManager|ThumbnailCache|ImageCache|cache)\.h$'
}

function Test-RepositoryHeader([string]$IncludePath) {
    $base = (($IncludePath -replace '\\', '/') -split '/')[-1]
    return $base -match '^(?i)(ImageRepository|Repository)\.h$'
}

function Test-ThumbnailHeader([string]$IncludePath) {
    $path = $IncludePath -replace '\\', '/'
    $parts = $path -split '/'
    foreach ($segment in $parts) {
        if ($segment -match '^(?i)thumbnail$') { return $true }
    }
    $base = $parts[-1]
    return $base -match '^(?i)thumbnail.*\.h$'
}

try {
    $Repo = (Resolve-Path -LiteralPath $Repo).Path
    $srcDir = Join-Path $Repo 'src'
    if (-not (Test-Path -LiteralPath $srcDir -PathType Container)) {
        throw "architecture gate: missing source directory '$srcDir'"
    }

    $src = @(Get-ChildItem -LiteralPath $srcDir -Recurse -File -ErrorAction Stop |
        Where-Object {
            $_.Extension -in @('.cpp', '.h') -and
            $_.FullName -notmatch '[\\/](build_msvc|build|build_sa|build_asan|build_ubsan|build_clazy|build_perf|testdata|node_modules)[\\/]'
        })

    $violations = [System.Collections.Generic.List[object]]::new()

    # R5: the only core headers allowed to pull in Qt. Keep this list short and
    # named — it is the sanctioned Qt boundary of the core image layer.
    $qtAdapterHeaders = @(
        'src/core/image/QtConvert.h',
        'src/core/image/QtMetadataSemantics.h'
    )

    foreach ($f in $src) {
        $rel = ConvertTo-RepoRelative $f.FullName
        # Benchmark / test / plugin harnesses are allowed to poke internal
        # implementations (they are tools, not product architecture) — skip them so
        # they don't drown out real product-layer violations.
        if (Test-Harness $rel $f.Name) { continue }
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
        $layer = Get-Layer $rel

        # R5 scope: headers of the core / domain layers, minus the named Qt adapters.
        $relFwd = $rel -replace '\\', '/'
        $isLayerHeader = ($f.Extension -eq '.h') -and ($layer -in @('core', 'domain'))
        $isQtAdapter = $false
        foreach ($adapter in $qtAdapterHeaders) {
            if ($relFwd.EndsWith($adapter, [System.StringComparison]::OrdinalIgnoreCase)) {
                $isQtAdapter = $true
            }
        }

        try {
            $rawLines = Get-Content -LiteralPath $f.FullName -Encoding UTF8 -ErrorAction Stop
        }
        catch {
            throw "architecture gate: cannot read '$($f.FullName)': $($_.Exception.Message)"
        }
        if ($null -eq $rawLines) { $lines = @() } else { $lines = @($rawLines) }
        for ($i = 0; $i -lt $lines.Length; $i++) {
            $ln = $lines[$i]
            if ($null -eq $ln) { continue }
            # R5: a core / domain header may not pull in Qt (the two named adapters
            # are the only exception). Qt in those layers' .cpp bodies is allowed.
            # -cmatch: Qt headers are spelled <Q...>; a case-insensitive match would
            # also flag standard headers such as <queue>.
            if ($isLayerHeader -and -not $isQtAdapter -and ($ln -cmatch '#\s*include\s*[<"]Q')) {
                $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$ln.Trim(); rule='R5';
                    message='Core/Domain header includes Qt (keep Qt in .cpp bodies, or add the header to the named adapter allow-list)' })
            }
            if ($ln -notmatch '#\s*include\s*"([^"]+)"') { continue }
            $inc = $Matches[1]
            # A file including its own header (e.g. thumbnailcache.cpp -> thumbnailcache.h)
            # is not a layering violation.
            $incBase = (($inc -replace '\\', '/') -split '/')[-1] -replace '\.h$', ''
            $ownBase = $f.BaseName
            if ($incBase -eq $ownBase) { continue }

            if ($layer -in @('ui', 'application', 'compare')) {
                if (Test-CacheHeader $inc) {
                    $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$inc; rule='R1';
                        message='UI/Application references Cache directly (use Repository/Domain)' })
                }
                if (Test-RepositoryHeader $inc) {
                    $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$inc; rule='R2';
                        message='Widget/Compare accesses Repository directly (go through the ImageLoading facade)' })
                }
            }
            if ($layer -eq 'compare') {
                if (Test-ThumbnailHeader $inc) {
                    $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$inc; rule='R3';
                        message='Compare depends on Thumbnail module (decouple the two view modules)' })
                }
            }
            if ($layer -eq 'domain') {
                $incNorm = $inc -replace '\\', '/'
                if ($incNorm -match '^(core|ui|widgets|application)/') {
                    $violations.Add([ordered]@{ file=$rel; line=($i+1); include=$inc; rule='R4';
                        message='Domain depends on an upper layer (Domain must stay dependency-free)' })
                }
            }
        }
    }

    $isClean = ($violations.Count -eq 0)
    $summary = [ordered]@{
        gate       = 'architecture'
        passed     = $isClean
        warnings   = $violations.Count
        violations = $violations
    }

    if ($Json) {
        $js = $summary | ConvertTo-Json -Depth 6
        if ([string]::IsNullOrWhiteSpace($js)) {
            throw 'architecture gate: ConvertTo-Json produced no output'
        }
        if ($OutJson) {
            Set-Content -LiteralPath $OutJson -Value $js -Encoding UTF8
            if (-not (Test-Path -LiteralPath $OutJson)) {
                throw "architecture gate: failed to write '$OutJson'"
            }
        }
        Write-Output $js
        if ($Strict -and -not $isClean) { exit 1 }
    }
    else {
        Write-Host "=== Architecture Gate ==="
        Write-Host "files scanned : $($src.Count)"
        # Keep this script ASCII-safe: Windows PowerShell can misread UTF-8 without
        # a BOM under a non-UTF-8 runner code page, turning typographic quotes into
        # parser tokens. The human report does not need a Unicode dash here.
        Write-Host "violations    : $($violations.Count)"
        if ($violations.Count) {
            foreach ($v in ($violations | Sort-Object rule, file)) {
                Write-Host ("  [{0}] {1} L{2}: {3}  ({4})" -f $v.rule, $v.file, $v.line, $v.include, $v.message)
            }
            if ($Strict) {
                Write-Host "`nARCHITECTURE: FAIL ($($violations.Count) violation(s))"
                exit 1
            }
        }
        Write-Host "`nARCHITECTURE: $($violations.Count) advisory violation(s)"
    }
}
catch {
    [Console]::Error.WriteLine("architecture gate: $($_.Exception.Message)")
    exit 1
}
