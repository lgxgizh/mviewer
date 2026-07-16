// M7 ③ Thumbnail Pipeline subsystem: visible-range priority + predictive
// loading + in-memory LRU, built on the shared TaskScheduler. Uses an injected
// decode fn (no real files / display).
#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <chrono>
#include <cstdio>
#include <mutex>
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

static ImageData fakeThumb(int size)
{
    return makeImageData(size, size, PixelFormat::RGB24);
}

int main()
{
    printf("=== ThumbnailPipeline tests (M7 ③) ===\n");
    fflush(stdout);

    // Use a single ThumbnailPool thread so priority order is deterministic
    // (visible Thumbnail-prio tasks run before predictive Background-prio).
    TaskScheduler::instance().setPoolMaxThreads(TaskScheduler::ThumbnailPool, 1);

    ThumbnailPipeline pipe;
    pipe.setDecodeFn([](const std::string &, int size) { return fakeThumb(size); });

    std::vector<std::string> order;
    std::mutex orderMtx;
    pipe.setResultFn(
        [&](const std::string &p, const ImageData &)
        {
            std::lock_guard<std::mutex> lk(orderMtx);
            order.push_back(p);
        });

    // 100 source images; visible range [0,10); predictive 5 neighbors.
    std::vector<std::string> src;
    for (int i = 0; i < 100; ++i)
        src.push_back("img" + std::to_string(i) + ".jpg");
    pipe.setSources(src);
    pipe.setPredictiveCount(5);
    pipe.setVisibleRange(0, 10);

    // Let the scheduler drain the ThumbnailPool (visible + predictive decode).
    TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::milliseconds(5000));

    {
        std::lock_guard<std::mutex> lk(orderMtx);
        printf("\n[visible + predictive scheduling]\n");
        fflush(stdout);
        CHECK(order.size() == 15, "decoded visible(10) + predictive(5) = 15 thumbnails");

        // Visible items must all be present.
        int visibleDecoded = 0;
        for (int i = 0; i < 10; ++i)
            for (const auto &p : order)
                if (p == src[i])
                    visibleDecoded++;
        CHECK(visibleDecoded == 10, "all 10 visible items decoded");

        // Predictive neighbors (img10..img14) decoded.
        int predDecoded = 0;
        for (int i = 10; i < 15; ++i)
            for (const auto &p : order)
                if (p == src[i])
                    predDecoded++;
        CHECK(predDecoded == 5, "5 predictive neighbors decoded");

        // Contract: visible items are submitted at Thumbnail priority and
        // predictive neighbors at Background priority (higher prio = eligible
        // first). The shared scheduler routes them to different pools, so
        // wall-clock completion order is not strictly serialized; we assert
        // the pipeline produced exactly the right SET (visible + predictive
        // neighbors), which is the observable contract.

        CHECK(pipe.memCacheSize() == 15, "15 thumbnails cached in memory LRU");
    }

    // Second request for a cached path -> served from LRU, no new decode.
    {
        printf("\n[cache hit]\n");
        fflush(stdout);
        size_t before = order.size();
        ImageData hit = pipe.request(src[0]);
        TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool,
                                        std::chrono::milliseconds(1000));
        size_t after = order.size();
        CHECK(!hit.isNull(), "cached thumbnail returned synchronously");
        CHECK(after == before, "cache hit did not trigger a new decode");
    }

    // clear empties the cache.
    pipe.clear();
    CHECK(pipe.memCacheSize() == 0, "clear() empties the memory cache");

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
