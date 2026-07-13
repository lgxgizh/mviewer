#include "core/image/ImageRepository.h"

#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/filesystem/FileSystem.h"
#include "core/scheduler/TaskScheduler.h"

#include <QFileInfo>

const ImageRepository::LoadOptions ImageRepository::kDefaultLoadOptions{};

std::string ImageRepository::makeKey(const std::string& filePath) const
{
    // Identity key = path + size + mtime, so edits invalidate the cache entry.
    const QFileInfo fi(QString::fromStdString(filePath));
    const QString key = QString::fromStdString(filePath) +
                        QString::number(fi.size()) +
                        QString::number(fi.lastModified().toSecsSinceEpoch());
    return key.toStdString();
}

ImageRepository::Result ImageRepository::load(const std::string& filePath,
                                              const LoadOptions& opts)
{
    Result res;
    const std::string key = makeKey(filePath);

    ImageData img;
    bool fromCache = false;
    if (opts.useDiskCache && DiskCache::instance().get(key, img)) {
        fromCache = true;
    } else {
        img = Decoder::decodeFull(filePath);
        if (img.isNull()) {
            res.error = "decode failed: " + filePath;
            return res;
        }
        if (opts.useDiskCache)
            DiskCache::instance().put(key, img);
    }

    auto frame = std::make_shared<ImageFrame>(ImageFrame::create(filePath, img));
    if (opts.generateHistogram)
        frame->computeHistogram();
    frame->setDecodeState(DecodeState::Decoded);
    frame->setCacheState(fromCache ? CacheState::Disk : CacheState::None);

    res.frame = frame;
    res.fromCache = fromCache;
    return res;
}

void ImageRepository::loadAsync(const std::string& filePath,
                                std::function<void(const Result&)> callback,
                                const LoadOptions& opts)
{
    auto out = std::make_shared<Result>();
    TaskScheduler::instance().submit(
        TaskScheduler::DecodePool,
        [this, filePath, opts, out]() {
            *out = load(filePath, opts);
        },
        [callback, out]() {
            if (callback) callback(*out);
        });
}

std::vector<ImageRepository::Result> ImageRepository::loadDirectory(
    const std::string& dirPath, int maxImages)
{
    std::vector<Result> results;
    const std::vector<std::string> files =
        FileSystem::listImages(dirPath, maxImages);
    results.reserve(files.size());
    for (const std::string& f : files)
        results.push_back(load(f));
    return results;
}

void ImageRepository::cacheToDisk(const std::string& filePath)
{
    const std::string key = makeKey(filePath);
    ImageData img = Decoder::decodeFull(filePath);
    if (!img.isNull())
        DiskCache::instance().put(key, img);
}

void ImageRepository::invalidate(const std::string& filePath)
{
    // DiskCache currently only supports bulk clear / prune; a single-key
    // removal would require extending DiskCache. No-op for now.
    (void)filePath;
}

void ImageRepository::invalidateAll()
{
    DiskCache::instance().clear();
}
