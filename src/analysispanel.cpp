#include "analysispanel.h"
#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "widgets/rawimageview.h"

#include "core/image/QtConvert.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

AnalysisPanel::AnalysisPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    setMinimumWidth(360);
    setMinimumHeight(480);
}

void AnalysisPanel::buildUi()
{
    QVBoxLayout *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(4);

    // Plugin selector — every analyzer is reachable through the AnalyzerPipeline
    // (the orchestration layer over the AnalyzerRegistry). The combo items carry
    // the pipeline id as user data so switching the active analyzer routes
    // through the pipeline, never the registry directly (M15 P0#3).
    QHBoxLayout *plugBar = new QHBoxLayout;
    plugBar->addWidget(new QLabel(tr("Analyzer:")));
    m_analyzerCombo = new QComboBox;
    auto &reg = m_pipeline ? m_pipeline->registry() : AnalyzerRegistry::instance();
    m_pluginIds = reg.availableAnalyzers();
    for (const auto &id : m_pluginIds)
    {
        const auto info = reg.infoFor(id);
        const QString label =
            info ? QString::fromStdString(info->name) : QString::fromStdString(id);
        m_analyzerCombo->addItem(label, QString::fromStdString(id));
    }
    // Dual-image comparison (PSNR/SSIM) is a built-in composite view, not a single
    // registry analyzer, so it stays as an extra option.
    m_analyzerCombo->addItem(tr("Dual Compare (PSNR/SSIM)"), QString("builtin_compare"));
    plugBar->addWidget(m_analyzerCombo, 1);
    // P1-6: one-click export of the current analysis report, so the analyzer
    // workflow (Image -> pipeline -> result panel -> export) stays inside the panel
    // instead of forcing a trip to the File menu.
    auto *exportBtn = new QPushButton(tr("导出报告"));
    connect(exportBtn, &QPushButton::clicked, this, &AnalysisPanel::exportRequested);
    plugBar->addWidget(exportBtn);
    mainLay->addLayout(plugBar);

    connect(m_analyzerCombo, QOverload<int>::of(&QComboBox::activated), this,
            &AnalysisPanel::onAnalyzerSelected);

    m_tabs = new QTabWidget;

    m_imageView = std::make_unique<RawImageView>(this);
    mainLay->addWidget(m_imageView.get(), 2);

    mainLay->addWidget(m_tabs, 1);

    // P1-1: Histogram tab: viz + stats text
    m_histogramLabel = new QLabel;
    m_histogramLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_histogramLabel->setStyleSheet("QLabel{background:#141414;}");
    m_statsLabel = new QLabel;
    m_statsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    auto *histPage = new QWidget;
    auto *histLay = new QVBoxLayout(histPage);
    histLay->setContentsMargins(0, 0, 0, 0);
    histLay->setSpacing(4);
    histLay->addWidget(m_histogramLabel, 1);
    histLay->addWidget(m_statsLabel);
    m_tabs->addTab(histPage, tr("Histogram"));

    // P1-1: RGB tab
    m_rgbLabel = new QLabel;
    m_rgbLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_rgbLabel->setStyleSheet("QLabel{background:#141414;}");
    m_rgbStatsLabel = new QLabel;
    m_rgbStatsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_rgbStatsLabel->setWordWrap(true);
    m_rgbStatsLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    auto *rgbPage = new QWidget;
    auto *rgbLay = new QVBoxLayout(rgbPage);
    rgbLay->setContentsMargins(0, 0, 0, 0);
    rgbLay->setSpacing(4);
    rgbLay->addWidget(m_rgbLabel, 1);
    rgbLay->addWidget(m_rgbStatsLabel);
    m_tabs->addTab(rgbPage, tr("RGB"));

    // P1-1: Exposure tab
    m_exposureLabel = new QLabel;
    m_exposureLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_exposureLabel->setWordWrap(true);
    m_exposureLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_exposureLabel, tr("Exposure"));

    // P1-1: Focus tab
    m_focusLabel = new QLabel;
    m_focusLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_focusLabel->setWordWrap(true);
    m_focusLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_focusLabel, tr("Focus"));

    // P1-1: Metadata tab (inside the analysis workspace)
    m_metaLabel = new QLabel;
    m_metaLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_metaLabel, tr("Metadata"));

    // Compare tab
    m_compareLabel = new QLabel;
    m_compareLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_compareLabel->setWordWrap(true);
    m_compareLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_compareLabel, tr("Compare"));

    // Diff preview
    m_diffPreview = new QLabel;
    m_diffPreview->setMinimumHeight(kPreviewSize);
    m_diffPreview->setAlignment(Qt::AlignCenter);
    m_diffPreview->setStyleSheet("QLabel{background:#1e1e1e;}");
    m_tabs->addTab(m_diffPreview, tr("Diff Map"));

    // Plugin tab
    m_pluginResult = new QLabel;
    m_pluginResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_pluginResult->setWordWrap(true);
    m_pluginResult->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_pluginResult, tr("Plugin"));

    // Pixel Inspector tab (M3 Phase-2, upgraded to Pro in M15 P0 #2)
    auto *inspectorPage = new QWidget;
    auto *insLay = new QVBoxLayout(inspectorPage);
    insLay->setContentsMargins(0, 0, 0, 0);
    insLay->setSpacing(4);

    // Color-space + kernel selectors (cheap; changing them just re-renders the
    // inspector text — no histogram recompute, so mouse-move stays smooth).
    QHBoxLayout *insBar = new QHBoxLayout;
    insBar->addWidget(new QLabel(tr("Space:")));
    QComboBox *csCombo = new QComboBox;
    csCombo->addItem(tr("RGB"), static_cast<int>(mviewer::core::ColorSpace::RGB));
    csCombo->addItem(tr("HSV"), static_cast<int>(mviewer::core::ColorSpace::HSV));
    csCombo->addItem(tr("Lab"), static_cast<int>(mviewer::core::ColorSpace::Lab));
    csCombo->addItem(tr("YUV"), static_cast<int>(mviewer::core::ColorSpace::YUV));
    csCombo->addItem(tr("YCbCr"), static_cast<int>(mviewer::core::ColorSpace::YCbCr));
    insBar->addWidget(csCombo, 1);
    insBar->addWidget(new QLabel(tr("Kernel:")));
    QComboBox *kCombo = new QComboBox;
    kCombo->addItem(tr("1×1"), 1);
    kCombo->addItem(tr("3×3"), 3);
    kCombo->addItem(tr("5×5"), 5);
    kCombo->addItem(tr("7×7"), 7);
    insBar->addWidget(kCombo, 1);
    insLay->addLayout(insBar);

    m_inspectorLabel = new QLabel;
    m_inspectorLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_inspectorLabel->setWordWrap(true);
    m_inspectorLabel->setStyleSheet(
        "QLabel{background:#1e1e1e;color:#eee;padding:8px;font-family:monospace;}");
    m_inspectorLabel->setText(tr("Move the mouse over an image to inspect pixels."));
    insLay->addWidget(m_inspectorLabel, 1);
    m_tabs->addTab(inspectorPage, tr("Inspector"));

    connect(csCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this, csCombo](int)
            {
                m_colorSpace = static_cast<mviewer::core::ColorSpace>(csCombo->currentData().toInt());
                updateInspectorPage();
            });
    connect(kCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this, kCombo](int)
            {
                m_kernel = kCombo->currentData().toInt();
                updateInspectorPage();
            });

    m_analyzerCombo->setCurrentIndex(0);
    onAnalyzerSelected(0);
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
        return;
    }

    if (m_frameA && !m_frameA->pixels().isNull() && !id.isEmpty())
    {
        auto analyzer = (m_pipeline ? m_pipeline->create(id.toStdString())
                                    : AnalyzerRegistry::instance().create(id.toStdString()));
        if (analyzer && analyzer->analyzeRegion(*m_frameA, m_roi))
        {
            m_statsA.pixelCount = std::max(0, m_roi.width) * std::max(0, m_roi.height);
            const std::string text = analyzer->resultText();
            const auto *hist = dynamic_cast<const HistogramAnalyzer *>(analyzer.get());
            if (hist)
            {
                const auto &h = hist->result();
                m_statsA.lumMean = h.lumMean;
                m_statsA.rMean = h.rMean;
                m_statsA.gMean = h.gMean;
                m_statsA.bMean = h.bMean;
                renderHistogramPixmap(h);
                m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>")
                                          .arg(tr("ROI Stats (registry)"))
                                          .arg(QString::fromStdString(text)));
            }
            else
            {
                m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>")
                                          .arg(tr("ROI Stats (registry)"))
                                          .arg(QString::fromStdString(text)));
            }
            return;
        }
    }

    if (m_hasA && m_hasROI)
    {
        m_statsA = AnalysisEngine::computeStatsROI(mvcore::fromQImage(m_imageA), m_roi);
        updateHistogramPage();
    }
}

