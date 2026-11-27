// CompareWorkspace control construction split from the lifecycle constructor.
#include "ThemeMetrics.h"
#include "compare_display_manager.h"
#include "compareworkspace_p.h"

#include "Theme.h"
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
    if (d->syncRotateChk && d->syncRotateChk->isChecked() != on)
    {
        const QSignalBlocker blocker(d->syncRotateChk);
        d->syncRotateChk->setChecked(on);
    }
    if (d->syncRotatePanelChk && d->syncRotatePanelChk->isChecked() != on)
    {
        const QSignalBlocker blocker(d->syncRotatePanelChk);
        d->syncRotatePanelChk->setChecked(on);
    }
    if (QAction *action = findChild<QAction *>(QStringLiteral("syncRotateAction")))
        action->setChecked(on);
    showCompareStatus(
        on ? tr("Sync rotation enabled: rotation applies to all images")
           : tr("Sync rotation disabled: rotation applies only to the selected image"));
}

void CompareWorkspace::setEditCellIndex(int cellIdx)
{
    m_hoverIdx = -1;
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
    d->syncZoomChk = new QCheckBox(mviewer::cw::compareShortcutText("Z"), this);
    d->syncZoomChk->setObjectName("syncZoomCheck");
    d->syncZoomChk->setChecked(true);
    d->syncZoomChk->setToolTip(tr("Sync zoom across all panes (Z)"));
    d->syncDragChk = new QCheckBox(mviewer::cw::compareShortcutText("D"), this);
    d->syncDragChk->setObjectName("syncDragCheck");
    d->syncDragChk->setChecked(true);
    d->syncDragChk->setToolTip(tr("Sync drag across all panes (D)"));
    d->syncRotateChk = new QCheckBox(mviewer::cw::compareShortcutText("Alt+R"), this);
    d->syncRotateChk->setObjectName("syncRotateCheck");
    d->syncRotateChk->setToolTip(tr("When checked, rotation and flip are applied in sync to all "
                                   "images being compared (Alt+R)"));
    d->syncRotateChk->setChecked(m_syncRotate);

    auto applySync = [this](bool) { update(); };
    connect(d->syncZoomChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncZoom = on;
                if (m_displayManager)
                    m_displayManager->setSyncZoom(on);
                m_engine.setSyncMode(m_syncZoom, m_syncDrag);
                applySync(on);
            });
    connect(d->syncDragChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncDrag = on;
                if (m_displayManager)
                    m_displayManager->setSyncDrag(on);
                m_engine.setSyncMode(m_syncZoom, m_syncDrag);
                applySync(on);
            });
    connect(d->syncRotateChk, &QCheckBox::toggled, this, &CompareWorkspace::setSyncRotate);
}

