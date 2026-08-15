#include "compareworkspace_p.h"

#include <utility>

CompareWorkspace::CompareWorkspace(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // m_engine is default-constructed as a member; no assignment needed.
    m_syncZoomChk = new QCheckBox("同步缩放(&Z)", this);
    m_syncZoomChk->setChecked(true);
    m_syncDragChk = new QCheckBox("同步拖动(&D)", this);
    m_syncDragChk->setChecked(true);

    auto applySync = [this](bool) { update(); };
    connect(m_syncZoomChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncZoom = on;
                m_engine.setSyncMode(m_syncZoom, m_syncDrag);
                applySync(on);
            });
    connect(m_syncDragChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncDrag = on;
                m_engine.setSyncMode(m_syncZoom, m_syncDrag);
                applySync(on);
            });

    auto *toolbarContainer = new QWidget(this);
    auto *toolbarLayout = new QVBoxLayout(toolbarContainer);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(4);
    auto makeToolbar = [toolbarContainer](const char *name)
    {
        auto *bar = new QWidget(toolbarContainer);
        bar->setObjectName(name);
        auto *layout = new QHBoxLayout(bar);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        return std::pair{bar, layout};
    };
    auto [modeBar, modeLayout] = makeToolbar("compareModeToolbar");
    auto [viewBar, viewLayout] = makeToolbar("compareViewToolbar");
    auto [toolBar, toolLayout] = makeToolbar("compareToolToolbar");
    toolbarLayout->addWidget(modeBar);
    toolbarLayout->addWidget(viewBar);
    toolbarLayout->addWidget(toolBar);
    viewLayout->addWidget(m_syncZoomChk);
    viewLayout->addWidget(m_syncDragChk);

    // H5: "统一像素倍率" — align every pane at the same zoom so images of
    // different resolutions can be compared 1:1 (pixel-corresponding). Independent
    // of the sync-zoom toggle; when on, Fit uses the shared (minimum) scale for all.
    m_uniformScaleChk = new QCheckBox(tr("统一像素倍率"), this);
    m_uniformScaleChk->setChecked(false);
    m_uniformScaleChk->setToolTip(tr("所有窗格以相同倍率显示，不同分辨率图像也能 1:1 像素对齐"));
    connect(m_uniformScaleChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_uniformScale = on;
                fitAll();
                update();
            });
    viewLayout->addWidget(m_uniformScaleChk);

    // M14-3: blink (flicker) compare — rapid toggle between base and target.
    // Click the button (or press B) to start/stop rapid blinking.
    m_blinkChk = new QCheckBox("闪烁对比(&B)", this);
    m_blinkChk->setToolTip(tr("点击开始/停止快速闪烁切换（快捷键: B）"));
    connect(m_blinkChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    startBlink(150); // fast flicker
                else
                    stopBlink();
            });
    modeLayout->addWidget(m_blinkChk);

    // P0-4: split / swipe compare for exactly two images.
    m_splitChk = new QCheckBox("左右分割(&S)", this);
    m_splitChk->setEnabled(false);
    // M24 (B#8): a disabled control must say why it is unavailable.
    m_splitChk->setToolTip(tr("仅 2 张图片时可用：左右并排对比（快捷键: S）"));
    connect(m_splitChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    exclusiveMode(m_splitChk);
                updateCanvasModeVisibility();
            });
    modeLayout->addWidget(m_splitChk);

    m_swipeChk = new QCheckBox("滑动对比(&W)", this);
    m_swipeChk->setEnabled(false);
    m_swipeChk->setToolTip(tr("仅 2 张图片时可用：滑动分割线对比（快捷键: W）"));
    connect(m_swipeChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    exclusiveMode(m_swipeChk);
                updateCanvasModeVisibility();
            });
    modeLayout->addWidget(m_swipeChk);

    // A-4.1: Overlay compare mode — semi-transparent blend of the two images.
    m_overlayChk = new QCheckBox("叠加对比(&O)", this);
    m_overlayChk->setEnabled(false);
    m_overlayChk->setToolTip(tr("仅 2 张图片时可用：半透明叠加对比（快捷键: O）"));
    connect(m_overlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    exclusiveMode(m_overlayChk);
                updateCanvasModeVisibility();
                if (m_overlayAlphaSlider)
                    m_overlayAlphaSlider->setEnabled(on);
            });
    modeLayout->addWidget(m_overlayChk);

    // A-4.1: overlay opacity slider (0–100%).
    m_overlayAlphaSlider = new QSlider(Qt::Horizontal, this);
    m_overlayAlphaSlider->setRange(0, 100);
    m_overlayAlphaSlider->setValue(m_overlayAlpha);
    m_overlayAlphaSlider->setMaximumWidth(80);
    m_overlayAlphaSlider->setEnabled(false);
    m_overlayAlphaSlider->setToolTip(tr("叠加不透明度（上层图片）"));
    connect(m_overlayAlphaSlider, &QSlider::valueChanged, this,
            [this](int v)
            {
                m_overlayAlpha = v;
                if (m_overlayAlphaLabel)
                    m_overlayAlphaLabel->setText(QString("%1%").arg(v));
                update();
            });
    modeLayout->addWidget(m_overlayAlphaSlider);
    m_overlayAlphaLabel = new QLabel(QString("%1%").arg(m_overlayAlpha), this);
    m_overlayAlphaLabel->setMinimumWidth(28);
    modeLayout->addWidget(m_overlayAlphaLabel);

    // M23: checkerboard compare mode (棋盘格) — alternating blocks of A/B.
    buildCheckerboardControls(modeLayout);

    // A-4.5: continuous compare — walk consecutive pairs without reopening.
    m_prevPairBtn = new QPushButton("◀ 上一对", this);
    m_prevPairBtn->setToolTip(tr("比较上一对图片 (PageUp)"));
    m_prevPairBtn->setEnabled(false);
    connect(m_prevPairBtn, &QPushButton::clicked, this, &CompareWorkspace::prevPair);
    modeLayout->addWidget(m_prevPairBtn);

    m_nextPairBtn = new QPushButton("下一对 ▶", this);
    m_nextPairBtn->setToolTip(tr("比较下一对图片 (PageDown)"));
    m_nextPairBtn->setEnabled(false);
    connect(m_nextPairBtn, &QPushButton::clicked, this, &CompareWorkspace::nextPair);
    modeLayout->addWidget(m_nextPairBtn);
    modeLayout->addStretch(1);

    // M15: threshold slider for difference heatmap (0-255).
    auto *thresholdLabel = new QLabel("阈值:", this);
    toolLayout->addWidget(thresholdLabel);
    m_thresholdSlider = new QSlider(Qt::Horizontal, this);
    m_thresholdSlider->setObjectName("diffThresholdSlider");
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(0);
    m_thresholdSlider->setMaximumWidth(120);
    m_thresholdSlider->setToolTip("差异阈值: 低于此值的像素将被隐藏");
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                m_thresholdValue = static_cast<uint8_t>(value);
                if (!m_thresholdSlider->isSliderDown())
                    refreshAllDiffOverlays();
            });
    connect(m_thresholdSlider, &QSlider::sliderReleased, this,
            &CompareWorkspace::refreshAllDiffOverlays);
    toolLayout->addWidget(m_thresholdSlider);
    m_thresholdLabel = new QLabel("0", this);
    m_thresholdLabel->setObjectName("diffThresholdValueLabel");
    m_thresholdLabel->setMinimumWidth(24);
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this](int value) { m_thresholdLabel->setText(QString::number(value)); });
    toolLayout->addWidget(m_thresholdLabel);

    // A-4.6: Diff highlight mode (red diffs / gray similar).
    m_diffOverlayChk = new QCheckBox(tr("显示差异"), this);
    m_diffOverlayChk->setObjectName("diffOverlayToggle");
    m_diffOverlayChk->setChecked(false);
    m_diffOverlayChk->setToolTip(tr("显式叠加差异热力图；普通 Compare 始终显示原图"));
    connect(m_diffOverlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_diffOverlayVisible = on;
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(m_diffOverlayChk);

    m_diffHighlightChk = new QCheckBox(tr("高亮差异"), this);
    m_diffHighlightChk->setToolTip(tr("差异区域红色高亮，相似区域灰度显示"));
    connect(m_diffHighlightChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_diffHighlight = on;
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(m_diffHighlightChk);

    // A-4.3: Pixel Link — mark corresponding points across cells.
    m_pixelLinkChk = new QCheckBox(tr("像素连线"), this);
    m_pixelLinkChk->setToolTip(tr("开启后点击图片添加标记点，显示各图 RGB 与差值"));
    connect(m_pixelLinkChk, &QCheckBox::toggled, this, &CompareWorkspace::onPixelLinkToggled);
    toolLayout->addWidget(m_pixelLinkChk);
    m_clearLinksBtn = new QPushButton(tr("清除标记"), this);
    m_clearLinksBtn->setEnabled(false);
    m_clearLinksBtn->setToolTip(tr("清除全部像素连线标记"));
    connect(m_clearLinksBtn, &QPushButton::clicked, this, &CompareWorkspace::clearLinkPoints);
    toolLayout->addWidget(m_clearLinksBtn);
    m_linkInfoLabel = new QLabel(tr("标记: 0"), this);
    m_linkInfoLabel->setMinimumWidth(48);
    m_linkInfoLabel->setStyleSheet("color:#aaa;");
    toolLayout->addWidget(m_linkInfoLabel);

    // P0 #③: explicit multi-layout selector (auto / single / 2-4 columns / row).
    auto *layoutLabel = new QLabel(tr("布局:"), this);
    viewLayout->addWidget(layoutLabel);
    m_layoutCombo = new QComboBox(this);
    m_layoutCombo->addItems(
        {tr("自动"), tr("单列"), tr("2 列"), tr("3 列"), tr("4 列"), tr("一行"), tr("自定义")});
    m_layoutCombo->setCurrentIndex(0);
    connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CompareWorkspace::onLayoutChanged);
    viewLayout->addWidget(m_layoutCombo);

    // A-4.2: custom M×N grid spin boxes (active when layout = 自定义).
    // A-4.2: custom M×N grid spin boxes (active when layout = 自定义).
    // The row count is informational only — the engine packs by columns, so the
    // 行 spin box is disabled and labeled as auto-derived to avoid a misleading UI.
    auto *rowsLabel = new QLabel(tr("行"), this);
    rowsLabel->setToolTip(tr("行数由列数自动推导（按列填充，无法单独设置）"));
    viewLayout->addWidget(rowsLabel);
    m_gridRowsSpin = new QSpinBox(this);
    m_gridRowsSpin->setRange(1, 8);
    m_gridRowsSpin->setValue(2);
    m_gridRowsSpin->setEnabled(false);
    m_gridRowsSpin->setMaximumWidth(48);
    m_gridRowsSpin->setToolTip(tr("行数由列数自动推导（按列填充，无法单独设置）"));
    connect(m_gridRowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onCustomGridChanged);
    viewLayout->addWidget(m_gridRowsSpin);
    viewLayout->addWidget(new QLabel(tr("列"), this));
    m_gridColsSpin = new QSpinBox(this);
    m_gridColsSpin->setRange(1, 8);
    m_gridColsSpin->setValue(2);
    m_gridColsSpin->setEnabled(false);
    m_gridColsSpin->setMaximumWidth(48);
    connect(m_gridColsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onCustomGridChanged);
    viewLayout->addWidget(m_gridColsSpin);

    // P0 #③: inspector + histogram side panel toggle.
    m_sideChk = new QCheckBox(tr("检视面板"), this);
    m_sideChk->setObjectName("analysisPanelToggle");
    m_sideChk->setChecked(false);
    connect(m_sideChk, &QCheckBox::toggled, this, &CompareWorkspace::onSideToggled);
    viewLayout->addWidget(m_sideChk);

    // M16.1: cursor-sync crosshair (n/n). When on, hovering any cell draws a
    // crosshair at the same image-space point across all compared cells, and the
    // inspector samples every cell at that point.
    m_crosshairChk = new QCheckBox(tr("同步准星"), this);
    m_crosshairChk->setChecked(false);
    viewLayout->addWidget(m_crosshairChk);

    // M16.1: focus-lock / reference pin (n/1). Locks a cell as the comparison
    // reference; diff overlays and inspector deltas use it as the base.
    m_focusBtn = new QPushButton(tr("锁定基准"), this);
    m_focusBtn->setCheckable(true);
    connect(m_focusBtn, &QPushButton::toggled, this,
            [this](bool on)
            {
                // Lock the focus on the cell under the cursor (fall back to cell 0 when
                // none is hovered). Toggling off clears the lock.
                const int idx = (on && m_hoverIdx >= 0) ? m_hoverIdx : (on ? 0 : m_focusIndex);
                onFocusRequested(on ? idx : -1);
            });
    viewLayout->addWidget(m_focusBtn);
    m_focusLabel = new QLabel(tr("基准: —"), this);
    m_focusLabel->setMinimumWidth(60);
    viewLayout->addWidget(m_focusLabel);

    viewLayout->addStretch(1);

    m_grid = new QWidget;
    m_layout = new QGridLayout(m_grid);
    m_layout->setSpacing(2);
    m_layout->setContentsMargins(0, 0, 0, 0);

    // M34: the grid scrolls inside "compareGridPage"; a stacked layout swaps it
    // with the dedicated "compareCanvas" widget in split/swipe/overlay/checker
    // modes. Construction lives in the render TU (ADR 014).
    auto *pages = buildCanvasPage();

    // P0 #③: right-side inspector/histogram panel (collapsible).
    m_sidePanel = new QWidget(this);
    m_sidePanel->setFixedWidth(280);
    auto *sideLay = new QVBoxLayout(m_sidePanel);
    sideLay->setContentsMargins(4, 4, 4, 4);
    sideLay->setSpacing(6);

    // M23: inspector (multi color-space) + histogram (channels/log/ROI) +
    // metrics — built in compareworkspace_analysis.cpp (ADR 014 TU split).
    buildAnalysisPanel(sideLay);

    // M16.2: per-cell image edit panel (collapsible section inside side panel)
    buildEditPanel(sideLay);

    m_sidePanel->setVisible(false);

    // M16.6: layout presets save/load + swap panes (sync bar right side)
    m_savePresetBtn = new QPushButton(tr("存储布局"), this);
    m_savePresetBtn->setToolTip(tr("将当前布局存储为预设"));
    connect(m_savePresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onSavePreset);
    toolLayout->addWidget(m_savePresetBtn);

    m_loadPresetBtn = new QPushButton(tr("读取布局"), this);
    m_loadPresetBtn->setToolTip(tr("从预设文件中读取布局"));
    connect(m_loadPresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onLoadPreset);
    toolLayout->addWidget(m_loadPresetBtn);

    m_swapBtn = new QPushButton(tr("交换窗格"), this);
    m_swapBtn->setToolTip(tr("交换选中的两个窗格"));
    m_swapBtn->setEnabled(false);
    connect(m_swapBtn, &QPushButton::clicked, this, &CompareWorkspace::onSwapPanes);
    toolLayout->addWidget(m_swapBtn);

    // P1 #④: Analyze & export buttons in the compare toolbar.
    m_analyzeBtn = new QPushButton(tr("分析"), this);
    m_analyzeBtn->setToolTip(tr("在分析面板中打开当前焦点图像"));
    connect(m_analyzeBtn, &QPushButton::clicked, this, &CompareWorkspace::analyzeCurrent);
    toolLayout->addWidget(m_analyzeBtn);

    m_exportReportBtn = new QPushButton(tr("导出报告"), this);
    m_exportReportBtn->setToolTip(tr("将对比结果导出为 HTML/Markdown/JSON 报告"));
    connect(m_exportReportBtn, &QPushButton::clicked, this,
            &CompareWorkspace::exportReportRequested);
    toolLayout->addWidget(m_exportReportBtn);
    toolLayout->addStretch(1);

    auto *leftLay = new QVBoxLayout;
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(4);
    leftLay->addWidget(toolbarContainer);
    leftLay->addLayout(pages, 1);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    root->addLayout(leftLay, 1);
    root->addWidget(m_sidePanel);
}

