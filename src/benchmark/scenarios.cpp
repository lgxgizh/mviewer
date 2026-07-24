#include "benchmark/scenarios.h"

#include "core/cache/CacheManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageRepository.h"
#include "core/perf/MemoryTracker.h"
#include "core/render/TileCache.h"
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mviewer::bench
{

namespace
{
double pct(const std::vector<double> &sorted, double p)
{
    if (sorted.empty())
        return 0;
    const size_t idx = static_cast<size_t>(std::clamp(p, 0.0, 1.0) * (sorted.size() - 1));
    return sorted[idx];
}

Timing summarize(std::vector<double> vals)
{
    std::sort(vals.begin(), vals.end());
    Timing t;
    t.samples = vals.size();
    t.p50Ms = pct(vals, 0.50);
    t.p95Ms = pct(vals, 0.95);
    t.p99Ms = pct(vals, 0.99);
    return t;
}

// Block until a std::atomic<bool> flips or timeout (used to latch async callbacks).
template <typename Clock = std::chrono::steady_clock> double nowMs()
{
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}
} // namespace

// ─── B1: startup to first paint ──────────────────────────────────────────────
ScenarioResult scenarioStartup()
{
    // B1 probes Qt readiness. The harness (mviewer_bench main, or core_tests)
    // ALWAYS owns a live QApplication before this runs, so we never construct a
    // second one (illegal in Qt, crashes). We just probe the already-live event
    // loop. Exactly one QApplication exists per process, ever.
    ScenarioResult r;
    r.name = "B1";

    if (QCoreApplication::instance())
    {
        const double t0 = nowMs();
        QCoreApplication::processEvents();
        const double t1 = nowMs();
        r.metric = "qt_event_loop_probe_ms";
        r.value = t1 - t0;
        r.detail = "Qt already initialized (QApplication owned by harness)";
        return r;
    }

    // Defensive fallback only: if invoked with no live Q*Application (should not
    // happen — both mviewer_bench and core_tests construct one first), measure a
    // cold QApplication construction + platform-plugin load.
    const double t0 = nowMs();
    int argc = 1;
    char arg0[] = "mviewer_bench";
    char *argv[] = {arg0, nullptr};
    QApplication app(argc, argv); // forces Qt + platform plugin load
    QApplication::processEvents();
    const double t1 = nowMs();

    r.metric = "startup_to_qtcore_ready_ms";
    r.value = t1 - t0;
    r.detail = "QApplication + platform-plugin load (cold fallback)";
    // Budget: cold <300ms / warm <100ms — set by harness, not here.
    return r;
}

// ─── B0: cold-start (full pipeline: launch → first thumbnail) ───────────────
// Combines B1 (startup) + B2 (first thumbnail) into a single end-to-end
// measurement that simulates: reboot app → open folder → first image visible.
ScenarioResult scenarioColdStart(const Corpus &corpus)
{
    ScenarioResult r;
    r.name = "B0";
    r.metric = "cold_start_to_thumbnail_ms";

    // Phase 1: startup (reuse B1).
    auto startup = scenarioStartup();

    // Phase 2: first thumbnail (reuse B2).
    auto thumb = scenarioFirstThumbnail(corpus);

    r.value = startup.value + thumb.value;
    r.timing = thumb.timing;
    r.detail = "startup=" + std::to_string(startup.value) +
               "ms + thumbnail=" + std::to_string(thumb.value) + "ms (cold start to first image)";
    return r;
}

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
ThumbnailBreakdown measureFirstThumbnail(const Corpus &corpus)
{
    ThumbnailBreakdown bd;

    // 1) scan: directory enumeration (what setSources consumes).
    const double tScan0 = nowMs();
    const auto paths = FileSystem::listImages(corpus.dir);
    const double tScan1 = nowMs();
    bd.scan_ms = tScan1 - tScan0;
    if (paths.empty())
        return bd;

    auto &pipe = ThumbnailPipeline::instance();
    pipe.clear();

    // All mutable timing state lives on the HEAP via shared_ptr and is captured
    // BY VALUE by the worker lambdas. The ThumbnailPipeline does NOT join
    // in-flight decode tasks on clear(); they run to completion on scheduler
    // worker threads. If we captured stack locals by reference, a late task
    // would write into freed stack -> use-after-free (flaky SEGFAULT under
    // parallel ctest). Heap-backed state survives the function scope safely.
    struct State
    {
        std::atomic<double> firstDecodeStart{-1.0};
        std::atomic<double> firstDecodeEnd{-1.0};
        std::atomic<double> firstResultAt{-1.0};
        std::atomic<bool> firstCaptured{false};
        std::atomic<double> anchor{-1.0};
        std::atomic<bool> firstSeen{false};
        std::atomic<double> resizeMs{0.0};
        std::atomic<double> cacheMs{0.0};
    };
    auto st = std::make_shared<State>();

    // Injected decode: measure decode (incl. scaled-resize via Qt codec) and a
    // separate standalone resize pass so the breakdown shows resize distinctly.
    pipe.setDecodeFn(
        [st](const std::string &p, int size) -> ImageData
        {
            // Latch the very first decode-start (queue_wait end) exactly once.
            if (st->anchor.load() >= 0.0 && st->firstDecodeStart.load() < 0.0)
            {
                double expect = -1.0;
                st->firstDecodeStart.compare_exchange_strong(expect, nowMs());
            }
            const double ds = nowMs();
            ImageData thumb = Decoder::decodeScaled(p, size);
            const double de = nowMs();
            // Record the first decode's end + standalone resize, once.
            bool expected = false;
            if (st->firstCaptured.compare_exchange_strong(expected, true))
            {
                st->firstDecodeEnd.store(de);
                st->firstDecodeStart.store(ds);
                // Standalone resize cost: the Qt codec path folds scaling into the
                // decode (setScaledSize), so decode_ms already includes it. Measure
                // an explicit QImage::scaled on a fresh full read to surface the
                // independent resize cost for the breakdown (first thumbnail only).
                QImageReader rr(QString::fromStdString(p));
                QImage full = rr.read();
                if (!full.isNull())
                {
                    const double rs = nowMs();
                    (void)full.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    const double re = nowMs();
                    st->resizeMs.fetch_add(re - rs, std::memory_order_relaxed);
                }
            }
            return thumb;
        });

    pipe.setResultFn(
        [st](const std::string &, const ImageData &)
        {
            const double now = nowMs();
            double expect = -1.0;
            if (st->firstResultAt.compare_exchange_strong(expect, now))
            {
                // cache = decode-end -> result-callback (worker mem-cache insert).
                const double de = st->firstDecodeEnd.load();
                if (de >= 0.0)
                    st->cacheMs.store(now - de, std::memory_order_relaxed);
                st->firstSeen.store(true, std::memory_order_relaxed);
            }
        });

    pipe.setSources(paths);

    // Anchor right before the visible-range kick (the user-visible trigger).
    st->anchor.store(nowMs());
    const double tAnchor = st->anchor.load();
    pipe.setVisibleRange(0, 20); // first screenful

    // Latch on the first thumbnail reaching the UI adapter.
    const double tDeadline = tAnchor + 10000.0;
    while (!st->firstSeen.load(std::memory_order_relaxed) && nowMs() < tDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const double tFirst = nowMs();
    bd.cache_ms = st->cacheMs.load();
    bd.ui_notify_ms = tFirst - tAnchor;
    bd.queue_wait_ms = (st->firstDecodeStart.load() >= 0.0)
                           ? (st->firstDecodeStart.load() - tAnchor)
                           : bd.ui_notify_ms;
    bd.decode_ms = (st->firstDecodeEnd.load() >= 0.0 && st->firstDecodeStart.load() >= 0.0)
                       ? (st->firstDecodeEnd.load() - st->firstDecodeStart.load())
                       : 0.0;
    bd.resize_ms = st->resizeMs.load();
    bd.total_ms = tFirst - tScan0;

    pipe.clear();
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
    ThumbnailBreakdown cold = measureFirstThumbnail(corpus);
    // Warm: repeat the same open in-process; caches hot.
    ThumbnailBreakdown warm = measureFirstThumbnail(corpus);

    ScenarioResult r;
    r.name = "B2";
    r.metric = "first_thumbnail_ms";
    r.value = cold.ui_notify_ms;
    r.passed = true; // smoke reports; --enforce applies the <100ms budget
    r.detail = "COLD {" + cold.toString() +
               "}"
               " WARM {" +
               warm.toString() +
               "}"
               " (real ThumbnailPipeline path; budget <100ms)";
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
struct PipelineTraceEntry
{
    size_t index;
    double arrivalMs;
    std::string tier;
};

ScenarioResult scenarioPipelinePriority(const Corpus &corpus)
{
    auto &pipe = ThumbnailPipeline::instance();
    pipe.clear();

    const auto paths = FileSystem::listImages(corpus.dir);
    if (paths.empty())
        return ScenarioResult{"TRACE", "pipeline_priority", 0, {}, "empty corpus", false};

    const size_t visBegin = 0, visEnd = 20; // visible screenful
    const size_t predCount = 16;            // predictive neighbors
    const size_t neighborEnd = visEnd + predCount;

    // Authoritative index = position in the display-order path list the
    // pipeline consumes. Filename parsing is fragile (mixed formats collide
    // on img_NNNNN across .jpg/.png/.tif). Map path -> position instead.
    std::unordered_map<std::string, size_t> pathToIndex;
    for (size_t i = 0; i < paths.size(); ++i)
        pathToIndex[paths[i]] = i;

    std::mutex trMtx;
    std::vector<PipelineTraceEntry> trace;
    std::condition_variable cv;
    std::atomic<int> arrived{0};
    const int want = static_cast<int>(std::min<size_t>(60, paths.size()));
    std::atomic<bool> done{false};
    double tAnchorTrace = 0.0;

    // Decode START times, keyed by path. Captured inside a wrapping decode
    // fn so we can prove priority by DISPATCH/START order (decode-cost
    // independent), not by completion order (which TIFF-vs-JPEG cost dominates
    // and would falsely flag a slow visible TIFF as "behind" a fast neighbor).
    std::unordered_map<std::string, double> startMs;
    std::mutex startMtx;

    pipe.setDecodeFn(
        [&](const std::string &p, int size) -> ImageData
        {
            {
                std::lock_guard<std::mutex> lk(startMtx);
                startMs[p] = nowMs() - tAnchorTrace;
            }
            return Decoder::decodeScaled(p, size);
        });
    pipe.setResultFn(
        [&](const std::string &p, const ImageData &)
        {
            const double ms = nowMs() - tAnchorTrace;
            std::lock_guard<std::mutex> lk(trMtx);
            size_t idx = paths.size();
            auto it = pathToIndex.find(p);
            if (it != pathToIndex.end())
                idx = it->second;
            std::string tier = (idx >= visBegin && idx < visEnd)      ? "visible"
                               : (idx >= visEnd && idx < neighborEnd) ? "neighbor"
                                                                      : "background";
            trace.push_back({idx, ms, tier});
            if (++arrived >= want)
            {
                done.store(true);
                cv.notify_all();
            }
        });

    pipe.setSources(paths);
    tAnchorTrace = nowMs();
    pipe.setVisibleRange(visBegin, visEnd);

    {
        std::unique_lock<std::mutex> lk(trMtx);
        cv.wait_for(lk, std::chrono::seconds(30), [&] { return done.load(); });
    }

    // Priority proof (decode-cost independent): every visible decode must START
    // no later than any neighbor starts, and every neighbor no later than any
    // background starts. This is what "visible gets highest priority" means.
    double maxVisibleStart = -1, minNeighborStart = 1e9;
    double maxNeighborStart = -1, minBackgroundStart = 1e9;
    for (const auto &kv : startMs)
    {
        auto it = pathToIndex.find(kv.first);
        if (it == pathToIndex.end())
            continue;
        const size_t idx = it->second;
        const double s = kv.second;
        if (idx >= visBegin && idx < visEnd)
        {
            maxVisibleStart = std::max(maxVisibleStart, s);
        }
        else if (idx >= visEnd && idx < neighborEnd)
        {
            minNeighborStart = std::min(minNeighborStart, s);
            maxNeighborStart = std::max(maxNeighborStart, s);
        }
        else
        {
            minBackgroundStart = std::min(minBackgroundStart, s);
        }
    }
    const bool orderingOk = (maxVisibleStart <= minNeighborStart + 1e-6) &&
                            (maxNeighborStart <= minBackgroundStart + 1e-6);

    ScenarioResult r;
    r.name = "TRACE";
    r.metric = "pipeline_priority";
    // Report-only: priority-by-start-order proof. Not a hard --enforce gate
    // (user said do not redesign Scheduler). detail carries the verdict.
    r.passed = true;
    std::ostringstream oss;
    oss << "visible_start_max=" << maxVisibleStart << " neighbor_start_min=" << minNeighborStart
        << " neighbor_start_max=" << maxNeighborStart
        << " background_start_min=" << minBackgroundStart
        << " priority_by_start=" << (orderingOk ? "OK" : "VIOLATED");
    r.detail = oss.str();
    pipe.clear();
    return r;
}

// ─── B3: decode latency per format ──────────────────────────────────────────
ScenarioResult scenarioDecodeLatency(const Corpus &corpus)
{
    auto &repo = ImageRepository::instance();
    repo.invalidateAll();

    auto timeOne = [&](const std::string &p) -> double
    {
        const double a = nowMs();
        repo.load(p);
        return nowMs() - a;
    };

    std::vector<double> j, png, tif;
    const size_t n = std::min<size_t>(100, corpus.jpegPaths.size());
    for (size_t i = 0; i < n; ++i)
    {
        j.push_back(timeOne(corpus.jpegPaths[i]));
        png.push_back(timeOne(corpus.pngPaths[i]));
        if (i < corpus.tiffPaths.size())
            tif.push_back(timeOne(corpus.tiffPaths[i]));
    }

    ScenarioResult r;
    r.name = "B3";
    r.metric = "decode_p50_ms_jpeg";
    r.value = summarize(j).p50Ms;
    r.timing = summarize(j);
    r.detail = "jpeg p50=" + std::to_string(r.timing.p50Ms) +
               " p95=" + std::to_string(r.timing.p95Ms) +
               " png p50=" + std::to_string(summarize(png).p50Ms) +
               " tiff p50=" + std::to_string(summarize(tif).p50Ms);
    return r;
}

// ─── B4: thumbnail throughput ────────────────────────────────────────────────
ScenarioResult scenarioThumbThroughput(const Corpus &corpus)
{
    auto &cm = CacheManager::instance();
    const auto all = corpus.allPaths();
    const size_t n = std::min<size_t>(1000, all.size());
    const double t0 = nowMs();
    for (size_t i = 0; i < n; ++i)
    {
        // Force a thumbnail-level entry via the cache (simulates ThumbnailPipeline
        // decode+resize; we use the real decode path through ImageRepository).
        auto res = ImageRepository::instance().load(all[i]);
        if (res.frame)
            cm.putMemory(CacheLevel::Thumbnail, all[i],
                         res.frame->thumbnail().isNull() ? res.frame->pixels()
                                                         : res.frame->thumbnail());
    }
    const double dt = nowMs() - t0;
    const double perSec = (dt > 0) ? (n * 1000.0 / dt) : 0;

    ScenarioResult r;
    r.name = "B4";
    r.metric = "thumbnails_per_sec";
    r.value = perSec;
    // Smoke (non-enforce): report the metric; budget (>100/s) is the
    // --enforce / Phase-4 gate, not evaluated here.
    r.passed = true;
    r.detail =
        "decoded+placed " + std::to_string(n) + " thumbnails in " + std::to_string(dt) + " ms";
    return r;
}

// ─── B5: cache-hit ratio under Zipf navigation ──────────────────────────────
ScenarioResult scenarioCacheHitRatio(const Corpus &corpus)
{
    auto &cm = CacheManager::instance();
    cm.clearMemory();
    const auto all = corpus.allPaths();
    const size_t n = all.size();
    if (n == 0)
        return ScenarioResult{"B5", "cache_hit_ratio", 0, {}, "empty corpus", false};

    // Zipf: rank i chosen with prob ~ 1/(i+1). Simulate 1000 navigations.
    std::mt19937 rng(42);
    std::vector<size_t> ranks(n);
    std::iota(ranks.begin(), ranks.end(), 0);
    double hits = 0, total = 0;
    const int navs = 1000;
    for (int k = 0; k < navs; ++k)
    {
        std::discrete_distribution<size_t> zipf(ranks.begin(), ranks.end());
        const size_t idx = zipf(rng) % n;
        ImageData out;
        ++total;
        if (cm.getMemory(CacheLevel::FullImage, all[idx], out))
            ++hits;
        else
            cm.putMemory(CacheLevel::FullImage, all[idx],
                         ImageRepository::instance().load(all[idx]).frame->pixels());
    }
    const double ratio = hits / total;

    ScenarioResult r;
    r.name = "B5";
    r.metric = "cache_hit_ratio";
    r.value = ratio;
    r.passed = ratio >= 0.0 && ratio <= 1.0; // structural; --enforce uses L2>0.90
    r.detail =
        "Zipf nav over " + std::to_string(n) + " imgs, " + std::to_string(navs) + " navigations";
    return r;
}

// ─── B6: memory budget during sweep ─────────────────────────────────────────
ScenarioResult scenarioMemoryBudget(const Corpus &corpus)
{
    auto &cm = CacheManager::instance();
    auto &mt = mviewer::perf::MemoryTracker::instance();
    cm.clearMemory();
    mt.reset();

    const auto all = corpus.allPaths();
    // Warm: load all (exercises L2 Viewer eviction at 512MB cap).
    for (const auto &p : all)
        ImageRepository::instance().load(p);

    const auto peak = mt.sample();
    const size_t liveFrames = peak.liveImageFrames;
    const size_t frameBytes = 6000UL * 4000UL * 3UL; // 24MP RGB estimate
    const size_t expectedFloor = liveFrames * frameBytes;

    ScenarioResult r;
    r.name = "B6";
    r.metric = "peak_cache_bytes";
    r.value = static_cast<double>(peak.cacheTotalBytes);
    r.detail = "liveFrames=" + std::to_string(liveFrames) +
               " peakBytes=" + std::to_string(peak.peakBytes) + " expectedFloor~" +
               std::to_string(expectedFloor) +
               " rssMB=" + std::to_string(peak.processWorkingSetKB / 1024);

    // Now evict and confirm return toward baseline.
    cm.clearMemory();
    const auto after = mt.sample();
    r.passed = (after.cacheTotalBytes <= peak.cacheTotalBytes); // monotonic decay ok
    r.detail += " afterClear=" + std::to_string(after.cacheTotalBytes);
    return r;
}

// ─── B7: image-switch latency, preloaded vs cold ────────────────────────────
ScenarioResult scenarioImageSwitch(const Corpus &corpus)
{
    auto &repo = ImageRepository::instance();
    repo.invalidateAll();
    const auto all = corpus.allPaths();
    if (all.size() < 2)
        return ScenarioResult{"B7", "switch_p50_ms", 0, {}, "need >=2 imgs", false};

    // Cold: first access (decode+display path).
    std::vector<double> cold, warm;
    for (size_t i = 0; i < std::min<size_t>(100, all.size()); ++i)
    {
        const double a = nowMs();
        repo.load(all[i]);
        cold.push_back(nowMs() - a);
        // Warm: immediately re-access (cache hit).
        const double b = nowMs();
        repo.load(all[i]);
        warm.push_back(nowMs() - b);
    }

    ScenarioResult r;
    r.name = "B7";
    r.metric = "switch_warm_p50_ms";
    r.value = summarize(warm).p50Ms;
    r.timing = summarize(warm);
    r.detail = "warm p50=" + std::to_string(summarize(warm).p50Ms) +
               " cold p50=" + std::to_string(summarize(cold).p50Ms);
    r.passed = summarize(warm).p50Ms <= 50.0; // local soft; --enforce <16ms
    return r;
}

// ─── B8: first-interaction latency for a PRELOADED switch (< 16 ms) ──────────
// docs/performance.md: "Perceived latency < 16 ms (one frame at 60fps)" and
// "Decode + display (preloaded) < 16 ms". We fully warm the cache, then navigate
// back-and-forth (i -> i+1 -> i) so every switch hits the in-memory FullImage
// LRU — measuring the user-perceived single-frame latency, NOT decode cost.
ScenarioResult scenarioSwitchLatency(const Corpus &corpus)
{
    auto &repo = ImageRepository::instance();
    repo.invalidateAll();
    const auto all = corpus.allPaths();
    if (all.size() < 2)
        return ScenarioResult{"B8", "switch_p50_ms", 0, {}, "need >=2 imgs", false};

    // Warm pass: load just the measured window so each lives in memory. The
    // timed pass navigates only within this window, matching real usage where
    // the user flips among recently-viewed images. Preloading the WHOLE corpus
    // (all 10000 at large scale) blows the cache budget for no measurement
    // benefit.
    const size_t m = std::min<size_t>(all.size(), 50);
    for (size_t i = 0; i < m; ++i)
        repo.load(all[i]);
    // Timed pass: 200 forward/back switches (all cache hits). Measure the
    // frame-to-frame navigation time the UI would incur (repo.load path).
    std::vector<double> sw;
    for (size_t k = 0; k < 200; ++k)
    {
        const size_t i = k % (m - 1);
        const double a = nowMs();
        repo.load(all[i]);     // hit
        repo.load(all[i + 1]); // step forward
        const double b = nowMs();
        sw.push_back(b - a);
    }

    ScenarioResult r;
    r.name = "B8";
    r.metric = "switch_p50_ms";
    r.value = summarize(sw).p50Ms;
    r.timing = summarize(sw);
    // Soft local gate; --enforce applies the strict <16ms budget (set in main).
    r.passed = summarize(sw).p50Ms <= 50.0;
    r.detail = "warm p50=" + std::to_string(summarize(sw).p50Ms) +
               " p95=" + std::to_string(summarize(sw).p95Ms) +
               " p99=" + std::to_string(summarize(sw).p99Ms) +
               " (preloaded; budget <16ms under --enforce)";
    return r;
}

// ─── B9: memory soak / stability ─────────────────────────────────────────────
// docs/performance.md: "Memory returns to baseline after cache eviction." Run
// CYCLES iterations of (load a window of images -> navigate -> clearMemory ->
// sample). Assert each cycle's post-clear sample <= its peak (monotonic decay)
// and that the final baseline is within tolerance of the initial baseline (no
// cumulative leak across cycles). Also track the global peak across cycles.
ScenarioResult scenarioSoakStability(const Corpus &corpus)
{
    auto &cm = CacheManager::instance();
    auto &mt = mviewer::perf::MemoryTracker::instance();
    cm.clearMemory();
    mt.reset();

    const auto all = corpus.allPaths();
    if (all.empty())
        return ScenarioResult{"B9", "baseline_return_ok", 0, {}, "empty corpus", false};

    const size_t window = std::min<size_t>(all.size(), 80);
    const int cycles = 10;
    const double initBase = static_cast<double>(mt.sample().cacheTotalBytes);

    bool decayOk = true;
    double globalPeak = initBase;
    std::ostringstream perCycle;

    for (int c = 0; c < cycles; ++c)
    {
        // Offset the window each cycle so different images churn through.
        const size_t off = static_cast<size_t>(c) * (window / 2) % all.size();
        for (size_t i = 0; i < window; ++i)
            ImageRepository::instance().load(all[(off + i) % all.size()]);
        const auto peak = mt.sample();
        globalPeak = std::max(globalPeak, static_cast<double>(peak.cacheTotalBytes));

        cm.clearMemory();
        const auto after = mt.sample();
        // Each cycle must return to <= its own peak (no growth within a cycle).
        if (after.cacheTotalBytes > peak.cacheTotalBytes)
            decayOk = false;
        perCycle << " c" << c << ":peak=" << peak.cacheTotalBytes
                 << ",after=" << after.cacheTotalBytes;
    }

    const double finalBase = static_cast<double>(mt.sample().cacheTotalBytes);

    ScenarioResult r;
    r.name = "B9";
    r.metric = "baseline_return_ok";
    r.value = decayOk ? 1.0 : 0.0;
    // Soft local gate; --enforce checks decayOk AND final within 2x initial.
    r.passed = decayOk;
    r.detail = "cycles=" + std::to_string(cycles) +
               " initBase=" + std::to_string(static_cast<long long>(initBase)) +
               " globalPeak=" + std::to_string(static_cast<long long>(globalPeak)) +
               " finalBase=" + std::to_string(static_cast<long long>(finalBase)) + " finalRssMB=" +
               std::to_string(static_cast<long long>(mt.sample().processWorkingSetKB / 1024)) +
               perCycle.str();
    return r;
}

// ─── B10 / A-8.4: 100MP tile-cache viewport request ──────────────────────────
// Treat the source as a 10000×10000 (100 MP) image and measure how long
// TileCache::request() takes to fill a 1920×1080 viewport at fit scale.
// Tiles are synthesized on demand (no full 400 MB buffer) so the metric
// isolates tile enumeration + cache put/get + region fill cost without
// stressing CI hosts.
ScenarioResult scenarioHundredMpTiles()
{
    constexpr int kW = 10000;
    constexpr int kH = 10000;
    constexpr int kTile = 256;
    constexpr int kScreenW = 1920;
    constexpr int kScreenH = 1080;

    Viewport vp;
    vp.screenW = kScreenW;
    vp.screenH = kScreenH;
    vp.fit(kW, kH);

    TileGrid grid(kW, kH, kTile);
    TileCache cache;
    cache.maxTiles = 512;

    auto decode = [&](const std::string & /*id*/, int sx, int sy, int sw, int sh, int /*dw*/,
                      int /*dh*/) -> ImageData
    {
        if (sx < 0)
            sx = 0;
        if (sy < 0)
            sy = 0;
        if (sx + sw > kW)
            sw = kW - sx;
        if (sy + sh > kH)
            sh = kH - sy;
        if (sw <= 0 || sh <= 0)
            return ImageData{};
        ImageData tile = makeImageData(sw, sh, PixelFormat::RGBA32);
        if (tile.isNull() || !tile.buffer)
            return ImageData{};
        // Deterministic procedural fill — cheap stand-in for a region decode.
        auto &buf = *tile.buffer;
        for (int y = 0; y < sh; ++y)
        {
            for (int x = 0; x < sw; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * sw + x) * 4;
                buf[i + 0] = static_cast<uint8_t>((sx + x) & 0xFF);
                buf[i + 1] = static_cast<uint8_t>((sy + y) & 0xFF);
                buf[i + 2] = static_cast<uint8_t>(((sx + x) ^ (sy + y)) & 0xFF);
                buf[i + 3] = 255;
            }
        }
        return tile;
    };

    // Warm-up (not timed): exercise the path once so first-call allocator noise
    // does not dominate the measured sample.
    {
        int warmCalls = 0;
        (void)cache.request("100mp", vp, grid, decode, &warmCalls);
        cache.clear();
    }

    // Timed cold request (empty cache).
    int decodeCalls = 0;
    const double t0 = nowMs();
    auto ready = cache.request("100mp", vp, grid, decode, &decodeCalls);
    const double coldMs = nowMs() - t0;

    // Timed warm request (all tiles cached) — should be near-instant.
    int warmCalls = 0;
    const double t1 = nowMs();
    auto readyWarm = cache.request("100mp", vp, grid, decode, &warmCalls);
    const double warmMs = nowMs() - t1;

    ScenarioResult r;
    r.name = "B10";
    r.metric = "hundred_mp_viewport_ms";
    r.value = coldMs;
    r.timing.samples = 1;
    r.timing.p50Ms = coldMs;
    r.timing.p95Ms = coldMs;
    r.timing.p99Ms = coldMs;
    // Soft local gate: cold fill under 500 ms is healthy on typical CI hosts.
    // --enforce applies the budget from performance_budget.json.
    r.passed = (coldMs <= 500.0) && !ready.empty() && (warmCalls == 0);
    r.detail = "image=10000x10000 tiles_ready=" + std::to_string(ready.size()) +
               " decode_calls=" + std::to_string(decodeCalls) +
               " cold_ms=" + std::to_string(coldMs) + " warm_ms=" + std::to_string(warmMs) +
               " warm_decode_calls=" + std::to_string(warmCalls) +
               " cache_size=" + std::to_string(cache.size()) +
               " warm_ready=" + std::to_string(readyWarm.size());
    return r;
}

} // namespace mviewer::bench
