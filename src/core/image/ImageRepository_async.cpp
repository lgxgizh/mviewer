// M46: the cancellable-request contract lives in its own TU (split out of
// ImageRepository.cpp so the delivery-gate machinery — opaque request state,
// consumer lifetime tokens, the terminal delivery gate and cancelAsync's
// wait-for-in-flight-delivery semantics — stays a bounded, testable unit).
#include "core/image/ImageRepository.h"

#include "core/scheduler/TaskScheduler.h"
#include "core/trace/Trace.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// M29: opaque cancellable-request state. Defined here (not in the header)
// so UI code only ever holds the shared_ptr handle and never touches
// TaskScheduler types. The state is kept alive by the worker closures (work +
// done) for the lifetime of the request. Every mutable field (kind/phase/path/
// callback/result/handle) is guarded by mtx; the client callback is NEVER
// invoked while holding mtx. cancelled is atomic so cancellation is observable
// without locking. Declared `class` in the header; the out-of-line definition
// matches with an explicit public section.
class ImageRepository::AsyncRequestState
{
  public:
    enum class Kind
    {
        Foreground,
        Preload
    };
    enum class Phase
    {
        Queued,
        Running,
        Finished,
        Cancelled
    };

    std::mutex mtx;
    Kind kind = Kind::Foreground;
    Phase phase = Phase::Queued;
    std::string path;
    std::function<void(const Result &)> callback;
    TaskScheduler::TaskHandle handle;
    std::atomic<bool> cancelled{false};
    ImageRepository::Result result;
    // M46: consumer-lifetime token (weak). Expired/invalidated => delivery is
    // suppressed before the client callback starts.
    std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime;
    // M46 delivery gate: serializes "terminal delivery" against cancelAsync().
    // The client callback runs OUTSIDE deliveryMtx (so a callback that calls
    // cancelAsync() for its own request cannot deadlock); cancelAsync() waits
    // on deliveryCv for an already-started delivery to finish, which makes
    // "no client callback runs or will start after cancelAsync() returns" an
    // airtight, testable property. A re-entrant cancelAsync() from the very
    // thread that is executing the delivery skips the wait (it would wait for
    // itself); the delivery completes normally right after the callback.
    std::mutex deliveryMtx;
    bool deliveryStarted = false;
    bool deliveryDone = false;
    std::thread::id deliveryThreadId;
    std::condition_variable deliveryCv;
};

// M46: terminal-delivery helper shared by every async request kind. Runs on
// the worker thread. Returns true when the caller must invoke `cb(res)`; the
// delivery-gate protocol guarantees the client callback is invoked at most
// once and only while the request is neither cancelled nor owned by a dead
// consumer.
static bool beginClientDelivery(const std::shared_ptr<ImageRepository::AsyncRequestState> &state,
                                ImageRepository::Result &res,
                                std::function<void(const ImageRepository::Result &)> &cb)
{
    std::unique_lock<std::mutex> gk(state->deliveryMtx);
    if (state->cancelled.load(std::memory_order_acquire))
        return false;
    // The lifetime token is OPTIONAL: an empty weak_ptr (no token supplied)
    // means the caller takes no lifetime restriction. Only a supplied token
    // that is expired (consumer destroyed) or invalidated suppresses delivery.
    if (!state->lifetime.expired())
    {
        const std::shared_ptr<mviewer::core::AsyncLifetimeToken> token = state->lifetime.lock();
        if (token && !token->isAlive())
            return false;
    }
    if (state->deliveryStarted || state->deliveryDone)
        return false; // defensive: exactly-once even under re-entrancy
    state->deliveryStarted = true;
    state->deliveryThreadId = std::this_thread::get_id();
    gk.unlock();
    return true;
}

// M46: test-hook invocation. The hooks are std::function values that tests may
// leave EMPTY; invoking an empty std::function throws bad_function_call, which
// must never escape the worker (the scheduler contains done-callback
// exceptions, which would silently abort the delivery before the client
// callback runs). The catch turns an empty hook into a no-op on every
// toolchain, including ones whose std::function operator bool is unreliable.
static void invokeDeliveryHook(const std::function<void()> &hook)
{
    try
    {
        hook();
    }
    catch (...)
    {
    }
}

static void finishClientDelivery(
    const std::shared_ptr<ImageRepository::AsyncRequestState> &state)
{
    {
        std::lock_guard<std::mutex> gk(state->deliveryMtx);
        state->deliveryDone = true;
    }
    state->deliveryCv.notify_all();
}