CompareWorkspace::~CompareWorkspace()
{
    // A batch completion is bookkeeping, not cancellation. Every request is
    // accounted exactly once when a batch is superseded so a cancelled queued
    // decode cannot leave `remaining` permanently non-zero. The generation
    // bump below remains the final stale-delivery guard for callbacks that were
    // already queued on qApp.
    ++m_loadGen;
    if (m_loadBatch)
    {
        cancelLoadBatch(m_loadBatch);
        m_loadBatch.reset();
    }
    m_loadInFlight = false;

    // Invalidate the in-flight async diff batch. The worker marshals its
    // result via qApp and re-checks the generation, which we bump here, so a
    // completion that is already queued cannot paint into a destroyed widget
    // (the QPointer guard covers the destroyed-owner case).
    if (m_diffTask)
        TaskScheduler::cancel(m_diffTask);
    m_diffTask.reset();
    ++m_diffGen;

    // Same lifetime handling for the async pane-materialization batch.
    if (m_displayTask)
        TaskScheduler::cancel(m_displayTask);
    m_displayTask.reset();
    ++m_displayGen;

    // Same lifetime handling for the async pane-histogram batch.
    if (m_histTask)
        TaskScheduler::cancel(m_histTask);
    m_histTask.reset();
    ++m_histGen;
}

