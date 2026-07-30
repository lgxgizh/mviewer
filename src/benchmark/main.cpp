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
//   mviewer_bench --enforce       hard-gate B1-B9 + B11-B15 against performance_budget.json;
//                                 exit !=0 on any budget violation (CI hard gate)
//   mviewer_bench --regression    ALSO diff against perf_baseline.json; >10% regression -> fail
//   mviewer_bench --budget <file> load performance_budget.json (data-driven gates)
//   mviewer_bench --baseline <f>  explicit baseline JSON (implies --regression)
//   mviewer_bench --corpus-size N generate N images per format
//   mviewer_bench --scale          scaling tiers 100/1000/5000: decode fps + peak
//                                 cache memory + GPU dedicated memory (report-only)
//   mviewer_bench --scenarios B16,S100  run a targeted set (e.g. B16 render-fps,
//                                 S100/S1000/S5000 scaling tiers)
//   mviewer_bench --corpus-dir <d> reuse an existing on-disk corpus (no regen)
//   mviewer_bench --scenarios <s> comma-separated ids (e.g. B1,B2,B8)
//   mviewer_bench --results <f>   write JSON verdicts to <f>
//   mviewer_bench --history <f>   append results row to history CSV (M15 regression tracking)
//   mviewer_bench --report  <f>   write markdown regression report to <f>
//
// Gate model (product力 #2 "稳得住"):
//   --enforce applies HARD budgets to the scenarios in performance_budget.json
//   ["scenario_map"] (B1-B9 + B11-B15). These are generous, cross-machine-stable
//   absolute caps, so the mandatory CI gate (ci.yml `test` job via the `bench_enforce` ctest:
//   `--enforce --budget`) never fails on hardware jitter. --regression (or --baseline) enables the
//   SEPARATE, noisier baseline-diff; it is run by nightly.yml, NOT the mandatory
//   PR gate, because the committed baseline is machine-specific. B0/B10/TRACE are
//   report-only and only regression-checked.

namespace
{
struct Budget
{
    bool enforce = false;

    // metric name -> hard limit. Lower-is-better unless the metric is listed in
    // higherIsBetter(). Loaded from performance_budget.json["budgets"]; the
    // defaults below mirror that file so `mviewer_bench --enforce` (no --budget)
    // still gates on the same targets. The JSON is the single source of truth in CI.
    std::unordered_map<std::string, double> limits = {
        {"qt_event_loop_probe_ms", 50.0},  {"first_thumbnail_ms", 100.0},
        {"decode_p50_ms_jpeg", 100.0},     {"thumbnails_per_sec", 30.0},
        {"cache_hit_ratio", 0.10},         {"peak_cache_bytes", 536870912.0}, // 512 MiB
        {"switch_warm_p50_ms", 50.0},      {"image_switch_ms", 16.0},
        {"baseline_return_ok", 1.0},       {"decode_4k_jpeg_ms", 400.0},
        {"decode_8k_ms", 2000.0},          {"cache_hit_rate", 0.10},
        {"first_frame_latency_ms", 120.0}, {"zoom_frame_ms_b15", 600.0},
    };

    // scenario id (B1..) -> metric name this scenario is gated against. Only
    // scenarios present here are hard-gated under --enforce. Matches
    // performance_budget.json["scenario_map"] (B1-B9 + B11-B15). B0/B10/TRACE are
    // report-only (B10 is still ±10% regression-checked when --regression is given).
    std::unordered_map<std::string, std::string> scenarioMap = {
        {"B1", "qt_event_loop_probe_ms"},  {"B2", "first_thumbnail_ms"},
        {"B3", "decode_p50_ms_jpeg"},      {"B4", "thumbnails_per_sec"},
        {"B5", "cache_hit_ratio"},         {"B6", "peak_cache_bytes"},
        {"B7", "switch_warm_p50_ms"},      {"B8", "image_switch_ms"},
        {"B9", "baseline_return_ok"},      {"B11", "decode_4k_jpeg_ms"},
        {"B12", "decode_8k_ms"},           {"B13", "cache_hit_rate"},
        {"B14", "first_frame_latency_ms"}, {"B15", "zoom_frame_ms_b15"},
    };

