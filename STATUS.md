# STATUS — MViewer

> Snapshot: 2026-08-05 · Version: **1.0.6 (in development)** · Last release tag: **v1.0.5** (2026-07-29)
> Single source of truth for "what the product is right now". For plans, see
> `docs/roadmap.md` (engineering) and `docs/ROADMAP_PUBLIC.md` (public).
> Evidence for the claims below: `docs/review/M24_BASELINE_2026-08-05.md`,
> `docs/review/M24_TEST_CREDIBILITY_2026-08-05.md`,
> `docs/review/M24_PERFORMANCE_2026-08-05.md`,
> `docs/review/M24_FINAL_VERDICT_2026-08-05.md` and `.\build.ps1 Test`.

## Positioning

A **visual analysis platform for image algorithm engineers** — compare, validate,
and analyze image-processing outputs (camera ISP, CV pipelines, SDK versions).
The core workflow is **compare → analyze**; browsing is only the entry point.
This is **not** a general-purpose image viewer.

## Version (single source)

- `CMakeLists.txt` `project(VERSION)` is the **only** version source (M24).
- Consumers of the same source: About dialog, UpdateChecker, workspace
  `appVersion`, session-start log banner, `app.setApplicationVersion()`,
  `build_msvc/version_info.txt` → portable ZIP / installer naming.
- Generated at configure time: `build/generated/MViewerVersion.h`
  (`MVIEWER_VERSION_STRING`, `MVIEWER_VERSION_FULL`).
- Gate: CTest `version_consistency` fails if code hard-codes a version literal,
  or STATUS/installer/package scripts disagree with CMake.
- Release process: bump `project(VERSION)` → tag `vX.Y.Z` → `release.yml` builds,
  packages, and attaches artifacts; local equivalent `scripts/package_release.ps1`.

## Architecture (frozen)

```
UI (Qt Widgets) → Application (UseCases) → Core → Domain
```

- **Domain**: zero-dependency `std` types; no Qt.
- **Core**: Qt-free headers; `mviewer_core` is a **SHARED** library (one vtable
  shared host↔plugin, required by the Plugin ABI). `.cpp` internals may use Qt.
- **UI**: Qt 6 Widgets boundary only (no D3D11/Vulkan direct compositing).
- **Build**: `build.ps1` (CMake + Ninja, MSVC 2022). Never invoke
  cmake/ninja/cl directly. Qt 6.10.x / MSVC 19.44 verified on the dev machine.

## Shipped capabilities (1.0.x, CTest-verified)

- **Decode**: `DecoderRegistry` dispatches to `QtDecoder` (JPEG/PNG/BMP/TIFF/…),
  `RawDecoder` (embedded-JPEG preview for CR2/CR3/NEF/ARW/DNG/ORF/RW2/PEF/RAF/…,
  graceful fallthrough), and `QtFallbackDecoder`. RAW = **preview-only**, no
  demosaic (libraw deferred — M18).
- **Cache**: 5-level (disk/memory/…) + predictive preload.
- **Scheduler**: `TaskScheduler` + `DecodePool`; background async decode, UI never blocks.
- **Compare**: 2–8 images, synchronized zoom/pan/selection, blink, swipe, split,
  overlay, difference maps, layout presets, pixel inspector, session
  persistence (incl. blink state — M24 fix).
- **Analyze**: histogram, RGB mean, PSNR, SSIM, noise estimation, entropy,
  sharpness, MTF50, dead-pixel detection, ColorChecker Δ-E, ROI statistics —
  via `AnalysisEngine`; History + Pinned results persist across restarts.
- **Plugin SDK**: `AnalyzerRegistry` + `extern "C"` ABI (`apiVersion=1` /
  `abiVersion=1` / `sdkVersion=10000`, M14.2); kinds: Analyzer, Decoder,
  Exporter, Importer; reference examples in `plugins/example` (round-trip
  verified by `pluginregistry_tests`).