void CompareWorkspace::setSelectionModel(SelectionModel *sel)
{
    m_selection = sel;
}

void CompareWorkspace::exclusiveMode(QCheckBox *keepOn)
{
    // Split / Swipe / Overlay / Checkerboard are mutually exclusive (2 images).
    auto uncheck = [keepOn](QCheckBox *c)
    {
        if (c && c != keepOn && c->isChecked())
            c->setChecked(false);
    };
    uncheck(m_splitChk);
    uncheck(m_swipeChk);
    uncheck(m_overlayChk);
    uncheck(m_checkerChk);
}

void CompareWorkspace::setImages(const QStringList &paths)
{
    // M28 P1-01: Compare loads are ASYNC. Decoding happens on the DecodePool,
    // never on the UI thread: setImages() returns immediately, and the frames
    // are applied by finishLoad() on the UI thread when every request in this
    // batch completes. A newer setImages() supersedes an in-flight batch via
    // the generation counter, so stale completions can never overwrite the
    // current compare set (A -> B -> A is safe).
    const uint64_t gen = ++m_loadGen;
    if (m_loadBatch)
    {
        cancelLoadBatch(m_loadBatch);
        m_loadBatch.reset();
    }
    m_loadInFlight = true;
    // A newer load supersedes a session that was pending on the older one.
    m_pendingSession.reset();

    std::vector<std::string> stdPaths;
    stdPaths.reserve(paths.size());
    for (const QString &p : paths)
        stdPaths.push_back(p.toStdString());
    const int requested = static_cast<int>(paths.size());
    if (requested == 0)
    {
        finishLoad({}, 0);
        return;
    }

    auto batch = std::make_shared<LoadBatch>();
    batch->generation = gen;
    batch->frames = std::make_shared<std::vector<std::shared_ptr<ImageFrame>>>(requested);
    batch->remaining = std::make_shared<std::atomic<int>>(requested);
    batch->failed = std::make_shared<std::atomic<int>>(0);
    batch->requests.reserve(static_cast<size_t>(requested));
    for (int i = 0; i < requested; ++i)
        batch->requests.push_back(std::make_unique<LoadRequest>());
    m_loadBatch = batch;
    auto self = std::make_shared<QPointer<CompareWorkspace>>(this);
    // Histograms are computed by the async Compare histogram batch on the
    // AnalysisPool; this decode path stays histogram-free so the UI thread
    // never pays for a full scan.
    const ImageLoadOptions opts{true, false, 256};

    for (int i = 0; i < requested; ++i)
    {
        auto &request = *batch->requests[static_cast<size_t>(i)];
        auto handle = ImageRepository::instance().loadAsyncCancellable(
            stdPaths[i],
            [self, batch, i](const ImageRepository::Result &res)
            {
                if (!CompareWorkspace::accountLoadRequest(batch, static_cast<size_t>(i), &res) ||
                    !qApp)
                    return;
                QMetaObject::invokeMethod(
                    qApp,
                    [self, batch]()
                    {
                        CompareWorkspace *ws = self->data();
                        if (!ws || batch->generation != ws->m_loadGen)
                            return;
                        ws->finishLoad(*batch->frames,
                                       batch->failed->load(std::memory_order_relaxed));
                    },
                    Qt::QueuedConnection);
            },
            opts);
        {
            std::lock_guard<std::mutex> lk(batch->handlesMutex);
            request.handle = std::move(handle);
        }
    }
}

