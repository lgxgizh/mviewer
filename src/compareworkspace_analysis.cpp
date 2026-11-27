// M23: CompareWorkspace analysis panel — Pixel Inspector Pro (multi color-space
// readout + neighborhood stats) and the channel/log/ROI-aware histogram section.
// Split out per ADR 014 (keep compareworkspace.cpp under the line budget).
#include "compareworkspace_p.h"

#include "Theme.h"
#include "core/analysis/PixelInspector.h"
#include "core/image/rawplain/PlainRawFile.h"

#include <cmath>
#include <utility>

#include <QApplication>
#include <QClipboard>
#include <QMenu>

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

// M30: write a cell's text into the existing QTableWidgetItem, lazily creating
// it on first use. Ordinary inspector renders update text in place instead of
// destroying and reallocating every cell (the old clearContents + new-item
// path churned the item heap on every hover). Only setText when the text
// actually changed, so stable index/name cells skip the model data-change and
// repaint work.
void setCellText(QTableWidget *table, int row, int col, const QString &text,
                 const QString &toolTip = QString())
{
    QTableWidgetItem *item = table->item(row, col);
    if (!item)
    {
        item = new QTableWidgetItem;
        table->setItem(row, col, item);
    }
    if (item->text() != text)
        item->setText(text);
    if (item->toolTip() != toolTip)
        item->setToolTip(toolTip);
}
void setupInspectorContextMenu(QTableWidget *inspector, QWidget *parent)
{
    if (!inspector)
        return;
    inspector->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(
        inspector, &QTableWidget::customContextMenuRequested, parent,
        [inspector, parent](const QPoint &pos)
        {
            QTableWidgetItem *item = inspector->itemAt(pos);
            QMenu menu(parent);
            if (item && !item->text().isEmpty())
            {
                const QString cellText = item->text();
                menu.addAction(QObject::tr("Copy cell contents (%1)").arg(cellText),
                               [cellText]() { QApplication::clipboard()->setText(cellText); });
            }
            auto rowCells = [inspector](int r) -> QStringList
            {
                QStringList cells;
                for (int c = 0; c < inspector->columnCount(); ++c)
                {
                    auto *it = inspector->item(r, c);
                    cells << (it ? it->text() : QString());
                }
                return cells;
            };
            const int row = item ? item->row() : inspector->currentRow();
            if (row >= 0 && row < inspector->rowCount())
            {
                const QString rowText = rowCells(row).join('\t');
                menu.addAction(QObject::tr("Copy this row's data"),
                               [rowText]() { QApplication::clipboard()->setText(rowText); });
                auto *item0 = inspector->item(row, 0);
                const QVariant varRgb = item0 ? item0->data(Qt::UserRole) : QVariant();
                if (varRgb.isValid())
                {
                    const uint u = varRgb.toUInt();
                    const int r = qRed(u), g = qGreen(u), b = qBlue(u);
                    const QString hex = QString("#%1%2%3")
                                            .arg(r, 2, 16, QLatin1Char('0'))
                                            .arg(g, 2, 16, QLatin1Char('0'))
                                            .arg(b, 2, 16, QLatin1Char('0'))
                                            .toUpper();
                    const QString rgb = QString("rgb(%1, %2, %3)").arg(r).arg(g).arg(b);
                    menu.addAction(QObject::tr("Copy HEX color (%1)").arg(hex),
                                   [hex]() { QApplication::clipboard()->setText(hex); });
                    menu.addAction(QObject::tr("Copy RGB color (%1)").arg(rgb),
                                   [rgb]() { QApplication::clipboard()->setText(rgb); });
                }
            }
            if (inspector->rowCount() > 0)
            {
                auto copyFormatted = [inspector, rowCells](bool md)
                {
                    QStringList lines, headers, seps;
                    for (int c = 0; c < inspector->columnCount(); ++c)
                    {
                        auto *h = inspector->horizontalHeaderItem(c);
                        headers << (h ? h->text() : QString());
                        if (md)
                            seps << "---";
                    }
                    lines << (md ? "| " + headers.join(" | ") + " |" : headers.join('\t'));
                    if (md)
                        lines << "| " + seps.join(" | ") + " |";
                    for (int r = 0; r < inspector->rowCount(); ++r)
                    {
                        const auto c = rowCells(r);
                        lines << (md ? "| " + c.join(" | ") + " |" : c.join('\t'));
                    }
                    QApplication::clipboard()->setText(lines.join('\n'));
                };
                menu.addAction(QObject::tr("Copy all table data"), [=]() { copyFormatted(false); });
                menu.addAction(QObject::tr("Copy as Markdown table"),
                               [=]() { copyFormatted(true); });
            }
            if (!menu.isEmpty())
                menu.exec(inspector->viewport()->mapToGlobal(pos));
        });
}
} // namespace

