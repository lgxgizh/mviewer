// CompareWorkspace control construction split from the lifecycle constructor.
#include "ThemeMetrics.h"
#include "compare_display_manager.h"
#include "compareworkspace_p.h"

#include "Theme.h"
#include "ThemeTokens.h"
#include "compareworkspace_shortcuts.h"

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
    if (QAction *action = findChild<QAction *>(QStringLiteral("syncRotateAction")))
        action->setChecked(on);
    showCompareStatus(on ? tr("Sync rotation enabled: rotation applies to all images")
                         : tr("Sync rotation disabled: rotation applies only to the selected image"));
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
    m_syncZoomChk->setToolTip(tr("Sync zoom across all panes (Z)"));
    m_syncDragChk = new QCheckBox(mviewer::cw::compareShortcutText("D"), this);
    m_syncDragChk->setObjectName("syncDragCheck");
    m_syncDragChk->setChecked(true);
    m_syncDragChk->setToolTip(tr("Sync drag across all panes (D)"));
    m_syncRotateChk = new QCheckBox(mviewer::cw::compareShortcutText("Alt+R"), this);
    m_syncRotateChk->setObjectName("syncRotateCheck");
    m_syncRotateChk->setToolTip(tr("When checked, rotation and flip are applied in sync to all images being compared (Alt+R)"));
    m_syncRotateChk->setChecked(m_syncRotate);

    auto applySync = [this](bool) { update(); };
    connect(m_syncZoomChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncZoom = on;
                if (m_displayManager)
                    m_displayManager->setSyncZoom(on);
                m_engine.setSyncMode(m_syncZoom, m_syncDrag);
                applySync(on);
            });
    connect(m_syncDragChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncDrag = on;
                if (m_displayManager)
                    m_displayManager->setSyncDrag(on);
                m_engine.setSyncMode(m_syncZoom, m_syncDrag);
                applySync(on);
            });
    connect(m_syncRotateChk, &QCheckBox::toggled, this, &CompareWorkspace::setSyncRotate);
}

