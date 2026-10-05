#include "core/image/ImageRepository.h"

#include "core/cache/CacheManager.h"
#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/image/ExifOrientation.h"
#include "core/image/QtMetadataSemantics.h"

#include <QFileInfo>
#include <QImageReader>

#include <cstring>

namespace
{
std::shared_ptr<std::vector<uint16_t>> packGrayscale16(const QImage &image)
{
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0)
        return nullptr;
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    auto samples = std::make_shared<std::vector<uint16_t>>(total);
    uint16_t *dst = samples->data();
    for (int y = 0; y < height; ++y)
    {
        const auto *row = reinterpret_cast<const uint16_t *>(image.constScanLine(y));
        std::memcpy(dst + static_cast<size_t>(y) * static_cast<size_t>(width), row,
                    static_cast<size_t>(width) * sizeof(uint16_t));
    }
    return samples;
}

std::shared_ptr<std::vector<uint16_t>> packRgb64(const QImage &image)
{
    const QImage source = image.format() == QImage::Format_RGBX64
                              ? image
                              : image.convertToFormat(QImage::Format_RGBX64);
    if (source.isNull() || source.width() <= 0 || source.height() <= 0)
        return nullptr;
    const int width = source.width();
    const int height = source.height();
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    auto samples = std::make_shared<std::vector<uint16_t>>(total);
    uint16_t *dst = samples->data();
    for (int y = 0; y < height; ++y)
    {
        const auto *row = reinterpret_cast<const uint16_t *>(source.constScanLine(y));
        size_t rowDst = static_cast<size_t>(y) * static_cast<size_t>(width) * 3;
        for (int x = 0; x < width; ++x)
        {
            const int pixel = x * 4;
            dst[rowDst] = row[pixel];
            dst[rowDst + 1] = row[pixel + 1];
            dst[rowDst + 2] = row[pixel + 2];
            rowDst += 3;
        }
    }
    return samples;
}

std::shared_ptr<std::vector<uint16_t>> orientPacked(std::shared_ptr<std::vector<uint16_t>> samples,
                                                    int width, int height, int channels,
                                                    int orientation)
{
    if (!samples || samples->empty() || orientation <= 1 || orientation > 8)
        return samples;
    auto oriented =
        mviewer::core::orientSampleGrid(samples->data(), width, height, channels, orientation);
    if (oriented.empty())
        return samples;
    return std::make_shared<std::vector<uint16_t>>(std::move(oriented));
}

std::shared_ptr<std::vector<uint16_t>> captureRaw16(const std::string &path)
{
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(false);
    const QImage::Format fmt = reader.imageFormat();
    const bool is16 = (fmt == QImage::Format_RGBX64 || fmt == QImage::Format_RGBA64 ||
                       fmt == QImage::Format_Grayscale16);
    if (!is16)
        return nullptr;
    const int orientation =
        mviewer::core::qtmetadata::orientationFromTransform(reader.transformation());
    QImage image = reader.read();
    if (image.isNull())
        return nullptr;
    if (image.format() == QImage::Format_Grayscale16)
        return orientPacked(packGrayscale16(image), image.width(), image.height(), 1, orientation);
    return orientPacked(packRgb64(image), image.width(), image.height(), 3, orientation);
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
        const QString ext =
            QFileInfo(QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size())))
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