void AnalysisPanel::setFrame(std::shared_ptr<ImageFrame> frame)
{
    m_frameA = std::move(frame);
    if (m_frameA && m_frameA->isValid())
    {
        const QImage img = mvcore::toQImage(m_frameA->pixels()).convertToFormat(QImage::Format_RGB32);
        setImage(img, QString::fromStdString(m_frameA->metadata().filePath));
    }
    reanalyze();
}

void AnalysisPanel::setRegionStats(const QString &text)
{
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("Region Stats")).arg(text));
}

void AnalysisPanel::showPixel(int x, int y, int leftR, int leftG, int leftB, bool valid)
{
    m_px = x;
    m_py = y;
    m_pR = leftR;
    m_pG = leftG;
    m_pB = leftB;
    m_pValid = valid;
    updateInspectorPage();
}

void AnalysisPanel::updateInspectorPage()
{
    if (!m_pValid)
    {
        m_inspectorLabel->setText(tr("Move the mouse over an image to inspect pixels."));
        return;
    }

    const char *csLabel = mviewer::core::colorSpaceLabel(m_colorSpace);
    const mviewer::core::ColorTriple px = mviewer::core::toColorSpace(
        static_cast<uint8_t>(m_pR), static_cast<uint8_t>(m_pG), static_cast<uint8_t>(m_pB),
        m_colorSpace);

    QString txt = QString("<h3>Pixel Inspector — %1</h3>").arg(csLabel);
    txt += QString("pos: (%1, %2)<br>").arg(m_px).arg(m_py);
    txt += QString("<span style='color:#e66;'>●</span> Left %2(%3, %4, %5)<br>")
               .arg(csLabel)
               .arg(px.c1, 0, 'f', 1)
               .arg(px.c2, 0, 'f', 1)
               .arg(px.c3, 0, 'f', 1);

    // NxN neighborhood luminance statistics over the left image (real pixels,
    // read from m_imageA which is Format_RGB32). Clipped to image bounds.
    if (m_hasA && !m_imageA.isNull())
    {
        const int w = m_imageA.width(), h = m_imageA.height();
        if (m_px >= 0 && m_py >= 0 && m_px < w && m_py < h)
        {
            const uchar *data = m_imageA.constBits();
            const int stride = m_imageA.bytesPerLine();
            const mviewer::core::NeighborhoodStats s =
                mviewer::core::neighborhoodStats(data, stride, w, h, m_px, m_py, m_kernel);
            txt += QString("<br><b>%1×%1 Kernel</b> (lum)<br>").arg(m_kernel);
            txt += QString("mean:%1  std:%2<br>")
                       .arg(s.mean, 0, 'f', 1)
                       .arg(s.stdDev, 0, 'f', 1);
            txt += QString("min:%1  max:%2  var:%3  n:%4")
                       .arg(s.min, 0, 'f', 0)
                       .arg(s.max, 0, 'f', 0)
                       .arg(s.variance, 0, 'f', 1)
                       .arg(s.count);
        }
    }

    if (m_hasB && !m_imageB.isNull() && m_px >= 0 && m_py >= 0 && m_px < m_imageB.width() &&
        m_py < m_imageB.height())
    {
        const QRgb c = m_imageB.pixel(m_px, m_py);
        const int rR = qRed(c), rG = qGreen(c), rB = qBlue(c);
        const int dR = m_pR - rR, dG = m_pG - rG, dB = m_pB - rB;
        const double dist = qSqrt(static_cast<double>(dR * dR + dG * dG + dB * dB));
        txt += QString("<br><span style='color:#6e6;'>●</span> Right RGB(%1, %2, %3)<br>")
                   .arg(rR)
                   .arg(rG)
                   .arg(rB);
        txt += QString("Δ      (%1, %2, %3)<br>").arg(dR).arg(dG).arg(dB);
        txt += QString("dist: %1").arg(dist, 0, 'f', 2);
    }
    else
    {
        txt += tr("<br>(load a second image to compare Left/Right/Δ)");
    }
    m_inspectorLabel->setText(txt);
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
               .arg(tr("R Mean")).arg(m_statsA.rMean, 0, 'f', 2)
               .arg(tr("G Mean")).arg(m_statsA.gMean, 0, 'f', 2)
               .arg(tr("B Mean")).arg(m_statsA.bMean, 0, 'f', 2);
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
               .arg(tr("Highlights (>=240)")).arg(highlightPct, 0, 'f', 2)
               .arg(tr("Shadows (<=15)")).arg(shadowPct, 0, 'f', 2)
               .arg(tr("Luminance Mean")).arg(m_statsA.lumMean, 0, 'f', 2);
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
               .arg(tr("Luminance Mean")).arg(m_statsA.lumMean, 0, 'f', 2)
               .arg(tr("Noise Estimate")).arg(noiseLevelText(noise))
               .arg(tr("Pixel Count")).arg(m_statsA.pixelCount);
    m_focusLabel->setText(txt);
}

