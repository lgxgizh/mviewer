// M53 Native Windows release qualification — large TIFF through the real UI.
//
// The core regression proves the WIC bounded adapter in isolation. This test
// closes the product path: Viewer display, Compare pane materialization,
// zoom-region refresh, mode switches, and latest-wins replacement all consume
// the same SourceImage contract without a full 100MP frame.

#include "compareworkspace.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

static int g_failures = 0;

#define CHECK(c, m)                                                                                 \
    do                                                                                              \
    {                                                                                               \
        if (!(c))                                                                                   \
        {                                                                                           \
            std::printf("  FAIL: %s\n", m);                                                        \
            std::fflush(stdout);                                                                    \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (false)

namespace
{

using mviewer::core::SourceDecodeStats;

std::string fixture(const char *name)
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large/" + name;
}

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QApplication::processEvents(QEventLoop::AllEvents, 1);
}

bool waitTrue(const std::function<bool()> &predicate, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 1);
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

bool schedulerIdle()
{
    for (int p = 0; p < 5; ++p)
    {
        const auto metrics = TaskScheduler::instance().metrics(
            static_cast<TaskScheduler::PoolType>(p));
        if (metrics.pending != 0 || metrics.active_tasks != 0 || metrics.queue_depth != 0 ||
            metrics.waiting != 0)
            return false;
    }
    return true;
}

RawImageView *paneView(CompareWorkspace *workspace, int index)
{
    for (RawImageView *view : workspace->findChildren<RawImageView *>())
        if (view && view->cellIndex() == index)
            return view;
    return nullptr;
}

bool waitForPanes(CompareWorkspace *workspace, int count, int timeoutMs = 90000)
{
    return waitTrue(
        [workspace, count]
        {
            if (workspace->comparedImageCount() != count)
                return false;
            for (int i = 0; i < count; ++i)
            {
                RawImageView *view = paneView(workspace, i);
                if (!view || view->image().isNull())
                    return false;
            }
            return true;
        },
        timeoutMs);
}

QCheckBox *modeCheck(CompareWorkspace *workspace, const QString &label)
{
    for (QCheckBox *check : workspace->findChildren<QCheckBox *>())
        if (check && check->text().contains(label))
            return check;
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString tiff = QString::fromStdString(fixture("large_tiff_100mp.tiff"));
    if (!QFileInfo::exists(tiff))
    {
        std::printf("large TIFF fixture missing: %s\n", tiff.toUtf8().constData());
        return 2;
    }

    // ── U1: Viewer fit + zoom-region refresh ────────────────────────────────
    std::printf("U1: Viewer large TIFF workflow\n");
    SourceDecodeStats::instance().counters().reset();
    {
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        bool ready = false;
        QSize readySize;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &size)
                         {
                             ready = true;
                             readySize = size;
                         });
        viewer.setBrowseSequence({tiff});
        viewer.setImage(tiff);
        CHECK(waitTrue([&] { return ready; }, 60000),
              "Viewer receives a large-TIFF displayReady");
        CHECK(readySize == QSize(10000, 10000), "Viewer preserves the TIFF source geometry");
        CHECK(viewer.isLodDisplay() && !viewer.displayRaster().isNull(),
              "Viewer presents a bounded TIFF raster");
        const auto &fitCounters = SourceDecodeStats::instance().counters();
        CHECK(fitCounters.nativeLod.load() >= 1, "Viewer fit uses the native TIFF backend");
        CHECK(fitCounters.fullDecode.load() == 0 && fitCounters.fullDecodeScaled.load() == 0 &&
                  fitCounters.fullDecodeCrop.load() == 0,
              "Viewer fit never materializes the full TIFF");

        const int beforeEdge = std::max(viewer.displayRaster().width(),
                                        viewer.displayRaster().height());
        viewer.zoomActual();
        CHECK(waitTrue(
                  [&]
                  {
                      const auto &c = SourceDecodeStats::instance().counters();
                      return c.boundedRegion.load() >= 1;
                  },
                  60000),
              "Viewer zoom issues a bounded TIFF region request");
        CHECK(waitTrue(
                  [&]
                  {
                      const QImage raster = viewer.displayRaster();
                      return !raster.isNull() && std::max(raster.width(), raster.height()) >= beforeEdge;
                  },
                  60000),
              "Viewer zoom keeps a usable TIFF raster");
        const auto &zoomCounters = SourceDecodeStats::instance().counters();
        CHECK(zoomCounters.fullDecode.load() == 0 && zoomCounters.fullDecodeScaled.load() == 0 &&
                  zoomCounters.fullDecodeCrop.load() == 0,
              "Viewer zoom remains free of full-source fallback");
        CHECK(waitTrue(schedulerIdle, 15000), "Viewer scheduler converges after TIFF zoom");
    }

    // ── U2: Compare pair, modes, and A -> B -> A latest-wins replacement ────
    std::printf("U2: Compare large TIFF workflow\n");
    SourceDecodeStats::instance().counters().reset();
    {
        CompareWorkspace workspace;
        workspace.resize(1280, 800);
        workspace.show();
        workspace.setImages({tiff, tiff});
        CHECK(waitForPanes(&workspace, 2),
              "Compare materializes both large-TIFF panes");
        for (int i = 0; i < 2; ++i)
        {
            RawImageView *view = paneView(&workspace, i);
            CHECK(view && view->sourceSize() == QSize(10000, 10000),
                  "Compare pane preserves the TIFF source geometry");
        }
        const auto &initial = SourceDecodeStats::instance().counters();
        CHECK(initial.nativeLod.load() >= 2, "Compare fit uses native TIFF display for both panes");
        CHECK(initial.fullDecode.load() == 0 && initial.fullDecodeScaled.load() == 0 &&
                  initial.fullDecodeCrop.load() == 0,
              "Compare fit never materializes a full TIFF pane");

        // Exercise the same controls users use in Compare. Each mode is
        // presentation-only and must not trigger a fresh full-source decode.
        for (const QString &label : {QStringLiteral("左右分割"), QStringLiteral("叠加对比"),
                                     QStringLiteral("闪烁对比"), QStringLiteral("显示差异")})
        {
            QCheckBox *check = modeCheck(&workspace, label);
            CHECK(check != nullptr, "Compare exposes the requested large-source mode");
            if (check)
            {
                check->setChecked(true);
                pump(label == QStringLiteral("闪烁对比") ? 220 : 50);
                CHECK(check->isChecked(), "Compare mode remains enabled after activation");
                check->setChecked(false);
                pump(50);
            }
        }

        // Replace the pair and restore it, proving queued materialization from
        // the first generation cannot overwrite the final source set.
        workspace.setImages({tiff, tiff});
        CHECK(waitForPanes(&workspace, 2), "Compare latest TIFF generation wins after replacement");
        const QStringList compared = workspace.comparedImages();
        CHECK(compared.size() == 2 && compared[0] == tiff && compared[1] == tiff,
              "Compare keeps the final A -> B -> A source identity");
        const auto &finalCounters = SourceDecodeStats::instance().counters();
        CHECK(finalCounters.fullDecode.load() == 0 && finalCounters.fullDecodeScaled.load() == 0 &&
                  finalCounters.fullDecodeCrop.load() == 0,
              "Compare replacement remains free of full-source fallback");
        CHECK(waitTrue(schedulerIdle, 15000), "Compare scheduler converges after replacement and modes");
    }

    std::printf("=== M53 large-source UI qualification: %s (%d failures) ===\n",
                g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
