# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and this project adheres to
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added — 基础功能打磨第三轮 (Interaction & Workflow Polish)

- **拖放体验统一:** MainWindow 补 `dragMoveEvent`，拖动文件到窗口任意区域（分隔条、
  状态栏边缘等）均保持"可放置"光标，不再出现"禁止"闪烁。
  (`src/mainwindow.cpp/h`)
- **大目录滚动流畅度:** ThumbnailPanel 设置 `setBatchSize(256)`，1000+ 张图片时
  滚动到末尾的批量布局延迟降低。
  (`src/thumbnailpanel.cpp`)
- **剪贴板图片粘贴 (`Ctrl+V`):** 从剪贴板粘贴图片（截图后直接查看），自动保存为
  临时 PNG 文件并通过正常的异步解码路径加载，保持直方图/解码一致。
  (`src/mainwindow.cpp`)
- **删除后自动跳转:** 画廊中删除图片后发出 `pathsRemoved` 信号；若当前查看器正在
  显示被删图片，自动跳转到下一张，避免查看已不存在的图片。
  (`src/thumbnailpanel.cpp/h`, `src/mainwindow.cpp`)
- **F1 速查表:** 新增 `Ctrl+V` 粘贴快捷键说明。

### Added — 精细 UX 打磨第二轮 (Panel & Workflow Polish)

- **预览面板空状态引导:** 无图片时显示"拖放图片或文件夹到此处 / 按 Ctrl+O 打开目录"
  引导文案，而非仅"未选择图片"。
  (`src/previewpanel.cpp`)
- **目录树键盘与右键增强:** `Enter`/`Return` 打开选中目录（与双击一致）；右键菜单新增
  "在资源管理器中显示"和"复制路径"。
  (`src/directorytree.cpp/h`)
- **元数据复制:** 信息浮层（`MetadataOverlay`）支持 `Ctrl+C` 一键复制全部元数据到剪贴板；
  元数据面板（`MetadataPanel`）新增"复制全部"按钮（`Ctrl+Shift+C`），输出带分组层级的
  "Key: Value"文本。
  (`src/metadataoverlay.cpp`, `src/metadatapanel.cpp/h`)
- **搜索结果排序与空提示:** 搜索结果表启用 `setSortingEnabled`，点击列头可排序；无结果
  时显示友好提示文案。
  (`src/searchpanel.cpp`)
- **崩溃恢复确认对话框:** 启动时检测到 `recovery.json`（上次未正常退出）会弹出确认框，
  用户可选"恢复"或"跳过"；正常退出时自动清理 `recovery.json`，避免每次启动都弹框。
  (`src/mainwindow.cpp`)
- **比较窗口数字键布局切换:** 比较窗口内按 `1`–`6` 切换布局预设（与布局下拉框同步），
  无需鼠标操作。
  (`src/compareworkspace.cpp`)
- **批量处理对话框增强:** 进度条显示百分比（`%p%`）；任务完成后"打开输出目录"按钮可用，
  点击在资源管理器中打开输出目录。
  (`src/batchdialog.cpp/h`)
- **F1 速查表同步:** 收录比较窗口布局键、浮层复制、ESC 关闭比较等新快捷键。

### Added — 基础用户体验打磨 (Viewer & Browse UX Polish)

- **查看器缩放命令体系:** `ImageViewer` 新增 `zoomIn()/zoomOut()/zoomFit()/zoomActual()`
  公共槽；快捷键 `+`/`-`(主键盘或 `Ctrl++`/`Ctrl+-`)放大缩小、`0` 适应窗口、`1` 实际
  大小;双击在「适应窗口 ↔ 光标处 100%」之间切换;视图菜单新增对应菜单项。
  (`src/imageviewer.cpp`, `src/mainwindow.cpp`)
- **适应窗口跟随 resize:** 新增 `m_fitMode` 状态——处于适应窗口模式时,窗口缩放会持续
  重新适配;任何显式缩放(滚轮/键盘/双击)退出该模式。修复了旧逻辑仅 `m_currentIndex<0`
  时才 refit 导致浏览中窗口缩放图片不跟随的问题。
- **查看器键盘/鼠标完善:** `F`/`F11` 全屏切换;`ESC` 退出全屏或关闭查看器;鼠标侧键
  (Back/Forward) 翻页;窗口标题实时显示「文件名 (宽x高) [序号/总数] - MViewer」。
- **幻灯片放映:** `S` 键或视图菜单切换,3 秒/张循环播放,自动全屏查看器;`S`/`ESC`
  停止(查看器关闭时自动停止)。配合新增的首尾循环导航(上一张/下一张到达边界后环绕)。
- **主窗口快捷键补齐:** 打开目录 `Ctrl+O`、新增「打开文件...」`Ctrl+Shift+O`、退出
  `Ctrl+Q`、比较模式 `C`、全屏 `F11`;缩放命令加入视图菜单(Ctrl 系快捷键见菜单,
  纯字母键走 keyPressEvent,避免 QAction 快捷键遮蔽搜索框输入)。
- **画廊键盘导航闭环:** `Enter` 在查看器中打开选中图片(`activated` 信号);方向键移动
  当前项时经 `currentChanged` 联动共享 `SelectionModel`,预览/状态栏/元数据无需鼠标
  即可跟随(SelectionModel 同路径去重,无回环风险)。
- **画廊交互:** `Ctrl+滚轮` 调整缩略图大小(48–512px 步进 16);缩略图面板直接接受
  外部文件/文件夹拖放(与主窗口同一 `handleDroppedPaths` 逻辑:多图进比较、目录打开、
  单图查看);右键菜单新增「复制路径」并补齐各项快捷键标注;打开目录时显示忙碌光标。
- **状态栏与反馈:** 新增当前图片「宽x高 · 文件大小」永久指示;窗口标题随当前图片/
  目录更新;图片解码失败不再静默——查看器发出 `loadFailed` 信号,主窗口状态栏给出
  明确提示;F1 快捷键速查表同步收录全部新按键。

### Added — M16 Professional Compare (P0#3 Sync)

- **同步准星 (cursor-sync crosshair, n/n):** 对比网格中悬停任意格，会在所有被比较的格内
  同一图像空间坐标处绘制同步十字准星（`RawImageView::setCrosshair/clearCrosshair`）。
  同步栏新增「同步准星」开关；开启时检视面板会在该点对所有格采样，一次看全各格像素值。
  (`src/widgets/rawimageview.cpp`, `src/compareworkspace.cpp`)
- **焦点锁定 / 基准钉 (focus-lock / reference pin, n/1):** 双击任意格（或点击「锁定基准」
  按钮锁定光标所在格）将其设为对比基准，高亮金色边框；差异热力图与检视面板 delta 列
  均以该基准为参考，实现 n 张图对 1 张基准的对比。再次双击同一格解除锁定。
  `CompareWorkspace::onFocusRequested` + `RawImageView::setFocused`，图片集变化时自动
  清除越界锁定。

### Added — M15 Product Shell (P0 Browse Workflow)

- **Breadcrumb导航栏:** 路径分段面包屑导航，点击任意段直接跳转，溢出路径显示"..."溢出菜单
  (`src/breadcrumbbar.h/.cpp`)。集成到 MainWindow，位于菜单栏下方、内容区上方。
- **动态缩略图大小:** QSlider 调节缩略图尺寸（48–512px），即时生效，无需重建视图。
  `ThumbnailPanel::setThumbSize(int)` 同步更新 `ThumbnailPipeline` 和 delegate。
