// M26 Phase 0 — ThumbnailPipeline lifecycle regression tests.
//
// Contracts under test (Phase 3 hardening):
//   1. setSources(B) while generation-A work is queued must CANCEL the
//      obsolete queued work so it stops before decoding (not decode-then-drop).
//   2. A path visible in BOTH generations must not be permanently blocked by
//      the old generation's pending key; the current generation must still
//      receive its thumbnail.
//   3. A scheduler submit rejection (paused pool) must not poison the key
//      forever — after resume, the same key must be schedulable again.
//   4. Completed/cancelled handles must leave the pipeline bookkeeping
//      (handlesCount returns to the live working set, not the whole history).
//
// Current (M25) behavior: setSources() only bumps the generation and never
// cancels in-flight work (obsolete tasks decode fully); a rejected submit
// leaves the key in m_pending permanently; completed handles stay in
// m_handles forever.

#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <condition_variable>
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

namespace
{

using Priority = TaskScheduler::Priority;
using PoolType = TaskScheduler::PoolType;
const auto kDrain = std::chrono::milliseconds(8000);

// Decode gate: worker threads block inside decode() until released. Lets the
// test deterministically hold generation-A tasks in flight/queued.
struct DecodeGate
{
    std::mutex mtx;
    std::condition_variable cv;
    bool released = false;
    std::vector<std::string> startedA; // decode() calls that entered (A paths)

    void reset()
    {
        std::lock_guard<std::mutex> lk(mtx);
        startedA.clear();
        released = false;
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            released = true;
        }
        cv.notify_all();
    }
    bool waitForAStarted(int count, int ms)
    {
        std::unique_lock<std::mutex> lk(mtx);
        return cv.wait_for(lk, std::chrono::milliseconds(ms),
                           [&] { return static_cast<int>(startedA.size()) >= count; });
    }
};

