// M47 Phase 3 — Compare large-image source-backed display tests.
//
// Proves the Compare pane pipeline over the deterministic corpus:
//   C1 100 MP JPEG pair (duplicate paths): BOTH panes exist — the engine keeps
//      a metadata-only placeholder per infeasible source so the requested pane
//      count and pane/path alignment survive — and both panes display through
//      the source-backed LOD path with bounded rasters. No full-frame load is
//      ever attempted (fullDecodeScaled == 0 && fullDecodeCrop == 0), no
//      loadWarning fires (a skip is not a failure), and process RSS stays far
//      below the ~286 MB a single full materialization would cost.
//   C2 mixed feasible + infeasible: the feasible pane loads its full frame
//      (analysis source) and displays the client-side bounded LOD; the
//      infeasible pane displays via SourceImage at its REQUESTED index.
//   C3 zooming a source-backed pane issues a denser re-materialization
//      (NativeLod classification grows, raster edge grows) with no full decode.
//   C4 destroy mid-request and destroy after display: no crash, pools drain.
//   C5 TIFF honest fallback: an infeasible TIFF pane stays blank (no native
//      LOD; the bounded attempt is recorded as FullDecodeScaled and fails
//      inside Qt's allocation limit), the JPEG pane still displays, and the
//      skip never surfaces as a load failure.
//   C6 failure vs infeasible accounting: a missing file among infeasible +
//      feasible sources emits a warning that counts ONLY the missing one.

#include "compareworkspace.h"
#include "core/compare/CompareEngine.h"
#include "core/image/ImageFrame.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QTemporaryDir>
#include <QWheelEvent>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
};

SchedSample sampleScheduler()
{
    SchedSample s;
    for (int p = 0; p < 5; ++p)
    {
        const auto m = TaskScheduler::instance().metrics(static_cast<TaskScheduler::PoolType>(p));
        s.pending += m.pending;
        s.active += m.active_tasks;
    }
    return s;
}

RawImageView *paneView(CompareWorkspace *ws, int index)
{
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (v && v->cellIndex() == index)
            return v;
    }
    return nullptr;
}

// Wait until comparedImageCount() reaches `expected` (async load terminal) and
// then until the panes with the given indices carry non-null images (the async
// display-materialization batch landed). Returns true on success.
bool waitForLoadedPanes(CompareWorkspace *ws, int expected, const std::vector<int> &panes,
                        int timeoutMs = 90000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto panesReady = [&]()
    {
        for (int idx : panes)
        {
            RawImageView *v = paneView(ws, idx);
            if (!v || v->image().isNull())
                return false;
        }
        return true;
    };
    while (std::chrono::steady_clock::now() < deadline)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 1);
        if (ws->comparedImageCount() == expected && panesReady())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return ws->comparedImageCount() == expected && panesReady();
}

struct WarningCapture
{
    bool warned = false;
    QString text;
};

