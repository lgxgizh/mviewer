// CompareWorkspace control construction split from the lifecycle constructor.
#include "compareworkspace_p.h"

#include "compareworkspace_caption.h"
#include "compareworkspace_shortcuts.h"
#include "core/image/ImageFrame.h"

#include <QAction>
#include <QMenu>
#include <QSettings>
#include <QTimer>
#include <QToolButton>

void CompareWorkspace::setSyncRotate(bool on)
{
    if (m_syncRotate == on)
        return;
    m_syncRotate = on;
    if (m_syncRotateChk && m_syncRotateChk->isChecked() != on)
    {
        const QSignalBlocker blocker(m_syncRotateChk);
        m_syncRotateChk->setChecked(on);
    }
    if (m_syncRotatePanelChk && m_syncRotatePanelChk->isChecked() != on)
    {
        const QSignalBlocker blocker(m_syncRotatePanelChk);
        m_syncRotatePanelChk->setChecked(on);
    }
    showCompareStatus(on ? tr("已开启同步旋转：旋转将同时作用于所有图像")
                         : tr("已关闭同步旋转：旋转仅对选中的图像生效"));
}

void CompareWorkspace::setEditCellIndex(int cellIdx)
{
    m_explicitEditIdx = cellIdx;
    onEditCellSelected(cellIdx);
}

int CompareWorkspace::cellRotation(int cellIdx) const
{
    if (cellIdx >= 0 && cellIdx < static_cast<int>(m_cellAdjusts.size()))
        return m_cellAdjusts[static_cast<size_t>(cellIdx)].rotation;
    return 0;
}

bool CompareWorkspace::cellFlipH(int cellIdx) const
{
    if (cellIdx >= 0 && cellIdx < static_cast<int>(m_cellAdjusts.size()))
        return m_cellAdjusts[static_cast<size_t>(cellIdx)].flipH;
    return false;
}

bool CompareWorkspace::cellFlipV(int cellIdx) const
{
    if (cellIdx >= 0 && cellIdx < static_cast<int>(m_cellAdjusts.size()))
        return m_cellAdjusts[static_cast<size_t>(cellIdx)].flipV;
    return false;
}

bool CompareWorkspace::syncRotate() const
{
    return m_syncRotate;
}

int CompareWorkspace::editCellIndex() const
{
    return m_editIdx;
}

void CompareWorkspace::buildSyncControls()
{
    m_syncZoomChk = new QCheckBox(mviewer::cw::compareShortcutText("Z"), this);
    m_syncZoomChk->setObjectName("syncZoomCheck");
    m_syncZoomChk->setChecked(true);
    m_syncZoomChk->setToolTip(tr("同步所有窗格的缩放倍率 (Z)"));
    m_syncDragChk = new QCheckBox(mviewer::cw::compareShortcutText("D"), this);
    m_syncDragChk->setObjectName("syncDragCheck");
    m_syncDragChk->setChecked(true);
    m_syncDragChk->setToolTip(tr("同步所有窗格的平移拖动 (D)"));
    m_syncRotateChk = new QCheckBox(mviewer::cw::compareShortcutText("Alt+R"), this);
    m_syncRotateChk->setObjectName("syncRotateCheck");
    m_syncRotateChk->setToolTip(tr("勾选后，旋转与翻转将同步作用于所有正在比较的图像 (Alt+R)"));
    m_syncRotateChk->setChecked(m_syncRotate);

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
    connect(m_syncRotateChk, &QCheckBox::toggled, this, &CompareWorkspace::setSyncRotate);
}

