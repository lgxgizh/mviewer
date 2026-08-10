// M27 Phase 6/8 — ThumbnailPipeline callback-under-lock & delivery edge cases.
//
// Contracts under test (must hold after Phase 6/8 hardening):
//   1. The result callback runs OUTSIDE the pipeline mutex — a callback that
//      re-enters request() / setSources() / clear() must not deadlock.
//   2. A throwing result callback is contained (never escapes the worker) and
//      leaves pending/handles at zero.
//   3. A throwing decoder is treated as a decode failure: pending/handles
//      converge to zero and the same key stays re-requestable.
//   4. Generation switch while a decoder throws/cancels leaves no residue and
//      the current generation can retry.
//
// Deadlock scenarios run under a watchdog thread: pre-fix the callback-under-
// lock deadlocks forever and the watchdog aborts the process (a FAIL). Post-
// fix they complete normally.
//
// Pre-fix (M26) failures reproduced: 1-3 (callback under lock; decoder throw
// leaks m_pending/m_handles forever — the exception escaped the task before
// its bookkeeping cleanup).

#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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
const auto kDrain = std::chrono::milliseconds(10000);

// Aborts the process if the scenario deadlocks (pre-fix the callback-under-
// lock reentrancy hangs forever).
struct DeadlockWatchdog
{
    std::atomic<bool> finished{false};
    std::thread thread;

    explicit DeadlockWatchdog(int seconds = 20)
    {
        thread = std::thread(
            [this, seconds]()
            {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                if (!finished.load())
                {
                    fprintf(stderr, "  WATCHDOG: deadlock detected, aborting\n");
                    std::abort();
                }
            });
    }
    ~DeadlockWatchdog()
    {
        finished.store(true);
        if (thread.joinable())
            thread.join();
    }
};

bool waitUntil(const std::function<bool()> &cond, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (cond())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return cond();
}