void CompareWorkspace::buildModeControls(QHBoxLayout *modeLayout, QHBoxLayout *viewLayout)
{
    // H5: "统一像素倍率" — align every pane at the same zoom so images of
    // different resolutions can be compared 1:1 (pixel-corresponding). Independent
    // of the sync-zoom toggle; when on, Fit uses the shared (minimum) scale for all.
    m_uniformScaleChk = new QCheckBox(tr("Uniform Pixel Scale"), this);
    m_uniformScaleChk->setChecked(false);
    m_uniformScaleChk->setToolTip(tr("Show all panes at the same scale so images of different resolutions stay 1:1 pixel-aligned"));
    connect(m_uniformScaleChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_uniformScale = on;
                if (m_displayManager)
                    m_displayManager->setUniformScale(on);
                fitAll();
                update();
            });
    viewLayout->addWidget(m_uniformScaleChk);

    // M14-3: blink (flicker) compare — rapid toggle between base and target.
    // Click the button (or press B) to start/stop rapid blinking.
    m_blinkChk = new QCheckBox(mviewer::cw::compareShortcutText("B"), this);
    m_blinkChk->setObjectName("blinkCompareToggle");
    m_blinkChk->setEnabled(false);
    m_blinkChk->setToolTip(tr("Click to start/stop rapid blink (Shortcut: B)"));
    connect(m_blinkChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (m_diffRenderer)
                    m_diffRenderer->setBlinkActive(on);
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
    m_splitChk->setToolTip(tr("Available only with 2 images: side-by-side comparison (Shortcut: S)"));
    connect(m_splitChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                {
                    exclusiveMode(m_splitChk);
                    if (m_diffRenderer)
                        m_diffRenderer->setBlendMode(mviewer::ui::DiffBlendMode::SideBySideSplit);
                }
                updateCanvasModeVisibility();
            });
    modeLayout->addWidget(m_splitChk);

    m_swipeChk = new QCheckBox(mviewer::cw::compareShortcutText("W"), this);
    m_swipeChk->setEnabled(false);
    m_swipeChk->setToolTip(tr("Available only with 2 images: sliding split comparison (Shortcut: W)"));
    connect(m_swipeChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                {
                    exclusiveMode(m_swipeChk);
                    if (m_diffRenderer)
                        m_diffRenderer->setBlendMode(mviewer::ui::DiffBlendMode::Swipe);
                }
                updateCanvasModeVisibility();
            });
    modeLayout->addWidget(m_swipeChk);

    buildOverlayControls(modeLayout);
    buildCheckerboardControls(modeLayout);

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
    m_thresholdSlider->setToolTip("Difference threshold: pixels below this value will be hidden");
    m_thresholdSlider->setEnabled(false);
    auto *sliderTimer = new QTimer(this);
    sliderTimer->setSingleShot(true);
    sliderTimer->setInterval(35);
    connect(sliderTimer, &QTimer::timeout, this, &CompareWorkspace::refreshAllDiffOverlays);
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this, sliderTimer](int value)
            {
                m_thresholdValue = static_cast<uint8_t>(value);
                if (m_diffRenderer)
                    m_diffRenderer->setThreshold(m_thresholdValue);
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

    m_autoThresholdBtn = new QPushButton(tr("Auto"), this);
    m_autoThresholdBtn->setObjectName("diffAutoThresholdButton");
    m_autoThresholdBtn->setMaximumWidth(42);
    resetSuggestedThreshold();
    connect(m_autoThresholdBtn, &QPushButton::clicked, this,
            &CompareWorkspace::onAutoThresholdClicked);
    toolLayout->addWidget(m_autoThresholdBtn);

    // A-4.6: Diff highlight mode (red diffs / gray similar).
    m_diffOverlayChk = new QCheckBox(tr("Show Difference"), this);
    m_diffOverlayChk->setObjectName("diffOverlayToggle");
    m_diffOverlayChk->setChecked(false);
    m_diffOverlayChk->setEnabled(false);
    m_diffOverlayChk->setToolTip(tr("Explicitly overlay the difference heatmap; normal Compare always shows the original image"));
    connect(m_diffOverlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_diffOverlayVisible = on;
                m_diffOverlayPromoted = false;
                syncContextualCompareControls();
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(m_diffOverlayChk);

    m_diffHighlightChk = new QCheckBox(mviewer::cw::compareShortcutText("H"), this);
    m_diffHighlightChk->setObjectName("diffHighlightToggle");
    m_diffHighlightChk->setEnabled(false);
    m_diffHighlightChk->setToolTip(tr("Highlight differences in red; show similar areas in grayscale (H)"));
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
                    m_diffOverlayPromoted = true;
                }
                // Off restores the plain image when highlight was what turned
                // the heatmap on; otherwise the right pane kept the diff view.
                if (!on && m_diffOverlayPromoted && m_diffOverlayChk)
                {
                    const QSignalBlocker blocker(m_diffOverlayChk);
                    m_diffOverlayChk->setChecked(false);
                    m_diffOverlayVisible = false;
                    m_diffOverlayPromoted = false;
                }
                m_diffHighlight = on;
                syncContextualCompareControls();
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(m_diffHighlightChk);

    auto *gainLabel = new QLabel(tr("Gain:"), this);
    gainLabel->setObjectName("diffGainCaption");
    toolLayout->addWidget(gainLabel);
    m_diffGainCombo = new QComboBox(this);
    m_diffGainCombo->setObjectName("diffGainCombo");
    m_diffGainCombo->addItems({"1x", "2x", "4x", "8x", "16x", "32x"});
    m_diffGainCombo->setCurrentIndex(0);
    m_diffGainCombo->setEnabled(false);
    m_diffGainCombo->setToolTip(tr("Difference amplification (for inspecting faint noise and compression residuals)"));
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
    m_pixelLinkChk->setToolTip(tr("When enabled, click an image to add markers showing each image's RGB and the difference (L)"));
    connect(m_pixelLinkChk, &QCheckBox::toggled, this, &CompareWorkspace::onPixelLinkToggled);
    toolLayout->addWidget(m_pixelLinkChk);
    m_clearLinksBtn = new QPushButton(tr("Clear Markers"), this);
    m_clearLinksBtn->setEnabled(false);
    m_clearLinksBtn->setToolTip(clearLinkTooltip());
    connect(m_clearLinksBtn, &QPushButton::clicked, this, &CompareWorkspace::clearLinkPoints);
    toolLayout->addWidget(m_clearLinksBtn);
    m_linkInfoLabel = new QLabel(tr("Markers: 0"), this);
    m_linkInfoLabel->setMinimumWidth(48);
    m_linkInfoLabel->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
    toolLayout->addWidget(m_linkInfoLabel);
}

