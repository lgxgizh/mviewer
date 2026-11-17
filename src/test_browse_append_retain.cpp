// Name-sort scan batches publish empty, then 1, then 16, then 16+128 files.
// An append must not cancel or restart the current image's still-needed
// neighbor, and must not evict that neighbor's warm raster. A same list is a
// no-op. A shrink that drops the neighbor still cancels or evicts it.

#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"

#include <QApplication>
#include <QColor>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <thread>

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

constexpr int kLargeW = 2000;
constexpr int kLargeH = 2001; // 4,002,000 px, just over the 8-bit 4 MP LOD cutoff
constexpr int kFillerEdge = 16;
constexpr int kBatch16 = 16;
constexpr int kBatch144 = 144; // first screen (16) plus one 128-file scan batch
constexpr int kReadyMs = 30000;
static_assert(kLargeW * kLargeH > 4 * 1000 * 1000, "neighbor must take the LOD preload path");
static_assert(kFillerEdge * kFillerEdge <= 4 * 1000 * 1000, "fillers stay under the LOD cutoff");

namespace
{

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QApplication::processEvents(QEventLoop::AllEvents, 1);
}

bool waitTrue(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 1);
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

bool waitLodReady(ImageViewer &viewer, const QString &path)
{
    return waitTrue([&] { return viewer.currentPath() == path && viewer.isLodDisplay(); },
                    kReadyMs);
}

bool writeJpeg(const QString &path, int w, int h, const QColor &color)
{
    QImage img(w, h, QImage::Format_RGB888);
    img.fill(color);
    return img.save(path, "JPEG", 80);
}

QStringList writeScanFolder(const QTemporaryDir &tmp)
{
    QStringList paths;
    paths.reserve(kBatch144);
    for (int i = 0; i < kBatch144; ++i)
    {
        const bool large = i < 2;
        const QString path =
            tmp.filePath(QStringLiteral("n%1.jpg").arg(i, 3, 10, QLatin1Char('0')));
        const QColor color(40 + (i * 17) % 200, 30 + (i * 31) % 180, 20 + (i * 13) % 200);
        const int w = large ? kLargeW : kFillerEdge;
        const int h = large ? kLargeH : kFillerEdge;
        if (!writeJpeg(path, w, h, color))
            return {};
        paths.push_back(path);
    }
    return paths;
}

using Stats = ImageViewer::DisplayRasterRetainStats;

void logStep(const char *step, const ImageViewer &viewer)
{
    const Stats s = viewer.displayRasterRetainStats();
    std::printf(
        "  %s: starts=%llu cancels=%llu evictions=%llu inFlight=%llu warm=%d "
        "ready=%d index=%d n=%d\n",
        step, static_cast<unsigned long long>(s.starts), static_cast<unsigned long long>(s.cancels),
        static_cast<unsigned long long>(s.evictions), static_cast<unsigned long long>(s.inFlight),
        viewer.displayRasterWarmCount(), viewer.isLodDisplay() ? 1 : 0, viewer.browseIndex(),
        static_cast<int>(viewer.browseSequence().size()));
    std::fflush(stdout);
}

void showViewer(ImageViewer &viewer)
{
    viewer.resize(1280, 800);
    viewer.show();
    pump(50);
}

bool sameWork(const Stats &a, const Stats &b)
{
    return a.starts == b.starts && a.cancels == b.cancels && a.evictions == b.evictions;
}

