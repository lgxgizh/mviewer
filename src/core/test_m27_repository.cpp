// M27 Phase 3 — ImageRepository timeout / rejection safety regression tests.
//
// Contracts under test (must hold after Phase 3 hardening):
//   1. loadAsync() must NEVER silently lose a request: on scheduler rejection
//      the callback fires EXACTLY ONCE, on the calling thread, with an
//      explicit rejection error.
//   2. loadDirectory() must honor its defensive budget (wall clock): it must
//      return within the budget even when accepted Decode tasks are stuck
//      behind a blocked pool.
//   3. loadDirectory() timeout must not leave late workers writing freed
//      stack state — worker state must use safe shared ownership and
//      outstanding work must be cancelled on timeout.
//   4. Late completions (workers resuming after the caller returned) must be
//      safe: no crash, no UAF, scheduler converges, results are explicit
//      failures.
//   5. Normal (no-timeout) directory load still succeeds.
//   6. A QUEUED loadAsyncCancellable request cancelled via cancelAsync() never
//      fires its callback and never warms the cache; DecodePool bookkeeping
//      and the dependency graph converge.
//   7. preloadAsync() submits at Background priority, returns an opaque
//      cancellable handle, and a cancelled queued preload warms nothing.
//   8. A histogram-free preload does not remove the histogram from the later
//      normal viewer load (the FullImage memory fast path honors
//      opts.generateHistogram).
//   9. A QUEUED preload promoted via promotePreloadAsync() is re-submitted at
//      Decode priority while the Background pool stays gated, decodes exactly
//      once, and delivers a histogram-bearing result.
//   10. A RUNNING preload promoted via promotePreloadAsync() reuses the
//       in-flight decode: no second DecodePool task is submitted, the returned
//       handle aliases the original request, and the result carries a
//       histogram after exactly one decode.
//   11. A FINISHED preload promoted via promotePreloadAsync() retains no
//       transient Result/ImageFrame (a live finished handle holds no frame),
//       falls back to the warmed FullImage cache with exactly one DecodePool
//       resubmission, and delivers a cache-backed, histogram-bearing result
//       after exactly one decode.
//   12. A RUNNING preload promoted and then cancelled via cancelAsync() never
//       fires the promoted callback, decodes exactly once, clears the caller's
//       handle, and leaves pools + the dependency graph converged.
//
// Pre-fix (M26) failures reproduced here: #1 (callback never fires on
// rejection), #2/#3/#4 (stack-reference capture; budget is a hard-coded
// 5-minute constant so the hazard cannot even be exercised).

#include "core/cache/CacheManager.h"
#include "core/image/Encoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/IDecoder.h"
#include "core/perf/MemoryTracker.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>
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
using PoolType = TaskScheduler::PoolType;
using Priority = TaskScheduler::Priority;
using TaskContext = TaskScheduler::TaskContext;

std::string makeDir(int count)
{
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    for (int i = 0; i < count; ++i)
    {
        QImage img(16, 16, QImage::Format_RGB32);
        img.fill(QColor((i * 7) % 256, (i * 13) % 256, (i * 29) % 256));
        const std::string path = tmp.path().toStdString() + "/img_" + std::to_string(i) + ".png";
        Encoder::encode(mvcore::fromQImage(img), path, Encoder::Params{});
    }
    return tmp.path().toStdString();
}

void cleanupDir(const std::string &dir)
{
    if (dir.empty())
        return;
    QDir qdir(QString::fromStdString(dir));
    qdir.removeRecursively();
}

// Reuses the (potentially freed) caller stack region while late workers fire.
volatile std::uint64_t g_churnSink = 0;
void churnStack()
{
    volatile char buf[65536];
    std::memset(const_cast<char *>(buf), 0xAA, sizeof(buf));
    g_churnSink += static_cast<std::uint64_t>(buf[4096]);
}

void writeDummyFile(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f)
    {
        std::fwrite("mvtest", 1, 6, f);
        std::fclose(f);
    }
}

// Live ImageFrame instances (not cache pixel buffers), for the transient
// Result-release assertions.
size_t liveFrames()
{
    return mviewer::perf::MemoryTracker::instance().sample().liveImageFrames;
}