void captureWarnings(CompareWorkspace *ws, WarningCapture &cap)
{
    QObject::connect(ws, &CompareWorkspace::loadWarning, ws,
                     [&cap](const QString &t)
                     {
                         cap.warned = true;
                         cap.text = t;
                     });
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
    const QString tiff100 = QString::fromStdString(fixture("large_tiff_100mp.tiff"));
    const QString highCompression = QString::fromStdString(fixture("high_compression.jpg"));

    // ── C1: all-infeasible pair (duplicate paths) displays via source-backed LOD
    {
        MARK("C1 start");
        SourceDecodeStats::instance().counters().reset();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        WarningCapture cap;
        captureWarnings(&ws, cap);
        const double rssBefore = rssMB();
        ws.setImages({jpeg100, jpeg100});
        CHECK(waitForLoadedPanes(&ws, 2, {0, 1}),
              "C1: both infeasible panes exist and display (placeholder keeps the pane)");
        pump(500); // settle worker teardown before sampling
        const double rssDelta = rssMB() - rssBefore;
        printf("  C1: rss delta (whole compare display path) = %.1f MB\n", rssDelta);
        CHECK(rssDelta < 100.0,
              "C1: display path RSS stays bounded (< 100 MB; one full 100MP RGB "
              "materialization would be ~286 MB and is rejected by Qt anyway)");
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.nativeLod.load() >= 2, "C1: both panes decoded via NativeLod");
        CHECK(c.fullDecodeScaled.load() == 0 && c.fullDecodeCrop.load() == 0 &&
                  c.fullDecode.load() == 0,
              "C1: no full-frame decode was ever attempted for display");
        CHECK(c.boundedRegion.load() == 0, "C1: fit display uses LOD only, no region path");
        CHECK(!cap.warned, "C1: infeasible skips never surface as load failures");
        for (int idx : {0, 1})
        {
            RawImageView *v = paneView(&ws, idx);
            const QSize imgSize = v ? v->image().size() : QSize();
            const QSize srcSize = v ? v->sourceSize() : QSize();
            CHECK(!imgSize.isEmpty() && std::max(imgSize.width(), imgSize.height()) <= 4096,
                  "C1: pane raster is bounded (<= 4096 edge)");
            CHECK(imgSize.width() <= srcSize.width() && imgSize.height() <= srcSize.height(),
                  "C1: pane raster never exceeds source dimensions");
            CHECK(srcSize == QSize(12000, 8333),
                  "C1: pane sourceSize carries the full source geometry");
            if (v && v->size().isValid() && srcSize.isValid())
            {
                const double fit = std::min(static_cast<double>(v->width()) / srcSize.width(),
                                            static_cast<double>(v->height()) / srcSize.height());
                CHECK(std::abs(v->scale() - fit) <= std::max(1e-6, fit * 0.02),
                      "C1: source-backed pane starts at its metadata-based Fit scale");
                CHECK(v->sourceRect() == QRect(QPoint(0, 0), srcSize),
                      "C1: initial source-backed LOD covers the full source");
            }
        }
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "C1: scheduler drains after source-backed display");
    }

    // ── C2: mixed feasible + infeasible keeps per-pane alignment ─────────────
    {
        MARK("C2 start");
        SourceDecodeStats::instance().counters().reset();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        WarningCapture cap;
        captureWarnings(&ws, cap);
        ws.setImages({highCompression, jpeg100});
        CHECK(waitForLoadedPanes(&ws, 2, {0, 1}),
              "C2: feasible + infeasible pair both display");
        const ImageFrame *frame0 = ws.engine().imageAt(0);
        const ImageFrame *frame1 = ws.engine().imageAt(1);
        CHECK(frame0 && !frame0->pixels().isNull(),
              "C2: pane 0 (feasible) holds its full analysis frame");
        CHECK(frame0 && frame0->width() == 6000 && frame0->height() == 4000,
              "C2: pane 0 frame is full resolution");
        CHECK(frame1 && frame1->pixels().isNull(),
              "C2: pane 1 (infeasible) holds a metadata-only placeholder");
        RawImageView *v1 = paneView(&ws, 1);
        CHECK(v1 && v1->sourceSize() == QSize(12000, 8333),
              "C2: pane 1 sourceSize matches its REQUESTED path (index alignment)");
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.nativeLod.load() >= 1, "C2: the infeasible pane decoded via NativeLod");
        CHECK(c.fullDecodeScaled.load() == 0 && c.fullDecodeCrop.load() == 0,
              "C2: no full-decode fallback ran for either pane");
        CHECK(!cap.warned, "C2: no warning for a mixed feasible+infeasible set");
    }

    // ── C3: zoom re-materializes the source-backed pane at denser quality ───
    {
        MARK("C3 start");
        SourceDecodeStats::instance().counters().reset();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        ws.setImages({jpeg100, jpeg100});
        CHECK(waitForLoadedPanes(&ws, 2, {0, 1}), "C3: initial LOD display lands");
        RawImageView *v1 = paneView(&ws, 1);
        const QSize before = v1 ? v1->image().size() : QSize();
        const uint64_t nativeLodBefore =
            SourceDecodeStats::instance().counters().nativeLod.load();
        // Zoom the pane exactly like the UI: wheel events route through the
        // workspace event filter -> applyAnchorZoom -> scheduleDisplayLodRefresh,
        // which re-materializes the source-backed pane at a denser edge. Two
        // wheel steps (x1.15^2) so the denser target edge robustly exceeds the
        // initial raster (whose edge was computed from the pre-settle viewport).
        if (v1)
        {
            const QPointF center(v1->rect().center());
            for (int step = 0; step < 2; ++step)
            {
                QWheelEvent event(center, center, QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(v1, &event);
                pump(30);
            }
        }
        // The NativeLod counter increments INSIDE the worker; the raster only
        // lands when the queued UI delivery (applyDisplayBatchResult) runs —
        // so wait for the denser raster itself, not the counter.
        const int beforeMax = std::max(before.width(), before.height());
        CHECK(waitTrue(
                  [&]
                  {
                      RawImageView *v = paneView(&ws, 1);
                      const QSize s = v ? v->image().size() : QSize();
                      return !s.isEmpty() && std::max(s.width(), s.height()) > beforeMax;
                  },
                  60000),
              "C3: the zoomed LOD raster is denser (re-materialization lands)");
        const QSize after = v1 ? v1->image().size() : QSize();
        printf("  C3: pane1 raster %dx%d -> %dx%d\n", before.width(), before.height(),
               after.width(), after.height());
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(!after.isEmpty() && std::max(after.width(), after.height()) <= 4096,
              "C3: the denser raster stays bounded");
        CHECK(c.fullDecodeScaled.load() == 0 && c.fullDecodeCrop.load() == 0,
              "C3: re-materialization never falls back to a full decode");
        const QRect fullSource(QPoint(0, 0), v1 ? v1->sourceSize() : QSize());
        if (c.boundedRegion.load() > 0)
        {
            CHECK(v1 && v1->sourceRect().isValid() && v1->sourceRect() != fullSource,
                  "C3: deep-enough zoom records the covered source region");
        }
        else
        {
            CHECK(c.nativeLod.load() > nativeLodBefore,
                  "C3: moderate zoom re-materializes through NativeLod");
        }
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "C3: scheduler drains after the zoom upgrade");
    }

    // ── C4: destroy mid-request and destroy after display ───────────────────
    {
        MARK("C4 start");
        {
            CompareWorkspace ws;
            ws.resize(1280, 800);
            ws.show();
            ws.setImages({jpeg100, jpeg100});
            // Destroy immediately: the load probe / LOD materialization may
            // still be running.
        }
        pump(1500);
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "C4a: pools drain after destroy mid-request (no crash, no leak)");
        {
            CompareWorkspace ws;
            ws.resize(1280, 800);
            ws.show();
            ws.setImages({jpeg100, jpeg100});
            CHECK(waitForLoadedPanes(&ws, 2, {0, 1}), "C4b: LOD display lands");
        } // destroyed after display
        pump(1500);
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "C4b: pools drain after destroy post-display");
    }

    // ── C5: TIFF honest fallback (no native LOD) ─────────────────────────────
    {
        MARK("C5 start");
        SourceDecodeStats::instance().counters().reset();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        WarningCapture cap;
        captureWarnings(&ws, cap);
        ws.setImages({jpeg100, tiff100});
        CHECK(waitForLoadedPanes(&ws, 2, {0}), "C5: JPEG pane displays via LOD");
        // The TIFF pane has no native LOD; the bounded attempt is recorded and
        // rejected inside Qt's allocation limit — the pane stays blank, but
        // the batch and the workspace stay healthy.
        CHECK(waitTrue(
                  [&]
                  {
                      const auto &c = SourceDecodeStats::instance().counters();
                      return c.fullDecodeScaled.load() >= 1;
                  },
                  60000),
              "C5: the TIFF bounded attempt is recorded (FullDecodeScaled)");
        RawImageView *v1 = paneView(&ws, 1);
        pump(500);
        CHECK(v1 && v1->image().isNull(),
              "C5: the infeasible TIFF pane stays blank (honest fallback)");
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.failed.load() >= 1, "C5: the TIFF attempt failed cleanly (no crash/OOM)");
        CHECK(c.fullDecodeCrop.load() == 0, "C5: no full-decode crop fallback was attempted");
        CHECK(!cap.warned, "C5: a skipped source is not a load failure");
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "C5: scheduler drains after the honest fallback");
    }

    // ── C6: failure vs infeasible accounting ─────────────────────────────────
    {
        MARK("C6 start");
        SourceDecodeStats::instance().counters().reset();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        WarningCapture cap;
        captureWarnings(&ws, cap);
        const QString missing = QString::fromStdString(fixtureRoot()) + "/no_such_file.jpg";
        ws.setImages({jpeg100, missing, highCompression});
        // Engine keeps the placeholder (pane 0) and the loaded frame (pane 1 —
        // indices renumber after the missing file drops, B#7): count == 2.
        CHECK(waitForLoadedPanes(&ws, 2, {0, 1}),
              "C6: placeholder + loaded panes display; missing file drops");
        RawImageView *v0 = paneView(&ws, 0);
        RawImageView *v1 = paneView(&ws, 1);
        CHECK(v0 && v0->sourceSize() == QSize(12000, 8333),
              "C6: pane 0 keeps its infeasible source geometry");
        CHECK(v1 && v1->sourceSize() == QSize(6000, 4000),
              "C6: pane 1 (feasible) keeps its full-frame geometry");
        CHECK(cap.warned && cap.text.contains(QStringLiteral("1 张图片无法加载")),
              "C6: the warning counts ONLY the real failure (not the skip)");
        CHECK(waitTrue([&] { return sampleScheduler().pending + sampleScheduler().active == 0; },
                       15000),
              "C6: scheduler drains");
    }

    std::printf("=== M47 compare source-backed display tests: %s ===\n",
                g_failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