// ─── 1. result callback re-enters request() ────────────────────────────────
void testCallbackReentersRequest()
{
    printf("\n[1. result callback re-enters request() — no deadlock]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setPoolMaxThreads(PoolType::ThumbnailPool, 2);

    ThumbnailPipeline pipe;
    std::atomic<int> delivered{0};
    std::atomic<bool> reentered{false};
    pipe.setDecodeFn([](const std::string &, int size)
                     { return makeImageData(size, size, PixelFormat::RGB24); });
    pipe.setResultFn(
        [&](const std::string &p, int size, const ImageData &)
        {
            if (delivered.fetch_add(1) == 0)
            {
                // Re-enter from inside the callback: a cache miss
                // for a NEW key kicks scheduleLocked() (submits a
                // new task), which pre-fix deadlocks on the
                // pipeline mutex.
                pipe.request("reentrant/new.png", size);
                reentered = true;
            }
        });
    {
        DeadlockWatchdog watchdog;
        pipe.setSources({"a/img0.png", "a/img1.png"});
        pipe.setVisibleRange(0, 2);
        CHECK(waitUntil([&] { return reentered.load(); }, 8000),
              "callback executed and re-entered request()");
        sched.drain(PoolType::ThumbnailPool, kDrain);
    }
    CHECK(delivered.load() >= 2, "both original + reentrant deliveries completed");
    CHECK(pipe.pendingCount() == 0, "pending converges to zero after reentrant request");
    CHECK(pipe.handlesCount() == 0, "handles converge to zero after reentrant request");
}

// ─── 2. result callback re-enters setSources() ─────────────────────────────
void testCallbackReentersSetSources()
{
    printf("\n[2. result callback re-enters setSources() — no deadlock]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setPoolMaxThreads(PoolType::ThumbnailPool, 2);

    ThumbnailPipeline pipe;
    std::atomic<int> delivered{0};
    std::atomic<bool> switched{false};
    pipe.setDecodeFn([](const std::string &, int size)
                     { return makeImageData(size, size, PixelFormat::RGB24); });
    pipe.setResultFn(
        [&](const std::string &, int, const ImageData &)
        {
            if (delivered.fetch_add(1) == 0)
            {
                pipe.setSources({"b/img0.png", "b/img1.png"});
                pipe.setVisibleRange(0, 2);
                switched = true;
            }
        });
    {
        DeadlockWatchdog watchdog;
        pipe.setSources({"a/img0.png", "a/img1.png"});
        pipe.setVisibleRange(0, 2);
        CHECK(waitUntil([&] { return switched.load(); }, 8000),
              "callback executed and re-entered setSources()");
        sched.drain(PoolType::ThumbnailPool, kDrain);
    }
    CHECK(delivered.load() >= 1, "generation-A delivery happened before the switch");
    CHECK(pipe.pendingCount() == 0, "pending converges to zero after reentrant setSources");
    CHECK(pipe.handlesCount() == 0, "handles converge to zero after reentrant setSources");
}

// ─── 3. result callback re-enters clear() ──────────────────────────────────
void testCallbackReentersClear()
{
    printf("\n[3. result callback re-enters clear() — no deadlock]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setPoolMaxThreads(PoolType::ThumbnailPool, 2);

    ThumbnailPipeline pipe;
    std::atomic<int> delivered{0};
    std::atomic<bool> cleared{false};
    pipe.setDecodeFn([](const std::string &, int size)
                     { return makeImageData(size, size, PixelFormat::RGB24); });
    pipe.setResultFn(
        [&](const std::string &, int, const ImageData &)
        {
            if (delivered.fetch_add(1) == 0)
            {
                pipe.clear();
                cleared = true;
            }
        });
    {
        DeadlockWatchdog watchdog;
        pipe.setSources({"a/img0.png", "a/img1.png"});
        pipe.setVisibleRange(0, 2);
        CHECK(waitUntil([&] { return cleared.load(); }, 8000),
              "callback executed and re-entered clear()");
        sched.drain(PoolType::ThumbnailPool, kDrain);
    }
    CHECK(delivered.load() >= 1, "delivery happened before clear()");
    CHECK(pipe.pendingCount() == 0, "pending converges to zero after reentrant clear");
    CHECK(pipe.handlesCount() == 0, "handles converge to zero after reentrant clear");
}

// ─── 4. throwing result callback is contained ──────────────────────────────
void testThrowingResultCallback()
{
    printf("\n[4. throwing result callback is contained]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setPoolMaxThreads(PoolType::ThumbnailPool, 2);

    ThumbnailPipeline pipe;
    std::atomic<int> deliveries{0};
    pipe.setDecodeFn([](const std::string &, int size)
                     { return makeImageData(size, size, PixelFormat::RGB24); });
    pipe.setResultFn(
        [&](const std::string &, int, const ImageData &)
        {
            deliveries.fetch_add(1);
            throw std::runtime_error("result callback boom");
        });
    pipe.setSources({"a/img0.png", "a/img1.png"});
    pipe.setVisibleRange(0, 2);
    sched.drain(PoolType::ThumbnailPool, kDrain);
    CHECK(deliveries.load() >= 1, "deliveries happened before the callback threw");
    CHECK(pipe.pendingCount() == 0, "pending converges to zero after throwing callback");
    CHECK(pipe.handlesCount() == 0, "handles converge to zero after throwing callback");
}

// ─── 5. throwing decoder: no leaked pending/handle, key stays schedulable ──
void testThrowingDecoder()
{
    printf("\n[5. throwing decoder leaves no pending/handle residue]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setPoolMaxThreads(PoolType::ThumbnailPool, 1);

    ThumbnailPipeline pipe;
    std::atomic<int> decodes{0};
    std::atomic<int> deliveries{0};
    pipe.setDecodeFn(
        [&](const std::string &, int size) -> ImageData
        {
            if (decodes.fetch_add(1) == 0)
                throw std::runtime_error("decode boom");
            return makeImageData(size, size, PixelFormat::RGB24);
        });
    pipe.setResultFn(
        [&](const std::string &, int, const ImageData &img)
        {
            if (!img.isNull())
                deliveries.fetch_add(1);
        });
    pipe.setSources({"a/img0.png"});
    pipe.setVisibleRange(0, 1);
    sched.drain(PoolType::ThumbnailPool, kDrain);
    CHECK(pipe.pendingCount() == 0, "pending converges to zero after throwing decoder");
    CHECK(pipe.handlesCount() == 0, "handles converge to zero after throwing decoder");
    // Same key must be schedulable again (the failure must not poison it).
    pipe.setSources({"a/img0.png"});
    pipe.setVisibleRange(0, 1);
    sched.drain(PoolType::ThumbnailPool, kDrain);
    CHECK(deliveries.load() >= 1, "the same key decodes successfully on retry");
    CHECK(pipe.pendingCount() == 0 && pipe.handlesCount() == 0, "converges after the retry too");
}

// ─── 6. generation switch while the decoder throws ─────────────────────────
void testGenerationSwitchWithThrowingDecoder()
{
    printf("\n[6. generation switch while decoder throws/cancels]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setPoolMaxThreads(PoolType::ThumbnailPool, 1);

    ThumbnailPipeline pipe;
    std::atomic<int> decodes{0};
    std::atomic<int> deliveries{0};
    pipe.setDecodeFn(
        [&](const std::string &p, int size) -> ImageData
        {
            if (p.starts_with("bad/"))
                throw std::runtime_error("decode boom");
            if (p.starts_with("slow/"))
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return makeImageData(size, size, PixelFormat::RGB24);
        });
    pipe.setResultFn(
        [&](const std::string &, int, const ImageData &img)
        {
            if (!img.isNull())
                deliveries.fetch_add(1);
        });

    // Gen A: slow task in flight, then a throwing task queued behind it.
    pipe.setSources({"slow/img0.png", "bad/img1.png"});
    pipe.setVisibleRange(0, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    // Gen B: switch while gen-A's throwing task is still queued.
    pipe.setSources({"b/img0.png", "b/img1.png"});
    pipe.setVisibleRange(0, 2);
    sched.drain(PoolType::ThumbnailPool, kDrain);

    CHECK(pipe.pendingCount() == 0, "pending converges to zero after gen switch + throw");
    CHECK(pipe.handlesCount() == 0, "handles converge to zero after gen switch + throw");
    CHECK(deliveries.load() >= 1, "current generation still delivers");
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("=== M27 ThumbnailPipeline callback/delivery edge cases ===\n");
    fflush(stdout);

    testCallbackReentersRequest();
    testCallbackReentersSetSources();
    testCallbackReentersClear();
    testThrowingResultCallback();
    testThrowingDecoder();
    testGenerationSwitchWithThrowingDecoder();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