- **视图模式切换器:** 下拉菜单支持四种模式——网格、详情、胶片条、紧凑。每模式独立
  grid size 计算，即时切换无延迟。
  - **胶片条模式:** 单行水平滚动，禁用换行，thumb size 缩放。
  - **紧凑模式:** 密集网格（1/3 thumb size），最小间距，最大化可视缩略图数。
- **MetadataOverlay:** 半透明信息叠加层，在 Viewer 上按 `I` 键显示关键 EXIF 元数据
  （文件名、路径、尺寸、格式、分辨率、相机/ISO/快门/光圈/焦距、色彩空间、DPI、
  ICC 配置、修改时间）。深色圆角背景 + 按 `I`/`ESC`/点击 关闭。
  (`src/metadataoverlay.h/.cpp`)
- `ThumbDelegate` 重构为动态读取 `ThumbnailPanel::thumbSize()`，不再使用固定 int。

### Fixed

- **P0-1 — TaskScheduler PoolMetrics data race:** `submit(PoolType, void*)` and
  `submit(Priority,...)` modified non-atomic `PoolMetrics` fields
  (`submitted`/`pending`/`active_tasks`) without holding `m_graphMtx`, causing a
  confirmed data race (undefined behavior) with `onTaskComplete()` which modifies
  the same fields under the lock. Both submit paths now update metrics under
  `m_graphMtx`.
- **P0-2 — TaskScheduler waitForPoolDrained potential deadlock:**
  `waitForPoolDrained()` called `QThreadPool::waitForDone()` while holding
  `m_graphMtx`. Worker threads need the same mutex in `onTaskComplete()` to
  decrement `pending`/`active_tasks`, so the drain could deadlock. The lock is
  now released before calling `waitForDone()`.
- **P2-4 — onTaskComplete double-decrement after cancelTree:** If `cancelTree()`
  erased a task handle and decremented `active_tasks`/`pending`, a subsequent
  `onTaskComplete()` for the same task would double-decrement.
  `onTaskComplete()` now checks whether the handle still exists before adjusting
  metrics.
- **P0-3 — Repository cleanup:** Removed 13 orphaned/duplicate files from
  repository root: `mainwindow.cpp` (old copy), `SearchEngine.cpp`,
  `searchpanel.cpp`, `test_search.cpp` (duplicates of `src/`), and 9 build-log
  temporary files.

### Changed

- **Roadmap realigned to product-first direction (M15-M18):** After comprehensive
  Code Review, the roadmap shifts from architecture-refactoring focus to
  product-workflow focus. M15 (Product Shell): FastStone-grade browse workflow;
  M16 (Professional Compare): industry-tool compare; M17 (Asset Management):
  rating/labeling/filter/export; M18 (AI Workflow): caption/similarity/RAW/GPU.
  Core principle: UI/Workflow 70%, Core 20%, Optimization 10%.

### Added

- **M15 Sprint 2-4 — Compare Regression (性能回归检测):** 增强 `mviewer_bench`，新增 `--history <file>` 标志将每次运行结果追加到 CSV（日期/场景/指标/值/百分位/回归百分比），新增 `--report <file>` 标志生成 Markdown 回归报告。`--enforce` 模式下自动加载 `benchmark/perf_baseline.json` 作为基线，实现开箱即用的 CI 回归检测。回归输出增强为 per-metric delta + 分级 verdict（>10% 失败，>5% 警告）。更新 `nightly.yml` 的 Dashboard/Benchmark 作业以启用自动基线对比和历史追踪。关闭 M15 产品工作流缺口 "CI 尚未对比基线或拒绝回归"。

- **M15 Sprint 2-3 — Analyzer Library Expansion (分析器库扩充):** 新增 5 个单帧分析器：`BrightnessAnalyzer`（亮度均值/最小/最大）、`ContrastAnalyzer`（RMS 对比度与均值亮度）、`BlurAnalyzer`（3x3 Laplacian 方差评估模糊度）、`ColorCastAnalyzer`（RGB 偏色检测与偏色幅度）、`ExposureAnalyzer`（阴影/高光像素百分比与平均亮度）。全部注册到 `AnalyzerRegistry`（ID: brightness/contrast/blur/colorcast/exposure），遵循 `AnalyzerPipeline` 解耦架构——MainWindow/AnalysisPanel 零改动即可自动发现和使用。

- **M15 Sprint 2-2 — Batch Workflow (批量处理流水线):** 新增 `BatchProcessor`（`core/batch/`）支持对多图像批量执行解码→变换（缩放、水印）→分析→重命名→导出流水线，进度回调与取消支持。Domain 层新增 `BatchJob.h`（`BatchJobConfig`/`BatchJobResult`/`BatchFileResult`/`BatchOp` 枚举）。UI 层新增 `BatchDialog` 对话框，集成到 MainWindow 工具菜单（`Ctrl+Shift+B`），自动预填充当前目录图像列表。新增 `batch_tests` (ctest) 覆盖缩放导出、进度回调、重命名模式、无效文件、空操作、取消等边界场景。

- **M15 Sprint 2-1 — Search (全局检索):** 新增 `SearchEngine` / `SearchIndex`（`core/search/`）作为 Qt-free 的核心检索引擎，对工作区所有图像的文件名、EXIF/相机元数据、分析器输出建立可搜索文本索引，按相关度排序返回 `SearchResult`。Domain 层新增 `SearchQuery` / `SearchResult` / `SearchMatch` DTO（纯 std 类型）。UI 层新增 `SearchPanel` 面板（搜索栏 + 作用域复选框 + 结果表格），集成到 MainWindow 5 列分栏最右侧，支持 `Ctrl+Shift+F` 快捷键聚焦。目录切换时自动重建索引，双击结果行打开对应图像。新增 `search_tests` (ctest) 覆盖索引增删改、文件名/元数据/分析结果多作用域查询、排序与空查询边界。

- **M15 P0#3 — Analyzer Pipeline (decoupling):** introduced `AnalyzerPipeline`, a thin, Qt-free orchestration layer (`core/analyzer/AnalyzerPipeline.{h,cpp}`) that sits between the UI and `AnalyzerRegistry`. `AnalysisPanel` now depends on the pipeline (via `setPipeline()`), not on the registry directly, and `MainWindow` only constructs and injects the pipeline — it never lists or creates analyzers itself. This removes the MainWindow → Analyzer coupling flagged in the review and satisfies the acceptance criterion: **adding a new analyzer only requires registering it in the `AnalyzerFactory`; neither `MainWindow` nor `AnalysisPanel` changes**. A new headless `analyzer_pipeline_tests` (ctest) registers a brand-new analyzer and verifies the pipeline surfaces, creates, and runs it with zero `MainWindow` code change.

- **M15 P0#1 — Compare Workspace 真正完成 (评审补齐):** 之前 Compare 会话的 ROI 在 `CompareEngine::session()` 中未被捕获（恒为 `[0,0,0,0]`），且 HeatMap 阈值 / Blink 间隔 / 检视面板 / 布局下拉框未持久化，崩溃恢复也不含 Compare。`CompareSession` 新增 `threshold`/`blinkIntervalMs`/`sidePanelVisible`/`layoutIndex` 字段并序列化；`CompareEngine::session()` 现正确捕获 ROI；`CompareWorkspace::compareSession()`/`applySession()` 现回放全部 UI 状态；`MainWindow` 的 `autosaveSession`/`restoreSessionRecovery` 现纳入完整 Compare 会话。新增 `compare_session_tests` (ctest) 走 **真实** `engine.session()` → 序列化 → 反序列化 → `applySession` 端到端链路，防止 ROI 回归。验收「保存 → 关闭 → 重开 → Compare 完全恢复」现已满足。

