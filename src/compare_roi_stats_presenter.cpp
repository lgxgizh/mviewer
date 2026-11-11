#include "compare_roi_stats_presenter.h"

#include <QCoreApplication>

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
    return QCoreApplication::translate("CompareWorkspace",
                                       "ΔH %1  ΔS %2  ΔV %3  ΔR %4  ΔG %5  ΔB %6  ΔR/G %7  ΔB/G %8")
        .arg(signedHueDelta(other.hMean, base.hMean),
             QString::number(other.sMean - base.sMean, 'f', 2),
             QString::number(other.vMean - base.vMean, 'f', 2),
             QString::number(other.rMean - base.rMean, 'f', 2),
             QString::number(other.gMean - base.gMean, 'f', 2),
             QString::number(other.bMean - base.bMean, 'f', 2), redGreen, blueGreen);
}

// ROI reasons are English QT_TRANSLATE_NOOP("CompareRoi", ...) literals set on
// the worker thread (or an exception message); translate them for display.
QString roiReasonText(const std::string &reason)
{
    return QCoreApplication::translate("CompareRoi", reason.c_str());
}

QString paneStateText(const mviewer::ui::ROIPaneMeasurement &pane)
{
    using State = mviewer::ui::ROIPaneState;
    switch (pane.state)
    {
    case State::Ready:
        return QCoreApplication::translate("CompareWorkspace", "Ready");
    case State::Unsupported:
        return QCoreApplication::translate("CompareWorkspace", "Unsupported: %1")
            .arg(roiReasonText(pane.reason));
    case State::Failed:
        return QCoreApplication::translate("CompareWorkspace", "Failed: %1")
            .arg(roiReasonText(pane.reason));
    case State::Cancelled:
        return QCoreApplication::translate("CompareWorkspace", "Cancelled");
    }
    return QCoreApplication::translate("CompareWorkspace", "Failed");
}

QString paneName(const mviewer::domain::ImageMetadata &metadata, int index)
{
    const QString name = metadata.fileName.empty() ? QStringLiteral("#%1").arg(index + 1)
                                                   : QString::fromStdString(metadata.fileName);
    return QStringLiteral("%1 — %2").arg(QChar('A' + index), name);
}
} // namespace

namespace mviewer::ui
{

RoiStatsView presentRoiStats(const ROIStatsBatchResult &result,
                             const std::vector<mviewer::domain::ImageMetadata> &paneMetadata)
{
    RoiStatsView view;
    bool unsupported = false;
    bool failed = false;
    bool hasAnyValid = false;
    view.rows.reserve(result.panes.size());
    for (int row = 0; row < static_cast<int>(result.panes.size()); ++row)
    {
        const auto &pane = result.panes[static_cast<size_t>(row)];
        const mviewer::domain::ImageMetadata metadata = row < static_cast<int>(paneMetadata.size())
                                                            ? paneMetadata[static_cast<size_t>(row)]
                                                            : mviewer::domain::ImageMetadata{};
        const QString stateText =
            (!result.linked && !pane.stats.valid)
                ? QCoreApplication::translate("CompareWorkspace", "No selection")
                : paneStateText(pane);
        RoiStatsRow statsRow;
        statsRow.cells = {paneName(metadata, row),
                          pane.stats.valid ? meanText(pane.stats.hMean) : QStringLiteral("—"),
                          pane.stats.valid ? meanText(pane.stats.sMean) : QStringLiteral("—"),
                          pane.stats.valid ? meanText(pane.stats.vMean) : QStringLiteral("—"),
                          pane.stats.valid ? meanText(pane.stats.rMean) : QStringLiteral("—"),
                          pane.stats.valid ? meanText(pane.stats.gMean) : QStringLiteral("—"),
                          pane.stats.valid ? meanText(pane.stats.bMean) : QStringLiteral("—"),
                          pane.stats.valid ? ratioText(pane.stats, true) : QStringLiteral("—"),
                          pane.stats.valid ? ratioText(pane.stats, false) : QStringLiteral("—"),
                          pane.stats.valid ? QString::number(pane.stats.pixelCount)
                                           : QStringLiteral("—"),
                          stateText};
        statsRow.nameToolTip = QString::fromStdString(metadata.filePath);
        statsRow.stateToolTip = stateText;
        view.rows.push_back(std::move(statsRow));
        if (pane.stats.valid)
            hasAnyValid = true;
        if (result.linked)
        {
            unsupported = unsupported || pane.state == ROIPaneState::Unsupported;
            failed = failed || pane.state == ROIPaneState::Failed;
        }
        else
        {
            failed = failed || pane.state == ROIPaneState::Failed;
        }
    }

    if (result.panes.size() >= 2 && result.panes[0].stats.valid)
    {
        const auto &a = result.panes[0].stats;
        if (result.panes.size() == 2)
        {
            const auto &b = result.panes[1].stats;
            view.deltaText =
                b.valid ? QCoreApplication::translate("CompareWorkspace", "Difference (B − A): %1")
                              .arg(roiDeltaMetrics(a, b))
                        : QCoreApplication::translate("CompareWorkspace", "Delta (B − A): —");
        }
        else
        {
            QStringList deltaLines;
            const int count = std::min(8, static_cast<int>(result.panes.size()));
            for (int i = 1; i < count; ++i)
            {
                const auto &p = result.panes[static_cast<size_t>(i)];
                const QChar paneChar('A' + i);
                deltaLines << (p.stats.valid
                                   ? QCoreApplication::translate("CompareWorkspace", "Δ(%1−A): %2")
                                         .arg(QString(paneChar), roiDeltaMetrics(a, p.stats))
                                   : QCoreApplication::translate("CompareWorkspace", "Δ(%1−A): —")
                                         .arg(paneChar));
            }
            view.deltaText = deltaLines.join('\n');
        }
    }
    else
    {
        view.deltaText = QCoreApplication::translate("CompareWorkspace", "Delta (B − A): —");
    }

    if (failed)
    {
        view.state = ROIMeasurementState::Failed;
        view.stateDetail =
            QCoreApplication::translate("CompareWorkspace", "Some source images failed to measure");
    }
    else if (unsupported)
    {
        view.state = ROIMeasurementState::Unsupported;
        view.stateDetail = QCoreApplication::translate(
            "CompareWorkspace", "Cannot decode this region precisely from the source image");
    }
    else if (hasAnyValid)
    {
        view.state = ROIMeasurementState::Ready;
        view.stateDetail =
            result.linked
                ? QCoreApplication::translate(
                      "CompareWorkspace",
                      "Source RGB · Full-resolution coordinates · 8-bit analysis")
                : QCoreApplication::translate("CompareWorkspace",
                                              "Independent selection · Source pixel RGB analysis");
    }
    else
    {
        view.state = ROIMeasurementState::Idle;
    }
    return view;
}

} // namespace mviewer::ui
