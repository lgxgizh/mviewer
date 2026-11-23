#include "core/image/decoder/QtDecoder.h"
#include "core/filesystem/Utf8Path.h"

#include "core/image/DitherQuantize.h"
#include "core/image/ExifOrientation.h"
#include "core/image/ImageBuffer.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/QtMetadataSemantics.h"
#include "core/image/decoder/JpegTurbo.h"
#include "core/image/decoder/QtDecoder_internal.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <QTransform>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

// Same transform QImageReader::read applies when autoTransform is set (Qt 6.8).
Q_GUI_EXPORT void qt_imageTransform(QImage &src, QImageIOHandler::Transformations orient);

namespace mviewer::core::detail
{
// Implemented in QtDecoder_wic.cpp: the bounded WIC TIFF raster read (null
// image off Windows or on any failure). fullSize receives the source size.
QImage decodeTiffWicRaster(const std::string &path, const QRect &rawRect, const QSize &target,
                           QSize &fullSize);
// Implemented in QtDecoder_bmp.cpp: bounded box-average BMP LOD (perf review
// P2-4). Eligible: uncompressed 8/24/32 bpp. Null image means use Qt.
bool bmpBoxLodEligible(const std::string &path, QSize *size);
QImage decodeBmpBoxLod(const std::string &path, const QSize &target);
// Implemented in QtDecoder_png.cpp: bounded box-average PNG LOD (perf review
// P2-4). Null image means use the QImageReader path.
bool pngBoxLodEligible(const std::string &path);
QImage decodePngBoxLod(const std::string &path, const QSize &target);
// QtDecoder_tiff.cpp: bounded uncompressed strip TIFF LOD where WIC is unavailable.
bool tiffStripLodEligible(const std::string &path);
QImage decodeTiffStripLod(const std::string &path, const QSize &target);
} // namespace mviewer::core::detail

namespace
{

void copyRasterRows(const QImage &img, ImageData &out, size_t rowBytes)
{
    if (!out.buffer || out.buffer->empty())
        return;
    const int h = img.height();
    const qsizetype bytesPerLine = img.bytesPerLine();
    if (bytesPerLine == static_cast<qsizetype>(rowBytes) && out.stride() == rowBytes)
    {
        std::memcpy(out.buffer->data(), img.constBits(), rowBytes * static_cast<size_t>(h));
        return;
    }
    for (int y = 0; y < h; ++y)
    {
        const uchar *s = img.constScanLine(y);
        uint8_t *d = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        std::memcpy(d, s, rowBytes);
    }
}

ImageData toImageData(const QImage &src)
{
    if (src.isNull())
        return ImageData();
    // 8-bit gray stays one sample per pixel.
    if (src.format() == QImage::Format_Grayscale8)
    {
        ImageData out = makeImageData(src.width(), src.height(), PixelFormat::Grayscale8);
        copyRasterRows(src, out, static_cast<size_t>(src.width()));
        return out;
    }
    // 16-bit display rasters are ordered-dithered to RGB888. A failed dither
    // falls back to Qt's converter so a valid source still decodes. Other
    // 8-bit sources keep the historical convertToFormat path (byte-identical).
    QImage dithered;
    const QImage::Format fmt = src.format();
    const bool sixteen = fmt == QImage::Format_RGBX64 || fmt == QImage::Format_RGBA64 ||
                         fmt == QImage::Format_Grayscale16;
    if (sixteen)
        mviewer::core::ditherQImage16ToRgb888(src, dithered);
    const bool useDither = sixteen && !dithered.isNull();
    const QImage img =
        useDither
            ? dithered
            : (fmt == QImage::Format_RGB888 ? src : src.convertToFormat(QImage::Format_RGB888));
    if (img.isNull())
        return ImageData();
    ImageData out = makeImageData(img.width(), img.height(), PixelFormat::RGB24);
    copyRasterRows(img, out, static_cast<size_t>(img.width()) * 3U);
    return out;
}

// Map the QImageIOHandler transformation bitmask to the EXIF orientation 1-8
// MirrorAndRotate90=5 (EXIF 7), FlipAndRotate90=6 (EXIF 5), Rotate270=7.
int orientationFromTransform(QImageIOHandler::Transformations t)
{
    return mviewer::core::qtmetadata::orientationFromTransform(t);
}

bool transformSwapsDimensions(QImageIOHandler::Transformations t)
{
    return mviewer::core::qtmetadata::transformSwapsDimensions(t);
}

bool isTiffPath(const std::string &path)
{
    const QString ext =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).suffix().toLower();
    return ext == "tif" || ext == "tiff";
}

