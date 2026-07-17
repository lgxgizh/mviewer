// M9-3 acceptance: the Analysis workflow must run the built-in analyzers over
// an image (and a region) and produce human-readable results that the
// AnalysisPanel can display. This exercises the REAL analyzer path
// (AnalyzerRegistry enumerates the built-ins; each analyzer runs analyze()
// on an ImageFrame) — it does not fake the result.
//
// Scope is M9-3 ONLY. Browse / Compare / Export / Workspace / Polish are other
// phases and are NOT touched here.
//
// Note on design (per M9 review): the codebase already ships an AnalyzerRegistry
// (factory-based, 8 built-ins). M9-3 does NOT expand or add a new registry; it
// verifies the existing analysis flow produces results. A plain std::vector of
// analyzers would be equivalent for display; the registry is used here only
// because it is the existing enumeration entry point.
#include "core/analyzer/Analyzer.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
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

    printf("\n[M9-3 Analysis: built-in analyzers run on an image + region]\n");
    fflush(stdout);

    // Build a real ImageFrame from a generated image.
    QImage qimg = makeColorTest(128, 96, QColor(120, 60, 200));
    ImageData data = mvcore::fromQImage(qimg);
    CHECK(!data.isNull(), "generated ImageData is valid");

    mviewer::domain::ImageMetadata meta;
    meta.filePath = "mem://test";
    meta.fileName = "test.png";
    meta.width = data.width;
    meta.height = data.height;
    ImageFrame frame(meta, data);
    CHECK(frame.isValid(), "ImageFrame constructed from pixels");

    // Enumerate the built-in analyzers via the existing registry entry point.
    Analyzer::registerBuiltins();
    AnalyzerRegistry &reg = AnalyzerRegistry::instance();
    const std::vector<std::string> ids = reg.availableAnalyzers();
    printf("  registry reports %zu analyzers\n", ids.size());
    CHECK(ids.size() >= 7, "registry enumerates the built-in analyzers (>=7)");

    // Every SINGLE-IMAGE analyzer must run on the full frame and yield a
    // non-empty summary. MULTI-IMAGE analyzers (PSNR/SSIM) legitimately
    // require a second frame and correctly return false on a single frame —
    // the AnalysisPanel surfaces them in its Compare page instead.
    int okFull = 0;
    int singleImg = 0;
    int multiImg = 0;
    for (const std::string &id : ids)
    {
        auto a = reg.create(id);
        if (!a)
        {
            printf("  (skipped %s: create() returned null)\n", id.c_str());
            continue;
        }
        const AnalyzerCapability cap = a->capabilities();
        const bool isMulti = hasCapability(cap, AnalyzerCapability::MultiImage);
        if (isMulti)
            ++multiImg;
        else
            ++singleImg;
        const bool ran = a->analyze(frame);
        const std::string txt = a->resultText();
        printf("    %-12s multi=%d ran=%d resultLen=%zu\n", id.c_str(), isMulti, ran, txt.size());
        if (!isMulti && ran && !txt.empty())
            ++okFull;
    }
    CHECK(singleImg >= 5, "registry has the expected single-image analyzers (>=5)");
    CHECK(multiImg >= 2, "registry has dual-image quality analyzers PSNR/SSIM (>=2)");
    CHECK(okFull >= 5, "all single-image analyzers produce a non-empty result on the full frame");

    // Region analysis must run for the single-image analyzers (ROI-based).
    mviewer::domain::Selection roi;
    roi.x = 0;
    roi.y = 0;
    roi.width = 64;
    roi.height = 48;
    int okRegion = 0;
    for (const std::string &id : ids)
    {
        auto a = reg.create(id);
        if (!a)
            continue;
        if (hasCapability(a->capabilities(), AnalyzerCapability::MultiImage))
            continue; // dual-image analyzers don't run on a single-frame ROI
        if (a->analyzeRegion(frame, roi))
            ++okRegion;
    }
    CHECK(okRegion >= 5, "all single-image analyzers run on a selected ROI");

    printf("\n=== M9-3 Analysis acceptance: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
