// CompareWorkspace edit panel: adjustments, metrics, per-pane histograms, presets (M20 P0#2).
#include "compareworkspace_p.h"

ImageData CompareWorkspace::applyAdjusts(const ImageData &src, const CellAdjust &a) const
{
    if (src.isNull() || a.isIdentity())
        return src;

    ImageData cur = src;

    // Order: brightness → contrast → gamma → white balance
    if (a.brightness != 0)
        cur = adjustBrightness(cur, a.brightness);
    if (std::abs(a.contrast - 1.0f) >= 1e-6f)
        cur = adjustContrast(cur, a.contrast);
    if (std::abs(a.gamma - 1.0f) >= 1e-6f)
        cur = adjustGamma(cur, a.gamma);
    if (std::abs(a.rGain - 1.0f) >= 1e-6f || std::abs(a.bGain - 1.0f) >= 1e-6f)
        cur = adjustWhiteBalance(cur, a.rGain, a.bGain);

    // Crop
    if (a.hasCrop && a.cropW > 0 && a.cropH > 0)
    {
        const mviewer::domain::Selection sel{a.cropX, a.cropY, a.cropW, a.cropH};
        cur = cropRegion(cur, sel);
    }

    // Rotation (apply after crop)
    if (a.rotation != 0)
    {
        int rot = a.rotation % 360;
        if (rot < 0)
            rot += 360;
        while (rot > 0)
        {
            cur = rotate90CW(cur);
            rot -= 90;
        }
    }

    return cur;
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

    // White balance: R gain [1..500] → float [0.01..5.0]; 100=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("WB R"), m_editPanel));
        m_rGainSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_rGainSlider->setRange(1, 500);
        m_rGainSlider->setValue(100);
        m_rGainVal = new QLabel("1.00", m_editPanel);
        m_rGainVal->setMinimumWidth(30);
        row->addWidget(m_rGainSlider);
        row->addWidget(m_rGainVal);
        editLay->addLayout(row);
        connect(m_rGainSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_rGainVal->setText(QString::number(v / 100.0, 'f', 2));
                    onAdjChanged();
                });
    }

    // White balance: B gain [1..500] → float [0.01..5.0]; 100=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("WB B"), m_editPanel));
        m_bGainSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_bGainSlider->setRange(1, 500);
        m_bGainSlider->setValue(100);
        m_bGainVal = new QLabel("1.00", m_editPanel);
        m_bGainVal->setMinimumWidth(30);
        row->addWidget(m_bGainSlider);
        row->addWidget(m_bGainVal);
        editLay->addLayout(row);
        connect(m_bGainSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_bGainVal->setText(QString::number(v / 100.0, 'f', 2));
                    onAdjChanged();
                });
    }

    // Reset button
    m_resetAdjBtn = new QPushButton(tr("重置调整"), m_editPanel);
    m_resetAdjBtn->setObjectName("resetAdjustmentsButton");
    connect(m_resetAdjBtn, &QPushButton::clicked, this, &CompareWorkspace::onResetAdj);
    editLay->addWidget(m_resetAdjBtn);

    connect(m_brightSlider, &QSlider::sliderReleased, this, &CompareWorkspace::onAdjEditFinished);
    connect(m_contrastSlider, &QSlider::sliderReleased, this, &CompareWorkspace::onAdjEditFinished);
    connect(m_gammaSlider, &QSlider::sliderReleased, this, &CompareWorkspace::onAdjEditFinished);
    connect(m_rGainSlider, &QSlider::sliderReleased, this, &CompareWorkspace::onAdjEditFinished);
    connect(m_bGainSlider, &QSlider::sliderReleased, this, &CompareWorkspace::onAdjEditFinished);

    sideLayout->addWidget(m_editPanel);
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
    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0 && m_lastInspectY >= 0)
        updateInspector(m_lastInspectX, m_lastInspectY);
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

    // Keep the inexpensive visual preview and Inspector live during a drag.
    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0 && m_lastInspectY >= 0)
        updateInspector(m_lastInspectX, m_lastInspectY);

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

    const CellAdjust &a = (cellIdx < static_cast<int>(m_cellAdjusts.size()))
                              ? m_cellAdjusts[static_cast<size_t>(cellIdx)]
                              : CellAdjust{};

    RawImageView *view = m_cellViews[cellIdx];
    const QSize oldSize = view ? view->image().size() : QSize();
    const double oldScale = view ? view->scale() : 1.0;
    const QPointF oldOffset = view ? view->offset() : QPointF();

    if (a.isIdentity())
    {
        // Just show original
        if (view)
            view->setImage(imageObjectToQImage(img));
    }
    else
    {
        ImageData adjusted = applyAdjusts(img->pixels(), a);
        if (adjusted.isNull())
            return;
        QImage qi = mvcore::toQImage(adjusted);
        if (view)
            view->setImage(qi);
    }

    if (view && view->image().size() == oldSize && !oldSize.isEmpty())
        view->setTransform(oldScale, oldOffset);

    update();
}

