// M3 acceptance verification: proves the two P0 acceptance bars from the
// architecture review against the REAL async pipeline (not faked):
//   1) "Open a directory containing 1000 images without blocking UI"
//      -> loadDirectoryAsync must RETURN immediately (non-blocking), while the
//         async callback later delivers all 1000 decoded frames.
//   2) "First thumbnail appears within ~200 ms"
//      -> ThumbnailPipeline must emit the first thumbnail within the budget.
#include "core/image/Encoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/image/QtConvert.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "core/scheduler/TaskScheduler.h"

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
    auto p = f.find("/src/core/test_m3_acceptance.cpp");
    if (p == std::string::npos)
        p = f.find("\\src\\core\\test_m3_acceptance.cpp");
    return p == std::string::npos ? "." : f.substr(0, p);
}
#define MVIEWER_SOURCE_DIR srcRootFromThisFile().c_str()
#endif

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
        {                                \
            printf("  PASS: %s\n", msg); \
            g_pass++;                    \
        }                                \
        else                             \
        {                                \
            printf("  FAIL: %s\n", msg); \
            g_fail++;                    \
        }                                \
    } while (0)

static QImage makeColorTest(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, c.rgb());
    return img;
}

// Budget for "non-blocking": the open() call must return well before the
// decodes finish. 1000 synchronous decodes take far longer than this.
static constexpr double kNonBlockingBudgetMs = 100.0;
// Review target: first thumbnail within ~200 ms.
static constexpr double kFirstThumbBudgetMs = 200.0;

static int write1000(const std::filesystem::path &dir)
{
    int written = 0;
    for (int i = 0; i < 1000; ++i)
    {
        QImage img = makeColorTest(16, 16, QColor((i * 7) % 256, (i * 13) % 256, (i * 29) % 256));
        const std::string path = (dir / ("img_" + std::to_string(i) + ".png")).string();
        if (Encoder::encode(mvcore::fromQImage(img), path, Encoder::Params{}))
            ++written;
        if ((i + 1) % 200 == 0)
        {
            printf("  ...wrote %d\n", i + 1);
            fflush(stdout);
        }
    }
    return written;
}

static void testNonBlocking1000()
{
    printf("\n[M3 acceptance: open 1000 images without blocking (async)]\n");
    fflush(stdout);

    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "mviewer_m3_accept";
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
    repo.loadDirectoryAsync(tempDir.string(),
        [&](const std::vector<ImageRepository::Result> &results) {
            finalResults = results;
            got.store(static_cast<int>(results.size()));
            done.store(true);
        },
        1000);
    const auto tCall = std::chrono::steady_clock::now();
    const double callMs = std::chrono::duration<double, std::milli>(tCall - t0).count();
    printf("  loadDirectoryAsync() returned in %.1f ms (budget %.0f ms)\n", callMs, kNonBlockingBudgetMs);
    fflush(stdout);

    CHECK(callMs < kNonBlockingBudgetMs,
          "open() returns immediately (does not block on 1000 decodes)");

    // The async callback is delivered on the Qt event loop (QTimer::singleShot),
    // exactly as in the real UI. Pump the loop until all 1000 decode, bounded.
    const auto tWait = std::chrono::steady_clock::now();
    while (!done.load() && std::chrono::steady_clock::now() - tWait < std::chrono::seconds(30))
    {
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
    }
    printf("  async callback delivered %d frames\n", got.load());
    CHECK(got.load() == 1000, "all 1000 images decoded via the async path");

    fs::remove_all(tempDir, ec);
}

static void testFirstThumbnailLatency()
{
    printf("\n[M3 acceptance: first thumbnail within ~200 ms]\n");
    fflush(stdout);

    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "mviewer_m3_thumb";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    write1000(tempDir);

    std::vector<std::string> paths;
    for (int i = 0; i < 1000; ++i)
        paths.push_back((tempDir / ("img_" + std::to_string(i) + ".png")).string());

    ThumbnailPipeline &pipe = ThumbnailPipeline::instance();
    pipe.clear();
    pipe.setDecodeFn([](const std::string &p, int size) {
        return Decoder::decodeScaled(p, size);
    });

    std::atomic<double> firstMs{-1.0};
    std::atomic<int> thumbCount{0};
    std::mutex cvMtx;
    std::condition_variable cv;
    bool firstSeen = false;
    std::chrono::steady_clock::time_point tAnchor;

    pipe.setResultFn([&](const std::string &, const ImageData &) {
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

    // Anchor is set right before the visible-range kick so the measurement
    // starts at the user-visible trigger.
    tAnchor = std::chrono::steady_clock::now();
    pipe.setVisibleRange(0, 20); // first screenful

    {
        std::unique_lock<std::mutex> lk(cvMtx);
        cv.wait_for(lk, std::chrono::seconds(10), [&] { return firstSeen; });
    }

    const double fm = firstMs.load();
    printf("  first thumbnail emitted at %.1f ms (budget %.0f ms), total emitted=%d\n",
           fm, kFirstThumbBudgetMs, thumbCount.load());
    CHECK(fm >= 0.0, "at least one thumbnail was produced");
    CHECK(fm < kFirstThumbBudgetMs, "first thumbnail appears within the ~200 ms budget");

    pipe.clear();
    fs::remove_all(tempDir, ec);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    (void)MVIEWER_SOURCE_DIR;

    // Ensure the shared scheduler worker threads are running.
    TaskScheduler::instance();

    testNonBlocking1000();
    testFirstThumbnailLatency();

    printf("\n=== M3 acceptance: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
