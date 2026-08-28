// M53 Native Windows release soak.
//
// Repeats the real large-TIFF Viewer/Compare lifecycle so cancellation,
// latest-wins generations, and scheduler/lifetime cleanup are measured over a
// session rather than a single lucky decode.

#include "compareworkspace.h"
#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QEventLoop>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>

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

bool waitForPanes(CompareWorkspace *workspace, int count)
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
        90000);
}

QCheckBox *findMode(CompareWorkspace *workspace, const QString &label)
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
        return 2;

    constexpr int kRounds = 5;
    std::printf("=== M53 large-source soak: %d rounds ===\n", kRounds);
    for (int round = 1; round <= kRounds; ++round)
    {
        std::printf("round %d/%d\n", round, kRounds);
        {
            ImageViewer viewer;
            viewer.resize(1280, 800);
            viewer.show();
            bool ready = false;
            QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                             [&](const QSize &) { ready = true; });
            viewer.setBrowseSequence({tiff});
            viewer.setImage(tiff);
            CHECK(waitTrue([&] { return ready; }, 90000),
                  "soak Viewer reaches a bounded TIFF display");
            viewer.zoomIn();
            viewer.zoomOut();
            viewer.zoomFit();
            pump(180);
        }
        CHECK(waitTrue(schedulerIdle, 20000), "soak Viewer teardown returns pools to idle");

        {
            CompareWorkspace workspace;
            workspace.resize(1280, 800);
            workspace.show();
            workspace.setImages({tiff, tiff});
            CHECK(waitForPanes(&workspace, 2), "soak Compare materializes both TIFF panes");
            if (QCheckBox *split = findMode(&workspace, QStringLiteral("左右分割")))
            {
                split->setChecked(true);
                pump(80);
                split->setChecked(false);
            }
            // Supersede a live display batch with an identical final pair.
            workspace.setImages({tiff, tiff});
            CHECK(waitForPanes(&workspace, 2), "soak Compare latest generation remains usable");
            pump(180);
        }
        CHECK(waitTrue(schedulerIdle, 20000), "soak Compare teardown returns pools to idle");
    }

    CHECK(waitTrue(schedulerIdle, 30000), "soak final scheduler state is idle");
    std::printf("=== M53 large-source soak: %s (%d failures) ===\n",
                g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
