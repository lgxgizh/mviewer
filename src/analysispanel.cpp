#include "analysispanel.h"
#include "analyzermodel.h"
#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/compare/Aligner.h"
#include "widgets/rawimageview.h"
#include <QSettings>

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

AnalysisPanel::AnalysisPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    setMinimumWidth(360);
    setMinimumHeight(480);
}

// A-7.2: rebuild the analyzer combo from the live registry/pipeline.
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
    m_imageA = img.convertToFormat(QImage::Format_RGB32);
    m_imagePath = path;
    m_hasA = true;
    m_hasB = false;
    m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
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
    m_imageA = a.convertToFormat(QImage::Format_RGB32);
    m_imageB = b.convertToFormat(QImage::Format_RGB32);
    m_hasA = m_hasB = true;
    m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
    m_statsB = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageB));
    updateComparePage();
}

void AnalysisPanel::clear()
{
    m_imageA = m_imageB = QImage();
    m_hasA = m_hasB = false;
    m_statsA = m_statsB = ImageStats();
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

// A-7.1 / A-7.3: unified entry — select, run, and surface the Plugin tab.
void AnalysisPanel::runAnalyzer(const QString &id)
{
    if (id.isEmpty())
        return;
    selectAnalyzer(id);
    reanalyze();
    // Surface the Plugin tab so context-menu / combo runs land in one place.
    if (m_tabs)
    {
        const int pluginTab = m_tabs->indexOf(m_pluginResult);
        if (pluginTab >= 0)
            m_tabs->setCurrentIndex(pluginTab);
    }
}

// Run the currently-selected analyzer (from the pipeline) over the left frame and
// the active ROI, then render its result. The analyzer consumes a domain
// Selection, never a QRect. Creation/execution routes through the injected
// AnalyzerPipeline so the panel never touches the registry directly (M15 P0#3).
void AnalysisPanel::reanalyze()
{
    const QString id = m_analyzerCombo ? m_analyzerCombo->currentData().toString() : QString();

    // Dual-image comparison is a built-in composite view, not a single registry analyzer.
    if (id == "builtin_compare")
    {
        updateComparePage();
        if (m_tabs && m_compareLabel)
        {
            const int tab = m_tabs->indexOf(m_compareLabel);
            if (tab >= 0)
                m_tabs->setCurrentIndex(tab);
        }
        return;
    }

    if (m_frameA && !m_frameA->pixels().isNull() && !id.isEmpty())
    {
        // M24 (C#7): a failing/throwing analyzer must degrade to an error note
        // instead of taking down the panel or the application.
        bool ran = false;
        try
        {
            auto analyzer = (m_pipeline ? m_pipeline->create(id.toStdString())
                                        : AnalyzerRegistry::instance().create(id.toStdString()));
            if (analyzer)
            {
                // Prefer ROI when set; otherwise analyze the full frame.
                const bool ok = m_hasROI ? analyzer->analyzeRegion(*m_frameA, m_roi)
                                         : analyzer->analyze(*m_frameA);
                if (ok)
                {
                    ran = true;
                    m_statsA.pixelCount = m_hasROI
                                              ? std::max(0, m_roi.width) * std::max(0, m_roi.height)
                                              : m_frameA->width() * m_frameA->height();
                    const std::string text = analyzer->resultText();
                    const auto metrics = analyzer->resultMetrics();
                    const auto *hist = dynamic_cast<const HistogramAnalyzer *>(analyzer.get());
                    if (hist)
                    {
                    const auto &h = hist->result();
                    m_statsA.lumMean = h.lumMean;
                    m_statsA.rMean = h.rMean;
                    m_statsA.gMean = h.gMean;
                    m_statsA.bMean = h.bMean;
                    renderHistogramPixmap(h);
                }
                // Unified result surface: Histogram stats + Plugin tab.
                const QString html = QString("<h3>%1</h3><p>%2</p>")
                                         .arg(QString::fromStdString(analyzer->name()))
                                         .arg(QString::fromStdString(text));
                m_statsLabel->setText(html);
                if (m_pluginResult)
                {
                    QString pluginHtml = html;
                    if (!metrics.empty())
                    {
                        pluginHtml += "<table>";
                        for (const auto &[k, v] : metrics)
                            pluginHtml += QString("<tr><td>%1</td><td>%2</td></tr>")
                                              .arg(QString::fromStdString(k))
                                              .arg(v, 0, 'f', 4);
                        pluginHtml += "</table>";
                    }
                    m_pluginResult->setText(pluginHtml);
                }
                // M21: publish plain result into AnalyzerModel (history + pin SSOT).
                publishResult(QString::fromStdString(text));
                return;
            }
        }
        }
        catch (...)
        {
            // M24 (C#7): analyzer failure (e.g. buggy plugin) — surface a
            // graceful note instead of propagating an exception into the UI.
            const QString err = tr("分析器执行失败（%1）。").arg(id);
            m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>")
                                      .arg(QStringLiteral("分析失败"))
                                      .arg(err));
            if (m_pluginResult)
                m_pluginResult->setText(err);
            publishResult(err);
            return;
        }
    }

    if (m_hasA && m_hasROI)
    {
        m_statsA = AnalysisEngine::computeStatsROI(mvcore::fromQImage(m_imageA), m_roi);
        updateHistogramPage();
        // Publish a plain-text ROI summary (never HTML from m_statsLabel).
        const QString plain = QString("ROI %1x%2 @(%3,%4) lum=%5 r=%6 g=%7 b=%8")
                                  .arg(m_roi.width)
                                  .arg(m_roi.height)
                                  .arg(m_roi.x)
                                  .arg(m_roi.y)
                                  .arg(m_statsA.lumMean, 0, 'f', 2)
                                  .arg(m_statsA.rMean, 0, 'f', 2)
                                  .arg(m_statsA.gMean, 0, 'f', 2)
                                  .arg(m_statsA.bMean, 0, 'f', 2);
        publishResult(plain);
    }
}

