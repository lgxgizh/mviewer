---
name: P0_Product_Polish_Browser_Selection_Overlay
overview: 按评审意见落实 P0 三项：浏览器体验补齐（目录自动定位/树内搜索/收藏可视化）、SelectionModel 真正统一（消除 Compare/Analysis/Metadata 各自的 current 副本）、Metadata Overlay 增强（GPS + 内嵌直方图）。
todos:
  - id: browser-directory
    content: 用 [subagent:code-explorer] 定位后补齐目录导航：自动定位/树内搜索/收藏可视化/目录历史
    status: completed
  - id: browser-raw-filter
    content: 扩 thumbnailpanel 类型筛选支持 raw/tiff 别名展开常见后缀
    status: completed
  - id: selection-unify
    content: 用 [subagent:code-explorer] 定位后统一 SelectionModel：Compare 写回、面板监听、记 hover 本地 ADR
    status: completed
  - id: metadata-overlay
    content: Metadata Overlay 增强：GPS 字段贯通（domain/core）与内嵌迷你直方图
    status: completed
  - id: verify-docs
    content: build.ps1 Test 全绿后更新 CHANGELOG 与 ADR 文档
    status: completed
    dependencies:
      - browser-directory
      - browser-raw-filter
      - selection-unify
      - metadata-overlay
---

## 用户需求

依据外部评审结论"MViewer 已进入做产品阶段，停止建设新基础设施，重心转向浏览→对比→分析→导出工作流"，本次落地 P0 三项：

## 产品概述

在不新增任何 Manager/Registry/Scheduler 的前提下，把现有功能打磨到成熟图片浏览器的产品体验，并消除"当前图/选中图"的多份状态副本。

## 核心特性

### ① 浏览器体验补齐

- 打开/切换图片时，左侧目录树自动定位并高亮当前图片所在目录（自动展开祖先）。
- 目录树顶部新增过滤框，支持按目录名模糊搜索，并保留匹配分支的祖先链。
- 目录树/左栏可视化"收藏目录"列表，支持点击跳转与右键/按钮添加移除（复用已有 DirectoryModel 收藏数据）。
- 新增独立的目录级前进/后退历史（与现有图片历史并存），提供工具栏按钮。
- 缩略图类型筛选支持 `raw` 别名，自动展开为常见 RAW 后缀集合（cr2/nef/arw/dng 等）。

### ② SelectionModel 真正统一

- CompareWorkspace 中锁定/切换基准格时，将对应图片写回全局 SelectionModel，使全局"当前图"唯一。
- SelectionModel 的 currentImageChanged 变化时，CompareWorkspace 若池中包含该图则同步焦点。
- MetadataPanel / AnalysisPanel 改为直接监听 SelectionModel，而非仅由 MainWindow 推送。
- 明确决策：hover 属于瞬态视觉状态，保留在各 Widget 本地（不进 SelectionModel），写入 ADR。

### ③ Metadata Overlay 增强

- 新增 GPS 元数据贯通：领域层 ImageMetadata 增加经纬度/海拔字段；MetadataReader 增加轻量 JPEG EXIF GPS 解析（QImageReader text 兜底）；Overlay 与 MetadataPanel 显示 GPS（度分秒格式化 + 地图链接）。
- Overlay 内嵌迷你直方图：复用现有 HistogramWidget，从 ImageViewer 已解码帧惰性计算，避免重复解码。

## 验收标准

- 跨目录打开图片时目录树自动定位无遗漏、无循环。
- 整个软件"当前图"仅 SelectionModel 一份真相；Compare 锁定基准图即反映到全局。
- Overlay 按 I 显示 / ESC 隐藏，且含 EXIF/镜头/ICC/GPS/直方图。

## 技术栈与约束

- C++20 / Qt 6 Widgets；`domain/`、`core/` 头文件禁止 Qt 类型，`UI` 层才可用 Qt（沿用现有分层）。
- 构建仅用 `powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 [Release|Debug|Test]`；本地 `.\build.ps1 Test` 必须绿后才能提交。
- 改动遵循现有模式：状态走 `SelectionModel`（UI 层 QObject SSOT）、持久化走 `AppState`（mviewer.json）/ `RatingStore`；不引入任何新的 Manager/Registry 类。

## 实现策略

1. **浏览器目录（低风险、纯增强）**：在 `onImageOpen`（`mainwindow.cpp:1595-1605`）中于 `setCurrentImage` 后调用 `m_directoryTree->navigateTo(QFileInfo(path).absolutePath())` 实现自动定位（`navigateTo` 已在 `directorytree.cpp:207` 展开祖先并滚动，且不改动图片选择，无需防循环）。目录树过滤：在 `DirectoryTree` 顶部加 `QLineEdit`，扩展 `DirectoryProxyModel::filterAcceptsRow`（`directorytree.cpp:29-45`）实现"目录名包含文本 或 存在匹配后代 或 为匹配节点的祖先"三态保留。收藏可视化：在左栏目录树上方新增轻量 `FavoritesBar` 小组件（QListWidget），数据源复用 `DirectoryModel::favorites()`（`directorymodel.h:28`），点击→`navigateTo`+打开目录，右键/按钮增删经 `DirectoryModel::addFavorite/removeFavorite`。目录历史：在 MainWindow 维护 `m_dirHistory`/`m_dirHistoryIdx`，于 `openDirectory`/`changeDirectory`/`onBreadcrumbPath` 入栈，新增两个工具栏动作触发 goBack/goForward。
2. **缩略图 raw 别名（极小改动）**：扩展 `ThumbnailPanel::setTypeFilter`（`thumbnailpanel.cpp:414-421`），将 `"raw"` 展开为常见 RAW 后缀集合（cr2,cr3,nef,arw,dng,raf,rw2,orf,sr2,pef,...），`"tiff"`→`"tif,ti"` 归一，其余原样透传。
3. **SelectionModel 统一（核心，需谨慎接线）**：将 `SelectionModel*` 传入 `CompareWorkspace`（构造函数或 `setSelectionModel`）。当 `m_focusIndex`/`m_pairIndex` 因用户锁定基准格变化（`compareworkspace.cpp:1192-1209`）时调用 `m_selection->setCurrentImage(pool[idx])`；并 `connect(selection, &SelectionModel::currentImageChanged, this, ...)` 在池中同步焦点（仅当该图存在于 `m_imagePool`）。`MetadataPanel` 改为 `connect(m_selection, currentImageChanged, ...)` 替代 MainWindow 推送；`AnalysisPanel` 读取当前图统一经 `resolveSelectedPaths`/`currentImagePath()`。hover 保留本地（见 ADR）。
4. **Metadata Overlay（中风险、需解析与复用）**：

