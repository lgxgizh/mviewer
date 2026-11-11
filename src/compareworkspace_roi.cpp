#include "compareworkspace_p.h"

#include "Theme.h"
#include "compare_roi_stats_presenter.h"
#include "core/analysis/PixelInspector.h"
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
QString measurementStateText(mviewer::ui::ROIMeasurementState state)
{
    using State = mviewer::ui::ROIMeasurementState;
    switch (state)
    {
    case State::Idle:
        return CompareWorkspace::tr("Idle");
    case State::Measuring:
        return CompareWorkspace::tr("Measuring…");
    case State::Ready:
        return CompareWorkspace::tr("Ready");
    case State::Unsupported:
        return CompareWorkspace::tr("Unsupported");
    case State::Failed:
        return CompareWorkspace::tr("Failed");
    case State::Backpressured:
        return CompareWorkspace::tr("Busy, try later");
    }
    return CompareWorkspace::tr("Idle");
}

} // namespace

void CompareWorkspace::buildROIMeasurementPanel(QVBoxLayout *sideLay)
{
    auto *roiHeader = new QHBoxLayout();
    auto *roiTitle = new QLabel(tr("ROI Measurement — Source RGB / HSV"), this);
    roiTitle->setObjectName("roiMeasurementTitle");
    roiHeader->addWidget(roiTitle);
    roiHeader->addStretch(1);
    m_copyRoiBtn = new QPushButton(tr("Copy"), this);
    m_copyRoiBtn->setObjectName("copyRoiMeasurementsButton");
    m_copyRoiBtn->setToolTip(tr("Copy ROI measurement results as TSV"));
    m_copyRoiBtn->setEnabled(false);
    connect(m_copyRoiBtn, &QPushButton::clicked, this, &CompareWorkspace::copyROIMeasurements);
    roiHeader->addWidget(m_copyRoiBtn);
    m_clearRoiBtn = new QPushButton(tr("Clear ROI"), this);
    m_clearRoiBtn->setObjectName("clearRoiButton");
    m_clearRoiBtn->setToolTip(tr("Clear the current ROI"));
    m_clearRoiBtn->setEnabled(false);
    connect(m_clearRoiBtn, &QPushButton::clicked, this, &CompareWorkspace::clearROI);
    roiHeader->addWidget(m_clearRoiBtn);
    sideLay->addLayout(roiHeader);

    m_roiStatusLabel = new QLabel(this);
    m_roiStatusLabel->setObjectName("roiStatusLabel");
    m_roiStatusLabel->setWordWrap(true);
    m_roiStatusLabel->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
    sideLay->addWidget(m_roiStatusLabel);
    m_roiGeometryLabel = new QLabel(tr("ROI: —"), this);
    m_roiGeometryLabel->setObjectName("roiGeometryLabel");
    sideLay->addWidget(m_roiGeometryLabel);

    m_roiTable = new QTableWidget(this);
    m_roiTable->setObjectName("roiMeasurementTable");
    m_roiTable->setColumnCount(11);
    m_roiTable->setHorizontalHeaderLabels(
        {tr("Image"), QStringLiteral("H Mean"), QStringLiteral("S Mean"), QStringLiteral("V Mean"),
         QStringLiteral("R Mean"), QStringLiteral("G Mean"), QStringLiteral("B Mean"),
         QStringLiteral("R/G"), QStringLiteral("B/G"), tr("Pixels"), tr("Status")});
    m_roiTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_roiTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_roiTable->setTextElideMode(Qt::ElideMiddle);
    m_roiTable->verticalHeader()->setVisible(false);
    m_roiTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 10; ++column)
        m_roiTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_roiTable->horizontalHeader()->setSectionResizeMode(10, QHeaderView::Stretch);
    m_roiTable->setMinimumHeight(90);
    m_roiTable->setMaximumHeight(200);
    sideLay->addWidget(m_roiTable);

    m_roiDeltaLabel = new QLabel(tr("Delta (B − A): —"), this);
    m_roiDeltaLabel->setObjectName("roiDeltaLabel");
    m_roiDeltaLabel->setWordWrap(true);
    m_roiDeltaLabel->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
    sideLay->addWidget(m_roiDeltaLabel);

    m_roiHud = new QPushButton(this);
    m_roiHud->setObjectName("roiMeasurementHud");
    m_roiHud->setCursor(Qt::PointingHandCursor);
    m_roiHud->setStyleSheet(
        "QPushButton{background:rgba(0,0,0,140);color:#ffffff;border:1px solid #FFD233;"
        "border-radius:6px;padding:8px;text-align:left;font-size:12px;font-weight:600;}"
        "QPushButton:hover{background:rgba(0,0,0,170);}");
    m_roiHud->setVisible(false);
    connect(m_roiHud, &QPushButton::clicked, this,
            [this]()
            {
                if (m_sideChk)
                    m_sideChk->setChecked(true);
            });
}

