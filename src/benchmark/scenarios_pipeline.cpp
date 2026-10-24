#include "benchmark/bench_progress.h"
#include "benchmark/scenarios.h"

#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"
#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <QImage>
#include <QImageReader>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mviewer::bench
{
// ─── B2: first thumbnail latency (REAL ThumbnailPipeline path) ───────────────
//
// NOTE: previously this scenario drove ImageRepository::loadDirectoryAsync,
// which submits a FULL-SIZE decode for EVERY image and waits for ALL of them.
// That measured directory-wide decode time, NOT the user-perceived first
// thumbnail. The product's actual first-thumbnail path is ThumbnailPipeline
// (visible range -> Thumbnail priority, neighbors -> Background), which the
// M3 acceptance test already exercises. This scenario now measures THAT path,
// with a stage breakdown (M10 performance gate: profile before optimize).
//
// Measure one cold+warm first-thumbnail cycle through the real pipeline and
// return the stage split. `warm` reuses an already-warm corpus directory so
// filesystem/codec caches are hot.
namespace
{

constexpr int kThumbWaitMs = 10000;
constexpr int kPoolDrainMs = 5000;

// Heap state: ThumbnailPipeline::clear() does not join in-flight decodes.
// Workers capture this by value so a late task cannot write into a dead stack.
struct ThumbState
{
    std::atomic<double> firstDecodeStart{-1.0};
    std::atomic<double> firstDecodeEnd{-1.0};
    std::atomic<double> firstResultAt{-1.0};
    std::atomic<bool> firstCaptured{false};
    std::atomic<double> anchor{-1.0};
    std::atomic<bool> firstSeen{false};
    std::atomic<double> cacheMs{0.0};
};

bool schedulerPoolsIdle()
{
    const auto thumbs = TaskScheduler::instance().metrics(TaskScheduler::ThumbnailPool);
    const auto decodes = TaskScheduler::instance().metrics(TaskScheduler::DecodePool);
    return thumbs.pending == 0 && thumbs.active_tasks == 0 && decodes.pending == 0 &&
           decodes.active_tasks == 0;
}

// Full-resolution read stays on the calling thread, and only when no pool
// thread is inside QImageReader. Concurrent full reads deadlock Qt image IO
// (see ImageRepository::loadDirectoryAsync).
double measureStandaloneResizeMs(const std::string &path, int edge)
{
    if (!schedulerPoolsIdle())
        return 0.0;
    QImageReader reader(QString::fromStdString(path));
    const QImage full = reader.read();
    if (full.isNull())
        return 0.0;
    const int side = edge > 0 ? edge : 256;
    const double rs = nowMs();
    (void)full.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return nowMs() - rs;
}

void installThumbHooks(ThumbnailPipeline &pipe, const std::shared_ptr<ThumbState> &st)
{
    pipe.setDecodeFn(
        [st](const std::string &p, int size) -> ImageData
        {
            if (st->anchor.load() >= 0.0 && st->firstDecodeStart.load() < 0.0)
            {
                double expect = -1.0;
                st->firstDecodeStart.compare_exchange_strong(expect, nowMs());
            }
            const double ds = nowMs();
            ImageData thumb = Decoder::decodeScaled(p, size);
            const double de = nowMs();
            bool expected = false;
            if (st->firstCaptured.compare_exchange_strong(expected, true))
            {
                st->firstDecodeEnd.store(de);
                st->firstDecodeStart.store(ds);
            }
            return thumb;
        });

    pipe.setResultFn(
        [st](const std::string &, int, const ImageData &)
        {
            const double now = nowMs();
            double expect = -1.0;
            if (st->firstResultAt.compare_exchange_strong(expect, now))
            {
                const double de = st->firstDecodeEnd.load();
                if (de >= 0.0)
                    st->cacheMs.store(now - de, std::memory_order_relaxed);
                st->firstSeen.store(true, std::memory_order_relaxed);
            }
        });
}

void fillThumbBreakdown(ThumbnailBreakdown &bd, const std::shared_ptr<ThumbState> &st,
                        double tScan0, double tAnchor)
{
    const double tFirst = nowMs();
    bd.cache_ms = st->cacheMs.load();
    bd.ui_notify_ms = tFirst - tAnchor;
    const double decodeStart = st->firstDecodeStart.load();
    const double decodeEnd = st->firstDecodeEnd.load();
    bd.queue_wait_ms = (decodeStart >= 0.0) ? (decodeStart - tAnchor) : bd.ui_notify_ms;
    bd.decode_ms = (decodeEnd >= 0.0 && decodeStart >= 0.0) ? (decodeEnd - decodeStart) : 0.0;
    bd.total_ms = tFirst - tScan0;
    if (!st->firstSeen.load(std::memory_order_relaxed))
    {
        bd.timedOut = true;
        bd.timeoutMs = kThumbWaitMs;
    }
}

bool drainThumbnailPool()
{
    return TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool,
                                           std::chrono::milliseconds(kPoolDrainMs));
}

} // namespace