ImageRepository::TestHooks &ImageRepository::testHooks()
{
    static TestHooks hooks;
    return hooks;
}

ImageRepository::Result ImageRepository::load(const std::string &filePath, const LoadOptions &opts)
{
    MV_TRACE_SCOPED("ImageRepository::load");
    Result res;
    std::string key = cachedKeyForPath(filePath);
    const bool hadCachedKey = !key.empty();
    if (key.empty())
        key = makeKey(filePath);
    if (loadMemoryHit(filePath, key, opts, res))
        return res;

    // A stale/missing warm entry must revalidate the identity before falling
    // through to disk; this keeps file-modification correctness without adding
    // filesystem work to a successful warm-memory hit.
    if (hadCachedKey)
    {
        const std::string validated = makeKey(filePath);
        if (validated != key)
            key = validated;
    }

    ImageData pixels;
    bool fromCache = false;
    mviewer::domain::ImageMetadata decodeMeta;
    if (!loadPixels(filePath, key, opts, pixels, fromCache, decodeMeta, res.error))
        return res;

    auto frame = std::make_shared<ImageFrame>(ImageFrame::create(filePath, pixels));
    enrichFrame(*frame, filePath, pixels, decodeMeta);
    restoreRaw16(*frame, filePath, key);
    if (opts.generateHistogram)
        frame->computeHistogram();
    frame->setDecodeState(DecodeState::Decoded);
    frame->setCacheState(fromCache ? CacheState::Disk : CacheState::None);
    CacheManager::instance().putMemory(CacheLevel::FullImage, key, pixels);
    CacheManager::instance().putMetadata(key, frame->metadata());
    res.frame = frame;
    res.fromCache = fromCache;
    return res;
}


void ImageRepository::loadAsync(const std::string &filePath,
                                std::function<void(const Result &)> callback,
                                const LoadOptions &opts)
{
    // M29: keep the legacy entry point as a thin wrapper so existing callers
    // keep their exact source/API behavior and exactly-once delivery for
    // non-cancelled requests. The returned handle is discarded; the request
    // state is kept alive by the worker closures.
    loadAsyncCancellable(filePath, std::move(callback), opts);
}

ImageRepository::AsyncRequestHandle
ImageRepository::loadAsyncCancellable(const std::string &filePath,
                                      std::function<void(const Result &)> callback,
                                      const LoadOptions &opts,
                                      std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime)
{
    auto state = std::make_shared<AsyncRequestState>();
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        state->kind = AsyncRequestState::Kind::Foreground;
        state->phase = AsyncRequestState::Phase::Queued;
        state->path = filePath;
        state->callback = std::move(callback);
        state->lifetime = std::move(lifetime);
    }
    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::DecodePool,
        [this, filePath, opts, state]()
        {
            {
                std::lock_guard<std::mutex> lk(state->mtx);
                if (state->cancelled.load(std::memory_order_acquire) ||
                    state->phase != AsyncRequestState::Phase::Queued)
                    return;
                state->phase = AsyncRequestState::Phase::Running;
            }
            // Decode outside the lock; only a Running, uncancelled request may
            // publish its transient result.
            Result res = load(filePath, opts);
            {
                std::lock_guard<std::mutex> lk(state->mtx);
                if (state->cancelled.load(std::memory_order_acquire) ||
                    state->phase != AsyncRequestState::Phase::Running)
                    return;
                state->result = std::move(res);
            }
        },
        [state]()
        {
            // Terminal transition: mark Finished, move the result/callback out,
            // and clear the transient Result (never retained after terminal
            // done). The client callback is invoked outside the state mutex,
            // exactly once, only when the request was not cancelled and its
            // consumer lifetime token is still alive.
            Result res;
            std::function<void(const Result &)> cb;
            bool deliver = false;
            {
                std::lock_guard<std::mutex> lk(state->mtx);
                state->phase = AsyncRequestState::Phase::Finished;
                if (!state->cancelled.load(std::memory_order_acquire))
                {
                    deliver = true;
                    res = std::move(state->result);
                    cb = std::move(state->callback);
                }
                state->result = {};
                state->callback = {};
            }
            if (!deliver)
                return;
            // M46 delivery gate: serialized with cancelAsync(); a cancelled
            // request or a dead consumer suppresses the callback BEFORE it
            // starts. The test hook lets deterministic tests pause the worker
            // at this exact point (decode-done vs cancel vs consumer-death
            // interleavings).
            if (!beginClientDelivery(state, res, cb))
                return;
            invokeDeliveryHook(ImageRepository::testHooks().onBeforeDelivery);
            try
            {
                cb(res);
            }
            catch (...)
            {
                // A throwing client callback must never strand the delivery
                // gate: cancelAsync() waits on deliveryDone.
            }
            invokeDeliveryHook(ImageRepository::testHooks().onAfterDelivery);
            finishClientDelivery(state);
        });
    if (!handle)
    {
        // M27: a rejected submission must not silently lose the request - the
        // caller must never have to infer failure from a missing callback.
        // The callback fires EXACTLY ONCE, here on the calling thread, with an
        // explicit rejection error (the worker-thread delivery path covers the
        // accepted case). M46: a dead consumer token still suppresses it.
        std::function<void(const Result &)> cb;
        {
            std::lock_guard<std::mutex> lk(state->mtx);
            cb = std::move(state->callback);
        }
        // M46: a dead consumer token (expired OR invalidated) suppresses even
        // the synchronous rejection callback - no client callback may start
        // once the consumer is gone.
        bool tokenAlive = true;
        if (!state->lifetime.expired())
        {
            const auto tok = state->lifetime.lock();
            tokenAlive = tok && tok->isAlive();
        }
        if (tokenAlive)
        {
            Result err;
            err.error = "scheduler rejected submission for: " + filePath;
            try
            {
                cb(err);
            }
            catch (...)
            {
                // A throwing rejection callback must not escape the caller.
            }
        }
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        state->handle = handle;
    }
    return state;
}