QWidget *CompareWorkspace::buildToolbarContainer(QHBoxLayout *&modeLayout, QHBoxLayout *&viewLayout,
                                                 QHBoxLayout *&toolLayout,
                                                 QHBoxLayout *&toolActionsLayout)
{
    auto *toolbarContainer = new QWidget(this);
    toolbarContainer->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    auto *toolbarLayout = new QVBoxLayout(toolbarContainer);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(2);
    auto makeToolbar = [toolbarContainer](const char *name)
    {
        auto *bar = new QWidget(toolbarContainer);
        bar->setObjectName(name);
        auto *layout = new QHBoxLayout(bar);
        layout->setContentsMargins(6, 1, 6, 1);
        layout->setSpacing(4);
        return std::pair{bar, layout};
    };
    auto [modeBar, modeLayoutLocal] = makeToolbar("compareModeToolbar");
    auto [viewBar, viewLayoutLocal] = makeToolbar("compareViewToolbar");
    auto *toolBar = new QWidget(toolbarContainer);
    toolBar->setObjectName("compareToolToolbar");
    auto *toolRows = new QVBoxLayout(toolBar);
    toolRows->setContentsMargins(0, 0, 0, 0);
    toolRows->setSpacing(2);
    auto makeToolRow = [toolBar]()
    {
        auto *row = new QWidget(toolBar);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(6, 1, 6, 1);
        layout->setSpacing(4);
        return std::pair{row, layout};
    };
    auto [toolDiffBar, toolLayoutLocal] = makeToolRow();
    auto [toolActionsBar, toolActionsLayoutLocal] = makeToolRow();
    toolRows->addWidget(toolDiffBar);
    toolRows->addWidget(toolActionsBar);
    toolbarLayout->addWidget(modeBar);
    toolbarLayout->addWidget(viewBar);
    toolbarLayout->addWidget(toolBar);
    // Shortcut hints widen the view row; keep it inside the 1100px budget.
    viewLayoutLocal->setSpacing(3);
    viewLayoutLocal->addWidget(m_syncZoomChk);
    viewLayoutLocal->addWidget(m_syncDragChk);
    viewLayoutLocal->addWidget(m_syncRotateChk);

    modeLayout = modeLayoutLocal;
    viewLayout = viewLayoutLocal;
    toolLayout = toolLayoutLocal;
    toolActionsLayout = toolActionsLayoutLocal;
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
    m_blinkChk = new QCheckBox(mviewer::cw::compareShortcutText("B"), this);
    m_blinkChk->setObjectName("blinkCompareToggle");
    m_blinkChk->setEnabled(false);
    m_blinkChk->setToolTip(tr("点击开始/停止快速闪烁切换（快捷键: B）"));
    connect(m_blinkChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                {
                    // Blink owns the normal grid surface. For a two-image
                    // compare, cancel any canvas mode before starting the
                    // timer so the visible page and the checked mode agree.
                    if (m_engine.imageCount() == 2)
                    {
                        exclusiveMode(m_blinkChk);
                        updateCanvasModeVisibility();
                    }
                    startBlink(150); // fast flicker
                }
                else
                    stopBlink();
            });
    modeLayout->addWidget(m_blinkChk);

    // P0-4: split / swipe compare for exactly two images.
    m_splitChk = new QCheckBox(mviewer::cw::compareShortcutText("S"), this);
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

    m_swipeChk = new QCheckBox(mviewer::cw::compareShortcutText("W"), this);
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

    buildOverlayControls(modeLayout);
    buildCheckerboardControls(modeLayout);

    // A-4.5: continuous compare — walk consecutive pairs without reopening.
    m_prevPairBtn =
        new QPushButton(QString::fromUtf8("◀ ") + mviewer::cw::compareShortcutText("P"), this);
    m_prevPairBtn->setToolTip(pairNavTooltip(false, true));
    m_prevPairBtn->setEnabled(false);
    connect(m_prevPairBtn, &QPushButton::clicked, this, &CompareWorkspace::prevPair);
    modeLayout->addWidget(m_prevPairBtn);

    const mviewer::cw::CompareShortcut *nextShortcut = mviewer::cw::findCompareShortcut("N");
    const QString nextText =
        nextShortcut ? QString::fromUtf8(nextShortcut->name) + QString::fromUtf8(" ▶ (") +
                           QString::fromUtf8(nextShortcut->keys) + QStringLiteral(")")
                     : QString::fromUtf8("下一对 ▶ (N)");
    m_nextPairBtn = new QPushButton(nextText, this);
    m_nextPairBtn->setToolTip(pairNavTooltip(true, true));
    m_nextPairBtn->setEnabled(false);
    connect(m_nextPairBtn, &QPushButton::clicked, this, &CompareWorkspace::nextPair);
    modeLayout->addWidget(m_nextPairBtn);
    modeLayout->addStretch(1);
}

