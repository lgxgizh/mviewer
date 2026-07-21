#include "benchmark/corpus.h"
#include "benchmark/scenarios.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTextStream>

#include "core/trace/TraceSink.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// mviewer_bench — M10 performance harness (docs/rfc/M10_PERFORMANCE_ENGINEERING.md).
//
// Usage:
//   mviewer_bench                  full corpus (1000/img), print verdicts, exit 0
//   mviewer_bench --smoke         small corpus (20/img), exit 0 (CI gate: prove it links+runs)
//   mviewer_bench --enforce       apply exact docs/performance.md budgets; exit !=0 on fail
//   mviewer_bench --corpus-size N generate N images per format
//   mviewer_bench --budget <file> load performance_budget.json (data-driven gates)
//   mviewer_bench --corpus-dir <d> reuse an existing on-disk corpus (no regen)
//   mviewer_bench --scenarios <s> comma-separated ids (e.g. B1,B2,B8)
//   mviewer_bench --results <f>   write JSON verdicts to <f>
//   mviewer_bench --baseline <f>  compare against baseline JSON; >10% regression -> fail
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

// Qt JSON-based budget reader (replaces naive string-parsing version).
bool loadBudgetJson(const std::string &path, Budget &b)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (doc.isNull() || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    const QJsonObject budgets = root.value("budgets").toObject();
    if (budgets.isEmpty())
        return false;

    auto get = [&](const char *key, double &out) {
        const QJsonValue v = budgets.value(QString::fromLatin1(key));
        if (v.isDouble()) { out = v.toDouble(); return true; }
        return false;
    };
    get("open_folder_ms", b.open_folder_ms);
    get("first_thumbnail_ms", b.first_thumbnail_ms);
    get("image_switch_ms", b.image_switch_ms);
    get("memory_mb", b.memory_mb);
    return true;
}

// Load baseline metrics from a JSON file using QJson.
// Returns a map of metric name -> baseline value.
std::unordered_map<std::string, double> loadBaselineJson(const std::string &path)
{
    std::unordered_map<std::string, double> m;
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return m;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (doc.isNull() || !doc.isObject())
        return m;

    const QJsonObject root = doc.object();
    const QJsonObject metrics = root.value("metrics").toObject();
    for (auto it = metrics.begin(); it != metrics.end(); ++it)
    {
        if (it.value().isDouble())
            m[it.key().toStdString()] = it.value().toDouble();
        else if (it.value().isString())
            m[it.key().toStdString()] = std::strtod(it.value().toString().toStdString().c_str(), nullptr);
    }
    return m;
}

// Write results to a JSON file.
void writeResultsJson(const std::string &path,
                      const std::vector<mviewer::bench::ScenarioResult> &results)
{
    QJsonArray arr;
    for (const auto &r : results)
    {
        QJsonObject o;
        o.insert("name", QString::fromStdString(r.name));
        o.insert("metric", QString::fromStdString(r.metric));
        o.insert("value", r.value);
        o.insert("passed", r.passed);
        arr.append(o);
    }
    QJsonObject root;
    root.insert("results", arr);
    root.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QJsonDocument doc(root);
    QFile f(QString::fromStdString(path));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();
    }
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