void CompareWorkspace::buildModeControls(QHBoxLayout *modeLayout, QHBoxLayout *viewLayout)
{
    // H5: "Uniform Pixel Scale" — align every pane at the same zoom so images of
    // different resolutions can be compared 1:1 (pixel-corresponding). Independent
    // of the sync-zoom toggle; when on, Fit uses the shared (minimum) scale for all.
    d->uniformScaleChk = new QCheckBox(tr("Uniform Pixel Scale"), this);
    d->uniformScaleChk->setChecked(false);
    d->uniformScaleChk->setToolTip(tr("Show all panes at the same scale so images of different "
                                     "resolutions stay 1:1 pixel-aligned"));
    connect(d->uniformScaleChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_uniformScale = on;
                if (m_displayManager)
                    m_displayManager->setUniformScale(on);
                fitAll();
                update();
            });
    viewLayout->addWidget(d->uniformScaleChk);

    // M14-3: blink (flicker) compare — rapid toggle between base and target.
    // Click the button (or press B) to start/stop rapid blinking.
    d->blinkChk = new QCheckBox(mviewer::cw::compareShortcutText("B"), this);
    d->blinkChk->setObjectName("blinkCompareToggle");
    d->blinkChk->setEnabled(false);
    d->blinkChk->setToolTip(tr("Click to start/stop rapid blink (Shortcut: B)"));
    connect(d->blinkChk, &QCheckBox::toggled, this,
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
                        exclusiveMode(d->blinkChk);
                        updateCanvasModeVisibility();
                    }
                    startBlink(150); // fast flicker
                }
                else
                    stopBlink();
            });
    modeLayout->addWidget(d->blinkChk);

    // P0-4: split / swipe compare for exactly two images.
    d->splitChk = new QCheckBox(mviewer::cw::compareShortcutText("S"), this);
    d->splitChk->setEnabled(false);
    // M24 (B#8): a disabled control must say why it is unavailable.
    d->splitChk->setToolTip(
        tr("Available only with 2 images: side-by-side comparison (Shortcut: S)"));
    connect(d->splitChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                {
                    exclusiveMode(d->splitChk);
                    if (m_diffRenderer)
                        m_diffRenderer->setBlendMode(mviewer::ui::DiffBlendMode::SideBySideSplit);
                }
                updateCanvasModeVisibility();
            });
    modeLayout->addWidget(d->splitChk);

    m_swipeChk = new QCheckBox(mviewer::cw::compareShortcutText("W"), this);
    m_swipeChk->setEnabled(false);
    m_swipeChk->setToolTip(
        tr("Available only with 2 images: sliding split comparison (Shortcut: W)"));
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
    auto *thresholdLabel = new QLabel(tr("Threshold:"), this);
    thresholdLabel->setObjectName("diffThresholdCaption");
    toolLayout->addWidget(thresholdLabel);
    d->thresholdSlider = new QSlider(Qt::Horizontal, this);
    d->thresholdSlider->setObjectName("diffThresholdSlider");
    d->thresholdSlider->setRange(0, 255);
    d->thresholdSlider->setValue(0);
    d->thresholdSlider->setMaximumWidth(120);
    d->thresholdSlider->setToolTip(
        tr("Difference threshold: pixels below this value will be hidden"));
    d->thresholdSlider->setEnabled(false);
    auto *sliderTimer = new QTimer(this);
    sliderTimer->setSingleShot(true);
    sliderTimer->setInterval(35);
    connect(sliderTimer, &QTimer::timeout, this, &CompareWorkspace::refreshAllDiffOverlays);
    connect(d->thresholdSlider, &QSlider::valueChanged, this,
            [this, sliderTimer](int value)
            {
                m_thresholdValue = static_cast<uint8_t>(value);
                if (m_diffRenderer)
                    m_diffRenderer->setThreshold(m_thresholdValue);
                if (!d->thresholdSlider->isSliderDown())
                    refreshAllDiffOverlays();
                else
                    sliderTimer->start();
            });
    connect(d->thresholdSlider, &QSlider::sliderReleased, this,
            [this, sliderTimer]()
            {
                sliderTimer->stop();
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(d->thresholdSlider);
    d->thresholdLabel = new QLabel("0", this);
    d->thresholdLabel->setObjectName("diffThresholdValueLabel");
    d->thresholdLabel->setMinimumWidth(24);
    connect(d->thresholdSlider, &QSlider::valueChanged, this,
            [this](int value) { d->thresholdLabel->setText(QString::number(value)); });
    toolLayout->addWidget(d->thresholdLabel);
    d->autoThresholdBtn = new QPushButton(tr("Auto"), this);
    d->autoThresholdBtn->setObjectName("diffAutoThresholdButton");
    d->autoThresholdBtn->setMaximumWidth(42);
    resetSuggestedThreshold();
    connect(d->autoThresholdBtn, &QPushButton::clicked, this,
            &CompareWorkspace::onAutoThresholdClicked);
    toolLayout->addWidget(d->autoThresholdBtn);
    // A-4.6: Diff highlight mode (red diffs / gray similar).
    d->diffOverlayChk = new QCheckBox(tr("Show Difference"), this);
    d->diffOverlayChk->setObjectName("diffOverlayToggle");
    d->diffOverlayChk->setChecked(false);
    d->diffOverlayChk->setEnabled(false);
    d->diffOverlayChk->setToolTip(tr("Explicitly overlay the difference heatmap; normal Compare "
                                    "always shows the original image"));
    connect(d->diffOverlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_diffOverlayVisible = on;
                m_diffOverlayPromoted = false;
                syncContextualCompareControls();
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(d->diffOverlayChk);

    d->diffHighlightChk = new QCheckBox(mviewer::cw::compareShortcutText("H"), this);
    d->diffHighlightChk->setObjectName("diffHighlightToggle");
    d->diffHighlightChk->setEnabled(false);
    d->diffHighlightChk->setToolTip(
        tr("Highlight differences in red; show similar areas in grayscale (H)"));
    connect(d->diffHighlightChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                // Highlighting without visualization is invisible. Promote
                // the display toggle first, but keep this as one latest-wins
                // refresh so the worker never publishes the intermediate
                // non-highlighted overlay.
                if (on && d->diffOverlayChk && !d->diffOverlayChk->isChecked())
                {
                    const QSignalBlocker blocker(d->diffOverlayChk);
                    d->diffOverlayChk->setChecked(true);
                    m_diffOverlayVisible = true;
                    m_diffOverlayPromoted = true;
                }
                // Off restores the plain image when highlight was what turned
                // the heatmap on; otherwise the right pane kept the diff view.
                if (!on && m_diffOverlayPromoted && d->diffOverlayChk)
                {
                    const QSignalBlocker blocker(d->diffOverlayChk);
                    d->diffOverlayChk->setChecked(false);
                    m_diffOverlayVisible = false;
                    m_diffOverlayPromoted = false;
                }
                m_diffHighlight = on;
                syncContextualCompareControls();
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(d->diffHighlightChk);

    auto *gainLabel = new QLabel(tr("Gain:"), this);
    gainLabel->setObjectName("diffGainCaption");
    toolLayout->addWidget(gainLabel);
    d->diffGainCombo = new QComboBox(this);
    d->diffGainCombo->setObjectName("diffGainCombo");
    d->diffGainCombo->addItems({"1x", "2x", "4x", "8x", "16x", "32x"});
    d->diffGainCombo->setCurrentIndex(0);
    d->diffGainCombo->setEnabled(false);
    d->diffGainCombo->setToolTip(
        tr("Difference amplification (for inspecting faint noise and compression residuals)"));
    connect(d->diffGainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx)
            {
                static const double kGains[] = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
                m_diffGain = (idx >= 0 && idx < 6) ? kGains[idx] : 1.0;
                refreshAllDiffOverlays();
            });
    toolLayout->addWidget(d->diffGainCombo);

    buildPixelLinkControls(toolLayout);
}

void CompareWorkspace::buildPixelLinkControls(QHBoxLayout *toolLayout)
{
    // A-4.3: Pixel Link — mark corresponding points across cells.
    d->pixelLinkChk = new QCheckBox(mviewer::cw::compareShortcutText("L"), this);
    d->pixelLinkChk->setObjectName("pixelLinkToggle");
    d->pixelLinkChk->setEnabled(false);
    d->pixelLinkChk->setToolTip(tr("When enabled, click an image to add markers showing each "
                                  "image's RGB and the difference (L)"));
    connect(d->pixelLinkChk, &QCheckBox::toggled, this, &CompareWorkspace::onPixelLinkToggled);
    toolLayout->addWidget(d->pixelLinkChk);
    d->clearLinksBtn = new QPushButton(tr("Clear Markers"), this);
    d->clearLinksBtn->setEnabled(false);
    d->clearLinksBtn->setToolTip(clearLinkTooltip());
    connect(d->clearLinksBtn, &QPushButton::clicked, this, &CompareWorkspace::clearLinkPoints);
    toolLayout->addWidget(d->clearLinksBtn);
    d->linkInfoLabel = new QLabel(tr("Markers: 0"), this);
    d->linkInfoLabel->setMinimumWidth(48);
    d->linkInfoLabel->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
    toolLayout->addWidget(d->linkInfoLabel);
}

void CompareWorkspace::buildViewControls(QHBoxLayout *viewLayout)
{
    // P0 #③: explicit multi-layout selector (auto / single / 2-4 columns / row).
    auto *layoutLabel = new QLabel(tr("Layout:"), this);
    viewLayout->addWidget(layoutLabel);
    d->layoutCombo = new QComboBox(this);
    d->layoutCombo->addItems({tr("Auto"), tr("1 column"), tr("2 columns"), tr("3 columns"),
                             tr("4 columns"), tr("1 row"), tr("Custom")});
    d->layoutCombo->setCurrentIndex(0);
    connect(d->layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CompareWorkspace::onLayoutChanged);
    viewLayout->addWidget(d->layoutCombo);

    // A-4.2: custom grid is column-driven. CompareEngine derives the row count.
    auto *columnsLabel = new QLabel(tr("Columns:"), this);
    columnsLabel->setObjectName("compareColumnsCaption");
    columnsLabel->setToolTip(tr("Only set the column count; the row count is derived automatically "
                                "from the current image count"));
    viewLayout->addWidget(columnsLabel);
    d->gridColsSpin = new QSpinBox(this);
    d->gridColsSpin->setRange(1, 8);
    d->gridColsSpin->setValue(2);
    d->gridColsSpin->setEnabled(false);
    d->gridColsSpin->setObjectName("compareColumnsSpin");
    d->gridColsSpin->setToolTip(tr("Only set the column count; the row count is derived "
                                  "automatically from the current image count"));
    connect(d->gridColsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onCustomGridChanged);
    viewLayout->addWidget(d->gridColsSpin);
    d->layoutStatusLabel = new QLabel(this);
    d->layoutStatusLabel->setObjectName("compareGridStatus");
    d->layoutStatusLabel->setToolTip(
        tr("Current grid: row count is derived from the image count and column count"));
    viewLayout->addWidget(d->layoutStatusLabel);

    // P0 #③: inspector + histogram side panel toggle.
    d->sideChk = new QCheckBox(mviewer::cw::compareShortcutText("I"), this);
    d->sideChk->setObjectName("analysisPanelToggle");
    d->sideChk->setChecked(false);
    connect(d->sideChk, &QCheckBox::toggled, this, &CompareWorkspace::onSideToggled);
    viewLayout->addWidget(d->sideChk);

    // M16.1: cursor-sync crosshair (n/n). When on, hovering any cell draws a
    // crosshair at the same image-space point across all compared cells, and the
    // inspector samples every cell at that point.
    d->crosshairChk = new QCheckBox(mviewer::cw::compareShortcutText("R"), this);
    d->crosshairChk->setChecked(false);
    d->crosshairChk->setObjectName("syncCrosshairToggle");
    connect(d->crosshairChk, &QCheckBox::toggled, this,
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
    viewLayout->addWidget(d->crosshairChk);

    // M16.1: focus-lock / reference pin (n/1). Locks a cell as the comparison
    // reference; diff overlays and inspector deltas use it as the base.
    d->focusBtn = new QPushButton(tr("Lock Reference"), this);
    d->focusBtn->setCheckable(true);
    connect(d->focusBtn, &QPushButton::toggled, this,
            [this](bool on)
            {
                // Lock the focus on the cell under the cursor (fall back to cell 0 when
                // none is hovered). Toggling off clears the lock.
                const int idx = (on && m_hoverIdx >= 0) ? m_hoverIdx : (on ? 0 : m_focusIndex);
                onFocusRequested(on ? idx : -1);
            });
    viewLayout->addWidget(d->focusBtn);
    d->focusLabel = new QLabel(tr("Reference: —"), this);
    d->focusLabel->setMinimumWidth(48);
    viewLayout->addWidget(d->focusLabel);

    // M57: Compare is static by default, but a focused animated/page source
    // can be switched to an explicit frame/page without changing the Browse
    // file index. The control is hidden for ordinary single-frame sources.
    d->frameLabel = new QLabel(tr("Frame/Page:"), this);
    d->frameSpin = new QSpinBox(this);
    d->frameSpin->setObjectName(QStringLiteral("compareFrameSpin"));
    d->frameSpin->setRange(1, 1);
    d->frameSpin->setVisible(false);
    d->frameSpin->setEnabled(false);
    d->frameLabel->setVisible(false);
    connect(d->frameSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CompareWorkspace::onFrameControlChanged);
    viewLayout->addWidget(d->frameLabel);
    viewLayout->addWidget(d->frameSpin);

    viewLayout->addStretch(1);
}

void CompareWorkspace::buildToolbarActions(QHBoxLayout *toolLayout)
{
    d->paneHistOverlayChk = new QCheckBox(tr("Histogram"), this);
    d->paneHistOverlayChk->setObjectName("paneHistogramOverlayToggle");
    d->paneHistOverlayChk->setChecked(m_paneHistOverlay);
    d->paneHistOverlayChk->setToolTip(
        tr("Translucently overlay the current histogram at the top-left of each comparison image"));
    connect(d->paneHistOverlayChk, &QCheckBox::toggled, this,
            &CompareWorkspace::onPaneHistOverlayToggled);
    toolLayout->addWidget(d->paneHistOverlayChk);

    d->filenameOverlayChk = new QCheckBox(tr("File Name"), this);
    d->filenameOverlayChk->setObjectName("compareFilenameOverlayToggle");
    d->filenameOverlayChk->setChecked(m_filenameOverlay);
    d->filenameOverlayChk->setToolTip(
        tr("Show or hide the filename overlaid on top of each comparison image (long names "
           "wrap onto several lines)"));
    connect(d->filenameOverlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                m_filenameOverlay = on;
                applyFilenameOverlays();
                positionCellHists();
                m_canvasBaseKey.clear();
                update();
            });
    toolLayout->addWidget(d->filenameOverlayChk);

    d->pixelGridChk = new QCheckBox(tr("Pixel Grid"), this);
    d->pixelGridChk->setObjectName("comparePixelGridToggle");
    d->pixelGridChk->setChecked(m_pixelGridEnabled);
    d->pixelGridChk->setToolTip(tr("Show pixel inspection grid when zoomed to 800% or above"));
    connect(d->pixelGridChk, &QCheckBox::toggled, this, &CompareWorkspace::setPixelGridEnabled);
    toolLayout->addWidget(d->pixelGridChk);

    d->channelCombo = new QComboBox(this);
    d->channelCombo->setObjectName("compareChannelCombo");
    d->channelCombo->setMaximumWidth(125);
    d->channelCombo->addItem(tr("RGB (Shift+1)"), static_cast<int>(mviewer::OverlayMode::None));
    d->channelCombo->addItem(tr("R (Shift+2)"), static_cast<int>(mviewer::OverlayMode::ChannelR));
    d->channelCombo->addItem(tr("G (Shift+3)"), static_cast<int>(mviewer::OverlayMode::ChannelG));
    d->channelCombo->addItem(tr("B (Shift+4)"), static_cast<int>(mviewer::OverlayMode::ChannelB));
    d->channelCombo->addItem(tr("Y (Shift+5)"), static_cast<int>(mviewer::OverlayMode::ChannelY));
    d->channelCombo->addItem(tr("V (Shift+6)"), static_cast<int>(mviewer::OverlayMode::ChannelV));
    d->channelCombo->setToolTip(tr("Isolate R/G/B/Y/V channels (Shift+1…6)"));
    connect(d->channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (!d->channelCombo)
                    return;
                const auto mode =
                    static_cast<mviewer::OverlayMode>(d->channelCombo->currentData().toInt());
                setOverlayMode(mode);
            });
    toolLayout->addWidget(d->channelCombo);

    // M16.6: layout presets save/load + swap panes (sync bar right side)
    d->savePresetBtn = new QPushButton(tr("Save Layout"), this);
    d->savePresetBtn->setToolTip(tr("Save the current layout as a preset"));
    connect(d->savePresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onSavePreset);
    toolLayout->addWidget(d->savePresetBtn);

    d->loadPresetBtn = new QPushButton(tr("Load Layout"), this);
    d->loadPresetBtn->setToolTip(tr("Load a layout from a preset file"));
    connect(d->loadPresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onLoadPreset);
    toolLayout->addWidget(d->loadPresetBtn);

    d->swapBtn = new QPushButton(mviewer::cw::compareShortcutText("X"), this);
    d->swapBtn->setObjectName("compareSwapPanesButton");
    d->swapBtn->setToolTip(tr("Swap A/B panes (Shortcut: X)"));
    d->swapBtn->setEnabled(false);
    connect(d->swapBtn, &QPushButton::clicked, this, &CompareWorkspace::onSwapPanes);
    toolLayout->addWidget(d->swapBtn);

    d->temporaryCompareButton = new QPushButton(mviewer::cw::compareShortcutText("Space"), this);
    d->temporaryCompareButton->setObjectName("temporaryCompareButton");
    d->temporaryCompareButton->setToolTip(temporaryCompareTooltip());
    d->temporaryCompareButton->setEnabled(false);
    connect(d->temporaryCompareButton, &QPushButton::pressed, this,
            &CompareWorkspace::beginClassicTemporaryCompare);
    connect(d->temporaryCompareButton, &QPushButton::released, this,
            &CompareWorkspace::endTemporaryCompare);
    d->temporaryCompareButton->installEventFilter(this);
    if (auto *w = window())
        w->installEventFilter(this);
    toolLayout->addWidget(d->temporaryCompareButton);

    addFitWindowButton(toolLayout);
    addSnapshotButton(toolLayout);

    // P1 #④: Analyze & export buttons in the compare toolbar.
    d->analyzeBtn = new QPushButton(tr("Analyze"), this);
    d->analyzeBtn->setObjectName("analyzeCompareButton");
    d->analyzeBtn->setEnabled(false);
    d->analyzeBtn->setToolTip(tr("Open Comparison Inspection Panel"));
    connect(d->analyzeBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (d->sideChk)
                    d->sideChk->setChecked(true);
                emit analyzeCurrent();
            });
    toolLayout->addWidget(d->analyzeBtn);

    d->exportReportBtn = new QPushButton(tr("Export Report"), this);
    d->exportReportBtn->setObjectName("exportCompareReportButton");
    d->exportReportBtn->setEnabled(false);
    d->exportReportBtn->setToolTip(tr("Export comparison results as an HTML/Markdown/JSON report"));
    connect(d->exportReportBtn, &QPushButton::clicked, this,
            &CompareWorkspace::exportReportRequested);
    toolLayout->addWidget(d->exportReportBtn);
    toolLayout->addStretch(1);
}