mviewer::core::AnalysisAdjustment CompareWorkspace::analysisAdjustment(const CellAdjust &adjust)
{
    return {adjust.brightness, adjust.contrast, adjust.gamma, adjust.rGain,   adjust.bGain,
            adjust.rotation,   adjust.flipH,    adjust.flipV, adjust.hasCrop, adjust.cropX,
            adjust.cropY,      adjust.cropW,    adjust.cropH};
}

void CompareWorkspace::buildAnalysisPanel(QVBoxLayout *sideLay)
{
    // ── Pixel Inspector Pro ──────────────────────────────────────────────
    auto *inspHeader = new QHBoxLayout();
    inspHeader->addWidget(new QLabel(tr("Pixel Inspector"), this));
    d->coordLabel = new QLabel(QStringLiteral("(—, —)"), this);
    d->coordLabel->setObjectName("pixelInspectorCoordLabel");
    d->coordLabel->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
    inspHeader->addWidget(d->coordLabel);
    inspHeader->addStretch(1);

    d->csCombo = new QComboBox(this);
    d->csCombo->setObjectName("pixelInspectorColorSpaceCombo");
    d->csCombo->addItems({QStringLiteral("RGB"), QStringLiteral("HEX"), QStringLiteral("HSV"),
                         QStringLiteral("Lab"), QStringLiteral("YUV"), QStringLiteral("YCbCr"),
                         QStringLiteral("XYZ")});
    d->csCombo->setToolTip(tr("Color space for pixel value display"));
    connect(d->csCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx)
            {
                updateInspectorHeader(idx);
                if (m_lastInspectX >= 0 && m_lastInspectY >= 0)
                    requestInspectorUpdate(m_lastInspectX, m_lastInspectY);
            });
    inspHeader->addWidget(d->csCombo);

    d->kernelCombo = new QComboBox(this);
    d->kernelCombo->setObjectName("pixelInspectorKernelCombo");
    d->kernelCombo->addItems({QStringLiteral("1×1"), QStringLiteral("3×3"), QStringLiteral("5×5"),
                             QStringLiteral("7×7")});
    d->kernelCombo->setCurrentIndex(1); // 3×3 by default
    d->kernelCombo->setToolTip(
        tr("Neighborhood stats kernel size (base cell luminance mean/variance)"));
    connect(d->kernelCombo, &QComboBox::currentIndexChanged, this,
            [this](int)
            {
                if (m_lastInspectX >= 0 && m_lastInspectY >= 0)
                    requestInspectorUpdate(m_lastInspectX, m_lastInspectY);
            });
    inspHeader->addWidget(d->kernelCombo);
    sideLay->addLayout(inspHeader);

    d->inspector = new QTableWidget(this);
    d->inspector->setObjectName("pixelInspectorTable");
    d->inspector->setColumnCount(7);
    d->inspector->setHorizontalHeaderLabels({tr("#"), tr("Name"), QStringLiteral("R"),
                                            QStringLiteral("G"), QStringLiteral("B"),
                                            QStringLiteral("Δ"), QStringLiteral("16bit/RAW")});
    d->inspector->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->inspector->setSelectionMode(QAbstractItemView::SingleSelection);
    d->inspector->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->inspector->horizontalHeader()->setStretchLastSection(true);
    d->inspector->setFixedHeight(200);
    setupInspectorContextMenu(d->inspector, this);
    sideLay->addWidget(d->inspector);

    d->statsLabel = new QLabel(tr("Neighborhood statistics: —"), this);
    d->statsLabel->setObjectName("pixelInspectorStatsLabel");
    d->statsLabel->setWordWrap(true);
    d->statsLabel->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
    sideLay->addWidget(d->statsLabel);

    buildHistogramPanel(sideLay);
    buildROIMeasurementPanel(sideLay);
}

