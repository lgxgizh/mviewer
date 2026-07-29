#include "compareworkspace_p.h"

CompareWorkspace::CompareWorkspace(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // m_engine is default-constructed as a member; no assignment needed.
    m_syncZoomChk = new QCheckBox("同步缩放(&Z)", this);
    m_syncZoomChk->setChecked(true);
    m_syncDragChk = new QCheckBox("同步拖动(&D)", this);
    m_syncDragChk->setChecked(true);

    auto applySync = [this](bool)
    {
        if (!m_syncZoom || !m_syncDrag)
        {
            // 关闭任一同步时,用当前 fit 结果初始化每张图的独立变换
            fitAll();
        }
        update();
    };
    connect(m_syncZoomChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncZoom = on;
                m_engine.setSyncEnabled(m_syncZoom && m_syncDrag);
                applySync(on);
            });
    connect(m_syncDragChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncDrag = on;
                m_engine.setSyncEnabled(m_syncZoom && m_syncDrag);
                applySync(on);
            });

    // Async diff result delivery. requestDiff() computes the diff on a worker
    // thread and publishes "CompareEngine.DiffResult" on the EventBus from that
    // thread. We hop to the UI thread before repainting (the engine pointer in
    // ctx identifies which CompareEngine produced it).
    m_diffSubId = EventBus::instance().subscribe("CompareEngine.DiffResult",
                                                 [this](void *ctx)
                                                 {
                                                     if (ctx != static_cast<void *>(&m_engine))
                                                         return;
                                                     // Repaint on the UI thread;
                                                     // refreshDiffOverlay() reads lastDiffImage().
                                                     QPointer<CompareWorkspace> guard(this);
                                                     QMetaObject::invokeMethod(
                                                         this,
                                                         [guard]()
                                                         {
                                                             if (!guard)
                                                                 return;
                                                             guard->refreshDiffOverlay();
                                                         },
                                                         Qt::QueuedConnection);
                                                 });

    auto *syncBar = new QWidget(this);
    auto *syncLayout = new QHBoxLayout(syncBar);
    syncLayout->setContentsMargins(0, 0, 0, 0);
    syncLayout->setSpacing(12);
    syncLayout->addWidget(m_syncZoomChk);
    syncLayout->addWidget(m_syncDragChk);

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
    syncLayout->addWidget(m_uniformScaleChk);

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
    syncLayout->addWidget(m_blinkChk);

    // P0-4: split / swipe compare for exactly two images.
    m_splitChk = new QCheckBox("左右分割(&S)", this);
    m_splitChk->setEnabled(false);
    connect(m_splitChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    exclusiveMode(m_splitChk);
                if (m_grid)
                    m_grid->setVisible(!anyCanvasCompareMode());
                update();
            });
    syncLayout->addWidget(m_splitChk);

    m_swipeChk = new QCheckBox("滑动对比(&W)", this);
    m_swipeChk->setEnabled(false);
    connect(m_swipeChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    exclusiveMode(m_swipeChk);
                if (m_grid)
                    m_grid->setVisible(!anyCanvasCompareMode());
                update();
            });
    syncLayout->addWidget(m_swipeChk);

    // A-4.1: Overlay compare mode — semi-transparent blend of the two images.
    m_overlayChk = new QCheckBox("叠加对比(&O)", this);
    m_overlayChk->setEnabled(false);
    connect(m_overlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    exclusiveMode(m_overlayChk);
                if (m_grid)
                    m_grid->setVisible(!anyCanvasCompareMode());
                if (m_overlayAlphaSlider)
                    m_overlayAlphaSlider->setEnabled(on);
                update();
            });
    syncLayout->addWidget(m_overlayChk);

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
    syncLayout->addWidget(m_overlayAlphaSlider);
    m_overlayAlphaLabel = new QLabel(QString("%1%").arg(m_overlayAlpha), this);
    m_overlayAlphaLabel->setMinimumWidth(28);
    syncLayout->addWidget(m_overlayAlphaLabel);

    // M23: checkerboard compare mode (棋盘格) — alternating blocks of A/B.
    buildCheckerboardControls(syncLayout);

    // A-4.5: continuous compare — walk consecutive pairs without reopening.
    m_prevPairBtn = new QPushButton("◀ 上一对", this);
    m_prevPairBtn->setToolTip(tr("比较上一对图片 (PageUp)"));
    m_prevPairBtn->setEnabled(false);
    connect(m_prevPairBtn, &QPushButton::clicked, this, &CompareWorkspace::prevPair);
    syncLayout->addWidget(m_prevPairBtn);

    m_nextPairBtn = new QPushButton("下一对 ▶", this);
    m_nextPairBtn->setToolTip(tr("比较下一对图片 (PageDown)"));
    m_nextPairBtn->setEnabled(false);
    connect(m_nextPairBtn, &QPushButton::clicked, this, &CompareWorkspace::nextPair);
    syncLayout->addWidget(m_nextPairBtn);

    // M15: threshold slider for difference heatmap (0-255).
    auto *thresholdLabel = new QLabel("阈值:", this);
    syncLayout->addWidget(thresholdLabel);
    m_thresholdSlider = new QSlider(Qt::Horizontal, this);
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(0);
    m_thresholdSlider->setMaximumWidth(120);
    m_thresholdSlider->setToolTip("差异阈值: 低于此值的像素将被隐藏");
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                m_thresholdValue = static_cast<uint8_t>(value);
                refreshDiffOverlay();
            });
    syncLayout->addWidget(m_thresholdSlider);
    m_thresholdLabel = new QLabel("0", this);
    m_thresholdLabel->setMinimumWidth(24);
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this](int value) { m_thresholdLabel->setText(QString::number(value)); });
    syncLayout->addWidget(m_thresholdLabel);

    // A-4.6: Diff highlight mode (red diffs / gray similar).
    m_diffHighlightChk = new QCheckBox(tr("高亮差异"), this);
    m_diffHighlightChk->setToolTip(tr("差异区域红色高亮，相似区域灰度显示"));
    connect(m_diffHighlightChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_diffHighlight = on;
                refreshDiffOverlay();
            });
    syncLayout->addWidget(m_diffHighlightChk);

    // A-4.3: Pixel Link — mark corresponding points across cells.
    m_pixelLinkChk = new QCheckBox(tr("像素连线"), this);
    m_pixelLinkChk->setToolTip(tr("开启后点击图片添加标记点，显示各图 RGB 与差值"));
    connect(m_pixelLinkChk, &QCheckBox::toggled, this, &CompareWorkspace::onPixelLinkToggled);
    syncLayout->addWidget(m_pixelLinkChk);
    m_clearLinksBtn = new QPushButton(tr("清除标记"), this);
    m_clearLinksBtn->setEnabled(false);
    m_clearLinksBtn->setToolTip(tr("清除全部像素连线标记"));
    connect(m_clearLinksBtn, &QPushButton::clicked, this, &CompareWorkspace::clearLinkPoints);
    syncLayout->addWidget(m_clearLinksBtn);
    m_linkInfoLabel = new QLabel(tr("标记: 0"), this);
    m_linkInfoLabel->setMinimumWidth(48);
    m_linkInfoLabel->setStyleSheet("color:#aaa;");
    syncLayout->addWidget(m_linkInfoLabel);

    // P0 #③: explicit multi-layout selector (auto / single / 2-4 columns / row).
    auto *layoutLabel = new QLabel(tr("布局:"), this);
    syncLayout->addWidget(layoutLabel);
    m_layoutCombo = new QComboBox(this);
    m_layoutCombo->addItems(
        {tr("自动"), tr("单列"), tr("2 列"), tr("3 列"), tr("4 列"), tr("一行"), tr("自定义")});
    m_layoutCombo->setCurrentIndex(0);
    connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CompareWorkspace::onLayoutChanged);
    syncLayout->addWidget(m_layoutCombo);

    // A-4.2: custom M×N grid spin boxes (active when layout = 自定义).
    // A-4.2: custom M×N grid spin boxes (active when layout = 自定义).
    // The row count is informational only — the engine packs by columns, so the
    // 行 spin box is disabled and labeled as auto-derived to avoid a misleading UI.
    auto *rowsLabel = new QLabel(tr("行"), this);
    rowsLabel->setToolTip(tr("行数由列数自动推导（按列填充，无法单独设置）"));
    syncLayout->addWidget(rowsLabel);
    m_gridRowsSpin = new QSpinBox(this);
    m_gridRowsSpin->setRange(1, 8);
    m_gridRowsSpin->setValue(2);
    m_gridRowsSpin->setEnabled(false);
    m_gridRowsSpin->setMaximumWidth(48);
    m_gridRowsSpin->setToolTip(tr("行数由列数自动推导（按列填充，无法单独设置）"));
    connect(m_gridRowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onCustomGridChanged);
    syncLayout->addWidget(m_gridRowsSpin);
    syncLayout->addWidget(new QLabel(tr("列"), this));
    m_gridColsSpin = new QSpinBox(this);
    m_gridColsSpin->setRange(1, 8);
    m_gridColsSpin->setValue(2);
    m_gridColsSpin->setEnabled(false);
    m_gridColsSpin->setMaximumWidth(48);
    connect(m_gridColsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onCustomGridChanged);
    syncLayout->addWidget(m_gridColsSpin);

    // P0 #③: inspector + histogram side panel toggle.
    m_sideChk = new QCheckBox(tr("检视面板"), this);
    m_sideChk->setChecked(false);
    connect(m_sideChk, &QCheckBox::toggled, this, &CompareWorkspace::onSideToggled);
    syncLayout->addWidget(m_sideChk);

    // M16.1: cursor-sync crosshair (n/n). When on, hovering any cell draws a
    // crosshair at the same image-space point across all compared cells, and the
    // inspector samples every cell at that point.
    m_crosshairChk = new QCheckBox(tr("同步准星"), this);
    m_crosshairChk->setChecked(false);
    syncLayout->addWidget(m_crosshairChk);

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
    syncLayout->addWidget(m_focusBtn);
    m_focusLabel = new QLabel(tr("基准: —"), this);
    m_focusLabel->setMinimumWidth(60);
    syncLayout->addWidget(m_focusLabel);

    syncLayout->addStretch(1);

    m_grid = new QWidget;
    m_layout = new QGridLayout(m_grid);
    m_layout->setSpacing(2);
    m_layout->setContentsMargins(0, 0, 0, 0);

    // QScrollArea wraps the grid so 2×4 layouts (5-8 images) can scroll
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(false);
    scroll->setWidget(m_grid);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

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
    syncLayout->addWidget(m_savePresetBtn);

    m_loadPresetBtn = new QPushButton(tr("读取布局"), this);
    m_loadPresetBtn->setToolTip(tr("从预设文件中读取布局"));
    connect(m_loadPresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onLoadPreset);
    syncLayout->addWidget(m_loadPresetBtn);

    m_swapBtn = new QPushButton(tr("交换窗格"), this);
    m_swapBtn->setToolTip(tr("交换选中的两个窗格"));
    m_swapBtn->setEnabled(false);
    connect(m_swapBtn, &QPushButton::clicked, this, &CompareWorkspace::onSwapPanes);
    syncLayout->addWidget(m_swapBtn);

    // P1 #④: Analyze & export buttons in the compare toolbar.
    m_analyzeBtn = new QPushButton(tr("分析"), this);
    m_analyzeBtn->setToolTip(tr("在分析面板中打开当前焦点图像"));
    connect(m_analyzeBtn, &QPushButton::clicked, this, &CompareWorkspace::analyzeCurrent);
    syncLayout->addWidget(m_analyzeBtn);

    m_exportReportBtn = new QPushButton(tr("导出报告"), this);
    m_exportReportBtn->setToolTip(tr("将对比结果导出为 HTML/Markdown/JSON 报告"));
    connect(m_exportReportBtn, &QPushButton::clicked, this,
            &CompareWorkspace::exportReportRequested);
    syncLayout->addWidget(m_exportReportBtn);

    auto *leftLay = new QVBoxLayout;
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(4);
    leftLay->addWidget(syncBar);
    leftLay->addWidget(scroll, 1);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    root->addLayout(leftLay, 1);
    root->addWidget(m_sidePanel);
}