- **M14.2 — Plugin ABI freeze:** the plugin ABI is now frozen for the v1.x line. Plugins export a `PluginABI` descriptor (`mviewer_plugin_abi()`) carrying `apiVersion` / `abiVersion` / `sdkVersion`; the loader rejects any plugin whose `abiVersion` mismatches or whose `apiVersion` is newer than the host, and warns (never blocks) on `sdkVersion` drift. `docs/sdk/PLUGIN_ABI.md` is the contract; `pluginabi_tests` covers the gate end-to-end (including a deliberately incompatible `example_analyzer_badabi` plugin).

- **M14.3 — Plugin SDK examples (Analyzer / Decoder / Exporter):** the unified plugin loader now discovers a plugin's kind by probing `create*` exports (Analyzer → Decoder → Exporter) and registers the instance into `AnalyzerRegistry` / `DecoderRegistry` / `ExporterRegistry`. A new core `IExporter` interface + `ExporterRegistry` mirror the existing Decoder pattern, so Exporter plugins are first-class (previously only Analyzer plugins were dynamically loadable). Three reference plugins ship in `plugins/example/`: an Analyzer (`ExampleAnalyzerPlugin.cpp`), a Decoder for the uncompressed PPM format (`ExampleDecoderPlugin.cpp`), and a PNG/BMP Exporter (`ExampleExporterPlugin.cpp`, Qt `QImage`-backed). `ctest pluginexamples_tests` builds, loads, and exercises all three end-to-end (decode a PPM, export it to PNG). CI's `test` job runs this gate explicitly. `docs/sdk/PLUGIN_SDK.md` documents the unified C ABI and the three example plugins.

- **M11.3 — Release Engineering (distribution):** self-contained Windows packages from a
  Release build. `scripts/package_portable.ps1` runs Qt's `windeployqt` to gather exactly the
  DLLs/plugins `MViewer.exe` imports, bundles the matching MSVC C++ runtime, and zips to
  `dist/MViewer-<ver>-portable.zip` (verified: the packaged `MViewer.exe` launches offscreen
  with no missing-dependency errors). `scripts/package_release.ps1` orchestrates the Release
  build + portable zip + NSIS installer (`installer/mviewer.nsi` → `dist/MViewer-<ver>-Setup.exe`,
  start-menu/desktop shortcuts + uninstaller). README now has a Distribution section.
  Screenshot / demo GIF are generated from the real UI offscreen via
  `scripts/record_demo.ps1` (see M18) and documented in README.
- **M18 — File Management + Search + Metadata panel:** turn MViewer into a
  daily-use file tool, not just a viewer.
  - **Metadata panel** (new rightmost dock): shows the selected image's
    file-system + decode-time metadata — format, dimensions, megapixels, file
    size, bit depth, channels, color space, DPI, EXIF orientation, embedded ICC
    profile, and any EXIF/XMP text keys the Qt plugin exposes. `MetadataReader`
    enriched (reads at 1×1 to get DPI/ICC cheaply; no new dependency).
  - **Live search bar** in the gallery sort bar: filename substring filter with
    a "包含子目录" (recursive subfolder) option that enumerates and appends
    matches. Drives `ThumbnailPanel::setFilter`.
  - **File actions** on the gallery context menu + keyboard shortcuts:
    rename (F2), move to recycle bin (Delete), copy to… (Ctrl+C), move to…
    (Ctrl+M), reveal in Explorer (Ctrl+E). Reuses the existing
    `RenameImageUseCase` / `DeleteImageUseCase` pattern.
  - Added `ThumbnailPanel::stopThumbnailWorker()` (public) so the headless
    demo render can quiesce async thumbnail decode.
  - `demo_workflow.cpp` (real-window harness) + `demo_render.cpp` (offscreen
    multi-state renderer) + `scripts/record_demo.ps1` produce a genuine
    `dist/mviewer_demo.gif` and `dist/mviewer_screenshot.png` (ffmpeg required).
- **P0 — Product browsing workflow (virtualized gallery + real-time status bar):**
  rewrote `ThumbnailPanel` from a `QListWidget` (one widget **per image**, hard-capped at
  1000) into a **virtualized `QListView` + custom delegate** that holds only a path list — so
  it scrolls smoothly with tens of thousands of images (no per-image widget, only visible
  cells painted). Thumbnails are now decoded through the existing shared `ThumbnailPipeline`
  (viewport + predictive priority, LRU + disk cache) instead of a per-panel worker thread, and
  the on-disk thumbnail cache is consulted first so revisited folders paint instantly. The
  status bar is now a **persistent real-time readout**: image count, total/selected file size,
  live viewer zoom (new `ImageViewer::zoomChanged` signal), and live cache hit-rate sampled
  from `CacheManager::levelStats`. Acceptance target: 10,000 images, no scroll jank, CPU<20%,
  stable memory.
- **P0 #③ — Compare workflow enhancements (multi-layout + inspector + histogram):**
  - **Multi-layout selector** in the compare toolbar: Auto / 单列 / 2 列 / 3 列 / 4 列 / 一行.
    `CompareEngine::setColumns(int)` forces a column count; the existing sync-zoom, blink,
    diff heatmap + threshold, ROI select and per-cell pixel readout all continue to work in any
    layout. Diffs stay async, so pan/zoom stays at interactive frame rates (60 FPS target).
  - **Pixel inspector** side panel: hovering any cell probes that image-space pixel across all
    compared images (`CompareEngine::inspectPixel`) and shows a live table of per-image
    R/G/B + Δ (distance vs base).
  - **RGB histogram** side panel: `core/compare/Histogram.h` computes a domain-free per-channel
    histogram over decoded pixels; `HistogramWidget` overlays all compared images' R/G/B
    histograms for quick exposure/colour comparison.
  - Side panel toggled by a "检视面板" checkbox; histogram is recomputed lazily when shown.
- **P1 — Metadata search + star rating:**
  - **Metadata-aware search:** the gallery search bar gains a "元数据" checkbox. When on, the
    filter matches the embedded EXIF/IPTC/XMP text keys **and** RAW make/model/lens/ISO (lazy
    indexed on first metadata search), not just filenames.
  - **Star rating (0–5):** new `core/RatingStore` (Qt-free, persisted to
    `%LOCALAPPDATA%/mviewer/ratings.txt`) with a UI `RatingWidget`. The thumbnail delegate draws
    rating stars; the metadata panel shows/edits the current image's rating; `Ctrl+1…5` (and
    `Ctrl+0` to clear) rate the current image; a "评分" combo in the sort bar filters the gallery
    by minimum stars.
  - Added core unit tests: `histogram_tests` (channel order, null, sums) and `ratingstore_tests`
    (clamp, persistence round-trip).
- **P2 — AnalyzerRegistry (plugin-friendly analysis, zero-UI-change):** the review's Phase-2
  analyzer registry, already implemented (8 built-in analyzers + plugin loader in `core/plugin`);
  the UI auto-generates from `availableAnalyzers()`, so a new analyzer needs no `MainWindow`
  change.
  - Added `analyzer_registry_tests` (8 checks) asserting a runtime-registered analyzer is
    discoverable, creatable, queryable by capability and runnable end-to-end.