QSize CompareWorkspace::paneEffectiveSize(int pane) const
{
    if (pane < 0 || pane >= m_engine.imageCount())
        return {};
    const ImageFrame *frame = m_engine.imageAt(pane);
    if (!frame)
        return {};
    const int rawW = frame->metadata().width > 0 ? frame->metadata().width : frame->width();
    const int rawH = frame->metadata().height > 0 ? frame->metadata().height : frame->height();
    if (rawW <= 0 || rawH <= 0)
        return {};
    const CellAdjust adjust = (pane >= 0 && pane < static_cast<int>(m_cellAdjusts.size()))
                                  ? m_cellAdjusts[static_cast<size_t>(pane)]
                                  : CellAdjust{};
    const auto crop = mviewer::core::analysisCropBounds(rawW, rawH, analysisAdjustment(adjust));
    const int rot = std::abs(adjust.rotation % 360);
    const int effW = (rot == 90 || rot == 270) ? crop.height : crop.width;
    const int effH = (rot == 90 || rot == 270) ? crop.width : crop.height;
    return QSize(effW, effH);
}

bool CompareWorkspace::linkedROIAvailable() const
{
    const int count = m_engine.imageCount();
    if (count < 2)
        return false;
    const QSize common = paneEffectiveSize(0);
    if (!common.isValid())
        return false;
    for (int i = 1; i < count; ++i)
    {
        if (paneEffectiveSize(i) != common)
            return false;
    }
    return true;
}

mviewer::ui::ROIPaneMeasurement
CompareWorkspace::computeSourceROI(const ROIInput &input, const mviewer::domain::Selection &roi,
                                   const TaskScheduler::TaskContext &context)
{
    return mviewer::ui::CompareRoiController::computeSourceROI(input, roi, context);
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
    for (const ROIInput &input : inputs)
    {
        if (context.isCancelled())
            return {};
        const mviewer::domain::Selection targetRoi = linked ? roi : input.roi;
        if (targetRoi.isEmpty())
        {
            mviewer::ui::ROIPaneMeasurement unmeasured;
            unmeasured.state = mviewer::ui::ROIPaneState::Unsupported;
            unmeasured.reason = QT_TRANSLATE_NOOP("CompareRoi", "This pane has no ROI");
            result.panes.push_back(std::move(unmeasured));
            continue;
        }
        result.panes.push_back(computeSourceROI(input, targetRoi, context));
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

    const bool linked = linkedROIAvailable() && m_roiLinked;
    const int paneCount = m_engine.imageCount();
    std::vector<ROIInput> inputs;
    inputs.reserve(static_cast<size_t>(paneCount));
    bool hasAnyRoi = false;
    for (int i = 0; i < paneCount; ++i)
    {
        ROIInput input;
        if (const ImageFrame *frame = m_engine.imageAt(i))
        {
            input.pixels = frame->pixels();
            input.metadata = frame->metadata();
            input.path = frame->metadata().filePath;
        }
        if (i < static_cast<int>(m_cellAdjusts.size()))
            input.adjustment = analysisAdjustment(m_cellAdjusts[static_cast<size_t>(i)]);
        if (linked)
            input.roi = m_lastSelection;
        else if (i < static_cast<int>(m_cellViews.size()) && m_cellViews[i])
            input.roi = m_cellViews[i]->selection();
        if (!input.roi.isEmpty())
            hasAnyRoi = true;
        inputs.push_back(std::move(input));
    }

    if (!hasAnyRoi)
    {
        clearROIStatsDisplay();
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Idle);
        return;
    }

    setROIMeasurementState(mviewer::ui::ROIMeasurementState::Measuring,
                           linked ? tr("Source RGB · 8-bit analysis")
                                  : tr("Independent selection · Source pixel RGB analysis"));
    m_roiTask = startROIStatsBatch(inputs, m_lastSelection, linked, m_roiGen, QPointer(this));
    if (!m_roiTask)
        setROIMeasurementState(
            mviewer::ui::ROIMeasurementState::Backpressured,
            tr("Analysis queue busy — retry after adjusting or releasing the ROI"));
}