void CompareWorkspace::addFitWindowButton(QHBoxLayout *toolLayout)
{
    // Fit to window (F). Uniform pixel scale decides shared vs per-pane scale. The view
    // row is already at the 1100px budget, so this stays on the actions row.
    auto *fitBtn = new QPushButton(mviewer::cw::compareShortcutText("F"), this);
    fitBtn->setObjectName("fitWindowButton");
    fitBtn->setToolTip(tr("Fit to window (Shortcut: F): when \"Uniform Pixel Scale\" is checked, "
                          "all panes fit at the same scale;"
                          "when unchecked, each pane fits its own resolution so images with "
                          "similar FOV/aspect appear similar in size"));
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
    auto *lay = new QHBoxLayout(strip);
    lay->setContentsMargins(mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace4,
                            mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace4);
    lay->setSpacing(8);

    d->metricLabel = new QLabel(strip);
    d->metricLabel->setObjectName("diffMetricsLabel");
    d->metricLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    d->metricLabel->setWordWrap(false);
    d->metricLabel->setFixedHeight(24);
    d->metricLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    presentDiffMetrics(tr("PSNR: —    SSIM: —"));
    lay->addWidget(d->metricLabel, 0);

    d->autoAlignChk = new QCheckBox(tr("Align"), strip);
    d->autoAlignChk->setObjectName("autoAlignBeforeDiffToggle");
    d->autoAlignChk->setToolTip(tr("Before comparison, auto-align by integer-pixel translation to "
                                  "remove shift error, then compute PSNR/SSIM"));
    m_autoAlignBeforeDiff = QSettings().value("autoAlignBeforeDiff", false).toBool();
    d->autoAlignChk->setChecked(m_autoAlignBeforeDiff);
    connect(d->autoAlignChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                QSettings().setValue("autoAlignBeforeDiff", on);
                setAutoAlignBeforeDiff(on);
            });
    lay->addWidget(d->autoAlignChk);

    d->compareStatusLabel = new QLabel(strip);
    d->compareStatusLabel->setObjectName("compareStatusLabel");
    lay->addWidget(d->compareStatusLabel, 1);

    d->exitBtn = new QPushButton(mviewer::cw::compareShortcutText("Esc"), strip);
    d->exitBtn->setObjectName("exitCompareButton");
    d->exitBtn->setToolTip(
        tr("Close the comparison window (Esc; with a selection, Esc clears it first)"));
    connect(d->exitBtn, &QPushButton::clicked, this, &CompareWorkspace::closeCompareHost);
    lay->addWidget(d->exitBtn);
    return strip;
}

