#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "benchmark/corpus.h"

// Benchmark scenarios B0-B19 (docs/rfc/M10_PERFORMANCE_ENGINEERING.md).
// Each scenario functor is UNIT-TESTABLE: it returns a structured Result and
// does NOT assert wall-clock budgets (that is the --enforce harness job). The
// CTest suites test structural correctness; mviewer_bench prints + enforces.

namespace mviewer::bench
{

struct Timing
{
    double p50Ms = 0;
    double p95Ms = 0;
    double p99Ms = 0;
    size_t samples = 0;
};

struct ScenarioResult
{
    std::string name;   // "B1".."B19"
    std::string metric; // human-readable metric name
    double value = 0;   // primary metric (ms or ratio or bytes)
    Timing timing;      // for timing scenarios
    std::string detail; // extra printed line
    bool passed = true; // vs budget (only meaningful under --enforce)
};

// Shared timing helpers (B7/B8 and the B18/B19 UI scenarios).
inline double nowMs()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

inline Timing summarize(std::vector<double> vals)
{
    std::sort(vals.begin(), vals.end());
    Timing t;
    t.samples = vals.size();
    if (vals.empty())
        return t;
    const auto pct = [&](double p)
    {
        const size_t idx =
            static_cast<size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(vals.size() - 1));
        return vals[idx];
    };
    t.p50Ms = pct(0.50);
    t.p95Ms = pct(0.95);
    t.p99Ms = pct(0.99);
    return t;
}

// B2 stage breakdown for the first-thumbnail pipeline (M10 performance gate).
// Measures the REAL ThumbnailPipeline path, split into user-facing stages:
//   scan       - directory enumeration (FileSystem::listImages)
//   queue_wait - anchor (visible-range kick) -> first worker begins decode
//   decode     - decodeScaled (includes scaled-resize for the Qt codec path)
//   resize     - standalone QImage::scaled on the calling thread, and only when
//                the thumbnail and decode pools are idle. A full-resolution
//                QImageReader::read() on a pool thread beside other decodes can
//                deadlock Qt image IO.
//   cache      - worker-side mem-cache insert before result callback fires
//   ui_notify  - anchor -> first thumbnail handed to the UI adapter (resultFn)
//   total      - scan + ui_notify (end-to-end, user-perceived)
struct ThumbnailBreakdown
{
    double scan_ms = 0;
    double queue_wait_ms = 0;
    double decode_ms = 0;
    double resize_ms = 0;
    double cache_ms = 0;
    double ui_notify_ms = 0;
    double total_ms = 0;
    // First-thumbnail wait hit its deadline. The scenario reports a failing
    // sentinel so --enforce cannot score a hang as 0 ms.
    bool timedOut = false;
    int timeoutMs = 0;
    // Thumbnail pool drain hit its own deadline. The measured latency stays;
    // the scenario only appends a note.
    bool drainTimedOut = false;

    std::string toString() const;
};

// B1: startup-to-first-paint. Offscreen QApplication + first QWidget paint.
ScenarioResult scenarioStartup();

// B0: cold-start — full pipeline from process launch to first thumbnail.
// Wraps scenarioStartup + scenarioFirstThumbnail and reports the combined
// end-to-end time (simulates: reboot app → open folder → first image visible).
// Requires g_coldStartAnchor (set in main() before QApplication construction).
ScenarioResult scenarioColdStart(const Corpus &corpus);

// B2: folder load -> first thumbnail emit. Real ThumbnailPipeline path + breakdown.
ScenarioResult scenarioFirstThumbnail(const Corpus &corpus);

// TRACE: prove ThumbnailPipeline priority scheduling (visible > neighbor > background).
ScenarioResult scenarioPipelinePriority(const Corpus &corpus);

// B3: decode latency per format (p50/p95/p99 ms).
ScenarioResult scenarioDecodeLatency(const Corpus &corpus);

// B4: thumbnail throughput (images/sec) over the corpus.
ScenarioResult scenarioThumbThroughput(const Corpus &corpus);

// B5: cache-hit ratio under a Zipf navigation pattern.
ScenarioResult scenarioCacheHitRatio(const Corpus &corpus);

// B6: memory budget during a 1000 x 24 MP sweep (peak vs ~96 MB/image).
ScenarioResult scenarioMemoryBudget(const Corpus &corpus);

// B7: image-switch latency, preloaded vs cold. Repository-level: times
// ImageRepository::load() only. End-to-end UI next/previous is B18/B19.
ScenarioResult scenarioImageSwitch(const Corpus &corpus);

// B8: first-interaction latency for a PRELOADED image switch (docs/performance.md:
// "Perceived latency < 16 ms (one frame at 60fps)" / "Decode + display (preloaded)
// < 16 ms"). Repository-level: warms the in-memory FullImage LRU and times
// ImageRepository::load() cache hits. End-to-end UI next/previous (ImageViewer
// present + paint, overlay/preview/metadata) is B18/B19. Under --enforce the
// budget is the strict <16ms gate (vs B7's softer <=50ms report).
ScenarioResult scenarioSwitchLatency(const Corpus &corpus);

// B9: memory soak / stability. Runs repeated open -> navigate -> evict cycles and
// asserts peak cache bytes stay bounded and RETURN TO BASELINE after each clear
// (no monotonic leak across cycles). docs/performance.md: "Memory returns to
// baseline after cache eviction". Under --enforce requires each cycle's post-clear
// sample <= its peak and final baseline within a tolerance of the initial baseline.
ScenarioResult scenarioSoakStability(const Corpus &corpus);

