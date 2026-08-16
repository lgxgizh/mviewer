// M46 — deterministic async lifetime / cancellation contract tests.
//
// The contract under test (see ImageRepository::AsyncRequestState + the
// delivery gate in ImageRepository.cpp):
//
//   C1  A request whose consumer lifetime token is invalidated/expired BEFORE
//       terminal delivery never invokes its client callback — the late
//       completion is a no-op before any consumer-visible code runs.
//   C2  cancelAsync() on a request whose delivery has NOT started returns
//       without waiting and guarantees the callback never starts.
//   C3  cancelAsync() on a request whose delivery HAS started waits (bounded)
//       for that in-flight delivery to finish, so after cancelAsync() returns
//       no client callback is running and none will start. The delivery that
//       already began completes exactly once.
//   C4  A client callback may re-enter cancelAsync() for its own request
//       without deadlocking (the callback runs outside every lock; the gate
//       wait is released by the worker's finishClientDelivery).
//   C5  Superseded requests (A→B→A, cancel of the first) never deliver.
//   C6  After every interleaving the scheduler pools and the dependency graph
//       converge (pending/active/queue_depth/handles == 0).
//
// All interleavings are constructed with latches/barriers (blocking decoder +
// delivery-gate hooks), never with sleep-based "run it 1000 times" timing.

#include "core/async/AsyncLifetimeToken.h"
#include "core/cache/CacheManager.h"
#include "core/image/Encoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/IDecoder.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
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
using PoolType = TaskScheduler::PoolType;
using Priority = TaskScheduler::Priority;
using TaskContext = TaskScheduler::TaskContext;
using namespace mviewer::core;

void writeDummyFile(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f)
    {
        std::fwrite("mvtest", 1, 6, f);
        std::fclose(f);
    }
}

void cleanupDir(const std::string &dir)
{
    if (dir.empty())
        return;
    QDir qdir(QString::fromStdString(dir));
    qdir.removeRecursively();
}

// Test-only decoder: counts calls, signals "entered", then blocks until
// "release" — a deterministic rendezvous for decode-phase races.
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

bool waitFlag(const std::atomic<bool> &flag, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (flag.load(std::memory_order_acquire))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return flag.load(std::memory_order_acquire);
}

bool waitTrue(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

bool poolsConverged()
{
    for (int p = 0; p < 5; ++p)
    {
        const auto m = TaskScheduler::instance().metrics(static_cast<PoolType>(p));
        if (m.pending != 0 || m.active_tasks != 0 || m.queue_depth != 0 || m.waiting != 0)
            return false;
    }
    const auto g = TaskScheduler::instance().graphMetrics();
    return g.handles == 0 && g.deferred == 0 && g.dep_graph_entries == 0 &&
           g.dependents_entries == 0;
}

// ─── 1. consumer destroyed before decode completes: callback never invoked ──
void testDestroyedBeforeDecodeDone()
{
    printf("\n[1. token invalidated while decode is running -> no callback]\n");
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before test 1");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string path = tmp.path().toStdString() + "/img_0.mvtest";
    writeDummyFile(path);

    auto token = AsyncLifetimeToken::create();
    std::atomic<int> callbacks{0};
    auto handle = repo.loadAsyncCancellable(
        path,
        [&](const ImageRepository::Result &) { callbacks.fetch_add(1); },
        ImageRepository::kDefaultLoadOptions, token);
    CHECK(handle != nullptr, "request accepted");
    CHECK(waitFlag(ctl->entered, 5000), "decode entered (worker running)");

    token->invalidate(); // consumer destroyed while decode is in flight
    ctl->release.store(true, std::memory_order_release);
    CHECK(waitTrue([&] { return ctl->calls.load() == 1 && poolsConverged(); }, 5000),
          "decode finished and pools converged");
    CHECK(callbacks.load() == 0, "client callback NEVER invoked for a dead consumer");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)), "DecodePool drains");

    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path().toStdString());
}

