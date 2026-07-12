# MViewer 架构（目标架构）

> 本文档是 **目标架构**，经用户评审确认（2026-07-11）。
> 当前可运行版本（MinGW 构建的 3 栏浏览器）为 **Milestone 0/1 原型**，
> 仅用于验证浏览流程，引擎尚未分离。后续按里程碑逐步演进到本架构。

## 1. 产品定位（最关键）

MViewer **不是图片浏览器**，而是 **图像算法验证工具**
（对标 FastStone Pro + 算法分析）。核心工作流是 **比较** 与 **分析**，
浏览只是入口。因此比较/分析/渲染引擎必须独立于 `QWidget`，
否则所有逻辑都会堆进 `Viewer.cpp` 变成 5000 行无法维护的代码。

## 2. 技术选型

| 项 | 决策 | 说明 |
|----|------|------|
| 语言 | C++20 | ✅ 用户赞成 |
| GUI | Qt 6 **Widgets**（非 QML） | ✅ 图像浏览器/分析工具更适合 Widgets：稳定、自绘可控、性能可控 |
| 工具链 | **目标 MSVC / VS2022** | 用户建议。Qt 官方支持最好、Win32/Direct2D/WIC/COM/DXGI/D3D11 调试最舒服、GPU 工作（GPU 缩放/Difference/Histogram）最顺。当前 MinGW 为临时方案，待评估切换成本 |
| 构建 | CMake + Ninja | 保持 |

## 3. 目标分层架构

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
   └── CommandPalette

Compare Engine   同步缩放 / 同步平移 / 同步框选 / 同步滚动 / 闪烁比较 / 差异图
Analysis Engine  Pixel Inspector / Histogram / RGB Mean / Difference → PSNR / SSIM / Noise
Render Engine    GPU 缩放 / 高质量插值 / Difference Overlay / HeatMap

Image Core（整个软件最重要的部分）
   ├── FileSystem
   ├── Decoder
   ├── Image Object
   ├── Cache        ← 三级以上：Thumbnail / Preview / Viewer / Full-res Decode
   ├── Scheduler    ← 统一任务调度
   └── Renderer
```

## 4. 并发模型（替换 ad-hoc QThread）

```
Task Scheduler
      ↓
Thread Pool  →  Worker
```
独立线程池（避免 1000 张图 + 8 张比较 + 预解码 + 缩略图 + Histogram + Difference 全部共用 QThread 导致混乱）：
- **Image Decode Pool**
- **Thumbnail Pool**
- **Analysis Pool**
- **IO Pool**

目标：16 核 CPU 直接跑满，UI 永不卡顿。

## 5. 缓存设计（替换单一缩略图缓存）

至少三级内存缓存 + 磁盘缓存：
- **Thumbnail Cache**（小图，列表用）
- **Preview Cache**（中图，左栏预览用）
- **Viewer Cache**（全分辨率解码后的 QImage/纹理，LRU）
- **Full Resolution Decode**（按需，流式/分块）

磁盘缓存用 **SQLite** 记录 `mtime / size / hash / thumbnail(可选)`，
千张图二次打开几乎秒开。

## 6. 演进路线（里程碑）

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| **M0 / M1** | 可运行浏览器：3 栏、目录树、画廊、预览、双击大图、后台缩略图 + 磁盘缓存 | ✅ 已完成原型（MinGW） |
| **M2** | 抽离 **Image Core + Task Scheduler**（所有引擎的基础） | ⏳ 下一步 |
| **M3** | **Compare Engine**：多图并排、同步缩放/平移/框选、闪烁比较、差异图 | 计划 |
| **M4** | **Analysis Engine**：Histogram / RGB Mean / Difference → PSNR / SSIM / Noise | 计划 |
| **M5** | **Render Engine**：高质量缩放、Difference Overlay、HeatMap | 计划 |
| **M6** | SQLite 磁盘缓存 + 三级内存缓存 | 计划 |
| **M7** | CommandPalette / AnalysisPanel 整合、文件操作（重命名/回收站） | 计划 |

原则：**保留 M0/M1 作为可运行基线，渐进抽离引擎**，不过度设计，
但保证后续功能不会越做越难维护。

## 7. 当前 v0.1 原型局限（待重构项）

- 后台仅单个 `QThread` 做缩略图，无统一调度/线程池
- 仅一级缩略图磁盘缓存，无三级内存缓存、无 SQLite
- Compare / Analysis / Render 逻辑尚未从 QWidget 分离
- 工具链为 MinGW（目标切换 MSVC）
