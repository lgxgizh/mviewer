// CompareWorkspace edit panel: adjustments, metrics, per-pane histograms, presets (M20 P0#2).
#include "Theme.h"
#include "ThemeMetrics.h"
#include "compareworkspace_p.h"
#include "compareworkspace_preset_sanitize.h"
#include "domain/CompareSession.h"
#include <QFontMetrics>

#include "runtime_storage.h"
#include "widgets/infooverlay.h"

#include <QSaveFile>
#include <algorithm>
#include <initializer_list>

namespace
{
bool anyAdjustSliderDown(std::initializer_list<QSlider *> sliders)
{
    for (QSlider *slider : sliders)
    {
        if (slider && slider->isSliderDown())
            return true;
    }
    return false;
}
} // namespace

mviewer::core::CompareAdjustmentState CompareWorkspace::reportAdjustment(const CellAdjust &adjust)
{
    mviewer::core::CompareAdjustmentState report;
    report.brightness = adjust.brightness;
    report.contrast = adjust.contrast;
    report.gamma = adjust.gamma;
    report.redGain = adjust.rGain;
    report.blueGain = adjust.bGain;
    report.rotation = adjust.rotation;
    report.flipH = adjust.flipH;
    report.flipV = adjust.flipV;
    report.hasCrop = adjust.hasCrop;
    report.cropX = adjust.cropX;
    report.cropY = adjust.cropY;
    report.cropW = adjust.cropW;
    report.cropH = adjust.cropH;
    return report;
}

mviewer::core::CellImageAdjust CompareWorkspace::toCellImageAdjust(const CellAdjust &adjust)
{
    mviewer::core::CellImageAdjust out;
    out.brightness = adjust.brightness;
    out.contrast = adjust.contrast;
    out.gamma = adjust.gamma;
    out.rGain = adjust.rGain;
    out.bGain = adjust.bGain;
    out.rotation = adjust.rotation;
    out.flipH = adjust.flipH;
    out.flipV = adjust.flipV;
    out.hasCrop = adjust.hasCrop;
    out.cropX = adjust.cropX;
    out.cropY = adjust.cropY;
    out.cropW = adjust.cropW;
    out.cropH = adjust.cropH;
    out.usmEnabled = adjust.usmEnabled;
    out.usmAmount = adjust.usmAmount;
    out.usmRadius = adjust.usmRadius;
    out.usmThreshold = adjust.usmThreshold;
    out.usmLumaOnly = adjust.usmLumaOnly;
    return out;
}

bool CompareWorkspace::CellAdjust::isIdentity() const
{
    return CompareWorkspace::toCellImageAdjust(*this).isIdentity();
}

ImageData CompareWorkspace::applyAdjusts(const ImageData &src, const CellAdjust &a,
                                         const std::function<bool()> &cancelled)
{
    return mviewer::core::applyCellImageAdjust(src, toCellImageAdjust(a), cancelled);
}