// ─── 2. cancel before delivery starts: returns fast, callback never runs ────
void testCancelBeforeDelivery()
{
    printf("\n[2. cancel of a queued/running-but-undelivered request]\n");
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before test 2");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string path = tmp.path().toStdString() + "/img_1.mvtest";
    writeDummyFile(path);

    // Occupy the single Decode worker so the request stays queued.
    sched.setQueueMaxThreads(Priority::Decode, 1);
    std::atomic<bool> gate{false};
    auto blocker = sched.submit(Priority::Decode,
                                [&gate](const TaskContext &)
                                {
                                    while (!gate.load(std::memory_order_acquire))
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                });
    CHECK(waitTrue([&] { return sched.metrics(PoolType::DecodePool).active_tasks >= 1; }, 5000),
          "Decode blocker occupies the single worker");

    auto token = AsyncLifetimeToken::create();
    std::atomic<int> callbacks{0};
    auto handle = repo.loadAsyncCancellable(
        path,
        [&](const ImageRepository::Result &) { callbacks.fetch_add(1); },
        ImageRepository::kDefaultLoadOptions, token);
    CHECK(handle != nullptr, "request accepted while Decode is gated");

    const auto t0 = std::chrono::steady_clock::now();
    repo.cancelAsync(handle);
    const auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    CHECK(waitMs < 500, "cancelAsync returns promptly when no delivery started");
    CHECK(handle == nullptr, "caller's handle cleared by cancelAsync");

    gate.store(true, std::memory_order_release);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(10)),
          "DecodePool drains after the queued cancel");
    CHECK(callbacks.load() == 0, "cancelled queued request never invokes the callback");
    CHECK(ctl->calls.load() == 0, "cancelled queued request never decodes");
    CHECK(poolsConverged(), "all pools converged after the queued cancel");

    sched.setQueueMaxThreads(Priority::Decode, qMax(1, QThread::idealThreadCount()));
    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path().toStdString());
}

// ─── 3. cancel during terminal delivery: waits, in-flight delivery finishes ─
void testCancelDuringTerminalDelivery()
{
    printf("\n[3. cancel racing a delivery that already started]\n");
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before test 3");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string path = tmp.path().toStdString() + "/img_2.mvtest";
    writeDummyFile(path);

    auto token = AsyncLifetimeToken::create();
    std::atomic<int> callbacks{0};
    std::atomic<bool> cbStarted{false};
    std::atomic<bool> cbDone{false};

    // Hold the worker INSIDE the delivery (after the gate passed, before the
    // client callback) with a latch, and again after the callback returns.
    // Heap-allocated so even a failed test that returns early can never leave
    // the stuck worker holding references into a dead stack frame.
    struct DeliveryLatches
    {
        std::atomic<bool> inBeforeDelivery{false};
        std::atomic<bool> releaseDelivery{false};
        std::atomic<bool> afterDeliveryDone{false};
    };
    auto latches = std::make_shared<DeliveryLatches>();
    auto &hooks = ImageRepository::testHooks();
    hooks.onBeforeDelivery = [latches]()
    {
        latches->inBeforeDelivery.store(true, std::memory_order_release);
        while (!latches->releaseDelivery.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    hooks.onAfterDelivery = [latches]() { latches->afterDeliveryDone.store(true, std::memory_order_release); };

    auto handle = repo.loadAsyncCancellable(
        path,
        [&](const ImageRepository::Result &r)
        {
            callbacks.fetch_add(1);
            cbStarted.store(true, std::memory_order_release);
            if (r.success() && r.frame)
                cbDone.store(true, std::memory_order_release);
        },
        ImageRepository::kDefaultLoadOptions, token);
    CHECK(handle != nullptr, "request accepted");

    ctl->release.store(true, std::memory_order_release); // decode completes
    CHECK(waitFlag(latches->inBeforeDelivery, 5000), "worker entered the delivery gate");

    // cancelAsync from a helper thread: it MUST block until the in-flight
    // delivery finishes.
    std::atomic<bool> cancelReturned{false};
    std::thread canceller([&]()
                          {
                              repo.cancelAsync(handle);
                              cancelReturned.store(true, std::memory_order_release);
                          });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(!cancelReturned.load(std::memory_order_acquire),
          "cancelAsync waits while a delivery is in flight");
    CHECK(!cbStarted.load(std::memory_order_acquire),
          "client callback not yet started (held by the test hook)");

    latches->releaseDelivery.store(true, std::memory_order_release);
    canceller.join();
    CHECK(cancelReturned.load(), "cancelAsync returned after the delivery finished");
    CHECK(cbStarted.load() && cbDone.load(), "the started delivery completed exactly once");
    CHECK(callbacks.load() == 1, "client callback invoked exactly once");
    CHECK(latches->afterDeliveryDone.load(), "worker observed the delivery completion");
    CHECK(waitTrue(poolsConverged, 5000), "pools converged after the cancel-during-delivery");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)), "DecodePool drains");

    hooks.onBeforeDelivery = {};
    hooks.onAfterDelivery = {};
    sched.setQueueMaxThreads(Priority::Decode, qMax(1, QThread::idealThreadCount()));
    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path().toStdString());
}

