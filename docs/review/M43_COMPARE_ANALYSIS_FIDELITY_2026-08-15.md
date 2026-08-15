# M43 — Compare Analysis Fidelity & Pixel-Accurate Inspection Closure

Date: 2026-08-15
Scope: Compare source truth, exact Inspector sampling, neighborhoods, crop/
rotation/adjustment semantics, display LOD separation, and resource-safe
regressions.

## Verdict

Implementation is complete for the automated contract. Compare analysis now
reads the decoded `ImageFrame::pixels()` source and never treats the bounded
`RawImageView::image()` display LOD as analysis input. The focused core and
Compare acceptance tests are green. Canonical CTest runs #1, #2 and #3 are all
green at 88/88.

Windows multi-DPI, physical ICC-profile, and long-session manual UX review are
not available in this offscreen environment and remain **MANUAL PENDING**.

## Failure that motivated M43

The pre-M43 Compare Inspector path received a source coordinate from
`RawImageView`, then `CompareWorkspace::updateInspector()` sampled the pane's
display `QImage`. That image is intentionally bounded to the viewport and may
also have preview-only adjustment and ICC conversion applied. Flat fixtures and
the prior 88-test gate did not force a source/LOD disagreement, so this failure
could remain invisible while all existing tests passed.

The new high-frequency fixture is 2400×1600 and assigns a deterministic,
different RGB value to every coordinate. Before the implementation was
switched to source-backed sampling, the targeted Compare acceptance test
reproduced the mismatch: the fixture found a coordinate where the pane LOD RGB
did not equal the source RGB and the Inspector assertion failed. After the
fix, the same adversarial test passes.

## Data-flow contract

| Surface | Pixel source | Full-resolution semantics | ICC | Adjustment semantics |
| --- | --- | --- | --- | --- |
| Pixel Inspector RGB/HEX/HSV/Lab/YUV/YCbCr/XYZ | `ImageFrame::pixels()` via `ImageData` | Yes; one exact source pixel | No; numeric source values | Point operations in adjusted pane space; inverse crop/rotation mapping |
| Pixel Inspector neighborhood | `ImageFrame::pixels()` | Yes; exact 1×1/3×3/5×5/7×7 samples, clipped at edges | No | Same as Inspector pixel |
| ROI histogram | `ImageData` + `CompareWorkspace::applyAdjusts()` | Yes; adjusted full-resolution source | No | Canonical brightness → contrast → gamma → WB → crop → rotation |
| Diff / PSNR / SSIM / diff statistics | Adjusted full-resolution `ImageData` | Yes | No | Same canonical adjustment pipeline |
| Report/export input | Adjusted full-resolution `ImageData` and provenance | Yes | No, unless an explicit future export contract requests display conversion | Adjustments and metadata are retained in the report bundle |
| Compare pane display | `RenderEngine::scaleBoundedStatic()` → display QImage | No; bounded visual LOD | Yes, display-only | Preview approximation; nonlinear gamma/clipping need not commute with resize |
| RAW16 Inspector field | `ImageFrame::raw16At()` | Exact only for identity/untransformed source | No | Adjusted/cropped/rotated values use the adjusted 8-bit analysis sample and are labeled accordingly |

The display worker still snapshots `ImageData` by value, keeps the source buffer
shared, performs bounded scaling, and delivers only value data to the UI. It
does not capture `this`/widgets in the worker, and hover analysis performs no
full-resolution `QImage` materialization.

## Exact coordinate semantics

The core `AnalysisAdjustment` sampler is Qt-free and is now the single source
for Compare Inspector pixels and neighborhoods. Given an adjusted-pane
coordinate `(x, y)`:

- crop bounds are clamped to the source image;
- identity maps `(x, y)` directly into the crop;
- 90° clockwise maps `(x, y)` to
  `(cropX + y, cropY + cropHeight - 1 - x)`;
- 180° maps to
  `(cropX + cropWidth - 1 - x, cropY + cropHeight - 1 - y)`;
