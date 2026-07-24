# 评审行动项 — 2026-07-24

> 来源：产品评审（Directory Tree / Thumbnail View / Selection / Compare / Analyzer / Workspace）
> 当前项目阶段：**Product Beta → RC**（M16 进行中，M17-M18 已规划）
> 核心理念：**每新增 1000 行代码，都应该能让用户明显感觉到价值。不要再为了架构增加架构。**

---

## 当前 Milestone 状态回顾

| Milestone | 主题 | 状态 |
|-----------|------|------|
| M14 | Hardening & Cleanup | ✅ Done |
| M15 | Product Shell — Browse Workflow | ✅ Done |
| M16 | Professional Compare | 🔄 In Progress |
| M17 | Asset Management | ⬜ Planned |
| M18 | AI Workflow | ⬜ Planned |

---

## P0 — 建议马上处理（阻塞 Beta→RC 的关键体验问题）

### A-1: Directory Tree 成熟化 ⭐⭐⭐⭐⭐

**当前状态:** 已有 `directorytree.cpp/h`（QTreeView + QFileSystemModel + DirectoryProxyModel）和 `breadcrumbbar.cpp/h`，支持 `navigateTo()`、`refresh()`、右键菜单。但浏览体验还不像真正的图片浏览器。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-1.1 | 自动同步当前目录 | 切换图片/目录时，树自动 expand 到当前目录并高亮，无需手动展开 | 2d |
| A-1.2 | 自动展开父节点 | `navigateTo()` 已经做了 expand，需确保所有进入路径都触发（菜单打开、路径输入框、快捷键跳转、历史导航） | 1d |
| A-1.3 | 当前目录高亮 | 目录树中当前活跃目录需要视觉高亮（加粗/背景色），不只是选中态 | 0.5d |
| A-1.4 | 文件系统变化自动刷新 | 使用 `QFileSystemWatcher` 监听当前目录，新增/删除/重命名子目录和图片时自动更新树和缩略图面板 | 1.5d |
| A-1.5 | 大目录异步展开 | 展开包含 >1000 个子目录的节点时不应阻塞 UI；子节点懒加载或分批显示 | 2d |
| A-1.6 | 加载状态提示 | 展开大目录时显示 loading 指示器（spinner 或进度条） | 0.5d |

**验收标准:**
- [ ] 切换目录不用重新展开树（目录树自动跟随）
- [ ] 展开一万个目录不卡 UI（异步加载 + 虚拟化）
- [ ] Explorer 删除目录后 MViewer 自动刷新
- [ ] 当前目录永远保持同步（6 种进入路径全覆盖）

---

### A-2: Thumbnail View 产品化 ⭐⭐⭐⭐⭐

**当前状态:** `ThumbnailPanel` 已有 Thumbnail / LargeIcon / SmallIcon / Details / Filmstrip / Compact 六种 ViewMode，支持 SortName / SortDate / SortSize / SortResolution 四种排序，支持文件名过滤、元数据搜索、评分过滤。但离 FastStone / ImageGlass 级别还有差距。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-2.1 | View Mode 完善 | 检查并修复 Filmstrip（水平单行胶片条）和 Compact（紧凑网格）模式的实际渲染效果，确保切换流畅 | 1d |
| A-2.2 | 排序增强 | 补充 SortType（按文件类型/扩展名排序）和 SortRating（按评分排序）；排序方向可切换（升序/降序） | 1d |
| A-2.3 | 过滤器增强 | 补充文件类型快捷过滤按钮（JPG / PNG / RAW / TIFF / WebP 等），一键切换；当前 `setFilter` 仅支持文本搜索 | 1.5d |
| A-2.4 | 搜索完善 | 当前搜索功能分散在 `searchpanel.cpp` 和 `ThumbnailPanel::setFilter`；统一搜索结果高亮 + 搜索历史 + 正则支持 | 1.5d |
| A-2.5 | 大目录性能验证 | 10000+ 图片目录下，切换排序/视图模式无感知延迟（当前 `buildModel` 会重建模型，需验证性能） | 1d |
| A-2.6 | 缩略图质量 | 确保高 DPI 下缩略图清晰，支持缩略图锐化选项 | 0.5d |

**验收标准:**
- [ ] 能够替代 Windows Explorer 浏览图片（所有常用视图和排序可用）
- [ ] 10000+ 图片目录切换视图 < 200ms
- [ ] 文件类型过滤一键可达，无需手动输入扩展名

---

### A-3: Selection Model 统一 ⭐⭐⭐⭐

