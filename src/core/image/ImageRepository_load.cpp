#include "core/image/ImageRepository.h"

#include "core/cache/CacheManager.h"
#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"

#include <QFileInfo>
#include <QImageReader>

namespace
{
std::shared_ptr<std::vector<uint16_t>> captureRaw16(const std::string &path)
{
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    const QImage::Format fmt = reader.imageFormat();
    const bool is16 = (fmt == QImage::Format_RGBX64 || fmt == QImage::Format_RGBA64 ||
                       fmt == QImage::Format_Grayscale16);
    if (!is16)
        return nullptr;
    QImage image = reader.read();
    if (image.isNull())
        return nullptr;
    if (image.format() == QImage::Format_Grayscale16)
    {
        const int width = image.width();
        const int height = image.height();
        auto samples = std::make_shared<std::vector<uint16_t>>();
        samples->reserve(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
        {
            const auto *row = reinterpret_cast<const uint16_t *>(image.constScanLine(y));
            for (int x = 0; x < width; ++x)
                samples->push_back(row[x]);
        }
        return samples;
    }
    const QImage source = image.convertToFormat(QImage::Format_RGBX64);
    if (source.isNull())
        return nullptr;
    const int width = source.width();
    const int height = source.height();
    auto samples = std::make_shared<std::vector<uint16_t>>();
    samples->reserve(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    for (int y = 0; y < height; ++y)
    {
        const auto *row = reinterpret_cast<const uint16_t *>(source.constScanLine(y));
        for (int x = 0; x < width; ++x)
        {
            const int pixel = x * 4;
            samples->push_back(row[pixel]);
            samples->push_back(row[pixel + 1]);
            samples->push_back(row[pixel + 2]);
        }
    }
    return samples;
}

void restoreCachedRaw16(ImageFrame &frame, const std::string &key)
{
    std::shared_ptr<std::vector<uint16_t>> samples;
    int channels = 0;
    uint16_t maxSample = 0;
    if (CacheManager::instance().getRaw16(key, samples, channels, maxSample) && samples)
        frame.setRaw16(std::move(samples), maxSample, channels);
}
} // namespace

bool ImageRepository::loadMemoryHit(const std::string &filePath, const std::string &key,
                                    const LoadOptions &opts, Result &result) const
{
    ImageData pixels;
    if (!CacheManager::instance().getMemory(CacheLevel::FullImage, key, pixels))
        return false;

    mviewer::domain::ImageMetadata metadata;
    if (!CacheManager::instance().getMetadata(key, metadata))
    {
        result.error = "memory cache entry has no metadata: " + filePath;
        return true;
    }
    auto frame = std::make_shared<ImageFrame>(metadata, pixels);
    restoreCachedRaw16(*frame, key);
    if (opts.generateHistogram)
        frame->computeHistogram();
    frame->setDecodeState(DecodeState::Decoded);
    frame->setCacheState(CacheState::Memory);
    CacheManager::instance().putMemory(CacheLevel::FullImage, key, pixels);
    result.frame = std::move(frame);
    result.fromCache = true;
    return true;
}

bool ImageRepository::loadPixels(const std::string &filePath, const std::string &key,
                                 const LoadOptions &opts, ImageData &pixels, bool &fromCache,
                                 mviewer::domain::ImageMetadata &decodeMeta,
                                 std::string &error) const
{
    if (opts.useDiskCache && DiskCache::instance().get(key, pixels))
    {
        fromCache = true;
        decodeMeta = makeMeta(filePath);
        return true;
    }

    pixels = Decoder::decodeFull(filePath, decodeMeta);
    if (pixels.isNull())
    {
        error = "decode failed: " + filePath;
        return false;
    }
    if (opts.useDiskCache)
        DiskCache::instance().put(key, pixels);
    return true;
}

void ImageRepository::enrichFrame(ImageFrame &frame, const std::string &filePath,
                                  const ImageData &pixels,
                                  const mviewer::domain::ImageMetadata &decodeMeta) const
{
    mviewer::domain::ImageMetadata metadata = frame.metadata();
    if (metadata.format.empty())
    {
        const QString ext = QFileInfo(
                                QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size())))
                                .suffix()
                                .toLower();
        if (ext == "jpg" || ext == "jpeg")
            metadata.format = "JPEG";
        else if (ext == "png")
            metadata.format = "PNG";
        else if (ext == "bmp")
            metadata.format = "BMP";
        else if (ext == "tif" || ext == "tiff")
            metadata.format = "TIFF";
        else if (!ext.isEmpty())
            metadata.format = ext.toUpper().toStdString();
    }
    metadata.channels = pixels.channelsPerPixel();
    metadata.bitDepth = 8;
    if (!decodeMeta.format.empty())
        metadata.format = decodeMeta.format;
    if (decodeMeta.channels > 0)
        metadata.channels = decodeMeta.channels;
    if (decodeMeta.bitDepth > 0)
        metadata.bitDepth = decodeMeta.bitDepth;
    if (!decodeMeta.colorSpace.empty())
        metadata.colorSpace = decodeMeta.colorSpace;
    if (decodeMeta.orientation >= 1 && decodeMeta.orientation <= 8)
        metadata.orientation = decodeMeta.orientation;
    metadata.hasIccProfile = decodeMeta.hasIccProfile;
    const auto displayIcc = decodeMeta.textKeys.find("MViewer.DisplayICC.Base64");
    if (displayIcc != decodeMeta.textKeys.end())
        metadata.textKeys[displayIcc->first] = displayIcc->second;
    if (!decodeMeta.iccDescription.empty())
        metadata.iccDescription = decodeMeta.iccDescription;
    if (!decodeMeta.iccCopyright.empty())
        metadata.iccCopyright = decodeMeta.iccCopyright;
    if (!decodeMeta.iccColorSpace.empty())
        metadata.iccColorSpace = decodeMeta.iccColorSpace;
    if (!decodeMeta.iccDeviceClass.empty())
        metadata.iccDeviceClass = decodeMeta.iccDeviceClass;
    if (!decodeMeta.iccPcs.empty())
        metadata.iccPcs = decodeMeta.iccPcs;
    if (!decodeMeta.iccRenderingIntent.empty())
        metadata.iccRenderingIntent = decodeMeta.iccRenderingIntent;
    if (!decodeMeta.iccVersion.empty())
        metadata.iccVersion = decodeMeta.iccVersion;
    frame.setMetadata(metadata);
}

void ImageRepository::restoreRaw16(ImageFrame &frame, const std::string &filePath,
                                   const std::string &key) const
{
    restoreCachedRaw16(frame, key);
    if (filePath.empty() || frame.metadata().format == "RAW" || frame.hasRaw16())
        return;
    auto samples = captureRaw16(filePath);
    if (!samples)
        return;
    const int channels = (frame.metadata().channels == 1) ? 1 : 3;
    frame.setRaw16(samples, 65535, channels);
    CacheManager::instance().putRaw16(key, std::move(samples), channels, 65535);
}
