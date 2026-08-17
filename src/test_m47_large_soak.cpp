// M47 Phase 6 — deterministic large-image soak (race/cancel/supersede/destroy).
//
// Repeatedly drives the real 100 MP-class display paths — Viewer LOD-first
// display and Compare source-backed panes — through the M47 contracts:
//   - open -> displayReady with the full source dims and a bounded raster
//   - zoom churn (densifier requests) with NO full-decode fallback
//   - A -> B -> A supersession (only the final generation lands)
//   - destroy mid-request on alternating rounds
// Every round verifies scheduler drain and bounded RSS; the counters prove the
// display path never attempted a full materialization (fullDecode* == 0), and
// the final assertion verifies the scheduler dependency graph converged to
// zero (no leaked handles/deferred entries across the whole soak).

#include "compareworkspace.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSize>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

bool schedulerIdle()
{
    const SchedSample s = sampleScheduler();
    return s.pending == 0 && s.active == 0;
}

bool graphConverged()
{
    const auto g = TaskScheduler::instance().graphMetrics();
    return g.handles == 0 && g.deferred == 0 && g.dep_graph_entries == 0 &&
           g.dependents_entries == 0;
}

// ── Viewer slice: open -> ready -> zoom churn -> (optional supersede) ───────
// Returns the observed display raster dims at readiness.
QSize viewerOpenReady(ImageViewer &viewer, const QString &path, int timeoutMs = 60000)
{
    QSize readySize;
    bool ready = false;
    QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                     [&](const QSize &s)
                     {
                         readySize = s;
                         ready = true;
                     });
    viewer.setImage(path);
    waitTrue([&] { return ready; }, timeoutMs);
    return readySize;
}

// ── Compare slice: set -> both panes materialized ───────────────────────────
RawImageView *paneView(CompareWorkspace *ws, int index)
{
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (v && v->cellIndex() == index)
            return v;
    }
    return nullptr;
}

bool comparePanesReady(CompareWorkspace *ws, int expected)
{
    if (ws->comparedImageCount() != expected)
        return false;
    for (int i = 0; i < expected; ++i)
    {
        RawImageView *v = paneView(ws, i);
        if (!v || v->image().isNull())
            return false;
    }
    return true;
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
    constexpr int kRounds = 6;
    const double rssStart = rssMB();
    double rssPeak = rssStart;
    std::vector<double> rssRounds;

    for (int round = 0; round < kRounds; ++round)
    {
        printf("  soak round %d/%d\n", round + 1, kRounds);
        std::fflush(stdout);
        SourceDecodeStats::instance().counters().reset();

        // ── Viewer slice ────────────────────────────────────────────────────
        {
            ImageViewer viewer;
            viewer.resize(1280, 800);
            viewer.show();
            viewer.setBrowseSequence({jpeg100, highCompression});

            if (round % 2 == 0)
            {
                // Even rounds: open -> ready -> zoom churn -> A/B/A supersede.
                const QSize first = viewerOpenReady(viewer, jpeg100);
                CHECK(first == QSize(12000, 8333),
                      "soak: first 100MP displayReady carries the full source dims");
                CHECK(viewer.isLodDisplay(),
                      "soak: the 100MP viewer is in LOD display mode");
                // Zoom churn: each wheel-style zoom requests denser rasters.
                viewer.zoomIn();
                viewer.zoomIn();
                viewer.zoomOut();
                viewer.zoomFit();
                const QSize second = viewerOpenReady(viewer, highCompression);
                CHECK(second == QSize(6000, 4000),
                      "soak: B (24MP) supersedes A and lands with its dims");
                const QSize third = viewerOpenReady(viewer, jpeg100);
                CHECK(third == QSize(12000, 8333),
                      "soak: final A lands (A->B->A supersession)");
                pump(300);
                CHECK(viewer.isLodDisplay() || !viewer.frame(),
                      "soak: final state is the 100MP LOD display");
            }
            else
            {
                // Odd rounds: destroy mid-request (setImage then immediate
                // scope exit) — the raster worker may still be running.
                viewer.setImage(jpeg100);
            }
        } // viewer destroyed
        pump(500);

        // ── Compare slice (every round) ─────────────────────────────────────
        {
            CompareWorkspace ws;
            ws.resize(1280, 800);
            ws.show();
            ws.setImages({jpeg100, jpeg100});
            CHECK(waitTrue([&] { return comparePanesReady(&ws, 2); }, 60000),
                  "soak: both compare panes materialize via source-backed LOD");
            // Densifier churn through the real wheel path on pane 1.
            RawImageView *v1 = paneView(&ws, 1);
            if (v1)
            {
                const QPointF center(v1->rect().center());
                QWheelEvent event(center, center, QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(v1, &event);
                pump(500);
            }
        } // workspace destroyed
        pump(500);

        // ── Per-round verdicts ──────────────────────────────────────────────
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.fullDecodeScaled.load() == 0 && c.fullDecodeCrop.load() == 0 &&
                  c.fullDecode.load() == 0,
              "soak: no full-decode fallback ever ran during display churn");
        CHECK(c.nativeLod.load() >= 1, "soak: the display path used native LOD");
        CHECK(waitTrue(schedulerIdle, 20000), "soak: pools drain after the round");
        const double now = rssMB();
        rssPeak = std::max(rssPeak, now);
        rssRounds.push_back(now);
        printf("  round %d: rss=%.1f MB (start %.1f)\n", round + 1, now, rssStart);
        std::fflush(stdout);
    }

    // ── Final soak verdicts ─────────────────────────────────────────────────
    CHECK(waitTrue(schedulerIdle, 20000), "soak: scheduler fully idle after all rounds");
    CHECK(waitTrue(graphConverged, 20000),
          "soak: scheduler dependency graph converged to zero (no leak)");
    const double rssFinal = rssMB();
    printf("  soak rss: start=%.1f peak=%.1f final=%.1f MB\n", rssStart, rssPeak, rssFinal);
    CHECK(rssPeak - rssStart < 350.0,
          "soak: peak RSS stays bounded (< 350 MB growth across 6 rounds of "
          "100MP display churn; a single full materialization would be ~286 MB)");
    // Windows WorkingSet keeps the heap high-water mark until memory pressure,
    // so absolute convergence is not the right signal. Assert PLATEAU
    // STABILITY: once the caches warm (round 3), the per-round footprint must
    // not keep growing — a leak would show monotonic growth here.
    if (rssRounds.size() >= 6)
    {
        const double plateauEarly = rssRounds[2]; // round 3
        const double plateauMax = *std::max_element(rssRounds.begin() + 3, rssRounds.end());
        const double plateauDrift = plateauMax - plateauEarly;
        printf("  soak plateau drift (rounds 3..6 vs round 3): %.1f MB\n", plateauDrift);
        CHECK(plateauDrift < 50.0,
              "soak: the RSS plateau is stable (no per-round leak)");
        CHECK(rssRounds[5] - rssRounds[4] < 30.0,
              "soak: no RSS spike in the final round");
    }
    else
    {
        CHECK(false, "soak: expected per-round RSS samples");
    }

    std::printf("=== M47 large-image soak: %s ===\n", g_failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
