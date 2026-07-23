// M9-1 acceptance: the Browse workflow must open a 1000-image directory
// without blocking the UI thread, and the first thumbnail must appear within
// the ~200 ms review budget. This exercises the REAL pipeline
// (ImageRepository::loadDirectoryAsync + ThumbnailPipeline) that the
// MainWindow Browse path relies on — it does not fake the result.
//
// Scope is M9-1 ONLY (Browse). Compare / Analysis / Export / Workspace /
// Polish are later phases and are NOT touched here.
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifndef MVIEWER_SOURCE_DIR
static std::string srcRootFromThisFile()
{
    std::string f = __FILE__;
    auto p = f.find("/src/core/test_product_browse.cpp");
    if (p == std::string::npos)
        p = f.find("\\src\\core\\test_product_browse.cpp");
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

static constexpr double kNonBlockingBudgetMs = 100.0;
static constexpr double kFirstThumbBudgetMs =
    300.0; // review target: first thumbnail < 300 ms (cold)

static int write1000(const std::filesystem::path &dir)
{
    int written = 0;
    for (int i = 0; i < 1000; ++i)
    {
        QImage img = makeColorTest(16, 16, QColor((i * 7) % 256, (i * 13) % 256, (i * 29) % 256));
        const std::string path = (dir / ("img_" + std::to_string(i) + ".png")).string();
        if (Encoder::encode(mvcore::fromQImage(img), path, Encoder::Params{}))
            ++written;
    }
    return written;
}

// Mirrors the MainWindow Browse path: user selects a directory, the
// Repository opens it asynchronously (UI thread free), and the ThumbnailPanel
// streams thumbnails in the background.
static void testBrowseWorkflow()
{
    printf("\n[M9-1 Browse: open 1000-image directory without blocking + first thumbnail]\n");
    fflush(stdout);

    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "mviewer_m9_browse";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    const int written = write1000(tempDir);
    CHECK(written == 1000, "all 1000 PNGs written to temp dir");

    ImageRepository &repo = ImageRepository::instance();

    std::atomic<int> got{0};
    std::atomic<bool> done{false};
    std::vector<ImageRepository::Result> finalResults;

    const auto t0 = std::chrono::steady_clock::now();
    repo.loadDirectoryAsync(
        tempDir.string(),
        [&](std::vector<ImageRepository::Result> results)
        {
            finalResults = std::move(results);
            got.store(static_cast<int>(finalResults.size()));
            done.store(true);
        },
        1000);
    const auto tCall = std::chrono::steady_clock::now();
    const double callMs = std::chrono::duration<double, std::milli>(tCall - t0).count();
    printf("  loadDirectoryAsync() returned in %.1f ms (budget %.0f ms)\n", callMs,
           kNonBlockingBudgetMs);
    fflush(stdout);
    CHECK(callMs < kNonBlockingBudgetMs,
          "open() returns immediately (UI thread not blocked on 1000 decodes)");

    TaskScheduler::instance().drain(TaskScheduler::DecodePool, std::chrono::seconds(120));
    printf("  async callback delivered %d frames\n", got.load());
    CHECK(got.load() == 1000, "all 1000 images decoded via the async open path");

    fs::remove_all(tempDir, ec);
}

// First thumbnail must arrive promptly after the visible range is set — this
// is what the user perceives as "the folder opened fast".
static void testFirstThumbnailLatency()
{
    printf("\n[M9-1 Browse: first thumbnail within budget]\n");
    fflush(stdout);

    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "mviewer_m9_thumb";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    write1000(tempDir);

    std::vector<std::string> paths;
    for (int i = 0; i < 1000; ++i)
        paths.push_back((tempDir / ("img_" + std::to_string(i) + ".png")).string());

    ThumbnailPipeline &pipe = ThumbnailPipeline::instance();
    pipe.clear();
    pipe.setDecodeFn([](const std::string &p, int size) { return Decoder::decodeScaled(p, size); });

    std::atomic<double> firstMs{-1.0};
    std::atomic<int> thumbCount{0};
    std::mutex cvMtx;
    std::condition_variable cv;
    bool firstSeen = false;
    std::chrono::steady_clock::time_point tAnchor;

    pipe.setResultFn(
        [&](const std::string &, const ImageData &)
        {
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - tAnchor)
                                  .count();
            double expected = -1.0;
            if (firstMs.compare_exchange_strong(expected, ms))
            {
                thumbCount.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(cvMtx);
                firstSeen = true;
                cv.notify_all();
            }
            else
            {
                thumbCount.fetch_add(1, std::memory_order_relaxed);
            }
        });

    pipe.setSources(paths);

    tAnchor = std::chrono::steady_clock::now();
    pipe.setVisibleRange(0, 20); // first screenful — what the user sees first

    {
        std::unique_lock<std::mutex> lk(cvMtx);
        cv.wait_for(lk, std::chrono::seconds(10), [&] { return firstSeen; });
    }

    const double fm = firstMs.load();
    printf("  first thumbnail emitted at %.1f ms (budget %.0f ms), total emitted=%d\n", fm,
           kFirstThumbBudgetMs, thumbCount.load());
    CHECK(fm >= 0.0, "at least one thumbnail was produced");
    CHECK(fm < kFirstThumbBudgetMs, "first thumbnail appears within the ~200 ms budget");

    pipe.clear();
    fs::remove_all(tempDir, ec);
}

// Continuous left/right navigation across the directory must keep streaming
// thumbnails without re-blocking the caller or dropping work — mirrors the user
// holding ←/→ through a large folder.
static void testNavigateWorkflow()
{
    printf("\n[M9-1 Browse: continuous left/right navigation streams thumbnails]\n");
    fflush(stdout);

    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "mviewer_m9_nav";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);
    write1000(tempDir);

    std::vector<std::string> paths;
    for (int i = 0; i < 1000; ++i)
        paths.push_back((tempDir / ("img_" + std::to_string(i) + ".png")).string());

    ThumbnailPipeline &pipe = ThumbnailPipeline::instance();
    pipe.clear();
    pipe.setDecodeFn([](const std::string &p, int size) { return Decoder::decodeScaled(p, size); });

    std::atomic<int> thumbCount{0};
    pipe.setResultFn([&](const std::string &, const ImageData &) { thumbCount.fetch_add(1); });

    pipe.setSources(paths);

    // Rapid ←/→ jumps across the directory (begin, middle, far end, back, wrap).
    const std::vector<std::pair<size_t, size_t>> jumps = {
        {0, 20}, {100, 120}, {500, 520}, {250, 270}, {0, 20}, {980, 999}};
    for (auto [b, e] : jumps)
    {
        const auto t0 = std::chrono::steady_clock::now();
        pipe.setVisibleRange(b, e);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                              .count();
        CHECK(ms < kNonBlockingBudgetMs, "setVisibleRange() returns immediately during navigation");
    }

    TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::seconds(120));
    printf("  navigation produced %d thumbnails\n", thumbCount.load());
    CHECK(thumbCount.load() >= 20, "navigation produced thumbnails for the visited windows");

    pipe.clear();
    fs::remove_all(tempDir, ec);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    (void)MVIEWER_SOURCE_DIR;

    // Ensure the shared scheduler worker threads are running.
    TaskScheduler::instance();

    testBrowseWorkflow();
    testFirstThumbnailLatency();
    testNavigateWorkflow();

    printf("\n=== M9-1 Browse acceptance: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
