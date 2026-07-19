# Workflow: Browse (Open Folder → Thumbnail)

**Phase:** M13 / Product Beta — Phase 1 (Product Workflow verification)
**Owner:** Hermes (commander)
**Status:** VERIFIED (automated) — see `product_browse_tests`

---

## 1. User actions

| Step | Action | UI surface |
|------|--------|-----------|
| 1 | Launch MViewer | — |
| 2 | Click "Open Folder" / use DirectoryTree | `DirectoryTree` → `OpenDirectoryUseCase` |
| 3 | Select a directory containing images | file dialog |
| 4 | Wait for thumbnails to stream in | `ThumbnailPanel` |
| 5 | Scroll the thumbnail grid | `ThumbnailPanel` |

## 2. Expected result

- The **UI thread is never blocked** while the directory is opened (open returns
  immediately; decodes run on the `DecodePool`).
- The **first thumbnail** of the visible range appears within the review budget.
- All images in the directory are decoded and thumbnails produced.
- Scrolling stays responsive (thumbnails load on demand for the visible range).

## 3. Acceptance criteria

| ID | Criterion | Target |
|----|-----------|--------|
| B-1 | `loadDirectoryAsync()` returns without blocking on 1000 decodes | call < 100 ms |
| B-2 | All images decoded via the async open path | 1000/1000 |
| B-3 | First thumbnail of visible range emitted | < 200 ms (worst-case ceiling under CI concurrency) |
| B-4 | No crash / no leak on open + scroll | — |

## 4. Automated test

**Executable:** `product_browse_tests` (ctest: `product_browse_tests`)
**Source:** `src/core/test_product_browse.cpp`
**What it drives (REAL path, not faked):**
- `ImageRepository::instance().loadDirectoryAsync(dir, cb, 1000)` — verifies the
  call returns immediately (B-1) and the async callback delivers all 1000 frames
  (B-2).
- `ThumbnailPipeline::instance()` with `setVisibleRange(0,20)` — verifies the
  first thumbnail is emitted within budget (B-3).

Run:
```powershell
powershell -ExecutionPolicy Bypass -File ./build.ps1 Test
# or directly:
./build_msvc/bin/product_browse_tests
```

## 5. Manual test

1. Open a real folder with ~1000 mixed JPEG/PNG/TIFF.
2. Confirm the window does not freeze during open.
3. Confirm thumbnails appear within ~1 s and scrolling is smooth.
4. Spot-check a corrupt image — it must be skipped, not crash the app.

## 6. Subtraction check (RFC §1)

This spec **documents and gates existing tests**; it adds no `core/` code. The
Browse path is already wired in `MainWindow`. No new module.

---
*Cross-refs: `docs/acceptance/user_workflow.md`, `docs/acceptance/workflow_walkthrough.md`,
`docs/rfc/M13_PRODUCT_BETA.md` (Phase 1).*
