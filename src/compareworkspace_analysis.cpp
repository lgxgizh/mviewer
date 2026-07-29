// M23: CompareWorkspace analysis panel — Pixel Inspector Pro (multi color-space
// readout + neighborhood stats) and the channel/log/ROI-aware histogram section.
// Split out per ADR 014 (keep compareworkspace.cpp under the line budget).
#include "compareworkspace_p.h"

#include "core/analysis/PixelInspector.h"

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
    m_inspector->setColumnCount(6);
    m_inspector->setHorizontalHeaderLabels({tr("#"), tr("名称"), QStringLiteral("R"),
                                            QStringLiteral("G"), QStringLiteral("B"),
                                            QStringLiteral("Δ")});
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

    const auto probe = m_engine.inspectPixel(x, y, diffBaseIndex());
    if (probe.samples.empty() || !probe.samples[0].valid)
    {
        m_inspector->setRowCount(0);
        if (m_coordLabel)
            m_coordLabel->setText(QStringLiteral("(—, —)"));
        if (m_statsLabel)
            m_statsLabel->setText(tr("邻域统计: —"));
        return;
    }

    if (m_coordLabel)
        m_coordLabel->setText(QStringLiteral("(%1, %2)").arg(x).arg(y));

    const int spaceIdx = m_csCombo ? std::clamp(m_csCombo->currentIndex(), 0, 6) : 0;
    const ColorSpace space = kSpaces[spaceIdx];
    m_inspector->setHorizontalHeaderLabels(
        {tr("#"), tr("名称"), QString::fromLatin1(kHeaders[spaceIdx][0]),
         QString::fromLatin1(kHeaders[spaceIdx][1]), QString::fromLatin1(kHeaders[spaceIdx][2]),
         QStringLiteral("Δ")});

    const int n = m_engine.imageCount();
    m_inspector->setRowCount(n);
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        const QString name = img ? QString::fromStdString(img->metadata().fileName) : QString();
        if (static_cast<size_t>(i) >= probe.samples.size())
            continue;
        const auto &s = probe.samples[static_cast<size_t>(i)];
        const double dist = (static_cast<size_t>(i) < probe.deltas.size())
                                ? probe.deltas[static_cast<size_t>(i)].dist
                                : 0.0;
        m_inspector->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_inspector->setItem(i, 1, new QTableWidgetItem(name));
        if (space == ColorSpace::HEX)
        {
            const auto hex = mviewer::core::toHex(
                static_cast<uint8_t>(s.r), static_cast<uint8_t>(s.g), static_cast<uint8_t>(s.b));
            m_inspector->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(hex)));
            m_inspector->setItem(i, 3, new QTableWidgetItem(QString()));
            m_inspector->setItem(i, 4, new QTableWidgetItem(QString()));
        }
        else
        {
            const auto t =
                mviewer::core::toColorSpace(static_cast<uint8_t>(s.r), static_cast<uint8_t>(s.g),
                                            static_cast<uint8_t>(s.b), space);
            m_inspector->setItem(i, 2, new QTableWidgetItem(formatChannel(space, t.c1)));
            m_inspector->setItem(i, 3, new QTableWidgetItem(formatChannel(space, t.c2)));
            m_inspector->setItem(i, 4, new QTableWidgetItem(formatChannel(space, t.c3)));
        }
        m_inspector->setItem(i, 5, new QTableWidgetItem(QString::number(static_cast<int>(dist))));
    }

    // Neighborhood statistics of the base (reference) cell around the cursor.
    if (m_statsLabel)
    {
        static const int kKernels[] = {1, 3, 5, 7};
        const int kIdx = m_kernelCombo ? std::clamp(m_kernelCombo->currentIndex(), 0, 3) : 1;
        const int kn = kKernels[kIdx];
        const ImageFrame *base = m_engine.imageAt(diffBaseIndex());
        bool ok = false;
        if (base && !base->pixels().isNull())
        {
            const auto v = base->pixels().view();
            if (v.format == PixelFormat::RGB24)
            {
                const auto st = mviewer::core::neighborhoodStats(
                    v.data, static_cast<int>(v.stride()), v.width, v.height, x, y, kn);
                if (st.count > 0)
                {
                    m_statsLabel->setText(
                        tr("邻域 %1×%1: 亮度 μ=%2 σ=%3 [%4, %5] · RGB均值 (%6, %7, %8)")
                            .arg(kn)
                            .arg(st.mean, 0, 'f', 1)
                            .arg(st.stdDev, 0, 'f', 1)
                            .arg(static_cast<int>(st.min))
                            .arg(static_cast<int>(st.max))
                            .arg(st.rMean, 0, 'f', 1)
                            .arg(st.gMean, 0, 'f', 1)
                            .arg(st.bMean, 0, 'f', 1));
                    ok = true;
                }
            }
        }
        if (!ok)
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
    auto histOf = [this, useRoi](const ImageFrame *img) -> mviewer::core::Histogram
    {
        if (!img)
            return mviewer::core::Histogram{};
        if (useRoi)
            return mviewer::core::computeHistogram(img->pixels(), m_lastSelection.x,
                                                   m_lastSelection.y, m_lastSelection.width,
                                                   m_lastSelection.height);
        return mviewer::core::computeHistogram(img->pixels());
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
        m_hist->setHistograms({histOf(m_engine.imageAt(m_editIdx))});
    }
    else
    {
        // Overlaid: show all
        std::vector<mviewer::core::Histogram> hists;
        hists.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            hists.push_back(histOf(m_engine.imageAt(i)));
        m_hist->setHistograms(hists);
        // M16.7: keep the in-cell per-pane histograms in sync.
        for (int i = 0; i < static_cast<int>(m_cellHists.size()); ++i)
            refreshCellHist(i);
    }
}