static QString formatToString(QImage::Format f)
{
    switch (f)
    {
        case QImage::Format_RGB32:
            return "RGB32";
        case QImage::Format_ARGB32:
            return "ARGB32";
        case QImage::Format_ARGB32_Premultiplied:
            return "ARGB32 PM";
        case QImage::Format_RGB888:
            return "RGB888";
        case QImage::Format_RGBA8888:
            return "RGBA8888";
        case QImage::Format_Grayscale8:
            return "Gray8";
        default:
            return QString("Format_%1").arg(static_cast<int>(f));
    }
}

void AnalysisPanel::updateMetadataPage()
{
    if (!m_hasA)
    {
        m_metaLabel->setText(tr("No image selected"));
        return;
    }
    QString txt = QString("<h3>%1</h3>").arg(tr("Metadata"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2 x %3</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "</table>")
               .arg(tr("Dimensions")).arg(m_imageA.width()).arg(m_imageA.height())
               .arg(tr("Format")).arg(formatToString(m_imageA.format()))
               .arg(tr("Depth")).arg(m_imageA.depth());
    if (!m_imagePath.isEmpty())
        txt += QString("<br><b>%1</b> %2").arg(tr("Path:")).arg(m_imagePath);
    m_metaLabel->setText(txt);
}

void AnalysisPanel::updateComparePage()
{
    if (!m_hasA || !m_hasB)
    {
        m_compareLabel->setText(tr("Need two images to compare"));
        return;
    }
    double psnr = AnalysisEngine::psnr(mvcore::fromQImage(m_imageA), mvcore::fromQImage(m_imageB));
    double ssim = AnalysisEngine::ssim(mvcore::fromQImage(m_imageA), mvcore::fromQImage(m_imageB));
    double noiseA = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageA));
    double noiseB = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageB));

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
    m_compareLabel->setText(txt);

    QImage diff = computeDifferencePreview(m_imageA, m_imageB);
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
    auto analyzer = (m_pipeline ? m_pipeline->create(id)
                                : AnalyzerRegistry::instance().create(id));
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