// ─── 4. callback re-enters cancelAsync() for its own request: no deadlock ───
void testReentrantCancelFromCallback()
{
    printf("\n[4. client callback re-enters cancelAsync for its own request]\n");
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before test 4");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string path = tmp.path().toStdString() + "/img_3.mvtest";
    writeDummyFile(path);

    auto token = AsyncLifetimeToken::create();
    std::atomic<int> callbacks{0};
    std::atomic<bool> reentrantCancelReturned{false};
    // The handle must exist before the callback can fire (the decode is gated
    // below), so the re-entrant cancel inside the callback is well-defined.
    ImageRepository::AsyncRequestHandle handle;
    handle = repo.loadAsyncCancellable(
        path,
        [&](const ImageRepository::Result &)
        {
            callbacks.fetch_add(1);
            // Re-entrancy: the callback cancels ITS OWN request (a copy of the
            // handle). The delivery gate must not deadlock.
            repo.cancelAsync(handle);
            reentrantCancelReturned.store(true, std::memory_order_release);
        },
        ImageRepository::kDefaultLoadOptions, token);
    CHECK(handle != nullptr, "request accepted");
    CHECK(waitFlag(ctl->entered, 5000), "decode entered");
    ctl->release.store(true, std::memory_order_release);
    CHECK(waitFlag(reentrantCancelReturned, 5000),
          "re-entrant cancelAsync from inside the callback returns (no deadlock)");
    CHECK(callbacks.load() == 1, "exactly one delivery despite the re-entrant cancel");
    CHECK(waitTrue(poolsConverged, 5000), "pools converged after the re-entrant cancel");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)), "DecodePool drains");

    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path().toStdString());
}

// ─── 5. A→B→A supersession: only the latest request delivers ───────────────
void testSupersedeSamePath()
{
    printf("\n[5. A -> B -> A supersession on the same path]\n");
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before test 5");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string path = tmp.path().toStdString() + "/img_4.mvtest";
    writeDummyFile(path);

    auto token = AsyncLifetimeToken::create();
    std::atomic<int> firstCallbacks{0};
    std::atomic<int> lastCallbacks{0};
    auto first = repo.loadAsyncCancellable(
        path,
        [&](const ImageRepository::Result &) { firstCallbacks.fetch_add(1); },
        ImageRepository::kDefaultLoadOptions, token);
    CHECK(first != nullptr, "request A accepted");
    CHECK(waitFlag(ctl->entered, 5000), "decode A running");

    auto last = repo.loadAsyncCancellable(
        path,
        [&](const ImageRepository::Result &r)
        {
            if (r.success() && r.frame)
                lastCallbacks.fetch_add(1);
        },
        ImageRepository::kDefaultLoadOptions, token);
    CHECK(last != nullptr, "request C accepted (queued behind A)");

    repo.cancelAsync(first); // supersede A
    ctl->release.store(true, std::memory_order_release);
    CHECK(waitTrue([&] { return lastCallbacks.load() == 1; }, 5000),
          "latest request delivered");
    CHECK(firstCallbacks.load() == 0, "superseded request A never delivered");
    // A ran to completion (a running QImage decode is non-interruptible) and
    // warmed the FullImage cache; the latest request C is therefore served
    // from the warm cache — exactly one decoder invocation total.
    CHECK(ctl->calls.load() == 1,
          "exactly one decode ran (A); C was served by the warmed FullImage cache");
    CHECK(waitTrue(poolsConverged, 5000), "pools converged after supersession");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)), "DecodePool drains");

    sched.setQueueMaxThreads(Priority::Decode, qMax(1, QThread::idealThreadCount()));
    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path().toStdString());
}

