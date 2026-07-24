# STATUS — MViewer

> Snapshot: 2026-07-21 · Target: **v1.0.0 Release Candidate** (Beta → 1.0)
> Single source of truth for "what the product is right now". For plans, see
> `docs/roadmap.md` (engineering) and `docs/ROADMAP_PUBLIC.md` (public).

## Positioning

A **visual analysis platform for image algorithm engineers** — compare, validate,
and analyze image-processing outputs (camera ISP, CV pipelines, SDK versions).
The core workflow is **compare → analyze**; browsing is only the entry point.
This is **not** a general-purpose image viewer.

## Architecture (frozen)

```
UI (Qt Widgets) → Application (UseCases) → Core → Domain
```

- **Domain**: zero-dependency `std` types; no Qt.
- **Core**: Qt-free headers; `mviewer_core` is a **SHARED** library (one vtable
  shared host↔plugin, required by the Plugin ABI). `.cpp` internals may use Qt.
- **UI**: Qt 6 Widgets boundary only (no D3D11/Vulkan direct compositing).
- **Build**: `build.ps1` (CMake + Ninja, MSVC). Never invoke cmake/ninja/cl directly.

## Shipped capabilities (v1.0.0)

- **Decode**: `DecoderRegistry` dispatches to `QtDecoder` (JPEG/PNG/BMP/TIFF/…),
  `RawDecoder` (embedded-JPEG preview for CR2/CR3/NEF/ARW/DNG/ORF/RW2/PEF/RAF/…;
  graceful fallthrough), and `QtFallbackDecoder`.
- **Cache**: 5-level (disk/memory/…) + predictive preload; >90% memory hit ratio.
- **Scheduler**: `TaskScheduler` + `DecodePool`; background async decode, UI never blocks.
- **Compare**: 2–8 images, synchronized zoom/pan/selection, blink, difference maps.
- **Analyze**: histogram, RGB mean, PSNR, SSIM, noise estimation, entropy,
  sharpness, MTF50, dead-pixel detection, ColorChecker Δ-E, ROI statistics —
  via `AnalysisEngine`.
- **Plugin SDK**: `AnalyzerRegistry` + `extern "C"` ABI; reference example at
  `plugins/example` (loaded and round-trip verified by `pluginregistry_tests`).
- **Robustness**: opt-in Windows minidump crash handler (env `MVIEWER_CRASH_DUMP=1`);
  `--selftest` headless decode→metadata gate (CTest `selftest`).
- **GPU**: `GpuTileUploader::available()` performs a real, safe GL-context probe
  and returns false under `QCoreApplication`/offscreen; the CPU compositor is the
  verified default.
- **UX polish (2026-07-23)**: viewer zoom command system (`+`/`-`/`0`/`1`, double-click
  fit↔100%, fit-follows-resize), ESC/F11 fullscreen handling, mouse back/forward
  navigation, wrap-around prev/next, slideshow (`S`, 3 s loop), open-file dialog
  (`Ctrl+Shift+O`), full shortcut coverage (`Ctrl+O`/`Ctrl+Q`/`C`/F11), gallery
  keyboard loop (Enter opens, arrows drive selection), Ctrl+wheel thumbnail sizing,
  gallery drag & drop, live window titles, status-bar image dimensions, and explicit
  decode-failure feedback (`ImageViewer::loadFailed`).
- **CI**: `ci.yml` (gate: format+build+test+package+clazy; clang-tidy/ASan advisory),
  `release.yml` (tag/dispatch → portable zip + NSIS installer → GitHub Release),
  `nightly.yml` (clang-tidy / benchmark / ASan / llvm-sanitizer / dashboard),
  `perf-gate.yml` (hard performance gate).
- **Packaging**: portable ZIP (`scripts/package_portable.ps1`) + NSIS installer
  (`scripts/package_release.ps1` → `installer/mviewer.nsi`). A G1 guard asserts
  `imageformats/qtiff.dll` ships, so TIFF opens on a clean Windows with no Qt.

## Deferred / future (not in v1.0.0)

- Full RAW demosaic (libraw).
- GPU Stage C/D: D3D11/Vulkan direct compositing (UI boundary frozen).
- Linux/macOS native installers (Linux CI artifacts build; only Windows ships an installer).
- Full GPU Stage A (QOpenGLWidget direct compositing) — CPU tile path + HiDPI
  tile decode remain the default; opt-in via `MVIEWER_GPU=1`.
- **M14.8**: release pipeline SHA256 manifest + auto-generated changelog.

## Known gaps

- RAW = preview-only (no demosaic); some large/edge RAW containers may fall through to the fallback decoder.
- GPU tile-upload host (QOpenGLWidget, Stage A) deferred per the M13 RFC;
  HiDPI-aware CPU tile decode is active.

## Plugin SDK (frozen)

- ABI triple: `apiVersion=1` / `abiVersion=1` / `sdkVersion=10000` (M14.2).
- Plugin kinds: Analyzer, Decoder, Exporter, **Importer** (A-9.3).
- Examples: `plugins/example/{ExampleAnalyzer,ExampleDecoder,ExampleExporter,ExampleImporter}Plugin.cpp`.
- Docs: `docs/sdk/PLUGIN_SDK.md`, `docs/sdk/PLUGIN_ABI.md`.

## Release process (v1.0.0)

1. Bump `CMakeLists.txt` `VERSION` — the single source of version.
2. `.\build.ps1 Test` must be green. The asset-independent CTest subset is the
   default gate; full assets acceptance needs the ~15 GB corpus + a real display.
3. Tag `v1.0.0` → `release.yml` builds, packages, and attaches artifacts to the
   GitHub Release.
4. Verify `dist/MViewer-1.0.0-portable.zip` and `dist/MViewer-1.0.0-Setup.exe`.

## Status verdict

The engine is release-grade. Documentation and release-metadata consistency is
tracked in `docs/review/M14_RELEASE_AUDIT.md` (M14.1). Resolve blockers B1–B5
there before tagging the RC.
