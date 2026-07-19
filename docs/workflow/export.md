# Workflow: Export (Analysis → Report file)

**Phase:** M13 / Product Beta — Phase 1 (Product Workflow verification)
**Owner:** Hermes (commander)
**Status:** VERIFIED (automated) — see `export_tests`

---

## 1. User actions

| Step | Action | UI surface |
|------|--------|-----------|
| 1 | Run an analysis (see `analyzer.md`) | `AnalysisPanel` |
| 2 | Click "Export" / `ExportCommand` | `ExportDialog` |
| 3 | Choose format (txt / csv / json / html) and path | `ExportDialog` |
| 4 | Confirm | `ExportReport` |

## 2. Expected result

- The analysis result (and per-image ROI/analysis when in a compare session)
  is serialized to the chosen format.
- The exported file is valid and re-readable.

## 3. Acceptance criteria

| ID | Criterion | Target |
|----|-----------|--------|
| E-1 | Export to txt | valid file |
| E-2 | Export to csv | valid file |
| E-3 | Export to json | parseable |
| E-4 | Export to html | well-formed |
| E-5 | Per-image ROI + analysis included when present | round-trips |

## 4. Automated test

**Executable:** `export_tests` (ctest: `export_tests`)
**Source:** `src/core/test_export.cpp`
**What it drives (REAL path, not faked):**
- `ExportReport` / `AnalysisEngine` serialization to each format; verifies the
  produced file is non-empty and structurally valid (E-1..E-4).
- Confirms per-image ROI/analysis payload is carried (E-5, ties to M12.2 G2-ext).

Run:
```powershell
powershell -ExecutionPolicy Bypass -File ./build.ps1 Test
# or directly:
./build_msvc/bin/export_tests
```

## 5. Manual test

1. Run SSIM on two images, export as HTML.
2. Open the HTML — it shows both images, the metric, and the ROI.
3. Export the same as CSV and confirm it loads in a spreadsheet.

## 6. Subtraction check (RFC §1)

Documents + gates existing test (M9-4). No new core code; `ExportReport` /
`ExportCommand` already exist.

---
*Cross-refs: `docs/acceptance/user_workflow.md`, `docs/rfc/M13_PRODUCT_BETA.md` (Phase 1).*
