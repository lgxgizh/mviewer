// M9-4 acceptance: the Export workflow must produce a compare analysis report
// (JSON + CSV) and a diff heatmap PNG from two images. This exercises the REAL
// export path (core::buildCompareReport + core::compareDiffImage + Encoder) —
// it does not fake the result.
//
// Scope is M9-4 ONLY. Browse / Compare / Analysis / Workspace / Polish are
// other phases and are NOT touched here.
#include "core/analysis/ExportReport.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/image/QtConvert.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>

#include <cstdio>
#include <filesystem>
#include <fstream>
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

static QImage makeColorTest(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, c.rgb());
    return img;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    printf("\n[M9-4 Export: compare report JSON/CSV + diff PNG]\n");
    fflush(stdout);

    namespace fs = std::filesystem;
    const fs::path outDir = fs::temp_directory_path() / "mviewer_m9_export";
    std::error_code ec;
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);

    // Two slightly different images (B is darker) so PSNR/SSIM are finite.
    QImage a = makeColorTest(128, 96, QColor(200, 100, 50));
    QImage b = makeColorTest(128, 96, QColor(180, 90, 45));
    ImageData da = mvcore::fromQImage(a);
    ImageData db = mvcore::fromQImage(b);
    CHECK(!da.isNull() && !db.isNull(), "both ImageData valid");

    mviewer::domain::ImageMetadata ma, mb;
    ma.filePath = "a.png";
    ma.fileName = "a.png";
    ma.width = da.width;
    ma.height = da.height;
    mb.filePath = "b.png";
    mb.fileName = "b.png";
    mb.width = db.width;
    mb.height = db.height;
    ImageFrame fa(ma, da);
    ImageFrame fb(mb, db);

    const mviewer::core::CompareReport report = mviewer::core::buildCompareReport(fa, fb);
    printf("  PSNR=%.2f dB  SSIM=%.4f  diffMean=%.2f  noiseA=%.1f\n", report.psnr, report.ssim,
           report.diffMean, report.noiseA);
    CHECK(report.psnr > 0.0, "PSNR computed and positive");
    CHECK(report.ssim >= 0.0 && report.ssim <= 1.0, "SSIM in [0,1]");
    CHECK(report.diffMean >= 0.0, "diff summary mean non-negative");

    // JSON + CSV files.
    const std::string jsonPath = (outDir / "compare_report.json").string();
    const std::string csvPath = (outDir / "compare_report.csv").string();
    {
        std::ofstream jf(jsonPath);
        jf << report.toJson();
    }
    {
        std::ofstream cf(csvPath);
        cf << report.toCsv();
    }
    CHECK(fs::exists(jsonPath, ec), "compare_report.json written");
    CHECK(fs::exists(csvPath, ec), "compare_report.csv written");

    // JSON must contain the metric keys (basic parse check).
    {
        std::ifstream jf(jsonPath);
        std::string content((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
        CHECK(content.find("psnr_dB") != std::string::npos, "JSON contains psnr_dB");
        CHECK(content.find("ssim") != std::string::npos, "JSON contains ssim");
        CHECK(content.find("meanRGB_A") != std::string::npos, "JSON contains meanRGB_A");
    }
    // CSV must have a header + data row (2 lines).
    {
        std::ifstream cf(csvPath);
        int lines = 0;
        std::string ln;
        while (std::getline(cf, ln))
            ++lines;
        CHECK(lines == 2, "CSV has header + one data row");
    }

    // Diff PNG.
    const ImageData diffImg = mviewer::core::compareDiffImage(fa, fb);
    CHECK(!diffImg.isNull(), "diff heatmap produced");
    const std::string pngPath = (outDir / "compare_diff.png").string();
    const bool wrote = Encoder::encode(diffImg, pngPath, Encoder::Params{});
    CHECK(wrote, "compare_diff.png encoded via Encoder");
    CHECK(fs::exists(pngPath, ec), "compare_diff.png written to disk");

    fs::remove_all(outDir, ec);

    printf("\n=== M9-4 Export acceptance: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