void CompareWorkspace::buildDiffControls(QHBoxLayout *toolLayout)
{
    // M15: threshold slider for difference heatmap (0-255).
    auto *thresholdLabel = new QLabel("阈值:", this);
    thresholdLabel->setObjectName("diffThresholdCaption");
    toolLayout->addWidget(thresholdLabel);
    m_thresholdSlider = new QSlider(Qt::Horizontal, this);
    m_thresholdSlider->setObjectName("diffThresholdSlider");
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(0);
    m_thresholdSlider->setMaximumWidth(120);
    m_thresholdSlider->setToolTip("差异阈值: 低于此值的像素将被隐藏");
    m_thresholdSlider->setEnabled(false);
    auto *sliderTimer = new QTimer(this);
    sliderTimer->setSingleShot(true);
    sliderTimer->setInterval(35);
    connect(sliderTimer, &QTimer::timeout, this, &CompareWorkspace::refreshAllDiffOverlays);
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this, sliderTimer](int value)
            {
                m_thresholdValue = static_cast<uint8_t>(value);
                if (!m_thresholdSlider->isSliderDown())
                    refreshAllDiffOverlays();
                else
                    sliderTimer->start();
            });
    connect(m_thresholdSlider, &QSlider::sliderReleased, this,
            [this, sliderTimer]()
            {
                sliderTimer->stop();
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(m_thresholdSlider);
    m_thresholdLabel = new QLabel("0", this);
    m_thresholdLabel->setObjectName("diffThresholdValueLabel");
    m_thresholdLabel->setMinimumWidth(24);
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this](int value) { m_thresholdLabel->setText(QString::number(value)); });
    toolLayout->addWidget(m_thresholdLabel);

    m_autoThresholdBtn = new QPushButton(tr("自动"), this);
    m_autoThresholdBtn->setObjectName("diffAutoThresholdButton");
    m_autoThresholdBtn->setMaximumWidth(42);
    resetSuggestedThreshold();
    connect(m_autoThresholdBtn, &QPushButton::clicked, this,
            &CompareWorkspace::onAutoThresholdClicked);
    toolLayout->addWidget(m_autoThresholdBtn);

    // A-4.6: Diff highlight mode (red diffs / gray similar).
    m_diffOverlayChk = new QCheckBox(tr("显示差异"), this);
    m_diffOverlayChk->setObjectName("diffOverlayToggle");
    m_diffOverlayChk->setChecked(false);
    m_diffOverlayChk->setEnabled(false);
    m_diffOverlayChk->setToolTip(tr("显式叠加差异热力图；普通 Compare 始终显示原图"));
    connect(m_diffOverlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_diffOverlayVisible = on;
                syncContextualCompareControls();
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(m_diffOverlayChk);

    m_diffHighlightChk = new QCheckBox(mviewer::cw::compareShortcutText("H"), this);
    m_diffHighlightChk->setObjectName("diffHighlightToggle");
    m_diffHighlightChk->setEnabled(false);
    m_diffHighlightChk->setToolTip(tr("差异区域红色高亮，相似区域灰度显示 (H)"));
    connect(m_diffHighlightChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                // Highlighting without visualization is invisible. Promote
                // the display toggle first, but keep this as one latest-wins
                // refresh so the worker never publishes the intermediate
                // non-highlighted overlay.
                if (on && m_diffOverlayChk && !m_diffOverlayChk->isChecked())
                {
                    const QSignalBlocker blocker(m_diffOverlayChk);
                    m_diffOverlayChk->setChecked(true);
                    m_diffOverlayVisible = true;
                }
                m_diffHighlight = on;
                syncContextualCompareControls();
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(m_diffHighlightChk);

    auto *gainLabel = new QLabel(tr("增益:"), this);
    gainLabel->setObjectName("diffGainCaption");
    toolLayout->addWidget(gainLabel);
    m_diffGainCombo = new QComboBox(this);
    m_diffGainCombo->setObjectName("diffGainCombo");
    m_diffGainCombo->addItems({"1x", "2x", "4x", "8x", "16x", "32x"});
    m_diffGainCombo->setCurrentIndex(0);
    m_diffGainCombo->setEnabled(false);
    m_diffGainCombo->setToolTip(tr("差异放大倍数（用于检查微弱噪点与压缩残差）"));
    connect(m_diffGainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx)
            {
                static const double kGains[] = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
                m_diffGain = (idx >= 0 && idx < 6) ? kGains[idx] : 1.0;
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(m_diffGainCombo);

    buildPixelLinkControls(toolLayout);
}

void CompareWorkspace::buildPixelLinkControls(QHBoxLayout *toolLayout)
{
    // A-4.3: Pixel Link — mark corresponding points across cells.
    m_pixelLinkChk = new QCheckBox(mviewer::cw::compareShortcutText("L"), this);
    m_pixelLinkChk->setObjectName("pixelLinkToggle");
    m_pixelLinkChk->setEnabled(false);
    m_pixelLinkChk->setToolTip(tr("开启后点击图片添加标记点，显示各图 RGB 与差值 (L)"));
    connect(m_pixelLinkChk, &QCheckBox::toggled, this, &CompareWorkspace::onPixelLinkToggled);
    toolLayout->addWidget(m_pixelLinkChk);
    m_clearLinksBtn = new QPushButton(tr("清除标记"), this);
    m_clearLinksBtn->setEnabled(false);
    m_clearLinksBtn->setToolTip(clearLinkTooltip());
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

    // A-4.2: custom grid is column-driven. CompareEngine derives the row count.
    auto *columnsLabel = new QLabel(tr("列:"), this);
    columnsLabel->setObjectName("compareColumnsCaption");
    columnsLabel->setToolTip(tr("只设置列数；行数由当前图片数自动推导"));
    viewLayout->addWidget(columnsLabel);
    m_gridColsSpin = new QSpinBox(this);
    m_gridColsSpin->setRange(1, 8);
    m_gridColsSpin->setValue(2);
    m_gridColsSpin->setEnabled(false);
    m_gridColsSpin->setObjectName("compareColumnsSpin");
    m_gridColsSpin->setToolTip(tr("只设置列数；行数由当前图片数自动推导"));
    connect(m_gridColsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onCustomGridChanged);
    viewLayout->addWidget(m_gridColsSpin);
    m_layoutStatusLabel = new QLabel(this);
    m_layoutStatusLabel->setObjectName("compareGridStatus");
    m_layoutStatusLabel->setToolTip(tr("当前网格：行数由图片数和列数推导"));
    viewLayout->addWidget(m_layoutStatusLabel);

    // P0 #③: inspector + histogram side panel toggle.
    m_sideChk = new QCheckBox(mviewer::cw::compareShortcutText("I"), this);
    m_sideChk->setObjectName("analysisPanelToggle");
    m_sideChk->setChecked(false);
    connect(m_sideChk, &QCheckBox::toggled, this, &CompareWorkspace::onSideToggled);
    viewLayout->addWidget(m_sideChk);

    // M16.1: cursor-sync crosshair (n/n). When on, hovering any cell draws a
    // crosshair at the same image-space point across all compared cells, and the
    // inspector samples every cell at that point.
    m_crosshairChk = new QCheckBox(mviewer::cw::compareShortcutText("R"), this);
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
    m_focusLabel->setMinimumWidth(48);
    viewLayout->addWidget(m_focusLabel);

    // M57: Compare is static by default, but a focused animated/page source
    // can be switched to an explicit frame/page without changing the Browse
    // file index. The control is hidden for ordinary single-frame sources.
    m_frameLabel = new QLabel(tr("帧/页:"), this);
    m_frameSpin = new QSpinBox(this);
    m_frameSpin->setObjectName(QStringLiteral("compareFrameSpin"));
    m_frameSpin->setRange(1, 1);
    m_frameSpin->setVisible(false);
    m_frameSpin->setEnabled(false);
    m_frameLabel->setVisible(false);
    connect(m_frameSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onFrameControlChanged);
    viewLayout->addWidget(m_frameLabel);
    viewLayout->addWidget(m_frameSpin);

    viewLayout->addStretch(1);
}

void CompareWorkspace::setOverlayMode(mviewer::OverlayMode mode)
{
    if (m_displayOverlay == mode)
    {
        if (m_channelCombo)
        {
            const int index = m_channelCombo->findData(static_cast<int>(mode));
            if (index >= 0 && m_channelCombo->currentIndex() != index)
            {
                const QSignalBlocker blocker(m_channelCombo);
                m_channelCombo->setCurrentIndex(index);
            }
        }
        return;
    }
    m_displayOverlay = mode;
    if (m_channelCombo)
    {
        const int index = m_channelCombo->findData(static_cast<int>(mode));
        if (index >= 0)
        {
            const QSignalBlocker blocker(m_channelCombo);
            m_channelCombo->setCurrentIndex(index);
        }
    }
    applyDisplayOverlayToViews();
    m_canvasBaseKey.clear();
    update();
}

void CompareWorkspace::applyDisplayOverlayToViews()
{
    for (RawImageView *view : m_cellViews)
    {
        if (view)
            view->setDisplayOverlay(m_displayOverlay);
    }
}

void CompareWorkspace::applyFilenameOverlays()
{
    for (int i = 0; i < m_cellViews.size(); ++i)
    {
        RawImageView *view = m_cellViews[i];
        if (!view)
            continue;
        QString name;
        if (i < m_cellLabels.size())
            name = comparePaneCaptionFullText(m_cellLabels[i]);
        if (name.isEmpty())
        {
            const ImageFrame *img = m_engine.imageAt(i);
            if (img)
                name = QString::fromStdString(img->metadata().fileName);
        }
        view->setFilenameOverlay(name, m_filenameOverlay);
        // Filename visibility is the top overlay only. A display-error status
        // is the one reason the below-pane label stays on screen.
        if (i < m_cellLabels.size() && m_cellLabels[i] &&
            !comparePaneCaptionHasStatus(m_cellLabels[i]))
            m_cellLabels[i]->setVisible(false);
    }
}

void CompareWorkspace::buildToolbarActions(QHBoxLayout *toolLayout)
{
    m_paneHistOverlayChk = new QCheckBox(tr("直方图"), this);
    m_paneHistOverlayChk->setObjectName("paneHistogramOverlayToggle");
    m_paneHistOverlayChk->setChecked(m_paneHistOverlay);
    m_paneHistOverlayChk->setToolTip(tr("在每张比较图左上角透明叠加当前直方图"));
    connect(m_paneHistOverlayChk, &QCheckBox::toggled, this,
            &CompareWorkspace::onPaneHistOverlayToggled);
    toolLayout->addWidget(m_paneHistOverlayChk);

    m_filenameOverlayChk = new QCheckBox(tr("文件名"), this);
    m_filenameOverlayChk->setObjectName("compareFilenameOverlayToggle");
    m_filenameOverlayChk->setChecked(m_filenameOverlay);
    m_filenameOverlayChk->setToolTip(tr("显示或隐藏每张比较图上方的文件名，过长时换行"));
    connect(m_filenameOverlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_filenameOverlay = on;
                applyFilenameOverlays();
                positionCellHists();
                m_canvasBaseKey.clear();
                update();
            });
    toolLayout->addWidget(m_filenameOverlayChk);

    m_channelCombo = new QComboBox(this);
    m_channelCombo->setObjectName("compareChannelCombo");
    m_channelCombo->setMaximumWidth(125);
    m_channelCombo->addItem(tr("RGB (Shift+1)"), static_cast<int>(mviewer::OverlayMode::None));
    m_channelCombo->addItem(tr("R (Shift+2)"), static_cast<int>(mviewer::OverlayMode::ChannelR));
    m_channelCombo->addItem(tr("G (Shift+3)"), static_cast<int>(mviewer::OverlayMode::ChannelG));
    m_channelCombo->addItem(tr("B (Shift+4)"), static_cast<int>(mviewer::OverlayMode::ChannelB));
    m_channelCombo->addItem(tr("Y (Shift+5)"), static_cast<int>(mviewer::OverlayMode::ChannelY));
    m_channelCombo->addItem(tr("V (Shift+6)"), static_cast<int>(mviewer::OverlayMode::ChannelV));
    m_channelCombo->setToolTip(tr("隔离 R/G/B/Y/V 通道（Shift+1…6）"));
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (!m_channelCombo)
                    return;
                const auto mode =
                    static_cast<mviewer::OverlayMode>(m_channelCombo->currentData().toInt());
                setOverlayMode(mode);
            });
    toolLayout->addWidget(m_channelCombo);

    // M16.6: layout presets save/load + swap panes (sync bar right side)
    m_savePresetBtn = new QPushButton(tr("存储布局"), this);
    m_savePresetBtn->setToolTip(tr("将当前布局存储为预设"));
    connect(m_savePresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onSavePreset);
    toolLayout->addWidget(m_savePresetBtn);

    m_loadPresetBtn = new QPushButton(tr("读取布局"), this);
    m_loadPresetBtn->setToolTip(tr("从预设文件中读取布局"));
    connect(m_loadPresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onLoadPreset);
    toolLayout->addWidget(m_loadPresetBtn);

    m_swapBtn = new QPushButton(mviewer::cw::compareShortcutText("X"), this);
    m_swapBtn->setObjectName("compareSwapPanesButton");
    m_swapBtn->setToolTip(tr("交换 A/B 窗格 (快捷键: X)"));
    m_swapBtn->setEnabled(false);
    connect(m_swapBtn, &QPushButton::clicked, this, &CompareWorkspace::onSwapPanes);
    toolLayout->addWidget(m_swapBtn);

    m_temporaryCompareButton = new QPushButton(mviewer::cw::compareShortcutText("Space"), this);
    m_temporaryCompareButton->setObjectName("temporaryCompareButton");
    m_temporaryCompareButton->setToolTip(temporaryCompareTooltip());
    m_temporaryCompareButton->setEnabled(false);
    connect(m_temporaryCompareButton, &QPushButton::pressed, this,
            &CompareWorkspace::beginClassicTemporaryCompare);
    connect(m_temporaryCompareButton, &QPushButton::released, this,
            &CompareWorkspace::endTemporaryCompare);
    m_temporaryCompareButton->installEventFilter(this);
    if (auto *w = window())
        w->installEventFilter(this);
    toolLayout->addWidget(m_temporaryCompareButton);

    addFitWindowButton(toolLayout);
    addSnapshotButton(toolLayout);

    // P1 #④: Analyze & export buttons in the compare toolbar.
    m_analyzeBtn = new QPushButton(tr("分析"), this);
    m_analyzeBtn->setObjectName("analyzeCompareButton");
    m_analyzeBtn->setEnabled(false);
    m_analyzeBtn->setToolTip(tr("打开比较检视面板"));
    connect(m_analyzeBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_sideChk)
                    m_sideChk->setChecked(true);
                emit analyzeCurrent();
            });
    toolLayout->addWidget(m_analyzeBtn);

    m_exportReportBtn = new QPushButton(tr("导出报告"), this);
    m_exportReportBtn->setObjectName("exportCompareReportButton");
    m_exportReportBtn->setEnabled(false);
    m_exportReportBtn->setToolTip(tr("将对比结果导出为 HTML/Markdown/JSON 报告"));
    connect(m_exportReportBtn, &QPushButton::clicked, this,
            &CompareWorkspace::exportReportRequested);
    toolLayout->addWidget(m_exportReportBtn);
    toolLayout->addStretch(1);
}