// ─── 6. dead token at rejection time: no callback ───────────────────────────
void testRejectionWithDeadToken()
{
    printf("\n[6. scheduler rejection with a dead consumer token]\n");
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    sched.pause(PoolType::DecodePool); // paused pools reject submissions

    auto token = AsyncLifetimeToken::create();
    token->invalidate(); // consumer already gone
    std::atomic<int> callbacks{0};
    auto handle = repo.loadAsyncCancellable(
        "some/missing.mvtest",
        [&](const ImageRepository::Result &) { callbacks.fetch_add(1); },
        ImageRepository::kDefaultLoadOptions, token);
    CHECK(handle == nullptr, "rejected submission returns nullptr");
    CHECK(callbacks.load() == 0, "rejection callback suppressed for a dead consumer");

    sched.resume(PoolType::DecodePool);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)), "DecodePool drains");
}

// ─── 7. stress: rapid submit/cancel/destroy churn converges ─────────────────
void testStressChurn()
{
    printf("\n[7. stress: 300 submit/cancel/invalidate cycles]\n");
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "DecodePool drained before stress");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const std::string path = tmp.path().toStdString() + "/img_5.mvtest";
    writeDummyFile(path);

    std::atomic<int> callbacksAfterCancel{0};
    std::atomic<int> callbacks{0};
    for (int i = 0; i < 300; ++i)
    {
        auto token = AsyncLifetimeToken::create();
        auto handle = repo.loadAsyncCancellable(
            path,
            [&](const ImageRepository::Result &)
            {
                callbacks.fetch_add(1);
                // A delivery must never start once this token is dead.
                callbacksAfterCancel.fetch_add(1);
            },
            ImageRepository::kDefaultLoadOptions, token);
        if (i % 2 == 0)
        {
            token->invalidate(); // consumer "dies"
            repo.cancelAsync(handle);
        }
        else
        {
            repo.cancelAsync(handle);
            token->invalidate();
        }
    }
    // Release the decoder for whatever decodes were actually running, then
    // let every accepted task finish and converge.
    ctl->release.store(true, std::memory_order_release);
    CHECK(waitTrue(poolsConverged, 15000), "pools converge after 300-cycle churn");
    CHECK(callbacksAfterCancel.load() == 0,
          "no callback ever delivered after its request was cancelled/invalidated");
    CHECK(callbacks.load() == 0, "all 300 requests were cancelled before delivery");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)), "DecodePool drains");

    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path().toStdString());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M46 ImageRepository delivery-gate tests ===\n");
    fflush(stdout);

    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);

    testDestroyedBeforeDecodeDone();
    testCancelBeforeDelivery();
    testCancelDuringTerminalDelivery();
    testReentrantCancelFromCallback();
    testSupersedeSamePath();
    testRejectionWithDeadToken();
    testStressChurn();

    // Leave the scheduler clean.
    sched.resume(PoolType::DecodePool);
    sched.setMaxQueueDepth(PoolType::DecodePool, 1000);
    sched.setPoolMaxThreads(PoolType::DecodePool, qMax(1, QThread::idealThreadCount()));
    sched.setQueueMaxThreads(Priority::Decode, qMax(1, QThread::idealThreadCount()));

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