// Build the scenario list, apply --enforce budgets, print verdicts, and return
// whether every scenario passed. Shared by the regen path and the --corpus-dir
// path so both exercise identical scenarios/printing.
bool runScenarios(const mviewer::bench::Corpus &corpus, const Budget &b,
                  const std::set<std::string> &runOnly = {},
                  std::vector<mviewer::bench::ScenarioResult> *outResults = nullptr)
{
    const bool filter = !runOnly.empty();
    // Lazy scenario registry, filtered on the AUTHORITATIVE name BEFORE
    // invocation. Corpus-flooding scenarios (B3-B6 preload all 10000 imgs) are
    // skipped entirely instead of OOM-ing the process when running only a
    // targeted subset (e.g. --scenarios B1,B2,B8 for large-scale acceptance).
    struct Item { std::string name; std::function<mviewer::bench::ScenarioResult()> fn; };
    std::vector<Item> items;
    items.push_back({"B0",    [&]() { return mviewer::bench::scenarioColdStart(corpus); }});
    items.push_back({"B1",    [&]() { return mviewer::bench::scenarioStartup(); }});
    items.push_back({"B2",    [&]() { return mviewer::bench::scenarioFirstThumbnail(corpus); }});
    items.push_back({"TRACE", [&]() { return mviewer::bench::scenarioPipelinePriority(corpus); }});
    items.push_back({"B3",    [&]() { return mviewer::bench::scenarioDecodeLatency(corpus); }});
    items.push_back({"B4",    [&]() { return mviewer::bench::scenarioThumbThroughput(corpus); }});
    items.push_back({"B5",    [&]() { return mviewer::bench::scenarioCacheHitRatio(corpus); }});
    items.push_back({"B6",    [&]() { return mviewer::bench::scenarioMemoryBudget(corpus); }});
    items.push_back({"B7",    [&]() { return mviewer::bench::scenarioImageSwitch(corpus); }});
    items.push_back({"B8",    [&]() { return mviewer::bench::scenarioSwitchLatency(corpus); }});
    items.push_back({"B9",    [&]() { return mviewer::bench::scenarioSoakStability(corpus); }});

    std::vector<mviewer::bench::ScenarioResult> results;
    for (const auto &it : items)
    {
        if (filter && !runOnly.count(it.name))
            continue;
        results.push_back(it.fn());
    }

    bool allPass = true;
    for (auto &r : results)
    {
        if (b.enforce)
        {
            if (r.name == "B2")
                r.passed = b.check(r.value, b.first_thumbnail_ms);
            else if (r.name == "B8")
                r.passed = b.check(r.value, b.image_switch_ms);
            else if (r.name == "B9")
            {
                bool ok = (r.value > 0.5);
                if (ok)
                {
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
    if (outResults)
        *outResults = std::move(results);
    return allPass;
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
    std::string corpusDir;          // P3: if set, reuse an existing on-disk corpus dir
    std::string scenariosArg;       // P3: comma-separated scenario ids to run (e.g. "B1,B2,B8")
    std::string resultsFile;        // M14: if set, write JSON results (for regression tracking)
    std::string baselineFile;       // M14: if set, compare against baseline for regression

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
        else if (a == "--corpus-dir" && i + 1 < argc)
            corpusDir = argv[++i];
        else if (a == "--scenarios" && i + 1 < argc)
            scenariosArg = argv[++i];
        else if (a == "--results" && i + 1 < argc)
            resultsFile = argv[++i];
        else if (a == "--baseline" && i + 1 < argc)
            baselineFile = argv[++i];
    }

    // Build the restricted scenario set (empty = run all). Normalize tokens to the
    // exact scenario names (B1..B9, TRACE) so "--scenarios B1,B2,B8" works.
    std::set<std::string> runOnly;
    if (!scenariosArg.empty())
    {
        size_t pos = 0;
        while (pos < scenariosArg.size())
        {
            size_t comma = scenariosArg.find(',', pos);
            std::string tok = (comma == std::string::npos)
                ? scenariosArg.substr(pos)
                : scenariosArg.substr(pos, comma - pos);
            // trim + uppercase
            auto start = tok.find_first_not_of(" \t");
            if (start != std::string::npos)
            {
                auto end = tok.find_last_not_of(" \t");
                tok = tok.substr(start, end - start + 1);
            }
            for (auto &c : tok)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!tok.empty())
                runOnly.insert(tok);
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
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

    std::vector<mviewer::bench::ScenarioResult> results;
    bool allPass = true;

    // P3 reusable-corpus mode: consume an existing on-disk dataset instead of
    // regenerating one. Behavior is otherwise identical to the regen path below.
    if (!corpusDir.empty())
    {
        mviewer::bench::Corpus corpus = mviewer::bench::makeCorpusFromDir(corpusDir);
        std::cout << "corpus: jpeg=" << corpus.jpegPaths.size() << " png=" << corpus.pngPaths.size()
                  << " tiff=" << corpus.tiffPaths.size() << " dir=" << corpus.dir << std::endl;
        allPass = runScenarios(corpus, b, runOnly, &results);
        std::cout << "=== " << (allPass ? "ALL PASS" : "SOME FAIL") << " ===" << std::endl;
    }
    else
    {
        mviewer::bench::Corpus corpus = mviewer::bench::makeCorpus(corpusSize);
        std::cout << "corpus: jpeg=" << corpus.jpegPaths.size() << " png=" << corpus.pngPaths.size()
                  << " tiff=" << corpus.tiffPaths.size() << " dir=" << corpus.dir << std::endl;

        allPass = runScenarios(corpus, b, runOnly, &results);
        corpus.clear();
    }

    std::cout << "=== " << (allPass ? "ALL PASS" : "SOME FAIL") << " ===" << std::endl;

    // M14: write JSON results if --results given.
    if (!resultsFile.empty())
        writeResultsJson(resultsFile, results);

    // M14: regression vs baseline if --baseline given.
    if (!baselineFile.empty())
    {
        auto baseline = loadBaselineJson(baselineFile);
        if (!baseline.empty())
        {
            std::cout << "=== REGRESSION CHECK ===" << std::endl;
            for (const auto &r : results)
            {
                // Match by metric name (e.g. "B2_first_thumbnail_ms").
                std::string key = r.name + "_" + r.metric;
                auto it = baseline.find(key);
                if (it != baseline.end() && it->second > 0.0)
                {
                    double delta = ((r.value - it->second) / it->second) * 100.0;
                    std::cout << "  " << r.name << ": current=" << r.value
                              << " baseline=" << it->second << " delta=" << delta << "%";
                    if (delta > 10.0)
                    {
                        std::cout << " *** REGRESSION >10% ***";
                        allPass = false;
                    }
                    std::cout << std::endl;
                }
            }
        }
        else
        {
            std::cout << "Warning: could not load baseline from " << baselineFile << std::endl;
        }
    }

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