void CompareWorkspace::buildHistogramPanel(QVBoxLayout *sideLay)
{
    // ── Histogram (channels / log / ROI) ─────────────────────────────────
    d->histTitle = new QLabel(tr("Histogram (full image)"), this);
    sideLay->addWidget(d->histTitle);

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
    d->histRChk = makeChanChk(QStringLiteral("R"), true, 0);
    d->histGChk = makeChanChk(QStringLiteral("G"), true, 1);
    d->histBChk = makeChanChk(QStringLiteral("B"), true, 2);
    d->histLumaChk = makeChanChk(tr("Brightness"), false, 3);
    d->histVChk = makeChanChk(tr("V (Value)"), false, 4);
    d->histVChk->setToolTip(tr("V component in HSV space (maximum channel value max(R,G,B))"));

    d->histLogChk = new QCheckBox(QStringLiteral("Log"), this);
    d->histLogChk->setToolTip(tr("Log Y-axis: low-count bins are no longer drowned out by peaks"));
    connect(d->histLogChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (m_hist)
                    m_hist->setLogScale(on);
            });
    histOpts->addWidget(d->histLogChk);

    d->roiHistChk = new QCheckBox(QStringLiteral("ROI"), this);
    d->roiHistChk->setObjectName("roiHistogramToggle");
    d->roiHistChk->setToolTip(
        tr("Count only pixels inside the current ROI (box-select an ROI on the image first)"));
    connect(d->roiHistChk, &QCheckBox::toggled, this, [this](bool) { refreshHistograms(); });
    histOpts->addWidget(d->roiHistChk);
    histOpts->addStretch(1);
    sideLay->addLayout(histOpts);

    m_hist = new HistogramWidget(this);
    m_hist->setObjectName("analysisHistogram");
    m_hist->setMinimumHeight(200);
    sideLay->addWidget(m_hist, 1);

    // M16.5: per-pane histogram toggle
    d->perPaneHistChk = new QCheckBox(tr("Independent histogram per pane"), this);
    d->perPaneHistChk->setObjectName("perPaneHistogramToggle");
    d->perPaneHistChk->setChecked(false);
    connect(d->perPaneHistChk, &QCheckBox::toggled, this, &CompareWorkspace::onPerPaneHistToggled);
    sideLay->addWidget(d->perPaneHistChk);
}

void CompareWorkspace::requestInspectorUpdate(int x, int y)
{
    if (x < 0 || y < 0)
        return;
    m_lastInspectX = x;
    m_lastInspectY = y;
    if (m_inspectQueued)
        return; // one queued render per event-loop turn is already scheduled
    m_inspectQueued = true;
    // Queued on `this`: if the workspace is destroyed before the event runs, Qt
    // drops it (receiver lifetime semantics) — no stale render, no use-after-free.
    // The callback renders the CURRENT m_lastInspectX/Y with the CURRENT semantic
    // state, so an older queued request can never overwrite newer state.
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
            m_inspectQueued = false;
            if (m_lastInspectX >= 0 && m_lastInspectY >= 0)
                updateInspector(m_lastInspectX, m_lastInspectY);
        },
        Qt::QueuedConnection);
}

