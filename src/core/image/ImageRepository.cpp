#include "core/image/ImageRepository.h"

#include "core/cache/CacheManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/image/ImageFrame.h"
#include "core/image/MetadataReader.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/trace/Trace.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <atomic>
#include <exception>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>

ImageRepository &ImageRepository::instance()
{
    static ImageRepository inst;
    return inst;
}

void ImageRepository::setSyncLoadBudget(std::chrono::milliseconds budget)
{
    m_syncLoadBudgetMs.store(budget.count());
}

std::chrono::milliseconds ImageRepository::syncLoadBudget() const
{
    return std::chrono::milliseconds(m_syncLoadBudgetMs.load());
}

std::string ImageRepository::makeKey(const std::string &filePath) const
{
    const std::string key = mviewer::core::MetadataReader::key(filePath);
    rememberKey(filePath, key);
    return key;
}

std::string ImageRepository::cachedKeyForPath(const std::string &filePath) const
{
    std::lock_guard<std::mutex> lock(m_keyMtx);
    const auto it = m_keyByPath.find(filePath);
    return it == m_keyByPath.end() ? std::string() : it->second;
}

void ImageRepository::rememberKey(const std::string &filePath, const std::string &key) const
{
    std::lock_guard<std::mutex> lock(m_keyMtx);
    m_keyByPath[filePath] = key;
}

void ImageRepository::forgetKey(const std::string &filePath) const
{
    std::lock_guard<std::mutex> lock(m_keyMtx);
    m_keyByPath.erase(filePath);
}

mviewer::domain::ImageMetadata ImageRepository::makeMeta(const std::string &filePath) const
{
    return mviewer::core::MetadataReader::read(filePath);
}

bool ImageRepository::getPreviewCache(const std::string &key, ImageData &out) const
{
    return CacheManager::instance().getMemory(CacheLevel::Preview, key, out);
}

void ImageRepository::putPreviewCache(const std::string &key, const ImageData &image)
{
    CacheManager::instance().putMemory(CacheLevel::Preview, key, image);
}

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
};

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
                                      const LoadOptions &opts)
{
    auto state = std::make_shared<AsyncRequestState>();
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        state->kind = AsyncRequestState::Kind::Foreground;
        state->phase = AsyncRequestState::Phase::Queued;
        state->path = filePath;
        state->callback = std::move(callback);
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
            // done). The client callback is invoked outside the lock, exactly
            // once, only when the request was not cancelled.
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
            if (deliver && cb && !state->cancelled.load(std::memory_order_acquire))
                cb(res);
        });
    if (!handle)
    {
        // M27: a rejected submission must not silently lose the request — the
        // caller must never have to infer failure from a missing callback.
        // The callback fires EXACTLY ONCE, here on the calling thread, with an
        // explicit rejection error (the worker-thread delivery path covers the
        // accepted case).
        std::function<void(const Result &)> cb;
        {
            std::lock_guard<std::mutex> lk(state->mtx);
            cb = std::move(state->callback);
        }
        if (cb)
        {
            Result err;
            err.error = "scheduler rejected submission for: " + filePath;
            cb(err);
        }
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        state->handle = handle;
    }
    return state;
}

ImageRepository::AsyncRequestHandle ImageRepository::preloadAsync(const std::string &filePath)
{
    auto state = std::make_shared<AsyncRequestState>();
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        state->kind = AsyncRequestState::Kind::Preload;
        state->phase = AsyncRequestState::Phase::Queued;
        state->path = filePath;
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
            if (!promoted || cancelled || !cb)
                return;
            if (res.success() && res.frame)
                res.frame->computeHistogram();
            // Re-check cancellation: the histogram pass took time and the
            // request may have been cancelled since the terminal transition.
            if (state->cancelled.load(std::memory_order_acquire))
                return;
            cb(res);
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
                                     std::function<void(const Result &)> callback)
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
            // The preload's terminal done path then delivers to this callback —
            // a running promotion never submits a second decode. Keep a local
            // copy of the state so the caller's handle can be consumed (reset)
            // only after the mutex guard is released.
            preload->kind = AsyncRequestState::Kind::Foreground;
            preload->callback = std::move(callback);
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
    return loadAsyncCancellable(path, std::move(callback));
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
    handle.reset();
}

