# MViewer 开发状态 — 最终版(全部 engine 完成)

## 完成度

- ✅ v0.1 浏览器原型(MSVC 构建通过)
- ✅ M2 Image Core + Task Scheduler
- ✅ M3 Compare Engine(多图同步比较+差异图+闪烁)
- ✅ M4 Analysis Engine(统计/直方图/PSNR/SSIM/热力图)
- ✅ M5 Render Engine(高质量缩放/Difference Overlay)
- ✅ UI 接入:MainWindow 3 栏布局 + 菜单 + 框选 + 导航 + 比较模式
- ✅ MSVC 工具链完整可用
- ✅ 视觉自测通过(3 栏布局像素分析确认)

## 架构(目标达成)

```
MainWindow (QSplitter 3栏)
├── 左: DirectoryTree(上) + PreviewPanel(下)
├── 中: ThumbnailPanel(画廊 + 排序栏)
└── 右: AnalysisPanel(直方图+统计+框选)

CompareWorkspace (QDialog 弹出,内含 CompareWorkspace)
ImageViewer (独立窗口,双击打开: 缩放/平移/框选/直方图/导航)

mviewer_core (静态库)
├── FileSystem → Decoder → ImageObject → ImageCache → Scheduler
├── CompareEngine (同步变换/差异图/闪烁)
├── AnalysisEngine (统计/PSNR/SSIM/热力图)
└── RenderEngine (缩放/Overlay)

mviewer_ui (静态库)
├── MainWindow / ImageViewer / ThumbnailPanel / PreviewPanel
├── AnalysisPanel / CompareWorkspace / DirectoryTree / ThumbnailCache
```

## 关键接口

- `ImageViewer::regionStats(QString)` → `AnalysisPanel::setRegionStats`
- `ImageViewer::requestPrev/requestNext` → `MainWindow::navigate`
- `ThumbnailPanel::itemClicked` → `AnalysisPanel::setImage` + `PreviewPanel::setImage`
- `ThumbnailPanel::itemDoubleClicked` → `ImageViewer::setImage` + show
- `MainWindow::openCompare` → QDialog + CompareWorkspace

## 视觉自测(2026-07-12)

3 栏布局像素分析:

- 左栏 x=0-360: 目录树+预览(亮度 165) ✅
- 中栏 x=360-1280: 画廊(亮度 253) ✅
- 右栏 x=1280-1600: AnalysisPanel(黑底,亮度 12) ✅

## 核心测试(全部绿)

```
LAYOUT_OK        (2=2x1, 3=3x1, 4=2x2, 7=4x2)
CELLMATH_OK      (网格几何计算)
ENG_COUNT_OK     (2 图加载)
SYNC_OK          (同步变换)
BLINK_OK         (闪烁比较)
DIFF_OK          (差异图 4080x3072)
REMOVE_OK / CLEAR_OK
ALL_COMPARE_OK=0
```

## M3 Phase-1 — Core Image Pipeline (2026-07-15)

评审意见落地:停止基础设施工作,聚焦生产图片管线。架构层(ImageRepository / ImageFrame /
5 级 LRU 缓存 / AnalyzerRegistry / Selection / CompareEngine 同步控制器)已先于评审存在,
本次补齐真实缺口:

- ✅ TIFF (`.tif`/`.tiff`) 加入 Decoder/FileSystem/UI 全部格式列表(解码依赖 qtiff 插件,
  见 CHANGELOG 的 TIFF codec note;测试在此缺失时自动 SKIP)。
- ✅ `ImageRepository::load` 现在写入内存 Viewer/FullImage LRU,相邻图切换首解后即时命中。
- ✅ `ImageViewer` 改为完全经 `ImageRepository` 加载(移除其私有的 Decoder/CacheManager
  解码路径与独立 QPixmap LRU),直方图复用 `ImageFrame` 缓存,不再二次解码。
- ✅ Pixel Inspector:`ImageViewer` 鼠标移动时从 `ImageFrame` 像素直接读取 RGB 并发出
  `pixelInfo(x,y,r,g,b,valid)`,接入主窗口状态栏。
- ✅ 新增 `m3pipeline_tests` 验收套件(仓库→帧 / 4 格式解码 / Viewer LRU 命中 /
  像素 inspector 读取),4 套测试全绿。
- ✅ **M3 Phase-2 (续3)**: Compare 网格同步框选落地。RawImageView 新增 ROI 选择(右键拖拽绘制,
  `Selection` 图像坐标,随 fit/pan 变换叠加红框,鼠标 hover 仍发 pixelInfo),`selectionChanged`
  信号经 CompareWorkspace::applySelectionToAll 写入 SelectionController 并镜像到每个 cell 的
  RawImageView 与 ImageFrame(`Selection` 跨单元格同步,产品灵魂)。新增 m3pipeline_tests
  同步框选验收(SelectionController 存共享 ROI + 每帧镜像);构建+4 套测试全绿。