void CompareWorkspace::addFitWindowButton(QHBoxLayout *toolLayout)
{
    // Fit to window (F). 统一像素倍率 decides shared vs per-pane scale. The view
    // row is already at the 1100px budget, so this stays on the actions row.
    auto *fitBtn = new QPushButton(mviewer::cw::compareShortcutText("F"), this);
    fitBtn->setObjectName("fitWindowButton");
    fitBtn->setToolTip(tr("适合窗口（快捷键: F）：勾选「统一像素倍率」时所有窗格用同一倍率适配；"
                          "不勾选时各窗格按自身分辨率适配，相同视野/宽高比的图显示为相近大小"));
    connect(fitBtn, &QPushButton::clicked, this,
            [this]()
            {
                fitAll();
                showCompareStatus(tr("视图自适应窗口 (Fit)"));
                if (m_compareCanvas)
                    m_compareCanvas->update();
                update();
            });
    toolLayout->addWidget(fitBtn);
}

QWidget *CompareWorkspace::buildStatusStrip()
{
    auto *strip = new QWidget(this);
    strip->setObjectName("compareStatusStrip");
    strip->setStyleSheet(
        "QWidget#compareStatusStrip{background:#141416; border-top:1px solid #27272a;}");
    auto *lay = new QHBoxLayout(strip);
    lay->setContentsMargins(8, 2, 8, 2);
    lay->setSpacing(8);

    m_metricLabel = new QLabel(tr("PSNR: —    SSIM: —"), strip);
    m_metricLabel->setObjectName("diffMetricsLabel");
    m_metricLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_metricLabel->setWordWrap(true);
    m_metricLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_metricLabel->setStyleSheet(
        "color:#38bdf8; font-family:monospace; font-weight:700; padding:2px 8px; "
        "background:#18181b; border:1px solid #27272a; border-radius:4px;");
    lay->addWidget(m_metricLabel, 0);

    m_autoAlignChk = new QCheckBox(tr("对齐"), strip);
    m_autoAlignChk->setObjectName("autoAlignBeforeDiffToggle");
    m_autoAlignChk->setStyleSheet("color:#e4e4e7;");
    m_autoAlignChk->setToolTip(tr("对比前按整数像素平移自动对齐，消除平移错位后再算 PSNR/SSIM"));
    m_autoAlignChk->setChecked(QSettings().value("autoAlignBeforeDiff", false).toBool());
    connect(m_autoAlignChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                QSettings().setValue("autoAlignBeforeDiff", on);
                refreshAllDiffOverlays();
            });
    lay->addWidget(m_autoAlignChk);

    m_compareStatusLabel = new QLabel(strip);
    m_compareStatusLabel->setObjectName("compareStatusLabel");
    m_compareStatusLabel->setStyleSheet("color:#a1a1aa;");
    lay->addWidget(m_compareStatusLabel, 1);

    m_exitBtn = new QPushButton(mviewer::cw::compareShortcutText("Esc"), strip);
    m_exitBtn->setObjectName("exitCompareButton");
    m_exitBtn->setToolTip(tr("关闭比较窗口（Esc；有选区时 Esc 先清除选区）"));
    connect(m_exitBtn, &QPushButton::clicked, this, &CompareWorkspace::closeCompareHost);
    lay->addWidget(m_exitBtn);
    return strip;
}

