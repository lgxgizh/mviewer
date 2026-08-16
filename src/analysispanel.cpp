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
    // M28 P1-04: noise is precomputed (async worker or legacy applyFrameImage)
    // and cached; updateFocusPage must not re-scan the image. The fallback
    // keeps the page correct for any path that did not populate the cache.
    const double noise =
        m_noiseValid ? m_noiseA : AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageA));

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
