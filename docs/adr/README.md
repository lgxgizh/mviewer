# Architecture Decision Records (ADR) — Index

> 本索引是 Agent / Reviewer 的**第一入口**。任何 Review、重构提案、架构讨论，
> 必须先对照本索引：已有 ADR 的决策不得被静默推翻；要推翻，先提交新 ADR 取代旧 ADR。

## 核心决策（按主题）

### 图像与数据模型

| ADR | 决策 | 一句话理由 |
|---|---|---|
| [002](002-why-image-data-not-qimage.md) | 核心层用 `ImageData`，不用 `QImage` | Core 必须 Qt-free，支持 >8bit / 多平面格式 |
| [008](008-why-unified-image-frame.md) | 统一 `ImageFrame` 贯穿解码→缓存→渲染 | 消除层间格式转换与拷贝 |

### 分层与依赖

| ADR | 决策 | 一句话理由 |
|---|---|---|
| [001](001-why-qt-widgets.md) | UI 用 Qt Widgets（非 QML） | 成熟、可控、专业桌面工具场景 |
| [004](004-why-repository-pattern.md) | Repository 统一管理图片访问 | UI 不直接触碰缓存/解码细节 |
| [010](010-why-ui-widgets-lightweight.md) | UI Widget 保持轻量 | 业务逻辑下沉 Application/Core |
| [011](011-viewer-core-boundary.md) | Viewer/Core 边界冻结 | 防止 UI 逻辑渗入核心层 |
| [014](014-ui-tu-split-by-responsibility.md) | 大 UI 文件按职责拆 TU | `mainwindow.cpp` <1000 行等硬约束 |

### 缓存与调度

| ADR | 决策 | 一句话理由 |
|---|---|---|
| [006](006-why-hierarchical-cache.md) | 分级缓存（内存/缩略图/磁盘） | 不同访问模式不同容量策略 |
| [007](007-why-priority-scheduler.md) | 优先级调度器 | 可视区域 > 预取 > 后台任务 |

### Compare 与分析

| ADR | 决策 | 一句话理由 |
|---|---|---|
| [003](003-why-compare-session-independent.md) | CompareSession 独立于浏览状态 | 比较是独立工作流，可随时进出 |
| [009](009-why-split-compare-engine.md) | Compare 引擎独立 Controller | 与 UI 解耦，可测试 |
| [005](005-why-plugin-analysis.md) | 分析能力走插件框架 | 核心不绑定具体算法 |
| [013](013-p2-plugin-sdk-frozen.md) | Plugin SDK 冻结 | ABI 稳定性优先于灵活性 |

### 交互细节

| ADR | 决策 | 一句话理由 |
|---|---|---|
| [012](012-p0-selection-hover-local.md) | 选中/悬停状态本地化 | 避免全局状态同步开销 |

## M22 专题决策

| 文档 | 主题 |
|---|---|
| [M22_COMPARE_ALIGNMENT](M22_COMPARE_ALIGNMENT.md) | Compare 对齐 |
| [M22_ANALYSIS_OVERLAYS](M22_ANALYSIS_OVERLAYS.md) | 分析叠加层 |
| [M22_FORMAT_COVERAGE](M22_FORMAT_COVERAGE.md) | 格式覆盖 |
| [M22_PREFERENCES](M22_PREFERENCES.md) | 偏好设置 |

## 治理规则（供 Agent Review 引用）

1. **UI 不得直接引用 Cache**（ADR-004/006）→ `scripts/architecture_gate.ps1` R1
2. **Widget 不得直接访问 Repository**（ADR-004/010）→ architecture_gate R2
3. **Compare 不得依赖 Thumbnail**（ADR-003/009）→ architecture_gate R3
4. **Core/Domain 头文件 Qt-free**（ADR-002/011）→ `scripts/audit_qt_boundary.ps1`
5. **UI 大文件行数上限**（ADR-014）→ `scripts/complexity_gate.ps1`

违反以上规则的 PR：architecture_gate 报 Warning，Reviewer（Hermes）依据对应 ADR 裁决。

## 如何新增 ADR

1. 复制模板：编号递增（`015-...md`），文件名 `NNN-短横线主题.md`
2. 必含四节：Context / Decision / Consequences / Status
3. 取代旧决策时：新 ADR 标注 `Supersedes: ADR-NNN`，旧 ADR 标注 `Superseded by`
4. 更新本索引

`legacy/` 目录存放历史文档，仅供考古，不作为当前决策依据。