ImageRepository::AsyncRequestHandle ImageRepository::preloadAsync(
    const std::string &filePath, std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime)
{
    auto state = std::make_shared<AsyncRequestState>();
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        state->kind = AsyncRequestState::Kind::Preload;
        state->phase = AsyncRequestState::Phase::Queued;
        state->path = filePath;
        state->lifetime = std::move(lifetime);
    }
    LoadOptions opts;
    opts.useDiskCache = true;
    opts.generateHistogram = false;
    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [this, filePath, opts, state](const TaskScheduler::TaskContext &)
        {
            {
                std::lock_guard<std::mutex> lk(state->mtx);
                if (state->cancelled.load(std::memory_order_acquire) ||
                    state->phase != AsyncRequestState::Phase::Queued)
                    return;
                state->phase = AsyncRequestState::Phase::Running;
            }
            // Best-effort cache warm only: the transient frame is stored so a
            // concurrent promotion can deliver it; a pure preload releases it
            // at completion (the FullImage memory cache populated by load() is
            // the only observable effect).
            Result res = load(filePath, opts);
            {
                std::lock_guard<std::mutex> lk(state->mtx);
                if (state->cancelled.load(std::memory_order_acquire) ||
                    state->phase != AsyncRequestState::Phase::Running)
                    return;
                state->result = std::move(res);
            }
        },
        {},                                           // deps
        std::chrono::steady_clock::time_point::max(), // deadline
        [state]()
        {
            // Terminal transition. A preload promoted to Foreground while
            // running delivers to the promoted callback (computing the
            // histogram the preload skipped); a pure preload releases its
            // transient Result and finishes with no delivery. The callback is
            // never invoked while holding the state mutex.
            bool promoted = false;
            bool cancelled = false;
            Result res;
            std::function<void(const Result &)> cb;
            {
                std::lock_guard<std::mutex> lk(state->mtx);
                state->phase = AsyncRequestState::Phase::Finished;
                cancelled = state->cancelled.load(std::memory_order_acquire);
                promoted = state->kind == AsyncRequestState::Kind::Foreground;
                if (promoted && !cancelled)
                {
                    res = std::move(state->result);
                    cb = std::move(state->callback);
                }
                state->result = {};
                state->callback = {};
            }
            if (!promoted || cancelled)
                return;
            if (res.success() && res.frame)
                res.frame->computeHistogram();
            // Re-check cancellation: the histogram pass took time and the
            // request may have been cancelled since the terminal transition.
            if (state->cancelled.load(std::memory_order_acquire))
                return;
            // M46: a pure preload has no client callback; a promoted preload
            // goes through the same delivery gate as a foreground load.
            if (!beginClientDelivery(state, res, cb))
                return;
            invokeDeliveryHook(ImageRepository::testHooks().onBeforeDelivery);
            try
            {
                cb(res);
            }
            catch (...)
            {
            }
            invokeDeliveryHook(ImageRepository::testHooks().onAfterDelivery);
            finishClientDelivery(state);
        });
    if (!handle)
        return nullptr;
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        state->handle = handle;
    }
    return state;
}

