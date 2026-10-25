#include "benchmark/scenarios.h"
#include "benchmark/bench_progress.h"

#include "core/cache/CacheManager.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageRepository.h"
#include "core/perf/GpuTracker.h"
#include "core/perf/MemoryTracker.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

namespace mviewer::bench
{
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
        // Above the 50 ms budget, so a queue that never goes idle still fails
        // the gate. An idle queue returns immediately. Far below the scenario
        // watchdog, so this probe cannot burn the ctest job.
        constexpr int kEventProbeCapMs = 2000;
        const double t0 = nowMs();
        QCoreApplication::processEvents(QEventLoop::AllEvents, kEventProbeCapMs);
        const double elapsed = nowMs() - t0;
        if (elapsed >= static_cast<double>(kEventProbeCapMs))
            return timeoutResult("B1", "qt_event_loop_probe_ms", kEventProbeCapMs, false);
        r.metric = "qt_event_loop_probe_ms";
        r.value = elapsed;
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
    QApplication::processEvents(QEventLoop::AllEvents, 2000);
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
    if (thumb.detail.rfind("timed out after ", 0) == 0)
    {
        r.value = thumb.value;
        r.passed = false;
        r.timing = thumb.timing;
        r.detail = thumb.detail;
        return r;
    }

    r.value = startup.value + thumb.value;
    r.timing = thumb.timing;
    r.detail = "startup=" + std::to_string(startup.value) +
               "ms + thumbnail=" + std::to_string(thumb.value) + "ms (cold start to first image)";
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
    ScenarioResult r;
    r.name = "B3";
    r.metric = "decode_p50_ms_jpeg";
    const size_t n = std::min<size_t>(100, corpus.jpegPaths.size());
    for (size_t i = 0; i < n; ++i)
    {
        if (failIfScenarioTimedOut(r, false))
            return r;
        j.push_back(timeOne(corpus.jpegPaths[i]));
        if (i < corpus.pngPaths.size())
            png.push_back(timeOne(corpus.pngPaths[i]));
        if (i < corpus.tiffPaths.size())
            tif.push_back(timeOne(corpus.tiffPaths[i]));
    }

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
    ScenarioResult early;
    early.name = "B4";
    early.metric = "thumbnails_per_sec";
    const double t0 = nowMs();
    for (size_t i = 0; i < n; ++i)
    {
        if (failIfScenarioTimedOut(early, true))
            return early;
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
    ScenarioResult early;
    early.name = "B5";
    early.metric = "cache_hit_ratio";
    for (int k = 0; k < navs; ++k)
    {
        if (failIfScenarioTimedOut(early, true))
            return early;
        std::discrete_distribution<size_t> zipf(ranks.begin(), ranks.end());
        const size_t idx = zipf(rng) % n;
        ImageData out;
        ++total;
        if (cm.getMemory(CacheLevel::FullImage, all[idx], out))
        {
            ++hits;
            continue;
        }
        const auto loaded = ImageRepository::instance().load(all[idx]);
        if (loaded.frame)
            cm.putMemory(CacheLevel::FullImage, all[idx], loaded.frame->pixels());
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
    ScenarioResult early;
    early.name = "B6";
    early.metric = "peak_cache_bytes";
    // Memory-tier sweep only: B6 reports peak_cache_bytes from CacheManager.
    // Default load() WAL-persists each decoded frame into DiskCache and that
    // I/O dominates the 120 s per-scenario watchdog.
    ImageRepository::LoadOptions memOnly;
    memOnly.useDiskCache = false;
    size_t failedLoads = 0;
    // Warm: load all (Viewer 512MB + Preview mip 256MB; B6 budget ≥ sum).
    for (const auto &p : all)
    {
        if (failIfScenarioTimedOut(early, false))
            return early;
        if (!ImageRepository::instance().load(p, memOnly).success())
            ++failedLoads;
    }

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
    if (failedLoads > 0)
        r.detail += " failedLoads=" + std::to_string(failedLoads);

    // Now evict and confirm return toward baseline.
    cm.clearMemory();
    const auto after = mt.sample();
    r.passed = (after.cacheTotalBytes <= peak.cacheTotalBytes); // monotonic decay ok
    r.detail += " afterClear=" + std::to_string(after.cacheTotalBytes);
    return r;
}

// ─── B7: image-switch latency, preloaded vs cold (repository-level) ─────────
// Times ImageRepository::load() only. End-to-end UI next/previous (ImageViewer
// present + paint, overlay/preview/metadata) is B18/B19 in scenarios_e2e.cpp.
ScenarioResult scenarioImageSwitch(const Corpus &corpus)
{
    auto &repo = ImageRepository::instance();
    repo.invalidateAll();
    const auto all = corpus.allPaths();
    if (all.size() < 2)
        return ScenarioResult{"B7", "switch_p50_ms", 0, {}, "need >=2 imgs", false};

    // Cold: first access (decode+display path).
    std::vector<double> cold, warm;
    ScenarioResult early;
    early.name = "B7";
    early.metric = "switch_warm_p50_ms";
    for (size_t i = 0; i < std::min<size_t>(100, all.size()); ++i)
    {
        if (failIfScenarioTimedOut(early, false))
            return early;
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
// Repository-level: warms the in-memory FullImage LRU and times
// ImageRepository::load() cache hits (not ImageViewer paint / MainWindow
// navigation). The end-to-end UI measurement is B18/B19 in scenarios_e2e.cpp.
// docs/performance.md: "Perceived latency < 16 ms (one frame at 60fps)".
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
    // B8 measures the preloaded display switch, not analysis work. Histogram
    // generation is O(image-data) and belongs to the analysis pipeline; doing
    // it for every navigation would make this gate measure the very blocking
    // work M36 removes from the browse hot path.
    ImageRepository::LoadOptions displayOpts;
    displayOpts.generateHistogram = false;
    ScenarioResult early;
    early.name = "B8";
    early.metric = "switch_p50_ms";
    for (size_t i = 0; i < m; ++i)
    {
        if (failIfScenarioTimedOut(early, false))
            return early;
        repo.load(all[i], displayOpts);
    }
    // Timed pass: 200 forward/back switches (all cache hits). Measure the
    // frame-to-frame navigation time the UI would incur (repo.load path).
    std::vector<double> sw;
    for (size_t k = 0; k < 200; ++k)
    {
        if (failIfScenarioTimedOut(early, false))
            return early;
        const size_t i = k % (m - 1);
        const double a = nowMs();
        repo.load(all[i], displayOpts);     // hit
        repo.load(all[i + 1], displayOpts); // step forward
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

    ScenarioResult early;
    early.name = "B9";
    early.metric = "baseline_return_ok";
    for (int c = 0; c < cycles; ++c)
    {
        if (failIfScenarioTimedOut(early, true))
            return early;
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

namespace
{

bool isTimeoutDetail(const std::string &detail)
{
    return detail.rfind("timed out after ", 0) == 0;
}

} // namespace

// B13: cache hit rate (0..1). Wraps the existing B5 Zipf navigation pattern.
ScenarioResult scenarioCacheHitRate(const Corpus &corpus)
{
    auto b5 = scenarioCacheHitRatio(corpus);
    ScenarioResult r;
    r.name = "B13";
    r.metric = "cache_hit_rate";
    r.value = b5.value;
    r.timing = b5.timing;
    r.passed = b5.passed;
    r.detail =
        isTimeoutDetail(b5.detail) ? b5.detail : "wraps B5 cache_hit_ratio under Zipf navigation";
    return r;
}

// B14: first-frame latency (ms) after opening the folder. Wraps B2's real
// ThumbnailPipeline path.
ScenarioResult scenarioFirstFrameLatency(const Corpus &corpus)
{
    auto b2 = scenarioFirstThumbnail(corpus);
    ScenarioResult r;
    r.name = "B14";
    r.metric = "first_frame_latency_ms";
    r.value = b2.value;
    r.timing = b2.timing;
    r.passed = b2.passed;
    r.detail = isTimeoutDetail(b2.detail) ? b2.detail
                                          : "wraps B2 real ThumbnailPipeline first-thumbnail path";
    return r;
}

// SCALE: corpus scaling tiers (100 / 1000 / 5000). Decodes `tierN` images
// (capped by corpus size) and reports decode throughput (fps) plus peak cache
// memory and GPU dedicated memory. The cache is evicted every window so peak
// memory stays bounded even at the 5000-image tier. Report-only.
ScenarioResult scenarioScaleTier(const Corpus &corpus, size_t tierN, const std::string &tierLabel)
{
    auto &repo = ImageRepository::instance();
    auto &cm = CacheManager::instance();
    auto &mt = mviewer::perf::MemoryTracker::instance();
    repo.invalidateAll();
    cm.clearMemory();
    mt.reset();

    const auto all = corpus.allPaths();
    if (all.empty())
        return ScenarioResult{"S" + tierLabel, "scale_decode_fps", 0, {}, "empty corpus", false};

    const size_t n = std::min(tierN, all.size());
    constexpr size_t kWindow = 64; // evict after each window to bound peak memory
    double peakCacheBytes = 0;

    ScenarioResult early;
    early.name = "S" + tierLabel;
    early.metric = "scale_decode_fps";
    const double t0 = nowMs();
    for (size_t i = 0; i < n; ++i)
    {
        if (failIfScenarioTimedOut(early, true))
            return early;
        repo.load(all[i]);
        peakCacheBytes = std::max(peakCacheBytes, static_cast<double>(mt.sample().cacheTotalBytes));
        if ((i + 1) % kWindow == 0)
        {
            cm.clearMemory();
            mt.reset();
        }
    }
    const double t1 = nowMs();
    const double elapsed = std::max(t1 - t0, 1e-6);
    const double fps = n / (elapsed / 1000.0);

    const auto gpu = mviewer::perf::sampleGpu();
    const double peakCacheMB = peakCacheBytes / (1024.0 * 1024.0);
    const double rssMB = mt.sample().processWorkingSetKB / 1024.0;
    const double gpuMB = gpu.dedicatedVideoBytes / (1024.0 * 1024.0);

    ScenarioResult r;
    r.name = "S" + tierLabel;
    r.metric = "scale_decode_fps";
    r.value = fps;
    r.passed = true; // report-only; never blocks CI
    r.detail = "tier=" + tierLabel + " n=" + std::to_string(n) +
               " elapsed_ms=" + std::to_string(elapsed) +
               " peak_cache_mb=" + std::to_string(static_cast<long long>(peakCacheMB)) +
               " rss_mb=" + std::to_string(static_cast<long long>(rssMB)) +
               " gpu_dedicated_mb=" + std::to_string(static_cast<long long>(gpuMB)) +
               (gpu.available ? "" : " (gpu:unavailable)");
    return r;
}

// ─── B17: workflow soak / resource-growth stability ──────────────────────────
// Repeats the canonical user workflow (open a window of images -> navigate ->
// compare two -> exit/clear) and samples process RSS + OS handle count after
// each cycle. A healthy build shows bounded growth; unbounded growth = a leak
// somewhere in the loop. Report-only: numbers feed the nightly dashboard.
namespace
{
size_t processHandleCount()
{
#ifdef _WIN32
    DWORD n = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &n))
        return static_cast<size_t>(n);
#endif
    return 0;
}
} // namespace

ScenarioResult scenarioWorkflowSoak(const Corpus &corpus)
{
    auto &repo = ImageRepository::instance();
    auto &cm = CacheManager::instance();
    auto &mt = mviewer::perf::MemoryTracker::instance();
    repo.invalidateAll();
    cm.clearMemory();
    mt.reset();

    const auto all = corpus.allPaths();
    if (all.size() < 2)
        return ScenarioResult{"B17", "workflow_soak_rss_growth_mb", 0, {}, "corpus too small",
                              false};

    const size_t window = std::min<size_t>(all.size(), 32);
    const int cycles = 12; // cycle 0 is warmup (caches/pools reach steady state)

    double rssFirstMB = 0, rssLastMB = 0;
    size_t handlesFirst = 0, handlesLast = 0;
    std::ostringstream perCycle;

    ScenarioResult early;
    early.name = "B17";
    early.metric = "workflow_soak_rss_growth_mb";
    for (int c = 0; c < cycles; ++c)
    {
        if (failIfScenarioTimedOut(early, false))
            return early;
        // 1) Browse: load a sliding window of images.
        const size_t off = static_cast<size_t>(c) * (window / 2) % all.size();
        for (size_t i = 0; i < window; ++i)
            repo.load(all[(off + i) % all.size()]);

        // 2) Compare: pull two frames and walk their pixel rows (proxies the
        //    diff / pixel-probe path without needing a QWidget).
        const auto ra = repo.load(all[off % all.size()]);
        const auto rb = repo.load(all[(off + 1) % all.size()]);
        volatile uint64_t sink = 0;
        for (const auto *res : {&ra, &rb})
        {
            if (!res->success())
                continue;
            const ImageData &img = res->frame->pixels();
            if (img.isNull())
                continue;
            const auto &bytes = *img.buffer;
            const size_t step = std::max<size_t>(1, bytes.size() / 4096);
            for (size_t i = 0; i < bytes.size(); i += step)
                sink += bytes[i];
        }
        (void)sink;

        // 3) Exit: drop the session and evict caches (same as closing Compare).
        cm.clearMemory();

        const auto s = mt.sample();
        const double rssMB = s.processWorkingSetKB / 1024.0;
        const size_t handles = processHandleCount();
        if (c == 1) // first post-warmup sample
        {
            rssFirstMB = rssMB;
            handlesFirst = handles;
        }
        rssLastMB = rssMB;
        handlesLast = handles;
        perCycle << " c" << c << ":rss=" << static_cast<long long>(rssMB) << ",h=" << handles;
    }

    const double rssGrowthMB = rssLastMB - rssFirstMB;
    const long long handleGrowth =
        static_cast<long long>(handlesLast) - static_cast<long long>(handlesFirst);

    ScenarioResult r;
    r.name = "B17";
    r.metric = "workflow_soak_rss_growth_mb";
    r.value = rssGrowthMB;
    r.passed = true; // report-only; the dashboard makes drift visible
    r.detail = "cycles=" + std::to_string(cycles) + " window=" + std::to_string(window) +
               " rss_growth_mb=" + std::to_string(static_cast<long long>(rssGrowthMB)) +
               " handle_growth=" + std::to_string(handleGrowth) +
               " handles_final=" + std::to_string(handlesLast) + perCycle.str();
    return r;
}

} // namespace mviewer::bench
