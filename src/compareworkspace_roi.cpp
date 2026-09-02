#include "compareworkspace_p.h"

#include "core/image/ExifOrientation.h"
#include "core/image/SourceImage.h"
#include "widgets/roioverlay.h"

#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QMetaObject>
#include <QTableWidget>

#include <algorithm>

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

QString measurementStateText(mviewer::ui::ROIMeasurementState state)
{
    using State = mviewer::ui::ROIMeasurementState;
    switch (state)
    {
    case State::Idle:
        return QStringLiteral("Idle");
    case State::Measuring:
        return QStringLiteral("Measuring…");
    case State::Ready:
        return QStringLiteral("Ready");
    case State::Unsupported:
        return QStringLiteral("Unsupported");
    case State::Failed:
        return QStringLiteral("Failed");
    case State::Backpressured:
        return QStringLiteral("Backpressured");
    }
    return QStringLiteral("Idle");
}

QString paneStateText(const mviewer::ui::ROIPaneMeasurement &pane)
{
    using State = mviewer::ui::ROIPaneState;
    switch (pane.state)
    {
    case State::Ready:
        return QStringLiteral("Ready");
    case State::Unsupported:
        return QStringLiteral("Unsupported: %1").arg(QString::fromStdString(pane.reason));
    case State::Failed:
        return QStringLiteral("Failed: %1").arg(QString::fromStdString(pane.reason));
    case State::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Failed");
}

QString paneName(const mviewer::domain::ImageMetadata &metadata, int index)
{
    const QString name = metadata.fileName.empty() ? QStringLiteral("#%1").arg(index + 1)
                                                   : QString::fromStdString(metadata.fileName);
    return QStringLiteral("%1 — %2").arg(QChar('A' + index), name);
}
} // namespace

void CompareWorkspace::buildROIMeasurementPanel(QVBoxLayout *sideLay)
{
    auto *roiHeader = new QHBoxLayout();
    auto *roiTitle = new QLabel(tr("ROI Measurement — Source RGB"), this);
    roiTitle->setObjectName("roiMeasurementTitle");
    roiHeader->addWidget(roiTitle);
    roiHeader->addStretch(1);
    m_copyRoiBtn = new QPushButton(tr("Copy"), this);
    m_copyRoiBtn->setObjectName("copyRoiMeasurementsButton");
    m_copyRoiBtn->setToolTip(tr("Copy ROI Measurements as TSV"));
    m_copyRoiBtn->setEnabled(false);
    connect(m_copyRoiBtn, &QPushButton::clicked, this, &CompareWorkspace::copyROIMeasurements);
    roiHeader->addWidget(m_copyRoiBtn);
    m_clearRoiBtn = new QPushButton(tr("Clear ROI"), this);
    m_clearRoiBtn->setObjectName("clearRoiButton");
    m_clearRoiBtn->setEnabled(false);
    connect(m_clearRoiBtn, &QPushButton::clicked, this, &CompareWorkspace::clearROI);
    roiHeader->addWidget(m_clearRoiBtn);
    sideLay->addLayout(roiHeader);

    m_roiStatusLabel = new QLabel(this);
    m_roiStatusLabel->setObjectName("roiStatusLabel");
    m_roiStatusLabel->setWordWrap(true);
    m_roiStatusLabel->setStyleSheet("color:#aaa;");
    sideLay->addWidget(m_roiStatusLabel);
    m_roiGeometryLabel = new QLabel(tr("ROI: —"), this);
    m_roiGeometryLabel->setObjectName("roiGeometryLabel");
    sideLay->addWidget(m_roiGeometryLabel);

    m_roiTable = new QTableWidget(this);
    m_roiTable->setObjectName("roiMeasurementTable");
    m_roiTable->setColumnCount(8);
    m_roiTable->setHorizontalHeaderLabels(
        {tr("Image"), QStringLiteral("R Mean"), QStringLiteral("G Mean"), QStringLiteral("B Mean"),
         QStringLiteral("R/G"), QStringLiteral("B/G"), tr("Pixels"), tr("Status")});
    m_roiTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_roiTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_roiTable->setTextElideMode(Qt::ElideMiddle);
    m_roiTable->verticalHeader()->setVisible(false);
    m_roiTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 7; ++column)
        m_roiTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_roiTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    m_roiTable->setMinimumHeight(90);
    m_roiTable->setMaximumHeight(200);
    sideLay->addWidget(m_roiTable);

    m_roiDeltaLabel = new QLabel(tr("Delta (B − A): —"), this);
    m_roiDeltaLabel->setObjectName("roiDeltaLabel");
    m_roiDeltaLabel->setWordWrap(true);
    m_roiDeltaLabel->setStyleSheet("color:#aaa;");
    sideLay->addWidget(m_roiDeltaLabel);

    m_roiHud = new QPushButton(this);
    m_roiHud->setObjectName("roiMeasurementHud");
    m_roiHud->setCursor(Qt::PointingHandCursor);
    m_roiHud->setStyleSheet(
        "QPushButton{background:rgba(20,20,20,225);color:#eee;border:1px solid #FFD233;"
        "border-radius:4px;padding:7px;text-align:left;}"
        "QPushButton:hover{background:rgba(35,35,35,240);}");
    m_roiHud->setVisible(false);
    connect(m_roiHud, &QPushButton::clicked, this,
            [this]()
            {
                if (m_sideChk)
                    m_sideChk->setChecked(true);
            });
}