// ─── 1+2. Generation switch cancels obsolete work; shared path still served ─
void testGenerationSwitchCancelsObsoleteWork()
{
    printf("\n[1/2. setSources(B) cancels obsolete A work, shared path re-served]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setPoolMaxThreads(PoolType::ThumbnailPool, 1);

    ThumbnailPipeline pipe;
    DecodeGate gate;
    pipe.setDecodeFn([&](const std::string &p, int size) -> ImageData
                     {
                         {
                             std::lock_guard<std::mutex> lk(gate.mtx);
                             gate.startedA.push_back(p);
                         }
                         std::unique_lock<std::mutex> lk(gate.mtx);
                         gate.cv.wait(lk, [&] { return gate.released; });
                         return makeImageData(size, size, PixelFormat::RGB24);
                     });

    std::mutex mtx;
    std::vector<std::string> delivered;
    pipe.setResultFn([&](const std::string &p, int, const ImageData &)
                     {
                         std::lock_guard<std::mutex> lk(mtx);
                         delivered.push_back(p);
                     });

    // Generation A: 40 paths (one worker blocked in decode; 39 queued).
    std::vector<std::string> srcA;
    const std::string shared = "shared/img42.jpg";
    for (int i = 0; i < 40; ++i)
        srcA.push_back("a/img" + std::to_string(i) + ".jpg");
    srcA.push_back(shared);
    pipe.setSources(srcA);
    pipe.setVisibleRange(0, static_cast<size_t>(srcA.size()));

    // Wait until the single worker has ENTERED decode (task 1 in flight).
    CHECK(gate.waitForAStarted(1, 8000), "generation-A decode is genuinely in flight");

    // Switch to generation B — which ALSO contains the shared path.
    std::vector<std::string> srcB;
    for (int i = 0; i < 40; ++i)
        srcB.push_back("b/img" + std::to_string(i) + ".jpg");
    srcB.push_back(shared);
    pipe.setSources(srcB);
    pipe.setVisibleRange(0, static_cast<size_t>(srcB.size()));

    gate.release();
    sched.drain(PoolType::ThumbnailPool, kDrain);

    size_t aDecodes = 0;
    {
        std::lock_guard<std::mutex> lk(gate.mtx);
        for (const auto &p : gate.startedA)
            if (p.rfind("a/", 0) == 0)
                ++aDecodes;
    }
    CHECK(aDecodes == 1, "obsolete generation-A queued work stopped before decoding (1 in flight only)");
    CHECK(pipe.pendingCount() == 0, "no permanently pending keys after generation switch");

    {
        std::lock_guard<std::mutex> lk(mtx);
        bool sharedDelivered = false;
        for (const auto &p : delivered)
            if (p == shared)
                sharedDelivered = true;
        CHECK(sharedDelivered, "path visible in both generations delivered for the CURRENT generation");
    }
    sched.drain(PoolType::ThumbnailPool, kDrain);
}

// ─── 3. Rejected submit must not permanently block the key ──────────────────
void testRejectedSubmitAllowsRetry()
{
    printf("\n[3. rejected submit leaves the key schedulable again]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    ThumbnailPipeline pipe;
    std::mutex mtx;
    std::vector<std::string> decoded;
    pipe.setDecodeFn([&](const std::string &p, int size) -> ImageData
                     {
                         {
                             std::lock_guard<std::mutex> lk(mtx);
                             decoded.push_back(p);
                         }
                         return makeImageData(size, size, PixelFormat::RGB24);
                     });

    // While the ThumbnailPool is paused every submit is rejected.
    sched.pause(PoolType::ThumbnailPool);
    std::vector<std::string> src;
    for (int i = 0; i < 10; ++i)
        src.push_back("c/img" + std::to_string(i) + ".jpg");
    pipe.setSources(src);
    pipe.setVisibleRange(0, 10);
    sched.resume(PoolType::ThumbnailPool);

    // Retry the same request — the keys must NOT still be poisoned.
    pipe.setVisibleRange(0, 10);
    sched.drain(PoolType::ThumbnailPool, kDrain);
    {
        std::lock_guard<std::mutex> lk(mtx);
        CHECK(decoded.size() == 10, "all 10 keys decoded after resume + retry");
    }
    CHECK(pipe.pendingCount() == 0, "no keys stuck pending after resume");
    CHECK(pipe.handlesCount() == 0, "all completed handles removed from bookkeeping");
}

// ─── 4. Handle bookkeeping is bounded across scroll + directory churn ───────
void testHandleBookkeepingBounded()
{
    printf("\n[4. handle bookkeeping bounded under scroll + generation churn]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    ThumbnailPipeline pipe;
    pipe.setDecodeFn([](const std::string &, int size) -> ImageData
                     { return makeImageData(size, size, PixelFormat::RGB24); });

    // Simulate a long browsing session: 10 directories x 1000 paths each, with
    // visible-window sweeps. Handles must not accumulate the whole history.
    for (int d = 0; d < 10; ++d)
    {
        std::vector<std::string> src;
        for (int i = 0; i < 1000; ++i)
            src.push_back("dir" + std::to_string(d) + "/img" + std::to_string(i) + ".jpg");
        pipe.setSources(src);
        for (size_t i = 0; i + 20 <= 1000; i += 20)
            pipe.setVisibleRange(i, i + 20); // scroll sweep
        sched.drain(PoolType::ThumbnailPool, kDrain);
        CHECK(pipe.pendingCount() == 0, "pending empty after each directory settles");
        // 10x1000 history browsed; only the current working set may remain.
        CHECK(pipe.handlesCount() <= 64,
              "handles do not accumulate the whole browse history (current set only)");
        CHECK(pipe.memCacheSize() <= pipe.memCacheMax,
              "memory cache stays within its LRU bound");
    }
}

} // namespace

int main()
{
    printf("=== M26 ThumbnailPipeline lifecycle tests ===\n");
    fflush(stdout);

    testGenerationSwitchCancelsObsoleteWork();
    testRejectedSubmitAllowsRetry();
    testHandleBookkeepingBounded();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