void AnalysisPanel::setFrame(std::shared_ptr<ImageFrame> frame)
{
    m_frameA = std::move(frame);
    if (m_frameA && m_frameA->isValid())
    {
        const QImage img =
            mvcore::toQImage(m_frameA->pixels()).convertToFormat(QImage::Format_RGB32);
        setImage(img, QString::fromStdString(m_frameA->metadata().filePath));
    }
    reanalyze();
}

void AnalysisPanel::setRegionStats(const QString &text)
{
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("Region Stats")).arg(text));
}

void AnalysisPanel::showPixel(int x, int y, int leftR, int leftG, int leftB, int leftA, int r16,
                              int g16, int b16, int rawKind, bool valid)
{
    if (m_frozen)
        return; // keep the last inspected pixel frozen
    m_px = x;
    m_py = y;
    m_pR = leftR;
    m_pG = leftG;
    m_pB = leftB;
    m_pA = leftA;
    m_r16 = r16;
    m_g16 = g16;
    m_b16 = b16;
    m_rawKind = rawKind;
    m_rawMax = (rawKind == 2) ? 65535 : 0;
    m_pValid = valid;
    // P0/P1 #⑥: draw a crosshair on the panel image at the inspected pixel so an
    // ISP engineer can screenshot the exact inspection point (Pixel Inspector).
    if (m_imageView)
    {
        if (valid)
            m_imageView->setCrosshair(QPointF(x + 0.5, y + 0.5));
        else
            m_imageView->clearCrosshair();
    }
    updateInspectorPage();
}

int AnalysisPanel::currentPage() const
{
    return m_tabs ? m_tabs->currentIndex() : 0;
}