**当前状态:** `SelectionModel` 已存在作为单一真实来源（`selectionmodel.cpp/h`），`ThumbnailPanel` 通过 `selectPath()` 与它同步。但需全局审计确保没有 Widget 自己维护 selected。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-3.1 | 全局审计 | 搜索所有 `QItemSelectionModel`、`selectedPaths()`、`currentIndex()` 调用，确认都走 `SelectionModel` | 1d |
| A-3.2 | 多选支持 | `SelectionModel::setSelection()` 已支持多选，确认 Compare / Delete / Export / Batch 都通过 `SelectionModel::selection()` 获取选中列表 | 1d |
| A-3.3 | Compare 入口统一 | CompareWorkspace 的 `setImages()` 应从 SelectionModel 获取，而非 MainWindow 直接传递路径列表 | 1d |
| A-3.4 | 选区变化通知 | 确保所有 panel 在 `SelectionModel::selectionChanged` 时正确响应（状态栏、右键菜单、工具栏按钮状态） | 0.5d |

**验收标准:**
- [ ] 整个软件只有一份 Selection（`SelectionModel` 单例）
- [ ] 任何 Widget 的选中状态变化都能被其他 Widget 正确感知
- [ ] Compare / Delete / Export / Workspace 不维护独立选中状态

---

## P1 — 近期完成（增强专业用户价值）

### A-4: Compare Workspace 完善 ⭐⭐⭐⭐

**当前状态:** `CompareWorkspace` 已支持同步缩放/平移、Blink、Split/Swipe、Diff Overlay、多布局（2/3/4/8）、准星同步、基准锁定、Per-cell 调整、PSNR/SSIM 指标、布局预设。M16 正在进行比较引擎的收尾工作。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-4.1 | Overlay 模式 | 两张图叠加（半透明混合），用滑块控制上层不透明度 | 1.5d |
| A-4.2 | Grid 模式完善 | 当前 2/3/4/8 布局已存在，增加自定义行列数（M×N grid） | 1d |
| A-4.3 | Pixel Link | 两张图之间连线标记对应像素点（算法工程师比对 pixel-level 差异） | 1.5d |
| A-4.4 | 滚动同步 | 当前已有 pan sync，确认图片尺寸不同时仍能保持相对位置同步 | 0.5d |
| A-4.5 | 连续比较 | 支持"下一组"快捷操作（批量比较几百张图片，自动两两分组） | 2d |
| A-4.6 | Diff 增强 | 当前阈值滑块已有，增加 highlight 模式（差异区域红色高亮，相似区域灰度） | 1d |

**验收标准:**
- [ ] Compare 可以连续比较几百张图片（批量模式不中断）
- [ ] 6 种比较模式全部可用：Sync / Blink / Swipe / Diff / Overlay / Grid
- [ ] Pixel Link 可在两张图上标记对应像素并显示差值

---

### A-5: Metadata Panel 交互优化 ⭐⭐⭐

**当前状态:** `MetadataPanel` 是一个固定 QWidget（含 QTreeView + Rating + ColorLabel），`MetadataOverlay` 是 Viewer 上的半透明浮层。评审要求是默认隐藏、点击图片后才浮出。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-5.1 | 默认隐藏 | MetadataPanel 默认不在主界面占位置，作为浮动面板存在 | 0.5d |
| A-5.2 | 触发方式 | 点击图片（或快捷键 I）弹出右侧浮层，半透明背景，显示完整 EXIF + 评分 | 1d |
| A-5.3 | ESC 关闭 | 浮层面板支持 ESC 和点击外部区域关闭 | 0.5d |
| A-5.4 | 位置跟随 | 浮层跟随主窗口移动和 resize，不遮挡图片主体 | 0.5d |

**验收标准:**
- [ ] 浏览图片时 Metadata Panel 完全不占视野（默认隐藏）
- [ ] 一键呼出/关闭，半透明不遮挡

---

### A-6: Workspace 恢复完善 ⭐⭐⭐

**当前状态:** `AppState` 已保存 `lastDir`、`lastImage`、`lastThumbScroll`、`analysisVisible`、`analysisPage`、`navSidebarVisible`、`navHistory`。`WorkspaceSerializer` 和 `CompareSession` 已支持比较会话持久化。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-6.1 | Compare 状态恢复 | 启动时检测上次关闭前是否有活跃的 Compare 会话，自动恢复 | 1d |
| A-6.2 | Window Layout 恢复 | 保存/恢复窗口大小、位置、面板分隔比例（QSplitter 位置） | 0.5d |
| A-6.3 | Zoom/Scroll 恢复 | 保存/恢复查看器的当前缩放级别和滚动位置 | 0.5d |
| A-6.4 | Sidebar 状态恢复 | 左右侧边栏的展开/折叠状态、宽度 | 0.5d |
| A-6.5 | 崩溃恢复 | 利用 autosave 机制，异常退出后下次启动提示"恢复上次会话？" | 1.5d |