void CompareWorkspace::buildViewControls(QHBoxLayout *viewLayout)
{
    // P0 #③: explicit multi-layout selector (auto / single / 2-4 columns / row).
    auto *layoutLabel = new QLabel(tr("Layout:"), this);
    viewLayout->addWidget(layoutLabel);
    m_layoutCombo = new QComboBox(this);
    m_layoutCombo->addItems(
        {tr("Auto"), tr("Single column"), tr("2 columns"), tr("3 columns"), tr("4 columns"), tr("One row"), tr("Custom")});
    m_layoutCombo->setCurrentIndex(0);
    connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CompareWorkspace::onLayoutChanged);
    viewLayout->addWidget(m_layoutCombo);

    // A-4.2: custom grid is column-driven. CompareEngine derives the row count.
    auto *columnsLabel = new QLabel(tr("Columns:"), this);
    columnsLabel->setObjectName("compareColumnsCaption");
    columnsLabel->setToolTip(tr("Only set the column count; the row count is derived automatically from the current image count"));
    viewLayout->addWidget(columnsLabel);
    m_gridColsSpin = new QSpinBox(this);
    m_gridColsSpin->setRange(1, 8);
    m_gridColsSpin->setValue(2);
    m_gridColsSpin->setEnabled(false);
    m_gridColsSpin->setObjectName("compareColumnsSpin");
    m_gridColsSpin->setToolTip(tr("Only set the column count; the row count is derived automatically from the current image count"));
    connect(m_gridColsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onCustomGridChanged);
    viewLayout->addWidget(m_gridColsSpin);
    m_layoutStatusLabel = new QLabel(this);
    m_layoutStatusLabel->setObjectName("compareGridStatus");
    m_layoutStatusLabel->setToolTip(tr("Current grid: row count is derived from the image count and column count"));
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
    m_crosshairChk->setObjectName("syncCrosshairToggle");
    connect(m_crosshairChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                // R / checkbox / menu all land here. Off must erase the synced
                // crosshair from every pane now, not on the next hover.
                if (on)
                    return;
                for (RawImageView *view : m_cellViews)
                    if (view)
                        view->clearCrosshair();
            });
    viewLayout->addWidget(m_crosshairChk);

    // M16.1: focus-lock / reference pin (n/1). Locks a cell as the comparison
    // reference; diff overlays and inspector deltas use it as the base.
    m_focusBtn = new QPushButton(tr("Lock Reference"), this);
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
    m_focusLabel = new QLabel(tr("Reference: —"), this);
    m_focusLabel->setMinimumWidth(48);
    viewLayout->addWidget(m_focusLabel);

    // M57: Compare is static by default, but a focused animated/page source
    // can be switched to an explicit frame/page without changing the Browse
    // file index. The control is hidden for ordinary single-frame sources.
    m_frameLabel = new QLabel(tr("Frame/Page:"), this);
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

