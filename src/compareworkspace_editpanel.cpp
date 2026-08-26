// CompareWorkspace edit panel: adjustments, metrics, per-pane histograms, presets (M20 P0#2).
#include "compareworkspace_p.h"

#include "runtime_storage.h"

#include <QSaveFile>

mviewer::core::CompareAdjustmentState
CompareWorkspace::reportAdjustment(const CellAdjust &adjust)
{
    mviewer::core::CompareAdjustmentState report;
    report.brightness = adjust.brightness;
    report.contrast = adjust.contrast;
    report.gamma = adjust.gamma;
    report.redGain = adjust.rGain;
    report.blueGain = adjust.bGain;
    report.rotation = adjust.rotation;
    report.hasCrop = adjust.hasCrop;
    report.cropX = adjust.cropX;
    report.cropY = adjust.cropY;
    report.cropW = adjust.cropW;
    report.cropH = adjust.cropH;
    return report;
}

ImageData CompareWorkspace::applyAdjusts(const ImageData &src, const CellAdjust &a)
{
    return mviewer::core::applyCompareAdjustments(src, reportAdjustment(a));
}

void CompareWorkspace::buildEditPanel(QVBoxLayout *sideLayout)
{
    m_editPanel = new QWidget(m_sidePanel);
    auto *editLay = new QVBoxLayout(m_editPanel);
    editLay->setContentsMargins(0, 4, 0, 0);
    editLay->setSpacing(3);

    m_editLabel = new QLabel(tr("— 选中窗格后可编辑 —"), m_editPanel);
    m_editLabel->setStyleSheet("font-weight:bold;color:#ccc;");
    editLay->addWidget(m_editLabel);

    // Brightness slider [-255, 255]; 0=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("亮度"), m_editPanel));
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
        row->addWidget(new QLabel(tr("对比度"), m_editPanel));
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
        row->addWidget(new QLabel(tr("伽马"), m_editPanel));
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
    auto addGain = [this, editLay](const QString &label, QSlider *&slider, QLabel *&value,
                                   auto callback)
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
    m_resetAdjBtn = new QPushButton(tr("重置调整"), m_editPanel);
    m_resetAdjBtn->setObjectName("resetAdjustmentsButton");
    connect(m_resetAdjBtn, &QPushButton::clicked, this, &CompareWorkspace::onResetAdj);
    editLay->addWidget(m_resetAdjBtn);
    for (QSlider *slider : {m_brightSlider, m_contrastSlider, m_gammaSlider, m_rGainSlider,
                            m_bGainSlider})
        connect(slider, &QSlider::sliderReleased, this, &CompareWorkspace::onAdjEditFinished);
}

void CompareWorkspace::onEditCellSelected(int cellIdx)
{
    m_editIdx = cellIdx;

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

    const ImageFrame *img = m_engine.imageAt(cellIdx);
    m_editLabel->setText(img ? QString::fromStdString(img->metadata().fileName)
                             : tr("窗格 %1").arg(cellIdx + 1));
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

    applyAdjToCell(m_editIdx);

    const bool sliderDown = (m_brightSlider && m_brightSlider->isSliderDown()) ||
                            (m_contrastSlider && m_contrastSlider->isSliderDown()) ||
                            (m_gammaSlider && m_gammaSlider->isSliderDown()) ||
                            (m_rGainSlider && m_rGainSlider->isSliderDown()) ||
                            (m_bGainSlider && m_bGainSlider->isSliderDown());
    if (!sliderDown)
        onAdjEditFinished();

    update();
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
        if (!frame || i >= m_cellViews.size())
            continue;
        const QRect r = m_cellViews[i]->geometry();
        const int w = qMin(160, r.width() / 2);
        const int h = 48;
        frame->setGeometry(r.right() - w - 4, r.bottom() - h - 4, w, h);
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
    const QString fileName = QFileDialog::getSaveFileName(
        this, tr("存储对比布局"), m_presetDir, tr("对比布局文件 (*.mvc)\n所有文件 (*.*)"));
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
        ao["hasCrop"] = a.hasCrop;
        if (a.hasCrop)
        {
            ao["cropX"] = a.cropX;
            ao["cropY"] = a.cropY;
            ao["cropW"] = a.cropW;
            ao["cropH"] = a.cropH;
        }
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
        QMessageBox::warning(this, tr("存储失败"), tr("无法写入文件:\n%1").arg(fileName));
        return;
    }
    if (f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !f.commit())
        QMessageBox::warning(this, tr("存储失败"), tr("无法提交文件:\n%1").arg(fileName));
}

