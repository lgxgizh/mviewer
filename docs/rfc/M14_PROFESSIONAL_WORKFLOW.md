# RFC — M14 Professional Workflow

**Status:** ACTIVE (execution)
**Author:** Hermes (commander), grounded in live `master` at `39f1ac8`.
**Date:** 2026-07-21.
**Source review:** M13 Product Beta review (2026-07-21) — "from usable software to professional image-engineering tool".

> Discipline: claims below are verified against source, not assumed. Where the
> proposal collides with an existing decision, it is flagged rather than silently
> adopted.

---

## 0. Reality check — what already exists (verified 2026-07-21)

The M14 review premise ("file ops / RAW / Compare Pro / Report Export / Plugin ABI all missing") is **partially stale**. Current `master` already has:

| Review premise | Current truth (verified) |
| --- | --- |
| "File ops (Delete/Rename/Copy/Move) missing" | **Exist.** `ThumbnailPanel::moveToTrashSelected/renameSelected/copySelectedTo/moveSelectedTo` + `RenameCommand/DeleteCommand` wired to MainWindow (F2/Delete/Ctrl+C/Ctrl+M). |
| "Compare has only basic zoom/pan" | **More than basic.** `CompareWorkspace` has sync-zoom + sync-drag checkboxes, `DifferenceEngine::differenceMap` + `heatMap` (pseudo-colormap), EventBus async diff delivery, `pixelInfo` signal with `[idx] (x,y) RGB(r,g,b)`. |
| "No pixel inspector" | **Exists (basic).** `CompareWorkspace::pixelInfo` emits cursor position + RGB. |
| "No export" | **Exists (CSV/JSON).** `ExportReport` + `buildBatchReport` + `toCsv/toJson`. |
| "No plugin system" | **Exists.** `PluginLoader/PluginManager` + `example_analyzer.dll` + ADR-005. |

**Genuinely open (the real M14 work):**

1. **Windows Native Workflow** — file ops exist but no **command-line open** (`mviewer.exe image.jpg`), no **Explorer "Open With"** shell integration, no **drag-drop** onto the .exe, no **MRU recent-files menu** beyond the existing recent-folders.
2. **RAW Workflow** — `QtDecoder` (QImage-based) handles JPEG/PNG/TIFF/BMP. **RAW formats (ARW/CR3/NEF/DNG) are NOT supported** — no decoder, no metadata panel. This is the biggest gap.
3. **Compare Pro** — diff/heatmap algorithms exist, but **no blink (flicker) compare**, **no heatmap overlay UI**, **no dedicated pixel inspector panel**, **no ROI sync** (only zoom/pan sync).
4. **Report Export** — CSV/JSON exist, but **no HTML report** (with embedded histograms / analysis / compare images).
5. **Plugin ABI Freeze** — plugin system exists but **no API version check**, **no capability negotiation**, **no compatibility handling**.
6. **Benchmarks** — no mixed-format corpus, no cold-start measurement, no performance-regression history.

---

## 1. Operating principle — finish, don't widen

Every proposed change must answer three questions; if **two** are "no", defer it:

1. Will a real user (ISP / CV / image-algorithm engineer) actually use this?
2. Can an existing module be reused instead of adding a layer?
3. Does it make the software faster / more stable / easier to maintain?

**Forbidden (carried from AGENTS.md + review):**
- ❌ Refactor `ImageRepository` / `CacheManager` / `RenderEngine`
- ❌ Modify `build.ps1` / `CMakePresets.json` / `ci.yml`
- ❌ Introduce Rust
- ❌ Introduce D3D11 (deferred until CPU Tile benchmark proves it is the bottleneck)
- ❌ Introduce external RAW libraries (libraw, RawSpeed) — license + build-complexity risk. Use self-contained TIFF/EXIF parsing for metadata; display uses QImage best-effort.

---

## 2. Task breakdown (execution order)

### M14-1 — Windows Native Workflow (P0)

**Goal:** Windows users can manage images like FastStone.

**Implementation:**
- `mviewer.exe image.jpg` — parse `__argc/__argv` or `GetCommandLineW`, open the image directly on launch.
- Explorer "Open With" — on first run (or via `--register-shell`), write `HKEY_CURRENT_USER\Software\Classes\Applications\mviewer.exe\shell\open\command`. Provide `--unregister-shell`.
- Drag-drop — accept `QFileOpenEvent` / `WM_DROPFILES`, open the dropped file.
- MRU recent-files menu — extend existing `RecentFiles` to track individual files (not just folders), show the last 10 in File menu.

**Acceptance:**
- [ ] `mviewer.exe D:/photos/img.jpg` opens the image directly.
- [ ] Right-click a .jpg → "Open with MViewer" launches the app with the image.
- [ ] Drag a .jpg onto the .exe → opens it.
- [ ] Recent-files menu shows last 10 opened images.

**Test:** `test_windows_native.cpp` — verify command-line parsing, shell-registry read-back, MRU ordering.

---

### M14-2 — RAW Workflow (P0)

**Goal:** Open ARW/CR3/DNG/NEF and show sensor metadata. This is MViewer's **core differentiator** vs FastStone.

**Design decision — Phase A (this RFC) vs Phase B (deferred):**

| Phase | Scope | Why now / later |
| --- | --- | --- |
| **A (P0, this RFC)** | RAW metadata panel + best-effort display | Metadata parsing (TIFF/EXIF) is self-contained, no external dep, no demosaic. ISP engineers value metadata MORE than preview. |
| **B (P1, deferred)** | True RAW demosaic display | Requires either libraw (GPL + vcpkg) or self-demosaic (heavy). Only justified if Phase A proves user demand. |

