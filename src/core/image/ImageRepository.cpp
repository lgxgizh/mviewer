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

ImageRepository &ImageRepository::instance()
{
    static ImageRepository inst;
    return inst;
}

std::string ImageRepository::makeKey(const std::string &filePath) const
{
    return mviewer::core::MetadataReader::key(filePath);
}

mviewer::domain::ImageMetadata ImageRepository::makeMeta(const std::string &filePath) const
{
    return mviewer::core::MetadataReader::read(filePath);
}

ImageRepository::Result ImageRepository::load(const std::string &filePath, const LoadOptions &opts)
{
    MV_TRACE_SCOPED("ImageRepository::load");
    Result res;
    const std::string key = makeKey(filePath);

    ImageData img;
    bool fromCache = false;
    mviewer::domain::ImageMetadata decodeMeta;

    // P0/B8: in-memory LRU fast-path. A preloaded image switch (the common case
    // for navigating an already-open folder) must be a PURE in-memory hit: no
    // disk I/O, no ImageFrame reallocation, no metadata re-parse. This avoids the
    // p95/p99 tail spikes caused by DiskCache::get / allocator contention.
    if (CacheManager::instance().getMemory(CacheLevel::FullImage, key, img))
    {
        auto frame = std::make_shared<ImageFrame>(ImageFrame::create(filePath, img));
        mviewer::domain::ImageMetadata m;
        if (CacheManager::instance().getMetadata(key, m))
            frame->setMetadata(m);
        frame->setDecodeState(DecodeState::Decoded);
        frame->setCacheState(CacheState::Memory);
        CacheManager::instance().putMemory(CacheLevel::FullImage, key, img);
        res.frame = frame;
        res.fromCache = true;
        return res;
    }

    if (opts.useDiskCache && DiskCache::instance().get(key, img))
    {
        fromCache = true;
    }
    else
    {
        img = Decoder::decodeFull(filePath, decodeMeta);
        if (img.isNull())
        {
            res.error = "decode failed: " + filePath;
            return res;
        }
        if (opts.useDiskCache)
            DiskCache::instance().put(key, img);
    }

    auto frame = std::make_shared<ImageFrame>(ImageFrame::create(filePath, img));

    // Enrich the frame metadata with decode-time fields (bitDepth, channels,
    // colorSpace, orientation, format). The file-level fields were already set
    // by ImageFrame::create. These are populated from the decoded pixels (always
    // available, whether served from cache or freshly decoded) so the metadata
    // is correct even on a disk-cache hit. Values the decoder can only determine
    // on a fresh decode (orientation, ICC) are taken from decodeMeta when present.
    {
        mviewer::domain::ImageMetadata m = frame->metadata();
        // format: derive from the file extension (covers all cached/uncached paths).
        if (m.format.empty())
        {
            const QString ext = QFileInfo(QString::fromStdString(filePath)).suffix().toLower();
            if (ext == "jpg" || ext == "jpeg")
                m.format = "JPEG";
            else if (ext == "png")
                m.format = "PNG";
            else if (ext == "bmp")
                m.format = "BMP";
            else if (ext == "tif" || ext == "tiff")
                m.format = "TIFF";
            else if (!ext.isEmpty())
                m.format = ext.toUpper().toStdString();
        }
        // channels / bitDepth from the actual RGB24 pixel buffer we hold.
        m.channels = img.channelsPerPixel();
        m.bitDepth = 8;
        if (!decodeMeta.format.empty())
            m.format = decodeMeta.format;
        if (decodeMeta.channels > 0)
            m.channels = decodeMeta.channels;
        if (decodeMeta.bitDepth > 0)
            m.bitDepth = decodeMeta.bitDepth;
        if (!decodeMeta.colorSpace.empty())
            m.colorSpace = decodeMeta.colorSpace;
        if (decodeMeta.orientation >= 1 && decodeMeta.orientation <= 8)
            m.orientation = decodeMeta.orientation;
        m.hasIccProfile = decodeMeta.hasIccProfile;
        frame->setMetadata(m);
    }
    if (opts.generateHistogram)
        frame->computeHistogram();
    frame->setDecodeState(DecodeState::Decoded);
    frame->setCacheState(fromCache ? CacheState::Disk : CacheState::None);

    // P0: keep the decoded pixels in the in-memory Viewer/FullImage LRU so that
    // switching to an adjacent image is instant after the first decode. Without
    // this, every navigation re-decodes from disk/DiskCache.
    if (!img.isNull())
        CacheManager::instance().put(CacheLevel::FullImage, key, img);

    CacheManager::instance().putMetadata(key, frame->metadata());

    res.frame = frame;
    res.fromCache = fromCache;
    return res;
}

void ImageRepository::loadAsync(const std::string &filePath,
                                std::function<void(const Result &)> callback,
                                const LoadOptions &opts)
{
    auto out = std::make_shared<Result>();
    TaskScheduler::instance().submit(
        TaskScheduler::DecodePool, [this, filePath, opts, out]() { *out = load(filePath, opts); },
        [callback, out]()
        {
            if (callback)
                callback(*out);
        });
}

std::vector<ImageRepository::Result> ImageRepository::loadDirectory(const std::string &dirPath,
                                                                    int maxImages)
{
    MV_TRACE_SCOPED("ImageRepository::loadDirectory");
    const std::vector<std::string> files = FileSystem::listImages(dirPath, maxImages);
    if (files.empty())
        return {};

    const int n = static_cast<int>(files.size());
    std::vector<Result> results(n);
    std::atomic<int> completed{0};

    // Bulk directory load must submit ALL n decode tasks and wait for every
    // one to finish. The TaskScheduler enforces max_queue_depth (default 1000);
    // if the pool is already busy (e.g. tasks from earlier work in flight),
    // submits that exceed the cap are SILENTLY DROPPED (submit returns nullptr
    // and the task never runs). A dropped task would never increment
    // `completed`, so the busy-wait below would spin forever. Relax the cap to
    // unlimited for this bulk load so no task is lost.
    TaskScheduler::instance().setMaxQueueDepth(TaskScheduler::DecodePool, 0);

    for (int i = 0; i < n; ++i)
    {
        TaskScheduler::instance().submit(
            TaskScheduler::Priority::Decode,
            [this, &files, &results, &completed, n, i](const TaskScheduler::TaskContext &)
            {
                results[i] = load(files[i]);
                completed.fetch_add(1, std::memory_order_release);
            });
    }

    while (completed.load(std::memory_order_acquire) < n)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return results;
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

    for (int i = 0; i < n; ++i)
    {
        TaskScheduler::instance().submit(
            TaskScheduler::Priority::Decode,
            [this, files, results, completed, n, i, callbackPtr](const TaskScheduler::TaskContext &)
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
                int prev = completed->fetch_add(1, std::memory_order_acq_rel);
                if (prev + 1 == n)
                {
                    auto cb = *callbackPtr;
                    auto resultCopy = *results;
                    cb(resultCopy);
                }
            },
            {},                                           // deps
            std::chrono::steady_clock::time_point::max(), // deadline
            [] {}); // done callback (required for drain tracking)
    }
}

void ImageRepository::prefetchVisible(const std::vector<std::string> &visiblePaths,
                                      const std::vector<std::string> &adjacentPaths)
{
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
}

void ImageRepository::invalidateAll()
{
    DiskCache::instance().clear();
    CacheManager::instance().clearMemory();
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
