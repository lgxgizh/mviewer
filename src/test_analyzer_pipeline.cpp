// M15 P0#3 — Analyzer Pipeline acceptance test.
//
// Acceptance: "新增一个 Analyzer，MainWindow 0 修改".
//
// This test proves the decoupling by registering a brand-new analyzer through
// the AnalyzerRegistry and verifying that the AnalyzerPipeline picks it up
// automatically. None of the MainWindow / AnalysisPanel source is touched here,
// mirroring how a real new analyzer is added in production (register via the
// registry only). If the pipeline still routed through MainWindow, this test
// would have to involve MainWindow — it does not.
//
// It is intentionally structured to mirror the passing P2 registry test
// (core/test_register.cpp) so the only variable under test is the
// AnalyzerPipeline facade itself.

#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "domain/Image.h"

#include <QCoreApplication>

#include <cstdio>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                             \
    do                                                                                               \
    {                                                                                                \
        if (cond)                                                                                    \
        {                                                                                            \
            printf("  PASS: %s\n", msg);                                                             \
            g_pass++;                                                                                \
        }                                                                                            \
        else                                                                                         \
        {                                                                                            \
            printf("  FAIL: %s\n", msg);                                                             \
            g_fail++;                                                                                \
        }                                                                                            \
    } while (0)

namespace
{
// A brand-new analyzer type, defined ONLY here — no MainWindow / UI change.
class PipelineTestAnalyzer : public Analyzer
{
  public:
    static constexpr char ID[] = "pipeline_test_analyzer";
    std::string name() const override { return ID; }
    std::string description() const override { return "M15 P0#3 pipeline test"; }
    bool analyze(const ImageFrame &) override
    {
        m_text = "ran:" + std::string(ID);
        return true;
    }
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &) override
    {
        return analyze(frame);
    }
    std::string resultText() const override { return m_text; }

  private:
    std::string m_text;
};
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("\n[M15 P0#3 AnalyzerPipeline]\n");

    Analyzer::registerBuiltins(); // normal setup, as the app does at startup

    // Register a NEW analyzer — the only change needed to extend analysis.
    AnalyzerRegistry::instance().registerAnalyzer(
        PipelineTestAnalyzer::ID,
        []() -> std::unique_ptr<Analyzer, AnalyzerDeleter> {
            return std::unique_ptr<Analyzer, AnalyzerDeleter>(new PipelineTestAnalyzer(),
                                                             [](Analyzer *p) { delete p; });
        });

    AnalyzerPipeline pipeline;

    // 1) The pipeline surfaces the newly registered analyzer without any
    //    MainWindow / AnalysisPanel code change.
    const auto ids = pipeline.analyzerIds();
    bool found = false;
    for (const auto &id : ids)
        if (id == PipelineTestAnalyzer::ID)
            found = true;
    CHECK(found, "pipeline surfaces a newly registered analyzer");

    // 2) The pipeline creates the analyzer via the registry facade.
    auto inst = pipeline.create(PipelineTestAnalyzer::ID);
    CHECK(static_cast<bool>(inst), "pipeline.create() returns a live instance");
    CHECK(inst && inst->name() == PipelineTestAnalyzer::ID,
          "pipeline-created instance has the correct id");

    // 3) Region analysis through the pipeline facade also works (it creates and
    //    drives the analyzer via the registry, never MainWindow).
    mviewer::domain::ImageMetadata meta;
    meta.width = 2;
    meta.height = 2;
    ImageData img = makeImageData(2, 2, PixelFormat::RGB24);
    auto *p = img.buffer->data();
    for (int i = 0; i < 2 * 2; ++i)
    {
        p[i * 3 + 0] = 10;
        p[i * 3 + 1] = 20;
        p[i * 3 + 2] = 30;
    }
    ImageFrame frame(meta, img);
    mviewer::domain::Selection roi(0, 0, 2, 2);
    const std::string regionText = pipeline.runRegion(frame, roi, PipelineTestAnalyzer::ID);
    CHECK(!regionText.empty(), "pipeline.runRegion() executed the new analyzer");

    printf("\n=== M15 P0#3 AnalyzerPipeline: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
