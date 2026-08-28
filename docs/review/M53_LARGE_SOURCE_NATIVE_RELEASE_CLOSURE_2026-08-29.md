# M53 — Large-Source Format Parity & Native Release Reality Closure

Date: 2026-08-29
Milestone: M53
Platform qualified: Windows x64, MSVC 2022, Qt 6.10.3
Baseline commit: `3a22f93`
Closure tree: source-stable before commit

## Verdict

**Automated closure: PASS.** The M53 implementation closes the measured
large-source TIFF display gap on Windows, makes RAW preview discovery
streaming and bounded, gives Compare a terminal display-failure explanation,
and adds deterministic Viewer/Compare/lifetime regressions. The final local
Release test gate passed twice consecutively with no failed tests. The Windows
portable package and installer passed the strict release contract.

Physical monitor, ICC/color-profile, DPI, GPU/OpenGL, Explorer/UNC/network
share, installer interaction, and long-session perceived-UX checks remain
MANUAL/BLOCKED. They are not represented as automated PASS claims.

## Phase 0 baseline

The baseline was captured on the clean M52 tree before implementation edits.
The full record is [M53 Phase 0 baseline](M53_PHASE0_BASELINE_2026-08-28.md).

| Item | Baseline evidence |
| --- | --- |
| Source tree | Clean `master` at `3a22f93`, `master...origin/master` |
| Canonical gate | `build.ps1 Test`: `114/114` passed, `751.39 s` |
| Benchmark gates | `bench_enforce 256.42 s`; `bench_smoke 140.20 s`; `m51_rc_soak 37.05 s` |
| Architecture | Grade A / 92.7 snapshot; architecture violations 0 |
| Complexity | Hard failures 0; 107 advisory warnings |
| Corpus | 100MP JPEG, 100MP TIFF, 24MP JPEG and existing small golden assets; no dedicated 16-bit TIFF, large PNG/BMP boundary, or large RAW scanner fixture |

The baseline reproduced two product gaps. Qt 6.10's TIFF `ClipRect` path still
attempted the full 100MP allocation before applying the clip, so 100MP TIFF
fit/region display failed under the configured allocation limit. The RAW
preview path used `QFile::readAll()`, so its old full-file-copy counter was
zero but did not measure the scanner's actual container-sized `QByteArray`.

## Root causes and implementation

### TIFF and large raster display

The first Phase 1 attempt used the Qt TIFF handler's clip path. A direct
100MP test measured the same allocation failure as baseline; that approach was
rejected and is not claimed as native.

On Windows, `QtDecoder` now uses a bounded Windows Imaging Component adapter:

`IWICBitmapDecoder` → frame → optional `IWICBitmapClipper` →
`IWICBitmapScaler` → `24bppBGR` converter → bounded `RGB888` raster.

The adapter avoids full-source RGB materialization for fit and region display,
rejects invalid/oversized output buffers, and does not upscale when the
requested LOD exceeds the source dimensions. Unrotated Windows TIFFs claim
`NativeLod`; TIFF regions are classified `BoundedRasterRegion` because this is
bounded raster work, not a true tile/random-access claim. JPEG keeps its
reduced-DCT native LOD path. PNG/BMP retain the honest `FullDecodeScaled`
classification.

### RAW preview scanning

`RawDecoder::extractPreview()` now scans fixed 64 KiB parser windows using
seek/read operations. JPEG marker and entropy parsing validates candidates
without retaining every candidate; only the largest valid preview is decoded.
The diagnostic peak includes parser windows and the selected/candidate preview
buffers. The 64 MiB synthetic RAW regression measured peak storage below 8 MiB
and selected the largest valid embedded JPEG. Truncated input fails gracefully.
No full-file `readAll()` remains in the preview scanner.

### Unified display failure reporting

Compare materialization now returns a structured terminal error alongside the
source-backed raster. A failed refresh preserves the last valid pane image
when available; an initial blank pane receives a caption and tooltip explaining
that the source could not be displayed and why. This removes the unexplained
blank state without pretending a failed source decode succeeded.

## Deterministic corpus and regressions

`testdata/generate_large_fixtures.py --ensure` now provisions the ignored,
deterministic M53 fixtures: a 16-bit 4096×4096 LZW TIFF, 4096×4096 PNG and
4096×4096 BMP. Unicode and space-containing paths are exercised by copying the
16-bit TIFF into a temporary path. Existing 24MP/100MP JPEG/TIFF and RAW-like
fixtures remain part of the matrix. Generated fixture binaries are not
committed; the generator is the reproducible source of the corpus.

