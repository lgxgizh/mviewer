---
name: MViewer Product-Focused Roadmap Refinement
overview: 基于指导意见，将现有 M16/M17/M18 里程碑重新定义为以产品体验和专业工作流为核心的新里程碑序列：M17（专业浏览器体验）、M18（专业分析工作流）、M19（产品化能力），明确每个行动项的当前状态、差距和具体交付物。
todos:
  - id: polish-directory-tree
    content: Enhance DirectoryTree with auto-sync, expand recent path, highlight current dir, folder change refresh, MRU, favorites, history, and filtered tree view.
    status: completed
  - id: polish-thumbnail-views
    content: Refine ThumbnailView modes, sorting, filtering, fuzzy search, rating/color label support, and selection-driven highlighting.
    status: completed
  - id: unify-selection-state
    content: Extend SelectionModel integration across Thumbnail, Compare, Workspace, Export, and Analyzer; harden multi-select, drag select, delete, and rename behaviors.
    status: completed
  - id: refine-metadata-ux
    content: "Improve MetadataOverlay UX: default hidden, click-to-show right panel, I key toggle, semi-transparent presentation, and clean dismissal flow."
    status: completed
  - id: complete-compare-workflow
    content: Finish editing inside compare, quick reference/difference metrics, per-pane histogram overlay, layout preset save/load, swap panes, and expanded multi-image sync support.
    status: completed
  - id: enhance-pixel-inspector
    content: Add copy RGB, copy HEX, copy XYZ, retain neighborhood statistics, and wire results into inspector and analysis outputs.
    status: completed
  - id: strengthen-roi-analyzer
    content: Add ROI histogram, mean, and standard deviation within selected regions and connect them to unified analyzer results.
    status: completed
  - id: parallel-analyzer-results
    content: Support multiple simultaneous analyzers and present combined results in one coherent panel instead of scattered outputs.
    status: completed
  - id: improve-batch-export-workflow
    content: Add pause, cancel, resume, and progress feedback to BatchDialog/BatchProcessor; extend ExportDialog with resize, crop, quality, metadata, color space, and ICC options.
    status: completed
  - id: harden-release-engineering
    content: Build installer/portable automation, crash reporting, structured logging, config import/export, version migration, and update pipeline readiness.
    status: completed
  - id: freeze-plugin-sdk-interface
    content: Stabilize plugin SDK interfaces for Decoder, Analyzer, Exporter, Importer, and Compare algorithm; document expectations and reduce third-party friction.
    status: completed
  - id: verify-product-acceptance
    content: Run local build+test verification, confirm browse replaces Explorer for images, validate unified selection, metadata UX, compare workflow, batch/export completeness, and release readiness.
    status: completed
---

## User Requirements

用户基于当前项目阶段，提出下一阶段重点从“基础设施稳定”转向“产品体验与专业工作流”。目标是把 MViewer 做成一款用户愿意每天使用的图片浏览器/分析工具。

### 功能范围

围绕四个核心方向细化后续行动项：

#### ① 浏览体验继续打磨（P0）

- **左侧 Directory Tree**
- 自动同步当前目录
- 自动展开最近访问路径
- 当前目录高亮
- 文件夹变化自动刷新
- 最近目录 / 收藏夹 / 历史记录入口
- **Thumbnail View**
- 大图标、小图标、列表、详情、Film Strip 等视图模式继续完善
- 排序能力扩展为：Name / Date / Size / Type / Rating / Color Label
- 筛选能力扩展为：文件类型 + 评分标签 + 颜色标签 + Reject/Pick/Recent
- 搜索支持：文件名、后缀、模糊匹配
- **Selection 统一**
- 建立唯一 `SelectionModel`，所有模块读取同一状态
- 覆盖 Thumbnail、Compare、Workspace、Export、Analyzer
- 支持框选、Shift/Ctrl 多选、Delete/Rename 自然联动
- **Metadata UX**
- 默认隐藏
- 点击图片后显示半透明右侧 Panel
- 快捷键 `I` 切换显示/隐藏

