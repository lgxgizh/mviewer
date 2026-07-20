// M13.4 — Batch multi-image analyzer -> CSV/JSON export.
// Headless: build synthetic frames, run one analyzer over the batch via the
// registry, assemble an AnalysisBatchReport, and assert the structured metrics
// + serialization are well-formed.
#include "core/analysis/ExportReport.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerResult.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
        fflush(stdout);                                                                            \
    } while (0)

namespace
{

std::shared_ptr<ImageFrame> makeFrame(const std::string &path, int w, int h,
                                      const std::function<void(uint8_t *, int, int, int)> &fill)
{
    mviewer::domain::ImageMetadata meta;
    meta.filePath = path;
    meta.width = w;
    meta.height = h;
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    auto view = d.view();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            fill(view.data + static_cast<size_t>(y) * view.stride() +
                     static_cast<size_t>(x) * view.channelsPerPixel(),
                 x, y, w);
    return std::make_shared<ImageFrame>(meta, d);
}

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

static void testHistogramBatch()
{
    printf("\n[batch histogram -> report]\n");
    fflush(stdout);

    std::vector<std::pair<std::string, std::shared_ptr<ImageFrame>>> frames;
    frames.emplace_back("dark.png", makeFrame("dark.png", 16, 16,
                                              [](uint8_t *p, int, int, int)
                                              { p[0] = p[1] = p[2] = 40; }));
    frames.emplace_back("bright.png", makeFrame("bright.png", 16, 16,
                                                [](uint8_t *p, int, int, int)
                                                { p[0] = p[1] = p[2] = 200; }));
    // A null frame must be skipped, not crash.
    frames.emplace_back("missing.png", std::shared_ptr<ImageFrame>());

    const auto results = ::AnalyzerRegistry::instance().runBatch(frames, "histogram");
    CHECK(results.size() == 2, "runBatch skips the null frame (2 results)");
    if (results.size() == 2)
    {
        CHECK(results[0].metrics.count("lumMean") == 1, "result 0 has lumMean metric");
        CHECK(results[1].metrics.count("lumMean") == 1, "result 1 has lumMean metric");
        CHECK(results[0].metrics.at("lumMean") < results[1].metrics.at("lumMean"),
              "dark image lumMean < bright image lumMean");
        CHECK(!results[0].detail.empty(), "result carries human-readable detail");
    }

    const auto report = mviewer::core::buildBatchReport("histogram", results);
    CHECK(report.analyzerId == "histogram", "report analyzerId preserved");
    CHECK(report.filenames.size() == 2, "report has 2 filenames");
    CHECK(!report.columns.empty(), "report has metric columns");
    bool hasLum = false;
    for (const auto &c : report.columns)
        if (c == "lumMean")
            hasLum = true;
    CHECK(hasLum, "columns include lumMean");

    const std::string csv = report.toCsv();
    printf("---- CSV ----\n%s\n", csv.c_str());
    fflush(stdout);
    CHECK(contains(csv, "filename"), "CSV header starts with filename");
    CHECK(contains(csv, "lumMean"), "CSV header lists lumMean column");
    CHECK(contains(csv, "dark.png"), "CSV contains dark.png row");
    CHECK(contains(csv, "bright.png"), "CSV contains bright.png row");

    const std::string json = report.toJson();
    printf("---- JSON ----\n%s\n", json.c_str());
    fflush(stdout);
    CHECK(contains(json, "\"analyzer\": \"histogram\""), "JSON names analyzer");
    CHECK(contains(json, "\"columns\""), "JSON has columns array");
    CHECK(contains(json, "\"rows\""), "JSON has rows array");
    CHECK(contains(json, "dark.png"), "JSON contains dark.png");
    CHECK(contains(json, "bright.png"), "JSON contains bright.png");
    CHECK(contains(json, "lumMean"), "JSON contains lumMean key");
}

static void testUnknownAnalyzer()
{
    printf("\n[batch unknown analyzer id]\n");
    fflush(stdout);
    std::vector<std::pair<std::string, std::shared_ptr<ImageFrame>>> frames;
    frames.emplace_back("a.png", makeFrame("a.png", 8, 8,
                                           [](uint8_t *p, int, int, int)
                                           { p[0] = p[1] = p[2] = 100; }));
    const auto results = ::AnalyzerRegistry::instance().runBatch(frames, "does_not_exist");
    CHECK(results.empty(), "unknown analyzer id -> empty results");
    const auto report = mviewer::core::buildBatchReport("does_not_exist", results);
    CHECK(report.filenames.empty(), "empty report has no rows");
    // Serialization must still be well-formed on an empty report.
    CHECK(contains(report.toJson(), "\"rows\""), "empty JSON still valid");
    CHECK(contains(report.toCsv(), "filename"), "empty CSV still has header");
}

static void testAvailableAnalyzers()
{
    printf("\n[registry availableAnalyzers]\n");
    fflush(stdout);
    const auto ids = ::AnalyzerRegistry::instance().availableAnalyzers();
    CHECK(ids.size() >= 10, "registry exposes >= 10 analyzers");
    bool hasHist = false;
    for (const auto &id : ids)
        if (id == "histogram")
            hasHist = true;
    CHECK(hasHist, "availableAnalyzers() lists histogram");
}

int main()
{
    printf("=== M13.4 batch analyzer export ===\n");
    fflush(stdout);
    testHistogramBatch();
    testUnknownAnalyzer();
    testAvailableAnalyzers();
    printf("\n=== M13.4 batch analyzer export: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
