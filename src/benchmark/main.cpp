#include "benchmark/corpus.h"
#include "benchmark/scenarios.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <QApplication>
#include <QImage>

// mviewer_bench — M10 performance harness (docs/rfc/M10_PERFORMANCE_ENGINEERING.md).
//
// Usage:
//   mviewer_bench                  full corpus (1000/img), print verdicts, exit 0
//   mviewer_bench --smoke         small corpus (20/img), exit 0 (CI gate: prove it links+runs)
//   mviewer_bench --enforce       apply exact docs/performance.md budgets; exit !=0 on fail
//   mviewer_bench --corpus-size N generate N images per format
//
// CI runs --smoke only (roadmap Phase-1). The regression gate (--enforce in CI)
// is roadmap Phase-4 and is intentionally NOT wired into ci.yml here.

namespace
{
struct Budget
{
    bool enforce = false;
    // docs/performance.md targets (applied only when enforce=true).
    bool check(double measured, double limit, bool lowerIsBetter = true) const
    {
        if (!enforce)
            return true;
        return lowerIsBetter ? (measured <= limit) : (measured >= limit);
    }
};

void printVerdict(const mviewer::bench::ScenarioResult &r, const Budget &b)
{
    const char *ok = r.passed ? "[PASS]" : "[FAIL]";
    std::cout << ok << " " << r.name << " " << r.metric << "=" << r.value;
    if (r.timing.samples > 0)
        std::cout << " (p50=" << r.timing.p50Ms << " p95=" << r.timing.p95Ms
                  << " p99=" << r.timing.p99Ms << ")";
    if (!r.detail.empty())
        std::cout << "  # " << r.detail;
    std::cout << std::endl;
}
} // namespace

int main(int argc, char **argv)
{
    // Qt (QImage codecs, event loop) requires a live QApplication BEFORE any
    // QImage / codec work. Construct it first so makeCorpus() and the scenarios
    // run inside a valid Qt context. Image codec plugins live in the GUI module,
    // so QApplication (not QCoreApplication) is required. scenarioStartup() detects
    // this instance and only probes the already-live event loop instead of
    // constructing a second one.
    QApplication app(argc, argv);

    Budget b;
    size_t corpusSize = 1000;
    bool smoke = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--enforce")
            b.enforce = true;
        else if (a == "--smoke")
        {
            smoke = true;
            corpusSize = 20;
        }
        else if (a == "--corpus-size" && i + 1 < argc)
            corpusSize = static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
    }

    std::cout << "=== MViewer benchmark (M10) ===" << std::endl;
    std::cout << (smoke ? "[smoke] " : "") << "corpus-size=" << corpusSize
              << " enforce=" << (b.enforce ? "yes" : "no") << std::endl;

    mviewer::bench::Corpus corpus = mviewer::bench::makeCorpus(corpusSize);
    std::cout << "corpus: jpeg=" << corpus.jpegPaths.size()
              << " png=" << corpus.pngPaths.size()
              << " tiff=" << corpus.tiffPaths.size()
              << " dir=" << corpus.dir << std::endl;

    std::vector<mviewer::bench::ScenarioResult> results;
    results.push_back(mviewer::bench::scenarioStartup());
    results.push_back(mviewer::bench::scenarioFirstThumbnail(corpus));
    results.push_back(mviewer::bench::scenarioDecodeLatency(corpus));
    results.push_back(mviewer::bench::scenarioThumbThroughput(corpus));
    results.push_back(mviewer::bench::scenarioCacheHitRatio(corpus));
    results.push_back(mviewer::bench::scenarioMemoryBudget(corpus));
    results.push_back(mviewer::bench::scenarioImageSwitch(corpus));

    bool allPass = true;
    for (auto &r : results)
    {
        // M10 performance gate: under --enforce, B2 (first thumbnail) must meet
        // the docs/performance.md budget of <100ms (cold). Smoke reports only.
        if (b.enforce && r.name == "B2")
            r.passed = b.check(r.value, 100.0);
        printVerdict(r, b);
        if (!r.passed)
            allPass = false;
    }

    corpus.clear();

    std::cout << "=== " << (allPass ? "ALL PASS" : "SOME FAIL") << " ==="
              << std::endl;
    // CI (--smoke) always exits 0 (proves links + runs). Local --enforce may exit 1.
    if (b.enforce && !allPass)
        return 1;
    return 0;
}
