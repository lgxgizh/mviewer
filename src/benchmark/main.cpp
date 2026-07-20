#include "benchmark/corpus.h"
#include "benchmark/scenarios.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QImage>

#include "core/trace/TraceSink.h"

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
    // mviewer_bench only synthesizes QImage corpora and decodes them — it needs
    // QtGui (QImage/QImageWriter) but NOT a windowing platform. Use
    // QCoreApplication (not QApplication) so it runs headless without a platform
    // plugin; this also avoids a QApplication init hang when the Qt platform
    // plugin search path is not set up in the build dir.
    QCoreApplication app(argc, argv);

    Budget b;
    size_t corpusSize = 1000;
    bool smoke = false;
    std::string emitData;           // P3: if set, emit corpus to this dir and exit.
    std::string emitFormat = "all"; // P3: "all" or "jpeg" (10000-jpeg large tier)
    std::string traceFile;          // M13.5: if set, flush a Chrome trace JSON at exit

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
        else if (a == "--emit-data" && i + 1 < argc)
            emitData = argv[++i];
        else if (a == "--emit-format" && i + 1 < argc)
            emitFormat = argv[++i];
        else if (a == "--corpus-size" && i + 1 < argc)
            corpusSize = static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--trace" && i + 1 < argc)
            traceFile = argv[++i];
    }

    std::cout << "=== MViewer benchmark (M10) ===" << std::endl;
    if (smoke)
        std::cout << "[smoke] ";
    std::cout << "corpus-size=" << corpusSize
              << " enforce=" << (b.enforce ? "yes" : "no") << std::endl;

    // P3 dataset emission mode: generate the corpus into emitData and stop
    // (no scenarios, no cleanup) so benchmark/data/{small,medium,large} can be
    // materialized as reproducible, reusable image sets.
    if (!emitData.empty())
    {
        mviewer::bench::Corpus corpus =
            mviewer::bench::makeCorpus(corpusSize, 512, 512, emitData, emitFormat);
        std::cout << "emitted: jpeg=" << corpus.jpegPaths.size()
                  << " png=" << corpus.pngPaths.size() << " tiff=" << corpus.tiffPaths.size()
                  << " dir=" << corpus.dir << std::endl;
        std::cout << "=== EMIT DONE ===" << std::endl;
        return 0;
    }

    mviewer::bench::Corpus corpus = mviewer::bench::makeCorpus(corpusSize);
    std::cout << "corpus: jpeg=" << corpus.jpegPaths.size() << " png=" << corpus.pngPaths.size()
              << " tiff=" << corpus.tiffPaths.size() << " dir=" << corpus.dir << std::endl;

    std::vector<mviewer::bench::ScenarioResult> results;
    results.push_back(mviewer::bench::scenarioStartup());
    results.push_back(mviewer::bench::scenarioFirstThumbnail(corpus));
    results.push_back(mviewer::bench::scenarioPipelinePriority(corpus));
    results.push_back(mviewer::bench::scenarioDecodeLatency(corpus));
    results.push_back(mviewer::bench::scenarioThumbThroughput(corpus));
    results.push_back(mviewer::bench::scenarioCacheHitRatio(corpus));
    results.push_back(mviewer::bench::scenarioMemoryBudget(corpus));
    results.push_back(mviewer::bench::scenarioImageSwitch(corpus));
    results.push_back(mviewer::bench::scenarioSwitchLatency(corpus));
    results.push_back(mviewer::bench::scenarioSoakStability(corpus));

    bool allPass = true;
    for (auto &r : results)
    {
        // M10 performance gate: under --enforce, apply the docs/performance.md
        // budgets. Smoke (no --enforce) reports metrics only.
        if (b.enforce)
        {
            if (r.name == "B2")
                r.passed = b.check(r.value, 100.0); // first thumbnail <100ms
            else if (r.name == "B8")
                r.passed = b.check(r.value, 16.0); // preloaded switch <16ms
            else if (r.name == "B9")
            {
                // baseline_return_ok == 1.0 AND final within 2x initial baseline.
                bool ok = (r.value > 0.5);
                if (ok)
                {
                    // Parse finalBase / initBase from detail for the tolerance check.
                    const auto posF = r.detail.find("finalBase=");
                    const auto posI = r.detail.find("initBase=");
                    if (posF != std::string::npos && posI != std::string::npos)
                    {
                        const double finalB = std::strtod(r.detail.c_str() + posF + 10, nullptr);
                        const double initB = std::strtod(r.detail.c_str() + posI + 9, nullptr);
                        if (initB > 0)
                            ok = (finalB <= initB * 2.0);
                    }
                }
                r.passed = ok;
            }
        }
        printVerdict(r, b);
        if (!r.passed)
            allPass = false;
    }

    corpus.clear();

    std::cout << "=== " << (allPass ? "ALL PASS" : "SOME FAIL") << " ===" << std::endl;
    // M13.5: flush a Chrome trace JSON if --trace was given (only meaningful
    // when built with MVIEWER_ENABLE_PERFETTO; otherwise the macros are no-ops
    // and the buffer is empty, so we report and skip).
#if defined(MVIEWER_ENABLE_PERFETTO)
    if (!traceFile.empty())
    {
        const bool ok = mviewer::trace::flush(traceFile);
        std::cout << "trace: " << (ok ? "wrote " : "FAILED to write ") << traceFile << " ("
                  << mviewer::trace::count() << " spans)" << std::endl;
    }
#else
    if (!traceFile.empty())
        std::cout << "trace: --trace needs a build with MVIEWER_ENABLE_PERFETTO=ON" << std::endl;
#endif

    // CI (--smoke) always exits 0 (proves links + runs). Local --enforce may exit 1.
    if (b.enforce && !allPass)
        return 1;
    return 0;
}