void CompareWorkspace::buildEditPanel(QVBoxLayout *sideLayout)
{
    m_editPanel = new QWidget(m_sidePanel);
    auto *editLay = new QVBoxLayout(m_editPanel);
    editLay->setContentsMargins(0, 4, 0, 0);
    editLay->setSpacing(mviewer::ui::metrics::kSpace4);

    m_editLabel = new QLabel(tr("— Select a pane to edit —"), m_editPanel);
    m_editLabel->setStyleSheet(
        QStringLiteral("font-weight:bold;color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextPrimary)));
    editLay->addWidget(m_editLabel);
    auto *previewHint = new QLabel(tr("Adjustments are preview-only and do not overwrite the "
                                      "original file; export as a new PNG or TIFF"),
                                   m_editPanel);
    previewHint->setObjectName("previewOnlyHint");
    previewHint->setStyleSheet("color:#f0c060;font-size:11px;");
    previewHint->setWordWrap(true);
    editLay->addWidget(previewHint);

    // Brightness slider [-255, 255]; 0=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Brightness"), m_editPanel));
        m_brightSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_brightSlider->setObjectName("brightnessSlider");
        m_brightSlider->setRange(-255, 255);
        m_brightSlider->setValue(0);
        m_brightVal = new QLabel("0", m_editPanel);
        m_brightVal->setMinimumWidth(30);
        row->addWidget(m_brightSlider);
        row->addWidget(m_brightVal);
        editLay->addLayout(row);
        connect(m_brightSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_brightVal->setText(QString::number(v));
                    onAdjChanged();
                });
    }

    // Contrast slider [0..300] → float [0.0..3.0]; 100=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Contrast"), m_editPanel));
        m_contrastSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_contrastSlider->setRange(0, 300);
        m_contrastSlider->setValue(100);
        m_contrastVal = new QLabel("1.0", m_editPanel);
        m_contrastVal->setMinimumWidth(30);
        row->addWidget(m_contrastSlider);
        row->addWidget(m_contrastVal);
        editLay->addLayout(row);
        connect(m_contrastSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_contrastVal->setText(QString::number(v / 100.0, 'f', 2));
                    onAdjChanged();
                });
    }

    // Gamma slider [5..800] → float [0.05..8.0]; 100=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Gamma"), m_editPanel));
        m_gammaSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_gammaSlider->setRange(5, 800);
        m_gammaSlider->setValue(100);
        m_gammaVal = new QLabel("1.00", m_editPanel);
        m_gammaVal->setMinimumWidth(30);
        row->addWidget(m_gammaSlider);
        row->addWidget(m_gammaVal);
        editLay->addLayout(row);
        connect(m_gammaSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_gammaVal->setText(QString::number(v / 100.0, 'f', 2));
                    onAdjChanged();
                });
    }

    buildSecondaryEditControls(editLay);

    sideLayout->addWidget(m_editPanel);
}