- **GPS**：`domain/Image.h` 的 `ImageMetadata` 增 `double gpsLatitude=0, gpsLongitude=0, gpsAltitude=0; bool hasGps=false;`（纯 std 类型，合规）。`MetadataReader::read` 增加 `readGps()`：先尝试 `QImageReader::text()` 的 GPS 键（Qt 插件可用时），否则解析 JPEG APP1→Exif→GPS IFD（自实现轻量解析，无第三方依赖），把 rational 转 double。Overlay/Panel 显示度分秒 + 可选地图链接。
- **直方图**：`ImageViewer` 暴露 `histogram()`（从已解码帧经 `AnalysisEngine`/`HistogramAnalyzer` 惰性计算并缓存），`MetadataOverlay` 内嵌一个 `HistogramWidget` 子控件，显示时向其 `setHistograms({viewer->histogram()})`；隐藏时 `clear()`。绝不重新解码像素。

## 实现注意事项（防回归）

- 自动定位：`navigateTo` 仅展开并滚动，不改图片选择，因此不会触发 `onImageOpen` 回环；若担心，可在 `onImageOpen` 处加一次 `m_dirNavigating` 标志保护。
- 性能：目录树文本过滤仅作用于目录节点（数量远小于图片），保留祖先链避免全树展开；缩略图面板已虚拟化（10000 图无压力）。GPS 解析仅对 JPEG 触发且只读头；直方图仅在 Overlay 显示时计算一次并缓存。
- 日志：新增 GPS 解析失败仅 `qDebug` 一行且不影响主流程；不 dump 大 payload。
- 向后兼容：`ImageMetadata` 新增字段均有默认值；既有 `AppState`/`RatingStore` 持久化格式不变。

## 架构设计

- 仍严格 `UI → Application → Core → Domain`。本次只改 UI 层接线 + 一处 Core（`MetadataReader` 加 GPS）+ 一处 Domain（`ImageMetadata` 加字段），不动架构。
- 数据流：用户操作 → SelectionModel（唯一 current/selection）→ 各面板经信号监听渲染；CompareWorkspace 反向写回 SelectionModel 形成闭环。

## 目录结构（修改/新增文件）

```
src/domain/Image.h                       # [MODIFY] ImageMetadata 增加 gpsLatitude/gpsLongitude/gpsAltitude/hasGps 字段（默认值）。
src/core/image/MetadataReader.cpp        # [MODIFY] 增加 readGps()：JPEG EXIF GPS IFD 解析 + QImageReader text 兜底；read() 填充 GPS 字段。
src/core/image/MetadataReader.h          # [MODIFY] 声明 readGps() 辅助函数。
src/metadataoverlay.h/.cpp               # [MODIFY] 顶部"GPS"行；内嵌 HistogramWidget 子控件，显示时由 ImageViewer 取直方图。
src/imageviewer.h/.cpp                   # [MODIFY] 暴露 histogram()（惰性计算并缓存当前帧直方图），供 Overlay 使用。
src/directorytree.h/.cpp                 # [MODIFY] 顶部过滤 QLineEdit；DirectoryProxyModel 增加目录名文本过滤（保留祖先链）。
src/mainwindow.cpp                       # [MODIFY] onImageOpen 调 navigateTo；新增目录历史栈+工具栏动作；左栏 FavoritesBar 接线；Overlay↔Viewer 直方图接线；向 CompareWorkspace 注入 SelectionModel。
src/compareworkspace.h/.cpp              # [MODIFY] 持有 SelectionModel*；焦点变化写回 setCurrentImage；监听 currentImageChanged 同步焦点。
src/metadatapanel.h/.cpp                 # [MODIFY] 改为监听 SelectionModel::currentImageChanged，移除 MainWindow 推送依赖。
src/analysispanel.cpp                    # [MODIFY] 当前图统一经 SelectionModel/currentImagePath 读取。
src/thumbnailpanel.cpp                   # [MODIFY] setTypeFilter 扩展 raw/tiff 别名展开。
src/test_metadata_gps.cpp                # [NEW] GPS rational→double 与解析路径的单元测试用例。
docs/adr/                                # [NEW] ADR：hover 保留本地、SelectionModel 为唯一 current/selection 真相源（P0 产品打磨决策）。
CHANGELOG.md                             # [MODIFY] 记录 P0 浏览器/Selection/Overlay 增强（用户可见）。
```