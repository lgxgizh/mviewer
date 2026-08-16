// CompareWorkspace control construction split from the lifecycle constructor.
#include "compareworkspace_p.h"

void CompareWorkspace::buildSyncControls()
{
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
}

QWidget *CompareWorkspace::buildToolbarContainer(QHBoxLayout *&modeLayout,
                                                  QHBoxLayout *&viewLayout,
                                                  QHBoxLayout *&toolLayout)
{
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
    auto [modeBar, modeLayoutLocal] = makeToolbar("compareModeToolbar");
    auto [viewBar, viewLayoutLocal] = makeToolbar("compareViewToolbar");
    auto [toolBar, toolLayoutLocal] = makeToolbar("compareToolToolbar");
    toolbarLayout->addWidget(modeBar);
    toolbarLayout->addWidget(viewBar);
    toolbarLayout->addWidget(toolBar);
    viewLayoutLocal->addWidget(m_syncZoomChk);
    viewLayoutLocal->addWidget(m_syncDragChk);

    modeLayout = modeLayoutLocal;
    viewLayout = viewLayoutLocal;
    toolLayout = toolLayoutLocal;
    return toolbarContainer;
}

void CompareWorkspace::buildModeControls(QHBoxLayout *modeLayout, QHBoxLayout *viewLayout)
{
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
}

void CompareWorkspace::buildDiffControls(QHBoxLayout *toolLayout)
{
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
}

void CompareWorkspace::buildViewControls(QHBoxLayout *viewLayout)
{
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
}

void CompareWorkspace::buildToolbarActions(QHBoxLayout *toolLayout)
{
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
}