void CompareWorkspace::applyROIStatsBatchResult(const ROIStatsBatchResult &result)
{
    if (result.generation != m_roiGen || result.paneCount != m_engine.imageCount())
        return;
    if (result.linked)
    {
        if (result.roi.x != m_lastSelection.x || result.roi.y != m_lastSelection.y ||
            result.roi.width != m_lastSelection.width ||
            result.roi.height != m_lastSelection.height || !m_roiLinked || !linkedROIAvailable())
            return;
    }
    m_roiTask.reset();
    m_roiResult = result;
    if (!m_roiTable)
        return;

    std::vector<mviewer::domain::ImageMetadata> paneMetadata;
    paneMetadata.reserve(result.panes.size());
    for (int row = 0; row < static_cast<int>(result.panes.size()); ++row)
    {
        const ImageFrame *frame = m_engine.imageAt(row);
        paneMetadata.push_back(frame ? frame->metadata() : mviewer::domain::ImageMetadata{});
    }
    const mviewer::ui::RoiStatsView view = mviewer::ui::presentRoiStats(result, paneMetadata);
    m_roiTable->setRowCount(static_cast<int>(view.rows.size()));
    for (int row = 0; row < static_cast<int>(view.rows.size()); ++row)
    {
        const auto &statsRow = view.rows[static_cast<size_t>(row)];
        for (int column = 0; column < statsRow.cells.size(); ++column)
        {
            auto *item = new QTableWidgetItem(statsRow.cells[column]);
            if (column > 0 && column < 10)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (column == 0)
                item->setToolTip(statsRow.nameToolTip);
            if (column == 10)
                item->setToolTip(statsRow.stateToolTip);
            m_roiTable->setItem(row, column, item);
        }
    }
    if (m_roiDeltaLabel)
        m_roiDeltaLabel->setText(view.deltaText);
    if (view.state == mviewer::ui::ROIMeasurementState::Idle)
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Idle);
    else
        setROIMeasurementState(view.state, view.stateDetail);

    updateROISurfaces();
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
    if (m_roiController)
        m_roiController->setMeasurementState(state);
    updateROISurfaces();
}