void AnalysisPanel::setCurrentPage(int index)
{
    if (m_tabs && index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
}

void AnalysisPanel::onAnalyzerSelected(int index)
{
    m_currentPluginIdx = index;
    reanalyze();
}

void AnalysisPanel::updateHistogramPage()
{
    if (!m_hasA)
    {
        m_statsLabel->setText(tr("No image selected"));
        return;
    }
    QString title = m_hasROI ? tr("ROI Stats") : tr("Full Image Stats");
    QString txt = QString("<h3>%1</h3>").arg(title);
    txt += QString("<table>"
                   "<tr><td>%2</td><td>%3</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "<tr><td>%8</td><td>%9</td></tr>"
                   "<tr><td>%10</td><td>%11</td></tr>"
                   "</table>")
               .arg(tr("Lum Mean"))
               .arg(m_statsA.lumMean, 0, 'f', 2)
               .arg(tr("R Mean"))
               .arg(m_statsA.rMean, 0, 'f', 2)
               .arg(tr("G Mean"))
               .arg(m_statsA.gMean, 0, 'f', 2)
               .arg(tr("B Mean"))
               .arg(m_statsA.bMean, 0, 'f', 2)
               .arg(tr("Pixels"))
               .arg(m_statsA.pixelCount);
    m_statsLabel->setText(txt);
    renderHistogramPixmap();
}

void AnalysisPanel::renderHistogramPixmap()
{
    if (!m_hasA)
        return;
    const int W = qMax(200, m_histogramLabel->width() - 8);
    const int H = 160;
    QPixmap pix(W, H);
    pix.fill(QColor(20, 20, 20));
    QPainter p(&pix);
    const int pad = 4;
    const QRect bg(pad, pad, W - pad * 2, H - pad * 2);
    // Overlaid 4 channels
    auto drawChannel = [&bg, &p](const int *hist, const QColor &color)
    {
        constexpr int srcBins = 256;
        constexpr int drawBins = 64;
        const double binW = static_cast<double>(bg.width()) / drawBins;
        long long agg[drawBins] = {0};
        long long maxV = 1;
        for (int i = 0; i < drawBins; ++i)
        {
            long long sum = 0;
            const int lo = i * srcBins / drawBins;
            const int hi = (i + 1) * srcBins / drawBins;
            for (int j = lo; j < hi && j < srcBins; ++j)
                sum += hist[j];
            agg[i] = sum;
            if (sum > maxV)
                maxV = sum;
        }
        p.setPen(color);
        for (int i = 0; i < drawBins; ++i)
        {
            const double h = static_cast<double>(agg[i]) / maxV * bg.height();
            const int x = bg.x() + static_cast<int>(i * binW);
            const int hh = qMax(1, static_cast<int>(h));
            p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
        }
    };
    drawChannel(m_statsA.histLum, QColor(220, 220, 220));
    drawChannel(m_statsA.histR, QColor(230, 70, 70));
    drawChannel(m_statsA.histG, QColor(70, 220, 70));
    drawChannel(m_statsA.histB, QColor(70, 130, 230));
    m_histogramLabel->setPixmap(pix);
}

// P1-1: RGB channel page — separate R/G/B histograms + per-channel means.
void AnalysisPanel::updateRgbPage()
{
    if (!m_hasA)
    {
        m_rgbLabel->setText(tr("No image selected"));
        m_rgbStatsLabel->setText(QString());
        return;
    }
    QString txt = QString("<h3>%1</h3>").arg(tr("RGB Channels"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2</td></tr>"
                   "<tr><td>%3</td><td>%4</td></tr>"
                   "<tr><td>%5</td><td>%6</td></tr>"
                   "</table>")
               .arg(tr("R Mean"))
               .arg(m_statsA.rMean, 0, 'f', 2)
               .arg(tr("G Mean"))
               .arg(m_statsA.gMean, 0, 'f', 2)
               .arg(tr("B Mean"))
               .arg(m_statsA.bMean, 0, 'f', 2);
    m_rgbStatsLabel->setText(txt);

    const int W = qMax(200, m_rgbLabel->width() - 8);
    const int H = 160;
    QPixmap pix(W, H);
    pix.fill(QColor(20, 20, 20));
    QPainter p(&pix);
    const int pad = 4;
    const QRect bg(pad, pad, W - pad * 2, H - pad * 2);
    auto drawChannel = [&bg, &p](const int *hist, const QColor &color)
    {
        constexpr int srcBins = 256;
        constexpr int drawBins = 64;
        const double binW = static_cast<double>(bg.width()) / drawBins;
        long long agg[drawBins] = {0};
        long long maxV = 1;
        for (int i = 0; i < drawBins; ++i)
        {
            long long sum = 0;
            const int lo = i * srcBins / drawBins;
            const int hi = (i + 1) * srcBins / drawBins;
            for (int j = lo; j < hi && j < srcBins; ++j)
                sum += hist[j];
            agg[i] = sum;
            if (sum > maxV)
                maxV = sum;
        }
        p.setPen(color);
        for (int i = 0; i < drawBins; ++i)
        {
            const double h = static_cast<double>(agg[i]) / maxV * bg.height();
            const int x = bg.x() + static_cast<int>(i * binW);
            const int hh = qMax(1, static_cast<int>(h));
            p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
        }
    };
    drawChannel(m_statsA.histR, QColor(230, 70, 70));
    drawChannel(m_statsA.histG, QColor(70, 220, 70));
    drawChannel(m_statsA.histB, QColor(70, 130, 230));
    m_rgbLabel->setPixmap(pix);
}

void AnalysisPanel::updateExposurePage()
{
    if (!m_hasA)
    {
        m_exposureLabel->setText(tr("No image selected"));
        return;
    }
    long long highlights = 0, shadows = 0, total = 0;
    for (int i = 0; i < 256; ++i)
    {
        const long long v = m_statsA.histLum[i];
        total += v;
        if (i >= 240)
            highlights += v;
        if (i <= 15)
            shadows += v;
    }
    const double highlightPct = total ? 100.0 * highlights / total : 0.0;
    const double shadowPct = total ? 100.0 * shadows / total : 0.0;

    QString txt = QString("<h3>%1</h3>").arg(tr("Exposure"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2%</td></tr>"
                   "<tr><td>%3</td><td>%4%</td></tr>"
                   "<tr><td>%5</td><td>%6</td></tr>"
                   "</table>")
               .arg(tr("Highlights (>=240)"))
               .arg(highlightPct, 0, 'f', 2)
               .arg(tr("Shadows (<=15)"))
               .arg(shadowPct, 0, 'f', 2)
               .arg(tr("Luminance Mean"))
               .arg(m_statsA.lumMean, 0, 'f', 2);
    m_exposureLabel->setText(txt);
}

void AnalysisPanel::updateFocusPage()
{
    if (!m_hasA)
    {
        m_focusLabel->setText(tr("No image selected"));
        return;
    }
    const double noise = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageA));

    QString txt = QString("<h3>%1</h3>").arg(tr("Focus / Sharpness"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2</td></tr>"
                   "<tr><td>%3</td><td>%4</td></tr>"
                   "<tr><td>%5</td><td>%6</td></tr>"
                   "</table>")
               .arg(tr("Luminance Mean"))
               .arg(m_statsA.lumMean, 0, 'f', 2)
               .arg(tr("Noise Estimate"))
               .arg(noiseLevelText(noise))
               .arg(tr("Pixel Count"))
               .arg(m_statsA.pixelCount);
    m_focusLabel->setText(txt);
}

void AnalysisPanel::updateComparePage()
{
    if (!m_hasA || !m_hasB)
    {
        m_compareLabel->setText(tr("Need two images to compare"));
        return;
    }
    QSettings s;
    const bool autoAlign = s.value("autoAlignBeforeDiff", false).toBool();

    ImageData a = mvcore::fromQImage(m_imageA);
    ImageData b = mvcore::fromQImage(m_imageB);
    QPoint offset(0, 0);
    bool aligned = false;
    if (autoAlign)
    {
        // F3 (M22): register B to A before diff so PSNR/SSIM reflect signal,
        // not mis-registration. Default off → no behavior change otherwise.
        mviewer::AlignOffset off = mviewer::Aligner::estimate(a, b, 32);
        offset = QPoint(off.x, off.y);
        if (off.x != 0 || off.y != 0)
        {
            b = mviewer::Aligner::shift(b, off.x, off.y);
            aligned = true;
        }
    }

    double psnr = AnalysisEngine::psnr(a, b);
    double ssim = AnalysisEngine::ssim(a, b);
    double noiseA = AnalysisEngine::noiseEstimate(a);
    double noiseB = AnalysisEngine::noiseEstimate(b);

    QString txt = QString("<h3>%1</h3>").arg(tr("Dual Compare"));
    txt += QString("<table>"
                   "<tr><td>%2</td><td>%3 dB</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "<tr><td>%8</td><td>%9</td></tr>"
                   "</table>")
               .arg(tr("PSNR"))
               .arg(psnr, 0, 'f', 2)
               .arg(tr("SSIM"))
               .arg(ssim, 0, 'f', 4)
               .arg(tr("Noise(A)"))
               .arg(noiseLevelText(noiseA))
               .arg(tr("Noise(B)"))
               .arg(noiseLevelText(noiseB));
    if (aligned)
        txt += QString("<p><b>%1</b> dx=%2 dy=%3</p>")
                   .arg(tr("Auto-aligned before diff"))
                   .arg(offset.x())
                   .arg(offset.y());
    m_compareLabel->setText(txt);

    QImage diff = computeDifferencePreview(m_imageA, mvcore::toQImage(b));
    if (!diff.isNull())
    {
        m_diffPreview->setPixmap(QPixmap::fromImage(diff).scaled(
            QSize(kPreviewSize, kPreviewSize), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void AnalysisPanel::updatePluginPage()
{
    if (m_pluginIds.empty())
    {
        m_pluginResult->setText(tr("No analyzer plugins available"));
        return;
    }
    int pluginIdx = m_currentPluginIdx - 2;
    if (pluginIdx < 0 || pluginIdx >= static_cast<int>(m_pluginIds.size()))
    {
        m_pluginResult->setText(tr("Select a plugin"));
        return;
    }
    const std::string &id = m_pluginIds[pluginIdx];
    auto analyzer = (m_pipeline ? m_pipeline->create(id) : AnalyzerRegistry::instance().create(id));
    if (!analyzer)
    {
        m_pluginResult->setText(tr("Cannot create: %1").arg(QString::fromStdString(id)));
        return;
    }
    QString txt = QString("<h3>%1</h3><p>%2</p>")
                      .arg(QString::fromStdString(analyzer->name()))
                      .arg(QString::fromStdString(analyzer->description()));
    m_pluginResult->setText(txt);
}

QImage AnalysisPanel::computeDifferencePreview(const QImage &a, const QImage &b)
{
    ImageData diff = AnalysisEngine::differenceMap(mvcore::fromQImage(a), mvcore::fromQImage(b));
    if (diff.isNull())
        return QImage();
    return mvcore::toQImage(diff);
}

QString AnalysisPanel::noiseLevelText(double variance)
{
    if (variance < 50)
        return tr("Very Low (%1)").arg(variance, 0, 'f', 1);
    if (variance < 150)
        return tr("Low (%1)").arg(variance, 0, 'f', 1);
    if (variance < 300)
        return tr("Medium (%1)").arg(variance, 0, 'f', 1);
    if (variance < 500)
        return tr("High (%1)").arg(variance, 0, 'f', 1);
    return tr("Very High (%1)").arg(variance, 0, 'f', 1);
}

void AnalysisPanel::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    // Histogram viz rendered via QPixmap in renderHistogramPixmap()
}

void AnalysisPanel::updateImage(const QImage &img)
{
    if (m_imageView)
    {
        if (img.isNull())
            m_imageView->clear();
        else
            m_imageView->setImage(img.convertToFormat(QImage::Format_RGB32));
    }
}

void AnalysisPanel::updateHistogram(const mviewer::domain::Histogram &hist)
{
    renderHistogramPixmap(hist);
    m_statsLabel->setText(QString("<h3>%1</h3>"
                                  "<table>"
                                  "<tr><td>%2</td><td>%3</td></tr>"
                                  "<tr><td>%4</td><td>%5</td></tr>"
                                  "<tr><td>%6</td><td>%7</td></tr>"
                                  "<tr><td>%8</td><td>%9</td></tr>"
                                  "<tr><td>%10</td><td>%11</td></tr>"
                                  "</table>")
                              .arg(tr("Full Image Stats"))
                              .arg(tr("Lum Mean"))
                              .arg(hist.lumMean, 0, 'f', 2)
                              .arg(tr("R Mean"))
                              .arg(hist.rMean, 0, 'f', 2)
                              .arg(tr("G Mean"))
                              .arg(hist.gMean, 0, 'f', 2)
                              .arg(tr("B Mean"))
                              .arg(hist.bMean, 0, 'f', 2)
                              .arg(tr("Pixels"))
                              .arg(hist.totalPixels()));
}

void AnalysisPanel::renderHistogramPixmap(const mviewer::domain::Histogram &hist)
{
    if (!m_histogramLabel)
        return;
    const int W = qMax(200, m_histogramLabel->width() - 8);
    const int H = 160;
    QPixmap pix(W, H);
    pix.fill(QColor(20, 20, 20));
    QPainter p(&pix);
    const int pad = 4;
    const QRect bg(pad, pad, W - pad * 2, H - pad * 2);

    auto drawChannel = [&bg, &p](const int *histBins, const QColor &color)
    {
        constexpr int srcBins = 256;
        constexpr int drawBins = 64;
        const double binW = static_cast<double>(bg.width()) / drawBins;
        long long agg[drawBins] = {0};
        long long maxV = 1;
        for (int i = 0; i < drawBins; ++i)
        {
            long long sum = 0;
            const int lo = i * srcBins / drawBins;
            const int hi = (i + 1) * srcBins / drawBins;
            for (int j = lo; j < hi && j < srcBins; ++j)
                sum += histBins[j];
            agg[i] = sum;
            if (sum > maxV)
                maxV = sum;
        }
        p.setPen(color);
        for (int i = 0; i < drawBins; ++i)
        {
            const double h = static_cast<double>(agg[i]) / maxV * bg.height();
            const int x = bg.x() + static_cast<int>(i * binW);
            const int hh = qMax(1, static_cast<int>(h));
            p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
        }
    };

    drawChannel(hist.luminance.data(), QColor(220, 220, 220));
    drawChannel(hist.red.data(), QColor(230, 70, 70));
    drawChannel(hist.green.data(), QColor(70, 220, 70));
    drawChannel(hist.blue.data(), QColor(70, 130, 230));
    m_histogramLabel->setPixmap(pix);
}