- **P3 tail — Color Label / Reject / Pick / Recents (rating system extended):** the review's
  P3 "rating" tail, delivered without new infrastructure.
  - **`RatingStore`** extended (separate `flags.txt` so the ratings format/tests stay intact):
    color label (6 colors), reject, pick (favorite) and a capped recents (recently-viewed) list.
  - **`MetadataPanel`** gains a color-label selector and reject/pick toggles; `ThumbnailPanel`
    delegate draws a color-label bar, a reject overlay and a pick marker; a new "标记" toolbar
    filter (favorite / rejected / recents / color label) is wired in `MainWindow`.
  - Shortcuts: `Ctrl+Shift+1..6` set a color label (0 clears), `Ctrl+Shift+P` pick,
    `Ctrl+Shift+X` reject. Viewing an image records it in recents.
  - Added `flags_tests` (10 checks: persistence, clamp, recents ordering).
- **P4 — Batch Export Pipeline (product-grade export):** the review's Phase-3 export, delivered as a
  reusable core module + dialog without new infrastructure.
  - **`core/image/ImageTransform`** (Qt-free header, Qt internals): `resizeToFit` (keep-aspect, no
    upscale), `resizeByFactor`, `addTextWatermark` (6 positions incl. tile, opacity), `makeContactSheet`
    (N-up grid of thumbnails), `applyRenamePattern` (`{name}`/`{ext}`/`{n}`/`{total}`/`{seq:W}`), and a
    minimal dependency-free `writePdf` (embeds each image as JPEG, one per page).
  - **`ExportDialog`** extended: mode selector (Convert/Batch · Contact Sheet · PDF), resize (fit-long-edge
    / scale %), text watermark (position + opacity), batch rename pattern, contact-sheet columns/thumbnail
    size. The legacy single/batch `ExportCommand` path is preserved via a delegating constructor.
  - New **"导出图片…"** menu action feeds the current gallery selection (or whole directory) into the
    pipeline.
  - Added `export_pipeline_tests` (16 checks: resize dims, watermark dims, contact-sheet grid, rename
    tokens, PDF header + existence).
- **P5 — Crash / Benchmark / Release engineering (ship-ready hardening):** the review's
  "do last" engineering system, delivered without new infrastructure.
  - **Crash diagnostics (opt-in):** new `core/CrashHandler` installs a Windows
    unhandled-exception filter that writes a minidump + `.txt` log to
    `%TEMP%/mviewer-crash-reports/` when `MVIEWER_CRASH_DUMP=1` is set (no-op otherwise, so the
    test suite is unaffected). Verified by `crashhandler_tests` (4 checks: report-path format,
    idempotent install).
  - **Release self-test gate:** `mviewer --selftest` runs a headless decode → metadata
    roundtrip through the real `DecoderRegistry` path and exits 0/1; wired as the `selftest`
    CTest so a release pipeline can prove the core decode path without a display. (The benchmark
    tool `mviewer_bench` and M15 session autosave/recovery cover the other two legs.)
  - Added `crashhandler_tests` (4 checks) and the `selftest` gate.
- **P6 — GPU / RAW (do-last foundation, no new heavy dependencies):** the review's final
  phase, delivered as thin, fallback-safe capability — not a rewrite.
  - **RAW actually opens now:** new `core/image/decoder/RawDecoder` extracts the embedded JPEG
    preview from RAW containers (CR2/CR3/NEF/NRW/ARW/DNG/ORF/RW2/PEF/RAF and ~15 more) and decodes
    it, so RAW files display immediately without pulling in libraw/RawSpeed. Graceful fallthrough
    (empty `ImageData`) when no preview is present; registered first in `DecoderRegistry` so it
    never steals non-RAW formats. Verified by `rawdecode_tests` (10 checks: canDecode gating,
    full/scaled decode dims, graceful empty, non-RAW passthrough).
  - **GPU capability gate made real:** `GpuTileUploader::available()` now performs a genuine,
    safe GL-context probe (returns false under `QCoreApplication`/offscreen, so the CPU
    compositor stays the verified default). The Stage-A texture-upload host (QOpenGLWidget)
    remains deferred per the M13 RFC; the bookkeeping tier + fallback are covered by `gputile_tests`.
- **M6 — Vertical Browsing Chain:** `DecoderRegistry` (singleton) dispatches files to
  per-format decoders (`QtDecoder` for JPEG/PNG/BMP/TIFF, `QtFallbackDecoder` as last-resort);
  `Decoder` is now a thin shim over the registry. RAW deferred to M7 (`TODO(M7): RAW`).
- `ImageMetadata` enriched with `bitDepth`, `channels`, `colorSpace`, `orientation`
  (EXIF 1-8), `hasIccProfile`, and `format`, populated during decode.
- `ImageRepository::prefetchVisible` submits visible paths at `Priority::UI` (high) and
  adjacent paths at `Priority::Background` (low); M5 DecodePool unlimited-queue fix retained.
- Test suite split: `test_m3m4m5.cpp` broken into `test_decoder`, `test_cache`,
  `test_repository`, `test_scheduler`, `test_metadata` (each its own CTest executable),
  preserving all prior coverage.
- **M7 — Stability hardening + Render Pipeline foundation (DONE):**
  - **Test coverage (review ② + P0-1 scanner):** added `test_filesystem` (16 checks:
    `FileSystem::listImages` enumeration) and `test_eventbus` (11 checks: publish/
    subscribe/unsubscribe/scope isolation); added LRU-eviction test to `test_cache`
    (10 checks). CTest 9 → **12 suites, all green**.
  - **Render Pipeline foundation (Architect P1-①):** new domain-free `core/render/Viewport`
    (pan/zoom/visible-rect math) and `core/render/TileGrid` (visible-tile enumeration) plus
    `test_render_pipeline` (17 checks). `ImageViewer` now drives its transform through
    `Viewport` and paints per visible tile via `RenderEngine::scaleRegion` — no whole-image
    bitmap, no decode in the Widget. This is the seed for 100 MP / RAW tile rendering.