bool isBmpPath(const std::string &path)
{
    const QString ext =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).suffix().toLower();
    return ext == "bmp" || ext == "dib";
}

bool isPngPath(const std::string &path)
{
    const QString ext =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).suffix().toLower();
    return ext == "png";
}

bool isJpegPath(const std::string &path)
{
    const QString ext =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).suffix().toLower();
    return ext == "jpg" || ext == "jpeg" || ext == "jpe";
}

// Qt 6.8 qt_imageTransform: None is identity; Rotate270 is one 270-degree
// clockwise rotation; the others mirror/flip first and then rotate 90 degrees
// clockwise when Rotate90 is set.
static QImage applyQtTransformation(const QImage &img, QImageIOHandler::Transformations t)
{
    if (t == QImageIOHandler::TransformationNone)
        return img;
    if (t == QImageIOHandler::TransformationRotate270)
        return img.transformed(QTransform().rotate(270), Qt::FastTransformation);
    QImage out = img.mirrored(static_cast<bool>(t & QImageIOHandler::TransformationMirror),
                              static_cast<bool>(t & QImageIOHandler::TransformationFlip));
    if (t & QImageIOHandler::TransformationRotate90)
        out = out.transformed(QTransform().rotate(90), Qt::FastTransformation);
    return out;
}

// Shared tail of the JPEG fast path (review P1-05): libjpeg-turbo decode, then
// the same metadata the Qt path reports. `maxEdge <= 0` means full size.
// Returns null when the fast path does not apply, so the caller falls back.
ImageData jpegFastTail(const std::string &path, int maxEdge, QImageReader &reader,
                       const QSize &full, QImage::Format sourceFormat,
                       mviewer::domain::ImageMetadata &outMeta)
{
    // The helper decides its own scale factor from the STORED raster it reads
    // out of the header: `full` is the DISPLAYED geometry (autoTransform may
    // have swapped the axes), so it must not drive the scaling choice.
    // An EXIF-rotated JPEG keeps Qt's decode. libjpeg-turbo scales the STORED
    // raster while `full` is the DISPLAYED geometry, so a rotated source would
    // get its scale factor from the wrong axes - and the transform itself needs
    // Qt's image operators anyway. Falls back on the m27 preview fixtures,
    // which pin the exact preview edge.
    const auto transform = reader.transformation();
    if (transform != QImageIOHandler::TransformationNone)
        return {};
    ImageData fast = mviewer::core::detail::decodeJpegTurbo(path, maxEdge);
    if (fast.isNull())
        return {};
    const QImage probe(
        1, 1, sourceFormat == QImage::Format_Invalid ? QImage::Format_RGB888 : sourceFormat);
    mviewer::core::detail::decoderFillMetadata(reader, sourceFormat, probe, outMeta);
    outMeta.width = full.width();
    outMeta.height = full.height();
    if (transformSwapsDimensions(transform))
        std::swap(outMeta.width, outMeta.height);
    outMeta.orientation = orientationFromTransform(transform);
    outMeta.format = "JPEG";
    return fast;
}