- 270° maps to
  `(cropX + cropWidth - 1 - y, cropY + x)`;
- brightness, contrast, gamma and white-balance gains match the existing
  `ImageAdjust` order and rounding/clamping rules;
- neighborhoods invoke the same sampler for every kernel position, so they
  remain source-exact under crop, rotation and point adjustments.

## Regression coverage

`src/core/test_pixelinspector.cpp` now covers:

- identity and out-of-bounds source sampling;
- crop plus 0/90/180/270° inverse coordinate mapping;
- exact equivalence with the canonical ImageAdjust brightness/contrast/gamma/
  white-balance pipeline;
- grayscale white-balance behavior;
- full-resolution 1×1, 3×3, 5×5 and 7×7 neighborhood counts/means, including
  clipped edge kernels.

`src/test_compare_acceptance.cpp` adds the adversarial UI regression:

- high-frequency source versus bounded display LOD mismatch discovery;
- exact source RGB Inspector value and explicit non-LOD assertion;
- exact 7×7 source neighborhood mean;
- stability after two zoom-driven LOD replacements and Fit restoration.

Existing M30 coalescing, M40 cancellation/latest-wins, M42 bounded-memory,
ROI/histogram, Diff/PSNR/SSIM, ICC-profile and RAW16 suites remain in the
canonical gate.

## M42 bounded-memory preservation

The focused M42 path still reports source bytes `92,160,000` and display bytes
`3,244,800` for the eight-pane large-image fixture. The M43 Inspector change
does not create a full-resolution adjusted `QImage`; it samples only the
requested source pixel or neighborhood. Display materialization remains
bounded, cancellable and generation-guarded.

## Verification

Commands use the project entry point for builds and tests:

```text
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Release
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test
```

Results:

- Release build: passed.
- `test_pixelinspector.exe`: passed.
- `compare_acceptance_tests.exe`: passed, including all M43 checks.
- Canonical CTest run #1: **88/88 passed**, total time 581.15 seconds;
  `bench_enforce` passed in 349.50 seconds and `bench_smoke` in 122.78 seconds.
- Canonical CTest run #2: **88/88 passed**, total time 525.56 seconds;
  `bench_enforce` passed in 294.71 seconds and `bench_smoke` in 122.34 seconds.
- Canonical CTest run #3: **88/88 passed**, total time 524.38 seconds;
  `bench_enforce` passed in 294.07 seconds and `bench_smoke` in 121.77 seconds.

The local health checks are the repository-provided `scripts/complexity_gate.ps1`,
`scripts/architecture_gate.ps1` and `scripts/health_score.ps1`. The frozen
responsibility TU counts remain within their caps: `mainwindow.cpp` 766 lines,
`compareworkspace.cpp` 791 lines and `thumbnailpanel.cpp` 742 lines.

The explicit health snapshot reported overall **78.2 / 100 (C)** with Build
100, Unit Test 100, Regression 100 and Architecture 80. Architecture reported
four existing R1/R2 warnings (direct Repository/Cache includes in older UI
paths). Complexity reported the repository's existing historical hard-fail
population (58 hard fails, 122 warnings, 6 cyclomatic fails and 44 long
functions); it is advisory and was not expanded into a M43 refactor. The M43
files remain within the frozen MainWindow/CompareWorkspace/ThumbnailPanel TU
caps, and the architecture gate passed according to its warning-only policy.

## Manual validation boundary

**MANUAL PENDING:** native Windows window interaction, mixed-DPI movement,
device-pixel-ratio changes, physical embedded ICC profiles, RAW16 display
hardware, and long-session perceived smoothness/flicker/zoom feel. Automated
offscreen CTest proves source/display data-flow and lifecycle contracts but does
not substitute for those human checks.

## Remaining risks

- Display LOD remains a visual approximation for nonlinear operations by
  design; analysis correctness must continue to use the source-backed API.
- Manual Windows/DPR/ICC review is still required before a hardware-oriented
  release sign-off.
- The benchmark gate is CPU-sensitive and intentionally remains part of the
  canonical local verification loop.