#### ② Compare Workflow 增强（P0）

- 在现有 2/3/4/8/自定义网格基础上，继续强化同步浏览
- 增加并完善：Blink、Overlay、Split、Swipe、Diff Heatmap、Pixel Link
- 向多图对比演进：支持 2/4/8/16 张图同步浏览
- 补齐编辑内比较、参考/差异指标、布局预设保存/加载、互换窗格

#### ③ Metadata UX 优化（P0）

- 默认隐藏元数据面板
- 点击图片后展示半透明 Metadata Overlay
- 再次点击或按 `I` 隐藏
- 展示文件名、尺寸、大小、日期、EXIF 信息

#### ④ Professional Analysis Workflow（P1）

- **Pixel Inspector 增强**
- Copy RGB / Copy HEX / Copy XYZ
- 保留现有 neighborhood stats 基础
- **ROI 增强**
- 在 ROI 内运行 Histogram / Mean / Stddev
- 结果直接接入 Analyzer 体系
- **Analyzer 工作流**
- 支持多个 Analyzer 同时运行
- 结果统一展示，避免多出口
- **Batch / Export 工作流**
- 后台运行 Batch Job
- 支持暂停、取消、恢复
- Export 增加 Resize / Crop / Quality / Metadata / Color Space / ICC 选项

#### ⑤ 产品化能力补强（P1/P2）

- **Installer / Portable / ZIP**
- 安装包、便携版、自动升级流程
- **Crash Report**
- minidump + 日志归档
- 崩溃后可查看上次会话上下文
- **日志系统**
- 结构化日志、分级输出
- **配置导入导出**
- Workspace / 插件 / 设置迁移
- **Plugin SDK 接口冻结与易用性**
- 保持 ABI 稳定
- 让第三方 DLL 可直接放入使用

### 验收标准

- 能替代 Windows Explorer 浏览图片
- Selection 在各模块间完全一致
- Metadata 默认不抢眼，需要时一键呈现
- Compare 成为专业对比工作流核心
- 批量处理、导出、崩溃报告形成完整闭环

## Technology Stack

- Frontend framework: **Qt 6 Widgets (C++)**
- Build system: **CMake + MSVC via `build.ps1`**
- Language: **C++20**
- Architecture: **UI → Application → Core → Domain**
- State management: **existing `SelectionModel` + domain types**
- Persistence: **existing Workspace serialization + JSON session files**
- Testing: **CTest + existing unit/integration suites**

## Implementation Approach

### High-Level Strategy

本轮不再重构底层引擎，而是在已有架构上做“垂直体验补全”：

1. **Browse**：把 DirectoryTree / ThumbnailView / Selection / Metadata 做成真正可用的浏览体验。
2. **Compare**：把 CompareEngine 已具备的能力收敛成专业工作流，补齐最后一段 UX 缺口。
3. **Analysis / Batch / Export**：把现有 Analyzer、Job、Export 串成连贯的专业工作流。
4. **Release engineering**：把安装、更新、崩溃上报、日志、配置迁移补齐到发布级。

### Key Technical Decisions

1. **复用现有 `SelectionModel`，不做新的全局状态管理器**

- 原因：当前已经存在统一的选择模型，应优先接驳而非新建抽象。

2. **Metadata 采用轻量 Overlay 方案，而不是重型独立面板**

- 原因：符合“默认隐藏、按需展示”的产品目标。

3. **Compare 继续沿用 `CompareEngine` + `CompareWorkspace` 分层**

- 原因：现有分解已经足够清晰，重点是补齐 UI 集成与持久化。

4. **Batch / Export 继续复用 `JobSystem` / `ExportManager`**

- 原因：已有任务调度和导出框架，只需补齐暂停/恢复/进度反馈。

5. **Release 工程以增量方式建设，不重写整个发布链路**

- 原因：安装包、zip、crash report 已有基础，先补自动化和稳定性。

### Performance & Reliability