bool CompareWorkspace::linkedROIAvailable() const
{
    const int count = m_engine.imageCount();
    if (count < 2)
        return false;
    QSize common;
    for (int i = 0; i < count; ++i)
    {
        const ImageFrame *frame = m_engine.imageAt(i);
        if (!frame)
            return false;
        const QSize dimensions(
            frame->metadata().width > 0 ? frame->metadata().width : frame->width(),
            frame->metadata().height > 0 ? frame->metadata().height : frame->height());
        if (!dimensions.isValid() || (i > 0 && dimensions != common))
            return false;
        common = dimensions;
    }
    return common.isValid();
}

mviewer::ui::ROIPaneMeasurement
CompareWorkspace::computeSourceROI(const ROIInput &input, const mviewer::domain::Selection &roi,
                                   const TaskScheduler::TaskContext &context)
{
    mviewer::ui::ROIPaneMeasurement result;
    const auto cancelled = [&context]() { return context.isCancelled(); };
    if (!input.pixels.isNull())
    {
        result.stats = mviewer::core::computeROIChannelStats(input.pixels, roi, cancelled);
        if (result.stats.cancelled)
        {
            result.state = mviewer::ui::ROIPaneState::Cancelled;
            return result;
        }
        result.state = result.stats.valid ? mviewer::ui::ROIPaneState::Ready
                                          : mviewer::ui::ROIPaneState::Failed;
        if (!result.stats.valid)
            result.reason = "ROI does not intersect source pixels";
        return result;
    }
    if (input.path.empty() || roi.isEmpty())
    {
        result.reason = "source pixels are unavailable";
        return result;
    }

    try
    {
        if (context.isCancelled())
        {
            result.state = mviewer::ui::ROIPaneState::Cancelled;
            return result;
        }
        const auto source = mviewer::core::SourceImage::open(input.path);
        if (!source)
        {
            result.reason = "source could not be opened";
            return result;
        }
        const QSize displaySize(source->metadata().width, source->metadata().height);
        const long long right = static_cast<long long>(roi.x) + roi.width;
        const long long bottom = static_cast<long long>(roi.y) + roi.height;
        if (!displaySize.isValid() || roi.x < 0 || roi.y < 0 || right > displaySize.width() ||
            bottom > displaySize.height())
        {
            result.reason = "ROI is outside the source";
            return result;
        }
        const mviewer::core::SourceRect displayed{roi.x, roi.y, roi.width, roi.height};
        const mviewer::core::SourceRect raw = mviewer::core::orientedRectToRaw(
            displayed, source->rawWidth(), source->rawHeight(), source->orientation());
        result.decodePath = source->regionDecodePath();
        if (result.decodePath == mviewer::core::SourceDecodePath::FullDecodeCrop)
        {
            result.state = mviewer::ui::ROIPaneState::Unsupported;
            result.reason = "bounded source-accurate region decode is unavailable";
            return result;
        }
        const auto decoded = source->decodeRegion(raw, std::max(1, raw.w), std::max(1, raw.h));
        result.decodePath = decoded.decodePath;
        if (context.isCancelled())
        {
            result.state = mviewer::ui::ROIPaneState::Cancelled;
            return result;
        }
        if (!decoded.ok || decoded.pixels.isNull())
        {
            result.reason = "source region decode failed";
            return result;
        }
        const mviewer::domain::Selection decodedRegion{0, 0, decoded.pixels.width,
                                                       decoded.pixels.height};
        result.stats =
            mviewer::core::computeROIChannelStats(decoded.pixels, decodedRegion, cancelled);
        if (result.stats.cancelled)
        {
            result.state = mviewer::ui::ROIPaneState::Cancelled;
            return result;
        }
        result.state = result.stats.valid ? mviewer::ui::ROIPaneState::Ready
                                          : mviewer::ui::ROIPaneState::Failed;
        if (!result.stats.valid)
            result.reason = "decoded region contains no source pixels";
    }
    catch (const std::exception &error)
    {
        result.reason = error.what();
    }
    catch (...)
    {
        result.reason = "unknown source measurement failure";
    }
    return result;
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
    result.panes.reserve(inputs.size());
    if (!linked || roi.isEmpty())
        return result;
    for (const ROIInput &input : inputs)
    {
        if (context.isCancelled())
            return {};
        result.panes.push_back(computeSourceROI(input, roi, context));
    }
    return result;
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
    m_roiResult.reset();

    if (!m_roiLinked || m_lastSelection.isEmpty() || !linkedROIAvailable())
    {
        clearROIStatsDisplay();
        setROIMeasurementState(linkedROIAvailable()
                                   ? mviewer::ui::ROIMeasurementState::Idle
                                   : mviewer::ui::ROIMeasurementState::Unsupported);
        return;
    }

    std::vector<ROIInput> inputs;
    const int paneCount = m_engine.imageCount();
    inputs.reserve(static_cast<size_t>(paneCount));
    for (int i = 0; i < paneCount; ++i)
    {
        ROIInput input;
        if (const ImageFrame *frame = m_engine.imageAt(i))
        {
            input.pixels = frame->pixels();
            input.metadata = frame->metadata();
            input.path = frame->metadata().filePath;
        }
        inputs.push_back(std::move(input));
    }
    setROIMeasurementState(mviewer::ui::ROIMeasurementState::Measuring,
                           tr("Source RGB · 8-bit analysis"));
    m_roiTask = startROIStatsBatch(inputs, m_lastSelection, true, m_roiGen, QPointer(this));
    if (!m_roiTask)
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Backpressured,
                               tr("Analysis queue busy — adjust or release ROI to retry"));
}