CompareWorkspace::~CompareWorkspace()
{
    // The EventBus is a process-global singleton. If we don't unsubscribe, a
    // pending "CompareEngine.DiffResult" could fire into this (now destroyed)
    // widget and crash. The subscription id is stored in m_diffSubId.
    if (m_diffSubId != 0)
        EventBus::instance().unsubscribe(m_diffSubId);
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

void CompareWorkspace::showShortcutHelp()
{
    // Lightweight status-bar style tip via window title flash — no modal dialog
    // so day-long keyboard work is not interrupted.
    const QString tip =
        tr("Compare 快捷键: B Blink · Space 按住Blink · S Split · W Swipe · O Overlay · "
           "K 棋盘 · H Diff高亮 · Z/D 同步缩放/拖动 · C 准星 · L 像素连线 · "
           "1~8 布局预设 · PgUp/PgDn 或 ←/→ 连续导航 · F Fit · X 交换 · ? 帮助 · Esc 关闭");
    if (auto *w = window())
        w->setWindowTitle(tip);
}

void CompareWorkspace::setImages(const QStringList &paths)
{
    std::vector<std::string> stdPaths;
    stdPaths.reserve(paths.size());
    for (const QString &p : paths)
        stdPaths.push_back(p.toStdString());
    m_engine.setImages(stdPaths);
    // A-4: loading a fresh comparison set should not inherit adjustments from
    // the previous session; applySession will repopulate persisted values.
    m_cellAdjusts.clear();
    rebuildCells();
    fitAll();
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
    setFocus();
    // P0-2: publish the compare set + reference to the app-wide SelectionModel so
    // Metadata/Analysis/Export stay in sync with what is being compared.
    if (m_selection)
    {
        m_selection->setCompared(comparedImages());
        m_selection->setFocused(focusImagePath());
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
    return m_syncZoom && m_syncDrag;
}

void CompareWorkspace::setSyncEnabled(bool on)
{
    m_syncZoom = on;
    m_syncDrag = on;
    m_syncZoomChk->setChecked(on);
    m_syncDragChk->setChecked(on);
    m_engine.setSyncEnabled(on);
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
    fitAll();
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
    fitAll();
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
        updateMetrics();
    }
    update();
}

void CompareWorkspace::fitAll()
{
    double sharedScale = 1.0;
    bool first = true;
    const int n = m_engine.imageCount();
    // Pass 1: fit each pane to its own viewport to learn the per-image fit scale,
    // and accumulate the shared (minimum) scale so different-resolution images can
    // be aligned at one common zoom.
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        const ImageFrame *img = m_engine.imageAt(i);
        const QSize qs = m_cellViews[i]->size();
        const CellSize cell{qs.width(), qs.height()};
        QPixmap pm = QPixmap::fromImage(imageObjectToQImage(img));
        if (pm.isNull() || cell.w <= 0 || cell.h <= 0)
            continue;
        CellSize imgSize{pm.width(), pm.height()};
        m_engine.fitCell(i, cell, imgSize);
        if (first || m_engine.cellScale(i) < sharedScale)
            sharedScale = m_engine.cellScale(i);
        first = false;
    }
    if (!first)
    {
        // H5: "统一像素倍率" forces every pane to the same zoom regardless of the
        // sync-zoom toggle, so images of different resolutions line up 1:1 (same
        // pixel scale, top-left aligned). Without it, sync off lets each pane fit
        // independently and lose cross-pane pixel correspondence.
        const bool unify = m_uniformScale || m_syncZoom;
        for (int i = 0; i < n; ++i)
        {
            if (i >= m_cellViews.size() || !m_cellViews[i])
                continue;
            if (unify)
                m_engine.setCellScale(i, sharedScale);
        }
        if (unify)
            m_engine.setScale(sharedScale);
        if (m_uniformScale || m_syncDrag)
        {
            m_engine.setOffset(0.0, 0.0);
            for (int i = 0; i < n; ++i)
                if (i < m_cellViews.size() && m_cellViews[i])
                    m_engine.setCellOffset(i, 0.0, 0.0);
        }
    }
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
    // Sample every cell at the synced image-space point.
    if (valid && m_sidePanel && m_sidePanel->isVisible())
    {
        m_lastInspectX = qRound(pos.x());
        m_lastInspectY = qRound(pos.y());
        updateInspector(m_lastInspectX, m_lastInspectY);
    }
}

void CompareWorkspace::onFocusRequested(int cellIndex)
{
    // Toggle the locked reference: re-clicking the focused cell clears it.
    const int newFocus = (cellIndex == m_focusIndex) ? -1 : cellIndex;
    const bool locking = newFocus >= 0;

    m_focusIndex = newFocus;
    if (m_focusBtn)
        m_focusBtn->setChecked(locking);
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

    // Re-request diffs against the new base and refresh the inspector deltas.
    if (n > 1)
    {
        for (int i = 0; i < n; ++i)
        {
            if (i != m_focusIndex)
                m_engine.requestDiff(i, diffBaseIndex());
        }
        refreshDiffOverlay();
    }
    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0)
        updateInspector(m_lastInspectX, m_lastInspectY);
    update();
}

void CompareWorkspace::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitAll();
    positionCellHists();
}

// ─── M16.2: Per-cell image adjustments ───────────────────────────────────────
