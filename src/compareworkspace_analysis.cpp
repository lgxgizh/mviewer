// M23: CompareWorkspace analysis panel — Pixel Inspector Pro (multi color-space
// readout + neighborhood stats) and the channel/log/ROI-aware histogram section.
// Split out per ADR 014 (keep compareworkspace.cpp under the line budget).
#include "compareworkspace_p.h"

#include "core/analysis/PixelInspector.h"

#include <cmath>

namespace
{
using mviewer::core::ColorSpace;

// Combo index → color space. Order matches the combo items built below.
constexpr ColorSpace kSpaces[] = {ColorSpace::RGB, ColorSpace::HEX, ColorSpace::HSV,
                                  ColorSpace::Lab, ColorSpace::YUV, ColorSpace::YCbCr,
                                  ColorSpace::XYZ};

// Per-space channel headers for the inspector table.
const char *const kHeaders[][3] = {{"R", "G", "B"}, {"HEX", "", ""}, {"H", "S", "V"},
                                   {"L", "a", "b"}, {"Y", "U", "V"}, {"Y", "Cb", "Cr"},
                                   {"X", "Y", "Z"}};

QString formatChannel(ColorSpace space, double v)
{
    switch (space)
    {
    case ColorSpace::RGB:
    case ColorSpace::YUV:
    case ColorSpace::YCbCr:
        return QString::number(static_cast<int>(v + (v >= 0 ? 0.5 : -0.5)));
    case ColorSpace::XYZ:
        return QString::number(v, 'f', 3);
    default: // HSV / Lab
        return QString::number(v, 'f', 1);
    }
}

struct DisplaySample
{
    int r = 0;
    int g = 0;
    int b = 0;
    bool valid = false;
};

DisplaySample sampleDisplayImage(const QImage &image, int x, int y)
{
    if (image.isNull() || x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return {};
    const QRgb pixel = image.pixel(x, y);
    return {qRed(pixel), qGreen(pixel), qBlue(pixel), true};
}

mviewer::core::NeighborhoodStats neighborhoodStatsFromDisplayImage(const QImage &image, int cx,
                                                                   int cy, int n)
{
    mviewer::core::NeighborhoodStats stats;
    if (image.isNull() || cx < 0 || cy < 0 || cx >= image.width() || cy >= image.height())
        return stats;

    const int half = n / 2;
    long long sum = 0;
    long long sumSq = 0;
    long long rSum = 0;
    long long gSum = 0;
    long long bSum = 0;
    int minValue = 255;
    int maxValue = 0;
    for (int dy = -half; dy <= half; ++dy)
    {
        const int y = cy + dy;
        if (y < 0 || y >= image.height())
            continue;
        for (int dx = -half; dx <= half; ++dx)
        {
            const int x = cx + dx;
            if (x < 0 || x >= image.width())
                continue;
            const QRgb pixel = image.pixel(x, y);
            const int r = qRed(pixel);
            const int g = qGreen(pixel);
            const int b = qBlue(pixel);
            const int luminance = static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5);
            sum += luminance;
            sumSq += static_cast<long long>(luminance) * luminance;
            rSum += r;
            gSum += g;
            bSum += b;
            minValue = std::min(minValue, luminance);
            maxValue = std::max(maxValue, luminance);
            ++stats.count;
        }
    }
    if (stats.count == 0)
        return stats;

    stats.mean = static_cast<double>(sum) / stats.count;
    const double variance = static_cast<double>(sumSq) / stats.count - stats.mean * stats.mean;
    stats.variance = std::max(0.0, variance);
    stats.stdDev = std::sqrt(stats.variance);
    stats.min = minValue;
    stats.max = maxValue;
    stats.rMean = static_cast<double>(rSum) / stats.count;
    stats.gMean = static_cast<double>(gSum) / stats.count;
    stats.bMean = static_cast<double>(bSum) / stats.count;
    return stats;
}
} // namespace