// B10 / A-8.4: 100MP tile-cache viewport request. Synthesizes a 10000x10000
// RGBA image (100 MP) and measures TileCache::request() for a 1920x1080
// viewport at fit scale. Budget: first-viewport tile fill < 200 ms (soft) /
// < 500 ms under --enforce. Does not require a corpus file on disk.
ScenarioResult scenarioHundredMpTiles();

// ─── Post-M22 / review follow-up: named performance scenarios ────────────────
// These directly answer the review's "performance regression" asks (decode_8k_raw,
// decode_4k_jpeg, cache_hit_rate, first_frame_latency, zoom_latency). They are
// all corpus/asset-free (synthesized in-process) so they run headless in CI and
// never OOM on a missing RAW sample. Each is tracked against perf_baseline.json
// via the same ±10% regression gate as B0-B10.

// B11: 4K JPEG full-res decode latency (p50/p95/p99 ms).
ScenarioResult scenarioDecode4kJpeg();

// B12: 8K frame decode latency (p50/p95/p99 ms). CI has no RAW sample, so this
// decodes a synthesized 8K RGB frame through the same ImageData pipeline a real
// RAW would feed; swap in a .raw/.dng asset path to measure the true RAW path.
ScenarioResult scenarioDecode8k();

// B13: cache hit rate (0..1). Wraps the existing B5 Zipf navigation pattern.
ScenarioResult scenarioCacheHitRate(const Corpus &corpus);

// B14: first-frame latency (ms) — time to first visible image after opening the
// folder. Wraps B2's real ThumbnailPipeline path.
ScenarioResult scenarioFirstFrameLatency(const Corpus &corpus);

// B15: per-frame zoom render cost (p50/p95/p99 ms). Proxies an 8K->viewport
// rescale, the hot path behind wheel/gesture zoom.
ScenarioResult scenarioZoomLatency();

// B16: render throughput (frames/sec). Synthesizes a viewport rescale loop and
// reports achieved FPS for the render/zoom hot path. Report-only (machine
// dependent) — never blocks CI under --enforce.
ScenarioResult scenarioRenderFps();

// B17: workflow soak / resource-growth stability. Repeats the canonical user
// workflow (open window of images -> navigate -> compare two -> clear/exit)
// for N cycles and samples process RSS and OS handle count after each cycle.
// Reports first->last growth after a warmup cycle. A healthy build shows
// bounded growth; unbounded RSS/handle growth = leak in the workflow loop.
// Report-only (never blocks under --enforce) — the numbers feed the nightly
// dashboard so drift is visible before it becomes a leak bug.
ScenarioResult scenarioWorkflowSoak(const Corpus &corpus);

// SCALE: corpus scaling tiers (100 / 1000 / 5000 images). Decodes `tierN` images
// (capped by corpus size) and reports decode throughput (fps) plus peak cache
// memory and GPU dedicated memory. Report-only; surfaces memory/GPU/FPS at scale.
// `tierLabel` is the human-readable tier name ("100"/"1000"/"5000").
ScenarioResult scenarioScaleTier(const Corpus &corpus, size_t tierN, const std::string &tierLabel);

// How long B18/B19 run. `scheduleE2eSwitch` fills this. A default-constructed
// schedule runs nothing (`warm`/`cold` false) so the enforce gate cannot pick
// up the full UI loop by accident. `full()` is the explicit / non-enforce sample.
struct E2eSchedule
{
    bool warm = false;
    bool cold = false;
    const char *label = "skip"; // skip | smoke | short | full
    int warmIterations = 0;
    int coldIterations = 0;
    int subsetMax = 4;
    // <0 warms every image in the subset (repository load + one UI step each).
    int warmupPasses = -1;
    int warmTimeoutMs = 800;
    int coldTimeoutMs = 1200;

    static E2eSchedule full();
};

// Inputs for `scheduleE2eSwitch`. `tokenWarm` / `tokenCold` are true when
// `--scenarios` names B18 / B19 (the `e2e` token expands to both). `budgetWarm`
// / `budgetCold` are true only when performance_budget.json maps that scenario
// onto a numeric limit.
struct E2eRequest
{
    bool smoke = false;
    bool enforce = false;
    bool e2eFlag = false;
    bool filterActive = false;
    bool tokenWarm = false;
    bool tokenCold = false;
    bool budgetWarm = false;
    bool budgetCold = false;
};

// --smoke skips B18/B19 unless `--e2e` or `--scenarios` names them (then 2
// samples). --enforce skips them unless the budget gates the metric, in which
// case it keeps a >=10 sample on a 4-image fixture. `--e2e` / `--scenarios e2e`
// (and a named B18/B19) keep the full 100/30 sample. Other non-enforce runs
// are full too.
E2eSchedule scheduleE2eSwitch(const E2eRequest &req);

// B18: end-to-end WARM view-switch (preloaded/cached). Issues the same next-image
// command the UI uses (CommandRegistry "navigate_next" → MainWindow::navigate →
// ImageViewer::setImage + preview/metadata/overlay) and waits until the viewer
// displays the new source and a paint has completed. Not a hard gate unless
// scenario_map budgets the metric.
ScenarioResult scenarioE2eSwitchWarm(const Corpus &corpus,
                                     const E2eSchedule &run = E2eSchedule::full());

// B19: end-to-end COLD view-switch (first visit). Same UI path as B18, but the
// in-memory caches are flushed before each navigation so the sample includes
// decode + first present + paint.
ScenarioResult scenarioE2eSwitchCold(const Corpus &corpus,
                                     const E2eSchedule &run = E2eSchedule::full());

} // namespace mviewer::bench
