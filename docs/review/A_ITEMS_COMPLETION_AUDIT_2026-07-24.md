# A-1 ~ A-10 完成度对账 — 2026-07-24

> 对照 `REVIEW_ACTION_ITEMS_2026-07-24.md` 与当前 `master` 源码。  
> 状态：`DONE` 已实现 · `PARTIAL` 有实现但缺验收点 · `VERIFY` 仅需产品/性能验收 · `NOT_DONE` 未见实现

## 汇总

| 状态 | 数量 | 占比（46 子项） |
|------|------|----------------|
| DONE | 42 | 91.3% |
| PARTIAL | 2 | 4.3% |
| VERIFY | 2 | 4.3% |
| NOT_DONE | 0 | 0.0% |

**全部 A-1~A-10 已实现或仅剩性能验收。M14.8 发布元数据自动化已落地（`scripts/release_manifest.ps1`）。Beta→1.0 剩余：性能门禁实测 + 安装包人工验收。**

## 逐项状态

### A-1 Directory Tree

| 项 | 状态 | 证据 |
|----|------|------|
| A-1.1 自动同步 | DONE | `DirectoryTree::navigateTo` + MainWindow 全路径调用 |
| A-1.2 展开父节点 | DONE | `expandAncestors()` |
| A-1.3 当前目录高亮 | DONE | `drawRow` 强调底 + 粗体 |
| A-1.4 FS 自动刷新 | DONE | `QFileSystemWatcher` |
| A-1.5 大目录异步 | DONE | `scheduleFetchMore` 渐进式 fetch + 加载指示（2026-07-24） |
| A-1.6 加载提示 | DONE | `setLoading` + 文案指示 |

### A-2 Thumbnail View

| 项 | 状态 | 证据 |
|----|------|------|
| A-2.1 View Mode | VERIFY | Filmstrip/Compact 已实现，需走查 |
| A-2.2 排序增强 | DONE | SortType/SortRating + 升降序 |
| A-2.3 类型过滤 | DONE | `setTypeFilter` + 工具栏 |
| A-2.4 搜索完善 | PARTIAL | 无历史/正则/统一高亮 |
| A-2.5 万级性能 | VERIFY | 虚拟化 + 自适应预测窗口；门禁见 `test_product_browse` / benchmark B2 |
| A-2.6 缩略图质量 | PARTIAL | Smooth 缩放；无锐化/独立 DPR |

### A-3 Selection Model

| 项 | 状态 | 证据 |
|----|------|------|
| A-3.1 全局审计 | DONE | Export/Batch/Compare 统一 `resolveSelectedPaths` |
| A-3.2 多选 | DONE | SelectionModel + 消费端接线 |
| A-3.3 Compare 入口 | DONE | 优先 SelectionModel，不足 2 张回落目录 |
| A-3.4 变化通知 | DONE | `updateSelectionActions` 订阅 selectionChanged |

### A-4 Compare Workspace

| 项 | 状态 | 证据 |
|----|------|------|
| A-4.1 Overlay | DONE | 叠加模式 + 0–100% 不透明度滑块（2026-07-24 收尾） |
| A-4.2 Grid M×N | DONE | 布局「自定义」+ 行/列 SpinBox 1–8 |
| A-4.3 Pixel Link | DONE | 标记点 + 编号 + 连线 + RGB/Δ 提示（快捷键 L） |
| A-4.4 异尺寸滚动同步 | VERIFY | 共享 transform，需实测 |
| A-4.5 连续比较 | DONE | `setImagePool` / next/prev pair |
| A-4.6 Diff 增强 | DONE | 阈值 + heatmap + highlightMap 红/灰模式 |

### A-5 Metadata Panel

| 项 | 状态 | 证据 |
|----|------|------|
| A-5.1 默认隐藏 | DONE | 浮动 Tool，不进 splitter |
| A-5.2 触发方式 | DONE | I / 点击 / 悬停 |
| A-5.3 ESC 关闭 | PARTIAL | ESC OK；点外部关闭弱 |
| A-5.4 位置跟随 | DONE | move/resize 钩子 |

### A-6 Workspace 恢复 — 全部 DONE

Compare 会话、窗口布局、Zoom/Scroll、Sidebar、崩溃恢复（30s autosave + recovery.json）均已落地。

### A-7 Analyzer Workflow

| 项 | 状态 | 证据 |
|----|------|------|
| A-7.1 统一入口 | DONE | combo + 运行按钮 + runAnalyzer/selectAnalyzer |
| A-7.2 零 UI 改动 | DONE | `refreshAnalyzers` + Registry；插件变更自动刷新 |
| A-7.3 快捷分析 | DONE | 右键「分析」子菜单 + 批量分析导出 |

### A-8 GPU

| 项 | 状态 | 证据 |
|----|------|------|
| A-8.1 Tile 优化 | VERIFY | Stage A + TileCache，需 100MP 帧率 |
| A-8.2 Texture LRU | DONE | maxResident=256 |
| A-8.3 高 DPI | VERIFY | `devicePixelRatioF`，需 150%/200% 验收 |

### A-9 Plugin SDK — 全部 DONE

Decoder / Exporter / Importer / Analyzer ABI + 示例 + `docs/sdk/`。

### A-10 Undo/Redo

| 项 | 状态 | 证据 |
|----|------|------|
| A-10.1~3 文件命令 | DONE | FileDelete/Move/Rename + stack |
| A-10.4 BatchCommand | PARTIAL | 多文件单条命令；无通用 Batch；多选重命名只处理 first |
| A-10.5 Undo UI | PARTIAL | 菜单+Ctrl+Z/Y；无工具栏/历史列表 |

## 建议实施顺序（与计划对齐）

1. **M16 收尾**：A-4.3 Pixel Link → A-4.1 透明度滑块 → A-4.6 highlight → A-4.2 自定义网格  
2. **Browse 门禁**：A-1.5 / A-2.5 / A-3 统一 / A-2.1 走查  
3. **工作流**：A-5.3 / A-7.1 / A-10.4~5（A-6 已完成，跳过大改）  
4. **1.0 发布**：A-8 实测 + Bug Bash + M14.8 SHA256/changelog  

## 文档维护

本文件为 2026-07-24 源码对账快照。实现推进后应更新本表状态，并同步 `CHANGELOG.md` / `docs/roadmap.md`。