void CompareWorkspace::syncContextualCompareControls()
{
    // Accessory sliders stay in the layout so toggling a mode does not shove
    // the primary buttons. Enabled state follows the mode; visibility does not.
    const bool overlayOn = m_overlayChk && m_overlayChk->isChecked();
    const bool checkerOn = m_checkerChk && m_checkerChk->isChecked();
    const bool diffOn = m_diffOverlayChk && m_diffOverlayChk->isChecked();
    const bool customGrid = m_layoutCombo && m_layoutCombo->currentIndex() == 6;
    auto keep = [](QWidget *widget, bool enabled)
    {
        if (!widget)
            return;
        widget->setVisible(true);
        widget->setEnabled(enabled);
    };
    keep(m_overlayAlphaSlider, overlayOn);
    keep(m_overlayAlphaLabel, overlayOn);
    keep(m_checkerSizeSlider, checkerOn);
    keep(m_checkerSizeLabel, checkerOn);
    keep(m_thresholdSlider, diffOn);
    keep(m_thresholdLabel, diffOn);
    keep(m_autoThresholdBtn, diffOn && m_hasSuggestedThreshold);
    keep(findChild<QLabel *>(QStringLiteral("diffThresholdCaption")), diffOn);
    keep(m_diffGainCombo, diffOn);
    keep(findChild<QLabel *>(QStringLiteral("diffGainCaption")), diffOn);
    keep(m_gridColsSpin, customGrid);
    keep(findChild<QLabel *>(QStringLiteral("compareColumnsCaption")), customGrid);
}

