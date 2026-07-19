# Workflow: Analyzer (Image → Analysis panel)

**Phase:** M13 / Product Beta — Phase 1 (Product Workflow verification)
**Owner:** Hermes (commander)
**Status:** VERIFIED (automated) — see `analysis_panel_tests`

---

## 1. User actions

| Step | Action | UI surface |
|------|--------|-----------|
| 1 | Open an image in the Viewer | `ImageViewer` |
| 2 | Open the Analysis panel | `AnalysisPanel` |
| 3 | Pick an analyzer (Histogram / RGB Mean / PSNR / SSIM / Sharpness / Noise / Entropy) | `AnalysisPanel` combo |
| 4 | (For PSNR/SSIM) select a reference image | `AnalysisPanel` |
| 5 | Read the result text | `AnalysisPanel` |

## 2. Expected result

- The `AnalyzerRegistry` resolves the requested analyzer by id and runs it on
  the `ImageFrame`.
- At least the review-required analyzers (Histogram, RGB Mean, PSNR, SSIM,
  Sharpness) produce a non-empty result for a valid frame.
- `runAnalyzer(frame)` runs all registered analyzers and returns an id→text map.

## 3. Acceptance criteria

| ID | Criterion | Target |
|----|-----------|--------|
| A-1 | `registerAnalyzer` exists and is used | contract |
| A-2 | `getAnalyzer(id)` returns a usable analyzer | contract (review P1) |
| A-3 | `runAnalyzer(frame)` returns id→resultText map | contract (review P1) |
| A-4 | Histogram / RGB Mean / PSNR / SSIM / Sharpness run on a valid frame | non-empty |
| A-5 | Analyzers have **no Qt dependency** in `core/analyzer/` | boundary |

## 4. Automated test

**Executable:** `analysis_panel_tests` (ctest: `analysis_panel_tests`)
**Source:** `src/core/test_analysis_panel.cpp`
**What it drives (REAL path, not faked):**
- `AnalyzerRegistry::getAnalyzer(id)` for each built-in id (A-2).
- `AnalyzerRegistry::runAnalyzer(frame)` returns a populated map (A-3).
- Runs Histogram/RGBMean/PSNR/SSIM/Sharpness on a synthesized `ImageFrame`
  (A-4).
- Asserts `core/analyzer/*.h` contains no forbidden Qt includes (A-5) — the
  Qt-boundary contract from review P5.

Run:
```powershell
powershell -ExecutionPolicy Bypass -File ./build.ps1 Test
# or directly:
./build_msvc/bin/analysis_panel_tests
```

## 5. Manual test

1. Open two near-identical images, run PSNR/SSIM — value is sane (>30 dB / >0.9).
2. Open one image, run Histogram — chart renders.
3. Toggle the histogram overlay from the Viewer (ToggleHistogramCommand).

## 6. Subtraction check (RFC §1)

Documents + gates existing test (M9-3). Closes review **P1** (`getAnalyzer` /
`runAnalyzer` contracts). No new core code; analyzers already exist.

---
*Cross-refs: `docs/acceptance/user_workflow.md`, `docs/api/Analyzer.md`,
`docs/rfc/M13_PRODUCT_BETA.md` (Phase 1).*