bool CompareWorkspace::accountLoadRequest(const std::shared_ptr<LoadBatch> &batch, size_t index,
                                          const ImageRepository::Result *result)
{
    if (!batch || index >= batch->requests.size())
        return false;
    auto &request = *batch->requests[index];
    if (request.accounted.exchange(true, std::memory_order_acq_rel))
        return false;

    if (result && result->success() && result->frame)
        (*batch->frames)[index] = result->frame;
    else
        batch->failed->fetch_add(1, std::memory_order_relaxed);

    return batch->remaining->fetch_sub(1, std::memory_order_acq_rel) == 1;
}

void CompareWorkspace::cancelLoadBatch(const std::shared_ptr<LoadBatch> &batch)
{
    if (!batch)
        return;
    for (size_t i = 0; i < batch->requests.size(); ++i)
    {
        ImageRepository::AsyncRequestHandle handle;
        {
            std::lock_guard<std::mutex> lk(batch->handlesMutex);
            handle = std::move(batch->requests[i]->handle);
        }
        ImageRepository::instance().cancelAsync(handle);
        // ImageRepository deliberately suppresses callbacks after cancel. Do
        // the batch's exactly-once accounting locally so cancellation cannot
        // strand the terminal completion on a queued request.
        accountLoadRequest(batch, i, nullptr);
    }
}

