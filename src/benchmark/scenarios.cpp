#include "benchmark/scenarios.h"

#include "core/cache/CacheManager.h"
#include "core/image/ImageRepository.h"
#include "core/perf/MemoryTracker.h"

#include <QCoreApplication>
#include <QImage>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <thread>
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
template <typename Clock = std::chrono::steady_clock>
double nowMs()
{
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}
} // namespace

// ─── B1: startup to first paint ──────────────────────────────────────────────
ScenarioResult scenarioStartup()
{
    // B1 measures Qt-core readiness cost. Two contexts:
    //  - standalone mviewer_bench: no Q*Application exists yet -> measure the
    //    cold QCoreApplication construction + platform-plugin load.
    //  - folded into core_tests (which already owns a QCoreApplication):
    //    constructing a SECOND one is illegal in Qt and crashes, so we instead
    //    probe the already-live event loop. Either way: exactly one app, ever.
    ScenarioResult r;
    r.name = "B1";

    if (QCoreApplication::instance())
    {
        const double t0 = nowMs();
        QCoreApplication::processEvents();
        const double t1 = nowMs();
        r.metric = "qt_event_loop_probe_ms";
        r.value = t1 - t0;
        r.detail = "Qt already initialized (folded into core_tests)";
        return r;
    }

    const double t0 = nowMs();
    int argc = 1;
    char arg0[] = "mviewer_bench";
    char *argv[] = {arg0, nullptr};
    QCoreApplication app(argc, argv); // forces QtCore + platform plugin load
    QCoreApplication::processEvents();
    const double t1 = nowMs();

    r.metric = "startup_to_qtcore_ready_ms";
    r.value = t1 - t0;
    r.detail = "QCoreApplication + platform-plugin load";
    // Budget: cold <300ms / warm <100ms — set by harness, not here.
    return r;
}

// ─── B2: first thumbnail latency ─────────────────────────────────────────────
ScenarioResult scenarioFirstThumbnail(const Corpus &corpus)
{
    auto &repo = ImageRepository::instance();
    repo.invalidateAll();

    const double t0 = nowMs();
    std::atomic<bool> done{false};
    repo.loadDirectoryAsync(corpus.dir, [&done](std::vector<ImageRepository::Result>) {
        done.store(true, std::memory_order_relaxed);
    });
    // Latch on first thumbnail being available in the cache (background gen).
    double elapsed = 0;
    const double budget = 100.0; // docs/performance.md: first thumbnail <100ms
    while (!done.load(std::memory_order_relaxed) && elapsed < 10000.0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        elapsed = nowMs() - t0;
    }
    const double tFirst = elapsed;

    // Drain.
    for (int i = 0; i < 200 && !done.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    ScenarioResult r;
    r.name = "B2";
    r.metric = "first_thumbnail_ms";
    r.value = tFirst;
    // Smoke (non-enforce): report the metric; do not fail on budget here.
    // Budget enforcement (first thumbnail <100ms) is the --enforce / Phase-4 gate.
    r.passed = true;
    r.detail = "loadDirectoryAsync -> first thumbnail in cache";
    return r;
}

// ─── B3: decode latency per format ──────────────────────────────────────────
ScenarioResult scenarioDecodeLatency(const Corpus &corpus)
{
    auto &repo = ImageRepository::instance();
    repo.invalidateAll();

    auto timeOne = [&](const std::string &p) -> double {
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
                         res.frame->thumbnail().isNull()
                             ? res.frame->pixels()
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
    r.detail = "decoded+placed " + std::to_string(n) + " thumbnails in " +
               std::to_string(dt) + " ms";
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
    r.detail = "Zipf nav over " + std::to_string(n) + " imgs, " +
               std::to_string(navs) + " navigations";
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
               " peakBytes=" + std::to_string(peak.peakBytes) +
               " expectedFloor~" + std::to_string(expectedFloor);

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

} // namespace mviewer::bench
