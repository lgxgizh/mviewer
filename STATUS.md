# STATUS — MViewer

> Snapshot: 2026-07-24 · Target: **v1.0.0 Release Candidate** (Beta → 1.0)
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
- **Robustness**: always-on Windows minidump crash handler (`CrashHandler` installed at
  startup; dumps land in `%APPDATA%/MViewer/crash-reports/`); on next launch a one-time
  **崩溃报告** dialog offers to open the crash directory. `--selftest` headless
  decode→metadata gate (CTest `selftest`).
- **GPU Stage A**: `ImageViewer` is a `QOpenGLWidget`; `GpuTileUploader` uploads
  tiles via real `glTexImage2D` when `MVIEWER_GPU=1` and a context is current,
  then composites with `QOpenGLTextureBlitter`. `available()` probes the current
  GL context (false under headless/`QCoreApplication`); CPU `drawImage` remains
  the verified default when GPU is off or upload fails.
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
- GPU Stage B/C/D: custom shaders, multi-pass, D3D11/Vulkan (UI boundary frozen).

## Known gaps

- RAW = preview-only (no demosaic); some large/edge RAW containers may fall through to the fallback decoder.
- GPU Stage A is opt-in (`MVIEWER_GPU=1`); default remains CPU tile path + HiDPI decode.
- ~~**M14.8**: release pipeline SHA256 manifest + auto-generated changelog~~ ✅ (`scripts/release_manifest.ps1`).

## Product-force progress (2026-07-24)

| 工作流 | 状态 | 说明 |
|--------|------|------|
| A-1~A-10 对账 | ✅ | `docs/review/A_ITEMS_COMPLETION_AUDIT_2026-07-24.md` — DONE ~91% |
| M16 Compare 收尾 | ✅ | Pixel Link / Overlay 透明度 / Diff 高亮 / 自定义网格 / 1~8 布局预设 / 编辑↔指标联动 / 每格直方图叠加 |
| Browse 门禁 | ✅ | Selection 统一 + 大目录渐进 fetch + 万级缩略图预测窗口 |
| M19 UI Model 收敛 | ✅ | `DirectoryModel` / `ImageListModel` / `WorkspaceModel` / `AnalyzerModel` 落地；MainWindow 镜像状态移除；Thumbnail multi-select 双向同步；Metadata Overlay `I`/`ESC` + Lens/ICC |
| M20 Compare 键盘流 | ✅ | Ctrl+2/4/8 布局预设；B/S/W/O/H/Z/D/C/L 模式键；连续导航保留模式/ROI；`?` 快捷键帮助 |
| M21 Analysis+Export | ✅ | AnalysisPanel↔AnalyzerModel History/Pin；`ExportJob` Convert 统一路径；Memory Timeline；Dashboard Canvas 趋势图 |
| Workspace 恢复 | ✅ | 布局/缩放/Compare/崩溃恢复（A-6 已落地） |
| M17 高价值子集 | ✅ | 批量分析导出 / 插件 rescan / 评分过滤导出 |
| M16 分析持久化 | ✅ | `AnalyzerModel` 历史/钉住/结果序列化到 `%APPDATA%/MViewer/analysis_history.json`，重启保留 |
| M17 自动更新 | ✅ | `UpdateChecker` 接 GitHub Releases（WinHTTP），帮助菜单「检查更新」+ 启动 8s 后静默检查，新版本弹窗引导下载 |
| 1.0 发布准备 | ✅ | 性能基线更新 + 安装包验收通过（M14.8 SHA256/notes ✅） |

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
5. Attach SHA256SUMS.txt (M14.8) and release notes from CHANGELOG.

## Strategic milestones (post-1.0)

Per the 2026-07-27 product review, the focus shifts from platform building to
product refinement around professional workflows:

- **M14 Professional Browser** — Directory tree completeness, multi-view
  (Thumbnail / List / Details / Filmstrip), unified Selection Model, Metadata
  Overlay, favorites / history / search / tags / ratings.
- **M15 Professional Compare** — Blink / Swipe / Overlay, Pixel Inspector,
  ROI sync, multi-image layouts, Compare keyboard scheme.
- **M16 Professional Analysis** — Analyzer workflow, Analysis History (persisted
  across restarts), Report Generator, Export pipeline, Session management.
- **M17 Professional Productivity** — Batch, Workspace enhancement, auto-recovery,
  release installer, crash report (always-on + launch dialog), auto-update
  checker (GitHub Releases).

**Frozen (do not refactor/extend):** CacheManager, Scheduler, DecoderRegistry,
Build System, CI, Plugin Framework, Workspace base, Performance Gate.
**Agent work ratio:** ~50% product / 25% core / 15% test-stability / 10% perf.

## Status verdict

The engine is release-grade. Product workflows (browse → compare → analyze → export)
are closed for Beta. **1.0 release preparation is complete**: performance baseline
recorded (2026-07-24, all scenarios PASS), portable zip + NSIS installer verified,
SHA256SUMS + RELEASE_NOTES generated. Ready for v1.0.0 tag.
