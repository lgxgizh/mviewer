#include "compare_roi_stats_presenter.h"

#include "core/image/ImageStats.h"
#include "domain/Image.h"

#include <QCoreApplication>

#include <iostream>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << std::endl;                                             \
            return 1;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << std::endl;                                             \
        }                                                                                          \
    } while (0)

using mviewer::ui::presentRoiStats;
using mviewer::ui::ROIMeasurementState;
using mviewer::ui::ROIPaneMeasurement;
using mviewer::ui::ROIPaneState;
using mviewer::ui::ROIStatsBatchResult;

namespace
{
mviewer::core::ROIChannelStats validStats(double h, double s, double v, double r, double g,
                                          double b, double rg, double bg, int64_t pixels)
{
    mviewer::core::ROIChannelStats stats;
    stats.hMean = h;
    stats.sMean = s;
    stats.vMean = v;
    stats.rMean = r;
    stats.gMean = g;
    stats.bMean = b;
    stats.rOverG = rg;
    stats.bOverG = bg;
    stats.pixelCount = pixels;
    stats.valid = true;
    stats.ratiosValid = true;
    return stats;
}

mviewer::domain::ImageMetadata metaNamed(const char *fileName, const char *filePath)
{
    mviewer::domain::ImageMetadata metadata;
    metadata.fileName = fileName;
    metadata.filePath = filePath;
    return metadata;
}

ROIPaneMeasurement paneWith(const mviewer::core::ROIChannelStats &stats, ROIPaneState state)
{
    ROIPaneMeasurement pane;
    pane.stats = stats;
    pane.state = state;
    return pane;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    {
        ROIStatsBatchResult result;
        result.linked = true;
        result.panes.push_back(
            paneWith(validStats(12.5, 0.25, 1, 10, 20, 30, 1.25, 0.5, 100), ROIPaneState::Ready));
        result.panes.push_back(
            paneWith(validStats(22.5, 0.5, 2, 11, 21, 31, 1.5, 0.75, 100), ROIPaneState::Ready));
        const auto view = presentRoiStats(
            result, {metaNamed("a.png", "/imgs/a.png"), metaNamed("b.png", "/imgs/b.png")});
        CHECK(view.rows.size() == 2, "linked 2-pane has two rows");
        CHECK(view.rows[0].cells.size() == 11, "linked row has 11 cells");
        CHECK(view.rows[0].cells[0] == QStringLiteral("A — a.png"), "pane name is A — a.png");
        CHECK(view.rows[0].cells[1] == QStringLiteral("12.50"), "H mean uses 2 decimals");
        CHECK(view.rows[0].cells[5] == QStringLiteral("20.00"), "G mean uses 2 decimals");
        CHECK(view.rows[0].cells[7] == QStringLiteral("1.2500"), "R/G uses 4 decimals");
        CHECK(view.rows[0].cells[8] == QStringLiteral("0.5000"), "B/G uses 4 decimals");
        CHECK(view.rows[0].nameToolTip == QStringLiteral("/imgs/a.png"),
              "name tooltip is file path");
        CHECK(view.deltaText.startsWith(QStringLiteral("Difference (B − A): ΔH")),
              "delta text starts with Difference (B − A): ΔH");
        CHECK(view.state == ROIMeasurementState::Ready, "valid linked result is Ready");
    }

    {
        ROIStatsBatchResult result;
        result.linked = true;
        result.panes.push_back(
            paneWith(validStats(350.0, 0, 0, 0, 0, 0, 1, 1, 1), ROIPaneState::Ready));
        result.panes.push_back(
            paneWith(validStats(10.0, 0, 0, 0, 0, 0, 1, 1, 1), ROIPaneState::Ready));
        const auto view =
            presentRoiStats(result, {metaNamed("a.png", "a"), metaNamed("b.png", "b")});
        CHECK(view.deltaText.contains(QStringLiteral("ΔH 20.00")),
              "hue wrap 350 vs 10 is ΔH 20.00");
    }

    {
        ROIStatsBatchResult result;
        result.linked = false;
        ROIPaneMeasurement pane;
        pane.state = ROIPaneState::Ready;
        result.panes.push_back(pane);
        const auto view = presentRoiStats(result, {metaNamed("c.png", "/imgs/c.png")});
        CHECK(view.rows[0].cells[10] == QStringLiteral("No selection"),
              "unlinked empty is No selection");
        bool dashes = true;
        for (int column = 1; column <= 9; ++column)
            dashes = dashes && view.rows[0].cells[column] == QStringLiteral("—");
        CHECK(dashes, "unlinked empty numeric cells are em dashes");
        CHECK(view.rows[0].stateToolTip == QStringLiteral("No selection"),
              "state tooltip matches status cell");
    }

    {
        ROIStatsBatchResult result;
        result.linked = true;
        ROIPaneMeasurement pane;
        pane.state = ROIPaneState::Failed;
        pane.reason = "decode failed";
        result.panes.push_back(pane);
        const auto view = presentRoiStats(result, {metaNamed("a.png", "a")});
        CHECK(view.state == ROIMeasurementState::Failed, "failed pane sets Failed");
        CHECK(view.stateDetail == QStringLiteral("Some source images failed to measure"),
              "failed detail text");
    }

    {
        ROIStatsBatchResult result;
        result.linked = true;
        for (int i = 0; i < 3; ++i)
            result.panes.push_back(
                paneWith(validStats(1, 1, 1, 1, 1, 1, 1, 1, 8), ROIPaneState::Ready));
        const auto view = presentRoiStats(
            result, {metaNamed("a.png", "a"), metaNamed("b.png", "b"), metaNamed("c.png", "c")});
        CHECK(view.deltaText.count(QStringLiteral("Δ(")) == 2, "3 panes produce two Δ( lines");
    }

    {
        ROIStatsBatchResult result;
        result.linked = true;
        ROIPaneMeasurement pane;
        pane.state = ROIPaneState::Ready;
        result.panes.push_back(pane);
        result.panes.push_back(pane);
        const auto view =
            presentRoiStats(result, {metaNamed("a.png", "a"), metaNamed("b.png", "b")});
        CHECK(view.state == ROIMeasurementState::Idle, "all invalid is Idle");
        CHECK(view.stateDetail.isEmpty(), "Idle detail is empty");
        CHECK(view.deltaText == QStringLiteral("Delta (B − A): —"), "invalid delta fallback");
    }

    return 0;
}
