// M21: ExportJob unit tests — config helpers + convert path smoke.
#include "core/export/ExportJob.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"

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

int main()
{
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

    // Non-convert modes are delegated (UI still owns specialized paths).
    {
        ExportJobConfig cfg;
        cfg.mode = Mode::Csv;
        cfg.sources = {"a.jpg"};
        cfg.outDir = ".";
        auto r = run(cfg);
        CHECK(r.message.find("delegated") != std::string::npos, "csv delegated");
    }

    // Convert smoke: write a tiny PNG source, re-export as JPEG via ExportJob.
    {
        const fs::path tmp = fs::temp_directory_path() / "mviewer_exportjob_test";
        fs::create_directories(tmp);
        const fs::path src = tmp / "src.png";
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
        CHECK(fs::exists(out / "src_exp.jpg") || !r.primaryOutput.empty(), "convert output exists");

        std::error_code ec;
        fs::remove_all(tmp, ec);
    }

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "HAS FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