    // Metrics where a HIGHER value is better (throughput / hit-rate / stability flag).
    // Every other metric is lower-is-better.
    static const std::set<std::string> &higherIsBetter()
    {
        static const std::set<std::string> s = {"cache_hit_rate", "cache_hit_ratio",
                                                "thumbnails_per_sec", "baseline_return_ok"};
        return s;
    }

    // True if `scenario` is part of the hard-gated set.
    bool appliesTo(const std::string &scenario) const
    {
        return scenarioMap.find(scenario) != scenarioMap.end();
    }

    // Enforce `measured` for `scenario` against its budget limit. Returns true
    // (pass) when the scenario is not in the gated set, so unspecified scenarios
    // keep their own structural verdict.
    bool checkScenario(const std::string &scenario, double measured) const
    {
        auto it = scenarioMap.find(scenario);
        if (it == scenarioMap.end())
            return true;
        auto lit = limits.find(it->second);
        if (lit == limits.end())
            return true;
        const bool hib = higherIsBetter().count(it->second) > 0;
        return hib ? (measured >= lit->second) : (measured <= lit->second);
    }
};

// Qt JSON-based budget reader (replaces naive string-parsing version).
// Loads EVERY metric in performance_budget.json["budgets"] into `limits` and the
// scenario->metric bindings in ["scenario_map"] into `scenarioMap`, overriding the
// in-code defaults. When the file is missing or malformed we keep the defaults so
// `mviewer_bench --enforce` (no --budget) still gates. The JSON is the single
// source of truth for limits in CI.
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
    for (auto it = budgets.begin(); it != budgets.end(); ++it)
    {
        if (it.value().isDouble())
            b.limits[it.key().toStdString()] = it.value().toDouble();
    }

    const QJsonObject smap = root.value("scenario_map").toObject();
    for (auto it = smap.begin(); it != smap.end(); ++it)
    {
        if (it.value().isString())
            b.scenarioMap[it.key().toStdString()] = it.value().toString().toStdString();
    }
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
            m[it.key().toStdString()] =
                std::strtod(it.value().toString().toStdString().c_str(), nullptr);
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
    std::cout << '\n';
}

