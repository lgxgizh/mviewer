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
    // Data-driven limits loaded from performance_budget.json (M13.3). When
    // no file is supplied we fall back to the historical hardcoded values so
    // `mviewer_bench --enforce` (no --budget) still gates on the docs
    // targets. The JSON is the single source of truth in CI.
    double open_folder_ms = 500.0;
    double first_thumbnail_ms = 100.0;
    double image_switch_ms = 16.0;
    double memory_mb = 512.0;
    bool check(double measured, double limit, bool lowerIsBetter = true) const
    {
        if (!enforce)
            return true;
        return lowerIsBetter ? (measured <= limit) : (measured >= limit);
    }
};

// Minimal JSON reader — the budget file is flat (no nesting beyond one
// `budgets` object), so we avoid pulling in a full JSON library into the
// benchmark binary. Returns false on any parse failure (caller keeps
// defaults and prints a warning).
bool loadBudgetJson(const std::string &path, Budget &b)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string txt((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
    auto findNum = [&](const std::string &key, double &out) -> bool
    {
        const auto pos = txt.find("\"" + key + "\"");
        if (pos == std::string::npos)
            return false;
        const auto colon = txt.find(':', pos + key.size() + 1);
        if (colon == std::string::npos)
            return false;
        const auto comma = txt.find_first_of(",}", colon);
        if (comma == std::string::npos)
            return false;
        // Manual numeric parse (exceptions may be disabled in this TU).
        const std::string slice = txt.substr(colon + 1, comma - colon - 1);
        const char *beg = slice.c_str();
        char *end = nullptr;
        double v = std::strtod(beg, &end);
        if (end == beg || *end != '\0')
            return false;
        out = v;
        return true;
    };
    // Parse the nested "budgets": { ... } object.
    const auto bpos = txt.find("\"budgets\"");
    const auto objStart = (bpos == std::string::npos) ? std::string::npos : txt.find('{', bpos);
    const auto objEnd = (objStart == std::string::npos) ? std::string::npos : txt.find('}', objStart);
    if (objStart != std::string::npos && objEnd != std::string::npos)
    {
        const std::string inner = txt.substr(objStart, objEnd - objStart + 1);
        auto findIn = [&](const std::string &key, double &out) -> bool
        {
            const auto p = inner.find("\"" + key + "\"");
            if (p == std::string::npos)
                return false;
            const auto c = inner.find(':', p + key.size() + 1);
            if (c == std::string::npos)
                return false;
            const auto e = inner.find_first_of(",}", c);
            if (e == std::string::npos)
                return false;
            // Manual numeric parse (exceptions may be disabled in this TU).
            const std::string slice = inner.substr(c + 1, e - c - 1);
            const char *beg = slice.c_str();
            char *end = nullptr;
            double v = std::strtod(beg, &end);
            if (end == beg || *end != '\0')
                return false;
            out = v;
            return true;
        };
        findIn("open_folder_ms", b.open_folder_ms);
        findIn("first_thumbnail_ms", b.first_thumbnail_ms);
        findIn("image_switch_ms", b.image_switch_ms);
        findIn("memory_mb", b.memory_mb);
    }
    // Tolerate a flat key as well (back-compat).
    findNum("open_folder_ms", b.open_folder_ms);
    findNum("first_thumbnail_ms", b.first_thumbnail_ms);
    findNum("image_switch_ms", b.image_switch_ms);
    findNum("memory_mb", b.memory_mb);
    return true;
}

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
    std::string budgetFile;         // M13.3: performance_budget.json (data-driven gates)

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--enforce")
            b.enforce = true;
        else if (a == "--budget" && i + 1 < argc)
            budgetFile = argv[++i];
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
              << " enforce=" << (b.enforce ? "yes" : "no");
    if (!budgetFile.empty())
    {
        if (loadBudgetJson(budgetFile, b))
            std::cout << " budget=" << budgetFile;
        else
            std::cout << " budget=LOAD-FAILED(" << budgetFile << ")";
    }
    std::cout << std::endl;

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
                r.passed = b.check(r.value, b.first_thumbnail_ms); // first thumbnail budget
            else if (r.name == "B8")
                r.passed = b.check(r.value, b.image_switch_ms); // preloaded switch budget
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
