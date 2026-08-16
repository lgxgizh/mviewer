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

#include <cstdio>
#include <cmath>
#include <string>
#include <unordered_map>


void AnalysisPanel::refreshAnalyzers()
{
    if (!m_analyzerCombo)
        return;
    const QString prev = m_analyzerCombo->currentData().toString();
    m_analyzerCombo->clear();
    m_pluginIds.clear();
    auto &reg = m_pipeline ? m_pipeline->registry() : AnalyzerRegistry::instance();
    m_pluginIds = reg.availableAnalyzers();
    for (const auto &id : m_pluginIds)
    {
        const auto info = reg.infoFor(id);
        const QString label =
            info ? QString::fromStdString(info->name) : QString::fromStdString(id);
        m_analyzerCombo->addItem(label, QString::fromStdString(id));
    }
    m_analyzerCombo->addItem(tr("Dual Compare (PSNR/SSIM)"), QString("builtin_compare"));
    // Restore previous selection if still present.
    const int idx = m_analyzerCombo->findData(prev);
    if (idx >= 0)
        m_analyzerCombo->setCurrentIndex(idx);
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
    m_hasB = false;
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

void AnalysisPanel::setImages(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull())
        return;
    // Explicit legacy API: a stale async delivery must never overwrite the
    // explicit compare state.
    invalidateAnalysis();
    // Clear the single-frame analyzer pending/result surface before rendering
    // the compare state.
    clearAnalyzerResultSurface();
    m_frameDirty = false;
    setReportAnalysisState(ReportAnalysisState::Unset);
    m_imageA = a.convertToFormat(QImage::Format_RGB32);
    m_imageB = b.convertToFormat(QImage::Format_RGB32);
    m_hasA = m_hasB = true;
    m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
    m_statsB = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageB));
    updateComparePage();
}

void AnalysisPanel::clear()
{
    invalidateAnalysis();
    m_frameDirty = false;
    setReportAnalysisState(ReportAnalysisState::Unset);
    m_imageA = m_imageB = QImage();
    m_hasA = m_hasB = false;
    m_statsA = m_statsB = ImageStats();
    m_noiseA = 0.0;
    m_noiseValid = false;
    m_hasROI = false;
    m_statsLabel->clear();
    m_compareLabel->clear();
    m_diffPreview->clear();
    m_pluginResult->clear();
    m_histogramLabel->clear();
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
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("Region Stats")).arg(text));
}