void CompareWorkspace::finishLoad(const std::vector<std::shared_ptr<ImageFrame>> &frames,
                                  int failedCount)
{
    Q_UNUSED(failedCount);
    m_loadInFlight = false;
    m_loadBatch.reset();
    m_engine.setFrames(frames);

    // M24 (B#7): failed loads are dropped by the engine — tell the user why
    // the grid has fewer cells than requested instead of silently shrinking.
    const int requested = static_cast<int>(frames.size());
    const int loaded = m_engine.imageCount();
    if (loaded < requested)
    {
        emit loadWarning(
            tr("%1 张图片无法加载（文件损坏、缺失或不支持），已保留 %2 张可用的进行对比。")
                .arg(requested - loaded)
                .arg(loaded));
    }
    // A-4: loading a fresh comparison set should not inherit adjustments from
    // the previous session; applySession will repopulate persisted values.
    m_cellAdjusts.clear();
    rebuildCells();
    schedulePostLayoutFit();
    update();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();

    // P0-4: split / swipe only make sense for exactly two images.
    const bool two = m_engine.imageCount() == 2;
    if (m_splitChk)
    {
        if (!two)
            m_splitChk->setChecked(false);
        m_splitChk->setEnabled(two);
    }
    if (m_swipeChk)
    {
        if (!two)
            m_swipeChk->setChecked(false);
        m_swipeChk->setEnabled(two);
    }
    // A-4.1: overlay mode also only for two images.
    if (m_overlayChk)
    {
        if (!two)
            m_overlayChk->setChecked(false);
        m_overlayChk->setEnabled(two);
    }
    // M23: checkerboard mode also only for two images.
    if (m_checkerChk)
    {
        if (!two)
            m_checkerChk->setChecked(false);
        m_checkerChk->setEnabled(two);
    }
    if (m_grid && !two)
        m_grid->setVisible(true);
    updateCanvasModeVisibility();
    setFocus();
    // P0-2: publish the compare set + reference to the app-wide SelectionModel so
    // Metadata/Analysis/Export stay in sync with what is being compared.
    if (m_selection)
    {
        m_selection->setCompared(comparedImages());
        m_selection->setFocused(focusImagePath());
    }

    // M28 P1-01: a session that was applied while the load was in flight is
    // replayed once the frames exist (openCompare -> setImages -> applySession).
    if (m_pendingSession)
    {
        const auto session = *m_pendingSession;
        m_pendingSession.reset();
        QPointer<CompareWorkspace> guard(this);
        // schedulePostLayoutFit() was queued first. Replay persisted transforms
        // after that settled-geometry Fit so it cannot reset the saved ratio.
        QTimer::singleShot(0, this,
                           [guard, session]()
                           {
                               if (guard)
                                   guard->applySession(session);
                           });
    }
}

