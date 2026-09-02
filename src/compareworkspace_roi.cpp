#include "compareworkspace_p.h"

#include "core/image/ExifOrientation.h"
#include "core/image/SourceImage.h"

#include <QMetaObject>

#include <algorithm>
#include <cmath>

namespace
{
QString ratioText(const mviewer::core::ROIChannelStats &stats, bool red)
{
    if (!stats.ratiosValid)
        return QStringLiteral("—");
    return QString::number(red ? stats.rOverG : stats.bOverG, 'f', 4);
}

QString meanText(double value)
{
    return QString::number(value, 'f', 2);
}
} // namespace

void CompareWorkspace::buildROIMeasurementPanel(QVBoxLayout *sideLay)
{
    auto *roiHeader = new QHBoxLayout();
    auto *roiTitle = new QLabel(tr("ROI Measurement — Source RGB"), this);
    roiTitle->setObjectName("roiMeasurementTitle");
    roiHeader->addWidget(roiTitle);
    roiHeader->addStretch(1);
    m_clearRoiBtn = new QPushButton(tr("Clear ROI"), this);
    m_clearRoiBtn->setObjectName("clearRoiButton");
    m_clearRoiBtn->setEnabled(false);
    connect(m_clearRoiBtn, &QPushButton::clicked, this, &CompareWorkspace::clearROI);
    roiHeader->addWidget(m_clearRoiBtn);
    sideLay->addLayout(roiHeader);

    m_roiStatusLabel = new QLabel(this);
    m_roiStatusLabel->setObjectName("roiStatusLabel");
    m_roiStatusLabel->setWordWrap(true);
    m_roiStatusLabel->setStyleSheet("color:#888;");
    sideLay->addWidget(m_roiStatusLabel);

    m_roiGeometryLabel = new QLabel(tr("ROI: —"), this);
    m_roiGeometryLabel->setObjectName("roiGeometryLabel");
    sideLay->addWidget(m_roiGeometryLabel);

    m_roiTable = new QTableWidget(this);
    m_roiTable->setObjectName("roiMeasurementTable");
    m_roiTable->setColumnCount(7);
    m_roiTable->setHorizontalHeaderLabels(
        {tr("Image"), QStringLiteral("R Mean"), QStringLiteral("G Mean"), QStringLiteral("B Mean"),
         QStringLiteral("R/G"), QStringLiteral("B/G"), tr("Pixels")});
    m_roiTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_roiTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_roiTable->verticalHeader()->setVisible(false);
    m_roiTable->horizontalHeader()->setStretchLastSection(true);
    m_roiTable->setMinimumHeight(80);
    m_roiTable->setMaximumHeight(180);
    sideLay->addWidget(m_roiTable);

    m_roiDeltaLabel = new QLabel(tr("Delta (B − A): —"), this);
    m_roiDeltaLabel->setObjectName("roiDeltaLabel");
    m_roiDeltaLabel->setWordWrap(true);
    m_roiDeltaLabel->setStyleSheet("color:#888;");
    sideLay->addWidget(m_roiDeltaLabel);
}

bool CompareWorkspace::linkedROIAvailable() const
{
    const int n = m_engine.imageCount();
    if (n < 2)
        return false;
    QSize common;
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *frame = m_engine.imageAt(i);
        if (!frame)
            return false;
        const QSize dims(frame->metadata().width > 0 ? frame->metadata().width : frame->width(),
                         frame->metadata().height > 0 ? frame->metadata().height : frame->height());
        if (dims.width() <= 0 || dims.height() <= 0)
            return false;
        if (i == 0)
            common = dims;
        else if (dims != common)
            return false;
    }
    return common.width() > 0 && common.height() > 0;
}

