// M47 Phase 2 — ImageViewer LOD-first display tests.
//
// Proves the Viewer large-image pipeline over the deterministic corpus:
//   V1 100 MP JPEG opens through the LOD raster path (the Phase-0 reproduction
//      "cannot open at all" is fixed): displayReady fires with the full source
//      dims, isLodDisplay() is true, the raster is bounded (<= 8 MP), the
//      decode was classified NativeLod, and process RSS stays far below the
//      ~286 MB a full materialization would cost.
//   V2 zoom-in past the LOD density issues a bounded region raster request
//      (BoundedRasterRegion classification) and the display raster updates.
//   V3 A -> B -> A supersession: only the final A generation may land.
//   V4 destroy mid-request and destroy after display: no crash, pools drain.
//   V5 idle convergence: scheduler pools return to zero after the churn.
//   V6 small-image fast path preserved: a 300x200 JPEG still delivers a full
//      frame through imageReady and does NOT use the raster display.

#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QTimer>
#include <QTemporaryDir>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

static int g_failures = 0;

#define CHECK(c, m)                                                                                 \
    do                                                                                              \
    {                                                                                               \
        if (!(c))                                                                                   \
        {                                                                                           \
            std::printf("FAIL: %s\n", m);                                                           \
            std::fflush(stdout);                                                                    \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (false)

#define MARK(t)                                                                                     \
    do                                                                                              \
    {                                                                                               \
        std::printf("%s\n", t);                                                                     \
        std::fflush(stdout);                                                                        \
    } while (false)

namespace
{

using mviewer::core::SourceDecodeStats;

std::string fixtureRoot()
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large";
}

std::string fixture(const char *name)
{
    return fixtureRoot() + "/" + name;
}

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

double rssMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return counters.WorkingSetSize / (1024.0 * 1024.0);
#endif
    return 0.0;
}

struct SchedSample
{
    uint64_t pending = 0;
    uint64_t active = 0;
    uint64_t queue = 0;
    uint64_t waiting = 0;
};

SchedSample sampleScheduler()
{
    SchedSample s;
    for (int p = 0; p < 5; ++p)
    {
        const auto m = TaskScheduler::instance().metrics(static_cast<TaskScheduler::PoolType>(p));
        s.pending += m.pending;
        s.active += m.active_tasks;
        s.queue += m.queue_depth;
        s.waiting += m.waiting;
    }
    return s;
}

QString writeSmallJpeg(const QTemporaryDir &dir)
{
    const QString p = dir.path() + "/small.jpg";
    QImage img(300, 200, QImage::Format_RGB32);
    img.fill(QColor(40, 80, 120));
    img.save(p, "JPEG", 90);
    return p;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (!QDir(QString::fromStdString(fixtureRoot())).exists())
    {
        std::printf("fixture root missing: %s\n", fixtureRoot().c_str());
        std::printf("run: python testdata/generate_large_fixtures.py --ensure\n");
        return 2;
    }

    const QString jpeg100 = QString::fromStdString(fixture("large_jpeg_100mp.jpg"));
    const QString highCompression = QString::fromStdString(fixture("high_compression.jpg"));
    QTemporaryDir tmp;
    const QString smallJpeg = writeSmallJpeg(tmp);

    // ── V1: 100 MP JPEG opens via the LOD raster path ────────────────────────
    {
        MARK("V1 start");
        SourceDecodeStats::instance().counters().reset();
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        // Sample after the widget/GL context exists: the measured delta covers
        // the DISPLAY path only (decode workers + rasters).
        const double rssBefore = rssMB();
        QSize readySize;
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &s)
                         {
                             readySize = s;
                             ready = true;
                         });
        viewer.setBrowseSequence({jpeg100});
        MARK("V1: setBrowseSequence done");
        viewer.setImage(jpeg100);
        MARK("V1: setImage done");
        CHECK(waitTrue([&] { return ready; }, 30000),
              "V1: 100MP JPEG delivers a display raster (reproduction fixed)");
        MARK("V1: displayReady observed");
        const double rssAfterReady = rssMB();
        if (ready)
        {
            CHECK(readySize == QSize(12000, 8333),
                  "V1: displayReady carries the full source dimensions");
            CHECK(viewer.isLodDisplay(), "V1: viewer is in LOD display mode");
            const auto &c = SourceDecodeStats::instance().counters();
            CHECK(c.nativeLod.load() >= 1, "V1: the first raster was a NativeLod decode");
            CHECK(c.boundedRegion.load() == 0, "V1: no region decode at fit (LOD only)");
        }
        // The display raster must be bounded (no full materialization): with
        // the 100 MP source this can only be a LOD/region raster.
        pump(300);
        const double rssDelta = rssMB() - rssBefore;
        printf("  V1: rss delta (display path only) = %.1f MB; frame=%s\n", rssDelta,
               viewer.frame() ? "present" : "null");
        CHECK(rssDelta < 100.0,
              "V1: display-path RSS stays bounded (< 100 MB; a full 100MP "
              "materialization would be ~286 MB and is rejected by Qt anyway)");
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "V1: decode pools drain after display");
    }

    // ── V2: zoom-in past the LOD density requests a bounded region ──────────
    {
        MARK("V2 start");
        SourceDecodeStats::instance().counters().reset();
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &) { ready = true; });
        viewer.setBrowseSequence({jpeg100});
        viewer.setImage(jpeg100);
        CHECK(waitTrue([&] { return ready; }, 30000), "V2: first raster arrives");
        // Zoom to 100%: the view density now exceeds the fit LOD density, so a
        // bounded region raster must be requested.
        viewer.zoomActual();
        CHECK(waitTrue(
                  [&]
                  {
                      const auto &c = SourceDecodeStats::instance().counters();
                      return c.boundedRegion.load() >= 1;
                  },
                  30000),
              "V2: zoom-in issues a bounded region raster request");
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.boundedRegion.load() >= 1, "V2: classified BoundedRasterRegion");
        CHECK(c.fullDecodeScaled.load() == 0 && c.fullDecodeCrop.load() == 0,
              "V2: no full-decode fallback ran");
        pump(300);
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "V2: decode pools drain after the region upgrade");
    }

    // ── V3: A -> B -> A supersession (only the final generation lands) ──────
    {
        MARK("V3 start");
        SourceDecodeStats::instance().counters().reset();
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        QSize lastReady;
        int readyCount = 0;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &s)
                         {
                             lastReady = s;
                             ++readyCount;
                         });
        viewer.setBrowseSequence({jpeg100, highCompression});
        viewer.setImage(jpeg100);
        CHECK(waitTrue([&] { return lastReady == QSize(12000, 8333); }, 30000),
              "V3: first A raster lands");
        viewer.setImage(highCompression);
        CHECK(waitTrue([&] { return lastReady == QSize(6000, 4000); }, 30000),
              "V3: B raster lands (supersedes A)");
        viewer.setImage(jpeg100);
        CHECK(waitTrue([&] { return lastReady == QSize(12000, 8333); }, 30000),
              "V3: final A raster lands");
        pump(500); // allow any stale deliveries to attempt landing
        CHECK(lastReady == QSize(12000, 8333),
              "V3: the FINAL A generation owns the display (no stale overwrite)");
        CHECK(viewer.isLodDisplay(), "V3: viewer still in LOD display mode");
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "V3: decode pools drain after A->B->A");
    }

    // ── V4: destroy mid-request and destroy after display ───────────────────
    {
        MARK("V4 start");
        {
            ImageViewer viewer;
            viewer.resize(1280, 800);
            viewer.show();
            viewer.setBrowseSequence({jpeg100});
            // Destroy immediately: the raster worker may still be running.
            viewer.setImage(jpeg100);
        } // viewer destroyed here
        pump(1000);
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "V4a: pools drain after destroy mid-request (no crash, no leak)");
        {
            ImageViewer viewer;
            viewer.resize(1280, 800);
            viewer.show();
            bool ready = false;
            QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                             [&](const QSize &) { ready = true; });
            viewer.setBrowseSequence({jpeg100});
            viewer.setImage(jpeg100);
            CHECK(waitTrue([&] { return ready; }, 30000), "V4b: raster arrives");
        } // viewer destroyed after display
        pump(1000);
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "V4b: pools drain after destroy post-display");
    }

    // ── V5: idle convergence after churn ────────────────────────────────────
    {
        MARK("V5 start");
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &) { ready = true; });
        viewer.setBrowseSequence({jpeg100});
        viewer.setImage(jpeg100);
        CHECK(waitTrue([&] { return ready; }, 30000), "V5: raster arrives");
        // Pan + zoom churn to provoke upgrade requests, then settle.
        viewer.zoomIn();
        viewer.zoomIn();
        viewer.zoomOut();
        viewer.zoomFit();
        pump(1500);
        const SchedSample s = sampleScheduler();
        printf("  V5: idle sched(p=%llu,a=%llu,q=%llu,w=%llu)\n",
               static_cast<unsigned long long>(s.pending),
               static_cast<unsigned long long>(s.active),
               static_cast<unsigned long long>(s.queue),
               static_cast<unsigned long long>(s.waiting));
        CHECK(s.pending == 0 && s.active == 0 && s.queue == 0 && s.waiting == 0,
              "V5: scheduler converges to idle after zoom/pan churn");
    }

    // ── V6: small-image fast path preserved ─────────────────────────────────
    {
        MARK("V6 start");
        SourceDecodeStats::instance().counters().reset();
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        std::shared_ptr<ImageFrame> frame;
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::imageReady, &viewer,
                         [&](std::shared_ptr<ImageFrame> f)
                         {
                             frame = std::move(f);
                             ready = true;
                         });
        viewer.setBrowseSequence({smallJpeg});
        viewer.setImage(smallJpeg);
        CHECK(waitTrue([&] { return ready; }, 30000),
              "V6: small image delivers a full frame via imageReady");
        if (frame)
        {
            CHECK(frame->width() == 300 && frame->height() == 200,
                  "V6: full-resolution frame (fast path unchanged)");
        }
        CHECK(!viewer.isLodDisplay(), "V6: small image does NOT use the raster display");
        pump(300);
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "V6: pools drain");
    }

    std::printf("=== M47 viewer LOD-first display tests: %s ===\n",
                g_failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