void CompareWorkspace::buildSecondaryEditControls(QVBoxLayout *editLay)
{
    auto addGain =
        [this, editLay](const QString &label, QSlider *&slider, QLabel *&value, auto callback)
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, m_editPanel));
        slider = new QSlider(Qt::Horizontal, m_editPanel);
        slider->setRange(1, 500);
        slider->setValue(100);
        value = new QLabel("1.00", m_editPanel);
        value->setMinimumWidth(30);
        row->addWidget(slider);
        row->addWidget(value);
        editLay->addLayout(row);
        connect(slider, &QSlider::valueChanged, this, callback);
    };
    addGain(tr("WB R"), m_rGainSlider, m_rGainVal,
            [this](int v)
            {
                m_rGainVal->setText(QString::number(v / 100.0, 'f', 2));
                onAdjChanged();
            });
    addGain(tr("WB B"), m_bGainSlider, m_bGainVal,
            [this](int v)
            {
                m_bGainVal->setText(QString::number(v / 100.0, 'f', 2));
                onAdjChanged();
            });
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Rotate"), m_editPanel));
        auto *btnCCW = new QPushButton(tr("↺ Counterclockwise (Ctrl+Shift+R)"), m_editPanel);
        btnCCW->setObjectName("rotateCCWButton");
        btnCCW->setToolTip(
            tr("Rotate 90° counterclockwise (preview only, does not modify the original file)"));
        auto *btnCW = new QPushButton(tr("↻ Clockwise (Ctrl+R)"), m_editPanel);
        btnCW->setObjectName("rotateCWButton");
        btnCW->setToolTip(
            tr("Rotate 90° clockwise (preview only, does not modify the original file)"));
        m_rotVal = new QLabel("0°", m_editPanel);
        m_rotVal->setMinimumWidth(30);
        m_syncRotatePanelChk = new QCheckBox(tr("Sync all images (Alt+R)"), m_editPanel);
        m_syncRotatePanelChk->setObjectName("syncRotatePanelCheck");
        m_syncRotatePanelChk->setToolTip(tr("When checked, rotation and flip are applied in sync "
                                            "to all images being compared (Alt+R)"));
        m_syncRotatePanelChk->setChecked(m_syncRotate);
        connect(m_syncRotatePanelChk, &QCheckBox::toggled, this, &CompareWorkspace::setSyncRotate);
        row->addWidget(btnCCW);
        row->addWidget(btnCW);
        row->addWidget(m_rotVal);
        row->addWidget(m_syncRotatePanelChk);
        editLay->addLayout(row);
        connect(btnCCW, &QPushButton::clicked, this, [this]() { rotateCurrentCell(-90); });
        connect(btnCW, &QPushButton::clicked, this, [this]() { rotateCurrentCell(90); });
    }
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Flip"), m_editPanel));
        auto *btnFlipH = new QPushButton(tr("⇄ Horizontal (Ctrl+Shift+H)"), m_editPanel);
        btnFlipH->setObjectName("flipHButton");
        btnFlipH->setToolTip(tr("Flip current pane horizontally (Ctrl+Shift+H, preview only, does "
                                "not modify the original file)"));
        auto *btnFlipV = new QPushButton(tr("⇅ Vertical (Ctrl+Alt+V)"), m_editPanel);
        btnFlipV->setObjectName("flipVButton");
        btnFlipV->setToolTip(tr("Flip current pane vertically (Ctrl+Alt+V, preview only, does "
                                "not modify the original file)"));
        row->addWidget(btnFlipH);
        row->addWidget(btnFlipV);
        editLay->addLayout(row);
        connect(btnFlipH, &QPushButton::clicked, this, [this]() { flipCurrentCell(true); });
        connect(btnFlipV, &QPushButton::clicked, this, [this]() { flipCurrentCell(false); });
    }
    buildUsmControls(editLay);
    m_resetAdjBtn = new QPushButton(tr("Reset adjustments"), m_editPanel);
    m_resetAdjBtn->setObjectName("resetAdjustmentsButton");
    connect(m_resetAdjBtn, &QPushButton::clicked, this, &CompareWorkspace::onResetAdj);
    editLay->addWidget(m_resetAdjBtn);
    addAdjustedExportButton(editLay);
    for (QSlider *slider :
         {m_brightSlider, m_contrastSlider, m_gammaSlider, m_rGainSlider, m_bGainSlider,
          m_usmAmountSlider, m_usmRadiusSlider, m_usmThresholdSlider})
        connect(slider, &QSlider::sliderReleased, this, &CompareWorkspace::onAdjEditFinished);
}

void CompareWorkspace::onEditCellSelected(int cellIdx)
{
    // Defensive: every caller must pass a live pane index. A negative index used
    // to reach m_cellAdjusts[SIZE_MAX] through the resize/index pair below.
    if (cellIdx < 0 || cellIdx >= static_cast<int>(m_cellViews.size()))
        return;

    m_editIdx = cellIdx;
    updateFrameControl();

    // Resize adjustment vector if needed
    const int needed = cellIdx + 1;
    if (static_cast<int>(m_cellAdjusts.size()) < needed)
        m_cellAdjusts.resize(static_cast<size_t>(needed));

    const CellAdjust &a = m_cellAdjusts[static_cast<size_t>(cellIdx)];

    // Block signals while setting slider values to avoid triggering onAdjChanged
    {
        QSignalBlocker b(m_brightSlider);
        m_brightSlider->setValue(a.brightness);
    }
    m_brightVal->setText(QString::number(a.brightness));

    {
        QSignalBlocker b(m_contrastSlider);
        m_contrastSlider->setValue(static_cast<int>(a.contrast * 100.0f));
    }
    m_contrastVal->setText(QString::number(a.contrast, 'f', 2));

    {
        QSignalBlocker b(m_gammaSlider);
        m_gammaSlider->setValue(static_cast<int>(a.gamma * 100.0f));
    }
    m_gammaVal->setText(QString::number(a.gamma, 'f', 2));

    {
        QSignalBlocker b(m_rGainSlider);
        m_rGainSlider->setValue(static_cast<int>(a.rGain * 100.0f));
    }
    m_rGainVal->setText(QString::number(a.rGain, 'f', 2));

    {
        QSignalBlocker b(m_bGainSlider);
        m_bGainSlider->setValue(static_cast<int>(a.bGain * 100.0f));
    }
    m_bGainVal->setText(QString::number(a.bGain, 'f', 2));
    if (m_rotVal)
        m_rotVal->setText(QString::number(a.rotation) + "°");
    syncUsmControls(a);

    const ImageFrame *img = m_engine.imageAt(cellIdx);
    m_editLabel->setText(img ? QString::fromStdString(img->metadata().fileName)
                             : tr("Pane %1").arg(cellIdx + 1));
    // Selecting a pane does not change the cursor's sample position. Repaint the
    // Inspector from the cached hover coordinate so a pane click immediately
    // reflects the current adjusted display without requiring another mouse move.
    // M30: coalesced with any pending hover render.
    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0 && m_lastInspectY >= 0)
        requestInspectorUpdate(m_lastInspectX, m_lastInspectY);
}

