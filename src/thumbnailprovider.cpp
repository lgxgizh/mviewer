#include "thumbnailprovider.h"
#include "thumbnail_orient.h"

#include "application/ImageLoadingService.h"
#include "core/image/Decoder.h"
#include "core/image/QtConvert.h"
#include "thumbnailcache.h"

#include "core/image/ExifOrientation.h"
#include "core/image/ImageFormats.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtMetadataSemantics.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QPainter>

namespace
{

QImage loadEmbeddedThumb(const std::vector<uint8_t> &bytes, int &ownOrientation)
{
    ownOrientation = 1;
    QBuffer buffer;
    buffer.setData(reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()));
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "JPEG");
    reader.setAutoTransform(false);
    ownOrientation = mviewer::core::qtmetadata::orientationFromTransform(reader.transformation());
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (!image.isNull())
        return image;
    ownOrientation = 1;
    QImage fallback;
    fallback.loadFromData(reinterpret_cast<const uchar *>(bytes.data()),
                          static_cast<int>(bytes.size()), "JPEG");
    return fallback;
}

QImage orientThumbnail(const QImage &thumb, int orientation)
{
    return orientThumbnailImage(thumb, orientation);
}

// Header-only facts about the source file: raw (stored) size and the main
// IFD orientation. One QImageReader probe serves both the orientation fix-up
// and the display-aspect crop.
struct SourceHeader
{
    QSize raw;
    int orientation = 1;
    QSize display() const
    {
        if (!raw.isValid() || raw.width() <= 0 || raw.height() <= 0)
            return {};
        return (orientation >= 5 && orientation <= 8) ? raw.transposed() : raw;
    }
};

SourceHeader readSourceHeader(const QString &sourcePath)
{
    SourceHeader out;
    QImageReader reader(sourcePath);
    reader.setAutoTransform(false);
    out.raw = reader.size();
    out.orientation = mviewer::core::qtmetadata::orientationFromTransform(reader.transformation());
    return out;
}

QImage maybeOrientEmbedded(const SourceHeader &header, const QImage &thumb, int thumbOwnOrientation)
{
    if (thumb.isNull() || thumbOwnOrientation != 1)
        return thumb;
    const QSize raw = header.raw;
    if (!raw.isValid() || raw.width() <= 0 || raw.height() <= 0)
        return thumb;
    if (!mviewer::core::embeddedThumbFollowsRawAspect(header.orientation, raw.width(), raw.height(),
                                                      thumb.width(), thumb.height()))
        return thumb;
    return orientThumbnail(thumb, header.orientation);
}

} // namespace

QImage ThumbnailProvider::squareFitImage(const QImage &q, int size)
{
    if (q.isNull() || size <= 0)
        return {};
    if (q.width() == size && q.height() == size &&
        (q.format() == QImage::Format_ARGB32 || q.format() == QImage::Format_RGB32))
        return q;

    const bool alreadyFitted =
        (q.width() == size && q.height() <= size) || (q.height() == size && q.width() <= size);
    const QImage scaled =
        alreadyFitted ? q : q.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.width() == size && scaled.height() == size)
    {
        return (scaled.format() == QImage::Format_ARGB32 || scaled.format() == QImage::Format_RGB32)
                   ? scaled
                   : scaled.convertToFormat(QImage::Format_ARGB32);
    }

    QImage pm(size, size, QImage::Format_ARGB32);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    painter.end();
    return pm;
}

ImageData ThumbnailProvider::produce(const std::string &path, int size)
{
    if (path.empty() || size <= 0)
        return {};
    const QString qp = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    QImage cached;
    if (ThumbnailCache::instance().get(qp, size, cached))
        return mvcore::fromQImage(cached);

    // Fast path: attempt embedded EXIF thumbnail extraction for JPEG/TIFF/RAW
    const std::vector<uint8_t> exifThumb =
        mviewer::core::MetadataReader::extractExifThumbnail(path);
    if (!exifThumb.empty())
    {
        int thumbOrientation = 1;
        QImage thumb = loadEmbeddedThumb(exifThumb, thumbOrientation);
        if (!thumb.isNull())
        {
            if (thumb.width() >= 64 && thumb.height() >= 64)
            {
                // Header-only size + transformation. Apply the main IFD
                // orientation only when the thumb itself is unrotated and its
                // aspect still matches the sensor (orientations 5-8).
                const SourceHeader header = readSourceHeader(qp);
                thumb = maybeOrientEmbedded(header, thumb, thumbOrientation);
                // Strip black bars baked into a fixed-aspect EXIF thumb so the
                // gallery thumb carries the photo's true display aspect. RAW
                // headers are not reliable display sizes here; leave RAW thumbs
                // to the existing orientation-only handling.
                if (!mviewer::core::isRawPath(path))
                {
                    const QSize display = header.display();
                    thumb = cropThumbnailToAspect(thumb, display.width(), display.height());
                }
                mviewer::domain::ImageMetadata meta;
                const QImage q = mvcore::toDisplayQImage(mvcore::fromQImage(thumb), meta);
                const QImage fitted = squareFitImage(q.isNull() ? thumb : q, size);
                if (!fitted.isNull())
                {
                    ThumbnailCache::instance().put(qp, size, fitted);
                    return mvcore::fromQImage(fitted);
                }
            }
        }
    }

    mviewer::domain::ImageMetadata meta;
    const ImageData decoded = Decoder::decodeScaled(path, size, meta);
    if (decoded.isNull())
        return {};
    const QImage q = mvcore::toDisplayQImage(decoded, meta);
    if (q.isNull())
        return {};
    const QImage fitted = squareFitImage(q, size);
    if (fitted.isNull())
        return {};
    // Disk-cache store happens on the WORKER (PNG encode/write must never run
    // on the GUI thread).
    ThumbnailCache::instance().put(qp, size, fitted);
    return mvcore::fromQImage(fitted);
}

void ThumbnailProvider::invalidateSource(const std::string &path)
{
    if (path.empty())
        return;
    ThumbnailCache::instance().invalidatePath(
        QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    mviewer::application::ImageLoadingService::instance().invalidateSource(path);
}
