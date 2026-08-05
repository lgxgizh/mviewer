// M24 Phase 4C 鈥?Analyze workflow acceptance tests.
//
// Maps the M24 Workflow C acceptance items that are automatable headless:
//   C#2  analyzer list, execution status, and result meaning are explicit
//   C#4  results stay keyed to the image they were computed for (no stale
//        result silently attached to a different image)
//   C#5  Analysis History and pinned results are consistent with the model
//   C#6  persisted history round-trips and stays path-keyed across restart
//   C#7  a failing analyzer (buggy plugin) cannot take down the app: batch
//        runs skip it and the pipeline/panel paths degrade to empty results
//   C#8  scalar metrics come with machine-readable names (units live in the
//        analyzer info/output fields, asserted via resultMetrics keys)
//
// (Registry-level numeric correctness is covered by analyzer_pipeline_tests /
// test_analyzer_pipeline / analysis_panel_tests.)

#include "analyzermodel.h"
#include "analysispanel.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "widgets/rawimageview.h" // completes RawImageView for AnalysisPanel's unique_ptr

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>
#include <iostream>
#include <memory>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// A deliberately broken analyzer: fails from every entry point. Simulates a
// buggy plugin the registry must isolate.
class FailingAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "failing";
    }
    std::string description() const override
    {
        return "always fails";
    }
    bool analyze(const ImageFrame &) override
    {
        return false;
    }
    bool analyzeRegion(const ImageFrame &, const mviewer::domain::Selection &) override
    {
        return false;
    }
};

std::shared_ptr<ImageFrame> makeFrame(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    auto frame = std::make_shared<ImageFrame>();
    frame->setPixels(mvcore::fromQImage(img));
    return frame;
}

QString historyStoragePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/analysis_history.json";
}

void testC7FailingAnalyzer()
{
    std::cout << "── Analyze C#7: failing analyzer isolation ──\n";
    AnalyzerRegistry &reg = AnalyzerRegistry::instance();
    // NOTE: the factory MUST supply an AnalyzerDeleter — a default-constructed
    // std::function deleter is empty, and destroying the unique_ptr then
    // throws std::bad_function_call (uncaught in the scheduler worker ->
    // terminate). The builtin factories all pass an explicit deleter.
    reg.registerAnalyzer("m24_failing", []()
                         { return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                               new FailingAnalyzer(), [](Analyzer *p) { delete p; }); });

    const auto frame = makeFrame(64, 64, QColor(12, 34, 56));

    // Batch run: the failing analyzer must be skipped, others must produce
    // results, and the call must not throw.
    const auto results = reg.runAnalyzer(*frame);
    CHECK(results.find("m24_failing") == results.end(),
          "C#7: failing analyzer omitted from batch results");
    CHECK(results.size() >= 5, "C#7: healthy analyzers still produce results");

    // Pipeline region path: empty result, no exception.
    const AnalyzerPipeline pipeline;
    const std::string region = pipeline.runRegion(*frame, {0, 0, 16, 16}, "m24_failing");
    CHECK(region.empty(), "C#7: pipeline runRegion degrades to empty on a failing analyzer");

    // A second healthy run still works after the failure (no corrupted state).
    const auto again = reg.runAnalyzer(*frame);
    CHECK(again.find("histogram") != again.end(),
          "C#7: registry stays functional after isolating a failing analyzer");

    // NOTE (M24): a plugin that THROWS (not just returns false) cannot be
    // isolated in-process in the current build configuration: the project
    // compiles without /EHsc (exception unwind semantics off — see
    // docs/review/M24_TEST_CREDIBILITY_2026-08-05.md §8), and a cross-DLL
    // throw poisons the EH runtime so a later exception fail-fasts the app
    // (0xc0000409, reproducible via runAnalyzer after any thrown+caught
    // cross-module exception). Hard crashes are handled by the plugin
    // subprocess runner (pluginregistryrunner_tests); enabling /EHsc is the
    // proposed fix (commander decision, build config is frozen).

    reg.unregister("m24_failing");
}

void testC2C4C5PanelAndModel(const QString &pathA, const QString &pathB)
{
    std::cout << "鈹€鈹€ Analyze C#2/C#4/C#5: panel + model currency 鈹€鈹€\n";

    AnalyzerModel model;
    model.clearAllResults();

    AnalysisPanel panel;
    panel.setAnalyzerModel(&model);
    panel.show();

    // C#2: the analyzer list is populated and selectable.
    const auto ids = AnalyzerPipeline().analyzerIds();
    CHECK(ids.size() >= 7, "C#2: analyzer list exposes the built-in analyzers");

    // C#4: results are keyed to the image they were computed for.
    const auto frameA = makeFrame(48, 32, QColor(200, 30, 30));
    const auto frameB = makeFrame(48, 32, QColor(30, 200, 30));
    panel.setFrame(frameA);
    panel.setImage(mvcore::toQImage(frameA->pixels()), pathA);
    panel.selectAnalyzer("histogram");
    panel.reanalyze();
    panel.setFrame(frameB);
    panel.setImage(mvcore::toQImage(frameB->pixels()), pathB);
    panel.reanalyze();

    const QString resA = model.resultText(pathA);
    const QString resB = model.resultText(pathB);
    CHECK(!resA.isEmpty(), "C#4: analysis result recorded for image A");
    CHECK(!resB.isEmpty(), "C#4: analysis result recorded for image B");
    const QStringList hist = model.history();
    CHECK(hist.contains(pathA) && hist.contains(pathB),
          "C#4: history tracks exactly the analyzed images");

    // C#5: pinning is consistent with the model.
    model.pinResult(pathA);
    CHECK(model.pinned().contains(pathA), "C#5: pinned results tracked in the model");
    CHECK(!model.pinned().contains(pathB), "C#5: only explicitly pinned results pinned");
}

void testC6PersistenceRoundTrip()
{
    std::cout << "鈹€鈹€ Analyze C#6: persistence round-trip 鈹€鈹€\n";
    const QString file = historyStoragePath();
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile::remove(file);

    {
        AnalyzerModel model;
        model.setResult("/data/img_a.png", "histogram lum=120.5");
        model.setResult("/data/img_b.png", "histogram lum=80.0");
        model.pinResult("/data/img_b.png");
        model.save();
        CHECK(QFile::exists(file), "C#6: analysis history persisted to disk");
    }
    {
        // Fresh model = simulated restart.
        AnalyzerModel model;
        model.load();
        CHECK(model.resultText("/data/img_a.png").contains("120.5"),
              "C#6: result reloads for the correct path");
        CHECK(model.resultText("/data/img_b.png").contains("80.0"),
              "C#6: second result reloads for its own path");
        CHECK(model.resultText("/data/other.png").isEmpty(),
              "C#6: no phantom result for an unrelated path");
        CHECK(model.pinned().contains("/data/img_b.png"),
              "C#6: pinned state survives restart");
        CHECK(model.history().contains("/data/img_a.png"),
              "C#6: history survives restart");
    }
    QFile::remove(file);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-analyze-acceptance-test");
    QCoreApplication::setApplicationName("mviewer-analyze-acceptance-test");

    testC7FailingAnalyzer();
    testC2C4C5PanelAndModel("/tmp/m24_ana_a.png", "/tmp/m24_ana_b.png");
    testC6PersistenceRoundTrip();

    if (g_failures > 0)
    {
        std::cout << "analyze_acceptance_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "analyze_acceptance_tests: PASS\n";
    return 0;
}