CompareWorkspace::ROIStatsBatchResult CompareWorkspace::computeROIStatsBatch(
    const std::vector<ROIInput> &inputs, const mviewer::domain::Selection &roi, bool linked,
    uint64_t generation, const TaskScheduler::TaskContext &context)
{
    ROIStatsBatchResult result;
    result.generation = generation;
    result.paneCount = static_cast<int>(inputs.size());
    result.linked = linked;
    result.roi = roi;
    result.stats.resize(inputs.size());
    if (!linked || roi.isEmpty())
        return result;
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        if (context.isCancelled())
            return {};
        result.stats[i] = computeSourceROI(inputs[i], roi);
    }
    return result;
}

mviewer::core::ROIChannelStats
CompareWorkspace::computeSourceROI(const ROIInput &input, const mviewer::domain::Selection &roi)
{
    if (!input.pixels.isNull())
        return mviewer::core::computeROIChannelStats(input.pixels, roi);
    if (input.path.empty() || roi.isEmpty())
        return {};

    // Metadata-only panes are the M47 source-backed path. Decode only the
    // selected source region, never the bounded display LOD. FullDecodeCrop
    // is intentionally rejected here: it cannot make an honest bounded-memory
    // promise for a decoder without region capability.
    const auto source = mviewer::core::SourceImage::open(input.path);
    if (!source)
        return {};
    const QSize displaySize(source->metadata().width, source->metadata().height);
    const long long roiRight = static_cast<long long>(roi.x) + roi.width;
    const long long roiBottom = static_cast<long long>(roi.y) + roi.height;
    if (displaySize.width() <= 0 || displaySize.height() <= 0 || roi.x < 0 || roi.y < 0 ||
        roiRight > displaySize.width() || roiBottom > displaySize.height())
        return {};
    const mviewer::core::SourceRect displayed{roi.x, roi.y, roi.width, roi.height};
    const mviewer::core::SourceRect raw = mviewer::core::orientedRectToRaw(
        displayed, source->rawWidth(), source->rawHeight(), source->orientation());
    const auto decoded = source->decodeRegion(raw, std::max(1, raw.w), std::max(1, raw.h));
    if (!decoded.ok || decoded.pixels.isNull() ||
        decoded.decodePath == mviewer::core::SourceDecodePath::FullDecodeCrop)
        return {};
    const mviewer::domain::Selection decodedRegion{0, 0, decoded.pixels.width,
                                                   decoded.pixels.height};
    return mviewer::core::computeROIChannelStats(decoded.pixels, decodedRegion);
}

TaskScheduler::TaskHandle
CompareWorkspace::startROIStatsBatch(const std::vector<ROIInput> &inputs,
                                     const mviewer::domain::Selection &roi, bool linked,
                                     uint64_t generation, const QPointer<CompareWorkspace> &guard)
{
    return TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [inputs, roi, linked, generation, guard](const TaskScheduler::TaskContext &context)
        {
            if (context.isCancelled())
                return;
            const ROIStatsBatchResult result =
                CompareWorkspace::computeROIStatsBatch(inputs, roi, linked, generation, context);
            if (context.isCancelled() || !qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, result]()
                {
                    if (CompareWorkspace *workspace = guard.data())
                        workspace->applyROIStatsBatchResult(result);
                },
                Qt::QueuedConnection);
        });
}

void CompareWorkspace::scheduleROIMeasurement()
{
    if (m_roiTask)
        TaskScheduler::cancel(m_roiTask);
    m_roiTask.reset();
    ++m_roiGen;

    if (!m_roiLinked || m_lastSelection.isEmpty() || !linkedROIAvailable())
    {
        clearROIStatsDisplay();
        updateROIAvailabilityStatus();
        return;
    }

    std::vector<ROIInput> inputs;
    const int paneCount = m_engine.imageCount();
    inputs.reserve(static_cast<size_t>(paneCount));
    for (int i = 0; i < paneCount; ++i)
    {
        ROIInput input;
        const ImageFrame *frame = m_engine.imageAt(i);
        if (frame)
        {
            input.pixels = frame->pixels();
            input.metadata = frame->metadata();
            input.path = frame->metadata().filePath;
        }
        inputs.push_back(std::move(input));
    }
    const uint64_t generation = m_roiGen;
    const QPointer<CompareWorkspace> guard(this);
    m_roiTask = startROIStatsBatch(inputs, m_lastSelection, true, generation, guard);
}

