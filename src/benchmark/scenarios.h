#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "benchmark/corpus.h"

// Benchmark scenarios B1-B7 (docs/rfc/M10_PERFORMANCE_ENGINEERING.md).
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
    std::string name;     // "B1".."B7"
    std::string metric;   // human-readable metric name
    double value = 0;     // primary metric (ms or ratio or bytes)
    Timing timing;         // for timing scenarios
    std::string detail;   // extra printed line
    bool passed = true;   // vs budget (only meaningful under --enforce)
};

// B2 stage breakdown for the first-thumbnail pipeline (M10 performance gate).
// Measures the REAL ThumbnailPipeline path, split into user-facing stages:
//   scan       - directory enumeration (FileSystem::listImages)
//   queue_wait - anchor (visible-range kick) -> first worker begins decode
//   decode     - decodeScaled (includes scaled-resize for the Qt codec path)
//   resize     - standalone resize cost (separate from decode; ~0 when folded)
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

    std::string toString() const;
};

// B1: startup-to-first-paint. Offscreen QApplication + first QWidget paint.
ScenarioResult scenarioStartup();

// B2: folder load -> first thumbnail emit. loadDirectoryAsync latched.
ScenarioResult scenarioFirstThumbnail(const Corpus &corpus);

// B3: decode latency per format (p50/p95/p99 ms).
ScenarioResult scenarioDecodeLatency(const Corpus &corpus);

// B4: thumbnail throughput (images/sec) over the corpus.
ScenarioResult scenarioThumbThroughput(const Corpus &corpus);

// B5: cache-hit ratio under a Zipf navigation pattern.
ScenarioResult scenarioCacheHitRatio(const Corpus &corpus);

// B6: memory budget during a 1000 x 24 MP sweep (peak vs ~96 MB/image).
ScenarioResult scenarioMemoryBudget(const Corpus &corpus);

// B7: image-switch latency, preloaded vs cold.
ScenarioResult scenarioImageSwitch(const Corpus &corpus);

} // namespace mviewer::bench