#if defined(Q_OS_WIN)
// Recover source bit depth and ICC from a bounded Qt probe so the native TIFF
// LOD never has to materialize the whole raster.
void probeTiffMetadata(const QString &filename, const QImage::Format metadataFormat,
                       mviewer::domain::ImageMetadata &meta)
{
    if (metadataFormat != QImage::Format_Invalid)
    {
        const QImage probeFormat(1, 1, metadataFormat);
        meta.channels = mviewer::core::qtmetadata::channelsForFormat(metadataFormat, probeFormat);
        meta.bitDepth = mviewer::core::qtmetadata::bitsPerChannel(metadataFormat, meta.channels);
    }
    const qint64 sourceSize = QFileInfo(filename).size();
    if (sourceSize < 0 || sourceSize > 8LL * 1024 * 1024)
        return;
    QImageReader profileReader(filename);
    profileReader.setAutoTransform(true);
    profileReader.setScaledSize(QSize(1, 1));
    QImage tiny;
    if (profileReader.read(&tiny))
        mviewer::core::qtmetadata::applyColorMetadata(tiny, meta);
}

void populateWicTiffMetadata(const QString &filename, const std::string &path, const QImage &output,
                             const QSize &fullSize, mviewer::domain::ImageMetadata &meta)
{
    QImageReader metadataReader(filename);
    metadataReader.setAutoTransform(true);
    const QImage::Format sourceFormat =
        mviewer::core::qtmetadata::readerImageFormat(metadataReader);
    mviewer::core::detail::decoderFillMetadata(metadataReader, sourceFormat, output, meta);
    if (meta.filePath.empty())
        meta.filePath = path;
    meta.fileSize = QFileInfo(filename).size();
    meta.width = fullSize.width();
    meta.height = fullSize.height();
    meta.orientation = 1;
    meta.format = "TIFF";
    probeTiffMetadata(filename, sourceFormat, meta);
}

// Qt's TIFF plugin reports ClipRect but still attempts to allocate the full
// display raster is materialized.
QImage decodeTiffWic(const std::string &path, const QRect &rawRect, const QSize &target,
                     mviewer::domain::ImageMetadata &meta)
{
    QSize fullSize;
    QImage output = mviewer::core::detail::decodeTiffWicRaster(path, rawRect, target, fullSize);
    if (output.isNull())
        return QImage();
    const QString filename = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    populateWicTiffMetadata(filename, path, output, fullSize, meta);
    return output;
}
#endif

// Container format: prefer the reader's reported format; fall back to the file
// extension (QImageReader leaves format() empty for some formats, e.g. BMP).
// Populate the M6 metadata fields from a decoded QImage + its reader. Any
// field that cannot be determined is left at its default. Never throws.
// (Defined in QtDecoder_meta.cpp.)

// F2 (M22): claim every format Qt can actually decode, derived from
// touching the frozen DecoderRegistry. The historical 6 are guaranteed present.
const std::vector<std::string> &supportedExts()
{
    static const std::vector<std::string> exts = []()
    {
        std::vector<std::string> out;
        const auto fmts = QImageReader::supportedImageFormats();
        out.reserve(fmts.size() + 6);
        for (const QByteArray &f : fmts)
            out.push_back(QString::fromLatin1(f).toLower().toStdString());
        for (const char *b : {"jpg", "jpeg", "bmp", "png", "tif", "tiff"})
            out.push_back(b);
        return out;
    }();
    return exts;
}

struct PendingRaw16
{
    std::string path;
    std::shared_ptr<std::vector<uint16_t>> samples;
    int channels = 0;
};

std::atomic<uint64_t> g_fullImageReads{0};
thread_local PendingRaw16 g_pendingRaw16;

void clearPendingRaw16()
{
    g_pendingRaw16.path.clear();
    g_pendingRaw16.samples.reset();
    g_pendingRaw16.channels = 0;
}

bool isSixteenBitFormat(QImage::Format format)
{
    return format == QImage::Format_RGBX64 || format == QImage::Format_RGBA64 ||
           format == QImage::Format_Grayscale16;
}

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
        if (row == nullptr)
            return nullptr;
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
        if (row == nullptr)
            return nullptr;
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

