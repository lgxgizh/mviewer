# M55 — Interactive Latency, Memory and Navigation Closure

Date: 2026-08-29 · Milestone: M55

## Result

The automated M55 scope is implemented and focused tests are green. The work
closes bounded UI thumbnail memory, same-generation thumbnail ABA, scan hot
paths, staged thumbnail-cache bootstrap, source-backed LOD neighbor warmth,
and coalesced TagStore persistence without changing the frozen scheduler,
decoder registry, cache-manager, or plugin architecture.

Final local acceptance passed in two consecutive source-stable Release runs:
`build.ps1 Test` completed 119/119 in 749.06 s and 119/119 in 750.28 s.
The architecture and complexity hard gates reported zero violations/failures.

## Root causes and changes

| Root cause | Change | Observable proof |
|---|---|---|
| Raw-path `QPixmap` map could grow with browsing history | Composite `(path,size)` identity, LRU accounting, 384-entry / 96 MiB hard budget, row-span invalidation | `m55_interactive_tests` drives 420 real images and asserts both limits |
| Viewport churn cancelled all work; owner identity was generation-only | Retain visible+predictive intersection; every enqueue has a unique owner token and owner-keyed completion cleanup | M55 ABA and overlap tests in `test_thumbnailpipeline.cpp` |
| Scan probe lock and comparator conversions sat in the iteration path | One immutable probe snapshot per scan; direct QString comparator | Existing Browse acceptance plus source review; no production probe lock per item |
| First exact cache hit waited on a directory-wide scan | Exact key probe returns immediately while a one-time indexed/cap pass bootstraps in the background; maintenance APIs join it | `cache_tests`, `m26_thumbnail_tests`, and `m27_thumbnail_tests` remain green |
| Large viewer had no bounded neighbor representation | Two source-backed `decodeLod(1024)` neighbors, 64 MiB budget, latest-intent generation, promotion and warm-hit reuse | Existing M47 LOD lifecycle suite plus bounded state accessors |
| Tag edits rewrote the whole file synchronously | RatingStore-shaped 100 ms debounce worker; `save()` remains a synchronous flush and retries failed writes | M46 persistence failure/recovery suite remains green |
| Compare tool controls exceeded the standard 1100 px toolbar width | Keep the semantic tool toolbar but place diff controls and actions in separate rows | `workflow_ux_tests` width contract is green |

## Before / after

| Metric or contract | Before M55 | After M55 |
|---|---|---|
| UI pixmap cap | None | 384 entries and 96 MiB |
| Display-raster neighbor cap | None for LOD-first viewer | At most 2 tracked neighbors and 64 MiB warm cache |
| Small forward viewport move | Cancel + re-submit all pending work | Retain demand intersection |
| Same-generation ABA ownership | Generation/key collision possible | Unique enqueue owner; stale completion cannot erase replacement |
| Cache bootstrap on first exact get/put | Full scan under cache mutex | Exact probe plus asynchronous bootstrap |
| TagStore edit persistence | One synchronous full rewrite per edit | Debounced worker; explicit flush boundary preserved |

The new standalone recorder was run with a fresh offscreen runtime and the
same 2x2 deterministic PNG shape used by the M54 large-directory harness. It
reported the following machine-local observations (times are milliseconds;
memory values are bytes):

| Corpus | First row | First thumbnail | Scan complete | Stable | Jump p95 | Peak pending / handles | UI pixmaps peak / bytes | Working-set start / peak / end |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 10,000 | 31 | 7,175 | 7,320 | 7,875 | 86 | 224 / 112 | 48 / 2,822,400 | 21,975,040 / 55,754,752 / 48,504,832 |
| 50,000 | 33 | 64 | 5,009 | 5,575 | 182 | 223 / 112 | 153 / 8,996,400 | 33,009,664 / 101,330,944 / 101,330,944 |

The pre-M55 control recorded first-row 59/41 ms and scan/stable 2,476/2,584
and 11,820/12,158 ms for cold 10k/50k runs, with scroll-jump p95 50 ms and
44 obsolete decodes. The M55 recorder uses a fresh process and records a
different demand/memory sample, so these values are retained as directional
evidence rather than a claimed apples-to-apples speedup. The UI-side raster
cache itself stayed far below its 384-entry / 96 MiB hard bound in both runs;
process RSS across different corpus sizes is not presented as a same-process
plateau claim.

The M54 control numbers are preserved in
`M55_PHASE0_BASELINE_2026-08-29.md`. They are not replaced by synthetic claims:
the standalone M55 recorder now has a fresh-runtime 10k/50k observation above.
Preview p50/p95 remains the existing M54 foreground-preview evidence (10k cold
2/6 ms in the final M54 run), while cold cache-bootstrap latency was not
isolated as a separate number; exact-hit responsiveness and eventual indexed
accounting are covered by the cache regression suites. Native fullscreen
sequential p50/p95 and long physical GUI feel remain `MANUAL/BLOCKED`.

## Verification

Focused checks passed during implementation:

- `thumbnailpipeline_tests` — legacy coverage plus deterministic M55 ABA and
  overlap-retention cases.
- `m55_interactive_tests` — real 420-image `ThumbnailPanel` QPixmap budget and
  thumbnail-size identity reset.
- `m46_persistence_tests` — TagStore atomic failure/recovery and all existing
  persistence contracts.
- `m54_perceived_performance_tests` — existing Browse/Preview benchmark.
- `m47_viewer_lod_tests` — existing large-source LOD lifecycle coverage (the
  Debug-only RSS assertion is machine/configuration sensitive; final Release
  `build.ps1 Test` is the canonical gate).
- Two consecutive local Release `build.ps1 Test` runs — 119/119 passed in
  749.06 s and 750.28 s; architecture and complexity hard gates were both 0.

No existing test was removed, weakened, or made conditional. The generated
test matrix is refreshed after this change. Native-only rows are reported as
`MANUAL/BLOCKED`, never as automated PASS.