ImageRepository::AsyncRequestHandle
ImageRepository::promotePreloadAsync(AsyncRequestHandle &preload,
                                     std::function<void(const Result &)> callback,
                                     std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime)
{
    // Consumes the preload handle; nullptr is a no-op. The client callback is
    // never invoked and scheduler/loadAsync are never touched while holding the
    // state mutex.
    if (!preload)
        return nullptr;
    std::string path;
    TaskScheduler::TaskHandle staleHandle;
    AsyncRequestHandle promoted;
    {
        std::lock_guard<std::mutex> lk(preload->mtx);
        if (preload->kind != AsyncRequestState::Kind::Preload)
            return nullptr;
        if (preload->phase == AsyncRequestState::Phase::Running)
        {
            // Reuse the running decode: promote the kind and stash the callback.
            // The preload's terminal done path then delivers to this callback -
            // a running promotion never submits a second decode. Keep a local
            // copy of the state so the caller's handle can be consumed (reset)
            // only after the mutex guard is released.
            preload->kind = AsyncRequestState::Kind::Foreground;
            preload->callback = std::move(callback);
            if (!lifetime.expired())
                preload->lifetime = std::move(lifetime);
            promoted = preload;
        }
        else
        {
            if (preload->phase == AsyncRequestState::Phase::Queued)
            {
                // Queued: suppress the stale Background preload and resubmit at
                // Decode priority so the foreground request is not starved
                // behind other background work.
                preload->cancelled.store(true, std::memory_order_release);
                preload->phase = AsyncRequestState::Phase::Cancelled;
                staleHandle = preload->handle;
                preload->handle = {};
                preload->callback = {};
                preload->result = {};
            }
            // The caller's `lifetime` stays with the caller here: the Queued
            // and Finished/Cancelled branches resubmit via loadAsyncCancellable
            // below, which receives it by move.
            path = preload->path;
        }
    }
    if (promoted)
    {
        preload.reset();
        return promoted;
    }
    if (staleHandle)
        TaskScheduler::cancel(staleHandle);
    // Finished/Cancelled: resubmit at Decode priority; a finished preload
    // already warmed the cache, so the foreground load resolves from cache.
    preload.reset();
    return loadAsyncCancellable(path, std::move(callback), kDefaultLoadOptions,
                                std::move(lifetime));
}

void ImageRepository::cancelAsync(AsyncRequestHandle &handle)
{
    if (!handle)
        return;
    // M29: best-effort cancellation. The atomic flag suppresses the client
    // callback; the scheduler soft-cancel skips queued work (a running QImage
    // decode is non-interruptible and may finish safely, merely warming cache).
    // Phase/fields are updated under the mutex; the callback and transient
    // Result are dropped and never delivered.
    TaskScheduler::TaskHandle staleHandle;
    {
        std::lock_guard<std::mutex> lk(handle->mtx);
        handle->cancelled.store(true, std::memory_order_release);
        handle->phase = AsyncRequestState::Phase::Cancelled;
        handle->callback = {};
        handle->result = {};
        staleHandle = handle->handle;
        handle->handle = {};
    }
    if (staleHandle)
        TaskScheduler::cancel(staleHandle);
    // M46 delivery gate: a terminal delivery that already started must be
    // allowed to finish (the client callback runs outside every lock and is
    // itself lifetime-guarded), but after cancelAsync() returns NO client
    // callback is running and none will start for this request. Waiting here
    // closes the check-then-call race between the worker's cancellation check
    // and its callback invocation. The wait is bounded by the callback itself:
    // a callback that re-enters cancelAsync() for its own request cannot
    // deadlock because the worker releases deliveryMtx while cb runs and only
    // needs it again to publish deliveryDone - and a re-entrant call from the
    // delivering thread skips the wait entirely (it would otherwise wait for
    // its own completion).
    {
        std::unique_lock<std::mutex> gk(handle->deliveryMtx);
        if (handle->deliveryStarted && !handle->deliveryDone &&
            std::this_thread::get_id() != handle->deliveryThreadId)
            handle->deliveryCv.wait(gk, [&]() { return handle->deliveryDone; });
    }
    handle.reset();
}