**验收标准:**
- [ ] 关闭软件 → 重新打开 → 完全恢复现场（目录、图片、Compare、Zoom、Layout）
- [ ] 崩溃后不丢数据，提示恢复

---

## P2 — 可以后做（增强平台能力）

### A-7: Analyzer Workflow 统一 ⭐⭐⭐

**当前状态:** `AnalysisPanel` 已有多页签（Histogram / RGB / Exposure / Focus / Metadata / Compare / DiffMap / Plugin / Inspector），已集成 `AnalyzerPipeline` 和 `AnalyzerRegistry`。评审认为入口偏工程化。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-7.1 | Analysis Panel 统一入口 | 将所有分析器（Histogram、Sharpness、Noise、Exposure、Color）在 Analysis Panel 中插件化展示，不再是散落的标签页 | 2d |
| A-7.2 | 新增 Analyzer 零 UI 改动 | 利用 `AnalyzerPipeline` + `AnalyzerRegistry`，新增分析器只需注册插件，Analysis Panel 自动发现并添加 | 1d |
| A-7.3 | 快捷分析 | 右键图片 → "分析" 子菜单列出所有可用分析器，一键运行并弹窗显示结果 | 1d |

**验收标准:**
- [ ] 新增 Analyzer 不需要改 MainWindow 或 AnalysisPanel
- [ ] 所有分析器统一从 Analysis Panel 进入，UI 一致

---

### A-8: GPU 优化（保持 OpenGL） ⭐⭐

**当前状态:** `GpuTileUploader` 已进行 GL 上下文探测，CPU compositor 是默认路径。评审明确建议不要切 D3D11/Vulkan。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-8.1 | Tile 渲染优化 | 大图分块加载 + 视口裁剪，减少 GPU 内存占用 | 2d |
| A-8.2 | Texture Cache 增强 | LRU 纹理缓存，避免重复上传同一纹理 | 1d |
| A-8.3 | 高 DPI 适配 | 确保 OpenGL 渲染在 150%/200% 缩放下像素级精确 | 1d |

**验收标准:**
- [ ] OpenGL 路径在 100MP+ 图片下滚动帧率 > 30fps
- [ ] 高 DPI 下无模糊和错位

---

### A-9: Plugin SDK 接口冻结 ⭐⭐

**当前状态:** `AnalyzerRegistry` + `extern "C"` ABI 已可用，`plugins/example` 已验证。但 Decoder / Exporter / Importer 接口尚未冻结。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-9.1 | Decoder 接口冻结 | 定义稳定的 `DecoderPlugin` ABI（输入路径 → 输出 ImageFrame），第三方可写解码器插件 | 2d |
| A-9.2 | Exporter 接口冻结 | 定义稳定的 `ExporterPlugin` ABI（输入 ImageFrame + 格式 + 参数 → 输出文件） | 1.5d |
| A-9.3 | Importer 接口冻结 | 定义稳定的 `ImporterPlugin` ABI（导入外部格式到 Workspace） | 1d |
| A-9.4 | SDK 文档 + 示例 | 为每种插件类型编写最小示例和文档 | 2d |

**验收标准:**
- [ ] Decoder / Analyzer / Exporter / Importer 四种插件接口全部冻结
- [ ] 每种类型至少一个可工作的第三方示例

---

### A-10: Undo/Redo 体系 ⭐⭐

**当前状态:** M7 引入了 `CommandStack` + `RotateCommand` + `LabelCommand`。`CropCommand` 也已存在。但 Delete / Move / Rename 等操作未进入 Command 体系。

**具体行动项:**

| # | 任务 | 说明 | 估时 |
|---|------|------|------|
| A-10.1 | DeleteCommand | 删除图片进入 Undo Stack，支持 Ctrl+Z 撤销删除 | 1d |
| A-10.2 | MoveCommand | 移动图片到其他目录可撤销 | 1d |
| A-10.3 | RenameCommand | 重命名图片可撤销 | 0.5d |
| A-10.4 | BatchCommand | 批量操作（多选删除/移动/重命名）作为一个原子 Command | 1d |
| A-10.5 | Undo/Redo UI | 工具栏按钮 + Ctrl+Z / Ctrl+Y 快捷键 + 操作历史列表 | 1d |

**验收标准:**
- [ ] Delete / Move / Rename / Rotate / Crop 全部可撤销
- [ ] 批量操作可原子撤销
- [ ] Ctrl+Z / Ctrl+Y 全局可用

---

## 保持不变（无需重构）

以下组件已达到产品级质量，**不建议为了"更优雅"去重构**：