void CompareWorkspace::applyROIStatsBatchResult(const ROIStatsBatchResult &result)
{
    if (result.generation != m_roiGen || result.paneCount != m_engine.imageCount() ||
        result.roi.x != m_lastSelection.x || result.roi.y != m_lastSelection.y ||
        result.roi.width != m_lastSelection.width || result.roi.height != m_lastSelection.height ||
        !m_roiLinked || !linkedROIAvailable())
        return;
    m_roiTask.reset();
    if (!m_roiTable)
        return;
    m_roiTable->setRowCount(static_cast<int>(result.stats.size()));
    for (int i = 0; i < static_cast<int>(result.stats.size()); ++i)
    {
        const ImageFrame *frame = m_engine.imageAt(i);
        const QString name = frame ? QString::fromStdString(frame->metadata().fileName)
                                   : QStringLiteral("#%1").arg(i + 1);
        const auto &stats = result.stats[static_cast<size_t>(i)];
        const QStringList cells = {name,
                                   stats.valid ? meanText(stats.rMean) : QStringLiteral("—"),
                                   stats.valid ? meanText(stats.gMean) : QStringLiteral("—"),
                                   stats.valid ? meanText(stats.bMean) : QStringLiteral("—"),
                                   stats.valid ? ratioText(stats, true) : QStringLiteral("—"),
                                   stats.valid ? ratioText(stats, false) : QStringLiteral("—"),
                                   stats.valid ? QString::number(stats.pixelCount)
                                               : QStringLiteral("—")};
        for (int col = 0; col < cells.size(); ++col)
        {
            QTableWidgetItem *item = m_roiTable->item(i, col);
            if (!item)
            {
                item = new QTableWidgetItem;
                m_roiTable->setItem(i, col, item);
            }
            item->setText(cells[col]);
        }
    }

    if (m_roiDeltaLabel)
    {
        if (result.stats.size() == 2 && result.stats[0].valid && result.stats[1].valid)
        {
            const auto &a = result.stats[0];
            const auto &b = result.stats[1];
            const QString dr = QString::number(b.rMean - a.rMean, 'f', 2);
            const QString dg = QString::number(b.gMean - a.gMean, 'f', 2);
            const QString db = QString::number(b.bMean - a.bMean, 'f', 2);
            const QString drg = (a.ratiosValid && b.ratiosValid)
                                    ? QString::number(b.rOverG - a.rOverG, 'f', 4)
                                    : QStringLiteral("—");
            const QString dbg = (a.ratiosValid && b.ratiosValid)
                                    ? QString::number(b.bOverG - a.bOverG, 'f', 4)
                                    : QStringLiteral("—");
            m_roiDeltaLabel->setText(tr("Delta (B − A): ΔR %1  ΔG %2  ΔB %3  ΔR/G %4  ΔB/G %5")
                                         .arg(dr, dg, db, drg, dbg));
        }
        else
            m_roiDeltaLabel->setText(tr("Delta (B − A): —"));
    }
}

void CompareWorkspace::clearROIStatsDisplay()
{
    if (m_roiTable)
        m_roiTable->setRowCount(0);
    if (m_roiDeltaLabel)
        m_roiDeltaLabel->setText(tr("Delta (B − A): —"));
}

void CompareWorkspace::updateROIAvailabilityStatus()
{
    if (!m_roiStatusLabel)
        return;
    QString text;
    if (m_engine.imageCount() < 2)
        text = tr("Linked ROI unavailable — at least two images required");
    else if (!linkedROIAvailable())
        text = tr("Linked ROI unavailable — image dimensions differ");
    else if (m_lastSelection.isEmpty())
        text = tr("Linked ROI ready — source coordinates");
    else
        text = tr("Linked ROI active — source coordinates");
    m_roiStatusLabel->setText(text);
    if (m_clearRoiBtn)
        m_clearRoiBtn->setEnabled(!m_lastSelection.isEmpty());
}