- Browse 场景继续保持非阻塞：缩略图、目录扫描、筛选仍走异步路径。
- Compare 场景关注滚动/变换同步抖动，避免大图下帧率下降。
- Batch 场景限制并发数，防止磁盘 I/O 打满。
- Metadata / Crash reporter 只做最小写入，不影响主流程性能。

### Avoiding Technical Debt

- 不在本轮引入新缓存层、新调度器、新注册表或新 CI 门禁。
- 所有新增交互尽量挂载到现有信号链和现有域对象上。
- 若发现跨模块边界问题，优先通过接口归一解决，不做大规模重构。

## Implementation Notes (Execution Details)

### Grounded in Existing Code

- Browse 相关实现主要在：
- `src/directorytree.h`
- `src/thumbnailpanel.h`
- `src/selectionmodel.h`
- `src/metadataoverlay.h`
- Compare 相关实现主要在：
- `src/compareworkspace.h`
- `src/core/compare/CompareEngine.h`
- Pixel / Analyzer 相关实现主要在：
- `src/widgets/rawimageview.h`
- `src/analysispanel.h`
- `src/core/analysis/PixelInspector.h`
- Batch / Export 相关实现主要在：
- `src/batchdialog.h`
- `src/exportdialog.h`
- `src/core/batch/BatchProcessor.h`
- Release 相关实现主要在：
- `scripts/package_release.ps1`
- `scripts/package_portable.ps1`
- `installer/mviewer.nsi`
- `src/core/CrashHandler.h`

### Performance Considerations

- DirectoryTree 在大目录场景要控制 watcher 事件风暴。
- ThumbnailPanel 在切换视图模式时要复用已完成的缩略图，避免重复解码。
- Compare 的 diff heatmap 只在变化区域绘制，不全屏覆写。
- Batch 任务内部要做分片与节流，避免同时打开过多文件句柄。

### Logging and Observability

- 复用现有日志机制，不新增额外 logger 体系。
- 对关键用户动作增加结构化埋点：浏览、选择、比较、批处理、导出、崩溃。
- 日志级别分级：Info / Warn / Error，避免调试噪声。

### Blast Radius Control

- 优先保证现有功能不被破坏：`build.ps1 Test` 必须持续绿。
- 新增交互尽量向后兼容，避免推翻既有快捷键与菜单结构。
- 若某项改动风险较高，拆成“最小可行版本 + 后续迭代”。

## Architecture Design

### System Structure

```text
UI Layer (Qt Widgets)
├── DirectoryTree
├── ThumbnailPanel
├── CompareWorkspace
├── AnalysisPanel
├── BatchDialog
├── ExportDialog
└── MetadataOverlay

Application / UseCase Layer
├── browse use cases
├── compare use cases
├── analysis use cases
├── batch use cases
└── export use cases

Core Layer
├── CompareEngine
├── AnalysisEngine
├── BatchProcessor
├── ExportManager
├── Scheduler / JobSystem
└── ImageRepository

Domain Layer
├── Selection
├── CompareSession
├── BatchJobConfig
└── Workspace
```

### Data Flow

1. **Browse**: Directory scan → thumbnail generation → selection model update → metadata overlay render.
2. **Compare**: image set selection → CompareWorkspace open → engine sync / blink / overlay → session persist.
3. **Analysis**: selection or region → AnalyzerRegistry run → unified result panel.
4. **Batch**: job config → background processor → progress / pause / cancel / resume.
5. **Export**: source list → format/quality/resize options → writer pipeline → output reporting.

### Module Responsibilities

- **DirectoryTree**: 目录导航、过滤、高亮、刷新。
- **ThumbnailPanel**: 多种视图、排序、筛选、搜索、选择联动。
- **SelectionModel**: 统一选择态，驱动所有子模块。
- **MetadataOverlay**: 轻量元数据显示与切换。
- **CompareWorkspace**: 多图布局、同步、编辑、持久化。
- **AnalysisPanel**: 多 Analyzer 并行结果汇总。
- **BatchDialog + BatchProcessor**: 批处理任务生命周期管理。
- **ExportDialog + ExportManager**: 导出参数编排与执行。
- **CrashHandler + logging**: 崩溃采集与运行日志。