// ─── Phase A: promotePreloadAsync regression scaffolding ────────────────────
// A test-only decoder claiming a unique extension so neither the memory nor
// the disk cache can satisfy the load before the decoder runs. The metadata
// decode overload counts calls, signals "entered", then blocks until
// "release" — a deterministic rendezvous for both promotion branches.
struct DecodeControl
{
    std::atomic<int> calls{0};
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
};

class BlockingCountingDecoder : public IDecoder
{
  public:
    explicit BlockingCountingDecoder(std::shared_ptr<DecodeControl> ctl) : m_ctl(std::move(ctl))
    {
    }

    bool canDecode(const std::string &path) const override
    {
        const std::string ext = ".mvtest";
        return path.size() > ext.size() &&
               path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
    }

    ImageData decodeFull(const std::string &path) const override
    {
        mviewer::domain::ImageMetadata meta;
        return decodeFull(path, meta);
    }

    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &outMeta) const override
    {
        m_ctl->calls.fetch_add(1, std::memory_order_relaxed);
        m_ctl->entered.store(true, std::memory_order_release);
        while (!m_ctl->release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        outMeta.width = 4;
        outMeta.height = 4;
        outMeta.channels = 3;
        outMeta.bitDepth = 8;
        outMeta.format = "TEST";
        outMeta.filePath = path;
        return makeImageData(4, 4, PixelFormat::RGB24);
    }

    ImageData decodeScaled(const std::string &path, int maxEdge) const override
    {
        (void)maxEdge;
        mviewer::domain::ImageMetadata meta;
        return decodeFull(path, meta);
    }

    std::vector<std::string> extensions() const override
    {
        return {"mvtest"};
    }

    const char *name() const override
    {
        return "BlockingCountingTestDecoder";
    }

  private:
    std::shared_ptr<DecodeControl> m_ctl;
};