void CompareWorkspace::updateInspectorHeader(int spaceIdx)
{
    if (!d->inspector)
        return;
    const int idx = std::clamp(spaceIdx, 0, 6);
    if (idx == m_inspectorSpaceIdx)
        return;
    d->inspector->setHorizontalHeaderLabels(
        {tr("#"), tr("Name"), QString::fromLatin1(kHeaders[idx][0]),
         QString::fromLatin1(kHeaders[idx][1]), QString::fromLatin1(kHeaders[idx][2]),
         QStringLiteral("Δ"), QStringLiteral("16bit/RAW")});
    m_inspectorSpaceIdx = idx;
}

void CompareWorkspace::updateInspector(int x, int y)
{
    if (!d->inspector)
        return;
    // M30 diagnostic: expose the actual render count as a dynamic QObject
    // property so the deterministic regression tests can observe coalescing
    // without widening the public API.
    ++m_inspectorRenderCount;
    setProperty("inspectorRenderCount", m_inspectorRenderCount);

    m_lastInspectX = x;
    m_lastInspectY = y;
    if (d->coordLabel)
        d->coordLabel->setText(QStringLiteral("(%1, %2)").arg(x).arg(y));

    const int spaceIdx = d->csCombo ? std::clamp(d->csCombo->currentIndex(), 0, 6) : 0;
    const ColorSpace space = kSpaces[spaceIdx];
    updateInspectorHeader(spaceIdx);

    const int n = m_engine.imageCount();
    const int baseIdx = diffBaseIndex();
    std::vector<InspectorSample> samples(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        if (!img)
            continue;
        const CellAdjust adjust = i < static_cast<int>(m_cellAdjusts.size())
                                      ? m_cellAdjusts[static_cast<size_t>(i)]
                                      : CellAdjust{};
        const auto sourceSample =
            mviewer::core::sampleAnalysisPixel(img->pixels(), analysisAdjustment(adjust), x, y);
        samples[static_cast<size_t>(i)] = {sourceSample.r, sourceSample.g, sourceSample.b,
                                           sourceSample.valid};
    }

    // Reuse existing QTableWidgetItem objects across ordinary hovers: only
    // resize the row count when it actually changed and update text in place,
    // so successive renders do not destroy/reallocate every cell.
    if (d->inspector->rowCount() != n)
        d->inspector->setRowCount(n);

    updateInspectorRows(samples, space, baseIdx, x, y);

    if (d->statsLabel)
    {
        static const int kKernels[] = {1, 3, 5, 7};
        const int kernelIndex = d->kernelCombo ? std::clamp(d->kernelCombo->currentIndex(), 0, 3) : 1;
        const int kernel = kKernels[kernelIndex];
        const ImageFrame *baseFrame = m_engine.imageAt(baseIdx);
        const CellAdjust baseAdjust =
            baseIdx >= 0 && baseIdx < static_cast<int>(m_cellAdjusts.size())
                ? m_cellAdjusts[static_cast<size_t>(baseIdx)]
                : CellAdjust{};
        const auto stats =
            baseFrame ? mviewer::core::neighborhoodStats(
                            baseFrame->pixels(), analysisAdjustment(baseAdjust), x, y, kernel)
                      : mviewer::core::NeighborhoodStats{};
        if (stats.count > 0)
        {
            d->statsLabel->setText(tr("Neighborhood %1×%1: luminance μ=%2 σ=%3 [%4, %5] · RGB mean "
                                     "(%6, %7, %8) · V mean %9")
                                      .arg(kernel)
                                      .arg(stats.mean, 0, 'f', 1)
                                      .arg(stats.stdDev, 0, 'f', 1)
                                      .arg(static_cast<int>(stats.min))
                                      .arg(static_cast<int>(stats.max))
                                      .arg(stats.rMean, 0, 'f', 1)
                                      .arg(stats.gMean, 0, 'f', 1)
                                      .arg(stats.bMean, 0, 'f', 1)
                                      .arg(stats.vMean, 0, 'f', 1));
        }
        else
            d->statsLabel->setText(tr("Neighborhood statistics: —"));
    }
}