void CompareWorkspace::applyROIStatsBatchResult(const ROIStatsBatchResult &result)
{
    if (result.generation != m_roiGen || result.paneCount != m_engine.imageCount() ||
        result.roi.x != m_lastSelection.x || result.roi.y != m_lastSelection.y ||
        result.roi.width != m_lastSelection.width || result.roi.height != m_lastSelection.height ||
        !m_roiLinked || !linkedROIAvailable())
        return;
    m_roiTask.reset();
    m_roiResult = result;
    if (!m_roiTable)
        return;
    m_roiTable->setRowCount(static_cast<int>(result.panes.size()));
    bool unsupported = false;
    bool failed = false;
    for (int row = 0; row < static_cast<int>(result.panes.size()); ++row)
    {
        const auto &pane = result.panes[static_cast<size_t>(row)];
        const ImageFrame *frame = m_engine.imageAt(row);
        const mviewer::domain::ImageMetadata metadata =
            frame ? frame->metadata() : mviewer::domain::ImageMetadata{};
        const QStringList cells = {
            paneName(metadata, row),
            pane.stats.valid ? meanText(pane.stats.rMean) : QStringLiteral("—"),
            pane.stats.valid ? meanText(pane.stats.gMean) : QStringLiteral("—"),
            pane.stats.valid ? meanText(pane.stats.bMean) : QStringLiteral("—"),
            pane.stats.valid ? ratioText(pane.stats, true) : QStringLiteral("—"),
            pane.stats.valid ? ratioText(pane.stats, false) : QStringLiteral("—"),
            pane.stats.valid ? QString::number(pane.stats.pixelCount) : QStringLiteral("—"),
            paneStateText(pane)};
        for (int column = 0; column < cells.size(); ++column)
        {
            auto *item = new QTableWidgetItem(cells[column]);
            if (column > 0 && column < 7)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (column == 0)
                item->setToolTip(QString::fromStdString(metadata.filePath));
            if (column == 7)
                item->setToolTip(cells[column]);
            m_roiTable->setItem(row, column, item);
        }
        unsupported = unsupported || pane.state == mviewer::ui::ROIPaneState::Unsupported;
        failed = failed || pane.state == mviewer::ui::ROIPaneState::Failed;
    }

    if (m_roiDeltaLabel && result.panes.size() == 2 && result.panes[0].stats.valid &&
        result.panes[1].stats.valid)
    {
        const auto &a = result.panes[0].stats;
        const auto &b = result.panes[1].stats;
        const QString redGreen = (a.ratiosValid && b.ratiosValid)
                                     ? QString::number(b.rOverG - a.rOverG, 'f', 4)
                                     : QStringLiteral("—");
        const QString blueGreen = (a.ratiosValid && b.ratiosValid)
                                      ? QString::number(b.bOverG - a.bOverG, 'f', 4)
                                      : QStringLiteral("—");
        m_roiDeltaLabel->setText(tr("Delta (B − A): ΔR %1  ΔG %2  ΔB %3  ΔR/G %4  ΔB/G %5")
                                     .arg(QString::number(b.rMean - a.rMean, 'f', 2),
                                          QString::number(b.gMean - a.gMean, 'f', 2),
                                          QString::number(b.bMean - a.bMean, 'f', 2), redGreen,
                                          blueGreen));
    }
    else if (m_roiDeltaLabel)
        m_roiDeltaLabel->setText(tr("Delta (B − A): —"));

    if (failed)
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Failed,
                               tr("One or more source measurements failed"));
    else if (unsupported)
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Unsupported,
                               tr("A bounded source-accurate region is unavailable"));
    else
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Ready,
                               tr("Source RGB · full-resolution coordinates · 8-bit analysis"));
}