- ✅ DecoderRegistry — 解码器分发已稳定
- ✅ CacheManager — 5 级缓存 + 预测预加载已工作良好
- ✅ TaskScheduler — 4 池优先级调度已验证
- ✅ Benchmark — 9 场景 harness 已就位
- ✅ Performance Gate — CI 硬性门禁已就位
- ✅ CI 工作流 — ci / release / nightly / perf-gate 完整
- ✅ Workspace 基础架构 — Workspace → Folder → ImageSet 数据模型稳定
- ✅ OpenGL 渲染路径 — 当前实现够用，无需切 D3D11/Vulkan
- ✅ Domain 分层 — Domain/Core/UI 三层分离已冻结

---

## 建议的后续 Milestone 调整

建议在现有 M16-M18 基础上，将评审指出的 P0/P1 行动项纳入，重新编排里程碑：

### M16: Professional Compare（当前 🔄，扩展范围）

原有 scope + A-4（Compare Workspace 完善）:
- Sync Compare Engine 收尾（editing-within-compare, reference/difference metrics, layout presets）
- **新增:** Overlay 模式、Pixel Link、连续比较、Diff 增强

**出口标准:** 算法工程师每天愿意用 Compare 功能。

---

### M17: Browse Experience Polish（新里程碑）

包含 A-1、A-2、A-3、A-5、A-10:

| Phase | 内容 | 优先级 |
|-------|------|--------|
| M17.1 | Directory Tree 成熟化（A-1.1 ~ A-1.6） | P0 |
| M17.2 | Thumbnail View 产品化（A-2.1 ~ A-2.6） | P0 |
| M17.3 | Selection Model 统一（A-3.1 ~ A-3.4） | P0 |
| M17.4 | Metadata Panel 交互优化（A-5.1 ~ A-5.4） | P1 |
| M17.5 | Undo/Redo 体系（A-10.1 ~ A-10.5） | P2 |

**出口标准:** 浏览体验达到 FastStone / ImageGlass 水平。

---

### M18: Professional Workflow（新里程碑）

包含 A-6、A-7、A-9:

| Phase | 内容 | 优先级 |
|-------|------|--------|
| M18.1 | Workspace 恢复完善（A-6.1 ~ A-6.5） | P1 |
| M18.2 | Analyzer Workflow 统一（A-7.1 ~ A-7.3） | P2 |
| M18.3 | Plugin SDK 接口冻结（A-9.1 ~ A-9.4） | P2 |

**出口标准:** 专业用户能建立完整工作流：打开 → 浏览 → 分析 → 比较 → 导出 → 关闭 → 恢复。

---

### M19: GPU + Release Preparation（新里程碑）

包含 A-8 + 发布准备:

| Phase | 内容 | 优先级 |
|-------|------|--------|
| M19.1 | GPU 优化（A-8.1 ~ A-8.3） | P2 |
| M19.2 | Bug Bash | — |
| M19.3 | 性能回归测试 | — |
| M19.4 | 长时间稳定性测试（24h 持续运行） | — |
| M19.5 | 安装包完善 + 自动更新 | — |
| M19.6 | Release Candidate | — |

**出口标准:** v1.0.0 正式发布。

---

## 优先级总结

```
P0 (阻塞 RC):
  A-1  Directory Tree    ⭐⭐⭐⭐⭐  ~7.5d
  A-2  Thumbnail View    ⭐⭐⭐⭐⭐  ~6.5d
  A-3  Selection Model   ⭐⭐⭐⭐   ~3.5d

P1 (增强专业价值):
  A-4  Compare Workspace ⭐⭐⭐⭐   ~7.5d
  A-5  Metadata Panel    ⭐⭐⭐    ~2.5d
  A-6  Workspace 恢复    ⭐⭐⭐    ~4d

P2 (平台能力):
  A-7  Analyzer Workflow ⭐⭐⭐    ~4d
  A-8  GPU 优化          ⭐⭐     ~4d
  A-9  Plugin SDK        ⭐⭐     ~6.5d
  A-10 Undo/Redo         ⭐⭐     ~4.5d
```

**总估时:** P0 ~17.5d / P1 ~14d / P2 ~19d

---

## 评审人最想看到的三个改动（按用户价值排序）

| 排名 | 改动 | 当前文件 | 核心工作 |
|------|------|----------|----------|
| 🥇 | Directory Tree 成熟化 | `src/directorytree.cpp/h` | A-1.1 ~ A-1.6 |
| 🥈 | Thumbnail 浏览体验 | `src/thumbnailpanel.cpp/h` | A-2.1 ~ A-2.6 |
| 🥉 | Compare Workflow | `src/compareworkspace.cpp/h` | A-4.1 ~ A-4.6 |
