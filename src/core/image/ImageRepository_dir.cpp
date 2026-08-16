// M46: directory/workspace/prefetch/cache-management members of
// ImageRepository, split out of the (previously monolithic) ImageRepository.cpp
// so each TU stays a bounded, testable unit under the complexity gate. The
// text is byte-identical to the pre-split definitions.
#include "core/image/ImageRepository.h"

#include "core/cache/CacheManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/image/ImageFrame.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/trace/Trace.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