void publishRaw16(const std::string &path, const QImage &image, int orientation)
{
    std::shared_ptr<std::vector<uint16_t>> packed;
    int channels = 3;
    if (image.format() == QImage::Format_Grayscale16)
    {
        packed =
            orientPacked(packGrayscale16(image), image.width(), image.height(), 1, orientation);
        channels = 1;
    }
    else if (image.format() == QImage::Format_RGBX64 || image.format() == QImage::Format_RGBA64)
    {
        packed = orientPacked(packRgb64(image), image.width(), image.height(), 3, orientation);
    }
    else
    {
        return;
    }
    if (!packed)
        return;
    g_pendingRaw16.path = path;
    g_pendingRaw16.samples = std::move(packed);
    g_pendingRaw16.channels = channels;
}

} // namespace

void QtDecoder::resetFullImageReadCount()
{
    g_fullImageReads.store(0, std::memory_order_relaxed);
}

uint64_t QtDecoder::fullImageReadCount()
{
    return g_fullImageReads.load(std::memory_order_relaxed);
}

void QtDecoder::noteFullImageRead()
{
    g_fullImageReads.fetch_add(1, std::memory_order_relaxed);
}

std::shared_ptr<std::vector<uint16_t>> QtDecoder::takePendingRaw16(const std::string &path,
                                                                   int &channels)
{
    channels = 0;
    if (g_pendingRaw16.path != path || !g_pendingRaw16.samples)
        return nullptr;
    channels = g_pendingRaw16.channels;
    auto samples = std::move(g_pendingRaw16.samples);
    g_pendingRaw16.path.clear();
    g_pendingRaw16.channels = 0;
    return samples;
}

bool QtDecoder::canDecode(const std::string &path) const
{
    QString ext =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).suffix().toLower();
    // Canonical aliases so .jpg/.jpeg and .tif/.tiff both match regardless of
    // which name QImageReader reports.
    if (ext == "jpg")
        ext = "jpeg";
    else if (ext == "tif")
        ext = "tiff";
    const std::string e = ext.toStdString();
    for (const auto &x : supportedExts())
        if (x == e)
            return true;
    return false;
}

ImageData QtDecoder::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
QtDecoder::decodeFullResult(const std::string &path, mviewer::domain::ImageMetadata &outMeta) const
{
    bool exists = true;
    try
    {
        const auto stdPath = mviewer::core::pathFromUtf8(path);
        std::error_code ec;
        if (!std::filesystem::exists(stdPath, ec) && !ec)
            exists = false;
    }
    catch (...)
    {
        exists = true; // Let QImageReader attempt to open natively
    }
    const bool isNetworkPath = (path.size() >= 2 && (path[0] == '/' || path[0] == '\\') &&
                                (path[1] == '/' || path[1] == '\\'));
    if (!exists && !isNetworkPath)
    {
        return mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>::err(
            {mviewer::domain::DecodeErrorCode::FileNotFound, "Image file not found: " + path,
             path});
    }

    clearPendingRaw16();
    const QString qPath = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    QImageReader reader(qPath);
    reader.setAutoTransform(false);
    const QImage::Format sourceFormat = mviewer::core::qtmetadata::readerImageFormat(reader);
    const bool sixteen = isSixteenBitFormat(sourceFormat);
    if (!sixteen)
        reader.setAutoTransform(true);
    noteFullImageRead();
    // Full-resolution JPEG fast path (review P1-05). Same contract as the LOD
    // path: baseline 8-bit RGB / greyscale JPEGs only; a null return means Qt.
    if (!sixteen && mviewer::core::detail::hasTurboJpeg() && isJpegPath(path))
    {
        const QSize full = mvcore::readerRasterSize(reader);
        const ImageData fast = jpegFastTail(path, 0, reader, full, sourceFormat, outMeta);
        if (!fast.isNull())
        {
            if (outMeta.filePath.empty())
                outMeta.filePath = path;
            return mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>::ok(fast);
        }
    }
    QImage img = reader.read();
    if (img.isNull())
    {
        const auto readerErr = reader.error();
        const QString errStr = reader.errorString();
        mviewer::domain::DecodeErrorCode code = mviewer::domain::DecodeErrorCode::CorruptData;
        if (readerErr == QImageReader::FileNotFoundError)
            code = mviewer::domain::DecodeErrorCode::FileNotFound;
        else if (readerErr == QImageReader::DeviceError)
            code = mviewer::domain::DecodeErrorCode::AccessDenied;
        else if (readerErr == QImageReader::UnsupportedFormatError)
            code = mviewer::domain::DecodeErrorCode::UnsupportedFormat;
        else if (readerErr == QImageReader::InvalidDataError)
            code = mviewer::domain::DecodeErrorCode::CorruptData;

        std::string msg =
            errStr.isEmpty() ? ("QtDecoder failed to decode " + path) : errStr.toStdString();
        qWarning("QtDecoder: failed to decode \"%s\": %s", path.c_str(), msg.c_str());
        return mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>::err(
            {code, std::move(msg), path});
    }

    if (outMeta.filePath.empty())
        outMeta.filePath = path;
    // One 16-bit read feeds both the display buffer and raw16. The 8-bit image
    // is the same auto-transform Qt would have applied inside read().
    if (sixteen)
    {
        const int orientation =
            mviewer::core::qtmetadata::orientationFromTransform(reader.transformation());
        publishRaw16(path, img, orientation);
        qt_imageTransform(img, reader.transformation());
    }
    mviewer::core::detail::decoderFillMetadata(reader, sourceFormat, img, outMeta);
    return mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>::ok(toImageData(img));
}