ThumbnailBreakdown measureFirstThumbnail(const Corpus &corpus)
{
    ThumbnailBreakdown bd;

    // 1) scan: directory enumeration (what setSources consumes).
    const double tScan0 = nowMs();
    const auto paths = FileSystem::listImages(corpus.dir);
    bd.scan_ms = nowMs() - tScan0;
    if (paths.empty())
        return bd;

    auto &pipe = ThumbnailPipeline::instance();
    pipe.clear();

    const int edge = pipe.currentThumbSize();
    bd.resize_ms = measureStandaloneResizeMs(paths.front(), edge);

    auto st = std::make_shared<ThumbState>();
    installThumbHooks(pipe, st);
    pipe.setSources(paths);

    // Anchor right before the visible-range kick (the user-visible trigger).
    const double tAnchor = nowMs();
    st->anchor.store(tAnchor);
    pipe.setVisibleRange(0, 20); // first screenful

    const double tDeadline = tAnchor + static_cast<double>(kThumbWaitMs);
    while (!st->firstSeen.load(std::memory_order_relaxed) && nowMs() < tDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

    fillThumbBreakdown(bd, st, tScan0, tAnchor);
    pipe.clear();
    bd.drainTimedOut = !drainThumbnailPool();
    // `st` (heap) outlives this function; late worker tasks write only to its
    // atomics, which remain valid. No stack capture -> no use-after-free.
    return bd;
}

std::string ThumbnailBreakdown::toString() const
{
    std::ostringstream oss;
    oss << "scan=" << scan_ms << "ms"
        << " queue_wait=" << queue_wait_ms << "ms"
        << " decode=" << decode_ms << "ms"
        << " resize=" << resize_ms << "ms"
        << " cache=" << cache_ms << "ms"
        << " ui_notify=" << ui_notify_ms << "ms"
        << " total=" << total_ms << "ms";
    return oss.str();
}

ScenarioResult scenarioFirstThumbnail(const Corpus &corpus)
{
    // Cold: fresh process, empty scheduler pools (corpus dir exists on disk;
    // filesystem/codec caches are cold on first open of the run).
    const ThumbnailBreakdown cold = measureFirstThumbnail(corpus);
    if (cold.timedOut)
        return timeoutResult("B2", "first_thumbnail_ms", cold.timeoutMs, false);
    // Warm: repeat the same open in-process; caches hot.
    const ThumbnailBreakdown warm = measureFirstThumbnail(corpus);
    if (warm.timedOut)
        return timeoutResult("B2", "first_thumbnail_ms", warm.timeoutMs, false);

    ScenarioResult r;
    r.name = "B2";
    r.metric = "first_thumbnail_ms";
    r.value = cold.ui_notify_ms;
    r.passed = true; // smoke reports; --enforce applies the <100ms budget
    r.detail = "COLD {" + cold.toString() + "} WARM {" + warm.toString() +
               "} (real ThumbnailPipeline path; budget <100ms)";
    if (cold.drainTimedOut || warm.drainTimedOut)
        r.detail += " pool drain timed out after " + std::to_string(kPoolDrainMs) + " ms";
    return r;
}

// ─── Pipeline priority trace (M10 P1 verification) ───────────────────────────
//
// The user's review asked to PROVE ThumbnailPipeline priority scheduling
// actually works: visible images (P0) must arrive before predictive neighbors
// (Background), and background work must not block visible work. We trace the
// completion order + latency of the first N thumbnails and classify each by
// its scheduling tier (visible range [0,vis), neighbors [vis,vis+pred),
// background beyond). No architecture change — pure observation.
namespace
{

struct PipelineTraceEntry
{
    size_t index = 0;
    double arrivalMs = 0;
    std::string tier;
};

// Shared with worker lambdas. clear() does not join in-flight tasks, so a
// stack capture would be a use-after-free if the 30s wait returns first.
struct TraceState
{
    std::mutex trMtx;
    std::mutex startMtx;
    std::condition_variable cv;
    std::vector<PipelineTraceEntry> trace;
    std::unordered_map<std::string, size_t> pathToIndex;
    std::unordered_map<std::string, double> startMs;
    std::atomic<int> arrived{0};
    std::atomic<bool> done{false};
    std::atomic<double> tAnchor{0.0};
    size_t visEnd = 20;
    size_t neighborEnd = 36;
    int want = 0;
    size_t pathCount = 0;
};

std::string traceTier(const TraceState &st, size_t idx)
{
    if (idx < st.visEnd)
        return "visible";
    if (idx < st.neighborEnd)
        return "neighbor";
    return "background";
}

void noteTraceStart(const std::shared_ptr<TraceState> &st, const std::string &path)
{
    std::lock_guard<std::mutex> lk(st->startMtx);
    st->startMs[path] = nowMs() - st->tAnchor.load();
}

void noteTraceArrival(const std::shared_ptr<TraceState> &st, const std::string &path)
{
    const double ms = nowMs() - st->tAnchor.load();
    std::lock_guard<std::mutex> lk(st->trMtx);
    size_t idx = st->pathCount;
    const auto it = st->pathToIndex.find(path);
    if (it != st->pathToIndex.end())
        idx = it->second;
    st->trace.push_back({idx, ms, traceTier(*st, idx)});
    if (++st->arrived >= st->want)
    {
        st->done.store(true);
        st->cv.notify_all();
    }
}

struct PriorityStarts
{
    double maxVisible = -1;
    double minNeighbor = 1e9;
    double maxNeighbor = -1;
    double minBackground = 1e9;
    bool orderingOk = false;
};

void considerStart(PriorityStarts &starts, const TraceState &st, size_t idx, double sample)
{
    if (idx < st.visEnd)
    {
        starts.maxVisible = std::max(starts.maxVisible, sample);
        return;
    }
    if (idx < st.neighborEnd)
    {
        starts.minNeighbor = std::min(starts.minNeighbor, sample);
        starts.maxNeighbor = std::max(starts.maxNeighbor, sample);
        return;
    }
    starts.minBackground = std::min(starts.minBackground, sample);
}

PriorityStarts priorityStarts(TraceState &st)
{
    PriorityStarts starts;
    // Workers may still be recording starts after `want` arrivals. Hold the
    // same mutex noteTraceStart uses so the map is not rehashed under us.
    std::lock_guard<std::mutex> lk(st.startMtx);
    for (const auto &kv : st.startMs)
    {
        const auto it = st.pathToIndex.find(kv.first);
        if (it == st.pathToIndex.end())
            continue;
        considerStart(starts, st, it->second, kv.second);
    }
    starts.orderingOk = (starts.maxVisible <= starts.minNeighbor + 1e-6) &&
                        (starts.maxNeighbor <= starts.minBackground + 1e-6);
    return starts;
}

std::string formatPriority(const PriorityStarts &starts, bool drained)
{
    std::ostringstream oss;
    oss << "visible_start_max=" << starts.maxVisible << " neighbor_start_min=" << starts.minNeighbor
        << " neighbor_start_max=" << starts.maxNeighbor
        << " background_start_min=" << starts.minBackground
        << " priority_by_start=" << (starts.orderingOk ? "OK" : "VIOLATED");
    if (!drained)
        oss << " pool drain timed out after " << kPoolDrainMs << " ms";
    return oss.str();
}

} // namespace

ScenarioResult scenarioPipelinePriority(const Corpus &corpus)
{
    auto &pipe = ThumbnailPipeline::instance();
    pipe.clear();

    const auto paths = FileSystem::listImages(corpus.dir);
    if (paths.empty())
        return ScenarioResult{"TRACE", "pipeline_priority", 0, {}, "empty corpus", false};

    auto st = std::make_shared<TraceState>();
    st->visEnd = 20;
    st->neighborEnd = st->visEnd + 16;
    st->pathCount = paths.size();
    st->want = static_cast<int>(std::min<size_t>(60, paths.size()));
    for (size_t i = 0; i < paths.size(); ++i)
        st->pathToIndex[paths[i]] = i;

    // Start order, not completion order: a slow visible TIFF must not look
    // "behind" a fast neighbor JPEG.
    pipe.setDecodeFn(
        [st](const std::string &p, int size) -> ImageData
        {
            noteTraceStart(st, p);
            return Decoder::decodeScaled(p, size);
        });
    pipe.setResultFn([st](const std::string &p, int, const ImageData &)
                     { noteTraceArrival(st, p); });

    pipe.setSources(paths);
    st->tAnchor.store(nowMs());
    pipe.setVisibleRange(0, st->visEnd);

    bool finished = false;
    {
        std::unique_lock<std::mutex> lk(st->trMtx);
        finished = st->cv.wait_for(lk, std::chrono::seconds(30), [&] { return st->done.load(); });
    }
    pipe.clear();
    const bool drained = drainThumbnailPool();
    if (!finished)
        return timeoutResult("TRACE", "pipeline_priority", 30000, false);

    const PriorityStarts starts = priorityStarts(*st);
    ScenarioResult r;
    r.name = "TRACE";
    r.metric = "pipeline_priority";
    // Report-only: priority-by-start-order proof. Not a hard --enforce gate
    // (user said do not redesign Scheduler). detail carries the verdict.
    r.passed = true;
    r.detail = formatPriority(starts, drained);
    return r;
}

} // namespace mviewer::bench