- **Robustness**: always-on Windows minidump crash handler (`CrashHandler`;
  dumps in `%APPDATA%/MViewer/crash-reports/`); one-time 崩溃报告 dialog on
  next launch; `--selftest` headless decode→metadata gate (CTest `selftest`).
- **Update check**: `UpdateChecker` (WinHTTP, GitHub Releases), Help menu
  「检查更新」 + silent check 8 s after startup; current version from
  `MViewerVersion.h` (M24).
- **Browse**: Directory tree + breadcrumb + recent/favorites/history,
  filter/sort/search, Thumbnail/List/Detail/Filmstrip views, unified Selection
  Model, Metadata overlay (`I`/`ESC`), batch rename/move/delete,
  rating store.
- **Export**: PNG/JPEG/TIFF/CSV/HTML/Report via unified `ExportJob`; batch
  export with progress/cancel; atomic temp-file replace (see Phase 4D notes).
- **Workspace**: `.mvws`/`.mvproj` persistence incl. compare session, ROI,
  analysis text; crash-recovery autosave.
- **UX**: viewer zoom command system, fullscreen, mouse back/forward, slideshow,
  gallery keyboard loop, Ctrl+wheel thumbnail sizing, drag & drop, status-bar
  dimensions, decode-failure feedback.
- **GPU Stage A (opt-in)**: `ImageViewer` is a `QOpenGLWidget`;
  `GpuTileUploader` uploads tiles via `glTexImage2D` when `MVIEWER_GPU=1`;
  CPU `drawImage` is the verified default.
- **CI**: `ci.yml` (PR gate: clang-format + cppcheck + clang-tidy +
  build/zero-warnings + CTest incl. `bench_enforce` + `golden_image`),
  `nightly.yml` (ASan/UBSan/clazy/benchmark regression/golden/Perfetto),
  `release.yml` (perf report / UI regression / Dr.Memory / package / release).
- **Packaging**: portable ZIP + NSIS installer; SHA256SUMS + auto release
  notes (`scripts/release_manifest.ps1`); both artifacts launch on a clean
  Windows (PATH stripped) — verified 2026-08-05.

## Deferred / future (not shipping now)

- Full RAW demosaic (libraw) — M18.
- GPU Stage C/D: custom shaders, multi-pass, D3D11/Vulkan direct compositing
  (UI boundary frozen).
- Linux/macOS native installers (Linux CI artifacts build; only Windows ships
  an installer).
- AI workflows (captioning, embeddings, similarity search) — M18, planned.

## Known gaps (honest, not hidden)

- RAW = preview-only (no demosaic); some large/edge RAW containers fall
  through to the fallback decoder.
- GPU Stage A is opt-in (`MVIEWER_GPU=1`); default is the CPU tile path.
- 100 MP first-viewport fill measured 1480 ms on a 2-core Xeon VM (vs 392.8 ms
  on the 2026-07-24 baseline box) — B10 is report-only by design
  (`performance_budget.json`); regression axis is `--regression`.
- C4819 warnings in a few sources (UTF-8 without BOM) and C4530 in some
  test/bench TUs are tracked in Phase 6 of M24.
- Installer/portable ship no `plugins/` directory; app logs a benign
  "Plugin directory not found" message (Phase 8 item).

## Product-force progress (2026-08-05 state)