void CompareWorkspace::updateInspectorRows(const std::vector<InspectorSample> &samples,
                                           ColorSpace space, int baseIdx, int x, int y)
{
    const int n = static_cast<int>(samples.size());
    for (int i = 0; i < n; ++i)
    {
        const InspectorSample &sample = samples[static_cast<size_t>(i)];
        const ImageFrame *img = m_engine.imageAt(i);
        setCellText(d->inspector, i, 0, QString::number(i + 1));
        const QString name = img ? QString::fromStdString(img->metadata().fileName) : QString();
        setCellText(d->inspector, i, 1, name);
        if (auto *it0 = d->inspector->item(i, 0))
        {
            it0->setData(Qt::UserRole,
                         sample.valid
                             ? QVariant(static_cast<uint>(qRgb(sample.r, sample.g, sample.b)))
                             : QVariant());
        }

        if (!sample.valid)
        {
            const QString invalid = QStringLiteral("Invalid");
            setCellText(d->inspector, i, 2, invalid);
            setCellText(d->inspector, i, 3,
                        space == ColorSpace::HEX ? QStringLiteral("—") : invalid);
            setCellText(d->inspector, i, 4,
                        space == ColorSpace::HEX ? QStringLiteral("—") : invalid);
        }
        else if (space == ColorSpace::HEX)
        {
            const auto hex =
                mviewer::core::toHex(static_cast<uint8_t>(sample.r), static_cast<uint8_t>(sample.g),
                                     static_cast<uint8_t>(sample.b));
            setCellText(d->inspector, i, 2, QString::fromStdString(hex));
            setCellText(d->inspector, i, 3, QStringLiteral("—"));
            setCellText(d->inspector, i, 4, QStringLiteral("—"));
        }
        else
        {
            const auto matrix = img ? mviewer::yuvMatrixFromMetadata(img->metadata())
                                    : mviewer::core::yuv::Matrix::BT601;
            const auto color = mviewer::core::toColorSpace(
                static_cast<uint8_t>(sample.r), static_cast<uint8_t>(sample.g),
                static_cast<uint8_t>(sample.b), space, matrix);
            setCellText(d->inspector, i, 2, formatChannel(space, color.c1));
            setCellText(d->inspector, i, 3, formatChannel(space, color.c2));
            setCellText(d->inspector, i, 4, formatChannel(space, color.c3));
        }

        QString delta = QStringLiteral("Invalid");
        QString deltaTip;
        if (sample.valid && baseIdx >= 0 && baseIdx < n &&
            samples[static_cast<size_t>(baseIdx)].valid)
        {
            const InspectorSample &base = samples[static_cast<size_t>(baseIdx)];
            const int dr = sample.r - base.r, dg = sample.g - base.g, db = sample.b - base.b;
            const double dist = std::sqrt(static_cast<double>(dr * dr + dg * dg + db * db));
            const auto labS = mviewer::core::toColorSpace(
                static_cast<uint8_t>(sample.r), static_cast<uint8_t>(sample.g),
                static_cast<uint8_t>(sample.b), ColorSpace::Lab);
            const auto labB = mviewer::core::toColorSpace(
                static_cast<uint8_t>(base.r), static_cast<uint8_t>(base.g),
                static_cast<uint8_t>(base.b), ColorSpace::Lab);
            const double dL = labS.c1 - labB.c1, da = labS.c2 - labB.c2, dbv = labS.c3 - labB.c3;
            const double dE76 = std::sqrt(dL * dL + da * da + dbv * dbv);
            if (i == baseIdx)
            {
                delta = QStringLiteral("0");
                deltaTip = tr("Reference image (difference is 0)");
            }
            else
            {
                delta = (space == ColorSpace::Lab) ? QString::number(dE76, 'f', 1)
                                                   : QString::number(dist, 'f', 0);
                deltaTip = tr("Compared to baseline: ΔR=%1, ΔG=%2, ΔB=%3 (Euclidean distance: %4, "
                              "ΔE76: %5)")
                               .arg(dr >= 0 ? QString("+%1").arg(dr) : QString::number(dr))
                               .arg(dg >= 0 ? QString("+%1").arg(dg) : QString::number(dg))
                               .arg(db >= 0 ? QString("+%1").arg(db) : QString::number(db))
                               .arg(dist, 0, 'f', 2)
                               .arg(dE76, 0, 'f', 2);
            }
        }
        setCellText(d->inspector, i, 5, delta, deltaTip);

        QString raw16 = QStringLiteral("Invalid");
        if (sample.valid)
        {
            const bool identity = i >= static_cast<int>(m_cellAdjusts.size()) ||
                                  m_cellAdjusts[static_cast<size_t>(i)].isIdentity();
            uint16_t r16 = 0, g16 = 0, b16 = 0;
            if (identity)
            {
                if (!img)
                    img = m_engine.imageAt(i);
                if (img && img->hasRaw16() && img->raw16At(x, y, r16, g16, b16))
                    raw16 = QString("%1,%2,%3").arg(r16).arg(g16).arg(b16);
                else
                    raw16 = QString("%1,%2,%3 (%4)")
                                .arg(sample.r)
                                .arg(sample.g)
                                .arg(sample.b)
                                .arg(QStringLiteral("Preview"));
            }
            else
            {
                raw16 = QString("%1,%2,%3 (%4)")
                            .arg(sample.r)
                            .arg(sample.g)
                            .arg(sample.b)
                            .arg(QStringLiteral("Adjusted preview"));
            }
        }
        setCellText(d->inspector, i, 6, raw16);
    }
}