ImageData QtDecoder::decodeFull(const std::string &path,
                                mviewer::domain::ImageMetadata &outMeta) const
{
    auto res = decodeFullResult(path, outMeta);
    if (res.has_value())
        return std::move(res.value());
    return ImageData();
}

ImageData QtDecoder::decodeScaled(const std::string &path, int maxEdge) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeScaled(path, maxEdge, meta);
}

ImageData QtDecoder::decodeScaled(const std::string &path, int maxEdge,
                                  mviewer::domain::ImageMetadata &outMeta) const
{
    return decodeLod(path, maxEdge, outMeta);
}

std::vector<std::string> QtDecoder::extensions() const
{
    return supportedExts();
}

// ── M47 source-backed capabilities ───────────────────────────────────────────

bool QtDecoder::canProbe(const std::string &path) const
{
    return canDecode(path);
}
bool QtDecoder::canNativeLod(const std::string &path) const
{
    // Evidence-based (M47/M53): JPEG uses the decoder's reduced DCT read. TIFF
    // uses the measured strip/clip adapter below; it never asks Qt to allocate
    // the full raster (off Windows: uncompressed strips). BMP/PNG: bounded box LOD.
    const QString ext =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg")
        return true;
    if (isBmpPath(path))
        return mviewer::core::detail::bmpBoxLodEligible(path, nullptr);
    if (isPngPath(path))
        return QtDecoder::hasBoundedPngLod() && mviewer::core::detail::pngBoxLodEligible(path);
    if (!isTiffPath(path))
        return false;
#if !defined(Q_OS_WIN)
    return mviewer::core::detail::tiffStripLodEligible(path);
#else
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(true);
    return mvcore::readerRasterSize(reader).isValid() &&
           !transformSwapsDimensions(reader.transformation()) &&
           reader.transformation() == QImageIOHandler::TransformationNone;
#endif
}

bool QtDecoder::canNativeRegion(const std::string &path) const
{
    (void)path;
    // Qt offers no true random-access tile decode. Evidence-backed bounded
    // handlers are declared separately through sourceRegionBehavior().
    return false;
}

mviewer::core::SourceRegionBehavior QtDecoder::sourceRegionBehavior(const std::string &path) const
{
    const QString ext =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).suffix().toLower();
    // Qt's JPEG handler has an evidence-backed clipped scanline path. It may
    // walk entropy data before the ROI, but it does not materialize the whole
    // RGB raster and returns the exact current 8-bit analysis pixels.
    if (ext == "jpg" || ext == "jpeg")
        return mviewer::core::SourceRegionBehavior::BoundedSourcePixels;