void testAppendRetain(const QStringList &paths)
{
    const QStringList first1 = paths.mid(0, 1);
    const QStringList first16 = paths.mid(0, kBatch16);
    ImageViewer viewer;
    showViewer(viewer);

    viewer.setBrowseSequence({});
    logStep("empty", viewer);
    viewer.setImage(paths[0]);
    viewer.setBrowseSequence(first1);
    logStep("empty->1 while current display is in flight", viewer);
    CHECK(waitLodReady(viewer, paths[0]), "current image becomes ready after the first append");
    CHECK(viewer.isLodDisplay(), "current image is on the LOD path");
    viewer.resetDisplayRasterRetainStats();
    logStep("ready, counters reset", viewer);

    viewer.setBrowseSequence(first16);
    const Stats after16 = viewer.displayRasterRetainStats();
    const int warm16 = viewer.displayRasterWarmCount();
    logStep("1->16", viewer);
    CHECK(after16.starts == 1, "1->16 starts the still-needed neighbor once");
    CHECK(after16.cancels == 0, "1->16 does not cancel");
    CHECK(after16.evictions == 0, "1->16 does not evict");
    CHECK(after16.inFlight == 1, "1->16 leaves that neighbor in flight");
    CHECK(viewer.isLodDisplay() && viewer.currentPath() == paths[0], "current stays ready at 16");
    CHECK(viewer.browseIndex() == 0 && viewer.browseSequence().size() == kBatch16, "index at 16");

    viewer.setBrowseSequence(paths);
    const Stats after144 = viewer.displayRasterRetainStats();
    const int warm144 = viewer.displayRasterWarmCount();
    logStep("16->144", viewer);
    CHECK(sameWork(after144, after16), "16->144 does not start, cancel, or evict");
    CHECK(after144.starts == after16.starts, "16->144 does not reissue the neighbor");
    CHECK(after144.cancels == after16.cancels, "16->144 does not cancel the neighbor");
    CHECK(after144.evictions == after16.evictions, "16->144 does not evict a warm neighbor");
    CHECK(after144.inFlight == after16.inFlight, "16->144 keeps the in-flight neighbor");
    CHECK(warm144 >= warm16, "16->144 does not drop a warm entry that was present");
    CHECK(viewer.isLodDisplay() && viewer.currentPath() == paths[0], "current stays ready at 144");
    CHECK(viewer.browseIndex() == 0 && viewer.browseSequence().size() == kBatch144 &&
              viewer.browseSequence().at(1) == paths[1],
          "neighbor keeps its index after the append");

    viewer.setBrowseSequence(paths);
    viewer.setBrowseSequence(paths);
    const Stats afterSame = viewer.displayRasterRetainStats();
    logStep("same-list x2", viewer);
    CHECK(afterSame.starts == after144.starts && afterSame.cancels == after144.cancels &&
              afterSame.evictions == after144.evictions && afterSame.inFlight == after144.inFlight,
          "same-list repeats do not start, cancel, evict, or drop in-flight work");

    CHECK(waitTrue([&] { return viewer.displayRasterWarmCount() >= 1; }, kReadyMs),
          "in-flight neighbor lands in the warm cache");
    const Stats afterWarm = viewer.displayRasterRetainStats();
    const int warmLanded = viewer.displayRasterWarmCount();
    logStep("neighbor warm", viewer);
    CHECK(sameWork(afterWarm, after144), "landing the neighbor does not restart it");
    CHECK(warmLanded >= 1, "warm entry is present");
    CHECK(viewer.isLodDisplay() && viewer.currentPath() == paths[0],
          "current stays ready once warm");

    viewer.setBrowseSequence(paths);
    const Stats afterSameWarm = viewer.displayRasterRetainStats();
    logStep("same-list after warm", viewer);
    CHECK(sameWork(afterSameWarm, afterWarm), "same-list after warm is a no-op");
    CHECK(viewer.displayRasterWarmCount() == warmLanded, "same-list does not evict the warm entry");
    CHECK(viewer.isLodDisplay(), "current stays ready across a same-list publish");

    QStringList filtered = paths;
    filtered.removeAt(1);
    const Stats beforeShrink = viewer.displayRasterRetainStats();
    const int warmBeforeShrink = viewer.displayRasterWarmCount();
    const uint64_t inFlightBeforeShrink = beforeShrink.inFlight;
    viewer.setBrowseSequence(filtered);
    const Stats afterShrink = viewer.displayRasterRetainStats();
    logStep("filter shrink drops neighbor", viewer);
    CHECK(warmBeforeShrink >= 1, "shrink has a warm neighbor to evict");
    CHECK(afterShrink.evictions == beforeShrink.evictions + static_cast<uint64_t>(warmBeforeShrink),
          "filter shrink evicts the warm cache");
    CHECK(afterShrink.cancels == beforeShrink.cancels + inFlightBeforeShrink,
          "filter shrink cancels whatever neighbor preload is still in flight");
    CHECK(viewer.displayRasterWarmCount() == 0, "warm cache is cleared on shrink");
    CHECK(viewer.isLodDisplay() && viewer.currentPath() == paths[0],
          "current image stays ready after the neighbor is dropped");
    CHECK(viewer.browseSequence().size() == kBatch144 - 1 && viewer.browseIndex() == 0,
          "current index survives the filter shrink");
}

void testShrinkCancelsInFlight(const QStringList &paths)
{
    const QStringList first1 = paths.mid(0, 1);
    const QStringList first16 = paths.mid(0, kBatch16);
    ImageViewer viewer;
    showViewer(viewer);
    viewer.setBrowseSequence({});
    viewer.setImage(paths[0]);
    viewer.setBrowseSequence(first1);
    viewer.resetDisplayRasterRetainStats();
    viewer.setBrowseSequence(first16);
    const Stats started = viewer.displayRasterRetainStats();
    logStep("shrink setup 1->16", viewer);
    CHECK(started.starts == 1 && started.inFlight == 1, "shrink setup started the neighbor");
    CHECK(started.cancels == 0 && started.evictions == 0, "shrink setup has not invalidated");

    viewer.setBrowseSequence(first1);
    const Stats after = viewer.displayRasterRetainStats();
    logStep("shrink 16->1 drops in-flight neighbor", viewer);
    CHECK(after.cancels == started.cancels + started.inFlight,
          "shrinking the neighbor out of the list cancels that preload");
    CHECK(after.starts == started.starts, "shrinking to the current file starts nothing new");
    CHECK(after.evictions == started.evictions,
          "no warm entry to evict while the preload is in flight");
    CHECK(after.inFlight == 0, "cancelled neighbor is no longer in flight");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::printf("FAIL: temp dir\n");
        return 1;
    }
    const QStringList paths = writeScanFolder(tmp);
    CHECK(paths.size() == kBatch144, "144 temp jpegs, two of them just over 4 MP");
    if (paths.size() != kBatch144)
        return 1;

    testAppendRetain(paths);
    testShrinkCancelsInFlight(paths);

    if (g_failures == 0)
        std::printf("OK: browse_append_retain (%d files, %dx%d neighbor)\n", kBatch144, kLargeW,
                    kLargeH);
    else
        std::printf("%d FAIL(s) in browse_append_retain\n", g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
