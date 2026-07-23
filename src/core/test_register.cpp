// P2 — AnalyzerRegistry extensibility test.
// Proves the review's acceptance criterion: a NEW analyzer can be added at
// runtime via registerAnalyzer() and is discoverable / runnable WITHOUT any
// change to MainWindow or any existing UI code (the UI auto-generates from
// the registry). This test registers a custom analyzer and verifies it shows
// up in availableAnalyzers(), can be created, queried by capability, and runs.
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "domain/Image.h"

#include <QCoreApplication>

#include <cstdio>
#include <string>

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
    } while (0)

namespace
{
// A brand-new analyzer type, defined ONLY here — no MainWindow / UI change.
class SumAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "Test Sum";
    }
    std::string description() const override
    {
        return "Sums all pixel channels (test).";
    }
    bool analyze(const ImageFrame &) override
    {
        m_sum = 123; // pretend work
        return true;
    }
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &) override
    {
        return analyze(frame);
    }
    std::string resultText() const override
    {
        return "sum=" + std::to_string(m_sum);
    }
    AnalyzerCapability capabilities() const override
    {
        return AnalyzerCapability::SingleImage;
    }

  private:
    int m_sum = 0;
};
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("\n[P2 AnalyzerRegistry]\n");

    Analyzer::registerBuiltins(); // normal setup, as the app does at startup
    auto &reg = AnalyzerRegistry::instance();

    const size_t before = reg.availableAnalyzers().size();

    // Register a NEW analyzer — the only change needed to extend analysis.
    reg.registerAnalyzer("test.custom.sum",
                         []
                         {
                             return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                 new SumAnalyzer(), [](Analyzer *p) { delete p; });
                         });

    const auto ids = reg.availableAnalyzers();
    bool found = false;
    for (const auto &id : ids)
        if (id == "test.custom.sum")
            found = true;
    CHECK(found, "new analyzer appears in availableAnalyzers()");
    CHECK(ids.size() == before + 1, "registry grew by exactly one");

    auto inst = reg.create("test.custom.sum");
    CHECK(static_cast<bool>(inst), "create() returns a live instance");
    CHECK(inst && inst->name() == "Test Sum", "created instance has correct name");

    auto info = reg.infoFor("test.custom.sum");
    CHECK(info.has_value() && info->name == "Test Sum", "infoFor() returns the new analyzer");

    const auto byCap = reg.queryByCapability(AnalyzerCapability::SingleImage);
    bool inQuery = false;
    for (const auto &id : byCap)
        if (id == "test.custom.sum")
            inQuery = true;
    CHECK(inQuery, "queryByCapability(SingleImage) includes the new analyzer");

    // Run it end-to-end on a real frame; the UI does exactly this generically.
    ImageData img = makeImageData(2, 2, PixelFormat::RGB24);
    auto *p = img.buffer->data();
    for (int i = 0; i < 2 * 2; ++i)
    {
        p[i * 3 + 0] = 10;
        p[i * 3 + 1] = 20;
        p[i * 3 + 2] = 30;
    }
    mviewer::domain::ImageMetadata meta;
    meta.width = 2;
    meta.height = 2;
    ImageFrame frame(meta, img);

    const auto results = reg.runAnalyzer(frame);
    const auto it = results.find("test.custom.sum");
    CHECK(it != results.end(), "runAnalyzer() executed the new analyzer");
    CHECK(it != results.end() && it->second == "sum=123", "new analyzer produced its result text");

    printf("\n=== P2 AnalyzerRegistry: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