void CompareWorkspace::buildToolbarActions(QHBoxLayout *toolLayout)
{
    m_paneHistOverlayChk = new QCheckBox(tr("Histogram"), this);
    m_paneHistOverlayChk->setObjectName("paneHistogramOverlayToggle");
    m_paneHistOverlayChk->setChecked(m_paneHistOverlay);
    m_paneHistOverlayChk->setToolTip(tr("Translucently overlay the current histogram at the top-left of each comparison image"));
    connect(m_paneHistOverlayChk, &QCheckBox::toggled, this,
            &CompareWorkspace::onPaneHistOverlayToggled);
    toolLayout->addWidget(m_paneHistOverlayChk);

    m_filenameOverlayChk = new QCheckBox(tr("File Name"), this);
    m_filenameOverlayChk->setObjectName("compareFilenameOverlayToggle");
    m_filenameOverlayChk->setChecked(m_filenameOverlay);
    m_filenameOverlayChk->setToolTip(tr("Show or hide the filename above each comparison image, wrapping when too long"));
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
    m_channelCombo->setToolTip(tr("Isolate R/G/B/Y/V channels (Shift+1…6)"));
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
    m_savePresetBtn = new QPushButton(tr("Save Layout"), this);
    m_savePresetBtn->setToolTip(tr("Save the current layout as a preset"));
    connect(m_savePresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onSavePreset);
    toolLayout->addWidget(m_savePresetBtn);

    m_loadPresetBtn = new QPushButton(tr("Load Layout"), this);
    m_loadPresetBtn->setToolTip(tr("Load a layout from a preset file"));
    connect(m_loadPresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onLoadPreset);
    toolLayout->addWidget(m_loadPresetBtn);

    m_swapBtn = new QPushButton(mviewer::cw::compareShortcutText("X"), this);
    m_swapBtn->setObjectName("compareSwapPanesButton");
    m_swapBtn->setToolTip(tr("Swap A/B panes (Shortcut: X)"));
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
    m_analyzeBtn = new QPushButton(tr("Analyze"), this);
    m_analyzeBtn->setObjectName("analyzeCompareButton");
    m_analyzeBtn->setEnabled(false);
    m_analyzeBtn->setToolTip(tr("Open Comparison Inspection Panel"));
    connect(m_analyzeBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_sideChk)
                    m_sideChk->setChecked(true);
                emit analyzeCurrent();
            });
    toolLayout->addWidget(m_analyzeBtn);

    m_exportReportBtn = new QPushButton(tr("Export Report"), this);
    m_exportReportBtn->setObjectName("exportCompareReportButton");
    m_exportReportBtn->setEnabled(false);
    m_exportReportBtn->setToolTip(tr("Export comparison results as an HTML/Markdown/JSON report"));
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
    fitBtn->setToolTip(tr("Fit to window (Shortcut: F): when \"Uniform pixel scale\" is checked, all panes fit at the same scale;"
                          "不勾选时各窗格按自身分辨率适配，相同视野/宽高比的图显示为相近大小"));
    connect(fitBtn, &QPushButton::clicked, this,
            [this]()
            {
                fitAll();
                showCompareStatus(tr("View Fitted to Window (Fit)"));
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
        QStringLiteral("QWidget#compareStatusStrip{background:%1; border-top:1px solid %2;}")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg0),
                 mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Border)));
    auto *lay = new QHBoxLayout(strip);
    lay->setContentsMargins(mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace4,
                            mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace4);
    lay->setSpacing(8);

    const int chipRadius = mviewer::ui::makeThemeTokens(true).radiusControl;
    m_metricLabel = new QLabel(strip);
    m_metricLabel->setObjectName("diffMetricsLabel");
    m_metricLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_metricLabel->setWordWrap(false);
    m_metricLabel->setFixedHeight(24);
    m_metricLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_metricLabel->setStyleSheet(
        QStringLiteral("color:#38bdf8; font-family:monospace; font-weight:700; padding:2px 8px; "
                       "background:%1; border:1px solid %2; border-radius:%3px;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg0),
                 mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Border))
            .arg(chipRadius));
    presentDiffMetrics(tr("PSNR: —    SSIM: —"));
    lay->addWidget(m_metricLabel, 0);

    m_autoAlignChk = new QCheckBox(tr("Align"), strip);
    m_autoAlignChk->setObjectName("autoAlignBeforeDiffToggle");
    m_autoAlignChk->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextPrimary)));
    m_autoAlignChk->setToolTip(tr("Before comparison, auto-align by integer-pixel translation to remove shift error, then compute PSNR/SSIM"));
    m_autoAlignBeforeDiff = QSettings().value("autoAlignBeforeDiff", false).toBool();
    m_autoAlignChk->setChecked(m_autoAlignBeforeDiff);
    connect(m_autoAlignChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                QSettings().setValue("autoAlignBeforeDiff", on);
                setAutoAlignBeforeDiff(on);
            });
    lay->addWidget(m_autoAlignChk);

    m_compareStatusLabel = new QLabel(strip);
    m_compareStatusLabel->setObjectName("compareStatusLabel");
    m_compareStatusLabel->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
    lay->addWidget(m_compareStatusLabel, 1);

    m_exitBtn = new QPushButton(mviewer::cw::compareShortcutText("Esc"), strip);
    m_exitBtn->setObjectName("exitCompareButton");
    m_exitBtn->setToolTip(tr("Close the comparison window (Esc; with a selection, Esc clears it first)"));
    connect(m_exitBtn, &QPushButton::clicked, this, &CompareWorkspace::closeCompareHost);
    lay->addWidget(m_exitBtn);
    return strip;
}