void CompareWorkspace::onLoadPreset()
{
    ensurePresetDir();
    const QString fileName = QFileDialog::getOpenFileName(
        this, tr("读取对比布局"), m_presetDir, tr("对比布局文件 (*.mvc)\n所有文件 (*.*)"));
    if (fileName.isEmpty())
        return;

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("读取失败"), tr("无法打开文件:\n%1").arg(fileName));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();

    // Restore adjustments
    if (root.contains("adjustments"))
    {
        const QJsonArray adjArray = root["adjustments"].toArray();
        m_cellAdjusts.resize(static_cast<size_t>(adjArray.size()));
        for (int i = 0; i < adjArray.size(); ++i)
        {
            const QJsonObject ao = adjArray[i].toObject();
            auto &a = m_cellAdjusts[static_cast<size_t>(i)];
            a.brightness = ao["brightness"].toInt();
            a.contrast = static_cast<float>(ao["contrast"].toDouble(1.0));
            a.gamma = static_cast<float>(ao["gamma"].toDouble(1.0));
            a.rGain = static_cast<float>(ao["rGain"].toDouble(1.0));
            a.bGain = static_cast<float>(ao["bGain"].toDouble(1.0));
            a.rotation = ao["rotation"].toInt();
            a.hasCrop = ao["hasCrop"].toBool();
            if (a.hasCrop)
            {
                a.cropX = ao["cropX"].toInt();
                a.cropY = ao["cropY"].toInt();
                a.cropW = ao["cropW"].toInt();
                a.cropH = ao["cropH"].toInt();
            }
        }
    }

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

    // Restore session settings inline
    if (root.contains("session") && root["session"].isObject())
    {
        const QJsonObject s = root["session"].toObject();
        mviewer::domain::CompareSession sess;
        if (s.contains("syncMode"))
        {
            const int mode = qBound(0, s["syncMode"].toInt(), 3);
            sess.syncMode = static_cast<mviewer::domain::SyncMode>(mode);
        }
        else
        {
            sess.syncMode = s["synced"].toBool(false) ? mviewer::domain::SyncMode::All
                                                      : mviewer::domain::SyncMode::Off;
        }
        sess.sharedScale = s["sharedScale"].toDouble(1.0);
        sess.sharedOffsetX = s["sharedOffsetX"].toDouble(0.0);
        sess.sharedOffsetY = s["sharedOffsetY"].toDouble(0.0);
        sess.blinkIndex = s["blinkIndex"].toInt(-1);
        sess.blinkIntervalMs = s["blinkIntervalMs"].toInt(500);
        sess.threshold = static_cast<uint8_t>(s["threshold"].toInt(0));
        sess.sidePanelVisible = s["sidePanelVisible"].toBool(false);
        sess.layoutIndex = s["layoutIndex"].toInt(0);
        sess.customColumns = s["customColumns"].toInt(2);
        if (s.contains("selection") && s["selection"].isObject())
        {
            const QJsonObject sel = s["selection"].toObject();
            sess.selection.x = sel["x"].toInt();
            sess.selection.y = sel["y"].toInt();
            sess.selection.w = sel["w"].toInt();
            sess.selection.h = sel["h"].toInt();
            sess.selection.active = sel["active"].toBool();
            sess.selection.synced = sel["synced"].toBool();
        }
        applySession(sess);
    }

    // Restore layout combo
    if (root.contains("layoutIndex") && m_layoutCombo)
    {
        const int li = root["layoutIndex"].toInt(0);
        if (li >= 0 && li < m_layoutCombo->count())
            m_layoutCombo->setCurrentIndex(li);
    }

    finishPresetRestore(displayGenBeforeRestore);
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

// ─── M16.6: Swap panes ───────────────────────────────────────────────────────

void CompareWorkspace::onSwapPanes()
{
    const int n = m_cellViews.size();
    if (n < 2)
        return;

    // Swap pane 0 and pane 1 by default; if editIdx is set, swap that with adjacent
    const int a = (m_editIdx >= 0 && m_editIdx < n) ? m_editIdx : 0;
    const int b = (a == 0) ? 1 : 0;

    if (a == b || a >= n || b >= n)
        return;

    // Swap engine frames
    m_engine.swapFrames(a, b);

    // Swap cell adjustments
    if (static_cast<size_t>(std::max(a, b)) < m_cellAdjusts.size())
        std::swap(m_cellAdjusts[static_cast<size_t>(a)], m_cellAdjusts[static_cast<size_t>(b)]);

    // M28 P1-01: rebuildCells() schedules the single all-pane display batch with
    // the already-swapped frames + adjustments, so no post-rebuild applyAdjToCell
    // calls are needed here (they would cancel it into a near-empty request).
    rebuildCells();
    schedulePostLayoutFit();
    update();

    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();
}

// ─── A-4.3: Pixel Link ───────────────────────────────────────────────────────