void CompareWorkspace::onAdjChanged()
{
    if (m_editIdx < 0 || m_editIdx >= static_cast<int>(m_cellAdjusts.size()))
        return;

    CellAdjust &a = m_cellAdjusts[static_cast<size_t>(m_editIdx)];
    a.brightness = m_brightSlider->value();
    a.contrast = m_contrastSlider->value() / 100.0f;
    a.gamma = m_gammaSlider->value() / 100.0f;
    a.rGain = m_rGainSlider->value() / 100.0f;
    a.bGain = m_bGainSlider->value() / 100.0f;
    if (m_usmChk)
        a.usmEnabled = m_usmChk->isChecked();
    if (m_usmAmountSlider)
        a.usmAmount = m_usmAmountSlider->value() / 100.0;
    if (m_usmRadiusSlider)
        a.usmRadius = m_usmRadiusSlider->value() / 10.0;
    if (m_usmThresholdSlider)
        a.usmThreshold = m_usmThresholdSlider->value();
    if (m_usmLumaChk)
        a.usmLumaOnly = m_usmLumaChk->isChecked();

    applyAdjToCell(m_editIdx);

    const bool sliderDown = anyAdjustSliderDown({m_brightSlider, m_contrastSlider, m_gammaSlider,
                                                 m_rGainSlider, m_bGainSlider, m_usmAmountSlider,
                                                 m_usmRadiusSlider, m_usmThresholdSlider});
    if (!sliderDown)
    {
        onAdjEditFinished();
        update();
    }
}

void CompareWorkspace::onResetAdj()
{
    if (m_editIdx < 0 || m_editIdx >= static_cast<int>(m_cellAdjusts.size()))
        return;

    m_cellAdjusts[static_cast<size_t>(m_editIdx)] = CellAdjust{};
    onEditCellSelected(m_editIdx); // resync sliders

    onAdjChanged();
}

void CompareWorkspace::applyAdjToCell(int cellIdx)
{
    if (cellIdx < 0 || cellIdx >= static_cast<int>(m_cellViews.size()))
        return;

    const ImageFrame *img = m_engine.imageAt(cellIdx);
    if (!img || img->pixels().isNull())
        return;

    // M28 P1-01: live adjustment previews are async and cancellable. The old
    // synchronous applyAdjusts + toQImage now runs on an Analysis worker; this
    // method only requests a display batch for the one pane. The transform is
    // preserved on delivery (applyDisplayBatchResult) when dimensions match.
    scheduleDisplayMaterialization({cellIdx});
}

// ─── M16.5: Per-pane histogram toggle ────────────────────────────────────────

void CompareWorkspace::onPerPaneHistToggled(bool on)
{
    m_perPaneHist = on;
    refreshHistograms();
}

// ─── M16.7: adjusted-aware diff/metrics + per-pane histogram overlay ───────

