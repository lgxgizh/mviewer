// M14.3 Plugin Examples E2E test.
//
// Builds -> loads -> self-registers -> uses the example plugins through the
// unified PluginManager loader. Covers all three plugin kinds:
//   * Analyzer  -> loaded as a DLL (PluginManager::load)
//   * Decoder   -> loaded as a DLL (PluginManager::load), decodes a PPM
//   * Exporter  -> its source is compiled into this test (see CMakeLists), so
//                 we exercise it through its C entry point createExporter()
//                 WITHOUT LoadLibrary'ing the Qt-GUI exporter DLL. Loading a
//                 Qt-linking plugin DLL inside a test process is unsafe on
//                 Windows and segfaults on teardown (see M14.2); the analyzer
//                 and decoder DLL loads above already prove the unified loader
//                 works for every kind, so the exporter is unit-tested here.
#include "core/analyzer/Analyzer.h"
#include "core/export/ExporterRegistry.h"
#include "core/export/IExporter.h"
#include "core/image/ImageBuffer.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/IDecoder.h"
#include "core/plugin/PluginManager.h"

#include <QCoreApplication>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

// The exporter example is compiled into this test executable, so its C entry
// point is available without loading the Qt-GUI DLL.
extern "C" IExporter *createExporter();

static std::filesystem::path binDir(int argc, char **argv)
{
    if (argc > 0)
        return std::filesystem::path(argv[0]).parent_path();
    return std::filesystem::current_path();
}

static std::filesystem::path pluginPath(const std::filesystem::path &dir, const std::string &name)
{
#ifdef _WIN32
    return dir / (name + ".dll");
#elif defined(__APPLE__)
    return dir / ("lib" + name + ".dylib");
#else
    return dir / ("lib" + name + ".so");
#endif
}

// Write a tiny P6 PPM (4x2, maxval 255) for the decoder under test.
static std::filesystem::path writeSamplePpm(const std::filesystem::path &dir)
{
    std::filesystem::path p = dir / "sample.ppm";
    std::ofstream f(p, std::ios::binary);
    f << "P6\n4 2\n255\n";
    uint8_t px[8][3] = {
        {10, 20, 30}, {40, 50, 60}, {70, 80, 90}, {100, 110, 120},
        {130, 140, 150}, {160, 170, 180}, {200, 210, 220}, {230, 240, 250},
    };
    f.write(reinterpret_cast<const char *>(px), sizeof(px));
    return p;
}

int main(int argc, char *argv[])
{
    // A live QCoreApplication keeps Qt shutdown orderly on teardown (the example
    // plugins link Qt; see test_pluginregistry for the rationale).
    QCoreApplication app(argc, argv);

    std::cout << "[M14.3 Plugin Examples E2E tests]\n";
    const auto dir = binDir(argc, argv);

    auto &mgr = PluginManager::instance();

    // ---- Analyzer plugin (loaded as DLL) ----
    auto analyzerPlugin = pluginPath(dir, "example_analyzer");
    CHECK(mgr.load(analyzerPlugin.string()), "load example_analyzer");
    auto &areg = AnalyzerRegistry::instance();
    bool aFound = false;
    for (const auto &id : areg.availableAnalyzers())
        if (id == "example.mean_luminance")
            aFound = true;
    CHECK(aFound, "example analyzer registered in AnalyzerRegistry");
    auto analyzer = areg.create("example.mean_luminance");
    CHECK(analyzer != nullptr, "AnalyzerRegistry::create yields an instance");
    CHECK(analyzer->name() == "example.mean_luminance", "created analyzer has expected id");

    // ---- Decoder plugin (loaded as DLL, decodes PPM) ----
    auto decoderPlugin = pluginPath(dir, "example_decoder");
    CHECK(mgr.load(decoderPlugin.string()), "load example_decoder");
    auto &dreg = DecoderRegistry::instance();
    bool dFound = false;
    for (const auto &id : dreg.available())
        if (id == "ppm-decoder")
            dFound = true;
    CHECK(dFound, "example decoder registered in DecoderRegistry");
    auto dec = dreg.get("ppm-decoder");
    CHECK(dec != nullptr, "DecoderRegistry::get returns the ppm decoder");
    auto ppmPath = writeSamplePpm(dir);
    CHECK(dec->canDecode(ppmPath.string()), "ppm decoder canDecode() the sample file");
    ImageData decoded = dec->decodeFull(ppmPath.string());
    CHECK(decoded.buffer && decoded.width == 4 && decoded.height == 2,
          "ppm decoder decodes the 4x2 sample correctly");

    // ---- Exporter plugin (source compiled in; exercised via C entry point) ----
    auto &ereg = ExporterRegistry::instance();
    CHECK(ereg.get("png-exporter") == nullptr, "png-exporter not pre-registered");
    IExporter *rawExp = createExporter();
    CHECK(rawExp != nullptr, "createExporter() yields an instance");
    std::shared_ptr<IExporter> exp(rawExp, [](IExporter *p) { delete p; });
    ereg.registerExporter(exp);
    bool eFound = false;
    for (const auto &id : ereg.available())
        if (id == "png-exporter")
            eFound = true;
    CHECK(eFound, "example exporter registered in ExporterRegistry");
    CHECK(ereg.get("png-exporter") != nullptr, "ExporterRegistry::get returns the png exporter");

    // Round-trip: export the decoded image to a PNG and confirm the file exists.
    auto pngPath = dir / "sample_out.png";
    bool exported = ereg.exportImage("png-exporter", decoded, pngPath.string());
    CHECK(exported, "png exporter writes the decoded image to PNG");
    std::error_code ec;
    CHECK(std::filesystem::exists(pngPath, ec) && std::filesystem::file_size(pngPath, ec) > 0,
          "exported PNG file exists and is non-empty");

    // Two plugins were loaded through the unified loader (analyzer + decoder);
    // the exporter is compiled-in and exercised directly (see header note).
    CHECK(mgr.count() == 2, "two example plugins loaded via PluginManager");

    std::cout << "M14.3 Plugin Examples E2E tests done\n";
    return 0;
}