void CompareWorkspace::syncContextualCompareControls()
{
    // Mode sliders appear only while that mode is on, so the idle chrome stays one row.
    const bool overlayOn = m_overlayChk && m_overlayChk->isChecked();
    const bool checkerOn = m_checkerChk && m_checkerChk->isChecked();
    const bool diffOn = m_diffOverlayChk && m_diffOverlayChk->isChecked();
    const bool customGrid = m_layoutCombo && m_layoutCombo->currentIndex() == 6;
    auto show = [](QWidget *widget, bool visible, bool enabled)
    {
        if (!widget)
            return;
        // Reserve the slot while hidden: toggling a mode must not change the
        // toolbar's minimum size, or the canvas resize would refit and drop
        // the shared zoom/pan.
        QSizePolicy policy = widget->sizePolicy();
        if (!policy.retainSizeWhenHidden())
        {
            policy.setRetainSizeWhenHidden(true);
            widget->setSizePolicy(policy);
        }
        widget->setVisible(visible);
        widget->setEnabled(enabled);
    };
    show(m_overlayAlphaSlider, overlayOn, overlayOn);
    show(m_overlayAlphaLabel, overlayOn, overlayOn);
    show(m_checkerSizeSlider, checkerOn, checkerOn);
    show(m_checkerSizeLabel, checkerOn, checkerOn);
    show(m_thresholdSlider, diffOn, diffOn);
    show(m_thresholdLabel, diffOn, diffOn);
    show(m_autoThresholdBtn, diffOn, diffOn && m_hasSuggestedThreshold);
    show(findChild<QLabel *>(QStringLiteral("diffThresholdCaption")), diffOn, diffOn);
    show(m_diffGainCombo, diffOn, diffOn);
    show(findChild<QLabel *>(QStringLiteral("diffGainCaption")), diffOn, diffOn);
    show(m_gridColsSpin, customGrid, customGrid);
    show(findChild<QLabel *>(QStringLiteral("compareColumnsCaption")), customGrid, customGrid);
}

void CompareWorkspace::onAutoThresholdClicked()
{
    if (!m_thresholdSlider || !m_hasSuggestedThreshold)
        return;
    const int value = static_cast<int>(m_suggestedThreshold);
    m_thresholdSlider->setValue(value);
    showCompareStatus(tr("Applied auto threshold %1").arg(value));
}

void CompareWorkspace::resetSuggestedThreshold()
{
    m_suggestedThreshold = 0;
    m_hasSuggestedThreshold = false;
    if (!m_autoThresholdBtn)
        return;
    m_autoThresholdBtn->setEnabled(false);
    m_autoThresholdBtn->setToolTip(tr("Automatically compute the best separation threshold from the difference distribution (Otsu)"));
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
        tr("Automatically compute the best separation threshold from the difference distribution (Otsu suggestion: %1)").arg(clamped));
    const bool diffOn = m_diffOverlayChk && m_diffOverlayChk->isChecked();
    m_autoThresholdBtn->setEnabled(diffOn);
}

void CompareWorkspace::addSnapshotButton(QHBoxLayout *toolLayout)
{
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("compareSnapshotButton"));
    button->setText(tr("Screenshot"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolTip(tr("Copy or save the current comparison view"));
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