**Implementation (Phase A):**
- `RawMetadata` struct — ISO, exposure time, aperture, focal length, WB, black level, Bayer pattern, camera model, lens.
- `RawMetadataReader::parse(path)` — self-contained TIFF/EXIF parser (DNG/ARW/CR3/NEF are all TIFF-based). Reuse `MetadataReader` infrastructure.
- `RawMetadataPanel` (Qt widget) — display the metadata in a dock/panel.
- Display: try `QImage` (works for some TIFF-based DNG); if it fails, show a placeholder + "RAW preview not available — metadata shown".
- Register in `MainWindow` — when a RAW file is detected, auto-show the panel.

**Acceptance:**
- [ ] Open a DNG → metadata panel shows ISO / Exposure / Aperture / Lens / WB.
- [ ] Open an ARW / CR3 / NEF → same.
- [ ] Non-RAW files → panel hidden (no regression).

**Test:** `test_raw_metadata.cpp` — parse a known DNG/ARW, assert ISO/exposure/aperture are read correctly.

---

### M14-3 — Compare Pro (P1)

**Goal:** Professional comparison workflow for image-engineering.

**Implementation:**
- **Blink compare** — timer-based 500ms toggle between image A and B (classic flicker comparison).
- **Difference heatmap overlay** — reuse `DifferenceEngine::differenceMap` + `heatMap`, render as a toggleable overlay with adjustable threshold.
- **Pixel inspector panel** — dedicated dock showing: X, Y, RGB (8-bit), RAW (16-bit if available), Delta E (vs reference).
- **ROI sync** — when user draws an ROI on one image, mirror it on the other (extend existing zoom/pan sync).

**Acceptance:**
- [ ] Two 8K images: blink compare toggles at 500ms.
- [ ] Heatmap overlay shows difference as pseudo-colormap.
- [ ] Pixel inspector updates live with cursor.
- [ ] ROI drawn on A appears on B.

**Test:** `test_compare_pro.cpp` — verify blink timer, heatmap image dimensions, pixel-inspector value correctness.

---

### M14-4 — Report Export (P1)

**Goal:** Generate shareable HTML/JSON reports for algorithm evaluation.

**Implementation:**
- `ReportHtml::build(ReportContext)` — template-based HTML with sections:
  - Image (thumbnail embedded as base64).
  - Histogram (embedded PNG).
  - Analyzer results (table).
  - Compare result (diff image + metrics).
  - ROI (annotated image).
- `ReportContext` — aggregates data from `AnalysisPanel`, `CompareWorkspace`, `Histogram`.
- Menu: File → Export Report → choose HTML or JSON.

**Acceptance:**
- [ ] Export an analyzed image → HTML shows histogram + metrics.
- [ ] Export a compared pair → HTML shows diff image + PSNR/SSIM.
- [ ] JSON export is well-formed and parseable.

**Test:** `test_report_export.cpp` — build a report, assert HTML contains expected sections, JSON parses.

---

### M14-5 — Plugin ABI Freeze (P1)

**Goal:** Stable plugin interface so third-party analyzers don't break on updates.

**Implementation:**
- `#define MVIEWER_PLUGIN_API_VERSION 1` in `PluginABI.h`.
- `PluginLoader::load` checks the exported `mviewer_plugin_api_version()` against the host version. Mismatch → refuse to load with a clear error.
- `PluginCapability` enum (extend existing `AnalyzerCapability`) — plugins declare what they support (SingleImage, Region, Batch, RAW).
- Backward compat: host supports `API_VERSION` and `API_VERSION - 1`.

**Acceptance:**
- [ ] A plugin compiled against API_VERSION=1 loads on host with API_VERSION=1.
- [ ] A plugin compiled against API_VERSION=0 is rejected with a clear message.
- [ ] Plugin capabilities are queryable by the host.

**Test:** `test_plugin_abi.cpp` — load a mock plugin, verify version check + capability query.

---

### M14-6 — Benchmarks (P1)

**Goal:** Mixed-format corpus + cold-start + performance-regression history.

**Implementation:**
- **Mixed-format corpus** — extend `generate_bench_data.ps1` to emit JPEG+PNG+TIFF+BMP in one tier (e.g. 2500 each = 10000 mixed).
- **Cold-start benchmark** — new scenario `B0` that measures time from `QApplication` construction to first thumbnail (simulates app launch → open folder).
- **Performance history** — `benchmark/report/history/*.json` with `{commit, date, baseline, current, diff}`. CI step computes diff vs last baseline; if any metric regresses >20%, fail.

**Acceptance:**
- [ ] Mixed-format corpus generates 10000 images across 4 formats.
- [ ] Cold-start scenario reports a number (no crash).
- [ ] Performance history JSON is written and diff is computed.

**Test:** `test_benchmark_mixed.cpp` — verify mixed corpus generation, cold-start scenario runs.

---

## 3. Explicit non-goals

- ❌ Rust rewrite.
- ❌ D3D11 GPU backend (deferred until CPU Tile benchmark proves bottleneck).
- ❌ External RAW libraries (libraw, RawSpeed) — license + build risk.
- ❌ More cache layers (existing hierarchy is sufficient).
- ❌ Repository / architecture refactor.

---

## 4. Execution order

```
M14-1 Windows Native (P0)  →  M14-2 RAW (P0)  →  M14-3 Compare Pro (P1)
→  M14-4 Report Export (P1)  →  M14-5 Plugin ABI (P1)  →  M14-6 Benchmarks (P1)
```

Each task: design → implement → test → benchmark → doc → commit → next.

---

## 5. Next step

Start M14-1 (Windows Native Workflow).