void CompareWorkspace::updateROIAvailabilityStatus()
{
    QString detail = m_roiStateDetail;
    const bool hasAnyRoi =
        m_roiLinked ? !m_lastSelection.isEmpty()
                    : std::any_of(m_cellViews.begin(), m_cellViews.end(),
                                  [](RawImageView *v) { return v && !v->selection().isEmpty(); });
    if (m_engine.imageCount() < 2)
        detail = tr("Cannot link ROI — at least two images are required");
    else if (!linkedROIAvailable())
        detail = tr("Cannot link ROI — Image dimensions do not match");
    else if (m_lastSelection.isEmpty())
        detail = tr("Linked ROI ready — source coordinates");
    else if (detail.isEmpty())
        detail = tr("Source RGB · 8-bit analysis");
    if (m_roiStatusLabel)
        m_roiStatusLabel->setText(
            QStringLiteral("%1 — %2").arg(measurementStateText(m_roiState), detail));
    if (m_clearRoiBtn)
        m_clearRoiBtn->setEnabled(hasAnyRoi || !m_lastSelection.isEmpty());
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
    // Linked sessions share one rectangle. Unlinked sizes keep a line per pane
    // so the paste names the box that pane's row was measured with.
    if (linkedROIAvailable())
    {
        lines << QStringLiteral("ROI\tX=%1\tY=%2\tW=%3\tH=%4\tPixels=%5")
                     .arg(m_lastSelection.x)
                     .arg(m_lastSelection.y)
                     .arg(m_lastSelection.width)
                     .arg(m_lastSelection.height)
                     .arg(static_cast<qint64>(m_lastSelection.width) * m_lastSelection.height);
    }
    else
    {
        for (int row = 0; row < m_roiTable->rowCount(); ++row)
        {
            mviewer::domain::Selection sel;
            if (row < m_cellViews.size() && m_cellViews[row])
                sel = m_cellViews[row]->selection();
            lines << QStringLiteral("ROI\t%1\tX=%2\tY=%3\tW=%4\tH=%5\tPixels=%6")
                         .arg(QChar('A' + row))
                         .arg(sel.x)
                         .arg(sel.y)
                         .arg(sel.width)
                         .arg(sel.height)
                         .arg(static_cast<qint64>(sel.width) * sel.height);
        }
    }
    if (m_roiDeltaLabel)
        lines << m_roiDeltaLabel->text();
    QApplication::clipboard()->setText(lines.join('\n'));
}

void CompareWorkspace::onCrosshairMoved(RawImageView *view, const QPointF &pos)
{
    if (!view)
        return;
    // Track the hovered cell so the focus-lock button knows which cell to pin.
    m_hoverIdx = view->cellIndex();

    if (!m_crosshairChk || !m_crosshairChk->isChecked())
        return;

    const bool valid = pos.x() >= 0.0 && pos.y() >= 0.0;
    const int n = m_engine.imageCount();
    bool asymmetricLocal = false;
    for (int i = 0; i < n; ++i)
    {
        if (std::abs(localZoomMultiplier(i) - 1.0) > 1e-9)
        {
            asymmetricLocal = true;
            break;
        }
    }
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        if (!valid)
        {
            m_cellViews[i]->clearCrosshair();
            continue;
        }
        if (asymmetricLocal)
        {
            // L11: map by viewport-normalized position so a Ctrl+wheel
            // asymmetric zoom keeps crosshairs on the visually aligned point.
            const QPointF widgetPt = view->sourcePointToWidget(pos);
            const double u =
                view->width() > 0 ? widgetPt.x() / static_cast<double>(view->width()) : 0.5;
            const double v =
                view->height() > 0 ? widgetPt.y() / static_cast<double>(view->height()) : 0.5;
            const QPoint mapped(static_cast<int>(std::lround(u * m_cellViews[i]->width())),
                                static_cast<int>(std::lround(v * m_cellViews[i]->height())));
            m_cellViews[i]->setCrosshair(m_cellViews[i]->widgetToImage(mapped));
        }
        else
        {
            m_cellViews[i]->setCrosshair(pos);
        }
    }
    // Sample every cell at the synced image-space point. M30: route through the
    // coalescer so this crosshair path cannot double-render the inspector that
    // the same hover's pixelInfo signal already requested.
    if (valid && m_sidePanel && m_sidePanel->isVisible())
        requestInspectorUpdate(qRound(pos.x()), qRound(pos.y()));
}
