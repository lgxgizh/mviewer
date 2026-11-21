// Stale promoted-display callbacks must not drop the current raster request.
// Calls applyDisplayRasterPreloadResult directly (the queued UI callback) so
// the old reset-before-validate path fails without sleeps or large fixtures.

#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"

#include <QApplication>
#include <QImage>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>
#include <utility>

static int g_failures = 0;

#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s\n", m);                                                          \
            std::fflush(stdout);                                                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

struct ImageViewerPromotedRasterTestAccess
{
    using Result = ImageViewer::DisplayRasterPreloadResult;
    using State = ImageViewer::DisplayRasterPreloadState;
    using Handle = TaskScheduler::TaskHandle;

    static Handle arm(ImageViewer &viewer, const QString &path, uint64_t gen, uint64_t serial)
    {
        auto handle = std::make_shared<TaskScheduler::TaskContext>();
        viewer.resize(320, 240);
        viewer.m_currentPath = path;
        viewer.m_requestGen = gen;
        viewer.m_displayRequestSerial = serial;
        viewer.m_displayRequest = handle;
        viewer.m_largeSourcePending = true;
        viewer.m_lodMode = false;
        viewer.m_raster = {};
        viewer.m_loading = true;
        viewer.m_displayDegraded = false;
        viewer.m_analysisLoadIssued = true;
        viewer.m_promotedDisplayRasterPreload = {};
        return handle;
    }

    static void park(ImageViewer &viewer, const std::shared_ptr<State> &state, Handle handle)
    {
        viewer.m_promotedDisplayRasterPreload.path = viewer.m_currentPath;
        viewer.m_promotedDisplayRasterPreload.state = state;
        viewer.m_promotedDisplayRasterPreload.handle = std::move(handle);
    }

    static void dropDisplayRequest(ImageViewer &viewer)
    {
        viewer.m_displayRequest.reset();
    }

    static Handle displayRequest(const ImageViewer &viewer)
    {
        return viewer.m_displayRequest;
    }
    static uint64_t serial(const ImageViewer &viewer)
    {
        return viewer.m_displayRequestSerial;
    }
    static bool pending(const ImageViewer &viewer)
    {
        return viewer.m_largeSourcePending;
    }
    static std::shared_ptr<State> promotedState(const ImageViewer &viewer)
    {
        return viewer.m_promotedDisplayRasterPreload.state;
    }
    static void cancelDisplay(ImageViewer &viewer)
    {
        viewer.cancelDisplayRequest();
    }

    static Result makeResult(
        const QString &path, uint64_t promotedGen, uint64_t displaySerial, bool failed,
        bool withImage,
        mviewer::core::DisplayColorContext target = mviewer::core::DisplayColorContext::sRGB())
    {
        Result result;
        result.path = path;
        result.browseGeneration = promotedGen;
        result.state = std::make_shared<State>();
        result.state->promotedGeneration.store(promotedGen, std::memory_order_release);
        result.state->displaySerial.store(displaySerial, std::memory_order_release);
        result.failed = failed;
        result.target = std::move(target);
        if (withImage && !failed)
        {
            result.image = QImage(8, 8, QImage::Format_RGB888);
            result.image.fill(Qt::red);
            result.sourceRect = QRect(0, 0, 8, 8);
            result.sourceSize = QSize(8, 8);
            result.density = 1.0;
        }
        return result;
    }

    static void deliver(ImageViewer &viewer, Result result)
    {
        viewer.applyDisplayRasterPreloadResult(std::move(result));
    }
};

