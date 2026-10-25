#include "benchmark/bench_progress.h"
#include "benchmark/corpus.h"
#include "benchmark/scenarios.h"

#include "application/Shutdown.h"
#include "core/image/DiskCache.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>

#include "core/trace/TraceSink.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// mviewer_bench — M10 performance harness (docs/rfc/M10_PERFORMANCE_ENGINEERING.md).
//
//   mviewer_bench                  full corpus, including full B18/B19, exit 0
//   mviewer_bench --smoke          20/img corpus, skip B18/B19, exit 0 (link+run)
//   mviewer_bench --enforce        hard-gate scenario_map; skip unbudgeted B18/B19
//   mviewer_bench --e2e            full B18/B19 (100/30). Same as --scenarios e2e
//   mviewer_bench --regression     also diff perf_baseline.json; >10% -> fail
//   mviewer_bench --budget <f>     --profile <auto|base|low-core|ci>  --baseline <f>
//   mviewer_bench --corpus-size N  --corpus-dir <d>  --scale  --scenarios B1,B8
//   mviewer_bench --results <f>    --history <f>  --report <f>  --trace <f>
//   mviewer_bench --scenario-timeout-ms N
//                                  --enforce watchdog per scenario (default 120000;
//                                  <=0 disables). A blocked scenario prints
//                                  `[bench] WATCHDOG <name> exceeded N ms` and exits 2.
//
// --enforce hard-gates scenario_map (B1-B9 + B11-B15). B18/B19 need QApplication
// (offscreen when QT_QPA_PLATFORM is unset). They run for --e2e / --scenarios,
// for a non-enforce run, or when the budget file actually gates them (12 samples).

namespace
{
struct HardwareProfile
{
    std::string name;
    double latencyMultiplier = 1.0;
    double throughputMultiplier = 1.0;
};

struct Budget
{
    bool enforce = false;
    unsigned logicalCores = 1;
    unsigned autoLowCoreMax = 4;
    std::string profileName = "base";
    std::unordered_map<std::string, HardwareProfile> profiles = {
        {"base", {"base", 1.0, 1.0}},
        {"low-core", {"low-core", 1.25, 0.80}},
        {"ci", {"ci", 1.0, 1.0}},
    };
    std::set<std::string> scalableLatencyMetrics = {
        "first_thumbnail_ms", "decode_p50_ms_jpeg", "switch_warm_p50_ms",     "image_switch_ms",
        "decode_4k_jpeg_ms",  "decode_8k_ms",       "first_frame_latency_ms", "zoom_frame_ms_b15",
    };
    std::set<std::string> scalableThroughputMetrics = {"thumbnails_per_sec"};

