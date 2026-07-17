// M6 unit tests: ImageRepository (predictive preload, 1000-image load, frame extras).
#include "core/cache/CacheManager.h"
#include "core/image/DiskCache.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifndef MVIEWER_SOURCE_DIR
static std::string srcRootFromThisFile()
{
    std::string f = __FILE__;
    auto p = f.find("/src/core/test_repository.cpp");
    if (p == std::string::npos)
        p = f.find("\\src\\core\\test_repository.cpp");
    return p == std::string::npos ? "." : f.substr(0, p);
}
#define MVIEWER_SOURCE_DIR srcRootFromThisFile().c_str()
#endif

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

static void testPredictivePreload()
{
    printf("\n[Predictive preload (M5)]\n");
    fflush(stdout);
    CacheManager &mgr = CacheManager::instance();
    DiskCache &disk = DiskCache::instance();
    mgr.clear();
    disk.clear();

    ImageRepository &repo = ImageRepository::instance();
    const std::string base = std::string(MVIEWER_SOURCE_DIR) + "/testdata/golden/256x256/";
    const char *files[] = {"flat_color_256x256.jpg", "checker_256x256.jpg", "gradient_256x256.png"};

    std::vector<std::string> keys;
    for (const char *f : files)
    {
        auto r = repo.load(base + f);
        CHECK(r.success(), ("repository load " + std::string(f)).c_str());
        keys.push_back(repo.makeKey(base + f));
    }
    CHECK(disk.entryCount() >= 3, "disk tier holds the loaded images");

    mgr.clearMemory();
    ImageData probe;
    CHECK(!mgr.getMemory(CacheLevel::FullImage, keys[1], probe),
          "neighbor not in memory before preload");

    repo.prefetch({keys[1], keys[2]}, CacheLevel::FullImage);

    ImageData warm;
    CHECK(mgr.getMemory(CacheLevel::FullImage, keys[1], warm),
          "preloaded neighbor present in memory");
    CHECK(mgr.getMemory(CacheLevel::FullImage, keys[2], warm),
          "preloaded second neighbor present in memory");

    mgr.clear();
    disk.clear();
}

static void test1000ImageNonBlocking()
{
    printf("\n[1000-image directory non-blocking open (M5)]\n");
    fflush(stdout);

    namespace fs = std::filesystem;
    ImageRepository &repo = ImageRepository::instance();

    const fs::path tempDir = fs::temp_directory_path() / "mviewer_m5_1k";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    bool allWritten = true;
    for (int i = 0; i < 1000; ++i)
    {
        QImage img = makeColorTest(16, 16, QColor((i * 7) % 256, (i * 13) % 256, (i * 29) % 256));
        const std::string path = (tempDir / ("img_" + std::to_string(i) + ".png")).string();
        const bool ok = Encoder::encode(mvcore::fromQImage(img), path, Encoder::Params{});
        if (!ok)
            allWritten = false;
    }
    CHECK(allWritten, "all 1000 PNGs written to temp dir");

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<ImageRepository::Result> results = repo.loadDirectory(tempDir.string(), 1000);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    CHECK(results.size() == 1000,
          (std::to_string(results.size()) + " images loaded from directory").c_str());
    printf("  loadDirectory(1000) elapsed = %.1f ms\n", ms);
    fflush(stdout);

    fs::remove_all(tempDir, ec);
}

static void testImageFrameExtras()
{
    printf("\n[ImageFrame Selection/Tags/Cache]\n");

    QImage img = makeColorTest(64, 64, QColor(100, 100, 100));
    ImageData data = mvcore::fromQImage(img);
    ImageFrame frame = ImageFrame::create("/tmp.png", data);

    mviewer::domain::Selection sel = {10, 20, 30, 40};
    frame.setSelection(sel);
    CHECK(frame.selection().x == 10, "Selection set");
    CHECK(frame.selection().width == 30, "Selection width");
    frame.clearSelection();
    CHECK(frame.selection().isEmpty(), "Selection cleared");

    frame.addTag("favorite");
    frame.addTag("checked");
    frame.addTag("favorite");
    CHECK(frame.hasTag("favorite"), "Tag added");
    CHECK(frame.hasTag("checked"), "Tag added");
    CHECK(!frame.hasTag("missing"), "Tag missing");
    frame.removeTag("favorite");
    CHECK(!frame.hasTag("favorite"), "Tag removed");

    frame.setAnalysisResult("rgbmean", true);
    frame.setAnalysisResult("entropy", true);
    auto *e1 = frame.findAnalysis("rgbmean");
    CHECK(e1 != nullptr && e1->ok, "Analysis cache hit");
    CHECK(frame.findAnalysis("missing") == nullptr, "Analysis cache miss");
    frame.clearAnalysisCache();
    CHECK(frame.findAnalysis("rgbmean") == nullptr, "Analysis cache cleared");

    ImageData thumb = mvcore::fromQImage(makeColorTest(32, 32, QColor(50, 50, 50)));
    RenderCacheEntry rce;
    rce.tag = RenderCacheEntry::Tag::ScaledView;
    rce.data = thumb;
    rce.srcWidth = 1920;
    rce.srcHeight = 1080;
    frame.setRenderCache(rce);
    auto *found = frame.findRenderCache(RenderCacheEntry::Tag::ScaledView);
    CHECK(found != nullptr, "Render cache hit");
    CHECK(found->srcWidth == 1920, "Render cache meta");
    frame.clearRenderCache();
    CHECK(frame.findRenderCache(RenderCacheEntry::Tag::ScaledView) == nullptr,
          "Render cache cleared");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== Repository Tests (M6) ===\n");
    fflush(stdout);

    testPredictivePreload();
    test1000ImageNonBlocking();
    testImageFrameExtras();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