void CompareWorkspace::clearROIStatsDisplay()
{
    m_roiResult.reset();
    if (m_roiTable)
        m_roiTable->setRowCount(0);
    if (m_roiDeltaLabel)
        m_roiDeltaLabel->setText(tr("Delta (B − A): —"));
    if (m_copyRoiBtn)
        m_copyRoiBtn->setEnabled(false);
}

void CompareWorkspace::setROIMeasurementState(mviewer::ui::ROIMeasurementState state,
                                              const QString &detail)
{
    m_roiState = state;
    m_roiStateDetail = detail;
    updateROISurfaces();
}

void CompareWorkspace::updateROIAvailabilityStatus()
{
    QString detail = m_roiStateDetail;
    if (m_engine.imageCount() < 2)
        detail = tr("Linked ROI unavailable — at least two images required");
    else if (!linkedROIAvailable())
        detail = tr("Linked ROI unavailable — image dimensions differ");
    else if (m_lastSelection.isEmpty())
        detail = tr("Linked ROI ready — source coordinates");
    else if (detail.isEmpty())
        detail = tr("Source RGB · 8-bit analysis");
    if (m_roiStatusLabel)
        m_roiStatusLabel->setText(
            QStringLiteral("%1 — %2").arg(measurementStateText(m_roiState), detail));
    if (m_clearRoiBtn)
        m_clearRoiBtn->setEnabled(!m_lastSelection.isEmpty());
}