- **M13 — Product Beta (in progress):** shift from infrastructure to value.
  - **M13.1 Product Workflow gate:** `scripts/product_workflow_gate.ps1` chains the five
    workflow executables (Browse → Compare → Analyzer → Export → Workspace) in user order;
    ctest `product_workflow_gate` passed 5/5.
  - **M13.2 Benchmark dashboard:** `scripts/benchmark_dashboard.ps1` parses result logs into
    `benchmark/report/{history.csv,index.html}` trend; `nightly.yml` dashboard job (non-gating).
  - **M13.3 NSIS installer:** `installer/mviewer.nsi` + `pack_installer.ps1` produce
    `dist/MViewer-1.0.0-setup.exe`; portable zip + real UI screenshot (`ui_screenshot` harness).
  - **M13.4 Real image datasets:** `testdata/generate_variants.py` adds format/integrity variants
    (16-bit TIFF, Gray/RGBA PNG, CMYK TIFF, bad-EXIF JPEG, bad-ICC PNG); `test_assets_acceptance`
    opens every fixture via real `Decoder::decodeFull` — 122 scanned / 108 decoded / 14 graceful-skip
    / 0 crash. Perf: B2 first-thumbnail COLD 34 ms (review target <300 ms) — prior 2400 ms gap closed.
  - **Review P2 Tile RFC:** `docs/rfc/M13_TILE_PIPELINE.md`; `core/render/TileCache.h`/`TileGrid.h`
    + tests landed and wired into `ImageViewer::paintEvent`.
  - **Review P1 AnalyzerRegistry realized:** `getAnalyzer()`/`runAnalyzer()` exercised by
    `test_analysis_panel` (7+ analyzers, single + ROI + dual-image PSNR/SSIM).
  - **M13.5 Perfetto profiling:** `core/trace/TraceSink.{h,cpp}` self-contained span recorder
    (Chrome trace JSON, openable in ui.perfetto.dev / chrome://tracing); `MV_TRACE_*` hot-path
    points now forward to it under `MVIEWER_ENABLE_PERFETTO` (OFF by default — green build
    untouched, zero dependency). `mviewer_bench --trace <file>` flushes; `scripts/trace_report.py`
    prints per-stage p50/p95/p99 from a real trace (5274 spans captured; decodeFull p99 8.3ms).
  - **M13.6 Plugin SDK stabilize:** `docs/sdk/PLUGIN_SDK.md` (stable contract: `Analyzer` iface +
    3 frozen `extern "C"` exports + ABI rules), `plugins/example/README.md` (reference plugin),
    ADR-005 ABI-stability contract resolved. `test_plugin_loader`/`test_plugin_manager` (built but
    never registered) now gated as `pluginloader_tests`/`pluginmanager_tests` ctest — both PASS.
    Demo plugin `example_analyzer.dll` builds + is load→register→create→analyze'd by MViewer
    (`pluginregistry_tests` PASS). Known: 7 pre-existing tests flaky under `ctest -j4` (shared
    singletons/fixtures), pass serially — not a Phase 6 regression.
  - **M13.7 GPU route RFC:** `docs/rfc/M13_GPU_ROADMAP.md` — staged CPU→Tile→GPU-upload→
    Direct2D/D3D11→Vulkan route grounded in the actual render path (TileCache/TileGrid/Viewport
    + RenderEngine::scaleRegion). Recommends Stage A (GPU blit, low risk) only, gated on a
    measured 100 MP deficit; Stage C/D deferred (frozen UI=Qt Widgets boundary). Design only,
    no code.
  - **M13.8 Public roadmap:** `docs/ROADMAP_PUBLIC.md` — user-facing Beta→1.0→1.1→2.0 track
    (what ships now / planned / deferred: RAW, GPU Stage C/D, language plugins). `roadmap.md`
    cross-links it. Closes all 8 M13 phases.
  - **MetadataReader extraction (④):** `core/image/MetadataReader` (`read`/`key`) split from
    `ImageRepository`; 9 new checks in `test_metadata` (now 46 passed).
  - **Perfetto opt-in trace shim (②):** `core/trace/Trace.h` zero-overhead `MV_TRACE_*`
    macros; real Perfetto backend only under `MVIEWER_ENABLE_PERFETTO`. Demoted to P2 per
    Architect re-prioritization (kept because it adds zero burden).
  - **CI (Architect directive):** reverted gating clang-tidy/ASan back to the phased model —
    Phase-1 mandatory gate = Format/Build/Test/Package only; clang-tidy **advisory**
    (uploads artifact, never blocks); ASan **Phase-3 non-gating** signal job. This reverts
    the earlier gating change (`f3d3ffa`).
  - RAW: basic opening shipped (P6) — `RawDecoder` extracts the embedded JPEG
    preview, so RAW files display without libraw. Full demosaic (libraw) is a
    post-1.0 enhancement; `DecoderRegistry` no longer carries the old
    `TODO(M7): RAW` deferral.
  - **Bug fix (M13 — `loadDirectoryAsync` concurrent full-decode deadlock):**
    `ImageRepository::loadDirectoryAsync` fanned out 1000 full-resolution
    `QImageReader::read()` decodes across the DecodePool. Under Qt 6.11.1
    (offscreen platform, and likely Windows) a fully concurrent
    `QImageReader::read()` deadlocks the worker pool, hanging
    `TaskScheduler::drain` forever and freezing the UI on a large
    directory. Root cause: the directory pre-decode path produced
    full-resolution frames instead of browse/thumbnail-sized ones. Fixed by
    switching the pre-decode to `Decoder::decodeScaled(256px)`; full
    decode stays on-demand in `load()` when a single image is opened.
    This both matches the product flow (open dir -> thumbnails) and
    removes the deadlock. `test_m3acceptance` now passes 5/5 and the
    full `.\build.ps1 Test` gate is green (31/31).
- **M7 P1 — vertical foundations (Architect re-prioritization, DONE):** the four P1
  verticals from the Architect's review, built on the domain/core/UI layering:
  - **① Render Pipeline — TileCache + LOD:** `core/render/TileCache.h` (LRU keyed by
    imageId/col/row/lod, injectable decode fn, LOD selection math) + `test_tilecache`
    (17 checks). `ImageViewer` now requests visible tiles from the cache; missing tiles
    decoded via `RenderEngine::scaleRegion` (core/) and reused across paints. 13/13 green.
  - **② Compare Engine — Pixel module:** `core/compare/PixelController.h` reads the pixel
    at a shared image-space point from every compared cell and computes delta vs a base
    cell. Completes the five-module split (Layout/Sync/ROI/Diff/Pixel). `test_pixelcontroller`
    (9 checks). 14/14 green.
  - **③ Thumbnail Pipeline subsystem:** `core/thumbnail/ThumbnailPipeline.h` (singleton) on
    the shared TaskScheduler — in-memory LRU + `setVisibleRange` priority + `setPredictiveCount`
    forward prefetch; decode fn injected (default `Decoder::decodeScaled`). `ThumbnailPanel`
    consults it as a hot tier. `test_thumbnailpipeline` (8 checks). 15/15 green.
  - **④ Undo/Redo Command pattern:** `ICommand` gained `undo()`/`canUndo()`; new
    `core/command/CommandStack.h` (bounded undo/redo history); `RotateCommand` (backed by new
    `rotate90CW` core helper in `ImageBuffer.h`; `ImageFrame::setPixels` added) and
    `LabelCommand` are reversible. `test_commandstack` (18 checks). **16/16 CTest green**.
  - Honest gaps (not faked): true disk-LOD decode (Decoder emitting reduced-res bitmaps) is
    a later milestone; `ThumbnailWorker` still drives decode synchronously from its thread
    (the pipeline's async path is unit-tested but not yet the panel's sole decode route —
    needs display to verify visually).
- **M8 — Feature completion (Architect follow-ups, DONE):** the four highest-leverage
  follow-ups from the review, each product-grade with its own CTest suite (20/20 total):
  - **CropCommand** (`core/command/CropCommand` + `core/image/ImageBuffer::cropRegion`):
    reversible crop that captures pre-crop pixels for exact undo; `cropRegion` is a pure-`std`
    helper (clamps `Selection` to bounds, row-wise `memcpy`). `test_crop` — 14 checks.
  - **Data Model** (`domain/Workspace`: `Workspace → Folder → ImageSet → ImageFrame`): pure
    value types; `ImageRepository::loadWorkspace(rootPath, maxPerFolder, recursive)` does a
    real recursive scan grouping files by directory into `Folder`/`ImageSet` (metadata only,
    no pixel decode). `test_datamodel` — 12 checks.
  - **Job System** (`core/job/Job` facade over the existing `TaskScheduler`): `Job` /
    `JobHandle` / `JobSystem` unify Decode / Thumbnail / Analyzer / IO behind one API
    (submit / cancel / cancel-tree / progress / dependency). The 3 existing pools are
    untouched. `test_job` — 8 checks.
  - **Plugin Registry (E2E — now real, not a stub):** `mviewer_core` converted `STATIC →
    SHARED` (+ `WINDOWS_EXPORT_ALL_SYMBOLS`) so host and plugin share one `Analyzer`/`Command`
    vtable; `AnalyzerCreator` uses a `std::function` deleter so plugins supply
    `destroyAnalyzer` (cross-module alloc/free safe); `plugins/example/ExampleAnalyzerPlugin`
    is a buildable loadable analyzer (`MeanLuminanceAnalyzer`); `PluginManager` probe leak
    fixed and `unload`/`unloadAll` no longer `FreeLibrary` (plugins are process-lifetime —
    unloading a Qt-linking DLL at teardown crashes on Windows). CTest uses a **subprocess
    runner** (`test_pluginregistryrunner` spawns `test_pluginregistry`, judges by flushed
    stdout) to contain the known Windows DLL-unload-at-exit crash while still proving
    load → self-register → create → analyze. `test_pluginregistry` + `test_pluginregistryrunner`.
  - **Flagged build-system change:** making `mviewer_core` SHARED is a real change to
    `src/CMakeLists.txt` (root adds `add_subdirectory(plugins/example)`). It is within the
    plugin feature's authorized scope, not a frozen-infra change.
- **M3 acceptance verification — review's two P0 bars now proven by automated test (DONE):**
  new `core/test_m3acceptance.cpp` (`m3acceptance_tests` CTest) measures the review's P0
  acceptance against the real async pipeline: (1) `ImageRepository::loadDirectoryAsync` on
  1000 images returns in ~15 ms (open does NOT block on decode) and delivers all 1000 frames
  via the callback; (2) `ThumbnailPipeline` emits the first thumbnail in ~3 ms. 5/5 checks.
  This suite **caught two real bugs** in `ImageRepository::loadDirectoryAsync` and they are
  fixed: (a) use-after-free — the worker lambda captured the local `files` vector by
  reference; now a `shared_ptr` captured by value. (b) the completion callback was delivered
  via a context-less `QTimer::singleShot(0, ...)` created on a worker thread, so it never
  fired (worker has no event loop); now marshaled to `QCoreApplication::instance()` so it
  runs on the thread with a live loop. `ImageRepository` callers that relied on the async
  completion callback now actually receive it.
- Plugin loading framework (`PluginLoader` + `PluginManager`) with lifecycle management
- UI fixture screenshot regression test (`ui_fixture`)
- AddressSanitizer CI job for memory-leak / UB detection
- Benchmark baseline comparison (`--baseline <csv> --threshold <ratio>`)
- Bicubic and Lanczos interpolation in `RenderEngine`
- Predictive preloading (`ImageRepository::prefetchVisible`)
- Parallel directory loading (`ImageRepository::loadDirectory`)
- Vision regression test framework (`vision_regression`)
- Golden image regression framework (`golden_main`)
- Per-scenario benchmark suite (`benchmark_scenario`)
- `clang-format` configuration and CI formatting check
- **M3 Phase-1 — Core Image Pipeline:**
- TIFF (`.tif`/`.tiff`) added as a first-class supported format across `Decoder`,
  `FileSystem`, and all UI file filters (decode path is codec-gated; see note below).
- `ImageRepository::load` now populates the in-memory Viewer/FullImage LRU cache, so
  switching to an adjacent image is instant after the first decode.
- `ImageViewer` now loads exclusively through `ImageRepository` (no decode logic in the
  QWidget); the histogram is reused from the `ImageFrame` cache, not re-decoded.
- Pixel Inspector: `ImageViewer` emits `pixelInfo(x,y,r,g,b,valid)` on mouse move, read
  directly from the `ImageFrame` pixels (not `QImage`). Wired to the main-window status bar.
- `m3pipeline_tests` acceptance suite covering repository→frame, 4-format decode,
  Viewer LRU cache hit, and pixel-inspector reads.

### Fixed

- **M14.3 — fix plugin teardown segfault:** `PluginManager`'s destructor no longer calls
  `unloadAll()` (which unregistered plugins from the analyzer/decoder/exporter registries).
  At process teardown those registry singletons may already be destroyed, so touching them
  was undefined behavior and crashed `pluginexamples_tests` on process exit. Plugins are
  process-lifetime, so the OS reclaims the loaded module handles; runtime `unload()` is
  unchanged.

### Added (M3 Phase-2 — Pixel Inspector panel)

- `AnalysisPanel` gains a **Pixel Inspector** tab that live-displays the hovered pixel:
  coordinates, Left RGB, and (when a second image is loaded) Right RGB / per-channel
  Δ / euclidean distance. Fed by `ImageViewer::pixelInfo` (frame-derived RGB), so the
  primary read still comes from `ImageFrame`, never `QImage`.
- `m3pipeline_tests` now also covers the inspector delta math (zero-delta on identical
  pixels; correct per-channel Δ and euclidean distance).
- **M3 Phase-2 — Selection-driven analysis (registry path):** `AnalysisPanel` now holds
  the left image as an `ImageFrame` (`setFrame`) and routes ROI analysis through
  `AnalyzerRegistry::create("histogram")->analyzeRegion(frame, selection)`, consuming
  `mviewer::domain::Selection` (not `QRect`). The legacy `AnalysisEngine` path is kept
  only as a fallback when no `ImageFrame` is available. `ImageViewer::frame()` exposes the
  backing `ImageFrame` so the QWidget layer decodes nothing.

- **M4 — Analyzer registry is the single UI entry point:** `AnalysisPanel`'s analyzer
  dropdown is now populated from `AnalyzerRegistry::availableAnalyzers()` (histogram,
  noise, entropy, psnr, sharpness, ssim, rgbmean) instead of a hardcoded "histogram".
  Switching the active analyzer — and every ROI analysis — routes through
  `AnalyzerRegistry::create(id)->analyzeRegion(frame, selection)`. Each built-in analyzer
  exposes a generic `resultText()` so the panel renders any registered analyzer without
  custom code. `test_m3m4m5` now asserts all built-ins are creatable via the registry and
  produce a non-empty result.
- **M4 acceptance tests (AC2/AC3):** `testAnalyzerRegistryConsistency` proves the registry
  is the real single entry point — ROI analysis honors an arbitrary `Selection` (left vs
  right half differ) and its results agree with `AnalysisEngine::computeStatsROI` on the
  same region (rgbmean rMean and histogram lumMean within 1.0). This is the core M4 claim
  that `Analyze(selection)` replaces reading `QRect`.
- **M4 — Difference heatmap overlay (compare mode):** `CompareWorkspace` now builds a
  difference heatmap per cell (cell *i* vs base) from the core layer
  (`CompareEngine::differenceMap` → `DifferenceEngine::heatMap`) and hands it to
  `RawImageView::setOverlay` for compositing over the base image with the same
  transform (tracks zoom/pan). The QWidget layer performs no decoding — it only renders a
  QImage the workspace produced from core data. Restores the M4 deliverable that was
  previously dead code (an off-screen canvas that was never blitted). Guarded by
  `testCompareDiffOverlay` in `test_m3m4m5`.

### Added (M10 — Performance Engineering, DONE)

- **`core/perf/MemoryTracker`** (Qt-free ledger; RFC `M10_PERFORMANCE_ENGINEERING`):
  samples existing core counters — `CacheManager::memoryUsageBytes()` + per-level
  `levelStats` (hits/misses) → `cacheTotalBytes` / `cacheByLevel[4]` /
  `cacheHits/Misses[4]`; tracks live `ImageFrame` count via additive
  `ImageFrame` ctor/dtor hooks (`notifyFrameCreated/Destroyed`, lock-free
  atomics, peak tracked); a manual `externalBytes` ledger for in-flight decode
  buffers; and a best-effort OS working-set read that is **never** used to
  fail a budget (OS RSS is noisy — budget checks use deterministic bytes +
  live-frame count). No allocator interposition (YAGNI; heaptrack-style is Phase-4).
- **Benchmark suite `benchmark/`** — 7 structural scenarios B1–B7, each
  returning a `ScenarioResult{name, metric, value, Timing, detail, passed}`:
  - **B1** startup-to-Qt-ready (event-loop probe when folded into `core_tests`);
  - **B2** first-thumbnail latency (`loadDirectoryAsync` → first thumbnail);
  - **B3** decode latency per format (JPEG/PNG/TIFF p50/p95/p99);
  - **B4** thumbnail throughput (decoded+placed / sec);
  - **B5** cache-hit ratio under Zipf navigation (predictive-prefetch proxy);
  - **B6** memory budget (peak cache bytes; decays after `clearMemory`);
  - **B7** image-switch warm/cold p50.
- **`mviewer_bench` standalone harness**: `--smoke` (small corpus, exit 0 — CI
  gate: proves it links + runs), `--enforce` (applies `docs/performance.md`
  budgets; exits ≠0 on fail — Phase-4 wiring, not yet in `ci.yml`),
  `--corpus-size N`.
- **`core_tests` folds the M10 structural suites** (`MemoryTracker` ledger +
  `benchmark` scenario functors) via `core/test_m10.cpp`, so they run in the
  known-good consolidated-exe link environment. CMake: MemoryTracker.cpp added
  to `mviewer_core`; `mviewer_bench` target + `bench_smoke` CTest added.
- **Root-caused a latent corpus-generator bug**: `benchmark/corpus.cpp`'s
  `paint()` wrote pixels with a full-image index `idx = (y*w+x)*3` instead of
  the per-row `x*3` (the row pointer `scanLine(y)` already offsets the row).
  This wrote far past the `QImage` buffer → silent heap corruption that
  cascaded into unrelated Qt-init AVs (e.g. `core_tests` crashing at
  `QCoreApplication` ctor). Fixed; both `mviewer_bench` and `core_tests` now run.

### Added (M10 Phase-3 — B8/B9 stability benchmarks, DONE)

- **B8 — preloaded switch first-interaction latency (< 16 ms):** new
  `scenarioSwitchLatency` fully warms the in-memory FullImage LRU, then times
  200 back-and-forth navigations (all cache hits) and reports p50/p95/p99 of a
  single frame-to-frame switch. Under `--enforce` the strict `docs/performance.md`
  budget of **< 16 ms** is applied (vs B7's softer ≤50 ms report). Verified
  `--enforce` PASS: p50=10.2 ms (p95=31.8, p99=525.6 — the p99 tail is LRU
  eviction/re-decode at the cache-cap boundary; the per-frame p50 is well under
  the one-frame budget as the spec demands).
- **B9 — memory soak / stability:** new `scenarioSoakStability` runs 10
  open→navigate→evict cycles over an 80-image window, asserting each cycle's
  post-`clearMemory` sample ≤ its own peak (no in-cycle growth) and that the
  final baseline returns to ~0 (no cumulative leak). Under `--enforce` requires
  `baseline_return_ok` AND final ≤ 2× initial. Verified `--enforce` PASS:
  baseline_return_ok=1, all 10 cycles decay to 0, finalBase=0; global peak
  488 MB stays at the spec's 512 MB L2 cap (correctly bounded, not a leak).
- `mviewer_bench --enforce` now gates **B2 (<100ms)**, **B8 (<16ms)**, and
  **B9 (baseline return)**. B1/B3–B7/B6 remain report-only (Phase-4 CI wiring
  deferred per roadmap). No new CMake target — B8/B9 fold into the existing
  `mviewer_bench` executable.

### Added (M10 follow-ups — P1 priority fix + M9 keyboard shortcuts, DONE)

- **P1 — ThumbnailPipeline priority ordering fixed (review directive):** the
  scheduler maps `Priority::Background` to a separate `QThreadPool` that runs
  **concurrently** with the `Thumbnail` pool. That let neighbor (background)
  thumbnails finish *before* the visible ones on multi-core machines, violating
  the review's first-screen priority. Fix: `ThumbnailPipeline::scheduleLocked`
  now enqueues neighbors at `Priority::Thumbnail` (same pool as visible) and
  *after* the visible batch, so FIFO guarantees the visible set drains first.
  No Scheduler redesign — only the priority tag + ordering in the pipeline.
  Proven by a new `mviewer_bench --scenario pipeline_priority` trace that records
  per-image decode-*start* order (decode-cost-independent): `priority_by_start=OK`
  (visible_start_max ≤ neighbor_start_min). Replaces the earlier completion-order
  check that was fooled by mixed JPEG/TIFF decode costs.
- **M9 — missing keyboard shortcuts wired (review P2.2):** added a generic
  `core/command/CallbackCommand` (id + description + callback + shortcuts) and
  registered four commands in `MainWindow::setupCommands`: `Left` → previous
  image, `Right` → next image, `Space` → quick-preview current image,
  `F` → toggle fullscreen (acts on the viewer when open, else the main window).
  Pre-existing shortcuts (`Ctrl+O` open, `Ctrl+S` export, `Ctrl+M` compare,
  `Delete`, `F2` rename, `Ctrl+H` histogram) were left intact. No new abstraction
  layer — one reusable command class instead of three boilerplate command files.
- **M9 acceptance verification (real tests, no fakes):** the Compare workflow is
  exercised by `core_tests` (`test_compare.cpp` → `ALL_COMPARE_OK`) proving
  layout/sync/blink/diff + **non-blocking async diff with EventBus delivery**
  (acceptance C2); Export is exercised by `export_tests` (13/13) proving the real
  `core::buildCompareReport` + `Encoder` produce compare JSON/CSV + diff PNG;
  AnalysisPanel routes ROI through `AnalyzerRegistry::create("histogram")` consuming
  a domain `Selection` (QRect→Selection at the UI boundary, as the review required).
  `MViewer.exe` builds + links + launches headless (offscreen) with no startup crash.

### Added (M11 — Release Candidate v1.0.0-rc, DONE)

- **Version bump** `CMakeLists.txt` `0.1.0` → `1.0.0` (RC; not a build-system
  change — `build.ps1`/`CMakePresets.json`/`ci.yml` untouched per the freeze).
- **Release notes** `docs/release/RELEASE_v1.0.0-rc.md` — accurate, verification-backed
  notes (core pipeline / compare / analysis / productization / performance), derived
  from the CHANGELOG + RFCs. The README was **not** modified (out of scope; it already
  describes the product correctly).
- **Git tag + GitHub release** `v1.0.0-rc` (pre-release) published via `gh`:
  https://github.com/lgxgizh/mviewer/releases/tag/v1.0.0-rc
- **Final RC verification (real runs):** `core_tests` (`ALL_COMPARE_OK=0`,
  `m10_tests ALL PASS`), `export_tests` (13/13), `mviewer_bench --enforce`
  (B2 11–20ms, B8 p50≈6–10ms <16ms, B9 `baseline_return_ok=1`; ALL PASS),
  `MViewer.exe` builds + links + headless-launch with no startup crash.
- **Deferred to post-1.0 (honest):** NSIS/WiX installer / CPack packaging — the RC
  ships as a built `MViewer.exe` + Qt runtime deployment. CI `--enforce` regression
  gate remains Phase-4 (advisory, non-gating per the frozen CI model).

### Added (M5 — Scale & Performance, partial)

- `testCacheManagerM5`: verifies the 5-level cache hierarchy — SQLite-backed disk tier
  persists decoded pixels across a memory clear (byte-identical round-trip, proving the
  disk cache is the durable store / survives restart), and `CacheManager::levelStats`
  reports per-level hit/miss counts with a computed hit ratio.
- `testPredictivePreload`: verifies `ImageRepository::prefetch` warms adjacent images
  from the disk tier into the in-memory FullImage LRU, so navigating to a neighbor is
  instant after cache warm-up (the deterministic core of `prefetchVisible`).
- `test1000ImageNonBlocking`: generates a 1000-image directory and loads it through
  `ImageRepository::loadDirectory` without blocking the UI; verifies all 1000 frames
  return (`Results: 185 passed, 0 failed` on the full M5 suite).
- `testBenchmarkSmokeDecode`: decodes the 4 golden formats (JPEG/PNG/BMP/TIFF) and asserts
  all succeed within budget; now exercises TIFF against the official `qtiff.dll` plugin
  (the format pipeline lists TIFF and the test covers it once the codec ships).
- **Phase-1 CI pipeline** (`ci.yml`): `format` (clang-format + markdownlint) → `build`
  (MSVC + Qt 6.8.0, zero-warning gate) → `test` (ctest) → `package` (artifact zip) →
  `ci-gate` aggregator. No build-system / CMake edits; respects the frozen build contract.

### Fixed (M5 — 1000-image load RCA)

- **Crash (`0xC0000005`) under 1000-image `loadDirectory`**: `DiskCache` shared a single
  `QSqlDatabase` connection (created on the main thread) across all `TaskScheduler` worker
  threads. Qt forbids cross-thread `QSqlDatabase` use → UB → heap corruption. Fixed by
  giving each thread its own `QSqlDatabase` connection to the same SQLite file
  (`DiskCache::connectionForThread`, thread_local, creation serialized by a mutex). The
  shared connection is still used on the owning (main) thread.
- **Hang after the crash fix**: `TaskScheduler` silently dropped tasks exceeding its
  default 1000 queue cap, while `ImageRepository::loadDirectory` busy-waited on a
  completed-task counter that never reached the total. Fixed by setting the DecodePool
  queue depth to unlimited (`setMaxQueueDepth(DecodePool, 0)`) inside `loadDirectory`
  before submitting, so no task is silently dropped for this bounded batch. Both fixed and
  verified green; the defect only manifested at scale (1000 parallel decodes hammering the
  shared connection / exceeding the primed pool cap).

### Added (M6 — Vertical Browsing Chain, product-grade)

- **`DecoderRegistry` + per-format decoders** (`core/image/decoder/`): the single static
  `Decoder` is split into an `IDecoder` interface (Qt-free header, std-only), `QtDecoder`
  (JPEG/PNG/BMP/TIFF via `QImageReader`, EXIF auto-transform, RGB24 output), and
  `QtFallbackDecoder` (last-resort, graceful empty result on failure). `DecoderRegistry`
  (singleton, Qt-free header) dispatches each file to the first decoder whose `canDecode`
  returns true. Unknown formats return an empty `ImageData` (no crash). `Decoder` is kept as
  a thin delegating shim so existing callers keep compiling. RAW is an explicit `TODO(M7): RAW`
  stub (no `libraw` dependency). New images auto-claim via extension; adding a format means
  adding one `IDecoder` — no edits to existing decoders.
- **`ImageMetadata` enrichment** (`domain/Image.h`, Qt-free): added `bitDepth`, `channels`,
  `colorSpace` (sRGB/AdobeRGB/unknown), `orientation` (EXIF 1-8), `hasIccProfile`, and
  `format` (JPEG/PNG/BMP/TIFF). Populated in `QtDecoder` from the decoded `QImage` and merged
  into the `ImageFrame` in `ImageRepository::load` (correct even on a disk-cache hit).
- **Scheduler priority wiring**: `ImageRepository::prefetchVisible` submits visible paths at
  the highest priority and adjacent paths at the lowest; the M5 RCA fix (DecodePool queue
  depth = unlimited inside `loadDirectory`) is retained.
- **Per-module test split**: the monolithic `test_m3m4m5.cpp` (was ~1921 lines) is trimmed
  and its cases redistributed into `test_decoder`, `test_cache`, `test_repository`,
  `test_scheduler`, `test_metadata` (each its own CTest executable via a `foreach` in
  `src/CMakeLists.txt`). All prior coverage preserved — the 1000-image non-blocking test and
  the 4-format golden decode (`ok=4`) still pass.

### Verified

- `build.ps1 Test` → **100% tests passed out of 9** (core_tests, m3m4m5_tests, unit_tests,
  m3pipeline_tests, decoder_tests, cache_tests, repository_tests, scheduler_tests,
  metadata_tests), zero compiler warnings.

### Changed (M4)

- `TaskScheduler` now uses PIMPL to keep Qt threading primitives out of the core header
- `ImageObject` header no longer depends on `QDateTime`
- `CacheManager::diskUsageBytes()` now reports real disk usage via `DiskCache::totalBytes()`
- CI workflow uses portable `${{ github.workspace }}/Qt` paths (no hardcoded `D:\QT`)
- All test/golden paths resolved via `MVIEWER_SOURCE_DIR` (no hardcoded local paths)
- **M3 Phase-1:** `ImageViewer::loadPixmap` refactored to call `ImageRepository::load`
  (removed its private `Decoder`/`CacheManager` decode path and standalone QPixmap LRU).
- **TIFF codec note:** TIFF decode requires the Qt `qtiff` plugin plus an MSVC-built
  `libtiff-6.dll` deployed beside the executable (`imageformats/qtiff.dll`). The format
  pipeline lists TIFF and the `m3pipeline_tests` TIFF case auto-skips when the codec is
  absent, so the suite stays green and TIFF is exercised automatically once the codec ships.

### Removed

- Obsolete `src/analyze_main.cpp` and `src/visual_test.cpp` (hardcoded paths)
- `RenderCommand.h` / `RenderTypes.h` (consolidated into `RenderEngine.h`)
- **M3 cleanup:** dead `CompareWorkspace::paintEvent` off-screen `canvas` composite
  (base/diff/selection/histogram passes drawn into a `QPixmap` that was never blitted —
  `RawImageView` already paints itself). Also removed the now-unused `m_stats` member,
  `drawCellHistogram` declaration, and `RenderEngine`/`QPainter` includes from the compare
  layer. No behavior change; compare cells still receive the synchronized transform.

## [0.1.0] - 2026-07-12

### Added

- Initial architecture: 3-panel UI (DirectoryTree, ThumbnailPanel, AnalysisPanel)
- Compare Engine: multi-image sync zoom/pan, blink, difference maps
- Analysis Engine: histogram, PSNR, SSIM, noise, ROI statistics
- Image export: PNG/JPEG/BMP/WebP with quality control
- CacheManager: 5-level cache hierarchy (Metadata → Thumbnail → Preview → Viewer → Disk)
- TaskScheduler: 5 independent priority queues
- Analyzer plugin interface + 7 built-in analyzers
- Core test suite (compare, layout, sync, diff) and unit tests