QStringList CompareWorkspace::comparedImages() const
{
    QStringList out;
    const int n = m_engine.imageCount();
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        if (img)
            out.push_back(QString::fromStdString(img->metadata().filePath));
    }
    return out;
}

bool CompareWorkspace::isSyncEnabled() const
{
    return m_syncZoom || m_syncDrag;
}

void CompareWorkspace::setSyncEnabled(bool on)
{
    m_syncZoom = on;
    m_syncDrag = on;
    m_syncZoomChk->setChecked(on);
    m_syncDragChk->setChecked(on);
    m_engine.setSyncMode(on, on);
}

// rebuildCells() lives in compareworkspace_render.cpp (ADR 014 TU split).

void CompareWorkspace::onLayoutChanged()
{
    if (!m_layoutCombo)
        return;
    const int idx = m_layoutCombo->currentIndex();
    const bool custom = (idx == 6); // 自定义 M×N
    if (m_gridRowsSpin)
        m_gridRowsSpin->setEnabled(custom);
    if (m_gridColsSpin)
        m_gridColsSpin->setEnabled(custom);

    int cols = 0;
    switch (idx)
    {
    case 1:
        cols = 1;
        break; // 单列
    case 2:
        cols = 2;
        break; // 2 列
    case 3:
        cols = 3;
        break; // 3 列
    case 4:
        cols = 4;
        break; // 4 列
    case 5:
        cols = m_engine.imageCount();
        break; // 一行
    case 6:
        // A-4.2: custom grid — columns from spin; rows derived by engine.
        cols = m_gridColsSpin ? m_gridColsSpin->value() : 2;
        break;
    default:
        cols = 0;
        break; // 自动
    }
    m_engine.setColumns(cols);
    rebuildCells();
    schedulePostLayoutFit();
    refreshLinkMarkers();
    update();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();
}

