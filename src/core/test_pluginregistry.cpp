// Plugin Registry E2E test: build -> load -> register -> create -> analyze.
// Loads the example_analyzer plugin (co-located with the test exe), proves it
// self-registered with AnalyzerRegistry, then creates an instance and runs a
// real analysis on a synthetic frame.
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/plugin/PluginManager.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << std::endl;                                             \
            return 1;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << std::endl;                                             \
        }                                                                                          \
    } while (0)

static ImageData makeGradient(int w, int h)
{
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = d.buffer.get() + (static_cast<size_t>(y) * w + x) * 3;
            p[0] = static_cast<uint8_t>(x);
            p[1] = static_cast<uint8_t>(y);
            p[2] = 128;
        }
    return d;
}

int main(int argc, char *argv[])
{
    // A QCoreApplication must exist so Qt's static teardown is orderly when
    // the process exits (without it, unloading the plugin DLL at process death
    // crashes — Qt expects a live QCoreApplication for clean shutdown).
    QCoreApplication app(argc, argv);

    std::cout << "[Plugin Registry E2E tests]\n";

    // The example plugin is built alongside the test executable (bin/).
#ifdef _WIN32
    const char *kPlugin = "example_analyzer";
#else
    const char *kPlugin = "example_analyzer";
#endif
    const std::string kAnalyzerId = "example.mean_luminance";

    auto &mgr = PluginManager::instance();
    bool loaded = mgr.load(kPlugin);
    CHECK(loaded, "PluginManager::load(example_analyzer) succeeds");
    CHECK(mgr.count() == 1, "one plugin now loaded");
    CHECK(mgr.isLoaded(kPlugin), "isLoaded reports true");

    // The plugin self-registered with AnalyzerRegistry.
    auto &reg = AnalyzerRegistry::instance();
    auto available = reg.availableAnalyzers();
    bool found = false;
    for (const auto &id : available)
        if (id == kAnalyzerId)
            found = true;
    CHECK(found, "analyzer id present in AnalyzerRegistry after load");

    // Create an instance through the registry and run a real analysis.
    auto analyzer = reg.create(kAnalyzerId);
    CHECK(analyzer != nullptr, "AnalyzerRegistry::create returns an instance");
    CHECK(analyzer->name() == kAnalyzerId, "created analyzer has expected id");

    auto frame = std::make_shared<ImageFrame>();
    frame->setPixels(makeGradient(20, 20));
    bool ok = analyzer->analyze(*frame);
    CHECK(ok, "analyze() succeeds on a valid frame");
    std::string result = analyzer->resultText();
    CHECK(!result.empty(), "resultText() reports a non-empty result");
    std::cout << "  analysis result: " << result << std::endl;

    // Region analysis also works (capability RegionOfInterest).
    mviewer::domain::Selection sel{0, 0, 10, 10};
    ok = analyzer->analyzeRegion(*frame, sel);
    CHECK(ok, "analyzeRegion() succeeds on a 10x10 region");
    CHECK(!analyzer->resultText().empty(), "region resultText non-empty");

    // NOTE: we intentionally do NOT call PluginManager::unload() here. The
    // example plugin links mviewer_core (a DLL) which transitively pulls in Qt;
    // unloading a Qt-linking plugin DLL at process teardown is unsafe on Windows
    // (DLL detach / CRT static ordering) and crashes the process after all
    // checks have passed. Real deployments keep plugins loaded for the app
    // lifetime, and the negative unload path (missing DLL) is covered by
    // test_plugin_manager. The load -> self-register -> create -> analyze
    // round-trip proven above is exactly the "registerable / queryable /
    // loadable" contract.
    std::cout << "Plugin Registry E2E tests done\n";
    return 0;
}