// ─── 1. loadAsync rejection: exactly-once callback with explicit error ─────
void testLoadAsyncRejection()
{
    printf("\n[1. loadAsync: rejection fires the callback exactly once]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    sched.pause(PoolType::DecodePool);
    std::atomic<int> calls{0};
    std::string lastError;
    repo.loadAsync("C:/definitely/missing/file.png",
                   [&](const ImageRepository::Result &r)
                   {
                       calls.fetch_add(1);
                       lastError = r.error;
                   });
    repo.loadAsync("C:/definitely/missing/file2.png",
                   [&](const ImageRepository::Result &r)
                   {
                       calls.fetch_add(1);
                       lastError = r.error;
                   });
    // Callback must have fired synchronously on this (calling) thread.
    CHECK(calls.load() == 2, "each rejected request fires its callback exactly once");
    CHECK(lastError.find("rejected") != std::string::npos,
          "rejection error is explicit about the scheduler rejection");
    sched.resume(PoolType::DecodePool);
}

// ─── 2. loadAsync normal success path ──────────────────────────────────────
void testLoadAsyncSuccess()
{
    printf("\n[2. loadAsync: success path still delivers]\n");
    fflush(stdout);
    auto &repo = ImageRepository::instance();

    const std::string dir = makeDir(1);
    const std::string path = dir + "/img_0.png";
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
    repo.loadAsync(path,
                   [&](const ImageRepository::Result &r)
                   {
                       ok.store(r.success());
                       done.store(true);
                   });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(done.load(), "async success callback fires");
    CHECK(ok.load(), "async load succeeds on a real file");
    cleanupDir(dir);
}

// ─── 3. loadDirectory timeout: budget honored, late completion safe ────────
void testLoadDirectoryTimeoutLateCompletion()
{
    printf("\n[3. loadDirectory: budget honored + late completion is safe]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    sched.setPoolMaxThreads(PoolType::DecodePool, 1);
    repo.setSyncLoadBudget(std::chrono::milliseconds(400));
    const std::string dir = makeDir(8);

    // Saturate the single Decode worker: loadDirectory's submissions are
    // ACCEPTED (queued) but cannot start for 3 s — longer than the budget.
    auto blocker = sched.submit(Priority::Decode, [](const TaskContext &)
                                { std::this_thread::sleep_for(std::chrono::seconds(3)); });

    const auto t0 = std::chrono::steady_clock::now();
    auto results = repo.loadDirectory(dir, 1000);
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    printf("  loadDirectory returned in %lld ms\n",
           static_cast<long long>(
               std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
    CHECK(elapsed < std::chrono::seconds(2),
          "loadDirectory returns within the budget (does not wait for blocked tasks)");

    int timeoutCount = 0;
    for (const auto &r : results)
        if (!r.success() && r.error.find("timed out") != std::string::npos)
            ++timeoutCount;
    CHECK(timeoutCount >= 1, "timed-out items carry explicit failure results");

    // Late-completion window: churn the stack (reusing the region that used to
    // hold loadDirectory's stack locals) while the queued tasks eventually run
    // after the blocker finishes. Pre-fix this is a UAF write; post-fix the
    // worker state lives on the heap and the tasks exit via cancellation.
    const auto churnUntil = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < churnUntil)
    {
        for (int i = 0; i < 64; ++i)
            churnStack();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    const auto m = sched.metrics(PoolType::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "scheduler converges after late completion");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live handles after late completion");

    // Recover global state for subsequent tests.
    repo.setSyncLoadBudget(std::chrono::milliseconds(5 * 60 * 1000));
    sched.setPoolMaxThreads(PoolType::DecodePool, qMax(1, QThread::idealThreadCount()));
    cleanupDir(dir);
}

// ─── 4. loadDirectory normal completion sanity ─────────────────────────────
void testLoadDirectoryNormal()
{
    printf("\n[4. loadDirectory: normal completion sanity]\n");
    fflush(stdout);
    auto &repo = ImageRepository::instance();

    const std::string dir = makeDir(4);
    auto results = repo.loadDirectory(dir, 1000);
    int ok = 0;
    for (const auto &r : results)
        ok += r.success() ? 1 : 0;
    CHECK(ok == 4, "all 4 images load successfully without a timeout");
    cleanupDir(dir);
}

// ─── 5. histogram-free preload does not remove the later histogram ─────────
// A preload (generateHistogram=false) warms the FullImage memory cache. A
// subsequent normal load (generateHistogram=true) hits the memory fast path
// and must STILL return a frame with a histogram. Pre-fix the fast path
// returns before honoring opts.generateHistogram, so the histogram is lost.
void testPreloadHistogramPreserved()
{
    printf("\n[5. histogram-free preload keeps the later histogram]\n");
    fflush(stdout);
    auto &repo = ImageRepository::instance();

    const std::string dir = makeDir(1);
    const std::string path = dir + "/img_0.png";

    ImageRepository::LoadOptions warmOpts;
    warmOpts.useDiskCache = true;
    warmOpts.generateHistogram = false;
    auto warm = repo.load(path, warmOpts);
    CHECK(warm.success(), "histogram-free (preload) load succeeds");
    CHECK(!warm.frame->hasHistogram(), "histogram-free preload does not compute a histogram");

    auto normal = repo.load(path);
    CHECK(normal.success() && normal.fromCache, "normal load hits the FullImage memory fast path");
    CHECK(normal.frame->hasHistogram(),
          "normal load still returns a frame with a histogram after a histogram-free preload");
    cleanupDir(dir);
}

// ─── 6. preloadAsync: Background pool + opaque cancellable handle ──────────
// A queued preload must NOT run while a Background (MetadataPool) worker is
// occupied by a blocker, while a foreground loadAsyncCancellable on DecodePool
// still completes — proving preloads never take Decode priority.
void testPreloadAsyncBackgroundAndHandle()
{
    printf("\n[6. preloadAsync: Background pool + opaque cancellable handle]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    sched.setQueueMaxThreads(Priority::Background, 1);
    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drained before the preload test");
    const std::string dir = makeDir(2);
    const std::string preloadPath = dir + "/img_0.png";
    const std::string fgPath = dir + "/img_1.png";

    std::atomic<bool> gate{false};
    auto blocker = sched.submit(Priority::Background,
                                [&gate](const TaskContext &)
                                {
                                    while (!gate.load(std::memory_order_acquire))
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                });
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sched.metrics(PoolType::MetadataPool).active_tasks < 1 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(sched.metrics(PoolType::MetadataPool).active_tasks >= 1,
          "release-gated blocker occupies the single Background worker");

    ImageRepository::AsyncRequestHandle pre = repo.preloadAsync(preloadPath);
    CHECK(pre != nullptr, "preloadAsync returns an opaque cancellable request");

    // Give the preload a moment; behind the Background blocker it must stay
    // queued (never on DecodePool, never warming the cache).
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, repo.makeKey(preloadPath),
                                                  probe),
              "queued preload stays behind a Background blocker (not DecodePool)");
    }

    // A foreground load on DecodePool completes while Background is gated.
    std::atomic<bool> fgDone{false};
    std::atomic<bool> fgOk{false};
    repo.loadAsyncCancellable(fgPath,
                              [&](const ImageRepository::Result &r)
                              {
                                  fgOk.store(r.success());
                                  fgDone.store(true);
                              });
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!fgDone.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(fgDone.load() && fgOk.load(),
          "foreground loadAsyncCancellable completes while Background is gated");
    {
        ImageData probe;
        CHECK(
            CacheManager::instance().getMemory(CacheLevel::FullImage, repo.makeKey(fgPath), probe),
            "foreground load warms the FullImage memory cache");
    }

    repo.cancelAsync(pre);
    CHECK(pre == nullptr, "cancelAsync clears the caller's handle");
    gate.store(true, std::memory_order_release);
    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "Background pool drains after the gate release");
    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "scheduler converges after a cancelled queued preload");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live scheduler handles after a cancelled queued preload");

    sched.setQueueMaxThreads(Priority::Background, std::max(1, QThread::idealThreadCount() / 2));
    cleanupDir(dir);
}