ImageData CompareWorkspace::adjustedPixels(int cellIdx) const
{
    if (cellIdx < 0 || cellIdx >= m_engine.imageCount())
        return ImageData();
    const ImageFrame *img = m_engine.imageAt(cellIdx);
    if (!img || img->pixels().isNull())
        return ImageData();
    const CellAdjust a = (cellIdx >= 0 && cellIdx < static_cast<int>(m_cellAdjusts.size()))
                             ? m_cellAdjusts[static_cast<size_t>(cellIdx)]
                             : CellAdjust{};
    return applyAdjusts(img->pixels(), a);
}

mviewer::core::CompareReportInput CompareWorkspace::captureReportInput() const
{
    const int imageCount = m_engine.imageCount();
    mviewer::core::CompareReportInput input;
    input.images.resize(static_cast<size_t>(imageCount));
    input.referenceIndex = diffBaseIndex();
    input.threshold = m_thresholdValue;
    input.roi = m_lastSelection;

    for (int i = 0; i < imageCount; ++i)
    {
        auto &entry = input.images[static_cast<size_t>(i)];
        const ImageFrame *source = m_engine.imageAt(i);
        if (source)
        {
            entry.metadata = source->metadata();
            entry.pixels = source->pixels();
        }

        if (i < static_cast<int>(m_cellAdjusts.size()))
            entry.adjustment = reportAdjustment(m_cellAdjusts[static_cast<size_t>(i)]);
    }

    return input;
}

mviewer::core::CompareReportBundle CompareWorkspace::buildReportBundle() const
{
    return mviewer::core::buildCompareReportBundle(captureReportInput());
}

void CompareWorkspace::onAdjEditFinished()
{
    // This is the sole full analysis refresh path for adjustment edits. When
    // the side panel is visible one includeMain batch also covers the pane
    // overlays; when it is hidden only the edited pane is refreshed (any
    // still-empty overlays are coalesced into the same batch by scheduling).
    const bool analysisVisible = m_sideChk && m_sideChk->isChecked();
    if (analysisVisible)
        refreshHistograms();
    else if (m_paneHistOverlay)
        refreshCellHist(m_editIdx);
    refreshAllDiffOverlays();
    updateROISurfaces();
    scheduleROIMeasurement();
}

void CompareWorkspace::refreshCellHist(int idx)
{
    if (!m_paneHistOverlay || idx < 0 || idx >= static_cast<int>(m_cellHists.size()))
        return;
    scheduleHistogramRefresh(false, {idx});
}

void CompareWorkspace::positionCellHists()
{
    for (size_t i = 0; i < m_cellHists.size(); ++i)
    {
        HistogramWidget *hw = m_cellHists[i];
        if (!hw)
            continue;
        QWidget *frame = hw->parentWidget();
        if (!frame || i >= static_cast<size_t>(m_cellViews.size()) || !m_cellViews[i])
            continue;
        const QRect pane = m_cellViews[i]->geometry();
        // Keep the histogram clear of the floating pane header (index chip +
        // wrapped file name), which overlays the top-left of the image.
        QRect filenameBox;
        const QWidget *header =
            frame->parentWidget()
                ? frame->parentWidget()->findChild<QWidget *>(QStringLiteral("comparePaneHeader"),
                                                              Qt::FindDirectChildrenOnly)
                : nullptr;
        if (header && !header->isHidden())
            filenameBox = header->geometry().translated(-pane.topLeft());
        const QRect box =
            mviewer::ui::histogramOverlayRect(QRect(QPoint(0, 0), pane.size()), filenameBox);
        if (box.isEmpty())
        {
            frame->setVisible(false);
            continue;
        }
        frame->setGeometry(pane.left() + box.x(), pane.top() + box.y(), box.width(), box.height());
        frame->setVisible(m_paneHistOverlay);
        frame->raise();
    }
}

void CompareWorkspace::onPaneHistOverlayToggled(bool on)
{
    m_paneHistOverlay = on;
    for (HistogramWidget *hw : m_cellHists)
    {
        if (hw && hw->parentWidget())
            hw->parentWidget()->setVisible(on);
    }
    if (on)
    {
        std::vector<int> all;
        const int n = static_cast<int>(m_cellHists.size());
        all.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            all.push_back(i);
        scheduleHistogramRefresh(false, all);
        positionCellHists();
    }
}