void CompareWorkspace::updateROISurfaces()
{
    updateROIAvailabilityStatus();
    if (m_copyRoiBtn)
        m_copyRoiBtn->setEnabled(m_roiResult.has_value() && m_roiTable &&
                                 m_roiTable->rowCount() > 0);
    if (!m_roiHud)
        return;
    if (m_lastSelection.isEmpty())
    {
        m_roiHud->setVisible(false);
        return;
    }

    QStringList lines;
    lines << QStringLiteral("ROI Measurement — Source RGB · 8-bit")
          << QStringLiteral("%1   X %2  Y %3  W %4  H %5  Pixels %6")
                 .arg(measurementStateText(m_roiState))
                 .arg(m_lastSelection.x)
                 .arg(m_lastSelection.y)
                 .arg(m_lastSelection.width)
                 .arg(m_lastSelection.height)
                 .arg(static_cast<qint64>(m_lastSelection.width) * m_lastSelection.height);
    if (m_roiResult)
    {
        const int count = std::min(2, static_cast<int>(m_roiResult->panes.size()));
        for (int index = 0; index < count; ++index)
        {
            const auto &pane = m_roiResult->panes[static_cast<size_t>(index)];
            if (pane.stats.valid)
                lines << QStringLiteral("%1  R %2  G %3  B %4  R/G %5  B/G %6")
                             .arg(QChar('A' + index), meanText(pane.stats.rMean),
                                  meanText(pane.stats.gMean), meanText(pane.stats.bMean),
                                  ratioText(pane.stats, true), ratioText(pane.stats, false));
            else
                lines << QStringLiteral("%1  %2").arg(QChar('A' + index), paneStateText(pane));
        }
        if (m_roiResult->panes.size() == 2 && m_roiResult->panes[0].stats.valid &&
            m_roiResult->panes[1].stats.valid)
        {
            const auto &a = m_roiResult->panes[0].stats;
            const auto &b = m_roiResult->panes[1].stats;
            lines << QStringLiteral("Δ  R %1  G %2  B %3  R/G %4  B/G %5")
                         .arg(QString::number(b.rMean - a.rMean, 'f', 2),
                              QString::number(b.gMean - a.gMean, 'f', 2),
                              QString::number(b.bMean - a.bMean, 'f', 2),
                              a.ratiosValid && b.ratiosValid
                                  ? QString::number(b.rOverG - a.rOverG, 'f', 4)
                                  : QStringLiteral("—"),
                              a.ratiosValid && b.ratiosValid
                                  ? QString::number(b.bOverG - a.bOverG, 'f', 4)
                                  : QStringLiteral("—"));
        }
    }
    else if (!m_roiStateDetail.isEmpty())
        lines << m_roiStateDetail;
    m_roiHud->setText(lines.join('\n'));
    m_roiHud->setToolTip(tr("Click to open the complete Analysis panel"));
    const bool sideVisible = m_sidePanel && m_sidePanel->isVisible();
    m_roiHud->setVisible(!sideVisible);
    if (!sideVisible)
    {
        positionROIHud();
        m_roiHud->raise();
    }
}

void CompareWorkspace::positionROIHud()
{
    if (!m_roiHud)
        return;
    QWidget *target =
        m_compareCanvas && m_compareCanvas->isVisible() ? m_compareCanvas : m_compareGridPage;
    if (!target || target->width() <= 0 || target->height() <= 0)
        return;
    const QPoint topLeft = target->mapTo(this, QPoint(0, 0));
    const QRect area(topLeft, target->size());
    const int width = std::min(370, std::max(180, area.width() - 16));
    m_roiHud->setFixedWidth(width);
    m_roiHud->adjustSize();
    const int height = std::min(m_roiHud->height(), std::max(40, area.height() - 16));
    QRect hudRect(area.right() - width - 8, area.bottom() - height - 8, width, height);
    if (target == m_compareCanvas && !m_lastSelection.isEmpty() && !m_cellViews.isEmpty() &&
        m_cellViews[0])
    {
        const QRectF destination = cellFullDestRect(0, canvasPaneGeometry(0));
        QRectF roiRect = mviewer::ui::roiPresentationRect(
            m_lastSelection, m_cellViews[0]->sourceSize(), destination);
        roiRect.translate(topLeft);
        const QRectF roiCenter(roiRect.center() - QPointF(24, 24), QSizeF(48, 48));
        if (roiCenter.intersects(hudRect))
            hudRect.moveTop(area.top() + 8);
    }
    m_roiHud->setGeometry(hudRect);
}

void CompareWorkspace::copyROIMeasurements()
{
    if (!m_roiTable || m_roiTable->rowCount() == 0)
        return;
    QStringList lines;
    QStringList headers;
    for (int column = 0; column < m_roiTable->columnCount(); ++column)
        headers << m_roiTable->horizontalHeaderItem(column)->text();
    lines << headers.join('\t');
    for (int row = 0; row < m_roiTable->rowCount(); ++row)
    {
        QStringList cells;
        for (int column = 0; column < m_roiTable->columnCount(); ++column)
            cells << (m_roiTable->item(row, column) ? m_roiTable->item(row, column)->text()
                                                    : QString());
        lines << cells.join('\t');
    }
    lines << QStringLiteral("ROI\tX=%1\tY=%2\tW=%3\tH=%4\tPixels=%5")
                 .arg(m_lastSelection.x)
                 .arg(m_lastSelection.y)
                 .arg(m_lastSelection.width)
                 .arg(m_lastSelection.height)
                 .arg(static_cast<qint64>(m_lastSelection.width) * m_lastSelection.height);
    if (m_roiDeltaLabel)
        lines << m_roiDeltaLabel->text();
    QApplication::clipboard()->setText(lines.join('\n'));
}