// ─── 7. cancelled queued preload leaves no FullImage entry ─────────────────
void testCancelQueuedPreloadLeavesNoCache()
{
    printf("\n[7. preloadAsync: cancelled queued preload warms nothing]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    sched.setQueueMaxThreads(Priority::Background, 1);
    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drained before the cancel test");
    const std::string dir = makeDir(1);
    const std::string path = dir + "/img_0.png";

    std::atomic<bool> gate{false};
    auto blocker = sched.submit(Priority::Background,
                                [&gate](const TaskContext &)
                                {
                                    while (!gate.load(std::memory_order_acquire))
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                });
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sched.metrics(PoolType::MetadataPool).active_tasks < 1 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto pre = repo.preloadAsync(path);
    CHECK(pre != nullptr, "preloadAsync accepted while Background is gated");

    // Wait until the preload is genuinely queued (blocker + preload pending).
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sched.metrics(PoolType::MetadataPool).pending < 2 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(sched.metrics(PoolType::MetadataPool).pending >= 2,
          "preload is queued behind the blocker");
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, repo.makeKey(path), probe),
              "queued preload has not warmed FullImage yet");
    }

    repo.cancelAsync(pre);
    CHECK(pre == nullptr, "cancelAsync clears the caller's handle");
    gate.store(true, std::memory_order_release);
    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "Background pool drains after the gate release");
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, repo.makeKey(path), probe),
              "cancelled queued preload leaves no FullImage memory cache entry");
    }
    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "scheduler converges after the cancelled preload");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live scheduler handles after the cancelled preload");

    sched.setQueueMaxThreads(Priority::Background, std::max(1, QThread::idealThreadCount() / 2));
    cleanupDir(dir);
}

// ─── 8. cancelled queued foreground: no callback, no cache warm ─────────────
// A QUEUED loadAsyncCancellable (accepted but not yet running behind a
// release-gated Decode blocker) cancelled via cancelAsync() must never fire its
// callback nor warm the cache, and DecodePool bookkeeping + the graph converge.
void testCancelQueuedForegroundLoad()
{
    printf("\n[8. loadAsyncCancellable: cancelled queued foreground warms nothing]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    sched.setQueueMaxThreads(Priority::Decode, 1);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before the foreground-cancel test");
    const std::string dir = makeDir(1);
    const std::string path = dir + "/img_0.png";

    // Occupy the single Decode worker with a release-gated blocker so the
    // foreground request is accepted (queued) but cannot start.
    std::atomic<bool> gate{false};
    auto blocker = sched.submit(Priority::Decode,
                                [&gate](const TaskContext &)
                                {
                                    while (!gate.load(std::memory_order_acquire))
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                });
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sched.metrics(PoolType::DecodePool).active_tasks < 1 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(sched.metrics(PoolType::DecodePool).active_tasks >= 1,
          "release-gated blocker occupies the single Decode worker");

    std::atomic<bool> callbackFired{false};
    ImageRepository::AsyncRequestHandle req = repo.loadAsyncCancellable(
        path, [&](const ImageRepository::Result &) { callbackFired.store(true); });
    CHECK(req != nullptr, "queued foreground request accepted while Decode is gated");

    // Prove the request is queued behind the blocker (2 pending: blocker + req).
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sched.metrics(PoolType::DecodePool).pending < 2 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(sched.metrics(PoolType::DecodePool).pending >= 2,
          "queued foreground request is behind the Decode blocker");
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, repo.makeKey(path), probe),
              "queued foreground request has not warmed FullImage yet");
    }

    repo.cancelAsync(req);
    CHECK(req == nullptr, "cancelAsync clears the caller's foreground handle");
    gate.store(true, std::memory_order_release);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drains after the gate release");
    CHECK(!callbackFired.load(), "cancelled queued foreground never fires its callback");
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, repo.makeKey(path), probe),
              "cancelled queued foreground leaves no FullImage memory cache entry");
    }
    const auto m = sched.metrics(PoolType::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "DecodePool converges after the cancelled foreground request");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live scheduler handles after the cancelled foreground request");

    sched.setPoolMaxThreads(PoolType::DecodePool, qMax(1, QThread::idealThreadCount()));
    cleanupDir(dir);
}

