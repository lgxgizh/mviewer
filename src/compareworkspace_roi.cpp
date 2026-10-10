#include "compareworkspace_p.h"

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

// Shortest signed hue step in degrees, so a shift across 0° stays near zero.
QString signedHueDelta(double later, double earlier)
{
    double delta = later - earlier;
    if (delta > 180.0)
        delta -= 360.0;
    else if (delta < -180.0)
        delta += 360.0;
    return QString::number(delta, 'f', 2);
}

QString roiDeltaMetrics(const mviewer::core::ROIChannelStats &base,
                        const mviewer::core::ROIChannelStats &other)
{
    const QString redGreen = (base.ratiosValid && other.ratiosValid)
                                 ? QString::number(other.rOverG - base.rOverG, 'f', 4)
                                 : QStringLiteral("—");
    const QString blueGreen = (base.ratiosValid && other.ratiosValid)
                                  ? QString::number(other.bOverG - base.bOverG, 'f', 4)
                                  : QStringLiteral("—");
    return CompareWorkspace::tr("ΔH %1  ΔS %2  ΔV %3  ΔR %4  ΔG %5  ΔB %6  ΔR/G %7  ΔB/G %8")
        .arg(signedHueDelta(other.hMean, base.hMean),
             QString::number(other.sMean - base.sMean, 'f', 2),
             QString::number(other.vMean - base.vMean, 'f', 2),
             QString::number(other.rMean - base.rMean, 'f', 2),
             QString::number(other.gMean - base.gMean, 'f', 2),
             QString::number(other.bMean - base.bMean, 'f', 2), redGreen, blueGreen);
}

QString measurementStateText(mviewer::ui::ROIMeasurementState state)
{
    using State = mviewer::ui::ROIMeasurementState;
    switch (state)
    {
    case State::Idle:
        return CompareWorkspace::tr("空闲");
    case State::Measuring:
        return CompareWorkspace::tr("测量中…");
    case State::Ready:
        return CompareWorkspace::tr("就绪");
    case State::Unsupported:
        return CompareWorkspace::tr("不支持");
    case State::Failed:
        return CompareWorkspace::tr("失败");
    case State::Backpressured:
        return CompareWorkspace::tr("繁忙稍后");
    }
    return CompareWorkspace::tr("空闲");
}