- ✅ **M4 (analyzer registry ↔ UI)**: AnalysisPanel 分析器下拉框改为由
  `AnalyzerRegistry::availableAnalyzers()` 填充(histogram/noise/entropy/psnr/sharpness/
  ssim/rgbmean),切换分析器与每次 ROI 分析均经 `AnalyzerRegistry::create(id)->
  analyzeRegion(frame, selection)`(统一 `IAnalyzer` 接口,`Selection` 为唯一 ROI 类型)。
  每个内置分析器新增 `resultText()` 通用结果文本,面板无需定制即可渲染任意注册分析器;
  `test_m3m4m5` 现断言所有内置分析器均可经 registry 创建且产出非空结果。构建+4 套测试全绿。
- ✅ **M3 收尾(死代码清理)**: 删除 `CompareWorkspace::paintEvent` 中向 off-screen `canvas`
  QPixmap 合成(基图/差异/框选/直方图 4 趟 RenderPass)却从未 blit 的死代码——`RawImageView`
  自身已绘制图像与选区。`RawImageView` 自行渲染,工作区仅推送同步变换(scale+offset)。连带移除
  无用 `m_stats` 成员、`drawCellHistogram` 声明、以及 compare 层不再需要的 `RenderEngine`/`QPainter`
  include。行为不变,Compare 单元格仍接收同步变换。构建+4 套测试全绿。
- ✅ **M4 (analyzer registry ↔ UI) 收口**: 全部 4 条验收标准已满足并勾选 roadmap。
  (AC1) CompareEngine::layout 支持 2-8 图 + test_compare 覆盖;(AC2) `testAnalyzerRegistryConsistency`
  断言 registry 分析与 `AnalysisEngine::computeStatsROI` 同区域参考值一致(rgbmean rMean /
  histogram lumMean 容差<1.0);(AC3) 同一 Selection 左右半 rMean 差>100,证明 ROI 真生效;
  (AC4) `test_plugin_loader/manager` 覆盖插件加载。新增 `testAnalyzerRegistryConsistency`
  测试,0 警告,4 套测试全绿。roadmap M4 状态 → ✅ Done。
- ✅ **M4 补完项: 差异热力图叠加(compare 模式)**: `CompareWorkspace::rebuildCells` 现在在
  2+ 图模式为每个 cell 经核心层(`CompareEngine::differenceMap(i)` →
  `DifferenceEngine::heatMap`)生成差异伪彩热力图,交给 `RawImageView::setOverlay` 以与基图
  相同的 fit/pan 变换叠加(随缩放/平移同步)。QWidget 层不解码,只渲染核心层产出的 QImage——
  修复了之前"合成 off-screen canvas 却从未 blit"的死代码对应的 M4 交付项。新增
  `testCompareDiffOverlay`(163 项全绿,0 警告)。roadmap M4 第4条交付项 → 完成。
- ✅ **M5 (Scale & Performance) 部分验收**: 新增 `testCacheManagerM5` 与 `testPredictivePreload`。
  (1) 5 级缓存层次验证——SQLite 磁盘缓存经内存清空后仍可字节级还原像素(证明磁盘层是持久层/
  重启存活),`CacheManager::levelStats` 报告逐层命中/未命中与命中率(实测 0.5);(2) 预测预取
  (`ImageRepository::prefetch`)将相邻图从磁盘层预热进内存 FullImage LRU,缓存预热后切换相邻图即时。
  实测 181 项全绿,0 警告。roadmap M5: 2/4 验收项已勾选(剩 1000 图非阻塞 + benchmark CI 门未测)。

## 开发角色分工(2026-07-15 起)

- **Hermes = 指挥官 / Reviewer**:产品方向、里程碑规划(roadmap M3/M4/M5 + 验收标准)、架构冻结、
  代码评审、构建+测试验证、commit/push。实现委托给 OpenCode,合入前审 diff,`build.ps1 Test` 不绿
  不允许合入。
- **OpenCode = 代码写手**:按 Hermes 委托的具体改动实现,对照当前 ADR 与冻结架构。写代码、本地构建
  确认编译,但**不** commit/push,也**不**自行扩范围/改架构。
- 原则:roadmap 与 ADR 为事实来源;写手发现缺失能力需上报指挥官,不静默扩范围;无本地构建+测试绿
  不提交;基础设施/构建/CI 冻结,除非指挥官显式要求。

## 后续可选

- SQLite 磁盘缓存(千张图秒开)
- GPU 渲染(Direct2D / OpenGL)
- PSNR/SSIM 面板化显示
- 中文编码(GBK→UTF-8)统一
- 文件操作(重命名/删除到回收站)

## 已知约束(诚实记录)

- 关机/重启:agent 硬屏蔽,我无法操作
- 微信:无 API/权限,无法通知

## 工具链

| 工具 | 路径 |
| ------ | ------ |
| MSVC | `D:\msvc\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\cl.exe` |
| Qt msvc | `D:\QT\6.11.1\msvc2022_64` |
| Win SDK | `C:\Program Files (x86)\Windows Kits\10` (10.0.26100.0) |
| CMake | `D:\msvc\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` |
| 生成器 | Ninja |

**注意**:MSVC bin 必须放 PATH 最前,否则 cl.exe 加载错误 vcruntime140.dll 会静默崩溃。