// ─── M16.6: Layout presets save/load ─────────────────────────────────────────

void CompareWorkspace::ensurePresetDir()
{
    if (!m_presetDir.isEmpty())
        return;
    // Store presets under the app's cache/config directory
    const QString base = mviewer::runtime::writableDirectory(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return;
    m_presetDir = QDir(base).filePath(QStringLiteral("compare_presets"));
    if (!QDir().mkpath(m_presetDir))
        m_presetDir.clear();
}

void CompareWorkspace::onSavePreset()
{
    ensurePresetDir();
    const QString fileName =
        QFileDialog::getSaveFileName(this, tr("Save Comparison Layout"), m_presetDir,
                                     tr("Compare Layout Files (*.mvc)\nAll Files (*.*)"));
    if (fileName.isEmpty())
        return;

    QJsonObject root;

    // Save per-cell adjustments
    QJsonArray adjArray;
    for (size_t i = 0; i < m_cellAdjusts.size(); ++i)
    {
        const auto &a = m_cellAdjusts[i];
        QJsonObject ao;
        ao["brightness"] = a.brightness;
        ao["contrast"] = static_cast<double>(a.contrast);
        ao["gamma"] = static_cast<double>(a.gamma);
        ao["rGain"] = static_cast<double>(a.rGain);
        ao["bGain"] = static_cast<double>(a.bGain);
        ao["rotation"] = a.rotation;
        ao["flipH"] = a.flipH;
        ao["flipV"] = a.flipV;
        ao["hasCrop"] = a.hasCrop;
        if (a.hasCrop)
        {
            ao["cropX"] = a.cropX;
            ao["cropY"] = a.cropY;
            ao["cropW"] = a.cropW;
            ao["cropH"] = a.cropH;
        }
        ao["usmEnabled"] = a.usmEnabled;
        ao["usmAmount"] = a.usmAmount;
        ao["usmRadius"] = a.usmRadius;
        ao["usmThreshold"] = a.usmThreshold;
        ao["usmLumaOnly"] = a.usmLumaOnly;
        adjArray.append(ao);
    }
    root["adjustments"] = adjArray;
    root["perPaneHist"] = m_perPaneHist;
    root["layoutIndex"] = m_layoutCombo ? m_layoutCombo->currentIndex() : 0;

    // Save session settings (includes engine state + UI state)
    const mviewer::domain::CompareSession sess = compareSession();
    QJsonObject sessionObj;
    // M36: retain the independent Zoom/Drag axes. `synced` remains for
    // backwards compatibility with older preset files.
    sessionObj["syncMode"] = static_cast<int>(sess.syncMode);
    sessionObj["synced"] = (sess.syncMode != mviewer::domain::SyncMode::Off);
    sessionObj["sharedScale"] = sess.sharedScale;
    sessionObj["sharedOffsetX"] = sess.sharedOffsetX;
    sessionObj["sharedOffsetY"] = sess.sharedOffsetY;
    sessionObj["blinkIndex"] = sess.blinkIndex;
    sessionObj["blinkIntervalMs"] = sess.blinkIntervalMs;
    sessionObj["threshold"] = static_cast<int>(sess.threshold);
    sessionObj["sidePanelVisible"] = sess.sidePanelVisible;
    sessionObj["layoutIndex"] = sess.layoutIndex;
    sessionObj["customColumns"] = sess.customColumns;
    // selection
    QJsonObject selObj;
    selObj["x"] = sess.selection.x;
    selObj["y"] = sess.selection.y;
    selObj["w"] = sess.selection.w;
    selObj["h"] = sess.selection.h;
    selObj["active"] = sess.selection.active;
    selObj["synced"] = sess.selection.synced;
    sessionObj["selection"] = selObj;
    root["session"] = sessionObj;

    // Save image paths
    const QStringList imgPaths = comparedImages();
    QJsonArray pathArray;
    for (const auto &p : imgPaths)
        pathArray.append(p);
    root["paths"] = pathArray;

    QSaveFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Save failed"), tr("Cannot write file:\n%1").arg(fileName));
        return;
    }
    if (f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !f.commit())
        QMessageBox::warning(this, tr("Save failed"), tr("Cannot commit file:\n%1").arg(fileName));
}