// ─── 9. queued preload promoted: re-submitted at Decode, one decode ─────────
void testPromoteQueuedPreload()
{
    printf("\n[9. promotePreloadAsync: queued preload promoted to Decode]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    sched.setQueueMaxThreads(Priority::Background, 1);
    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drained before the queued-promotion test");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before the queued-promotion test");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();
    const std::string path = dir + "/img_0.mvtest";
    writeDummyFile(path);

    // Occupy the single Background worker so the preload is accepted (queued)
    // but cannot start until the gate releases.
    std::atomic<bool> bgGate{false};
    auto blocker = sched.submit(Priority::Background,
                                [&bgGate](const TaskContext &)
                                {
                                    while (!bgGate.load(std::memory_order_acquire))
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                });
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sched.metrics(PoolType::MetadataPool).active_tasks < 1 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(sched.metrics(PoolType::MetadataPool).active_tasks >= 1,
          "release-gated blocker occupies the single Background worker");

    ImageRepository::AsyncRequestHandle pre = repo.preloadAsync(path);
    CHECK(pre != nullptr, "preload accepted while Background is gated");
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sched.metrics(PoolType::MetadataPool).pending < 2 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(sched.metrics(PoolType::MetadataPool).pending >= 2,
          "preload is queued behind the Background blocker");

    std::atomic<bool> fgDone{false};
    std::atomic<bool> fgOk{false};
    std::atomic<bool> fgHistogram{false};
    ImageRepository::AsyncRequestHandle fg =
        repo.promotePreloadAsync(pre,
                                 [&](const ImageRepository::Result &r)
                                 {
                                     fgOk.store(r.success());
                                     fgHistogram.store(r.frame && r.frame->hasHistogram());
                                     fgDone.store(true);
                                 });
    CHECK(pre == nullptr, "promotePreloadAsync consumes the queued preload handle");
    CHECK(fg != nullptr, "promoted queued preload returns a foreground handle");

    // Release the decoder gate so the promoted Decode-priority load completes
    // while the Background blocker stays held (the cancelled preload remains
    // queued behind it and must never decode).
    ctl->release.store(true, std::memory_order_release);
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!fgDone.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(fgDone.load() && fgOk.load(),
          "promoted queued preload delivers while the Background pool is gated");
    CHECK(fgHistogram.load(), "promoted queued preload result carries a histogram");
    CHECK(ctl->calls.load() == 1, "queued promotion decodes exactly once");

    bgGate.store(true, std::memory_order_release);
    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drains after the Background release");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drains after the queued promotion");
    CHECK(ctl->calls.load() == 1, "cancelled queued preload adds no second decode");
    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "MetadataPool converges after the queued promotion");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live scheduler handles after the queued promotion");

    ctl->release.store(true, std::memory_order_release);
    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    sched.setQueueMaxThreads(Priority::Background, std::max(1, QThread::idealThreadCount() / 2));
    cleanupDir(dir);
}