QString CompareWorkspace::formatPixelInfo(int cellIndex, const QString &cellName, int x, int y,
                                          const mviewer::core::AnalysisPixel &sample) const
{
    QString info = QString("[%1] (%2,%3) RGB(%4,%5,%6)")
                       .arg(cellName)
                       .arg(x)
                       .arg(y)
                       .arg(sample.r)
                       .arg(sample.g)
                       .arg(sample.b);
    // ADR 033: stored sensor value of a plain .raw cell shown untransformed
    // (a temporary rotate / flip / crop moves pixels off source coordinates).
    const ImageFrame *cellFrame = m_engine.imageAt(cellIndex);
    const CellAdjust cellAdjust =
        cellIndex >= 0 && cellIndex < static_cast<int>(m_cellAdjusts.size())
            ? m_cellAdjusts[static_cast<size_t>(cellIndex)]
            : CellAdjust{};
    if (cellFrame && cellAdjust.rotation % 360 == 0 && !cellAdjust.flipH && !cellAdjust.flipV &&
        !cellAdjust.hasCrop)
    {
        const std::string rawText = mviewer::core::rawplain::plainRawSampleText(
            cellFrame->metadata().filePath, x, y,
            mviewer::core::rawplain::SampleRead::CachedOrAsync);
        if (!rawText.empty())
            info += QStringLiteral("  ") + QString::fromStdString(rawText);
    }
    const int baseIdx = diffBaseIndex();
    if (cellIndex != baseIdx && baseIdx >= 0 && baseIdx < m_engine.imageCount())
    {
        const ImageFrame *baseFrame = m_engine.imageAt(baseIdx);
        const CellAdjust baseAdjust = baseIdx < static_cast<int>(m_cellAdjusts.size())
                                          ? m_cellAdjusts[static_cast<size_t>(baseIdx)]
                                          : CellAdjust{};
        const auto baseSample =
            baseFrame ? mviewer::core::sampleAnalysisPixel(baseFrame->pixels(),
                                                           analysisAdjustment(baseAdjust), x, y)
                      : mviewer::core::AnalysisPixel{};
        if (baseSample.valid)
        {
            const int dr = sample.r - baseSample.r;
            const int dg = sample.g - baseSample.g;
            const int db = sample.b - baseSample.b;
            info += QString(" Δ(%1,%2,%3)")
                        .arg(dr >= 0 ? QString("+%1").arg(dr) : QString::number(dr))
                        .arg(dg >= 0 ? QString("+%1").arg(dg) : QString::number(dg))
                        .arg(db >= 0 ? QString("+%1").arg(db) : QString::number(db));
        }
    }
    return info;
}