void CompareWorkspace::buildAnalysisPanel(QVBoxLayout *sideLay)
{
    // ── Pixel Inspector Pro ──────────────────────────────────────────────
    auto *inspHeader = new QHBoxLayout();
    inspHeader->addWidget(new QLabel(tr("像素检视"), this));
    m_coordLabel = new QLabel(QStringLiteral("(—, —)"), this);
    m_coordLabel->setStyleSheet("color:#888;");
    inspHeader->addWidget(m_coordLabel);
    inspHeader->addStretch(1);

    m_csCombo = new QComboBox(this);
    m_csCombo->addItems({QStringLiteral("RGB"), QStringLiteral("HEX"), QStringLiteral("HSV"),
                         QStringLiteral("Lab"), QStringLiteral("YUV"), QStringLiteral("YCbCr"),
                         QStringLiteral("XYZ")});
    m_csCombo->setToolTip(tr("像素值显示的色彩空间"));
    connect(m_csCombo, &QComboBox::currentIndexChanged, this,
            [this](int)
            {
                if (m_lastInspectX >= 0 && m_lastInspectY >= 0)
                    updateInspector(m_lastInspectX, m_lastInspectY);
            });
    inspHeader->addWidget(m_csCombo);

    m_kernelCombo = new QComboBox(this);
    m_kernelCombo->addItems({QStringLiteral("1×1"), QStringLiteral("3×3"), QStringLiteral("5×5"),
                             QStringLiteral("7×7")});
    m_kernelCombo->setCurrentIndex(1); // 3×3 by default
    m_kernelCombo->setToolTip(tr("邻域统计核大小（基准格亮度均值/方差）"));
    connect(m_kernelCombo, &QComboBox::currentIndexChanged, this,
            [this](int)
            {
                if (m_lastInspectX >= 0 && m_lastInspectY >= 0)
                    updateInspector(m_lastInspectX, m_lastInspectY);
            });
    inspHeader->addWidget(m_kernelCombo);
    sideLay->addLayout(inspHeader);

    m_inspector = new QTableWidget(this);
    m_inspector->setObjectName("pixelInspectorTable");
    m_inspector->setColumnCount(7);
    m_inspector->setHorizontalHeaderLabels({tr("#"), tr("名称"), QStringLiteral("R"),
                                            QStringLiteral("G"), QStringLiteral("B"),
                                            QStringLiteral("Δ"), QStringLiteral("16bit/RAW")});
    m_inspector->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_inspector->setSelectionMode(QAbstractItemView::NoSelection);
    m_inspector->horizontalHeader()->setStretchLastSection(true);
    m_inspector->setFixedHeight(200);
    sideLay->addWidget(m_inspector);

    m_statsLabel = new QLabel(tr("邻域统计: —"), this);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet("color:#888;");
    sideLay->addWidget(m_statsLabel);

    // ── Histogram (channels / log / ROI) ─────────────────────────────────
    m_histTitle = new QLabel(tr("直方图（全图）"), this);
    sideLay->addWidget(m_histTitle);

    auto *histOpts = new QHBoxLayout();
    auto makeChanChk = [this, histOpts](const QString &text, bool on, int channel) -> QCheckBox *
    {
        auto *chk = new QCheckBox(text, this);
        chk->setChecked(on);
        connect(chk, &QCheckBox::toggled, this,
                [this, channel](bool v)
                {
                    if (m_hist)
                        m_hist->setChannelVisible(channel, v);
                });
        histOpts->addWidget(chk);
        return chk;
    };
    m_histRChk = makeChanChk(QStringLiteral("R"), true, 0);
    m_histGChk = makeChanChk(QStringLiteral("G"), true, 1);
    m_histBChk = makeChanChk(QStringLiteral("B"), true, 2);
    m_histLumaChk = makeChanChk(tr("亮度"), false, 3);

    m_histLogChk = new QCheckBox(QStringLiteral("Log"), this);
    m_histLogChk->setToolTip(tr("对数纵轴：低计数区间不再被峰值淹没"));
    connect(m_histLogChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (m_hist)
                    m_hist->setLogScale(on);
            });
    histOpts->addWidget(m_histLogChk);

    m_roiHistChk = new QCheckBox(QStringLiteral("ROI"), this);
    m_roiHistChk->setToolTip(tr("仅统计当前 ROI 选区内的像素（先在图像上框选 ROI）"));
    connect(m_roiHistChk, &QCheckBox::toggled, this, [this](bool) { refreshHistograms(); });
    histOpts->addWidget(m_roiHistChk);
    histOpts->addStretch(1);
    sideLay->addLayout(histOpts);

    m_hist = new HistogramWidget(this);
    m_hist->setObjectName("analysisHistogram");
    m_hist->setMinimumHeight(140);
    sideLay->addWidget(m_hist, 1);

    // M16.5: per-pane histogram toggle
    m_perPaneHistChk = new QCheckBox(tr("每窗格独立直方图"), this);
    m_perPaneHistChk->setChecked(false);
    connect(m_perPaneHistChk, &QCheckBox::toggled, this, &CompareWorkspace::onPerPaneHistToggled);
    sideLay->addWidget(m_perPaneHistChk);

    // M16.7: per-pane histogram overlay toggle
    m_paneHistOverlayChk = new QCheckBox(tr("每格直方图叠加"), this);
    m_paneHistOverlayChk->setChecked(m_paneHistOverlay);
    connect(m_paneHistOverlayChk, &QCheckBox::toggled, this,
            &CompareWorkspace::onPaneHistOverlayToggled);
    sideLay->addWidget(m_paneHistOverlayChk);

    // M16.4: quick PSNR/SSIM metrics label (M23: + diff stats)
    sideLay->addWidget(new QLabel(tr("差异指标"), this));
    m_metricLabel = new QLabel(tr("PSNR: —  SSIM: —"), this);
    m_metricLabel->setObjectName("diffMetricsLabel");
    m_metricLabel->setWordWrap(true);
    m_metricLabel->setStyleSheet("color:#888;");
    sideLay->addWidget(m_metricLabel);
}

