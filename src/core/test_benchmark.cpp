#include "benchmark/corpus.h"
#include "benchmark/scenarios.h"

#include <cassert>
#include <cmath>
#include <iostream>

// Structural tests for the benchmark scenario functors. These assert the
// functors return well-formed results (finite timing, ratios in [0,1], memory
// decays after clear) — NOT wall-clock budget enforcement (that is --enforce).

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " #cond " @ " << __LINE__ << "\n";                                 \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

using namespace mviewer::bench;

static bool finite(double d)
{
    return d == d && std::isfinite(d);
}

int benchmark_suite()
{
    // Small corpus (20/img) keeps this CTest fast and deterministic.
    Corpus corpus = makeCorpus(20);
    CHECK(!corpus.jpegPaths.empty());
    CHECK(!corpus.pngPaths.empty());
    const auto all = corpus.allPaths();
    CHECK(all.size() == corpus.jpegPaths.size() + corpus.pngPaths.size() + corpus.tiffPaths.size());

    // B1 startup: finite, positive.
    auto b1 = scenarioStartup();
    CHECK(b1.name == "B1");
    CHECK(finite(b1.value) && b1.value >= 0);

    // B2 first thumbnail: finite.
    auto b2 = scenarioFirstThumbnail(corpus);
    CHECK(b2.name == "B2");
    CHECK(finite(b2.value) && b2.value >= 0);

    // B3 decode latency: timing populated, p50 finite and >=0.
    auto b3 = scenarioDecodeLatency(corpus);
    CHECK(b3.name == "B3");
    CHECK(b3.timing.samples > 0);
    CHECK(finite(b3.timing.p50Ms) && b3.timing.p50Ms >= 0);
    CHECK(b3.timing.p50Ms <= b3.timing.p99Ms + 1e-6);

    // B4 throughput: positive images/sec.
    auto b4 = scenarioThumbThroughput(corpus);
    CHECK(b4.name == "B4");
    CHECK(b4.value > 0);

    // B5 cache-hit ratio: in [0,1].
    auto b5 = scenarioCacheHitRatio(corpus);
    CHECK(b5.name == "B5");
    CHECK(b5.value >= 0.0 && b5.value <= 1.0);

    // B6 memory budget: peak reported; after clear, cache bytes <= peak.
    auto b6 = scenarioMemoryBudget(corpus);
    CHECK(b6.name == "B6");
    CHECK(b6.value >= 0);
    CHECK(b6.passed);

    // B7 image switch: warm p50 finite.
    auto b7 = scenarioImageSwitch(corpus);
    CHECK(b7.name == "B7");
    CHECK(finite(b7.value) && b7.value >= 0);

    corpus.clear();
    return g_failures;
}