void CompareWorkspace::onLoadPreset()
{
    ensurePresetDir();
    const QString fileName =
        QFileDialog::getOpenFileName(this, tr("Load Comparison Layout"), m_presetDir,
                                     tr("Compare Layout Files (*.mvc)\nAll Files (*.*)"));
    if (fileName.isEmpty())
        return;

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Read Failed"), tr("Cannot open file:\n%1").arg(fileName));
        return;
    }
    constexpr qint64 kMaxPresetFileBytes = 16LL * 1024 * 1024;
    if (f.size() > kMaxPresetFileBytes)
    {
        QMessageBox::warning(this, tr("Read Failed"),
                             tr("Preset file is too large:\n%1").arg(fileName));
        return;
    }
    const QJsonDocument doc =
        QJsonDocument::fromJson(f.read(std::min<qint64>(f.size(), kMaxPresetFileBytes)));
    f.close();

    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    restorePresetAdjustments(root);

    // per-pane histogram
    if (root.contains("perPaneHist"))
    {
        const bool ph = root["perPaneHist"].toBool();
        m_perPaneHist = ph;
        if (m_perPaneHistChk)
            m_perPaneHistChk->setChecked(ph);
    }

    // Checkpoint before session/layout restoration: if applySession or the
    // layout combo restore below triggers rebuildCells() (layout changes are a
    // rebuild trigger), that rebuild already schedules the single all-pane
    // display batch with the loaded m_cellAdjusts. Only schedule explicitly
    // when no display generation was scheduled during that restoration.
    const uint64_t displayGenBeforeRestore = m_displayGen;
    restorePresetSession(root);

    // Restore layout combo
    if (root.contains("layoutIndex") && m_layoutCombo)
    {
        const int li = root["layoutIndex"].toInt(0);
        if (li >= 0 && li < m_layoutCombo->count())
            m_layoutCombo->setCurrentIndex(li);
    }

    finishPresetRestore(displayGenBeforeRestore);
}

void CompareWorkspace::restorePresetAdjustments(const QJsonObject &root)
{
    if (!root.contains(QStringLiteral("adjustments")))
        return;
    const QJsonArray adjArray = root[QStringLiteral("adjustments")].toArray();
    const qsizetype cap = static_cast<qsizetype>(mviewer::domain::CompareSession::MAX_IMAGES);
    const qsizetype count = (std::min)(adjArray.size(), cap);
    m_cellAdjusts.resize(static_cast<size_t>(count));
    for (qsizetype i = 0; i < count; ++i)
    {
        const QJsonObject ao = adjArray.at(i).toObject();
        CellAdjust &cell = m_cellAdjusts[static_cast<size_t>(i)];
        cell.brightness = mviewer::cw::clampBrightness(ao[QStringLiteral("brightness")].toInt());
        cell.contrast = mviewer::cw::clampContrast(
            static_cast<float>(ao[QStringLiteral("contrast")].toDouble(1.0)));
        cell.gamma =
            mviewer::cw::clampGamma(static_cast<float>(ao[QStringLiteral("gamma")].toDouble(1.0)));
        cell.rGain =
            mviewer::cw::clampGain(static_cast<float>(ao[QStringLiteral("rGain")].toDouble(1.0)));
        cell.bGain =
            mviewer::cw::clampGain(static_cast<float>(ao[QStringLiteral("bGain")].toDouble(1.0)));
        cell.rotation = mviewer::cw::clampRotation(ao[QStringLiteral("rotation")].toInt());
        cell.flipH = ao[QStringLiteral("flipH")].toBool();
        cell.flipV = ao[QStringLiteral("flipV")].toBool();
        cell.usmEnabled = ao[QStringLiteral("usmEnabled")].toBool(false);
        cell.usmAmount = mviewer::cw::clampUsmAmount(ao[QStringLiteral("usmAmount")].toDouble(1.5));
        cell.usmRadius = mviewer::cw::clampUsmRadius(ao[QStringLiteral("usmRadius")].toDouble(1.5));
        cell.usmThreshold =
            mviewer::cw::clampUsmThreshold(ao[QStringLiteral("usmThreshold")].toInt(4));
        cell.usmLumaOnly = ao[QStringLiteral("usmLumaOnly")].toBool(false);
        cell.hasCrop = ao[QStringLiteral("hasCrop")].toBool();
        if (!cell.hasCrop)
            continue;
        const mviewer::cw::PresetCrop crop = mviewer::cw::clampCrop(
            ao[QStringLiteral("cropX")].toInt(), ao[QStringLiteral("cropY")].toInt(),
            ao[QStringLiteral("cropW")].toInt(), ao[QStringLiteral("cropH")].toInt());
        cell.cropX = crop.x;
        cell.cropY = crop.y;
        cell.cropW = crop.w;
        cell.cropH = crop.h;
    }
}