void CompareWorkspace::onCustomGridChanged()
{
    if (!m_layoutCombo || m_layoutCombo->currentIndex() != 6)
        return;
    // Re-apply custom columns; rows are informational (engine packs by cols).
    const int cols = m_gridColsSpin ? m_gridColsSpin->value() : 2;
    m_engine.setColumns(cols);
    rebuildCells();
    schedulePostLayoutFit();
    refreshLinkMarkers();
    update();
}

void CompareWorkspace::onSideToggled(bool on)
{
    if (!m_sidePanel)
        return;
    m_sidePanel->setVisible(on);
    if (on)
    {
        refreshHistograms();
        refreshAllDiffOverlays();
    }
    update();
}

void CompareWorkspace::onCrosshairMoved(RawImageView *view, const QPointF &pos)
{
    if (!view)
        return;
    // Track the hovered cell so the focus-lock button knows which cell to pin.
    m_hoverIdx = view->cellIndex();

    if (!m_crosshairChk || !m_crosshairChk->isChecked())
        return;

    const bool valid = pos.x() >= 0.0 && pos.y() >= 0.0;
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        if (valid)
            m_cellViews[i]->setCrosshair(pos);
        else
            m_cellViews[i]->clearCrosshair();
    }
    // Sample every cell at the synced image-space point. M30: route through the
    // coalescer so this crosshair path cannot double-render the inspector that
    // the same hover's pixelInfo signal already requested.
    if (valid && m_sidePanel && m_sidePanel->isVisible())
        requestInspectorUpdate(qRound(pos.x()), qRound(pos.y()));
}

void CompareWorkspace::onFocusRequested(int cellIndex)
{
    // Toggle the locked reference: re-clicking the focused cell clears it.
    const int newFocus = (cellIndex == m_focusIndex) ? -1 : cellIndex;
    const bool locking = newFocus >= 0;

    m_focusIndex = newFocus;
    if (m_focusBtn)
    {
        // Sync the button without emitting toggled: the button's toggled handler
        // re-enters onFocusRequested and would immediately clear/replace the
        // requested focus (visible when a pane double-click drives this path).
        const QSignalBlocker blocker(m_focusBtn);
        m_focusBtn->setChecked(locking);
    }
    if (m_focusLabel)
        m_focusLabel->setText(locking ? tr("基准: %1").arg(newFocus + 1) : tr("基准: —"));

    // P0: Write the reference cell's image back to the global SelectionModel so
    // the rest of the app (MetadataPanel/AnalysisPanel/Export/etc.) stays in sync.
    if (locking && m_selection && newFocus >= 0)
    {
        const int poolIdx = m_pairIndex + newFocus;
        if (poolIdx >= 0 && poolIdx < m_imagePool.size())
            m_selection->setCurrentImage(m_imagePool[poolIdx]);
    }
    // P0-2: publish the locked reference to the app-wide SelectionModel.
    if (m_selection)
        m_selection->setFocused(locking ? focusImagePath() : QString());

    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        m_cellViews[i]->setFocused(i == m_focusIndex);
    }

    // Recompute all diff overlays against the new base (async batch, latest-wins).
    refreshAllDiffOverlays();
    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0)
        requestInspectorUpdate(m_lastInspectX, m_lastInspectY);
    update();
}

// ─── M16.2: Per-cell image adjustments ───────────────────────────────────────