    // metric name -> hard limit. Lower-is-better unless the metric is listed in
    // higherIsBetter(). Loaded from performance_budget.json["budgets"]; the
    // defaults below mirror that file so `mviewer_bench --enforce` (no --budget)
    // still gates on the same targets. The JSON is the single source of truth in CI.
    std::unordered_map<std::string, double> limits = {
        {"qt_event_loop_probe_ms", 50.0},
        {"first_thumbnail_ms", 100.0},
        {"decode_p50_ms_jpeg", 100.0},
        {"thumbnails_per_sec", 30.0},
        {"cache_hit_ratio", 0.10},
        {"peak_cache_bytes", 939524096.0}, // 896 MiB (Viewer+Preview mip)
        {"switch_warm_p50_ms", 50.0},
        {"image_switch_ms", 16.0},
        {"baseline_return_ok", 1.0},
        {"decode_4k_jpeg_ms", 400.0},
        {"decode_8k_ms", 2000.0},
        {"cache_hit_rate", 0.10},
        {"first_frame_latency_ms", 120.0},
        {"zoom_frame_ms_b15", 600.0},
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

    bool configureProfile(const std::string &requested)
    {
        const unsigned detected = std::thread::hardware_concurrency();
        logicalCores = detected == 0 ? 1 : detected;

        if (requested == "auto")
            profileName = logicalCores <= autoLowCoreMax ? "low-core" : "base";
        else
            profileName = requested;

        return profiles.find(profileName) != profiles.end();
    }

    double effectiveLimit(const std::string &metric, double limit) const
    {
        const auto profile = profiles.find(profileName);
        if (profile == profiles.end())
            return limit;
        if (scalableLatencyMetrics.count(metric) > 0)
            return limit * profile->second.latencyMultiplier;
        if (scalableThroughputMetrics.count(metric) > 0)
            return limit * profile->second.throughputMultiplier;
        return limit;
    }

    // True if `scenario` is part of the hard-gated set.
    bool appliesTo(const std::string &scenario) const
    {
        return scenarioMap.find(scenario) != scenarioMap.end();
    }

    // True when scenario_map binds `scenario` to a metric that has a numeric limit.
    bool gated(const std::string &scenario) const
    {
        const auto it = scenarioMap.find(scenario);
        if (it == scenarioMap.end())
            return false;
        return limits.find(it->second) != limits.end();
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
        const double limit = effectiveLimit(it->second, lit->second);
        return hib ? (measured >= limit) : (measured <= limit);
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
    // ifstream, not QFile: this runs before QCoreApplication exists so the
    // harness can choose QApplication only when B18/B19 are actually scheduled.
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string text;
    char buf[4096]{};
    while (in.read(buf, sizeof(buf)))
        text.append(buf, sizeof(buf));
    text.append(buf, static_cast<size_t>(in.gcount()));
    QJsonParseError err;
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray(text.data(), static_cast<int>(text.size())), &err);
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

    const QJsonObject scaling = root.value("hardware_scaling").toObject();
    if (scaling.value("auto_low_core_max").isDouble())
        b.autoLowCoreMax =
            static_cast<unsigned>(std::max(1.0, scaling.value("auto_low_core_max").toDouble()));

    const QJsonArray latencyMetrics = scaling.value("latency_metrics").toArray();
    if (!latencyMetrics.isEmpty())
        b.scalableLatencyMetrics.clear();
    for (const auto &value : latencyMetrics)
    {
        if (value.isString())
            b.scalableLatencyMetrics.insert(value.toString().toStdString());
    }

    const QJsonArray throughputMetrics = scaling.value("throughput_metrics").toArray();
    if (!throughputMetrics.isEmpty())
        b.scalableThroughputMetrics.clear();
    for (const auto &value : throughputMetrics)
    {
        if (value.isString())
            b.scalableThroughputMetrics.insert(value.toString().toStdString());
    }

    const QJsonObject profiles = scaling.value("profiles").toObject();
    for (auto it = profiles.begin(); it != profiles.end(); ++it)
    {
        const QJsonObject profile = it.value().toObject();
        HardwareProfile parsed{it.key().toStdString(), 1.0, 1.0};
        if (profile.value("latency_multiplier").isDouble())
            parsed.latencyMultiplier = profile.value("latency_multiplier").toDouble();
        if (profile.value("throughput_multiplier").isDouble())
            parsed.throughputMultiplier = profile.value("throughput_multiplier").toDouble();
        b.profiles[it.key().toStdString()] = parsed;
    }
    return true;
}

struct BenchOptions
{
    Budget budget;
    size_t corpusSize = 1000;
    bool smoke = false;
    bool regression = false;
    bool scaleMode = false;
    bool e2eFlag = false;
    int scenarioTimeoutMs = mviewer::bench::kDefaultScenarioTimeoutMs;
    std::string emitData;
    std::string emitFormat = "all";
    std::string traceFile;
    std::string budgetFile;
    std::string profileArg = "auto";
    std::string corpusDir;
    std::string scenariosArg;
    std::string resultsFile;
    std::string baselineFile;
    std::string historyFile;
    std::string reportFile;
};

struct Prepared
{
    bool budgetLoaded = false;
    bool needGui = false;
    mviewer::bench::E2eSchedule e2e;
    std::set<std::string> runOnly;
};

struct BenchSession
{
    double t0 = mviewer::bench::nowMs();
    ~BenchSession()
    {
        const long long ms = static_cast<long long>(mviewer::bench::nowMs() - t0);
        mviewer::bench::writeLine("[bench] total " + std::to_string(ms) + " ms");
        // Drain the stderr tap before reading the count so a libpng line still
        // in the pipe is included, then print on the restored stderr/stdout.
        mviewer::bench::shutdownBenchIo();
        const int errors = mviewer::bench::libpngReadErrorCount();
        if (errors > 0)
            mviewer::bench::writeLine("[bench] libpng Read Error count=" + std::to_string(errors));
    }
};

void printVerdict(const mviewer::bench::ScenarioResult &r, const Budget &b)
{
    (void)b;
    const char *ok = r.passed ? "[PASS]" : "[FAIL]";
    std::cout << ok << " " << r.name << " " << r.metric << "=" << r.value;
    if (r.timing.samples > 0)
        std::cout << " (p50=" << r.timing.p50Ms << " p95=" << r.timing.p95Ms
                  << " p99=" << r.timing.p99Ms << ")";
    if (!r.detail.empty())
        std::cout << "  # " << r.detail;
    std::cout << '\n' << std::flush;
}

// Build the scenario list, apply --enforce budgets, print verdicts, and return
// whether every scenario passed. Shared by the regen path and the --corpus-dir
// path so both exercise identical scenarios/printing.
bool runScenarios(const mviewer::bench::Corpus &corpus, const Budget &b,
                  const std::set<std::string> &runOnly,
                  std::vector<mviewer::bench::ScenarioResult> *outResults, bool scaleMode,
                  const mviewer::bench::E2eSchedule &e2e, int scenarioWatchdogMs)
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
    items.push_back({"B11", [&]() { return mviewer::bench::scenarioDecode4kJpeg(); }});
    items.push_back({"B12", [&]() { return mviewer::bench::scenarioDecode8k(); }});
    items.push_back({"B13", [&]() { return mviewer::bench::scenarioCacheHitRate(corpus); }});
    items.push_back({"B14", [&]() { return mviewer::bench::scenarioFirstFrameLatency(corpus); }});
    items.push_back({"B15", [&]() { return mviewer::bench::scenarioZoomLatency(); }});
    // B16: render throughput (fps), report-only (machine dependent).
    items.push_back({"B16", [&]() { return mviewer::bench::scenarioRenderFps(); }});
    items.push_back({"B17", [&]() { return mviewer::bench::scenarioWorkflowSoak(corpus); }});
    // B18/B19 length comes from scheduleE2eSwitch (skip / smoke / short / full).
    if (e2e.warm)
        items.push_back(
            {"B18", [&]() { return mviewer::bench::scenarioE2eSwitchWarm(corpus, e2e); }});
    if (e2e.cold)
        items.push_back(
            {"B19", [&]() { return mviewer::bench::scenarioE2eSwitchCold(corpus, e2e); }});
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
    bool allPass = true;
    for (const auto &it : items)
    {
        if (filter && runOnly.count(it.name) == 0)
            continue;
        mviewer::bench::ScenarioSpan span(it.name, scenarioWatchdogMs);
        auto result = it.fn();
        span.finish();
        // scenario_map entries are hard-gated. Anything else keeps its own verdict.
        if (b.enforce && b.appliesTo(result.name))
            result.passed = b.checkScenario(result.name, result.value);
        printVerdict(result, b);
        if (!result.passed)
            allPass = false;
        results.push_back(std::move(result));
    }
    if (outResults)
        *outResults = std::move(results);
    return allPass;
}

bool takeFlag(BenchOptions &opt, const std::string &arg)
{
    if (arg == "--enforce")
    {
        opt.budget.enforce = true;
        return true;
    }
    if (arg == "--regression")
    {
        opt.regression = true;
        return true;
    }
    if (arg == "--smoke")
    {
        opt.smoke = true;
        opt.corpusSize = 20;
        return true;
    }
    if (arg == "--e2e")
    {
        opt.e2eFlag = true;
        return true;
    }
    if (arg == "--scale")
    {
        // Tiers are capped by the corpus. Pass --corpus-size 5000 for the full sweep.
        opt.scaleMode = true;
        return true;
    }
    return false;
}

void setScenarioTimeout(BenchOptions &opt, const char *text)
{
    char *end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || (end != nullptr && *end != '\0'))
        return;
    if (parsed <= 0)
    {
        opt.scenarioTimeoutMs = 0;
        return;
    }
    if (parsed > 1800000L)
    {
        opt.scenarioTimeoutMs = 1800000;
        return;
    }
    opt.scenarioTimeoutMs = static_cast<int>(parsed);
}

