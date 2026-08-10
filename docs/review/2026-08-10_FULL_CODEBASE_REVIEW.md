# MViewer 全量 Codebase Review Report

- 日期: 2026-08-10
- 范围: `src/`(core / domain / application / ui / gpu)、CMake、build.ps1、CI/CD(ci / nightly / release)、静态分析配置、benchmark、测试体系、文档 / ADR / README / 打包脚本
- 方式: 通读源码与调用链(含 CodeGraph), 核对文档声明与实际代码, 未修改任何代码

## 总体结论

这是一个**工程纪律明显高于平均水平**的代码库——分层基本干净、异步生命周期处理是目前见过最严谨的之一、文档与代码基本一致。但它**尚未达到"可长期维护且能继续扩展"的最终状态**: 存在 1 个 P0 级可复现性问题(干净检出下测试/CI 数据依赖断裂)、若干 P1 级"UI 线程大图阻塞"与"渲染热路径 O(全图)"问题, 以及性能门禁形同虚设的问题。这些问题全部可以在**现有冻结架构内**修复, 不需要任何大规模重构。

## 1. 项目模型(实际代码 vs 文档模型)

### Application Layer

```
main.cpp
  -> QApplication + 日志 + 崩溃处理 + Settings 迁移 + --selftest
  -> startupPlugins()(PluginManager::loadDirectory, 紧邻 exe 的 plugins/)
  -> MainWindow(构造 -> setupUi() -> setupCommands() -> restoreLastSession())
  -> app.exec()
```

