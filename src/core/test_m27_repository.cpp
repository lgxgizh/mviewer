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
#include "test_m27_repository_cases.inc"
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