QString paneStateText(const mviewer::ui::ROIPaneMeasurement &pane)
{
    using State = mviewer::ui::ROIPaneState;
    switch (pane.state)
    {
    case State::Ready:
        return CompareWorkspace::tr("就绪");
    case State::Unsupported:
        return CompareWorkspace::tr("不支持: %1").arg(QString::fromStdString(pane.reason));
    case State::Failed:
        return CompareWorkspace::tr("失败: %1").arg(QString::fromStdString(pane.reason));
    case State::Cancelled:
        return CompareWorkspace::tr("已取消");
    }
    return CompareWorkspace::tr("失败");
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
    auto *roiTitle = new QLabel(tr("ROI 测量 — 源图 RGB / HSV"), this);
    roiTitle->setObjectName("roiMeasurementTitle");
    roiHeader->addWidget(roiTitle);
    roiHeader->addStretch(1);
    m_copyRoiBtn = new QPushButton(tr("复制"), this);
    m_copyRoiBtn->setObjectName("copyRoiMeasurementsButton");
    m_copyRoiBtn->setToolTip(tr("将 ROI 测量结果复制为 TSV"));
    m_copyRoiBtn->setEnabled(false);
    connect(m_copyRoiBtn, &QPushButton::clicked, this, &CompareWorkspace::copyROIMeasurements);
    roiHeader->addWidget(m_copyRoiBtn);
    m_clearRoiBtn = new QPushButton(tr("清除 ROI"), this);
    m_clearRoiBtn->setObjectName("clearRoiButton");
    m_clearRoiBtn->setToolTip(tr("清除当前 ROI"));
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
    m_roiTable->setColumnCount(11);
    m_roiTable->setHorizontalHeaderLabels(
        {tr("图像"), QStringLiteral("H Mean"), QStringLiteral("S Mean"), QStringLiteral("V Mean"),
         QStringLiteral("R Mean"), QStringLiteral("G Mean"), QStringLiteral("B Mean"),
         QStringLiteral("R/G"), QStringLiteral("B/G"), tr("像素"), tr("状态")});
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

    m_roiDeltaLabel = new QLabel(tr("差值 (B − A): —"), this);
    m_roiDeltaLabel->setObjectName("roiDeltaLabel");
    m_roiDeltaLabel->setWordWrap(true);
    m_roiDeltaLabel->setStyleSheet("color:#aaa;");
    sideLay->addWidget(m_roiDeltaLabel);

    m_roiHud = new QPushButton(this);
    m_roiHud->setObjectName("roiMeasurementHud");
    m_roiHud->setCursor(Qt::PointingHandCursor);
    m_roiHud->setStyleSheet(
        "QPushButton{background:rgba(20,20,20,235);color:#ffffff;border:1px solid #FFD233;"
        "border-radius:4px;padding:8px;text-align:left;font-size:12px;font-weight:600;}"
        "QPushButton:hover{background:rgba(35,35,35,245);}");
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
    mviewer::ui::ROIPaneMeasurement result;
    const auto cancelled = [&context]() { return context.isCancelled(); };

    const int srcW = input.metadata.width > 0 ? input.metadata.width : input.pixels.width;
    const int srcH = input.metadata.height > 0 ? input.metadata.height : input.pixels.height;
    const auto sourceRoi =
        mviewer::core::mapDisplaySelectionToSource(roi, input.adjustment, srcW, srcH);

    if (!input.pixels.isNull())
    {
        result.stats = mviewer::core::computeROIChannelStats(input.pixels, sourceRoi, cancelled);
        if (result.stats.cancelled)
        {
            result.state = mviewer::ui::ROIPaneState::Cancelled;
            return result;
        }
        result.state = result.stats.valid ? mviewer::ui::ROIPaneState::Ready
                                          : mviewer::ui::ROIPaneState::Failed;
        if (!result.stats.valid)
            result.reason = "ROI 与源图像素没有交集";
        return result;
    }
    if (input.path.empty() || sourceRoi.isEmpty())
    {
        result.reason = "源图像素不可用";
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
            result.reason = "无法打开源图";
            return result;
        }
        const QSize displaySize(source->metadata().width, source->metadata().height);
        const long long right = static_cast<long long>(sourceRoi.x) + sourceRoi.width;
        const long long bottom = static_cast<long long>(sourceRoi.y) + sourceRoi.height;
        if (!displaySize.isValid() || sourceRoi.x < 0 || sourceRoi.y < 0 ||
            right > displaySize.width() || bottom > displaySize.height())
        {
            result.reason = "ROI 超出源图范围";
            return result;
        }
        const mviewer::core::SourceRect displayed{sourceRoi.x, sourceRoi.y, sourceRoi.width,
                                                  sourceRoi.height};
        const mviewer::core::SourceRect raw = mviewer::core::orientedRectToRaw(
            displayed, source->rawWidth(), source->rawHeight(), source->orientation());
        result.decodePath = source->regionDecodePath();
        if (result.decodePath == mviewer::core::SourceDecodePath::FullDecodeCrop)
        {
            result.state = mviewer::ui::ROIPaneState::Unsupported;
            result.reason = "无法按源图精确解码该区域";
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
            result.reason = "源图区域解码失败";
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
            result.reason = "解码区域没有源图像素";
    }
    catch (const std::exception &error)
    {
        result.reason = error.what();
    }
    catch (...)
    {
        result.reason = "未知的源图测量失败";
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
    for (const ROIInput &input : inputs)
    {
        if (context.isCancelled())
            return {};
        const mviewer::domain::Selection targetRoi = linked ? roi : input.roi;
        if (targetRoi.isEmpty())
        {
            mviewer::ui::ROIPaneMeasurement unmeasured;
            unmeasured.state = mviewer::ui::ROIPaneState::Unsupported;
            unmeasured.reason = "此窗格没有 ROI";
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
                           linked ? tr("源图 RGB · 8 位分析") : tr("独立选区 · 源像素 RGB 分析"));
    m_roiTask = startROIStatsBatch(inputs, m_lastSelection, linked, m_roiGen, QPointer(this));
    if (!m_roiTask)
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Backpressured,
                               tr("分析队列繁忙 — 调整或松开 ROI 后重试"));
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
    m_roiTable->setRowCount(static_cast<int>(result.panes.size()));
    bool unsupported = false;
    bool failed = false;
    bool hasAnyValid = false;
    for (int row = 0; row < static_cast<int>(result.panes.size()); ++row)
    {
        const auto &pane = result.panes[static_cast<size_t>(row)];
        const ImageFrame *frame = m_engine.imageAt(row);
        const mviewer::domain::ImageMetadata metadata =
            frame ? frame->metadata() : mviewer::domain::ImageMetadata{};
        const QString stateText =
            (!result.linked && !pane.stats.valid) ? tr("未框选") : paneStateText(pane);
        const QStringList cells = {
            paneName(metadata, row),
            pane.stats.valid ? meanText(pane.stats.hMean) : QStringLiteral("—"),
            pane.stats.valid ? meanText(pane.stats.sMean) : QStringLiteral("—"),
            pane.stats.valid ? meanText(pane.stats.vMean) : QStringLiteral("—"),
            pane.stats.valid ? meanText(pane.stats.rMean) : QStringLiteral("—"),
            pane.stats.valid ? meanText(pane.stats.gMean) : QStringLiteral("—"),
            pane.stats.valid ? meanText(pane.stats.bMean) : QStringLiteral("—"),
            pane.stats.valid ? ratioText(pane.stats, true) : QStringLiteral("—"),
            pane.stats.valid ? ratioText(pane.stats, false) : QStringLiteral("—"),
            pane.stats.valid ? QString::number(pane.stats.pixelCount) : QStringLiteral("—"),
            stateText};
        for (int column = 0; column < cells.size(); ++column)
        {
            auto *item = new QTableWidgetItem(cells[column]);
            if (column > 0 && column < 10)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (column == 0)
                item->setToolTip(QString::fromStdString(metadata.filePath));
            if (column == 10)
                item->setToolTip(cells[column]);
            m_roiTable->setItem(row, column, item);
        }
        if (pane.stats.valid)
            hasAnyValid = true;
        if (result.linked)
        {
            unsupported = unsupported || pane.state == mviewer::ui::ROIPaneState::Unsupported;
            failed = failed || pane.state == mviewer::ui::ROIPaneState::Failed;
        }
        else
        {
            failed = failed || pane.state == mviewer::ui::ROIPaneState::Failed;
        }
    }

    if (m_roiDeltaLabel && result.panes.size() >= 2 && result.panes[0].stats.valid)
    {
        const auto &a = result.panes[0].stats;
        if (result.panes.size() == 2)
        {
            const auto &b = result.panes[1].stats;
            m_roiDeltaLabel->setText(b.valid ? tr("差值 (B − A): %1").arg(roiDeltaMetrics(a, b))
                                             : tr("差值 (B − A): —"));
        }
        else
        {
            QStringList deltaLines;
            const int count = std::min(8, static_cast<int>(result.panes.size()));
            for (int i = 1; i < count; ++i)
            {
                const auto &p = result.panes[static_cast<size_t>(i)];
                const QChar paneChar('A' + i);
                deltaLines
                    << (p.stats.valid
                            ? tr("Δ(%1−A): %2").arg(QString(paneChar), roiDeltaMetrics(a, p.stats))
                            : tr("Δ(%1−A): —").arg(paneChar));
            }
            m_roiDeltaLabel->setText(deltaLines.join('\n'));
        }
    }
    else if (m_roiDeltaLabel)
    {
        m_roiDeltaLabel->setText(tr("差值 (B − A): —"));
    }

    if (failed)
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Failed, tr("部分源图测量失败"));
    else if (unsupported)
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Unsupported,
                               tr("无法按源图精确解码该区域"));
    else if (hasAnyValid)
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Ready,
                               result.linked ? tr("源图 RGB · 全分辨率坐标 · 8 位分析")
                                             : tr("独立选区 · 源像素 RGB 分析"));
    else
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Idle);

    updateROISurfaces();
}