void CompareWorkspace::updateInspector(int x, int y)
{
    if (!m_inspector)
        return;
    m_lastInspectX = x;
    m_lastInspectY = y;
    if (m_coordLabel)
        m_coordLabel->setText(QStringLiteral("(%1, %2)").arg(x).arg(y));

    const int spaceIdx = m_csCombo ? std::clamp(m_csCombo->currentIndex(), 0, 6) : 0;
    const ColorSpace space = kSpaces[spaceIdx];
    m_inspector->setHorizontalHeaderLabels(
        {tr("#"), tr("名称"), QString::fromLatin1(kHeaders[spaceIdx][0]),
         QString::fromLatin1(kHeaders[spaceIdx][1]), QString::fromLatin1(kHeaders[spaceIdx][2]),
         QStringLiteral("Δ"), QStringLiteral("16bit/RAW")});

    const int n = m_engine.imageCount();
    const int baseIdx = diffBaseIndex();
    std::vector<DisplaySample> samples(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        if (i < m_cellViews.size() && m_cellViews[i])
            samples[static_cast<size_t>(i)] = sampleDisplayImage(m_cellViews[i]->image(), x, y);
    }

    m_inspector->clearContents();
    m_inspector->setRowCount(n);
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        const QString name = img ? QString::fromStdString(img->metadata().fileName) : QString();
        const DisplaySample &sample = samples[static_cast<size_t>(i)];
        m_inspector->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_inspector->setItem(i, 1, new QTableWidgetItem(name));

        if (!sample.valid)
        {
            const QString invalid = QStringLiteral("无效");
            m_inspector->setItem(i, 2, new QTableWidgetItem(invalid));
            m_inspector->setItem(
                i, 3,
                new QTableWidgetItem(space == ColorSpace::HEX ? QStringLiteral("—") : invalid));
            m_inspector->setItem(
                i, 4,
                new QTableWidgetItem(space == ColorSpace::HEX ? QStringLiteral("—") : invalid));
        }
        else if (space == ColorSpace::HEX)
        {
            const auto hex =
                mviewer::core::toHex(static_cast<uint8_t>(sample.r), static_cast<uint8_t>(sample.g),
                                     static_cast<uint8_t>(sample.b));
            m_inspector->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(hex)));
            m_inspector->setItem(i, 3, new QTableWidgetItem(QStringLiteral("—")));
            m_inspector->setItem(i, 4, new QTableWidgetItem(QStringLiteral("—")));
        }
        else
        {
            const auto color = mviewer::core::toColorSpace(static_cast<uint8_t>(sample.r),
                                                           static_cast<uint8_t>(sample.g),
                                                           static_cast<uint8_t>(sample.b), space);
            m_inspector->setItem(i, 2, new QTableWidgetItem(formatChannel(space, color.c1)));
            m_inspector->setItem(i, 3, new QTableWidgetItem(formatChannel(space, color.c2)));
            m_inspector->setItem(i, 4, new QTableWidgetItem(formatChannel(space, color.c3)));
        }

        QString delta = QStringLiteral("无效");
        if (sample.valid && baseIdx >= 0 && baseIdx < n &&
            samples[static_cast<size_t>(baseIdx)].valid)
        {
            const DisplaySample &base = samples[static_cast<size_t>(baseIdx)];
            const int dr = sample.r - base.r;
            const int dg = sample.g - base.g;
            const int db = sample.b - base.b;
            const double dist = std::sqrt(static_cast<double>(dr * dr + dg * dg + db * db));
            delta = (i == baseIdx) ? QStringLiteral("0") : QString::number(dist, 'f', 0);
        }
        m_inspector->setItem(i, 5, new QTableWidgetItem(delta));

        QString raw16 = QStringLiteral("无效");
        if (sample.valid)
        {
            const bool identity = i >= static_cast<int>(m_cellAdjusts.size()) ||
                                  m_cellAdjusts[static_cast<size_t>(i)].isIdentity();
            uint16_t r16 = 0, g16 = 0, b16 = 0;
            if (identity && img && img->hasRaw16() && img->raw16At(x, y, r16, g16, b16))
                raw16 = QString("%1,%2,%3").arg(r16).arg(g16).arg(b16);
            else
                raw16 = QString("%1,%2,%3 (%4)")
                            .arg(sample.r)
                            .arg(sample.g)
                            .arg(sample.b)
                            .arg(identity ? QStringLiteral("预览") : QStringLiteral("已调整预览"));
        }
        m_inspector->setItem(i, 6, new QTableWidgetItem(raw16));
    }

    if (m_statsLabel)
    {
        static const int kKernels[] = {1, 3, 5, 7};
        const int kernelIndex = m_kernelCombo ? std::clamp(m_kernelCombo->currentIndex(), 0, 3) : 1;
        const int kernel = kKernels[kernelIndex];
        const QImage *baseImage = nullptr;
        if (baseIdx >= 0 && baseIdx < m_cellViews.size() && m_cellViews[baseIdx])
            baseImage = &m_cellViews[baseIdx]->image();
        const auto stats = baseImage ? neighborhoodStatsFromDisplayImage(*baseImage, x, y, kernel)
                                     : mviewer::core::NeighborhoodStats{};
        if (stats.count > 0)
        {
            m_statsLabel->setText(tr("邻域 %1×%1: 亮度 μ=%2 σ=%3 [%4, %5] · RGB均值(%6, %7, %8)")
                                      .arg(kernel)
                                      .arg(stats.mean, 0, 'f', 1)
                                      .arg(stats.stdDev, 0, 'f', 1)
                                      .arg(static_cast<int>(stats.min))
                                      .arg(static_cast<int>(stats.max))
                                      .arg(stats.rMean, 0, 'f', 1)
                                      .arg(stats.gMean, 0, 'f', 1)
                                      .arg(stats.bMean, 0, 'f', 1));
        }
        else
            m_statsLabel->setText(tr("邻域统计: —"));
    }
}

