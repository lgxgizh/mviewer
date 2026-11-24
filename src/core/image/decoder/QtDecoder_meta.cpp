// QtDecoder metadata helpers, split out of QtDecoder.cpp (complexity gate):
// the container-format name, the raster metadata fill, and the header-only
// probe. See QtDecoder_internal.h.
#include "core/image/decoder/QtDecoder_internal.h"

#include "core/image/decoder/QtDecoder.h"

#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/QtMetadataSemantics.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>

namespace
{
#if defined(MVIEWER_HAS_PNG_LOD)
constexpr bool kHasBoundedPngLod = true;
#else
constexpr bool kHasBoundedPngLod = false;
#endif
} // namespace

namespace mviewer::core::detail
{

std::string decoderFormatName(const QImageReader &reader)
{
    const QByteArray fmt = reader.format();
    if (!fmt.isEmpty())
        return QString::fromLatin1(fmt).toUpper().toStdString();
    const QString ext = QFileInfo(reader.fileName()).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg")
        return "JPEG";
    if (ext == "png")
        return "PNG";
    if (ext == "bmp")
        return "BMP";
    if (ext == "tif" || ext == "tiff")
        return "TIFF";
    return ext.toUpper().toStdString();
}

void decoderFillMetadata(const QImageReader &reader, int sourceFormat, const QImage &img,
                         mviewer::domain::ImageMetadata &meta)
{
    meta.width = img.width();
    meta.height = img.height();
    meta.orientation = mviewer::core::qtmetadata::orientationFromTransform(reader.transformation());
    const QImage::Format format = sourceFormat >= 0 ? static_cast<QImage::Format>(sourceFormat)
                                                    : QImage::Format_Invalid;
    mviewer::core::qtmetadata::applyReaderRasterMetadata(format, img, meta);
    meta.format = decoderFormatName(reader);
}

} // namespace mviewer::core::detail

bool QtDecoder::probeMetadata(const std::string &path, mviewer::domain::ImageMetadata &meta) const
{
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(true);
    const QSize full = mvcore::readerRasterSize(reader);
    if (!full.isValid() || full.isEmpty())
        return false;
    const QFileInfo info(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    if (meta.filePath.empty())
        meta.filePath = path;
    meta.fileName = info.fileName().toUtf8().toStdString();
    meta.fileSize = info.size();
    meta.modifiedEpochSec = info.lastModified().toSecsSinceEpoch();
    meta.width = full.width();
    meta.height = full.height();
    // Report the DISPLAYED geometry (EXIF applied), matching the decodeFull /
    // decodeLod metadata semantics.
    const auto t = reader.transformation();
    if (mviewer::core::qtmetadata::transformSwapsDimensions(t))
        std::swap(meta.width, meta.height);
    meta.orientation = mviewer::core::qtmetadata::orientationFromTransform(t);
    meta.format = mviewer::core::detail::decoderFormatName(reader);

    // Header-only probe: never 1x1-decode (that still walks the full JPEG/PNG
    // decodeLod/decodeFull via fillMetadata).
    const QImage::Format sourceFormat = mviewer::core::qtmetadata::readerImageFormat(reader);
    if (sourceFormat != QImage::Format_Invalid)
    {
        const QImage probe(1, 1, sourceFormat);
        meta.channels = mviewer::core::qtmetadata::channelsForFormat(sourceFormat, probe);
        meta.bitDepth = mviewer::core::qtmetadata::bitsPerChannel(sourceFormat, meta.channels);
    }
    const QString ext = info.suffix().toLower();
    if (ext == "jpg" || ext == "jpeg" || ext == "jpe")
    {
        const std::vector<uint8_t> icc = mviewer::core::MetadataReader::extractJpegIccProfile(path);
        if (!icc.empty())
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            const QByteArray bytes(reinterpret_cast<const char *>(icc.data()),
                                   static_cast<qsizetype>(icc.size()));
            mviewer::core::qtmetadata::applyIccProfileBytes(bytes, meta);
        }
        if (meta.channels == 0)
            meta.channels = 3;
        if (meta.bitDepth == 0)
            meta.bitDepth = 8;
    }
    return true;
}

bool QtDecoder::hasBoundedPngLod()
{
    return kHasBoundedPngLod;
}