std::vector<ImageRepository::Result> ImageRepository::loadDirectory(const std::string &dirPath,
                                                                    int maxImages)
{
    MV_TRACE_SCOPED("ImageRepository::loadDirectory");
    // M27: every piece of worker state lives in shared ownership. The previous
    // implementation captured STACK references (files/results/completed); if an
    // accepted task outlived the defensive timeout, a late worker wrote freed
    // stack memory after this function returned. Nothing here may outlive the
    // call except through shared_ptr.
    auto files =
        std::make_shared<std::vector<std::string>>(FileSystem::listImages(dirPath, maxImages));
    if (files->empty())
        return {};

    const int n = static_cast<int>(files->size());
    auto results = std::make_shared<std::vector<Result>>(n);
    auto completed = std::make_shared<std::atomic<int>>(0);
    auto handles = std::make_shared<std::vector<TaskScheduler::TaskHandle>>(n);

    // M26: every submission is accounted for — a task that the scheduler
    // REJECTS (saturated / paused pool) becomes an explicit failure Result
    // instead of a silently dropped submission. No global queue-depth
    // mutation is needed and none is performed: the caller's scheduler
    // configuration stays untouched.
    for (int i = 0; i < n; ++i)
    {
        auto handle = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Decode,
            [this, files, results, completed, n, i](const TaskScheduler::TaskContext &ctx)
            {
                try
                {
                    if (ctx.isCancelled())
                    {
                        // M27: the defensive timeout cancelled this task while
                        // it was still queued — report an explicit timeout
                        // failure instead of decoding into a superseded load.
                        Result err;
                        err.error = "loadDirectory timed out for: " + (*files)[i];
                        (*results)[i] = std::move(err);
                    }
                    else
                    {
                        Result loaded = load((*files)[i]);
                        // A timeout can race a decode that was already
                        // running. Do not publish a late result into the
                        // shared vector after the caller has begun returning;
                        // the timeout path below owns the terminal failure
                        // value for any unfinished slot.
                        if (!ctx.isCancelled())
                            (*results)[i] = std::move(loaded);
                    }
                }
                catch (...)
                {
                    Result err;
                    err.error = "load threw for: " + (*files)[i];
                    (*results)[i] = err;
                }
                completed->fetch_add(1, std::memory_order_release);
            });
        if (!handle)
        {
            Result err;
            err.error = "scheduler rejected submission for: " + (*files)[i];
            (*results)[i] = std::move(err);
            completed->fetch_add(1, std::memory_order_release);
        }
        else
        {
            (*handles)[i] = handle;
        }
    }

    // Bounded wait: accepted tasks either run to completion or are terminal;
    // the overall budget is a defensive cap against a hung decode so this call
    // can never busy-wait forever (e.g. while the pool is paused/shut down).
    const auto deadline = std::chrono::steady_clock::now() + syncLoadBudget();
    while (completed->load(std::memory_order_acquire) < n &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (completed->load(std::memory_order_acquire) < n)
    {
        // M27: actively cancel the outstanding work. Queued tasks observe the
        // token (see the isCancelled branch above) and exit with an explicit
        // timeout result. Use cancelTree rather than the scheduler's soft
        // cancel so queued work is finalized immediately instead of remaining
        // behind a blocked worker until that worker happens to drain. A
        // decoder that cannot be interrupted mid-flight is still safe because
        // every worker state handle is shared-owned; late completions never
        // touch freed memory.
        for (auto &h : *handles)
        {
            if (h)
                TaskScheduler::cancelTree(h->id);
        }
    }
    for (int i = 0; i < n; ++i)
    {
        if (!(*results)[i].frame && (*results)[i].error.empty())
        {
            Result err;
            err.error = "loadDirectory timed out for: " + (*files)[i];
            (*results)[i] = std::move(err);
        }
    }
    return *results;
}

void ImageRepository::loadDirectoryAsync(const std::string &dirPath,
                                         std::function<void(std::vector<Result>)> callback,
                                         int maxImages)
{
    loadDirectoryAsyncImpl(dirPath, std::move(callback), maxImages);
}

void ImageRepository::loadDirectoryAsyncImpl(const std::string &dirPath,
                                             std::function<void(std::vector<Result>)> callback,
                                             int maxImages)
{
    auto files =
        std::make_shared<std::vector<std::string>>(FileSystem::listImages(dirPath, maxImages));
    if (files->empty())
    {
        auto cb = std::move(callback);
        cb({});
        return;
    }

    const int n = static_cast<int>(files->size());
    auto results = std::make_shared<std::vector<Result>>(n);
    auto completed = std::make_shared<std::atomic<int>>(0);
    auto callbackPtr =
        std::make_shared<std::function<void(std::vector<Result>)>>(std::move(callback));

    // Fire the aggregate exactly once when the last item is accounted for
    // (whether it ran or was rejected at submit time).
    auto finishIfLast = [results, completed, n, callbackPtr](int idx)
    {
        const int prev = completed->fetch_add(1, std::memory_order_acq_rel);
        if (prev + 1 == n)
        {
            auto cb = *callbackPtr;
            auto resultCopy = *results;
            cb(resultCopy);
        }
    };

    for (int i = 0; i < n; ++i)
    {
        auto handle = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Decode,
            [this, files, results, n, i, finishIfLast](const TaskScheduler::TaskContext &)
            {
                try
                {
                    // Directory pre-decode produces browse/thumbnail-sized
                    // frames, NOT full-resolution pixels. Full decode is
                    // done on demand when the user opens a single image
                    // (load()). This matches the product flow (open
                    // directory -> thumbnails) and, critically, avoids
                    // QImageReader::read() at full resolution: under the
                    // offscreen platform (and likely Windows too) a fully
                    // concurrent QImageReader::read() deadlocks the
                    // worker pool, which hung the M3 acceptance test
                    // forever and would freeze the UI on a large
                    // directory. The scaled path (setScaledSize +
                    // read()) does not hit that deadlock.
                    static constexpr int kBrowseEdge = 256;
                    ImageData thumb = Decoder::decodeScaled((*files)[i], kBrowseEdge);
                    if (thumb.isNull())
                    {
                        Result err;
                        err.error = "decode failed for: " + (*files)[i];
                        (*results)[i] = std::move(err);
                    }
                    else
                    {
                        Result r;
                        r.frame =
                            std::make_shared<ImageFrame>(ImageFrame::create((*files)[i], thumb));
                        r.fromCache = false;
                        (*results)[i] = std::move(r);
                    }
                }
                catch (...)
                {
                    Result err;
                    err.error = "decode threw for: " + (*files)[i];
                    (*results)[i] = std::move(err);
                }
                finishIfLast(i);
            },
            {},                                           // deps
            std::chrono::steady_clock::time_point::max(), // deadline
            [] {}); // done callback (required for drain tracking)
        if (!handle)
        {
            // M26: a rejected submission must NOT silently disappear — the
            // aggregate callback still fires exactly once, and this item
            // carries an explicit failure.
            Result err;
            err.error = "scheduler rejected submission for: " + (*files)[i];
            (*results)[i] = std::move(err);
            finishIfLast(i);
        }
    }
}