void CompareWorkspace::refreshHistograms()
{
    if (!m_hist)
        return;
    const int n = m_engine.imageCount();

    // M23: ROI + Histogram 联动 — when the ROI toggle is on and a selection
    // exists, every histogram is computed over the ROI only.
    const bool useRoi = m_roiHistChk && m_roiHistChk->isChecked() && !m_lastSelection.isEmpty();
    auto histOf = [this, useRoi](int idx) -> mviewer::core::Histogram
    {
        const ImageData pixels = adjustedPixels(idx);
        if (pixels.isNull())
            return mviewer::core::Histogram{};
        if (useRoi)
            return mviewer::core::computeHistogram(pixels, m_lastSelection.x, m_lastSelection.y,
                                                   m_lastSelection.width, m_lastSelection.height);
        return mviewer::core::computeHistogram(pixels);
    };

    if (m_histTitle)
    {
        if (useRoi)
            m_histTitle->setText(tr("直方图（ROI %1,%2 %3×%4）")
                                     .arg(m_lastSelection.x)
                                     .arg(m_lastSelection.y)
                                     .arg(m_lastSelection.width)
                                     .arg(m_lastSelection.height));
        else
            m_histTitle->setText(tr("直方图（全图）"));
    }

    if (m_perPaneHist && m_editIdx >= 0 && m_editIdx < n)
    {
        // Per-pane: show only the selected cell's histogram
        m_hist->setHistograms({histOf(m_editIdx)});
    }
    else
    {
        // Overlaid: show all
        std::vector<mviewer::core::Histogram> hists;
        hists.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            hists.push_back(histOf(i));
        m_hist->setHistograms(hists);
        // M16.7: keep the in-cell per-pane histograms in sync.
        for (int i = 0; i < static_cast<int>(m_cellHists.size()); ++i)
            refreshCellHist(i);
    }
}