// ─── 10. running preload promoted: decode reused, no second submission ──────
void testPromoteRunningPreload()
{
    printf("\n[10. promotePreloadAsync: running preload reuses the decode]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drained before the running-promotion test");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before the running-promotion test");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();
    const std::string path = dir + "/img_0.mvtest";
    writeDummyFile(path);

    // No scheduler blocker: the preload starts immediately and blocks inside
    // the decoder until the gate releases.
    ImageRepository::AsyncRequestHandle pre = repo.preloadAsync(path);
    CHECK(pre != nullptr, "preloadAsync accepted with an idle Background pool");
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!ctl->entered.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(ctl->entered.load(), "preload decode is blocked on the decoder gate");
    CHECK(ctl->calls.load() == 1, "running preload performed exactly one decode");

    const uint64_t decodeSubmittedBefore = sched.metrics(PoolType::DecodePool).submitted;
    ImageRepository::AsyncRequestHandle original = pre;

    std::atomic<bool> fgDone{false};
    std::atomic<bool> fgOk{false};
    std::atomic<bool> fgHistogram{false};
    ImageRepository::AsyncRequestHandle fg =
        repo.promotePreloadAsync(pre,
                                 [&](const ImageRepository::Result &r)
                                 {
                                     fgOk.store(r.success());
                                     fgHistogram.store(r.frame && r.frame->hasHistogram());
                                     fgDone.store(true);
                                 });
    CHECK(pre == nullptr, "promotePreloadAsync consumes the running preload handle");
    CHECK(fg != nullptr, "running promotion returns a foreground handle");
    CHECK(fg == original, "running promotion aliases the in-flight decode (no new request state)");
    CHECK(sched.metrics(PoolType::DecodePool).submitted == decodeSubmittedBefore,
          "running promotion submits no second DecodePool task");

    ctl->release.store(true, std::memory_order_release);
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!fgDone.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(fgDone.load() && fgOk.load(),
          "running promotion delivers a successful result from the reused decode");
    CHECK(fgHistogram.load(), "running promotion result carries a histogram");
    CHECK(ctl->calls.load() == 1, "running promotion decoded exactly once");

    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drains after the running promotion");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drains after the running promotion");
    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "MetadataPool converges after the running promotion");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live scheduler handles after the running promotion");

    ctl->release.store(true, std::memory_order_release);
    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(dir);
}

// ─── 11. finished preload promoted: cache fallback + transient release ──────
// A preload that has already reached Phase::Finished (pure preload, opaque
// handle still alive) must release its transient Result/ImageFrame — the live
// handle retains no frame. Promoting it falls back to the warmed FullImage
// cache: exactly one DecodePool resubmission, no second decode, and a
// cache-backed, histogram-bearing result.
void testPromoteFinishedPreload()
{
    printf("\n[11. promotePreloadAsync: finished preload falls back to cache]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drained before the finished-promotion test");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before the finished-promotion test");
    const size_t framesBefore = liveFrames();

    auto ctl = std::make_shared<DecodeControl>();
    ctl->release.store(true, std::memory_order_release);
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();
    const std::string path = dir + "/img_0.mvtest";
    writeDummyFile(path);

    // Pure preload to completion: release=true so the decode never blocks, the
    // Background pool drains the request into Phase::Finished, and the opaque
    // handle stays alive.
    ImageRepository::AsyncRequestHandle pre = repo.preloadAsync(path);
    CHECK(pre != nullptr, "preloadAsync accepted with an idle Background pool");
    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "Background pool drains so the preload reaches Finished");
    CHECK(ctl->calls.load() == 1, "finished preload performed exactly one decode");
    CHECK(liveFrames() == framesBefore,
          "a live finished preload handle retains no transient ImageFrame");
    {
        ImageData probe;
        CHECK(CacheManager::instance().getMemory(CacheLevel::FullImage, repo.makeKey(path), probe),
              "finished preload warmed the FullImage memory cache");
    }

    const uint64_t decodeSubmittedBefore = sched.metrics(PoolType::DecodePool).submitted;
    std::atomic<bool> fgDone{false};
    std::atomic<bool> fgOk{false};
    std::atomic<bool> fgFromCache{false};
    std::atomic<bool> fgHistogram{false};
    ImageRepository::AsyncRequestHandle fg =
        repo.promotePreloadAsync(pre,
                                 [&](const ImageRepository::Result &r)
                                 {
                                     fgOk.store(r.success());
                                     fgFromCache.store(r.fromCache);
                                     fgHistogram.store(r.frame && r.frame->hasHistogram());
                                     fgDone.store(true);
                                 });
    CHECK(pre == nullptr, "promotePreloadAsync consumes the finished preload handle");
    CHECK(fg != nullptr, "finished promotion returns a foreground handle");
    CHECK(sched.metrics(PoolType::DecodePool).submitted == decodeSubmittedBefore + 1,
          "finished promotion resubmits exactly one DecodePool task");

    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!fgDone.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(fgDone.load() && fgOk.load(), "finished promotion delivers a successful result");
    CHECK(fgFromCache.load(), "finished promotion resolves from the warmed memory cache");
    CHECK(fgHistogram.load(), "finished promotion result carries a histogram");
    CHECK(ctl->calls.load() == 1, "finished promotion adds no second decode");

    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drains after the finished promotion");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drains after the finished promotion");
    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "MetadataPool converges after the finished promotion");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live scheduler handles after the finished promotion");
    CHECK(liveFrames() == framesBefore,
          "live frames return to baseline after the finished promotion");

    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(dir);
}