职责划分与文档一致:
- **domain/**: 纯业务对象(Image / ImageFrame / Workspace / CompareSession / Selection / Histogram), 零 Qt 依赖(已用 grep 验证, 无任何 `#include <Q`)。
- **core/**: 基础设施(Repository / CacheManager / TaskScheduler / Decoder / RenderEngine / Analyze / Compare / Export / Search / Plugin / Update)。头文件基本 Qt-free(唯一例外是明确标注"仅供 .cpp 内部"的 `core/image/QtConvert.h`)。
- **application/**: 用例层(OpenDirectory / CompareImages / Rename / Delete / Startup)。当前只有 5 个小文件, 用例层非常薄。
- **ui/**: Qt Widgets 边界, 按 ADR-014 拆成责任 TU。

**实际与文档的偏差(关键)**:
1. **CompareEngine 在 UI 线程同步解码**(详见 §6 P1-02), 违反"decode off UI thread"这一核心设计承诺。
2. **"GPU 渲染"实际是休眠状态**: `GpuTileUploader` 需要 `MVIEWER_GPU=1` 且只做纹理上传/记账, 产品渲染路径全部走 CPU QPainter(详见 §7)。
3. **优先级语义与直觉不同**: `Priority::UI` 的任务并不在 UI 线程运行, 而是进入独立 `IOPool`(文档已注明这是契约, 但"UI"这个命名对后续维护者是陷阱)。

### Image Pipeline(实际数据流)

```
用户操作(缩略图点击 / 键盘导航)
  -> SelectionModel::setCurrentImage(SSOT)
  -> MainWindow::onCurrentImageChanged
      |-- PreviewPanel::setImage   -> ImageRepository::loadAsync -> DecodePool(异步 OK)
      |-- ImageViewer::setImage    -> ImageRepository::loadAsync -> DecodePool(异步 OK)
      |     -> 完成回调(UI 线程): toQImage(全图) -> QPixmap(UI 线程全图转换)
      |     -> imageReady -> AnalysisPanel::setFrame(再转一次 + 全图统计)
      `-- 缩略图: ThumbnailPanel -> ThumbnailPipeline -> ThumbnailPool(异步 OK)
Compare 打开:
  -> MainWindow::openCompare -> CompareWorkspace::setImages x2
      -> CompareEngine::setImages -> ImageRepository::load(同步, UI 线程全图解码 xN)
      -> rebuildCells -> toQImage(全图) x每格(UI 线程)
渲染:
  -> ImageViewer::paintEvent -> TileCache::request -> RenderEngine::scaleRegion
      -> SoftwareRenderer::scaleRegion -> toQImage(全图)(每缺块一次 O(全图) 转换) -> 缩放 -> drawImage
```

结论: 文档描述的"Widget 永不整图光栅化"只对了一半——paint 路径确实是分块的, 但块解码与若干回调路径仍然做全图转换; 而 Compare 路径则完全不符合"异步解码"承诺。

## 2. 架构评审

### 合理并建议保持的设计
- **分层依赖方向**: UI -> Application -> Core -> Domain 基本成立; Domain 纯 std; Core 头文件 Qt-free 的纪律被 `architecture_gate.ps1`(R1-R4)和 `audit_qt_boundary.ps1` 自动监督。
- **单一数据流入口**: `SelectionModel` 作为"当前图片/选中"的 SSOT, MainWindow 的 `onCurrentImageChanged` 是唯一扇出点, 目录切换/缩略图/预览/查看器/状态栏没有第二套状态。
- **冻结组件(CacheManager / Scheduler / DecoderRegistry / Plugin / Build / CI)**: M23-M27 的修复确实是在既有架构内做的(复现->修->回归), 没有为了"架构漂亮"而重写。保持冻结。
- **ADR-014 TU 拆分**: `mainwindow.cpp` 622 行、`compareworkspace.cpp` 677 行、`thumbnailpanel.cpp` 677 行, 全部低于硬上限; 拆出的责任 TU 有明确的 `*_p.h` 映射。
- **TaskScheduler 的 M26/M27 修复**(cancelTree 走依赖者、deferred 计数、等待态计数、backpressure 回调在锁外、drain 不在持锁时 waitForDone、deadline 终态只走一次)——每一处都有明确 bug 复现与回归测试, 质量很高。

### 问题(详见 §14 完整清单)
- **God Window**: `MainWindow` 横跨 7 个 TU 约 3900 行, 承担了 UI 构建、导航、会话恢复、崩溃恢复、更新检查、幻灯放映、历史栈、收藏、工作区/项目序列化等十余种职责。ADR-014 Phase 2(抽真实 Controller)被正确推迟, 但现在不改的后果是: 每次新功能都必须读 `mainwindow_p.h` 的近 300 行成员清单; 未来何时会爆: 任何需要"MainWindow 之外复用会话/历史/恢复逻辑"的新功能(例如多窗口、托盘、CLI 批处理)都会被迫复制状态; 建议: v1.0 之后、在真实 Controller 需求出现时再做, 不要现在动。
- **`mainwindow_ui.cpp` 1337 行**: 超过通用 800 行 advisory 上限(在 2500 的拆分子 TU 上限内), 可接受但要注意继续增长。
- **架构门禁是 advisory**: `architecture_gate.ps1` 永不 fail(注释明确 "never fails the build"), 且豁免了 `*_p.h` 与 thumbnail 基础设施。这意味着架构腐化只能靠健康分"看得见", 不会被 PR 阻塞。属于已知取舍。

## 3. C++ 质量评审

整体 C++20 使用是克制的: `std::span`(RenderCommand::drawHistogram)、`std::optional`、`std::string_view`(UpdateChecker)、enum class、shared_ptr<vector> 作为像素载体(规避 MSVC ASan 的 `shared_ptr<T[]>` 问题, 并在头文件里写了完整理由)——都是"为了解决问题而用", 没有为现代化而现代化。

**主要问题**:

| # | 位置 | 问题 | 级别 |
|---|---|---|---|
| C-1 | `core/image/ImageFrame.h`(全类) | 注释声称 "Thread-safe for read-only access", 但 `computeHistogram()` 是 const 却修改 `m_histogram/m_histogramComputed`; `setThumbnail/setPixels/setRaw16/setMetadata/tags/renderCache` 全是无锁普通成员写。当前数据流(工作线程算完再投递 UI)恰好没撞车, 但该注释是虚假安全承诺 | P2 |
| C-2 | `CacheManager::prefetch(std::function)` | `if (!nextKeys) return;` 检查了 `std::function::operator bool`, 而 `ImageRepository.h` 注释明确警告 MSVC 19.51 该操作符对非空函数恒返回 false -> 该重载在当前工具链上可能永远静默 no-op | P3 |
| C-3 | `core/image/ImageCache.cpp` put/get | `ImageData` 是 shared_ptr 拷贝(便宜), 无深拷贝问题; 但 `DiskCache::get/put` 的 `QByteArray` 构造是整图深拷贝 | P2 |
| C-4 | `mainwindow.cpp:537` | `m_analysisPanel->setImage(QImage(path), path)` —— `QImage(path)` 在 UI 线程同步全图解码(Compare->Analyze 工作流) | P2 |
| C-5 | `core/image/ImageRepository.cpp` loadDirectory | 用 1ms sleep 轮询原子计数做同步等待(防御预算内), 虽不再 busy-spin 死等, 但调用线程(如将来 UI 线程误用)仍会阻塞到预算耗尽 | P2 |

没有发现明显的 dangling lambda capture / use-after-free / iterator invalidation——M26/M27 的回归测试(`test_m27_lifetime.cpp`、`m27_lifecycle_torture`)确实覆盖了这些类别, 且实现里到处是 `QPointer` + alive token + 请求代数三重守卫。

## 4. 线程 / 并发评审

**做得好的**:
- 所有异步 UI 回调用 `QMetaObject::invokeMethod(qApp, ...)` 投递 + `QPointer` 守卫 + 请求代数(`m_requestGen` / `m_dirGen` / `m_reindexGen`), 且 lambda 内再次检查。
- `DiskCache` 的 per-thread SQLite 连接 + 创建互斥 + 全访问串行化, 并在头文件里写清了当初 heap corruption 的根因。
- `CompareEngine::requestDiff` 在 AnalysisPool 上跑 diff, 通过 `AsyncState::owner` 原子 + EventBus snapshot 语义防悬垂, CompareWorkspace 析构时退订。
- `ThumbnailPipeline` 的 generation + pending/handles 记账 + 拒绝重试 + 完成即删句柄, 是教科书级的有界工作集管理。

**问题**:

| # | 位置 | 问题 | 级别 |
|---|---|---|---|
| T-1 | `TaskScheduler.cpp` 构造 | 5 个独立 QThreadPool 各自 `idealThreadCount` 线程: 16 核 -> 理论 ~58 线程; 2 核 VM -> 8 线程。无全局并发上限 | P2 |
| T-2 | `TaskScheduler.h` `Priority::UI` -> `IOPool` | "UI 优先级"只是独立池 + FIFO 队列, 不是抢占/严格优先级; 跨池任务完全并发 | P2 |
| T-3 | `ImageViewer::preloadNeighbors`(imageviewer.cpp:263-280) | 每次打开图片就 `loadAsync` 全分辨率邻居(24MP ~= 72MB/张), 无取消、无代数; 快速连按会堆积注定被丢弃的全图解码 + DiskCache 写入 | P1 |
| T-4 | `CompareEngine::setImages` / `CompareWorkspace::setImages` | 同步全图解码跑在 UI 线程 | P1 |
| T-5 | 退出路径 | `TaskScheduler::instance()` 有意泄漏以避免 QThreadPool 析构晚于 QCoreApplication 死锁; `MainWindow::closeEvent` 不 drain | P3 |
| T-6 | `RawDecoder::extractPreview` | `f.readAll()` 整读 RAW 文件 + 再复制进 std::vector(~2x 文件大小瞬时内存) | P2 |

结论: 取消机制对"排队任务"有效(未开始的跳过), 对"运行中的解码"无效(QImage 不可打断)——这是所有 Qt 解码器的共性, 可以接受, 但意味着快速浏览时 UI 等待最坏情况 = 最长一次解码。

## 5. 图像管线 / 性能评审

### Decode
- JPEG/PNG/BMP/TIFF/WebP/GIF/RAW 走 Qt 插件 + RawDecoder(嵌入 JPEG 预览提取); 解码在 DecodePool 线程。OK
- **问题**: `SoftwareRenderer::scaleRegion`(RenderEngine.cpp:298-305)先 `toQImage(全图)`(逐行 memcpy, 24MP ~= 72MB 拷贝) -> `full.copy(region)` -> `scaleQ`。每个缺失 tile 都付出 O(全图) 转换成本, 却在 UI 线程的 paintEvent 里同步执行。8K 首显、100MP 缩放的"分块"承诺被这个实现架空。**P1**。
- RAW 只有嵌入预览, 无真正全分辨率 RAW 解码——影响 v1.0 定位。

### Cache
- `ImageCache` 四层独立 LRU + 每池独立 mutex: 设计正确。`ImageData` shared_ptr 拷贝便宜, 内存上限 16/64/256/512MB 生效。OK
- `CacheManager` 元数据 50k 条、Raw16 2000 条上限(按条数不按字节——16-bit 100MP 图每条 ~600MB, 2 条即超 1GB, 且 Raw16 与像素池双份存在)。**P2**。
- `DiskCache`: 存储全分辨率未压缩 RGB24 BLOB。每次 put 构造整图 QByteArray、get 再整图 memcpy; 全局 recursive_mutex 串行化所有 SQL; put 超限时循环 SELECT COUNT/DELETE。默认 m_maxBytes=2GB 与 CacheConfig.diskCacheSize=1GB 不一致。**P1/P2**。
- 缩略图磁盘缓存(ThumbnailCache): 键含 path+mtime+size+size+schema(M25 修得对), 但无容量上限、无清理。**P2**。
- 缓存命中率统计存在且进状态栏, 但没有 hit-rate 指标写入任何持久化报告。**P3**。

### Scheduler 回答"用户点击下一张是否优先"
是(同池内 FIFO + 独立池), 但不是抢占式的; 且全图邻居预取与当前图解码同池同优先级(T-3), 快速浏览时预取会实质占用解码能力。**P1 修复点**。

## 6. 渲染 / GPU 评审

- **实际渲染后端 = 软件 QPainter**。`RenderEngine` 默认 `SoftwareRenderer`; `GpuTileUploader` 需 `MVIEWER_GPU=1` + 真实 GL 上下文, 且只负责纹理上传与记账, 没有任何产品代码用 GPU 纹理做最终合成。**P1(性能/架构债务, 不是 bug)**: v1.0 之前要么把 CPU 路径做到预算内(优先), 要么明确"GPU 是后续里程碑"并更新文档。
- **compare 渲染**: `RawImageView` 持整图 QImage, paintEvent 每次 `p.drawImage` 由 QPainter 全图缩放; `rebuildCells` 对每格做整图 `toQImage`(compareworkspace_render.cpp:68)。2-8 张 24MP 图同时驻留 QImage(~72MBx8)+ 模式切换都触发整图重绘。**这是 Compare 流畅度的最大瓶颈**。
- **HiDPI 路径**: `ImageViewer::paintEvent` 按 dpr 请求 tile, 逻辑正确。
- **TileCache** 抽象(TileGrid/Viewport/TileKey/LRU)干净、有单测(`tilecache_tests`), 是保留项。

## 7. 内存 / 泄漏评审

**结论: 没有发现明确的持续内存泄漏**; 存在 3 个有界性/双份存储问题:

1. **UI 侧全图副本链**: 每次打开查看器 = ImageFrame(72MB) + QPixmap 全图(72MB) + AnalysisPanel QImage RGB32(72MB) + computeStats 拷贝——同像素在内存里最多 4 份; Compare 打开 8 图更是 8x(frame+pixmap/QImage)。不是泄漏, 但峰值显著。
2. **Raw16 双份 + 按条数上限**(§5)。
3. **ThumbnailCache 无磁盘上限**(§5)。
4. **生命周期**: `MainWindow` 析构 `delete m_imageViewer`(修复了历史泄漏, M22 Issue-002 有回归); `CompareWorkspace` 退订 EventBus; `ThumbnailPanel` alive token + pool clear; `MetadataIndexer` bounded FIFO(100k)。`m27_lifecycle_torture` 覆盖 close/reopen。OK

失败路径上(解码中关窗)均有 QPointer 守卫; `ImageRepository::loadDirectory` 超时路径用 shared_ptr 托管全部工作状态(M27 修掉 stack-use-after-free)。

## 8. 静态分析 / CI 评审

### CI 结构(Tier 1/2/3)设计合理
- PR 门禁: format(clang-format 22.1.8 + markdownlint)、cppcheck(全树、必过)、clang-tidy(仅增量文件、bugprone/performance/analyzer 必过)、MSVC build(日志扫描零警告)、CTest(含 bench_enforce + golden_image)、adr-gate、known-issues-gate。`ci-gate` 聚合。
- Nightly: ASan/UBSan(clang-cl)、clazy、quality(bench+golden)、perfetto、coverage(OpenCppCoverage, advisory)、publish-health(自动提交 dashboard)。
- Release: full bench、golden、Dr.Memory(best-effort)、NSIS 打包。

### 问题(按严重度)

| # | 位置 | 问题 | 级别 |
|---|---|---|---|
| CI-1 | `.gitignore:24` + `ci.yml:263` / `nightly.yml:324` | **`testdata/` 整个目录被 gitignore 且未跟踪**(git ls-files testdata 为空, git ls-tree HEAD 无 testdata), 但两个 workflow 都执行 `python testdata/generate_fixtures.py`——该脚本在仓库和磁盘上都不存在(已全盘搜索)。干净 CI checkout 会在该步骤直接失败; 即使跳过, decoder/repository/metadata/m3pipeline/m3m4m5/core 等测试会因 testdata/golden 缺失而失败。**"干净检出可复现"契约断裂** | **P0** |
| CI-2 | `build.ps1:191-192` | Test 分支 ctest 失败只 Write-Warning, 不 exit 非零(ci.yml 注释也承认)。本地 `.\build.ps1 Test` 的退出码不可信 | **P1** |
| CI-3 | `src/CMakeLists.txt` bench 注册 | `bench_smoke` / `bench_enforce` 未设 RUN_SERIAL, 而 ci test 用 `ctest -j4` -> 性能门禁与其它测试并行跑, 测量带噪 | P1 |
| CI-4 | `ci.yml` clang-tidy | 只检查 PR 变更文件(增量), 遗留代码永不复查; `-I src/ui` 等 include 路径不存在 | P3 |
| CI-5 | `nightly.yml` | ASan/UBSan/clazy/coverage 全部非阻塞, ASan 明确排除多个大套件; Dr.Memory best-effort | P2 |
| CI-6 | 版本/环境一致性 | 本地 Qt 6.11.1 vs CI Qt 6.8.0; 本地 clang-format 版本未 pin; /W4 /WX 未进 CMake, 零警告靠 CI 日志 grep | P2 |

**"看起来配置了、实际没执行"**: 最典型就是 CI-1(testdata 脚本不存在); 其次是 coverage 的 core>=85% 目标是 advisory; `pre-commit` 配置在 CI 中并未作为入口运行。

## 9. 测试体系评审

**体量**: 84 个 add_test(实际注册 ~78), 覆盖 unit/integration/acceptance/UI workflow/golden/perf/soak。质量分层清晰: core 单测(M6/M26/M27 系列)、真实 MainWindow 的 `workflow_ux_tests`(1379 行, 真实键盘/鼠标事件)、M24 四个 acceptance 套件、`m27_lifecycle_torture`。**这是当前仓库最强的资产之一。**

**缺口**(按重要性):
1. **没有"用户可感知延迟"的端到端测量测试**: B7/B8 只测 `repo.load()`(见 §10), 没有任何测试断言"打开 Compare 的 UI 卡顿 < 预算"或"查看器首帧 paint 延迟 < 预算"。
2. **Compare 的异步化没有回归测试**: `test_compare_acceptance` 等全部用同步 setImages(因为实现就是同步的), 一旦改异步, 现有测试会先挂——正确方向是先写"UI 不被阻塞"的测试再改实现。
3. **GPU 路径零测试**: `test_gputile` 只测记账, 无任何真实 GL 合成测试; CI 无 GPU。
4. **RAW**: 只测预览提取, 无坏 RAW/超长文件/内存峰值测试。
5. **Stress**(S1-S9/T1-T4)是手动工具 + 文档报告, 不在门禁内。
6. **UI 测试依赖 `pump(ms)` 计时**: 在满载 runner 上存在 flaky 风险。

## 10. Benchmark 评审

**结论: harness 结构好, 测量内容与门禁强度不合格。**

- **B7/B8(图片切换延迟)**: 测的是 `ImageRepository::load`(缓存命中 ~0.2ms), 不是用户路径(解码->QPixmap->paint->analysis 转换)。预算 `image_switch_ms=16ms` vs 基线 0.245ms = 65 倍余量; `switch_warm_p50=50ms` vs 0.19ms = 250 倍余量。真正的大图首显/Compare 打开延迟完全没被测。
- **B10(100MP)**: 用程序化填充代替真实 scaleRegion, 因此测不到"每 tile 全图转换"的 O(N) 问题, 基线 392ms 是假乐观。
- **绝对预算极宽松**(文档自认): `decode_p50_jpeg=250ms` vs 基线 16.4ms; `thumbnails_per_sec=7` vs 62; `cache_hit_ratio=0.10` vs 0.157(命中率掉 36% 仍通过)。`B6 peak_cache_bytes` 预算 512MB vs 基线 536,347,000B——基线距硬顶不到 0.1%。
- **±10% 回归轴只在 nightly 跑**, 且文档承认 noisy、部分指标被"填充到加宽值"以压抖动——PR 门禁实际上只查宽松绝对预算。
- **统计**: 有 p50/p95/p99、有 warm-up、有 cold/warm 区分、语料确定性生成——方法论其余部分合格。

**最值得新增的 benchmark**: 真实 UI 路径的 Compare 打开延迟(含解码+paint); 查看器首帧(setImage->paint 完成)延迟; 8K/100MP 缩放的 scaleRegion 单块成本; AnalysisPanel setFrame 的 UI 线程阻塞时间; 快速浏览 100 张时 DecodePool 队列深度/丢弃解码数。

## 11. 架构债务(现在不改的后果 / 何时爆 / 何时处理)

| 债务 | 现在不改的后果 | 未来何时爆 | 建议处理时机 |
|---|---|---|---|
| Compare/分析路径 UI 线程大图工作 | 核心工作流(对比)在大图上卡顿 | 第一个真实 ISP 用户用 8K/24MP 对比时 | 现在(P0/P1) |
| scaleRegion O(全图) | 8K/100MP 缩放/平移每帧卡顿 | 用户放大 8K 图平移时 | 现在(P1) |
| DiskCache 存全分辨率未压缩图 | I/O 放大、SQLite 串行瓶颈 | 千图目录 + 大图混合场景 | v1.0 前(P1/P2) |
| GPU 路径休眠 | 性能上限被 CPU 缩放锁死, 且文档误导 | 用户期望 50MP 流畅缩放时 | v1.0 后, 先更新文档 |
| MainWindow God-window | 新功能必须读懂 300 行成员清单 | 出现"多窗口/托盘/无头批处理"需求时 | v1.0 后, 按需提取 |
| ThumbnailCache 无上限 | 磁盘缓慢增长 | 数月高频使用后用户 C 盘被占 | v1.0 前(P2) |
| Raw16 按条数不按字节 | 16-bit 大图双份内存 | 用户连续打开多张 16-bit 全景时 | v1.0 前(P2) |

## 12. 合理设计——明确建议保持

- **SelectionModel SSOT + onCurrentImageChanged 单一扇出**: 保留, 不要为每个面板加自己的状态。
- **ThumbnailPipeline 的 generation/cancel/bounded-working-set 模型**: 保留。
- **TaskScheduler 的 M26/M27 记账模型**(pending/waiting/active 精确转移、cancelTree 走 dependents、backpressure 在锁外): 保留。
- **QPointer + alive + 代数三守卫的异步投递纪律**: 作为全仓规范固化下来。
- **ImageData=shared_ptr<vector> 的廉价拷贝语义 + 每级独立 mutex 的 LRU**: 保留。
- **domain 零依赖、core 头 Qt-free、ADR-014 TU 拆分、CI 三层(PR/nightly/release)**: 保留。
- **工作流测试(真实 MainWindow + 真实键鼠事件)**: 保留并继续扩展。
- **插件 ABI 冻结 + 失败隔离(catch(...) + 指标)**: 保留。

## 13. 问题清单(结构化)

### P0 - 必须立即修复

#### P0-01 | 干净检出的测试/CI 数据依赖断裂
- Severity: P0
- Category: CI / Testing
- Location: `.gitignore:24`(`testdata/`); `.github/workflows/ci.yml:263`; `.github/workflows/nightly.yml:324`; `src/core/test_decoder.cpp:54-61`(及 repository/metadata/m3pipeline/m3m4m5/core 等)
- Problem: `testdata/` 全目录被忽略且未跟踪; `testdata/generate_fixtures.py` 在仓库与磁盘上均不存在; CI 却执行 `python testdata/generate_fixtures.py`。
- Why it matters: 任何干净 clone(本地或 GitHub runner)都无法复现测试; 要么 CI test job 直接失败, 要么靠残留工作区"偶然通过"——整个质量门禁的可信度取决于一台机器的现场状态。
- Evidence: `git ls-files testdata` -> 空; `git ls-tree -r HEAD --name-only | Select-String testdata` -> 仅 `tests/testdata_regression.cpp`; `Get-ChildItem -Recurse -Filter generate_fixtures.py` -> 无结果; `git check-ignore -v testdata/generate_fixtures.py` -> `.gitignore:24:testdata/`。
- Recommended fix: 把生成器脚本移出 `testdata/`(如 `scripts/generate_fixtures.py`)并提交, `testdata/` 只忽略产物; 或 `git add -f testdata/generate_fixtures.py` 并在 .gitignore 加 `!testdata/generate_fixtures.py`; CI 保留"先生成再测试"; 本地加"clean-clone 冒烟"。
- Risk: 低(纯工程修复)。
- Estimated effort: S

### P1 - 下一阶段

#### P1-01 | 打开 Compare = UI 线程同步全图解码 x2 + 每格全图转换
- Severity: P1(对大图接近 P0 体验)
- Category: Performance / UX
- Location: `mainwindow.cpp:511,568-570`; `compareworkspace.cpp:436-443`; `core/compare/CompareEngine.cpp:37-44`; `compareworkspace_render.cpp:68`
- Problem: openCompare 先同步 setImages(在 show 之前, UI 线程 ImageRepository::load 全图解码 N 张), show() 后再 QTimer::singleShot(0) 又跑一次 setImages; rebuildCells 每格再 toQImage 全图。
- Why it matters: 违反自家 pipeline 规范(decode off UI thread)与 beta_checklist"无等待"; 对比是产品核心工作流。
- Evidence: CompareWorkspace::setImages -> m_engine.setImages -> ImageRepository::load(同步); mainwindow.cpp 两处调用。
- Recommended fix: 删除第一次 setImages, 改为 show 后单次异步加载(DecodePool + QPointer 投递 + 代数); CompareEngine 增加 setImagesAsync, 每帧就绪即增量建格, 全部就绪后 fitAll; 用 ImageFrame 像素直接驱动 RawImageView 渲染。先写"openCompare 在 2 核 VM 上 UI gap < 预算"的回归测试。
- Risk: 中(Compare 状态机涉及 session/ROI/调整), 建议测试先行。
- Estimated effort: M

#### P1-02 | 查看器/分析面板在 UI 线程做全图转换与统计
- Severity: P1
- Category: Performance / UX
- Location: `imageviewer.cpp:188`; `mainwindow_ui.cpp:953`; `analysispanel.cpp:59-70,251-262`
- Problem: 每次查看器异步解码完成, UI 线程执行 toQImage(全图)->QPixmap; 随后 imageReady 无条件触发 AnalysisPanel::setFrame -> 再 toQImage+convertToFormat(RGB32)+fromQImage+computeStats 全图遍历(面板隐藏也执行)。24MP 图 = UI 线程多次 72MB 拷贝 + 全图扫描。
- Why it matters: 首显/切换延迟与"8K 首显 < 500ms"预算直接冲突。
- Evidence: 上面 3 处调用链; M26 只修了 PreviewPanel 的统计(m26_stats), AnalysisPanel 未修。
- Recommended fix: setFrame 改为直接持有 ImageFrame 引用, 统计用 worker 端 ImageStats(M26 已有); 仅在面板可见时做 UI 渲染; imageReady 连接改为可见性门控。
- Risk: 低-中。
- Estimated effort: M

#### P1-03 | 渲染热路径: 每缺失 tile 都 O(全图) 转换
- Severity: P1
- Category: Performance / Rendering
- Location: `core/render/RenderEngine.cpp:298-305`(`SoftwareRenderer::scaleRegion`); `core/image/QtConvert.cpp`(`toQImage` 逐行全拷贝); `imageviewer_paint.cpp`(paint 内同步解码缺失 tile)
- Problem: scaleRegion(src, region, ...) 先把整幅源图转成 QImage 再 copy 区域再缩放; tile 缺失时该工作在 UI 线程 paintEvent 中同步执行。8K/24MP/100MP 的"分块渲染"实际是"每块付一次全图代价"。
- Why it matters: 缩放/平移时新块出现即卡顿; TileCache 的 LOD 设计收益被实现抵消; B10 基准因为用合成填充而测不到此问题。
- Evidence: scaleRegion 实现; imageviewer_paint.cpp decode lambda 调 eng.scaleRegion(...)。
- Recommended fix: 直接对 ImageData 区域做缩放(纯 std 双线性/最近邻, 或 QImage 只拷贝区域而非全图); 或把 tile 解码移到 worker。先 1 后 2。
- Risk: 中(渲染正确性需 golden/vision 回归覆盖)。
- Estimated effort: M

#### P1-04 | 性能门禁形同虚设(测量内容 + 预算 + 并行度)
- Severity: P1
- Category: CI / Benchmark / Performance
- Location: `benchmark/performance_budget.json`; `benchmark/perf_baseline.json`; `src/benchmark/scenarios.cpp:567-650,730-790`; `src/CMakeLists.txt`(bench 未 RUN_SERIAL); `ci.yml` test job(ctest -j4)
- Problem: B7/B8 只测 repo.load() 缓存命中, 不测 UI 路径; 绝对预算宽到 65-250 倍; ±10% 回归轴只在 nightly; bench 与其它测试并行跑; B6 基线距预算顶 <0.1%。
- Why it matters: "性能退化会被 CI 拦下"是当前最重要的安全网之一, 但它实际上拦不住 2x 级退化。
- Evidence: performance_budget.json 注释自认"generous, cross-machine-stable"; 基线 vs 预算数字如上。
- Recommended fix: 新增真实路径场景(Compare 打开延迟、查看器首帧、scaleRegion 单块、AnalysisPanel setFrame); 这些场景先设"宽松但能抓 2x 退化"的预算, 回归轴(±10%)纳入 PR 门禁并设 RUN_SERIAL; 修复 B6 基线口径。
- Risk: 低-中(需要先稳定测量)。
- Estimated effort: M

#### P1-05 | `build.ps1 Test` 吞掉测试失败
- Severity: P1
- Category: CI / Process
- Location: `build.ps1:191-192`
- Problem: ctest 失败仅警告、退出码 0; AGENTS.md 规定"本地先 .\build.ps1 Test 验证"——该命令可能绿着返回而测试实际红。
- Why it matters: 本地验证策略失效, CI 注释也承认绕开了它。
- Evidence: ci.yml test job 注释 "build.ps1 Test swallows CTest failures... we must NOT rely on its exit code"。
- Recommended fix: Test 分支末尾 if ($LASTEXITCODE -ne 0) { Write-Warning ...; exit 1 }。
- Risk: 低。
- Estimated effort: S

### P2 - 后续优化

| # | Severity | Category | Location | Problem / Fix | Effort |
|---|---|---|---|---|---|
| P2-01 | P2 | Performance | core/image/DiskCache.cpp(get/put) + ImageRepository.cpp load | 全分辨率未压缩 BLOB + QByteArray 深拷贝 + 全局串行 + put 时 O(n) 清理; 改为缩略图/预览优先、写队列、容量口径统一(1GB) | L |
| P2-02 | P2 | Threading | TaskScheduler.cpp 构造 | 5 池 x idealThreadCount 无全局上限 -> 超订; 加全局活跃任务上限或共享线程池调度 | M |
| P2-03 | P2 | Threading | imageviewer.cpp:263-280 | 邻居全图预取无取消/代数; 改为低分辨率预取 + 取消旧句柄 | M |
| P2-04 | P2 | Memory | core/cache/CacheManager.h(kRaw16MaxEntries=2000) | 按条数不按字节, 16-bit 大图双份; 加字节上限 | S |
| P2-05 | P2 | Memory | thumbnailcache.cpp | 磁盘缩略图缓存无上限/无清理; 加 maxBytes + LRU 清理 + 启动 prune | S-M |
| P2-06 | P2 | Performance | analysispanel.cpp reanalyze | 分析器在 UI 线程同步跑; 迁 AnalysisPool + 结果回投 | M |
| P2-07 | P2 | Performance | mainwindow.cpp:537 | QImage(path) 同步解码; 改走 ImageRepository::loadAsync | S |
| P2-08 | P2 | Rendering | widgets/rawimageview.cpp + compareworkspace_render.cpp | Compare 每格整图 QImage + 每帧 QPainter 全图缩放; 引入 tile/低分辨率 LOD | L |
| P2-09 | P2 | CI | nightly.yml | ASan/UBSan/clazy/coverage 非阻塞且 ASan 排除多套件; 逐步收紧 | M |
| P2-10 | P2 | CI | 环境 | Qt 6.11.1(本地)vs 6.8.0(CI)、clang-format 版本、/W4 /WX 未进 CMake | S |
| P2-11 | P2 | Architecture | ImageFrame.h | 移除/更正"线程安全"注释; 明确 frame 所有权转移契约, 或为共享场景加同步 | S |
| P2-12 | P2 | Threading | RawDecoder.cpp | 整读 RAW + 复制; 改流式扫描嵌入 JPEG | M |

### P3 - Nice to Have

- P3-01: CacheManager::prefetch(std::function) 的 operator bool 检查与 MSVC 19.51 已知缺陷冲突, 去掉 bool 检查。
- P3-02: DiskCache key 只用 mtime(秒)+ size, 同秒同大小修改会脏命中; 加纳秒 mtime 或内容哈希。
- P3-03: -I src/ui 等失效 include 路径清理; cppcheck suppressions 定期审计。
- P3-04: clang-tidy 增量模式增加"遗留文件按季度全量扫描"。
- P3-05: B9/soak 结果纳入 dashboard(目前报告-only)。
- P3-06: mainwindow_session.cpp 787 行 / mainwindow_ui.cpp 1337 行接近拆分子 TU 上限(2500), 继续增长前先拆分。

## 14. Roadmap(按优先级)

### P0 - 立即修复(1 周内)
1. P0-01: 修复 testdata 生成器跟踪 + 干净检出 build.ps1 Test 全绿(含 CI test job)。
2. P1-05: build.ps1 Test 失败退出码(顺手, 5 分钟)。

### P1 - 下一阶段(2-3 周)
3. P1-01: Compare 异步化(先写"openCompare UI gap < 预算"回归测试, 再改实现)。
4. P1-02: AnalysisPanel/ImageViewer UI 线程全图转换移除。
5. P1-03: scaleRegion 区域直缩放(消除每 tile O(全图))。
6. P1-04: 真实路径 benchmark + RUN_SERIAL + 回归轴进 PR。

### P2 - 后续优化(1 个月窗口内)
7. P2-01 DiskCache 重构(缩略图/预览优先、容量对齐)。
8. P2-02/03 调度全局上限 + 邻居预取取消。
9. P2-04/05 Raw16 与 ThumbnailCache 字节上限。
10. P2-06/07/08 分析器异步化、QImage(path) 消除、Compare 渲染 LOD。
11. P2-09/10 CI 收紧与版本一致性。

### P3 - Nice to Have(v1.0 之后)
12. P3 清单全部 + MainWindow Controller 提取(按需求触发, 不主动做)。

## 15. 必须回答的问题

### Q1: 当前架构是否足够稳定, 可以停止大规模架构重构?
是, 可以停止。分层、SSOT、冻结组件、TU 拆分都是对的; M23-M27 证明该架构能承载高难度并发正确性修复。不需要任何大规模重构。需要做的是在架构内修 3 个热点(Compare 异步化、UI 线程全图转换、scaleRegion O(全图))和 1 个工程断点(testdata/CI 复现性)。

### Q2: 当前最大的 5 个技术风险
1. CI/干净检出不可复现(P0-01): 质量门禁可能建立在残留现场上。
2. Compare 打开 = UI 线程同步解码: 核心工作流在大图上不可用。
3. 渲染热路径 O(全图) + GPU 休眠: 8K/100MP 性能承诺无法兑现。
4. DiskCache 全分辨率未压缩存储: I/O 放大 + SQLite 串行化瓶颈。
5. 性能门禁测量错位 + 预算过宽: 退化检测失效。

### Q3: 当前最大的 5 个性能风险
1. Compare 打开/切模式的 UI 冻结(多张 24MP 同步解码 + 每格全图 QImage)。
2. 查看器首显的 UI 线程全图转换链(QPixmap + AnalysisPanel 双份 + 统计)。
3. scaleRegion 每缺失 tile 的 O(全图) 转换(缩放/平移卡顿)。
4. DiskCache 的 BLOB 深拷贝与全局串行(并行解码互相阻塞)。
5. 全图邻居预取无取消 + 5 池超订(快速浏览资源堆积)。

### Q4: 当前最大的 5 个用户体验问题
1. 大图打开 Compare 时界面冻结数秒(无等待原则破坏)。
2. 大图首显/切换存在可感知停顿(首帧路径含全图转换)。
3. 8 图 Compare 内存峰值高(8x 全图副本), 低配机可能卡死或崩溃。
4. RAW 只能看嵌入预览——ISP 用户放大即糊。
5. 长期使用磁盘缓存(缩略图)只增不减, 无任何用户可见提示。

### Q5: 当前测试体系最大的缺口
用户可感知延迟的端到端测试缺失: 没有测试断言"打开 Compare/查看器首帧/模式切换"在预算内完成, 且现有 B7/B8 测的不是 UI 路径。其次是 GPU 路径零覆盖、压力测试(S/T 系列)不在门禁内、RAW 内存峰值无测试。

### Q6: CI / Quality Gate 是否已达到可长期维护的水平?
接近, 但未达到。结构(三层 CI、adr/known-issues/version/golden 门禁、自动 dashboard)是长期可维护的; 但 P0-01(testdata 脚本不存在)让 test 门禁在干净环境不可执行, bench 门禁因测量错位而无效, 复杂度/覆盖率/架构门禁是 advisory。修复 P0-01 + 让 bench 测真实路径后, 可以达到。

### Q7: 如果只有 2 周开发时间, 优先做什么?
1. P0-01: testdata/CI 复现性(1 天)。
2. P1-01: Compare 异步化 + 回归测试先行(5-6 天)。
3. P1-02: AnalysisPanel/ImageViewer UI 线程全图转换移除(2-3 天)。
4. P1-03: scaleRegion 区域直缩放(2-3 天)。
5. P1-05: build.ps1 退出码(0.5 天)。
6. 若有余量: 给 Compare 打开与首帧加真实路径基准(1-2 天)。

### Q8: 如果只有 1 个月开发时间, 优先做什么?
2 周清单 + :
7. P1-04: 真实路径 benchmark + RUN_SERIAL + 回归轴进 PR(3 天)。
8. P2-01: DiskCache 预览/缩略图优先改造(4-5 天)。
9. P2-02/03: 调度全局上限 + 预取取消(3 天)。
10. P2-04/05: Raw16/ThumbnailCache 字节上限(2 天)。
11. 目标硬件压力测试 S1-S9/T1-T4 + 人工 UX 签核 + beta_checklist 收口(1 周, 与 7-10 并行)。

### Q9: 哪些事情现在明确"不应该做"?
- 任何大规模重构: 不抽新架构层、不换 QML、不重写 RenderEngine 为完整 GPU 引擎、不替换 QThreadPool。
- 不触碰冻结区: build.ps1 / CMakePresets.json / .github/workflows/ci.yml 结构 / CacheManager / Scheduler / DecoderRegistry / Plugin / Workspace 基础架构。
- 不加新能力类别: AI、更多格式、更多分析器、更多插件、更多里程碑基础设施。
- 不追求"现代 C++"表演: ranges/coroutine/variant 在无正确性/性能收益处不要引入。
- 现在不抽 MainWindow/CompareWorkspace 的 Controller(ADR-014 Phase 2 条件未到)。
- 不把 advisory 门禁直接改成硬门禁(会立刻阻塞大量既有债务); 先修 P0/P1, 再逐步收紧。

### Q10: 从当前版本到 v1.0 的推荐 Roadmap

| 阶段 | 内容 | 时长 |
|---|---|---|
| M28 Release-Hardening | P0-01、P1-05、P1-01、P1-02、P1-03(先测试后实现) | 2 周 |
| M29 Perf & Pipeline Closure | P1-04 真实路径基准 + 门禁收紧、P2-01 DiskCache、P2-02/03 调度与预取、P2-04/05 内存上限、P2-06/07 | 2 周 |
| M30 Product Sign-off | 目标硬件 S1-S9/T1-T4、人工 UX 签核、beta_checklist 收口、RAW 能力话术、打包/更新/卸载验证、v1.0 发布 | 2 周 |

## 16. 附注

本轮 Review 未修改任何代码。所有结论均基于实际读码与调用链验证, 关键问题都附了 file:line 证据; 对没有发现问题的地方(SSOT、异步生命周期、分层、工作流测试、文档纪律)也明确给出了"建议保持"的结论。