#if defined(Q_OS_WIN)
    // The M53 WIC adapter clips before CopyPixels. Rotated TIFF falls through
    // to Qt and therefore cannot make the same memory claim.
    // rotated TIFF regions still use Qt; LOD uses WIC + post-rotation (review P2-5)
    if (ext == "tif" || ext == "tiff")
    {
        QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
        if (reader.transformation() == QImageIOHandler::TransformationNone)
            return mviewer::core::SourceRegionBehavior::BoundedSourcePixels;
    }
#endif
    // PNG/BMP and optional Qt plugins have no proven bounded exact-source
    // region path. Display may still attempt them, but measurement must reject
    // the FullDecodeCrop classification.
    return mviewer::core::SourceRegionBehavior::MayMaterializeFullRaster;
}

namespace
{

ImageData tryBoundedLod(const std::string &path, QImageReader &reader, const QSize &full,
                        const QSize &target, mviewer::domain::ImageMetadata &outMeta)
{
    QImage img;
    if (isBmpPath(path))
        img = mviewer::core::detail::decodeBmpBoxLod(path, target);
    else if (isPngPath(path) && QtDecoder::hasBoundedPngLod())
        img = mviewer::core::detail::decodePngBoxLod(path, target);
    else if (isTiffPath(path))
        img = mviewer::core::detail::decodeTiffStripLod(path, target);
    if (img.isNull())
        return ImageData();
    const QImage::Format sourceFormat = mviewer::core::qtmetadata::readerImageFormat(reader);
    mviewer::core::detail::decoderFillMetadata(reader, sourceFormat, img, outMeta);
    outMeta.width = full.width();
    outMeta.height = full.height();
    return toImageData(img);
}

} // namespace

ImageData QtDecoder::decodeLod(const std::string &path, int maxEdge,
                               mviewer::domain::ImageMetadata &outMeta) const
{
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(true);
    const QSize full = mvcore::readerRasterSize(reader);
    if (!full.isValid() || full.isEmpty())
        return ImageData();
    if (outMeta.filePath.empty())
        outMeta.filePath = path;
    outMeta.fileSize =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).size();

#if defined(Q_OS_WIN)
    // reader.size() is the stored (unrotated) raster. Scale that rect in WIC,
    // then apply EXIF orientation to the small image. Region decode stays on
    // Qt: rotated TIFF regions still use Qt; LOD uses WIC + post-rotation (review P2-5).
    if (isTiffPath(path))
    {
        const int sourceEdge = std::max(full.width(), full.height());
        const int requestedEdge = std::min(sourceEdge, std::max(1, maxEdge));
        const double ratio = static_cast<double>(requestedEdge) / sourceEdge;
        const QSize target(std::max(1, static_cast<int>(std::lround(full.width() * ratio))),
                           std::max(1, static_cast<int>(std::lround(full.height() * ratio))));
        QImage img = decodeTiffWic(path, QRect(0, 0, full.width(), full.height()), target, outMeta);
        if (img.isNull())
            return ImageData();
        const auto transform = reader.transformation();
        img = applyQtTransformation(img, transform);
        // Match the non-WIC autoTransform path below: report the displayed
        // (post-orientation) source size, not the stored or scaled size.
        outMeta.width = full.width();
        outMeta.height = full.height();
        if (transformSwapsDimensions(transform))
            std::swap(outMeta.width, outMeta.height);
        outMeta.orientation = orientationFromTransform(transform);
        return toImageData(img);
    }
