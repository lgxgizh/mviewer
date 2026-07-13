#include "core/image/ImageRepository.h"

#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/cache/CacheManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/scheduler/TaskScheduler.h"

#include <QFileInfo>
#include <QImageReader>
#include <QSize>

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
        return meta; // 空（默认）metadata 表示文件不存在
    meta.filePath = filePath;
    meta.fileName = fi.fileName().toStdString();
    meta.fileSize = fi.size();
    meta.modifiedEpochSec = fi.lastModified().toSecsSinceEpoch();
    // 仅读取头信息获取尺寸，不做完整解码。
    QImageReader reader(QString::fromStdString(filePath));
    const QSize s = reader.size();
    meta.width = s.width();
    meta.height = s.height();
    meta.hash = filePath + "|" + std::to_string(meta.fileSize) + "|" +
                std::to_string(meta.modifiedEpochSec);
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

    // 元数据也写入 Metadata 缓存层，供 metadata()/prefetch 复用。
    CacheManager::instance().putMetadata(key, frame->metadata());

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

void ImageRepository::prefetch(const std::string& filePath, const LoadOptions& opts)
{
    // 后台预热：解码并写入缓存（磁盘 + 内存），不阻塞调用线程。
    TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [this, filePath, opts](const TaskScheduler::TaskContext&) {
            (void)load(filePath, opts);
        });
}

void ImageRepository::release(const std::string& filePath)
{
    // 生命周期结束：丢弃该路径在所有缓存层中的全部表示。
    CacheManager::instance().invalidate(makeKey(filePath));
}

mviewer::domain::ImageMetadata ImageRepository::metadata(const std::string& filePath) const
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

void ImageRepository::cacheToDisk(const std::string& filePath)
{
    const std::string key = makeKey(filePath);
    ImageData img = Decoder::decodeFull(filePath);
    if (!img.isNull())
        DiskCache::instance().put(key, img);
}

void ImageRepository::invalidate(const std::string& filePath)
{
    // 移除该 path 在内存像素池 + 元数据对象 + 磁盘中的全部缓存。
    CacheManager::instance().invalidate(makeKey(filePath));
}

void ImageRepository::invalidateAll()
{
    DiskCache::instance().clear();
    CacheManager::instance().clearMemory();
}
