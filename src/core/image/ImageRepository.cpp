#include "core/image/ImageRepository.h"

#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/image/ImageFrame.h"
#include "core/cache/CacheManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/scheduler/TaskScheduler.h"

#include <QFileInfo>

const ImageRepository::LoadOptions ImageRepository::kDefaultLoadOptions{};

ImageRepository& ImageRepository::instance()
{
    static ImageRepository inst;
    return inst;
}

std::string ImageRepository::makeKey(const std::string& filePath) const
{
    // Identity key = path + size + mtime, so edits invalidate the cache entry.
    const QFileInfo fi(QString::fromStdString(filePath));
    const QString key = QString::fromStdString(filePath) +
                        QString::number(fi.size()) +
                        QString::number(fi.lastModified().toSecsSinceEpoch());
    return key.toStdString();
}

mviewer::domain::ImageMetadata ImageRepository::makeMeta(const std::string& filePath) const
{
    mviewer::domain::ImageMetadata meta;
    const QFileInfo fi(QString::fromStdString(filePath));
    if (!fi.exists())
        return meta; // 空（默认）表示文件不存在
    meta.filePath = filePath;
    meta.fileName = fi.fileName().toStdString();
    meta.fileSize = static_cast<int64_t>(fi.size());
    meta.modifiedEpochSec = fi.lastModified().toSecsSinceEpoch();
    const std::string composite = filePath + "|" +
        std::to_string(meta.fileSize) + "|" +
        std::to_string(meta.modifiedEpochSec);
    meta.hash = std::to_string(std::hash<std::string>{}(composite));
    return meta;
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
        TaskScheduler::Priority::Decode,
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

void ImageRepository::prefetch(const std::string& filePath, const LoadOptions& opts)
{
    // 后台预热：解码并写入磁盘/内存缓存，不阻塞调用方，也不回调。
    TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [this, filePath, opts]() {
            const std::string key = makeKey(filePath);
            ImageData img;
            if (DiskCache::instance().get(key, img))
                return; // 已在磁盘缓存
            img = Decoder::decodeFull(filePath);
            if (img.isNull())
                return;
            if (opts.useDiskCache)
                DiskCache::instance().put(key, img);
            CacheManager::instance().put(CacheLevel::FullImage, key, img);
        });
}

void ImageRepository::release(const std::string& filePath)
{
    // 仓库放弃该图像的生命周期：从所有缓存层丢弃其条目。
    CacheManager::instance().erase(makeKey(filePath));
}

mviewer::domain::ImageMetadata ImageRepository::metadata(const std::string& filePath) const
{
    return makeMeta(filePath);
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
    // 真实失效：从磁盘与所有内存层删除该路径的缓存条目。
    CacheManager::instance().erase(makeKey(filePath));
}

void ImageRepository::invalidateAll()
{
    DiskCache::instance().clear();
}