void CompareWorkspace::syncContextualCompareControls()
{
    // Mode sliders appear only while that mode is on, so the idle chrome stays one row.
    const bool overlayOn = d->overlayChk && d->overlayChk->isChecked();
    const bool checkerOn = d->checkerChk && d->checkerChk->isChecked();
    const bool diffOn = d->diffOverlayChk && d->diffOverlayChk->isChecked();
    const bool customGrid = d->layoutCombo && d->layoutCombo->currentIndex() == 6;
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
    show(d->overlayAlphaSlider, overlayOn, overlayOn);
    show(d->overlayAlphaLabel, overlayOn, overlayOn);
    show(d->checkerSizeSlider, checkerOn, checkerOn);
    show(d->checkerSizeLabel, checkerOn, checkerOn);
    show(d->thresholdSlider, diffOn, diffOn);
    show(d->thresholdLabel, diffOn, diffOn);
    show(d->autoThresholdBtn, diffOn, diffOn && m_hasSuggestedThreshold);
    show(findChild<QLabel *>(QStringLiteral("diffThresholdCaption")), diffOn, diffOn);
    show(d->diffGainCombo, diffOn, diffOn);
    show(findChild<QLabel *>(QStringLiteral("diffGainCaption")), diffOn, diffOn);
    show(d->gridColsSpin, customGrid, customGrid);
    show(findChild<QLabel *>(QStringLiteral("compareColumnsCaption")), customGrid, customGrid);
}

void CompareWorkspace::onAutoThresholdClicked()
{
    if (!d->thresholdSlider || !m_hasSuggestedThreshold)
        return;
    const int value = static_cast<int>(m_suggestedThreshold);
    d->thresholdSlider->setValue(value);
    showCompareStatus(tr("Applied auto threshold %1").arg(value));
}

void CompareWorkspace::resetSuggestedThreshold()
{
    m_suggestedThreshold = 0;
    m_hasSuggestedThreshold = false;
    if (!d->autoThresholdBtn)
        return;
    d->autoThresholdBtn->setEnabled(false);
    d->autoThresholdBtn->setToolTip(tr("Automatically compute the best separation threshold from "
                                      "the difference distribution (Otsu)"));
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
    if (!d->autoThresholdBtn)
        return;
    d->autoThresholdBtn->setToolTip(tr("Automatically compute the best separation threshold from "
                                      "the difference distribution (Otsu suggestion: %1)")
                                       .arg(clamped));
    const bool diffOn = d->diffOverlayChk && d->diffOverlayChk->isChecked();
    d->autoThresholdBtn->setEnabled(diffOn);
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

bool CompareWorkspace::autoAlignBeforeDiff() const
{
    return m_autoAlignBeforeDiff;
}