// Build the scenario list, apply --enforce budgets, print verdicts, and return
// whether every scenario passed. Shared by the regen path and the --corpus-dir
// path so both exercise identical scenarios/printing.
bool runScenarios(const mviewer::bench::Corpus &corpus, const Budget &b,
                  const std::set<std::string> &runOnly = {},
                  std::vector<mviewer::bench::ScenarioResult> *outResults = nullptr,
                  bool scaleMode = false)
{
    const bool filter = !runOnly.empty();
    // Lazy scenario registry, filtered on the AUTHORITATIVE name BEFORE
    // invocation. Corpus-flooding scenarios (B3-B6 preload all 10000 imgs) are
    // skipped entirely instead of OOM-ing the process when running only a
    // targeted subset (e.g. --scenarios B1,B2,B8 for large-scale acceptance).
    struct Item
    {
        std::string name;
        std::function<mviewer::bench::ScenarioResult()> fn;
    };
    std::vector<Item> items;
    items.push_back({"B0", [&]() { return mviewer::bench::scenarioColdStart(corpus); }});
    items.push_back({"B1", [&]() { return mviewer::bench::scenarioStartup(); }});
    items.push_back({"B2", [&]() { return mviewer::bench::scenarioFirstThumbnail(corpus); }});
    items.push_back({"TRACE", [&]() { return mviewer::bench::scenarioPipelinePriority(corpus); }});
    items.push_back({"B3", [&]() { return mviewer::bench::scenarioDecodeLatency(corpus); }});
    items.push_back({"B4", [&]() { return mviewer::bench::scenarioThumbThroughput(corpus); }});
    items.push_back({"B5", [&]() { return mviewer::bench::scenarioCacheHitRatio(corpus); }});
    items.push_back({"B6", [&]() { return mviewer::bench::scenarioMemoryBudget(corpus); }});
    items.push_back({"B7", [&]() { return mviewer::bench::scenarioImageSwitch(corpus); }});
    items.push_back({"B8", [&]() { return mviewer::bench::scenarioSwitchLatency(corpus); }});
    items.push_back({"B9", [&]() { return mviewer::bench::scenarioSoakStability(corpus); }});
    // B10 does not need the corpus; it synthesizes a 100MP buffer in-process.
    items.push_back({"B10", [&]() { return mviewer::bench::scenarioHundredMpTiles(); }});
    // Post-M22 named scenarios (review "性能回归" asks). All synthesized
    // in-process, so they run headless and never OOM on a missing RAW sample.
    // They are report + baseline-regression (±10%) gated, not hard exit-fails,
    // to avoid CI flakiness on uncalibrated hardware.
    items.push_back({"B11", [&]() { return mviewer::bench::scenarioDecode4kJpeg(); }});
    items.push_back({"B12", [&]() { return mviewer::bench::scenarioDecode8k(); }});
    items.push_back({"B13", [&]() { return mviewer::bench::scenarioCacheHitRate(corpus); }});
    items.push_back({"B14", [&]() { return mviewer::bench::scenarioFirstFrameLatency(corpus); }});
    items.push_back({"B15", [&]() { return mviewer::bench::scenarioZoomLatency(); }});
    // B16: render throughput (fps), report-only (machine dependent).
    items.push_back({"B16", [&]() { return mviewer::bench::scenarioRenderFps(); }});
    // SCALE tiers (opt-in via --scale): decode throughput (fps) plus peak cache
    // memory and GPU dedicated memory at 100 / 1000 / 5000 images. Report-only.
    if (scaleMode)
    {
        items.push_back(
            {"S100", [&]() { return mviewer::bench::scenarioScaleTier(corpus, 100, "100"); }});
        items.push_back(
            {"S1000", [&]() { return mviewer::bench::scenarioScaleTier(corpus, 1000, "1000"); }});
        items.push_back(
            {"S5000", [&]() { return mviewer::bench::scenarioScaleTier(corpus, 5000, "5000"); }});
    }

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
        // Data-driven hard gate (product力 #2 "稳得住"). Each scenario listed in
        // Budget::scenarioMap (B1-B9 + B11-B15, sourced from performance_budget.json)
        // is enforced against its budget limit. Scenarios NOT in the map
        // (B0/B10/TRACE) keep their structural verdict and are only
        // regression-checked (when --regression is given). This is the canonical CI
        // hard gate: generous, cross-machine-stable absolute caps, so it never fails
        // on CI hardware jitter.
        if (b.enforce && b.appliesTo(r.name))
            r.passed = b.checkScenario(r.name, r.value);
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
    bool regression = false; // M15: enable baseline diff (separate from --enforce hard budget)
    bool scaleMode = false;  // M-XX: run the 100/1000/5000 scaling tiers
    std::string historyFile; // M15: if set, append results row to history CSV
    std::string reportFile;  // M15: if set, write markdown regression report

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--enforce")
            b.enforce = true;
        else if (a == "--regression")
            regression = true;
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
        else if (a == "--history" && i + 1 < argc)
            historyFile = argv[++i];
        else if (a == "--report" && i + 1 < argc)
            reportFile = argv[++i];
        else if (a == "--scale")
        {
            scaleMode = true;
            // Scale tiers (100/1000/5000) are capped by the available corpus.
            // Pass --corpus-size 5000 for the full sweep; smaller sizes run the
            // same tiers capped to the generated image count.
        }
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
            std::string tok = (comma == std::string::npos) ? scenariosArg.substr(pos)
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

    std::cout << "=== MViewer benchmark (M10) ===" << '\n';
    if (smoke)
        std::cout << "[smoke] ";
    std::cout << "corpus-size=" << corpusSize << " enforce=" << (b.enforce ? "yes" : "no");
    if (!budgetFile.empty())
    {
        if (loadBudgetJson(budgetFile, b))
            std::cout << " budget=" << budgetFile;
        else
            std::cout << " budget=LOAD-FAILED(" << budgetFile << ")";
    }
    std::cout << '\n';

    // P3 dataset emission mode: generate the corpus into emitData and stop
    // (no scenarios, no cleanup) so benchmark/data/{small,medium,large} can be
    // materialized as reproducible, reusable image sets.
    if (!emitData.empty())
    {
        mviewer::bench::Corpus corpus =
            mviewer::bench::makeCorpus(corpusSize, 512, 512, emitData, emitFormat);
        std::cout << "emitted: jpeg=" << corpus.jpegPaths.size()
                  << " png=" << corpus.pngPaths.size() << " tiff=" << corpus.tiffPaths.size()
                  << " dir=" << corpus.dir << '\n';
        std::cout << "=== EMIT DONE ===" << '\n';
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
                  << " tiff=" << corpus.tiffPaths.size() << " dir=" << corpus.dir << '\n';
        allPass = runScenarios(corpus, b, runOnly, &results, scaleMode);
        std::cout << "=== " << (allPass ? "ALL PASS" : "SOME FAIL") << " ===" << '\n';
    }
    else
    {
        mviewer::bench::Corpus corpus = mviewer::bench::makeCorpus(corpusSize);
        std::cout << "corpus: jpeg=" << corpus.jpegPaths.size() << " png=" << corpus.pngPaths.size()
                  << " tiff=" << corpus.tiffPaths.size() << " dir=" << corpus.dir << '\n';

        allPass = runScenarios(corpus, b, runOnly, &results, scaleMode);
        corpus.clear();
    }

    std::cout << "=== " << (allPass ? "ALL PASS" : "SOME FAIL") << " ===" << '\n';

    // M14: write JSON results if --results given.
    if (!resultsFile.empty())
        writeResultsJson(resultsFile, results);

    // M15: auto-baseline — when --enforce + --regression are on but no --baseline
    // is specified, attempt to load benchmark/perf_baseline.json from cwd. This
    // closes the M15 gap: "CI does not yet diff against baseline or fail on
    // regression." Regression is a SEPARATE, noisier axis (it compares against a
    // machine-specific baseline); the mandatory CI hard gate (ci.yml `test` job, via the
    // `bench_enforce` ctest) runs
    // `--enforce --budget` WITHOUT --regression so it never fails on hardware
    // jitter. Nightly runs `--enforce --budget --regression` for trend tracking.
    if (b.enforce && regression && baselineFile.empty())
    {
        static const char *const kCandidates[] = {"benchmark/perf_baseline.json",
                                                  "../benchmark/perf_baseline.json", nullptr};
        for (int ci = 0; kCandidates[ci]; ++ci)
        {
            if (QFile::exists(QString::fromLatin1(kCandidates[ci])))
            {
                baselineFile = kCandidates[ci];
                std::cout << "auto-baseline: " << baselineFile << '\n';
                break;
            }
        }
    }

    // M15: regression vs baseline.
    std::vector<std::string> regressionIssues; // { "B2: +12.3%", ... }
    if (!baselineFile.empty())
    {
        auto baseline = loadBaselineJson(baselineFile);
        if (!baseline.empty())
        {
            std::cout << "=== REGRESSION CHECK ===" << '\n';
            for (const auto &r : results)
            {
                std::string key = r.name + "_" + r.metric;
                auto it = baseline.find(key);
                if (it != baseline.end() && it->second > 0.0)
                {
                    double delta = ((r.value - it->second) / it->second) * 100.0;
                    const char *flag = "";
                    if (delta > 10.0)
                    {
                        flag = " *** REGRESSION >10% ***";
                        allPass = false;
                    }
                    else if (delta > 5.0)
                    {
                        flag = " * WARN >5%";
                    }
                    std::cout << "  " << r.name << ": current=" << r.value
                              << " baseline=" << it->second << " delta=" << delta << "%" << flag
                              << '\n';
                    if (delta > 10.0)
                    {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "%s: %+.1f%% (%.3f -> %.3f)",
                                      r.name.c_str(), delta, it->second, r.value);
                        regressionIssues.push_back(buf);
                    }
                }
            }
        }
        else
        {
            std::cout << "Warning: could not load baseline from " << baselineFile << '\n';
        }
    }

    // M15: append results row to history CSV.
    if (!historyFile.empty())
    {
        // Determine if the file exists (needs header on first write).
        bool hasHeader = QFile::exists(QString::fromStdString(historyFile));
        std::ofstream csv(historyFile, std::ios::app);
        if (csv.is_open())
        {
            if (!hasHeader)
            {
                csv << "date,scenario,metric,value,p50,p95,p99,passed,regression_pct\n";
            }
            const std::string today =
                QDateTime::currentDateTime().toString("yyyy-MM-dd").toStdString();
            // Load baseline once (avoid re-parsing per row).
            auto bl = baselineFile.empty() ? std::unordered_map<std::string, double>{}
                                           : loadBaselineJson(baselineFile);
            for (const auto &r : results)
            {
                csv << today << ',' << r.name << ',' << r.metric << ',' << r.value << ','
                    << r.timing.p50Ms << ',' << r.timing.p95Ms << ',' << r.timing.p99Ms << ','
                    << (r.passed ? "1" : "0") << ',';
                // Include regression delta for baseline-matched metrics.
                if (!bl.empty())
                {
                    std::string key = r.name + "_" + r.metric;
                    auto it = bl.find(key);
                    if (it != bl.end() && it->second > 0.0)
                        csv << ((r.value - it->second) / it->second) * 100.0;
                }
                csv << '\n';
            }
            csv.close();
            std::cout << "history: appended " << results.size() << " rows to " << historyFile
                      << '\n';
        }
    }

    // M15: write markdown regression report.
    if (!reportFile.empty())
    {
        std::ofstream rpt(reportFile);
        if (rpt.is_open())
        {
            rpt << "# MViewer Benchmark Regression Report\n\n";
            rpt << "**Date:** "
                << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString()
                << "\n\n";
            rpt << "**Baseline:** " << (baselineFile.empty() ? "(none)" : baselineFile) << "\n\n";
            rpt << "| Scenario | Metric | Value | Passed | Regression |\n";
            rpt << "|----------|--------|-------|--------|------------|\n";
            auto baseline = baselineFile.empty() ? std::unordered_map<std::string, double>{}
                                                 : loadBaselineJson(baselineFile);
            for (const auto &r : results)
            {
                std::string regr = "—";
                if (!baseline.empty())
                {
                    std::string key = r.name + "_" + r.metric;
                    auto it = baseline.find(key);
                    if (it != baseline.end() && it->second > 0.0)
                    {
                        double delta = ((r.value - it->second) / it->second) * 100.0;
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%+.1f%%", delta);
                        regr = buf;
                    }
                }
                rpt << "| " << r.name << " | " << r.metric << " | " << r.value << " | "
                    << (r.passed ? "PASS" : "FAIL") << " | " << regr << " |\n";
            }
            if (!regressionIssues.empty())
            {
                rpt << "\n### Regressions (>10%)\n\n";
                for (const auto &issue : regressionIssues)
                    rpt << "- " << issue << "\n";
            }
            else
            {
                rpt << "\n### No regressions detected\n";
            }
            rpt.close();
            std::cout << "report: wrote " << reportFile << '\n';
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
                  << mviewer::trace::count() << " spans)" << '\n';
    }
#else
    if (!traceFile.empty())
        std::cout << "trace: --trace needs a build with MVIEWER_ENABLE_PERFETTO=ON" << '\n';
#endif

    // CI (--smoke) always exits 0 (proves links + runs). Local --enforce may exit 1.
    if (b.enforce && !allPass)
        return 1;
    return 0;
}