// ─── 12. running promoted preload cancelled: no callback, one decode ─────────
// A RUNNING preload promoted (kind switched to Foreground, callback stashed)
// and then cancelled via cancelAsync() must never deliver: the decode that was
// already in flight finishes once and only warms the cache, no second DecodePool
// task is submitted, the caller's handle is consumed, and everything converges.
void testCancelPromotedRunningPreload()
{
    printf("\n[12. promotePreloadAsync: cancelAsync on a running promoted request]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "MetadataPool drained before the promoted-cancel test");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before the promoted-cancel test");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string dir = tmp.path().toStdString();
    const std::string path = dir + "/img_0.mvtest";
    writeDummyFile(path);

    // No scheduler blocker: the preload starts immediately and blocks inside
    // the decoder until the gate releases.
    ImageRepository::AsyncRequestHandle pre = repo.preloadAsync(path);
    CHECK(pre != nullptr, "preloadAsync accepted with an idle Background pool");
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!ctl->entered.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(ctl->entered.load(), "preload decode is blocked on the decoder gate");
    CHECK(ctl->calls.load() == 1, "running preload performed exactly one decode");

    const uint64_t decodeSubmittedBefore = sched.metrics(PoolType::DecodePool).submitted;
    std::atomic<bool> fgDone{false};
    ImageRepository::AsyncRequestHandle fg =
        repo.promotePreloadAsync(pre, [&](const ImageRepository::Result &) { fgDone.store(true); });
    CHECK(pre == nullptr, "promotePreloadAsync consumes the running preload handle");
    CHECK(fg != nullptr, "running promotion returns a foreground handle");

    // Cancel the promoted request while the decode is still gated: the decode
    // is not interruptible, but the deliver path must be suppressed.
    repo.cancelAsync(fg);
    CHECK(fg == nullptr, "cancelAsync clears the promoted foreground handle");
    CHECK(!fgDone.load(), "cancelled promoted request never fires its callback");

    ctl->release.store(true, std::memory_order_release);
    CHECK(sched.drain(PoolType::MetadataPool, std::chrono::seconds(15)),
          "Background pool drains after the decoder release");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drains after the promoted cancellation");
    CHECK(!fgDone.load(), "cancelled promoted request callback stays silent after release");
    CHECK(ctl->calls.load() == 1, "cancelled promoted request decoded exactly once");
    CHECK(sched.metrics(PoolType::DecodePool).submitted == decodeSubmittedBefore,
          "cancelled promoted running request submits no DecodePool task");
    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "scheduler converges after the promoted cancellation");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live scheduler handles after the promoted cancellation");

    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(dir);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M27 ImageRepository timeout/rejection tests ===\n");
    fflush(stdout);

    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);

    testLoadAsyncRejection();
    testLoadAsyncSuccess();
    testLoadDirectoryTimeoutLateCompletion();
    testLoadDirectoryNormal();
    testPreloadHistogramPreserved();
    testPreloadAsyncBackgroundAndHandle();
    testCancelQueuedPreloadLeavesNoCache();
    testCancelQueuedForegroundLoad();
    testPromoteQueuedPreload();
    testPromoteRunningPreload();
    testPromoteFinishedPreload();
    testCancelPromotedRunningPreload();

    // Leave the scheduler clean.
    sched.resume(PoolType::DecodePool);
    sched.setMaxQueueDepth(PoolType::DecodePool, 1000);
    sched.setPoolMaxThreads(PoolType::DecodePool, qMax(1, QThread::idealThreadCount()));

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