void CompareWorkspace::clearROIStatsDisplay()
{
    m_roiResult.reset();
    if (m_roiTable)
        m_roiTable->setRowCount(0);
    if (m_roiDeltaLabel)
        m_roiDeltaLabel->setText(tr("差值 (B − A): —"));
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
    const bool hasAnyRoi =
        m_roiLinked ? !m_lastSelection.isEmpty()
                    : std::any_of(m_cellViews.begin(), m_cellViews.end(),
                                  [](RawImageView *v) { return v && !v->selection().isEmpty(); });
    if (m_engine.imageCount() < 2)
        detail = tr("无法联动 ROI — 至少需要两张图");
    else if (!linkedROIAvailable())
        detail = tr("无法联动 ROI — 图像尺寸不一致");
    else if (m_lastSelection.isEmpty())
        detail = tr("联动 ROI 就绪 — 源图坐标");
    else if (detail.isEmpty())
        detail = tr("源图 RGB · 8 位分析");
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
    lines << QStringLiteral("ROI\tX=%1\tY=%2\tW=%3\tH=%4\t像素=%5")
                 .arg(m_lastSelection.x)
                 .arg(m_lastSelection.y)
                 .arg(m_lastSelection.width)
                 .arg(m_lastSelection.height)
                 .arg(static_cast<qint64>(m_lastSelection.width) * m_lastSelection.height);
    if (m_roiDeltaLabel)
        lines << m_roiDeltaLabel->text();
    QApplication::clipboard()->setText(lines.join('\n'));
}
