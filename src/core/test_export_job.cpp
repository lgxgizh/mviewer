// M21: ExportJob unit tests — config helpers + convert path smoke.
#include "core/export/ExportJob.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"

#include <QCoreApplication>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL: %s\n", msg);                                                        \
            ++g_failures;                                                                          \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::printf("PASS: %s\n", msg);                                                        \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv)
{
    // Required so Qt resolves imageformat plugins (qjpeg) relative to the test
    // executable directory; without an app object JPEG encode fails silently.
    QCoreApplication app(argc, argv);
    using namespace mviewer::exportjob;

    CHECK(modeName(Mode::Convert) == "convert", "modeName convert");
    CHECK(modeFromName("contact") == Mode::ContactSheet, "modeFromName contact");
    CHECK(modeFromName("html") == Mode::HtmlReport, "modeFromName html");

    // Empty sources → graceful failure.
    {
        ExportJobConfig cfg;
        cfg.mode = Mode::Convert;
        cfg.outDir = ".";
        auto r = run(cfg);
        CHECK(r.done == 0 && r.total == 0, "empty sources: zero done");
        CHECK(r.message.find("no sources") != std::string::npos, "empty sources message");
    }

    // Non-convert modes now share the same worker-side ExportJob entry point.
    {
        ExportJobConfig cfg;
        cfg.mode = Mode::Csv;
        cfg.sources = {"a.jpg"};
        cfg.outDir = ".";
        auto r = run(cfg);
        CHECK(r.message.find("delegated") == std::string::npos,
              "csv is handled by the unified job path");
    }

    // Convert smoke: write a tiny PNG source, re-export as JPEG via ExportJob.
    {
        const fs::path tmp = fs::temp_directory_path() / "mviewer_exportjob_test";
        fs::create_directories(tmp);
        const fs::path src = tmp / "photo.v2.png"; // dotted base name
        const fs::path out = tmp / "out";
        fs::create_directories(out);

        ImageData img = makeImageData(32, 24, PixelFormat::RGB24);
        // Fill with a solid color so the encoder has real pixels.
        auto &bytes = *img.buffer;
        for (size_t i = 0; i + 2 < bytes.size(); i += 3)
        {
            bytes[i] = 200;
            bytes[i + 1] = 100;
            bytes[i + 2] = 50;
        }
        const bool encoded = Encoder::encode(img, src.string(), Encoder::Params{90});
        CHECK(encoded, "seed source png");

        ExportJobConfig cfg;
        cfg.mode = Mode::Convert;
        cfg.sources = {src.string()};
        cfg.outDir = out.string();
        cfg.format = "jpeg";
        cfg.quality = 85;
        cfg.renamePattern = "{name}_exp";
        auto r = run(cfg);
        CHECK(r.done == 1, "convert done=1");
        CHECK(r.failed == 0, "convert failed=0");
        // Dotted base name must be preserved (photo.v2_exp.jpg, not photo_exp.jpg).
        CHECK(fs::exists(out / "photo.v2_exp.jpg") ||
                  r.primaryOutput.find("photo.v2_exp") != std::string::npos,
              "convert preserves dotted base name");

        // M40: viewer Save As uses an explicit destination but still runs
        // through the worker-side Convert contract.
        ExportJobConfig explicitCfg;
        explicitCfg.mode = Mode::Convert;
        explicitCfg.sources = {src.string()};
        explicitCfg.outDir = out.string();
        explicitCfg.destinationPath = (out / "explicit-save.png").string();
        explicitCfg.format = "png";
        explicitCfg.preserveDisplayAppearance = true;
        const auto explicitResult = run(explicitCfg);
        CHECK(explicitResult.done == 1 && fs::exists(out / "explicit-save.png"),
              "M40: explicit Save As destination is handled off the UI path");

        ExportJobConfig clipboardCfg;
        clipboardCfg.mode = Mode::Clipboard;
        clipboardCfg.sources = {src.string()};
        clipboardCfg.preserveDisplayAppearance = true;
        const auto clipboardResult = run(clipboardCfg);
        CHECK(clipboardResult.done == 1 && !clipboardResult.clipboardImage.isNull(),
              "M40: clipboard job returns a display-ready worker image");

        auto alreadyCancelled = std::make_shared<std::atomic<bool>>(true);
        ExportJobConfig cancelledCfg;
        cancelledCfg.mode = Mode::Convert;
        cancelledCfg.sources = {src.string()};
        cancelledCfg.outDir = out.string();
        cancelledCfg.format = "png";
        cancelledCfg.cancel = alreadyCancelled;
        cancelledCfg.renamePattern = "cancelled-output";
        const auto cancelledResult = run(cancelledCfg);
        CHECK(cancelledResult.done == 0 &&
                  cancelledResult.message.find("cancelled") != std::string::npos,
              "M40: pre-cancelled ExportJob produces no stale output");

        ExportJobConfig budgetCfg;
        budgetCfg.mode = Mode::ContactSheet;
        budgetCfg.sources = {src.string()};
        budgetCfg.outDir = out.string();
        budgetCfg.contactThumb = 32;
        budgetCfg.stagingMemoryBudgetBytes = 1;
        const auto budgetResult = run(budgetCfg);
        CHECK(budgetResult.done == 0 &&
                  budgetResult.message.find("staging memory budget") != std::string::npos,
              "M40: Contact/PDF staging memory has a hard bounded contract");

        ExportJobConfig directoryCfg;
        directoryCfg.mode = Mode::Csv;
        directoryCfg.sourceDirectory = tmp.string();
        directoryCfg.outDir = out.string();
        const auto directoryResult = run(directoryCfg);
        CHECK(directoryResult.done == 1 && directoryResult.failed == 0,
              "worker-side source directory enumeration completes");

        for (const Mode mode : {Mode::ContactSheet, Mode::Pdf, Mode::Csv, Mode::Json,
                                Mode::HtmlReport})
        {
            ExportJobConfig reportCfg;
            reportCfg.mode = mode;
            reportCfg.sources = {src.string()};
            reportCfg.outDir = out.string();
            reportCfg.contactCols = 1;
            reportCfg.contactThumb = 32;
            const auto reportResult = run(reportCfg);
            CHECK(reportResult.done == 1 && reportResult.failed == 0,
                  "all export modes complete through ExportJob");
        }

        std::error_code ec;
        fs::remove_all(tmp, ec);
    }

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "HAS FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