namespace
{

using Access = ImageViewerPromotedRasterTestAccess;

bool currentUntouched(ImageViewer &viewer, const Access::Handle &handle, uint64_t serialBefore,
                      const std::shared_ptr<Access::State> &parked)
{
    return Access::displayRequest(viewer) == handle && !handle->isCancelled() &&
           Access::serial(viewer) == serialBefore && Access::promotedState(viewer) == parked &&
           !viewer.isLodDisplay() && viewer.displayRaster().isNull();
}

void expectStaleIgnored(ImageViewer &viewer, const Access::Handle &handle, uint64_t serialBefore,
                        const std::shared_ptr<Access::State> &parked, const char *label)
{
    CHECK(currentUntouched(viewer, handle, serialBefore, parked), label);
    Access::cancelDisplay(viewer);
    CHECK(handle->isCancelled(), label);
    CHECK(!Access::displayRequest(viewer), label);
}

std::shared_ptr<Access::State> parkSentinel(ImageViewer &viewer)
{
    auto state = std::make_shared<Access::State>();
    Access::park(viewer, state, std::make_shared<TaskScheduler::TaskContext>());
    return state;
}

void testOldGeneration(const QString &path)
{
    ImageViewer viewer;
    constexpr uint64_t kGen = 3;
    constexpr uint64_t kSerial = 5;
    const auto handle = Access::arm(viewer, path, kGen, kSerial);
    const auto parked = parkSentinel(viewer);
    // First A was promoted as generation 1 / serial 2. Current visit is generation 3.
    Access::deliver(viewer, Access::makeResult(path, /*promotedGen=*/1, /*displaySerial=*/2,
                                               /*failed=*/false, /*withImage=*/true));
    expectStaleIgnored(viewer, handle, kSerial, parked,
                       "A-B-A old generation keeps the new handle");
}

void testStaleSerial(const QString &path)
{
    ImageViewer viewer;
    constexpr uint64_t kGen = 4;
    constexpr uint64_t kSerial = 9;
    const auto handle = Access::arm(viewer, path, kGen, kSerial);
    const auto parked = parkSentinel(viewer);
    Access::deliver(viewer, Access::makeResult(path, kGen, /*displaySerial=*/8, /*failed=*/false,
                                               /*withImage=*/true));
    expectStaleIgnored(viewer, handle, kSerial, parked,
                       "same generation, older serial does not retry");
}

void testDifferentPath(const QString &path, const QString &other)
{
    ImageViewer viewer;
    constexpr uint64_t kGen = 6;
    constexpr uint64_t kSerial = 11;
    const auto handle = Access::arm(viewer, path, kGen, kSerial);
    const auto parked = parkSentinel(viewer);
    Access::deliver(viewer, Access::makeResult(other, kGen, kSerial, /*failed=*/false,
                                               /*withImage=*/true));
    expectStaleIgnored(viewer, handle, kSerial, parked, "late result for a different path");
}

void testTargetMismatch(const QString &path)
{
    ImageViewer viewer;
    constexpr uint64_t kGen = 7;
    constexpr uint64_t kSerial = 12;
    const auto handle = Access::arm(viewer, path, kGen, kSerial);
    const auto parked = parkSentinel(viewer);
    Access::deliver(viewer, Access::makeResult(path, kGen, kSerial, /*failed=*/false,
                                               /*withImage=*/true,
                                               mviewer::core::DisplayColorContext::raw()));
    expectStaleIgnored(viewer, handle, kSerial, parked, "stale display target");
}

void testUnpromotedWhileRequestActive(const QString &path)
{
    ImageViewer viewer;
    constexpr uint64_t kGen = 8;
    constexpr uint64_t kSerial = 13;
    const auto handle = Access::arm(viewer, path, kGen, kSerial);
    const auto parked = parkSentinel(viewer);
    // waitingForThisPath, serial never stamped. Must not steal the live serial.
    Access::deliver(viewer, Access::makeResult(path, /*promotedGen=*/0, /*displaySerial=*/0,
                                               /*failed=*/false, /*withImage=*/true));
    expectStaleIgnored(viewer, handle, kSerial, parked,
                       "unpromoted result leaves the active request alone");
}

void testCurrentPromotedSuccess(const QString &path)
{
    ImageViewer viewer;
    constexpr uint64_t kGen = 3;
    constexpr uint64_t kSerial = 5;
    const auto handle = Access::arm(viewer, path, kGen, kSerial);
    // Slot already moved into m_displayRequest. The result must still apply.
    Access::deliver(viewer, Access::makeResult(path, kGen, kSerial, /*failed=*/false,
                                               /*withImage=*/true));
    CHECK(viewer.isLodDisplay(), "current promoted raster is shown");
    CHECK(viewer.displayRaster().size() == QSize(8, 8), "adopted the callback image");
    CHECK(!Access::pending(viewer), "current success clears the pending flag");
    CHECK(!Access::displayRequest(viewer), "finished promoted request is not left in flight");
    CHECK(Access::serial(viewer) == kSerial, "current success does not allocate another serial");
    CHECK(!handle->isCancelled(), "a delivered request is reset, not cancelled again");
}

void testCurrentPromotedFailure(const QString &path)
{
    ImageViewer viewer;
    constexpr uint64_t kGen = 3;
    constexpr uint64_t kSerial = 5;
    const auto handle = Access::arm(viewer, path, kGen, kSerial);
    Access::deliver(viewer,
                    Access::makeResult(path, kGen, kSerial, /*failed=*/true, /*withImage=*/false));
    CHECK(Access::pending(viewer), "failed current preload stays a pending re-probe");
    CHECK(!viewer.isLodDisplay(), "failed current preload does not pretend to have a raster");
    const auto retry = Access::displayRequest(viewer);
    CHECK(retry && retry != handle, "failure fallback replaces the swallowed handle");
    CHECK(Access::serial(viewer) == kSerial + 1, "re-probe takes the next display serial");
    Access::cancelDisplay(viewer);
    CHECK(retry->isCancelled(), "the re-probe handle is the one a later cancel flags");
}

void testUnpromotedAdoption(const QString &path)
{
    ImageViewer viewer;
    constexpr uint64_t kGen = 9;
    constexpr uint64_t kSerial = 4;
    Access::arm(viewer, path, kGen, kSerial);
    Access::dropDisplayRequest(viewer);
    Access::deliver(viewer, Access::makeResult(path, /*promotedGen=*/0, /*displaySerial=*/0,
                                               /*failed=*/false, /*withImage=*/true));
    CHECK(viewer.isLodDisplay(), "unpromoted waiting result is adopted when nothing is in flight");
    CHECK(viewer.displayRaster().size() == QSize(8, 8), "adopted pixels come from the callback");
    CHECK(!Access::pending(viewer), "adopted unpromoted result is not left pending");
    CHECK(!Access::displayRequest(viewer), "adoption does not leave a second probe in flight");
    CHECK(Access::serial(viewer) == kSerial + 1,
          "adoption mints a new serial instead of reusing the previous one");
}

QString writeTinyPng(const QTemporaryDir &dir)
{
    QImage image(4, 4, QImage::Format_RGB888);
    image.fill(Qt::blue);
    const QString path = dir.filePath(QStringLiteral("tiny.png"));
    if (!image.save(path, "PNG"))
        return {};
    return path;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid())
    {
        std::printf("FAIL: temp dir\n");
        return 1;
    }
    const QString path = writeTinyPng(dir);
    CHECK(!path.isEmpty(), "tiny png written");
    if (path.isEmpty())
        return 1;
    const QString other = dir.filePath(QStringLiteral("other.png"));

    testOldGeneration(path);
    testStaleSerial(path);
    testDifferentPath(path, other);
    testTargetMismatch(path);
    testUnpromotedWhileRequestActive(path);
    testCurrentPromotedSuccess(path);
    testCurrentPromotedFailure(path);
    testUnpromotedAdoption(path);

    if (g_failures == 0)
        std::printf("OK: promoted_raster_identity\n");
    else
        std::printf("%d FAIL(s) in promoted_raster_identity\n", g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