void CompareWorkspace::onAutoThresholdClicked()
{
    if (!m_thresholdSlider || !m_hasSuggestedThreshold)
        return;
    const int value = static_cast<int>(m_suggestedThreshold);
    m_thresholdSlider->setValue(value);
    showCompareStatus(tr("已应用自动阈值 %1").arg(value));
}

void CompareWorkspace::resetSuggestedThreshold()
{
    m_suggestedThreshold = 0;
    m_hasSuggestedThreshold = false;
    if (!m_autoThresholdBtn)
        return;
    m_autoThresholdBtn->setEnabled(false);
    m_autoThresholdBtn->setToolTip(tr("基于差异分布自动计算最佳分离阈值 (Otsu)"));
}

void CompareWorkspace::noteSuggestedThreshold(bool hasSuggestion, int value)
{
    if (!hasSuggestion)
    {
        resetSuggestedThreshold();
        return;
    }
    const int clamped = value < 0 ? 0 : (value > 255 ? 255 : value);
    m_suggestedThreshold = static_cast<uint8_t>(clamped);
    m_hasSuggestedThreshold = true;
    if (!m_autoThresholdBtn)
        return;
    m_autoThresholdBtn->setToolTip(
        tr("基于差异分布自动计算最佳分离阈值 (Otsu 建议: %1)").arg(clamped));
    const bool diffOn = m_diffOverlayChk && m_diffOverlayChk->isChecked();
    m_autoThresholdBtn->setEnabled(diffOn);
}

void CompareWorkspace::addSnapshotButton(QHBoxLayout *toolLayout)
{
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("compareSnapshotButton"));
    button->setText(tr("截图"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolTip(tr("复制或保存当前比较视图"));
    auto *menu = new QMenu(button);
    auto *copyAction = menu->addAction(mviewer::cw::compareShortcutText("Ctrl+C"));
    copyAction->setObjectName(QStringLiteral("compareSnapshotCopyAction"));
    auto *saveAction = menu->addAction(mviewer::cw::compareShortcutText("Ctrl+S"));
    saveAction->setObjectName(QStringLiteral("compareSnapshotSaveAction"));
    connect(copyAction, &QAction::triggered, this,
            &CompareWorkspace::copyComparisonViewToClipboard);
    connect(saveAction, &QAction::triggered, this, &CompareWorkspace::saveComparisonViewToFile);
    button->setMenu(menu);
    toolLayout->addWidget(button);
}