#endif

    // JPEG fast path (review P1-05): libjpeg-turbo writes RGB24 / Greyscale8
    // straight into ImageData with a SIMD IDCT and can emit 1/2, 1/4 and 1/8
    // scale natively, so the LOD request costs no second rescale. Qt remains the
    // fallback for CMYK/YCCK JPEGs, ICC-bearing files, and decode failures.
    if (mviewer::core::detail::hasTurboJpeg() && isJpegPath(path))
    {
        const QImage::Format sourceFormat = mviewer::core::qtmetadata::readerImageFormat(reader);
        const ImageData fast = jpegFastTail(path, maxEdge, reader, full, sourceFormat, outMeta);
        if (!fast.isNull())
            return fast;
    }

    if (full.width() > maxEdge || full.height() > maxEdge)
    {
        const double ratio = static_cast<double>(maxEdge) / std::max(full.width(), full.height());
        // Same rule as the WIC TIFF target above: lround, then clamp each side
        // to at least 1. Truncating a very wide, short frame asks Qt for height 0.
        const QSize target(std::max(1, static_cast<int>(std::lround(full.width() * ratio))),
                           std::max(1, static_cast<int>(std::lround(full.height() * ratio))));
        ImageData bounded = tryBoundedLod(path, reader, full, target, outMeta);
        if (!bounded.isNull())
            return bounded;
        reader.setScaledSize(target);
    }
    outMeta.lodFellBack = isBmpPath(path) || isPngPath(path) || isTiffPath(path);
    const QImage::Format sourceFormat = mviewer::core::qtmetadata::readerImageFormat(reader);
    const QImage img = reader.read();
    if (img.isNull())
        return ImageData();
    const int sourceWidth = full.width();
    const int sourceHeight = full.height();
    mviewer::core::detail::decoderFillMetadata(reader, sourceFormat, img, outMeta);
    // QImageReader::read() returns the scaled image, so restore the source
    // dimensions for presentation metadata. EXIF auto-transform rotates the
    // displayed geometry and must be reflected in the reported aspect.
    outMeta.width = sourceWidth;
    outMeta.height = sourceHeight;
    const auto transform = reader.transformation();
    if (transformSwapsDimensions(transform))
        std::swap(outMeta.width, outMeta.height);
    return toImageData(img);
}

ImageData QtDecoder::decodeRegion(const std::string &path, int x, int y, int w, int h, int targetW,
                                  int targetH, mviewer::domain::ImageMetadata &meta) const
{
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(true);
    const QSize full = mvcore::readerRasterSize(reader);
    if (!full.isValid() || full.isEmpty() || w <= 0 || h <= 0)
        return ImageData();

#if defined(Q_OS_WIN)
    // rotated TIFF regions still use Qt; LOD uses WIC + post-rotation (review P2-5)
    if (isTiffPath(path) && reader.transformation() == QImageIOHandler::TransformationNone)
    {
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min(full.width(), x + w);
        const int y1 = std::min(full.height(), y + h);
        if (x1 <= x0 || y1 <= y0 || targetW <= 0 || targetH <= 0)
            return ImageData();
        const QImage img =
            decodeTiffWic(path, QRect(x0, y0, x1 - x0, y1 - y0), QSize(targetW, targetH), meta);
        return toImageData(img);
    }
#endif

    // The clip rect is in source (pre-transform) coordinates per Qt semantics.
    // Windows WIC TIFF path above is unchanged.
    const auto transform = reader.transformation();
    reader.setClipRect(QRect(x, y, w, h));
    if (targetW > 0 && targetH > 0)
    {
        QSize scaled(targetW, targetH);
        if (transformSwapsDimensions(transform))
            scaled = QSize(targetH, targetW);
        reader.setScaledSize(scaled);
    }
    const QImage::Format sourceFormat = mviewer::core::qtmetadata::readerImageFormat(reader);
    const QImage img = reader.read();
    if (img.isNull())
        return ImageData();
    if (meta.filePath.empty())
        meta.filePath = path;
    meta.width = full.width();
    meta.height = full.height();
    if (transformSwapsDimensions(transform))
        std::swap(meta.width, meta.height);
    meta.orientation = orientationFromTransform(transform);
    meta.format = mviewer::core::detail::decoderFormatName(reader);
    mviewer::core::qtmetadata::applyReaderRasterMetadata(sourceFormat, img, meta);
    return toImageData(img);
}
