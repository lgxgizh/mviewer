# MViewer 架构（目标架构）

> 本文档是 **目标架构**，经用户评审确认（2026-07-11）。
> **M2 架构固化已完成（2026-07-13）**——下文第 3–5 节描述的目标架构已全部落地，
> 代码与规范文档同步冻结。后续按里程碑逐步演进到 M3（分析面板）、M4（导出/格式转换）、
> M5（性能剖析）、M6（磁盘缓存深化）、M7（CommandPalette 整合）。
>
> **详细架构规范**见同目录下的 `vision.md`、`roadmap.md` 及 8 份英文规范文档。

## 1. 产品定位（最关键）

MViewer **不是图片浏览器**，而是 **图像算法验证工具**
（对标 FastStone Pro + 算法分析）。核心工作流是 **比较** 与 **分析**，
浏览只是入口。因此比较/分析/渲染引擎必须独立于 `QWidget`，
否则所有逻辑都会堆进 `Viewer.cpp` 变成 5000 行无法维护的代码。

## 2. 技术选型

| 项 | 决策 | 说明 |
| ---- | ------ | ------ |
| 语言 | C++20 | ✅ 已验证 |
| GUI | Qt 6 **Widgets**（非 QML） | ✅ 已验证 |
| 工具链 | **MSVC 2022**（目标已达成） | M2 已完成从 MinGW 到 MSVC 的切换 |
| 构建 | CMake + Ninja | ✅ 已验证 |

## 3. 目标分层架构（✅ M2 已冻结落地）

```
MainWindow
   │
   ├── DirectoryTree
   ├── ThumbnailPanel
   ├── CompareWorkspace        ← 真正的大头，不是目录树
   │      ├── Render Engine
   │      └── Analysis Engine
   ├── AnalysisPanel
   ├── StatusBar
   └── CommandPalette          ← UI 留待 M7（接口已冻结）

Compare Engine   同步缩放 / 同步平移 / 同步框选 / 同步滚动 / 闪烁比较 / 差异图
Analysis Engine  Pixel Inspector / Histogram / RGB Mean / Difference → PSNR / SSIM / Noise
Render Engine    GPU 缩放 / 高质量插值 / Difference Overlay / HeatMap

domain/           纯业务对象（Image/Histogram/Selection/CompareSession，零 Qt 依赖）
   ↑
core/            基础设施层（Image Core / Scheduler / Cache / EventBus / Analyzer）
   ↑
application/     用例层（UseCases）
   ↑
ui/              QWidget 边界
```

**依赖方向严格向内**：UI → Application → Core → Domain。
Domain 层零依赖，Core 层仅在 .cpp 内部使用 Qt。

## 4. 并发模型（✅ M2 已冻结落地）

```
Task Scheduler
      ↓
独立线程池（避免 1000 张图 + 8 张比较 + 预解码 + 缩略图 + Histogram + Difference 全部共用 QThread 导致混乱）：
- Image Decode Pool
- Thumbnail Pool
- Analysis Pool
- IO Pool
```

目标：16 核 CPU 直接跑满，UI 永不卡顿。

## 5. 缓存设计（✅ M2 已冻结落地）

至少三级内存缓存 + 磁盘缓存：

- **Thumbnail Cache**（小图，列表用）
- **Preview Cache**（中图，左栏预览用）
- **Viewer Cache**（全分辨率解码后的 ImageData，LRU）
- **Full Resolution Decode**（按需，流式/分块）

磁盘缓存用 **SQLite** 记录 `mtime / size / hash / thumbnail(可选)`，
千张图二次打开几乎秒开。

## 6. 演进路线（里程碑）

| 里程碑 | 内容 | 状态 |
| -------- | ------ | ------ |
| **M0 / M1** | 可运行浏览器：3 栏、目录树、画廊、预览、双击大图、后台缩略图 + 磁盘缓存 | ✅ 已完成原型 |
| **M2** | 抽离 **Image Core + Task Scheduler + domain/ 层 + 统一事件/命令/缓存体系** | ✅ **已完成，架构冻结** |
| **M3** | **Analysis Engine 面板**：Histogram / RGB Mean / Difference → PSNR / SSIM / Noise 可视化 | 计划 |
| **M4** | 图像导出 / 格式转换 | 计划 |
| **M5** | 性能剖析 + 内存优化（CacheManager 接入真实加载路径、LRU 淘汰策略、基准测试） | 计划 |
| **M6** | SQLite 磁盘缓存深化（接入更多缓存层级） | 计划 |
| **M7** | CommandPalette / AnalysisPanel 整合、文件操作（重命名/回收站） | 计划 |

原则：**M2 架构冻结后，渐进实现面板与功能**，不破坏已冻结的接口。

## 7. M2 已冻结基础设施清单

| 组件 | 文件 | 说明 |
| ------ | ------ | ------ |
| domain/ 层 | `src/domain/{Image,Histogram,Selection,CompareSession}.h` | 纯业务对象，零 Qt 依赖 |
| ImageFrame | `src/core/image/ImageFrame.{h,cpp}` | 通用图像载体（像素+状态+直方图） |
| ImageRepository | `src/core/image/ImageRepository.{h,cpp}` | 图片生命周期抽象（FileSystem + Decoder + Cache） |
| CacheManager | `src/core/cache/CacheManager.{h,cpp}` | 统一缓存调度（内存+磁盘） |
| DiskCache | `src/core/image/DiskCache.{h,cpp}` | SQLite 磁盘缓存 |
| TaskScheduler | `src/core/scheduler/TaskScheduler.{h,cpp}` | 4 独立池（Decode/Thumbnail/Analysis/IO） |
| Analyzer | `src/core/analyzer/Analyzer.{h,cpp}` + `HistogramAnalyzer.{h,cpp}` | 插件化分析接口 |
| EventBus | `src/core/EventBus.{h,cpp}` | 4 域隔离事件总线 |
| CommandRegistry | `src/core/command/CommandRegistry.{h,cpp}` + 5 种子命令 | 命令系统基础设施（CommandPalette UI 留待 M7） |
| ADR 文档 | `docs/adr/001` – `010` | 架构决策记录 |

## 8. 详细架构规范

本目录下的英文规范文档是 M2 的详细权威参考：

- `vision.md` — 产品愿景与范围
- `roadmap.md` — 演进路线与里程碑规划
- `architecture.md`（本文件）— 高层定位与冻结架构总览
- `coding_style.md` — C++20 / Qt 编码规范
- `performance.md` — 性能目标与基准
- `image_pipeline.md` — 图像处理管线规范
- `cache.md` — 缓存体系详细设计
- `ui.md` — UI 层规范
- `plugin.md` — Analyzer 插件体系规范