The M53 focused results in both final full-gate runs were:

| Regression | Result |
| --- | --- |
| `m53_large_source_tests` | PASS; 31 core parity/RAW assertions, 3.31 s first run / 3.21 s second run |
| `m53_large_source_ui_tests` | PASS; real Viewer and Compare qualification, 8.49 s / 8.06 s |
| `m53_large_source_soak` | PASS; five real large-TIFF Viewer/Compare lifecycle rounds, 32.72 s / 32.69 s |
| `m47_source_tests` | PASS; JPEG source-backed behavior preserved and TIFF now bounded/native on Windows |
| `m47_compare_lod_tests` | PASS; TIFF Compare bounded/native behavior and pane replacement preserved |

The five-round soak is a deterministic lifecycle regression, not a substitute
for the manual long-session UX review. Existing M46/M47/M51 workflow and soak
gates continue to cover Browse navigation, restore, cancellation, scheduler
teardown, and broader release-contract behavior.

## Workflow and safety coverage

- Viewer: 100MP TIFF probe, bounded fit LOD, bounded zoom/region request,
  source geometry, and scheduler-idle completion.
- Compare: two 100MP TIFF panes, split/overlay/blink/diff mode switches,
  A→B→A replacement, source geometry, bounded pane rasters, and scheduler
  idle completion.
- Exact source: source metadata/geometry remains separate from the bounded
  display raster; no full decode is used for the M53 TIFF display path.
- Cancellation and lifetime: inherited M40/M46/M47 contracts plus the M53
  repeated widget destruction and superseding `setImages` rounds pass.
- JPEG non-regression: existing 100MP JPEG source-backed display and Compare
  paths remain green in the updated M47 regressions.
- Fault handling: truncated RAW input and decoder failures are terminal and
  bounded; Compare preserves a prior valid raster or explains an initial blank.

## Final local gates

The source-stable tree was verified with the mandated entry point twice in a
row:

| Run | Result | CTest real time |
| --- | --- | ---: |
| 1 | `100% tests passed, 0 tests failed out of 117` | 793.12 s |
| 2 | `100% tests passed, 0 tests failed out of 117` | 792.20 s |

Both runs included `bench_enforce`, `bench_smoke`, `product_workflow_gate`,
`workflow_ux_tests`, `m51_rc_soak`, architecture/complexity regressions,
release-version/contract tests, the updated M47 tests, and all three M53
targets.

Direct final gate results:

- Architecture gate: **PASS**, 0 violations, 0 warnings.
- Strict complexity gate: **PASS**, 0 hard failures, 0 cyclomatic failures,
  0 function line-cap failures; 107 advisory warnings remain accepted and
  visible baseline debt.

## Release package and native automation

Command:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_release.ps1 -Version 1.0.13
```

The package script deployed runtime dependencies, ran packaged self-test,
checked the installer PE version, produced `SHA256SUMS.txt`, and passed:

`Strict RC release contract: PASS (1.0.13; Windows metadata 1.0.13.0)`

| Artifact | Size | SHA256 |
| --- | ---: | --- |
| `dist/MViewer-1.0.13-portable.zip` | 27,557,398 bytes | `57E8227805068829461C7B0DD7402409CAEC33E03A0516D013FB3BE8C66CB4C4` |
| `dist/MViewer-1.0.13-Setup.exe` | 27,599,116 bytes | `8FCD7C1E0B7F80FAD98C0B2071C349C5E6BE5C89469F49D988CCB26455D4019B` |

The artifacts are generated release outputs under ignored `dist/`; they are
not source-control deliverables in this commit.

## Manual / blocked / incomplete

The following remain explicitly unresolved because they require a physical
target environment or human perception:

- ICC profile/color-managed output fidelity and 16-bit visual interpretation;
- physical DPI scaling, mixed-DPI movement, and resize/flicker feel;
- GPU/OpenGL driver behavior and visual zoom smoothness;
- Explorer association, installer interaction, UNC/network-share behavior;
- prolonged real-user Browse → Compare → ROI → Analyze → Export sessions;
- multi-hour soak, suspend/resume, sleep/wake, and resource-pressure behavior.

No new format family, plugin framework, scheduler/cache refactor, or build/CI
change was introduced. M53 is therefore closed for the automated Windows
release-quality scope, with the physical qualification items preserved as
MANUAL/BLOCKED follow-up rather than silently marked complete.