bool takeCorpusArgs(BenchOptions &opt, const std::string &arg, int &i, int argc, char **argv)
{
    if (i + 1 >= argc)
        return false;
    if (arg == "--corpus-size")
    {
        opt.corpusSize = static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
        return true;
    }
    if (arg == "--corpus-dir")
    {
        opt.corpusDir = argv[++i];
        return true;
    }
    if (arg == "--scenarios")
    {
        opt.scenariosArg = argv[++i];
        return true;
    }
    if (arg == "--scenario-timeout-ms")
    {
        setScenarioTimeout(opt, argv[++i]);
        return true;
    }
    return false;
}

bool takeOutputArgs(BenchOptions &opt, const std::string &arg, int &i, int argc, char **argv)
{
    if (i + 1 >= argc)
        return false;
    if (arg == "--budget")
    {
        opt.budgetFile = argv[++i];
        return true;
    }
    if (arg == "--profile")
    {
        opt.profileArg = argv[++i];
        return true;
    }
    if (arg == "--emit-data")
    {
        opt.emitData = argv[++i];
        return true;
    }
    if (arg == "--emit-format")
    {
        opt.emitFormat = argv[++i];
        return true;
    }
    if (arg == "--trace")
    {
        opt.traceFile = argv[++i];
        return true;
    }
    if (arg == "--results")
    {
        opt.resultsFile = argv[++i];
        return true;
    }
    if (arg == "--baseline")
    {
        opt.baselineFile = argv[++i];
        return true;
    }
    if (arg == "--history")
    {
        opt.historyFile = argv[++i];
        return true;
    }
    if (arg == "--report")
    {
        opt.reportFile = argv[++i];
        return true;
    }
    return false;
}