// ─── M16.4: Quick PSNR/SSIM metrics ─────────────────────────────────────────

void CompareWorkspace::updateMetrics()
{
    if (!m_metricLabel)
        return;
    const int n = m_engine.imageCount();
    if (n < 2)
    {
        m_metricLabel->setText(tr("PSNR: —  SSIM: —"));
        return;
    }

    const int baseIdx = diffBaseIndex();
    // Pick the first non-base cell
    int targetIdx = -1;
    for (int i = 0; i < n; ++i)
    {
        if (i != baseIdx)
        {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx < 0)
    {
        m_metricLabel->setText(tr("PSNR: —  SSIM: —"));
        return;
    }

    const ImageData basePx = adjustedPixels(baseIdx);
    const ImageData tgtPx = adjustedPixels(targetIdx);
    if (basePx.isNull() || tgtPx.isNull())
    {
        m_metricLabel->setText(tr("PSNR: —  SSIM: —"));
        return;
    }

    const auto baseV = basePx.view();
    const auto tgtV = tgtPx.view();
    if (baseV.width != tgtV.width || baseV.height != tgtV.height)
    {
        m_metricLabel->setText(tr("PSNR: —  SSIM: —\n(图像尺寸不一致)"));
        return;
    }

    const double psnrVal = AnalysisEngine::psnr(basePx, tgtPx);
    const double ssimVal = AnalysisEngine::ssim(basePx, tgtPx);

    const QString psnrStr = QString::number(psnrVal, 'f', 2) + " dB";
    const QString ssimStr = QString::number(ssimVal, 'f', 4);

    QString text = tr("PSNR: %1  SSIM: %2\n(Image #%3 vs #%4)")
                       .arg(psnrStr, ssimStr)
                       .arg(baseIdx + 1)
                       .arg(targetIdx + 1);

    // M23: quantitative diff statistics (threshold-aware), full image + ROI.
    const ImageData diff = DifferenceEngine::differenceMap(tgtPx, basePx);
    if (!diff.isNull())
    {
        const auto st = DifferenceEngine::computeStats(diff, m_thresholdValue);
        text += tr("\n差异: %1%  均值 %2  峰值 %3")
                    .arg(st.diffRatio * 100.0, 0, 'f', 2)
                    .arg(st.meanDiff, 0, 'f', 2)
                    .arg(st.maxDiff);
        if (!m_lastSelection.isEmpty())
        {
            const auto rs = DifferenceEngine::computeStats(
                diff, m_thresholdValue, m_lastSelection.x, m_lastSelection.y, m_lastSelection.width,
                m_lastSelection.height);
            if (rs.totalPixels > 0)
                text += tr("\nROI差异: %1%  均值 %2  峰值 %3")
                            .arg(rs.diffRatio * 100.0, 0, 'f', 2)
                            .arg(rs.meanDiff, 0, 'f', 2)
                            .arg(rs.maxDiff);
        }
    }
    m_metricLabel->setText(text);
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

mviewer::core::CompareReportBundle CompareWorkspace::buildReportBundle() const
{
    const int imageCount = m_engine.imageCount();
    std::vector<ImageFrame> adjustedImages;
    adjustedImages.reserve(static_cast<size_t>(imageCount));

    std::vector<mviewer::core::CompareAdjustmentState> adjustments;
    adjustments.resize(static_cast<size_t>(imageCount));

    for (int i = 0; i < imageCount; ++i)
    {
        const ImageFrame *source = m_engine.imageAt(i);
        mviewer::domain::ImageMetadata metadata;
        if (source)
            metadata = source->metadata();

        const ImageData pixels = adjustedPixels(i);
        metadata.width = pixels.width;
        metadata.height = pixels.height;
        adjustedImages.emplace_back(metadata, pixels);

        if (i < static_cast<int>(m_cellAdjusts.size()))
        {
            const CellAdjust &cell = m_cellAdjusts[static_cast<size_t>(i)];
            auto &report = adjustments[static_cast<size_t>(i)];
            report.brightness = cell.brightness;
            report.contrast = cell.contrast;
            report.gamma = cell.gamma;
            report.redGain = cell.rGain;
            report.blueGain = cell.bGain;
            report.rotation = cell.rotation;
            report.hasCrop = cell.hasCrop;
            report.cropX = cell.cropX;
            report.cropY = cell.cropY;
            report.cropW = cell.cropW;
            report.cropH = cell.cropH;
        }
    }

    return mviewer::core::buildCompareReportBundle(adjustedImages, diffBaseIndex(),
                                                   m_thresholdValue, m_lastSelection, adjustments);
}

void CompareWorkspace::onAdjEditFinished()
{
    // This is the sole full analysis refresh path for adjustment edits.
    const bool analysisVisible = m_sideChk && m_sideChk->isChecked();
    if (analysisVisible)
        refreshHistograms();
    if (m_paneHistOverlay && (!analysisVisible || m_perPaneHist))
        refreshCellHist(m_editIdx);
    refreshAllDiffOverlays();
}

void CompareWorkspace::refreshCellHist(int idx)
{
    if (!m_paneHistOverlay || idx < 0 || idx >= static_cast<int>(m_cellHists.size()))
        return;
    HistogramWidget *hw = m_cellHists[static_cast<size_t>(idx)];
    if (!hw)
        return;
    hw->setHistograms({histogramForImage(idx)});
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
        for (int i = 0; i < static_cast<int>(m_cellHists.size()); ++i)
            refreshCellHist(i);
        positionCellHists();
    }
}

// ─── M16.6: Layout presets save/load ─────────────────────────────────────────

void CompareWorkspace::ensurePresetDir()
{
    if (!m_presetDir.isEmpty())
        return;
    // Store presets under the app's cache/config directory
    m_presetDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/compare_presets";
    QDir().mkpath(m_presetDir);
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
    sessionObj["synced"] = (sess.syncMode != mviewer::domain::SyncMode::Off);
    sessionObj["sharedScale"] = sess.sharedScale;
    sessionObj["sharedOffsetX"] = sess.sharedOffsetX;
    sessionObj["sharedOffsetY"] = sess.sharedOffsetY;
    sessionObj["blinkIndex"] = sess.blinkIndex;
    sessionObj["blinkIntervalMs"] = sess.blinkIntervalMs;
    sessionObj["threshold"] = static_cast<int>(sess.threshold);
    sessionObj["sidePanelVisible"] = sess.sidePanelVisible;
    sessionObj["layoutIndex"] = sess.layoutIndex;
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

    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("存储失败"), tr("无法写入文件:\n%1").arg(fileName));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
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

    // Restore session settings inline
    if (root.contains("session") && root["session"].isObject())
    {
        const QJsonObject s = root["session"].toObject();
        mviewer::domain::CompareSession sess;
        sess.syncMode = s["synced"].toBool(false) ? mviewer::domain::SyncMode::All
                                                  : mviewer::domain::SyncMode::Off;
        sess.sharedScale = s["sharedScale"].toDouble(1.0);
        sess.sharedOffsetX = s["sharedOffsetX"].toDouble(0.0);
        sess.sharedOffsetY = s["sharedOffsetY"].toDouble(0.0);
        sess.blinkIndex = s["blinkIndex"].toInt(-1);
        sess.blinkIntervalMs = s["blinkIntervalMs"].toInt(500);
        sess.threshold = static_cast<uint8_t>(s["threshold"].toInt(0));
        sess.sidePanelVisible = s["sidePanelVisible"].toBool(false);
        sess.layoutIndex = s["layoutIndex"].toInt(0);
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

    // Apply adjustments to all cells
    for (size_t i = 0; i < m_cellAdjusts.size(); ++i)
    {
        const int idx = static_cast<int>(i);
        if (idx < m_engine.imageCount())
            applyAdjToCell(idx);
    }

    // Restore layout combo
    if (root.contains("layoutIndex") && m_layoutCombo)
    {
        const int li = root["layoutIndex"].toInt(0);
        if (li >= 0 && li < m_layoutCombo->count())
            m_layoutCombo->setCurrentIndex(li);
    }

    if (m_sidePanel && m_sidePanel->isVisible())
    {
        refreshHistograms();
        updateMetrics();
    }
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

    rebuildCells();

    // Re-apply adjustments to swapped positions
    applyAdjToCell(a);
    applyAdjToCell(b);

    fitAll();
    update();

    if (m_sidePanel && m_sidePanel->isVisible())
    {
        refreshHistograms();
        updateMetrics();
    }
}

// ─── A-4.3: Pixel Link ───────────────────────────────────────────────────────
