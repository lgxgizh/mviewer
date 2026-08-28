# M53 Phase 0 — Large-Source Characterization Baseline

Date: 2026-08-28 · Milestone: M53 · Baseline commit: `3a22f93`

## Scope and discipline

This is the required M53 characterization record. The canonical baseline was
run on the source-stable tree before the M53 implementation changes below were
started. Existing M47–M52 behavior was treated as the baseline; no scheduler,
cache, decoder-registry, plugin, or build-system redesign was attempted.

## Canonical baseline

Command:

```text
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test
```

Result: **114/114 CTest tests passed**, with total CTest real time of
**751.39 s**. The build configured with Qt **6.10.3 / MSVC 2022** and had no
pending build work (`ninja: no work to do`). The long-running baseline checks
included `bench_enforce` (**256.42 s**), `bench_smoke` (**140.20 s**),
`m51_rc_soak` (**37.05 s**), `m47_large_soak`, `m46_workflow_soak`, workflow
UX, release contract, architecture, and complexity regressions.

| Gate / signal | Baseline observation |
| --- | --- |
| Build health | 92.7 / grade A (last committed health snapshot; complexity score 60) |
| Architecture | 0 violations; architecture regression PASS |
| Complexity | 0 hard failures; 107 advisory warnings in the current M52 evidence |
| Scheduler | Existing M46/M47/M50/M51 tests passed; their pool/graph drain assertions were green |
| Benchmark | `bench_enforce` and `bench_smoke` passed in the canonical gate |
| Git tree | clean before M53 characterization edits; `master` at `3a22f93` |

## Deterministic corpus inventory

The existing `testdata/generate_large_fixtures.py --ensure` corpus was present
and the `large_fixture_gate` passed. It covered the 100MP JPEG/TIFF, a 24MP
JPEG, orientation/ICC/high-frequency sources, extreme aspect ratios, and
truncated inputs. Before M53, it did **not** include a dedicated 16-bit TIFF,
large PNG/BMP pair, or a generated large RAW-like container; those are added
to the M53 characterization matrix and remain generated/ignored test data.

| Required characterization class | Baseline fixture / observation |
| --- | --- |
| 24MP JPEG | `high_compression.jpg`, 6000×4000 |
| 100MP JPEG | `large_jpeg_100mp.jpg`, 12000×8333 |
| 100MP TIFF | `large_tiff_100mp.tiff`, 10000×10000, LZW |
| Larger TIFF | No additional fixture in the pre-M53 corpus |
| 16-bit TIFF | No dedicated fixture in the pre-M53 corpus |
| Large PNG/BMP boundary | Only small golden PNG/BMP before M53 |
| RAW-like / embedded JPEG | Synthetic tests existed, but no large generated container |
| Unicode path | Covered by existing M49/M50 workflow regressions |

## Measured source behavior

The existing M47 baseline recorder was rerun as:

```text
build_msvc\bin\m47_large_image_baseline.exe --out benchmark\report\m53_phase0_large_image_baseline.json
```

The output was written to the ignored benchmark-report directory. The current
run measured the following representative values; RSS is evidence only and is
not used as the sole correctness gate.

| Source / operation | Result | Classification / counters |
| --- | --- | --- |
| 100MP JPEG full decode | 0×0, failed in 9.1ms | `fullDecode=1` |
| 100MP JPEG scaled 256 | 256×177, 94.3ms, 135,936 RGB bytes | scaled decode succeeds; source-backed tests classify native LOD |
| 100MP TIFF full decode | 0×0, failed in 1.8ms | `fullDecode=1` |
| 100MP TIFF scaled 256 | 0×0, failed in 1.3ms | `FullDecodeScaled`, `scaledDecode=1` |
| 24MP JPEG full decode | 6000×4000, 966.6ms, 72,000,000 RGB bytes | full source materialized |
| 100MP JPEG Viewer | display-ready in 447.1ms; source 12000×8333; no full frame | source-backed display, bounded raster |
| 100MP JPEG Compare×2 | 935.9ms; both panes ready; pane raster 869×604 | source-backed display, no full decode |

### Explicit reproduction: TIFF parity gap

`m47_source_tests` passed while preserving the honest M47 expectation that a
100MP TIFF `decodeLod(256)` failed and was classified `FullDecodeScaled`. The
direct Qt TIFF path therefore had no usable bounded fit raster for a 100MP
source. `m47_compare_lod_tests` likewise preserved the documented blank TIFF
fallback. This historical reproduction defined the M53 Phase 1 tests-first
target; the implemented Windows WIC bounded path and current regression result
are recorded in the M53 closure review.

### Explicit reproduction: RAW boundedness gap

`RawDecoder` already reported zero bytes for the old “second full-file copy”
diagnostic, and ordinary/multiple/malformed preview semantics passed. Source
inspection and the large-container characterization show that
`RawDecoder::extractPreview()` still executes `QFile::readAll()`, retaining the
entire RAW container in one `QByteArray`. The zero diagnostic therefore does
not prove bounded scanner memory. M53 adds a peak-buffer diagnostic and a
64MiB synthetic RAW regression; the tests are intentionally red against this
pre-fix implementation.

## Test-first regressions introduced after the baseline

`m53_large_source_tests` freezes the required behavior before implementation:

- a 100MP TIFF must return a non-empty bounded fit raster without
  `fullDecode`/`fullDecodeScaled` display work;
- a 100MP TIFF region must return a non-empty target raster without full
  materialization;
- a 64MiB synthetic RAW must choose the largest valid embedded JPEG while
  scanner peak storage remains below 8MiB;
- malformed large RAW input must fail gracefully and remain bounded.

The target was introduced red against the baseline, then turned green by the
M53 implementation. No existing test was removed, weakened, or skipped.

## Baseline verdict

M52 release-quality and workflow gates are green, but M53 has two measured
product gaps: large TIFF source-backed presentation is unavailable, and RAW
preview scanning is not memory-bounded despite the older copy counter being
zero. JPEG source-backed display, exact-source separation, cancellation, and
existing scheduler/lifetime contracts are the protected baseline.