void parseArgs(int argc, char **argv, BenchOptions &opt)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (takeFlag(opt, arg))
            continue;
        if (takeCorpusArgs(opt, arg, i, argc, argv))
            continue;
        (void)takeOutputArgs(opt, arg, i, argc, argv);
    }
}

void insertScenarioToken(std::set<std::string> &runOnly, const std::string &tok)
{
    if (tok.empty())
        return;
    if (tok == "E2E")
    {
        runOnly.insert("B18");
        runOnly.insert("B19");
        return;
    }
    runOnly.insert(tok);
}

std::string normalizeScenarioToken(const std::string &raw)
{
    const auto start = raw.find_first_not_of(" \t");
    if (start == std::string::npos)
        return {};
    const auto end = raw.find_last_not_of(" \t");
    std::string tok = raw.substr(start, end - start + 1);
    for (char &c : tok)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return tok;
}

std::set<std::string> parseScenarioList(const std::string &scenariosArg)
{
    std::set<std::string> runOnly;
    size_t pos = 0;
    while (pos < scenariosArg.size())
    {
        const size_t comma = scenariosArg.find(',', pos);
        const std::string raw = (comma == std::string::npos)
                                    ? scenariosArg.substr(pos)
                                    : scenariosArg.substr(pos, comma - pos);
        insertScenarioToken(runOnly, normalizeScenarioToken(raw));
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return runOnly;
}

Prepared prepareRun(BenchOptions &opt, int &exitCode)
{
    Prepared prep;
    if (!opt.budgetFile.empty())
        prep.budgetLoaded = loadBudgetJson(opt.budgetFile, opt.budget);
    if (!opt.budget.configureProfile(opt.profileArg))
    {
        std::cerr << "Unknown hardware profile: " << opt.profileArg
                  << " (expected auto, base, low-core, or ci)" << '\n'
                  << std::flush;
        exitCode = 2;
        return prep;
    }
    prep.runOnly = parseScenarioList(opt.scenariosArg);
    // `--e2e` alongside a scenario filter still runs the full UI sample.
    if (opt.e2eFlag && !prep.runOnly.empty())
    {
        prep.runOnly.insert("B18");
        prep.runOnly.insert("B19");
    }
    prep.e2e = mviewer::bench::scheduleE2eSwitch(
        {opt.smoke, opt.budget.enforce, opt.e2eFlag, !prep.runOnly.empty(),
         prep.runOnly.count("B18") > 0, prep.runOnly.count("B19") > 0, opt.budget.gated("B18"),
         opt.budget.gated("B19")});
    prep.needGui = opt.emitData.empty() && (prep.e2e.warm || prep.e2e.cold);
    if (prep.needGui && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    return prep;
}

void printBanner(const BenchOptions &opt, const Prepared &prep)
{
    std::ostringstream oss;
    oss << "=== MViewer benchmark (M10) ===\n";
    if (opt.smoke)
        oss << "[smoke] ";
    if (!opt.budgetFile.empty() && !prep.budgetLoaded)
        oss << "budget=LOAD-FAILED(" << opt.budgetFile << ")\n";
    oss << "corpus-size=" << opt.corpusSize << " enforce=" << (opt.budget.enforce ? "yes" : "no")
        << " e2e=" << prep.e2e.label << " logical-cores=" << opt.budget.logicalCores
        << " profile=" << opt.budget.profileName;
    if (prep.budgetLoaded)
        oss << " budget=" << opt.budgetFile;
    oss << '\n';
    if (opt.budget.enforce)
        oss << "scenario-timeout-ms=" << opt.scenarioTimeoutMs << '\n';
    std::cout << oss.str();
    std::cout.flush();
    std::fflush(stdout);
    const char *qt = prep.needGui ? "QApplication" : "QCoreApplication";
    mviewer::bench::writeLine(std::string("[bench] qt=") + qt);
}

void warnIfCorpusIncomplete(const mviewer::bench::Corpus &corpus, bool expectPng)
{
    const bool missing = corpus.jpegPaths.empty() || (expectPng && corpus.pngPaths.empty());
    if (missing)
        mviewer::bench::writeLine("[bench] corpus is missing jpeg or png files");
}

void printCorpusLine(const mviewer::bench::Corpus &corpus)
{
    std::ostringstream oss;
    oss << "corpus: jpeg=" << corpus.jpegPaths.size() << " png=" << corpus.pngPaths.size()
        << " tiff=" << corpus.tiffPaths.size() << " dir=" << corpus.dir;
    mviewer::bench::writeLine(oss.str());
}

int runEmit(const BenchOptions &opt)
{
    mviewer::bench::writeLine("[bench] start corpus");
    const double t0 = mviewer::bench::nowMs();
    const mviewer::bench::Corpus corpus =
        mviewer::bench::makeCorpus(opt.corpusSize, 512, 512, opt.emitData, opt.emitFormat);
    const long long ms = static_cast<long long>(mviewer::bench::nowMs() - t0);
    mviewer::bench::writeLine("[bench] done corpus in " + std::to_string(ms) + " ms");
    std::ostringstream oss;
    oss << "emitted: jpeg=" << corpus.jpegPaths.size() << " png=" << corpus.pngPaths.size()
        << " tiff=" << corpus.tiffPaths.size() << " dir=" << corpus.dir;
    mviewer::bench::writeLine(oss.str());
    warnIfCorpusIncomplete(corpus, opt.emitFormat != "jpeg");
    mviewer::bench::writeLine("=== EMIT DONE ===");
    return 0;
}

bool runCollected(const BenchOptions &opt, const Prepared &prep,
                  const mviewer::bench::Corpus &corpus,
                  std::vector<mviewer::bench::ScenarioResult> &results)
{
    printCorpusLine(corpus);
    const int watchdogMs = opt.budget.enforce ? opt.scenarioTimeoutMs : 0;
    return runScenarios(corpus, opt.budget, prep.runOnly, &results, opt.scaleMode, prep.e2e,
                        watchdogMs);
}

void flushTrace(const std::string &traceFile)
{
#if defined(MVIEWER_ENABLE_PERFETTO)
    if (traceFile.empty())
        return;
    const bool ok = mviewer::trace::flush(traceFile);
    std::ostringstream oss;
    oss << "trace: " << (ok ? "wrote " : "FAILED to write ") << traceFile << " ("
        << mviewer::trace::count() << " spans)";
    mviewer::bench::writeLine(oss.str());
#else
    if (!traceFile.empty())
        mviewer::bench::writeLine("trace: --trace needs a build with MVIEWER_ENABLE_PERFETTO=ON");
#endif
}

int finishBenchRun(BenchOptions &opt, bool allPass,
                   const std::vector<mviewer::bench::ScenarioResult> &results)
{
    if (!opt.resultsFile.empty())
    {
        const mviewer::bench::BenchResultMeta meta{opt.budget.profileName, opt.budget.logicalCores};
        mviewer::bench::writeResultsJson(opt.resultsFile, results, meta);
    }
    // Regression is a separate axis from the hard budget. Nightly passes
    // --regression; the PR bench_enforce gate does not.
    if (opt.budget.enforce && opt.regression && opt.baselineFile.empty())
    {
        const std::string found = mviewer::bench::findDefaultBaselineFile();
        if (!found.empty())
        {
            opt.baselineFile = found;
            mviewer::bench::writeLine("auto-baseline: " + opt.baselineFile);
        }
    }
    std::vector<std::string> issues;
    if (!opt.baselineFile.empty())
    {
        const bool ok = mviewer::bench::baselineComparisonPassed(
            opt.baselineFile, opt.budget.profileName, results, Budget::higherIsBetter(), &issues);
        if (!ok)
            allPass = false;
    }
    if (!opt.historyFile.empty())
    {
        const auto baseline = opt.baselineFile.empty()
                                  ? std::unordered_map<std::string, double>{}
                                  : mviewer::bench::loadBaselineMetrics(opt.baselineFile);
        mviewer::bench::appendHistory(opt.historyFile, results, baseline);
    }
    if (!opt.reportFile.empty())
    {
        const auto baseline = opt.baselineFile.empty()
                                  ? std::unordered_map<std::string, double>{}
                                  : mviewer::bench::loadBaselineMetrics(opt.baselineFile);
        mviewer::bench::writeMarkdownReport(opt.reportFile, opt.baselineFile, results, baseline,
                                            issues);
    }
    flushTrace(opt.traceFile);
    if (opt.budget.enforce && !allPass)
        return 1;
    return 0;
}

int runBench(int argc, char **argv, BenchOptions opt)
{
    int exitCode = 0;
    const Prepared prep = prepareRun(opt, exitCode);
    if (exitCode != 0)
        return exitCode;

    std::unique_ptr<QCoreApplication> app;
    if (prep.needGui)
        app = std::make_unique<QApplication>(argc, argv);
    else
        app = std::make_unique<QCoreApplication>(argc, argv);
    // Isolate DiskCache from the user's real CacheLocation. DiskCache opens
    // mviewer_disk.db in its constructor (first instance() call); this must
    // run before any ImageRepository / CacheManager use. Test-mode only in
    // mviewer_bench — core_tests compile scenarios.cpp without this path.
    // Budget/corpus/baseline files are explicit CLI paths, not standard
    // locations, so setTestModeEnabled does not change what the harness reads.
    QStandardPaths::setTestModeEnabled(true);
    DiskCache::instance().clear();
    // B18/B19 start UI workers: join them before the app object goes away
    // (ADR 017 shared shutdown path; declared after `app` so it runs first).
    std::optional<mviewer::application::ScopedProcessShutdown> processShutdown;
    if (prep.needGui)
        processShutdown.emplace();

    printBanner(opt, prep);
    if (!opt.emitData.empty())
        return runEmit(opt);

    std::vector<mviewer::bench::ScenarioResult> results;
    bool allPass = true;
    if (!opt.corpusDir.empty())
    {
        const mviewer::bench::Corpus corpus = mviewer::bench::makeCorpusFromDir(opt.corpusDir);
        allPass = runCollected(opt, prep, corpus, results);
    }
    else
    {
        // Corpus generation is outside the per-scenario watchdog: 3000 images
        // can legitimately take a large slice of a passing run. The start line
        // is flushed so a hang here is visible before ctest's own kill.
        mviewer::bench::writeLine("[bench] start corpus");
        const double t0 = mviewer::bench::nowMs();
        mviewer::bench::Corpus corpus = mviewer::bench::makeCorpus(opt.corpusSize);
        const long long ms = static_cast<long long>(mviewer::bench::nowMs() - t0);
        mviewer::bench::writeLine("[bench] done corpus in " + std::to_string(ms) + " ms");
        warnIfCorpusIncomplete(corpus, true);
        allPass = runCollected(opt, prep, corpus, results);
        corpus.clear();
    }
    mviewer::bench::writeLine(std::string("=== ") + (allPass ? "ALL PASS" : "SOME FAIL") + " ===");
    return finishBenchRun(opt, allPass, results);
}

} // namespace

int main(int argc, char **argv)
{
    mviewer::bench::installBenchIo();
    const BenchSession session;
    BenchOptions opt;
    parseArgs(argc, argv, opt);
    return runBench(argc, argv, std::move(opt));
}