void CompareWorkspace::restorePresetSession(const QJsonObject &root)
{
    if (!root.contains(QStringLiteral("session")) || !root[QStringLiteral("session")].isObject())
        return;
    const QJsonObject session = root[QStringLiteral("session")].toObject();
    mviewer::domain::CompareSession restored;
    if (session.contains(QStringLiteral("syncMode")))
    {
        const int mode = qBound(0, session[QStringLiteral("syncMode")].toInt(), 3);
        restored.syncMode = static_cast<mviewer::domain::SyncMode>(mode);
    }
    else
    {
        restored.syncMode = session[QStringLiteral("synced")].toBool(false)
                                ? mviewer::domain::SyncMode::All
                                : mviewer::domain::SyncMode::Off;
    }
    restored.sharedScale =
        mviewer::cw::clampSharedScale(session[QStringLiteral("sharedScale")].toDouble(1.0));
    restored.sharedOffsetX = session[QStringLiteral("sharedOffsetX")].toDouble(0.0);
    restored.sharedOffsetY = session[QStringLiteral("sharedOffsetY")].toDouble(0.0);
    restored.blinkIndex = session[QStringLiteral("blinkIndex")].toInt(-1);
    restored.blinkIntervalMs =
        mviewer::cw::clampBlinkMs(session[QStringLiteral("blinkIntervalMs")].toInt(500));
    restored.threshold = mviewer::cw::clampThreshold(session[QStringLiteral("threshold")].toInt(0));
    restored.sidePanelVisible = session[QStringLiteral("sidePanelVisible")].toBool(false);
    restored.layoutIndex = session[QStringLiteral("layoutIndex")].toInt(0);
    restored.customColumns =
        mviewer::cw::clampColumns(session[QStringLiteral("customColumns")].toInt(2));
    if (session.contains(QStringLiteral("selection")) &&
        session[QStringLiteral("selection")].isObject())
    {
        const QJsonObject sel = session[QStringLiteral("selection")].toObject();
        restored.selection.x = sel[QStringLiteral("x")].toInt();
        restored.selection.y = sel[QStringLiteral("y")].toInt();
        restored.selection.w = sel[QStringLiteral("w")].toInt();
        restored.selection.h = sel[QStringLiteral("h")].toInt();
        restored.selection.active = sel[QStringLiteral("active")].toBool();
        restored.selection.synced = sel[QStringLiteral("synced")].toBool();
    }
    applySession(restored);
}

void CompareWorkspace::finishPresetRestore(uint64_t displayGenBeforeRestore)
{
    if (m_displayGen == displayGenBeforeRestore)
    {
        std::vector<int> all;
        const int n = m_engine.imageCount();
        all.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            all.push_back(i);
        scheduleDisplayMaterialization(all);
    }
    refreshAllDiffOverlays();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();
    update();
}