void ImageRepository::prefetchVisible(const std::vector<std::string> &visiblePaths,
                                      const std::vector<std::string> &adjacentPaths)
{
    // M27: prefetch is documented as best-effort — a scheduler rejection
    // (paused/saturated pool) is an explicit no-op for that path; the caller
    // is not waiting on a callback, so there is no lost-request hazard.
    for (const auto &p : visiblePaths)
    {
        TaskScheduler::instance().submit(TaskScheduler::Priority::UI,
                                         [this, p](const TaskScheduler::TaskContext &)
                                         { load(p); });
    }

    for (const auto &p : adjacentPaths)
    {
        TaskScheduler::instance().submit(TaskScheduler::Priority::Background,
                                         [this, p](const TaskScheduler::TaskContext &)
                                         { load(p); });
    }
}

void ImageRepository::prefetch(const std::vector<std::string> &keys, CacheLevel level)
{
    if (level == CacheLevel::Disk)
        return;
    for (const auto &key : keys)
    {
        ImageData img;
        if (CacheManager::instance().getDisk(key, img))
            CacheManager::instance().putMemory(level, key, img);
    }
}

void ImageRepository::release(const std::string &filePath)
{
    CacheManager::instance().invalidate(makeKey(filePath));
}

mviewer::domain::ImageMetadata ImageRepository::metadata(const std::string &filePath) const
{
    const std::string key = makeKey(filePath);
    mviewer::domain::ImageMetadata meta;
    if (CacheManager::instance().getMetadata(key, meta))
        return meta;
    meta = makeMeta(filePath);
    if (!meta.filePath.empty())
        CacheManager::instance().putMetadata(key, meta);
    return meta;
}

void ImageRepository::cacheToDisk(const std::string &filePath)
{
    const std::string key = makeKey(filePath);
    ImageData img = Decoder::decodeFull(filePath);
    if (!img.isNull())
        DiskCache::instance().put(key, img);
}

void ImageRepository::invalidate(const std::string &filePath)
{
    CacheManager::instance().invalidate(makeKey(filePath));
    forgetKey(filePath);
}

void ImageRepository::invalidateAll()
{
    DiskCache::instance().clear();
    CacheManager::instance().clearMemory();
    std::lock_guard<std::mutex> lock(m_keyMtx);
    m_keyByPath.clear();
}

mviewer::domain::Workspace ImageRepository::loadWorkspace(const std::string &rootPath,
                                                          int maxPerFolder, bool recursive) const
{
    mviewer::domain::Workspace ws;
    ws.rootPath = rootPath;

    std::error_code ec;
    const std::filesystem::path root(rootPath);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
        return ws;

    // Collect (directory -> image paths) by walking the tree.
    std::map<std::string, std::vector<std::string>> byDir;

    auto visitDir = [&](const std::filesystem::path &dir)
    {
        std::vector<std::string> files = FileSystem::listImages(dir.string(), maxPerFolder);
        if (!files.empty())
            byDir[dir.string()] = std::move(files);
    };

    if (recursive)
    {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec))
        {
            if (entry.is_directory(ec))
                visitDir(entry.path());
        }
        // recursive_directory_iterator does not yield the root itself if it has
        // no subdirectories; ensure the root is scanned too.
        visitDir(root);
    }
    else
    {
        visitDir(root);
    }

    for (const auto &[dir, files] : byDir)
    {
        mviewer::domain::Folder folder;
        folder.path = dir;
        folder.name = std::filesystem::path(dir).filename().string();
        mviewer::domain::ImageSet set;
        set.folderPath = dir;
        set.images.reserve(files.size());
        for (const auto &f : files)
            set.images.push_back(makeMeta(f));
        folder.imageSet = std::move(set);
        ws.folders.push_back(std::move(folder));
    }
    return ws;
}
