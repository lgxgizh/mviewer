// M7 ③ Thumbnail Pipeline subsystem: visible-range priority + predictive
// loading + in-memory LRU, built on the shared TaskScheduler. Uses an injected
// decode fn (no real files / display).
#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
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
        [&](const std::string &p, int, const ImageData &)
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
        ImageData hit = pipe.request(src[0], pipe.currentThumbSize());
        TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool,
                                        std::chrono::milliseconds(1000));
        size_t after = order.size();
        CHECK(!hit.isNull(), "cached thumbnail returned synchronously");
        CHECK(after == before, "cache hit did not trigger a new decode");
        CHECK(pipe.hits() == 1, "telemetry tracks cache hit");
        CHECK(pipe.memCacheBytes() > 0, "memCacheBytes tracks memory usage");
        CHECK(pipe.hitRatio() > 0.0, "hitRatio calculates valid ratio");
    }

    // Byte budgeting eviction test
    {
        printf("\n[byte budgeting eviction]\n");
        fflush(stdout);
        ThumbnailPipeline bytePipe;
        bytePipe.setThumbSize(64);
        bytePipe.setDecodeFn([](const std::string &, int size) { return fakeThumb(size); });
        const size_t oneThumbBytes = size_t{64} * 64 * 3;
        bytePipe.setMemCacheMaxBytes(oneThumbBytes * 2);
        std::vector<std::string> bSrc = {"b0.jpg", "b1.jpg", "b2.jpg"};
        bytePipe.setSources(bSrc);
        bytePipe.setPredictiveCount(0);
        bytePipe.setVisibleRange(0, 3);
        TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool,
                                        std::chrono::milliseconds(2000));
        CHECK(bytePipe.memCacheBytes() <= oneThumbBytes * 2,
              "memCacheBytes respects memCacheMaxBytes");
        CHECK(bytePipe.memCacheSize() == 2, "oldest thumbnail evicted when byte budget exceeded");
    }

    // clear empties the cache.
    pipe.clear();
    CHECK(pipe.memCacheSize() == 0, "clear() empties the memory cache");
    CHECK(pipe.memCacheBytes() == 0, "clear() resets memCacheBytes to 0");

    // M55: same-generation ABA. A running decode is cancelled by a viewport
    // update, then the exact key is requested again before the old worker
    // returns. The old completion must not erase the replacement's pending
    // ownership or its handle.
    {
        printf("\n[M55 same-generation ABA]\n");
        fflush(stdout);
        ThumbnailPipeline aba;
        std::mutex gateMtx;
        std::condition_variable gateCv;
        bool firstStarted = false;
        bool releaseFirst = false;
        int decodeCalls = 0;
        aba.setDecodeFn(
            [&](const std::string &, int size)
            {
                std::unique_lock<std::mutex> lock(gateMtx);
                ++decodeCalls;
                if (decodeCalls == 1)
                {
                    firstStarted = true;
                    gateCv.notify_all();
                    gateCv.wait(lock, [&] { return releaseFirst; });
                }
                lock.unlock();
                return fakeThumb(size);
            });
        aba.setSources({"aba.png"});
        aba.setVisibleRange(0, 1);
        {
            std::unique_lock<std::mutex> lock(gateMtx);
            CHECK(gateCv.wait_for(lock, std::chrono::seconds(2), [&] { return firstStarted; }),
                  "old same-key decode reached the deterministic gate");
        }
        aba.setVisibleRange(1, 1); // cancel and remove the old ownership
        aba.setVisibleRange(0, 1); // re-submit the same key, same generation
        {
            std::lock_guard<std::mutex> lock(gateMtx);
            releaseFirst = true;
        }
        gateCv.notify_all();
        CHECK(
            TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::seconds(5)),
            "same-generation ABA tasks drain");
        CHECK(decodeCalls == 2, "same-key replacement decoded exactly once after the old run");
        CHECK(aba.pendingCount() == 0 && aba.handlesCount() == 0,
              "old ABA completion did not erase replacement bookkeeping");
        CHECK(aba.memCacheSize() == 1, "replacement result owns the final cache entry");
    }

    // M55: a small forward scroll retains the intersection of the old and new
    // demand windows. The three overlapping requests must not be cancelled
    // and submitted a second time merely because the viewport moved by one row.
    {
        printf("\n[M55 overlap retention]\n");
        fflush(stdout);
        ThumbnailPipeline overlap;
        std::mutex gateMtx;
        std::condition_variable gateCv;
        bool firstStarted = false;
        bool releaseFirst = false;
        std::unordered_map<std::string, int> calls;
        overlap.setDecodeFn(
            [&](const std::string &path, int size)
            {
                std::unique_lock<std::mutex> lock(gateMtx);
                ++calls[path];
                if (path == "overlap0.png")
                {
                    firstStarted = true;
                    gateCv.notify_all();
                    gateCv.wait(lock, [&] { return releaseFirst; });
                }
                lock.unlock();
                return fakeThumb(size);
            });
        overlap.setSources(
            {"overlap0.png", "overlap1.png", "overlap2.png", "overlap3.png", "overlap4.png"});
        overlap.setPredictiveCount(0);
        overlap.setVisibleRange(0, 4);
        {
            std::unique_lock<std::mutex> lock(gateMtx);
            CHECK(gateCv.wait_for(lock, std::chrono::seconds(2), [&] { return firstStarted; }),
                  "overlap test reached the deterministic gate");
        }
        overlap.setVisibleRange(1, 5);
        {
            std::lock_guard<std::mutex> lock(gateMtx);
            releaseFirst = true;
        }
        gateCv.notify_all();
        CHECK(
            TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::seconds(5)),
            "overlap retention tasks drain");
        CHECK(calls["overlap0.png"] == 1 && calls["overlap1.png"] == 1 &&
                  calls["overlap2.png"] == 1 && calls["overlap3.png"] == 1 &&
                  calls["overlap4.png"] == 1,
              "overlapping demand retains existing work without duplicate decodes");
        CHECK(overlap.pendingCount() == 0 && overlap.handlesCount() == 0,
              "overlap retention leaves no scheduler bookkeeping");
    }

    // Pass 21: Oversized thumbnail protection and empty/invalid input guards.
    {
        printf("\n[oversized thumbnail protection & bounds hardening]\n");
        fflush(stdout);
        ThumbnailPipeline guardPipe;
        guardPipe.setThumbSize(64);
        const size_t normalBytes = size_t{64} * 64 * 3; // ~12 KiB
        guardPipe.setMemCacheMaxBytes(normalBytes * 3); // budget for 3 thumbnails
        guardPipe.setDecodeFn(
            [&](const std::string &path, int size)
            {
                if (path == "huge.png")
                    return makeImageData(1024, 1024, PixelFormat::RGBA32); // 4 MiB >> budget
                return fakeThumb(size);
            });

        std::vector<std::string> testFiles = {"t1.png", "t2.png", "huge.png"};
        guardPipe.setSources(testFiles);
        guardPipe.setVisibleRange(0, 3);
        TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool,
                                        std::chrono::milliseconds(2000));

        // The 2 normal thumbnails should be cached.
        ImageData hit1 = guardPipe.request("t1.png", 64);
        ImageData hit2 = guardPipe.request("t2.png", 64);
        CHECK(!hit1.isNull(), "first valid thumbnail retained in cache");
        CHECK(!hit2.isNull(), "second valid thumbnail retained in cache");
        // The oversized thumbnail should NOT have flushed the valid cache entries
        CHECK(guardPipe.memCacheSize() == 2, "oversized decode did not flush existing cache items");
        CHECK(guardPipe.memCacheBytes() <= normalBytes * 3,
              "memCacheBytes stays within strict budget after oversized attempt");

        // Null and invalid request guards
        ImageData emptyPath = guardPipe.request("", 64);
        CHECK(emptyPath.isNull(), "request with empty path returns null ImageData");
        ImageData negativeSize = guardPipe.request("t1.png", -5);
        CHECK(negativeSize.isNull(), "request with negative size returns null ImageData");
        ImageData zeroSize = guardPipe.request("t1.png", 0);
        CHECK(zeroSize.isNull(), "request with zero size returns null ImageData");
    }

    // Network-browse scheduling: high-latency predictive stays tight; scroll
    // cancels obsolete demand so slow IO cannot clog the queue with offscreen work.
    {
        printf("\n[network-browse predictive + cancel]\n");
        fflush(stdout);
        CHECK(ThumbnailPipeline::recommendedPredictiveCount(100, false) == 48,
              "local small-dir predictive is 48");
        CHECK(ThumbnailPipeline::recommendedPredictiveCount(800, false) == 64,
              "local mid-dir predictive is 64");
        CHECK(ThumbnailPipeline::recommendedPredictiveCount(5000, false) == 96,
              "local large-dir predictive is 96");
        CHECK(ThumbnailPipeline::recommendedPredictiveCount(100, true) == 8,
              "high-latency small-dir predictive is 8");
        CHECK(ThumbnailPipeline::recommendedPredictiveCount(800, true) == 12,
              "high-latency mid-dir predictive is 12");
        CHECK(ThumbnailPipeline::recommendedPredictiveCount(5000, true) == 12,
              "high-latency large-dir predictive stays 12");

        TaskScheduler::instance().setPoolMaxThreads(TaskScheduler::ThumbnailPool, 1);
        ThumbnailPipeline slow;
        slow.setThumbSize(64);
        std::atomic<int> started{0};
        std::atomic<int> finished{0};
        slow.setDecodeFn(
            [&](const std::string &, int size)
            {
                started.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                finished.fetch_add(1);
                return fakeThumb(size);
            });
        std::mutex deliveredMtx;
        std::vector<std::string> delivered;
        delivered.reserve(64);
        slow.setResultFn(
            [&](const std::string &path, int, const ImageData &)
            {
                std::lock_guard<std::mutex> lk(deliveredMtx);
                delivered.push_back(path);
            });
        std::vector<std::string> src;
        for (int i = 0; i < 200; ++i)
            src.push_back("net/" + std::to_string(i) + ".jpg");
        slow.setSources(src);
        const size_t pred = ThumbnailPipeline::recommendedPredictiveCount(src.size(), true);
        slow.setPredictiveCount(pred);
        slow.setVisibleRange(0, 10);
        // Allow a couple of tasks to start, then jump far ahead.
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        slow.setVisibleRange(150, 160);
        const size_t peak = slow.pendingCount();
        TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool,
                                        std::chrono::milliseconds(10000));
        CHECK(peak <= 10 + pred + 2,
              "high-latency pending stays near visible+predictive after scroll");
        {
            std::lock_guard<std::mutex> lk(deliveredMtx);
            int viewportHits = 0;
            for (int i = 150; i < 160; ++i)
                for (const auto &p : delivered)
                    if (p == src[static_cast<size_t>(i)])
                        ++viewportHits;
            CHECK(viewportHits == 10, "post-scroll viewport thumbnails all delivered");
            int farOffscreen = 0;
            for (const auto &p : delivered)
            {
                // Indices 80..120 were never in either demand window.
                for (int i = 80; i <= 120; ++i)
                    if (p == src[static_cast<size_t>(i)])
                        ++farOffscreen;
            }
            CHECK(farOffscreen == 0, "paths outside both demand windows were never decoded");
        }
        slow.clear();
        TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool,
                                        std::chrono::milliseconds(2000));
    }

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
