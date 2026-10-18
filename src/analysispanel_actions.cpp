#include "analysispanel.h"
#include "analyzermodel.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/compare/Aligner.h"
#include "core/scheduler/TaskScheduler.h"
#include "widgets/rawimageview.h"
#include <QPointer>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>

#include "core/image/QtConvert.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

void AnalysisPanel::fillAnalyzerCombo(const QString &restoreId)
{
    if (!m_analyzerCombo)
        return;
    m_analyzerCombo->setObjectName(QStringLiteral("analysisAnalyzerCombo"));
    m_analyzerCombo->clear();
    m_pluginIds.clear();
    auto &registry = m_pipeline ? m_pipeline->registry() : AnalyzerRegistry::instance();
    m_pluginIds = registry.availableAnalyzers();
    for (const auto &id : m_pluginIds)
    {
        const auto info = registry.infoFor(id);
        const QString label =
            info ? QString::fromStdString(info->name) : QString::fromStdString(id);
        m_analyzerCombo->addItem(label, QString::fromStdString(id));
    }
    if (m_analyzerCombo->count() == 0)
        return;
    // An empty id must not match a blank userData. A persisted
    // builtin_compare selection (the old AnalysisPanel two-image tab; compare
    // now lives in CompareWorkspace) is not a real analyzer and falls back to
    // the first one.
    const bool staleCompare = restoreId == QLatin1String("builtin_compare");
    const int found =
        restoreId.isEmpty() || staleCompare ? -1 : m_analyzerCombo->findData(restoreId);
    m_analyzerCombo->setCurrentIndex(found >= 0 ? found : 0);
}

void AnalysisPanel::refreshAnalyzers()
{
    if (!m_analyzerCombo)
        return;
    const QString previous = m_analyzerCombo->currentData().toString();
    fillAnalyzerCombo(previous);
}

void AnalysisPanel::setImage(const QImage &img)
{
    setImage(img, QString());
}

void AnalysisPanel::setImage(const QImage &img, const QString &path)
{
    if (img.isNull())
    {
        clear();
        return;
    }
    // Explicit legacy API: cancel any in-flight analysis task so a stale async
    // delivery can never overwrite this explicit image. There is no pending
    // frame left to auto-analyze either.
    invalidateAnalysis();
    // The pending/manual result surface must not survive the explicit state
    // replacement while applyFrameImage renders the new base image.
    clearAnalyzerResultSurface();
    m_frameDirty = false;
    setReportAnalysisState(ReportAnalysisState::Unset);
    applyFrameImage(img.convertToFormat(QImage::Format_RGB32), path);
}

void AnalysisPanel::applyFrameImage(const QImage &rgb32, const QString &path)
{
    // M28 P1-02: caller already provides RGB32 — no second conversion here.
    m_imageA = rgb32;
    m_imagePath = path;
    m_hasA = true;
    m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
    // Cache the noise estimate so the cheap page updates never re-scan the
    // image (updateFocusPage reads this cache).
    m_noiseA = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageA));
    m_noiseValid = true;
    updateHistogramPage();
    updateRgbPage();
    updateExposurePage();
    updateFocusPage();
    updateMetadataPage();
}

void AnalysisPanel::clear()
{
    invalidateAnalysis();
    m_frameDirty = false;
    setReportAnalysisState(ReportAnalysisState::Unset);
    m_frameA = nullptr;
    m_imageA = QImage();
    m_hasA = false;
    m_statsA = ImageStats();
    m_noiseA = 0.0;
    m_noiseValid = false;
    m_hasROI = false;
    m_statsLabel->clear();
    m_pluginResult->clear();
    m_histogramLabel->clear();
    if (m_rgbLabel)
        m_rgbLabel->clear();
    if (m_rgbStatsLabel)
        m_rgbStatsLabel->clear();
    if (m_exposureLabel)
        m_exposureLabel->clear();
    if (m_focusLabel)
        m_focusLabel->clear();
    if (m_metaLabel)
        m_metaLabel->clear();
    if (m_imageView)
    {
        m_imageView->setImage(QImage());
        m_imageView->clearCrosshair();
    }
}

void AnalysisPanel::setROI(const mviewer::domain::Selection &roi)
{
    m_roi = roi;
    m_hasROI = !roi.isEmpty();
    reanalyze();
}

// A-7.1: select an analyzer by registry id without running it.
void AnalysisPanel::selectAnalyzer(const QString &id)
{
    if (id.isEmpty() || !m_analyzerCombo)
        return;
    refreshAnalyzers();
    const int idx = m_analyzerCombo->findData(id);
    if (idx < 0)
        return;
    const QSignalBlocker blocker(m_analyzerCombo);
    m_analyzerCombo->setCurrentIndex(idx);
    m_currentPluginIdx = idx;
}

void AnalysisPanel::setRegionStats(const QString &text)
{
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("选区统计")).arg(text));
}