| 工作项 | 状态 | 说明 |
|--------|------|------|
| A-1~A-10 对账 | ✅ | `docs/review/A_ITEMS_COMPLETION_AUDIT_2026-07-24.md` (~91%) |
| M16 Compare 收尾 | ✅ | Pixel Link / Overlay 透明度 / Diff 高亮 / 1~8 布局预设 / 编辑↔指标联动 / 每格直方图叠加 |
| Browse 门禁 | ✅ | Selection 统一 + 大目录渐进 fetch + 万级缩略图预测窗口 |
| M19 UI Model 收敛 | ✅ | `DirectoryModel` / `ImageListModel` / `WorkspaceModel` / `AnalyzerModel`；Metadata Overlay `I`/`ESC` + Lens/ICC |
| M20 Compare 键盘流 | ✅ | Ctrl+2/4/8 布局预设；B/S/W/O/H/Z/D/C/L 模式键；连续导航保留模式/ROI；`?` 快捷键帮助 |
| M21 Analysis+Export | ✅ | AnalysisPanel↔AnalyzerModel History/Pin；`ExportJob` 统一路径；Memory Timeline；Dashboard |
| Workspace 恢复 | ✅ | 布局/缩放/Compare/崩溃恢复（A-6 已落地） |
| M17 高价值子项 | ✅ | 批量分析导出 / 插件 rescan / 评分过滤导出 |
| M16 分析持久化 | ✅ | `AnalyzerModel` 历史/钉住/结果序列化，重启保留 |
| M17 自动更新 | ✅ | `UpdateChecker`（GitHub Releases，WinHTTP），版本来自 CMake SSOT（M24） |
| M23 质量自动化 | ✅ | ADR/Bug 门禁、自动 dashboard、test matrix、benchmark trend（ADR-015） |
| M24 稳定性收敛 | 🔄 进行中 | 基线 2026-08-05 已验证（构建/测试/启动/打包）；异步生命周期、工作流验收、测试可信度、性能、RC 验证 |

## Plugin SDK (frozen)

- ABI triple: `apiVersion=1` / `abiVersion=1` / `sdkVersion=10000` (M14.2).
- Plugin kinds: Analyzer, Decoder, Exporter, **Importer** (A-9.3).
- Examples: `plugins/example/{ExampleAnalyzer,ExampleDecoder,ExampleExporter,ExampleImporter}Plugin.cpp`.
- Docs: `docs/sdk/PLUGIN_SDK.md`, `docs/sdk/PLUGIN_ABI.md`.
- Per-analyzer metadata version (`0.1.0`) is a plugin component version, not the
  app version; the app version is `MVIEWER_VERSION_STRING` only.

## Release process (current)

1. Bump `CMakeLists.txt` `project(VERSION)` — the single source of version.
2. `.\build.ps1 Test` must be green (70 tests incl. `version_consistency`,
   `bench_enforce`, `golden_image`, the four M24 workflow acceptance suites).
3. Tag `vX.Y.Z`; `release.yml` builds, packages, attaches artifacts.
4. Verify `dist/MViewer-<X.Y.Z>-portable.zip` and `dist/MViewer-<X.Y.Z>-Setup.exe`.
5. Attach `SHA256SUMS.txt` (M14.8) and release notes from CHANGELOG.

## Status verdict

**M24 (Product Reality & Stability Convergence) complete — 2026-08-05.**
Verified: clean build from scratch (497.6 s, 0 errors, C4530 warnings 47 → 0
after the MSVC defaults were restored), **70/70 CTest green**, real-display
launch + graceful exit, portable ZIP and NSIS installer both launch on a
PATH-stripped clean environment and the installer uninstalls cleanly, SHA256
manifests generated. All detached UI workers removed (bounded pools +
QPointer marshaling); large-folder UI stall fixed (2.7 s → 184 ms on
10 000-image folders); Browse/Compare/Analyze/Export workflows exercised by
four new acceptance suites; version is a single source; STATUS/roadmap/
installer/code agree.

Open items (non-blocking, see the M24 verdict doc): 24 MP JPEG cold decode
~3.5 s and 4 K TIFF ~2.1 s on the 2-core VM (single-threaded decode);
C4819 warnings in 6 UTF-8-without-BOM sources; installer/portable ship no
`plugins/` dir (benign startup log message); in-process throw isolation for
plugin analyzers requires /EHsc (now restored on this machine) plus a
subprocess runner for hard crashes. Verdict:
**READY WITH DOCUMENTED NON-BLOCKING LIMITATIONS** — see
`docs/review/M24_FINAL_VERDICT_2026-08-05.md`.
